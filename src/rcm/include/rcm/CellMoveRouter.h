#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/graph/grid_graph.hpp>

#include "grt/PinGridLocation.h"
#include "rcm/Abacus.h"
#include "grt/GRoute.h"
#include "sta/Delay.hh"

namespace odb {
  class dbDatabase;
  class dbNet;
  class dbInst;
  class Rect;
  class Point;
  class dbRow;
}

namespace utl {
  class Logger;
}

namespace grt {
  class GlobalRouter;
  class IncrementalGRoute;
  struct GSegment;
}

namespace stt {
class SteinerTreeBuilder;
class Tree;
}

namespace sta {
class dbSta;
class dbNetwork;
class LibertyCell;
class Pin;
}

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;
typedef bg::model::point<int64_t, 2, bg::cs::cartesian> point_t;

namespace rcm {
using median = std::pair<int, int>;

class RectangleRender;
class Abacus;

struct RcmCell {
  odb::dbInst* inst;
  odb::dbITerm* iterm;
  double weight;
  int distance_to_mediana;
  int init_stwl;
  median mediana;
  bool operator<(const RcmCell& c2) const {
    if (init_stwl != c2.init_stwl)
        return init_stwl < c2.init_stwl;
    return inst < c2.inst;
  }
  bool operator>(const RcmCell& c2) const {
    return c2 < *this;
  }
};

struct TimeSubPath {
  odb::dbITerm* driver;
  odb::dbITerm* sink;
  double weight;
  int wl;
  bool operator<(const TimeSubPath& c2) const {
    if (wl != c2.wl)
        return wl < c2.wl;
    return driver < c2.driver;
  }
  bool operator>(const TimeSubPath& c2) const {
    return c2 < *this;
  }
};

using Node = std::tuple<int, int, int>; // (x, y, layer)

struct NodeData {
    int  x, y, layer;
    int  distance = std::numeric_limits<int>::max(); // initialized as unreachable
    bool visited  = false;
};

struct GraphEdge {
    Node from;
    Node to;
    int  length;
    bool is_via;
    bool is_jumper;
    bool is_3d_route;
};

struct RoutingGraph {
    std::map<Node, NodeData>         nodes;     // Node -> its data (includes distance)
    std::vector<GraphEdge>           edges;
    std::map<Node, std::vector<int>> adjacency;
};

class CellMoveRouter {
  private:
    // Define a 2D cartesian point using geometry box of DBUs.
    typedef bg::model::box<point_t> box_t;
    // Define RTree type of DBU box using R-Star algorithm.
    typedef std::pair<box_t, odb::dbInst *> CellElement;
    typedef std::pair<box_t, odb::Rect> GCellElement;
    typedef bgi::rtree<CellElement, bgi::rstar<16>> CRTree;
    typedef bgi::rtree<GCellElement, bgi::rstar<16>> GRTree;

    // Define Median as a pair (x, y)
    using median = std::pair<int, int>;

  public:

    CellMoveRouter();

    void testRevertingRouting();

    void runAbacus();

    void shuffleAbacus() { abacus_.shuffle(); };

    void drawRectangle(int x1, int y1, int x2, int y2);

    void Cell_Move_Rerout();

    bool isClockBuffer(odb::dbInst* cell);

    bool isSequential(odb::dbInst* cell);

    void InitCellsWeight();
    
    void set_debug(bool debug) { debug_ = debug; };

    void report_nets_pins();

    void reportCellSlack();

    void reportWl2SlackRatio(const char* file_name);

    void reportCandidateCells(const char * file_name);

    void moveSpecificCell();

  private:

    void InitCellTree();

    void InitGCellTree();

    void initSta();

    static bool isHorizontal(const grt::GSegment& seg);

    static bool isVertical(const grt::GSegment& seg);

    // Returns true and sets `intersection` if a horizontal and a vertical
    // segment cross each other (including T-junctions and endpoints).
    static bool computeIntersection(const grt::GSegment& h_seg, const grt::GSegment& v_seg, Node& intersection);

    // Returns true if `point` lies anywhere on `seg` (including endpoints).
    static bool isPointOnSegment(const Node& point, const grt::GSegment& seg);

    static void addEdge(RoutingGraph& graph, const Node& from, const Node& to, const grt::GSegment& original_seg);

    static void registerNode(RoutingGraph& graph, const Node& node);

    RoutingGraph buildGraphFromGRoute(odb::dbNet* net);

    void writeGraphToFile(std::map<odb::dbNet*, RoutingGraph>& routing_grpahs, const std::string& filename);

    int getWl2Sink(RoutingGraph& graph, const odb::dbITerm* sink);

    void computeDistancesFromSource(RoutingGraph& graph, const Node& source);

    void populateRoutingGraphs();

    void findCandidatePaths();

    odb::Point findSinkTargetPosition(odb::dbInst* cell, odb::dbITerm* input_pin);

    odb::Point findSourceTargetPosition(odb::dbITerm* output_pin);

    sta::Slack slackWeigthSimple(odb::dbITerm* term);

    sta::Slack slackWeigthExp(odb::dbITerm* term);

    sta::Slack slackWeigthExpSum(odb::dbITerm* term);
  
    bool Swap_and_Rerout(odb::dbInst * moving_cell, int& failed_legalization, int& worse_wl, int before_estimate);

    
    median nets_Bboxes_median(std::vector<int>& Xs, std::vector<int>& Ys);

    median compute_cell_median(odb::dbInst* cell);
    median compute_cells_nets_median(odb::dbInst* cell);

    void sortCellsToMoveMedian();
    std::vector<RcmCell>::iterator findInstIterator(const odb::dbInst* inst);
    std::vector<RcmCell>::iterator findInstIteratorWeight(const odb::dbInst* inst);

    median compute_net_median(odb::dbNet* net);
    int compute_manhattan_distance(median loc1, median loc2);

    int getTreeWl(const stt::Tree &tree);

    stt::Tree buildSteinerTree(odb::dbNet * net);
    stt::Tree buildSteinerTree(odb::dbNet * net, odb::dbITerm* movedTerm, int termX, int termY);
    
    bool debug() {return debug_; };

    std::vector<RcmCell> cells_weight_; //mapa de cells e deltas
    std::vector<std::pair<int, odb::dbNet *>> nets_weight_;
    std::vector<RcmCell> cells_to_move_;
    std::map<sta::Slack, std::set<TimeSubPath>> candidate_paths_;
    int ggrid_max_x_;
    int ggrid_min_x_;
    int ggrid_max_y_;
    int ggrid_min_y_;
    std::string outGraphName_;
    
    odb::dbDatabase* db_;
    utl::Logger* logger_;
    std::unique_ptr<RectangleRender> rectangleRender_;
    grt::GlobalRouter *grt_;
    grt::IncrementalGRoute *icr_grt_;// = grt::IncrementalGRoute(grt_, block);
    std::unique_ptr<CRTree> cellrTree_;
    std::unique_ptr<GRTree> gcellTree_;
    std::map<odb::dbNet*, RoutingGraph> routing_grpahs_;
    bool debug_ = false;
    int candidate_percentage_ = 0.05;
    bool limit_candidate_size_ = false;
    sta::Slack worst_slack_;
    Abacus abacus_;
    stt::SteinerTreeBuilder *stt_ = nullptr;
    sta::dbSta* openSta_ = nullptr;
    sta::dbNetwork* network_ = nullptr;
};
}
