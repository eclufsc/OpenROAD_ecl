#pragma once

#include <memory>
#include <vector>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/graph/grid_graph.hpp>

#include "rcm/Abacus.h"
#include "grt/GRoute.h"

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

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;
typedef bg::model::point<int64_t, 2, bg::cs::cartesian> point_t;

namespace rcm {
using median = std::pair<int, int>;

class RectangleRender;
class Abacus;

struct RcmCell {
  odb::dbInst* inst;
  int weight;
  median mediana;
  int distance_to_mediana;
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

    void test_error_cell();

    void helloWorld();

    void drawRectangle(int x1, int y1, int x2, int y2);

    void ShowFirstNetRout();

    void Cell_Move_Rerout();

    void InitCellsWeight();

    void InitNetsWeight();
    
    void set_debug(bool debug) { debug_ = debug; };

    void report_nets_pins();

  private:

    void InitCellTree();

    void InitGCellTree();

    void InitAbacus();
  
    bool Swap_and_Rerout(odb::dbInst * moving_cell, int& failed_legalization, int& worse_wl);

    int getNetHPWLFast(odb::dbNet * net) const;

    void SelectCellsToMove();

    
    median nets_Bboxes_median(std::vector<int>& Xs, std::vector<int>& Ys);

    median compute_cell_median(odb::dbInst* cell);
    median compute_cells_nets_median(odb::dbInst* cell);

    void sortCellsToMoveMedian();
    std::vector<RcmCell>::iterator findInstIterator(const odb::dbInst* inst);

    median compute_net_median(odb::dbNet* net);
    int compute_manhattan_distance(median loc1, median loc2);
    
    bool debug() {return debug_; };

    std::vector<RcmCell> cells_weight_; //mapa de cells e deltas
    std::vector<std::pair<int, odb::dbNet *>> nets_weight_;
    std::vector<RcmCell> cells_to_move_;
    int ggrid_max_x_;
    int ggrid_min_x_;
    int ggrid_max_y_;
    int ggrid_min_y_;
    
    odb::dbDatabase* db_;
    utl::Logger* logger_;
    std::unique_ptr<RectangleRender> rectangleRender_;
    grt::GlobalRouter *grt_;
    grt::IncrementalGRoute *icr_grt_;// = grt::IncrementalGRoute(grt_, block);
    std::unique_ptr<CRTree> cellrTree_;
    std::unique_ptr<GRTree> gcellTree_;
    bool debug_ = false;
    Abacus abacus_;
};
}
