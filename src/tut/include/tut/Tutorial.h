#pragma once

#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include "ppl/IOPlacer.h"
#include "tut/Partition.h"
#include "odb/db.h"
#include "sta/GraphClass.hh"
#include "sta/NetworkClass.hh"

namespace sta {
class dbSta;
class BfsFwdIterator;
class dbNetwork;
class LibertyPort;
}  // namespace sta

namespace odb {
  class dbDatabase;
  class dbBTerm;
}

namespace utl {
  class Logger;
}

namespace ppl {
class IOPlacer;
} // namespace ppl

namespace tut {

class Layout;

using std::map;
using std::pair;
using std::set;
using std::string;
using std::unordered_map;
using std::vector;

typedef set<Macro*> MacroSet;
// vertex -> fanin macro set
typedef map<sta::Vertex*, MacroSet> VertexFaninMap;
typedef pair<Macro*, Macro*> MacroPair;
// from/to -> weight
// weight = from/pin -> to/pin count
typedef map<MacroPair, int> AdjWeightMap;

enum class CoreEdge
{
  West,
  East,
  North,
  South,
};

constexpr int core_edge_count = 4;
const char* coreEdgeString(CoreEdge edge);
CoreEdge coreEdgeFromIndex(int edge_index);
int coreEdgeIndex(CoreEdge edge);

struct exclude_struct {
      ppl::Edge edge;
      int begin;
      int end;
};

class Macro
{
 public:
  Macro(double _lx, double _ly, double _w, double _h, odb::dbInst* _dbInstPtr);
  Macro(double _lx, double _ly, const Macro& copy_from);
  string name();

  double lx, ly;
  double w, h;
  odb::dbInst* dbInstPtr;
};

class MacroSpacings
{
 public:
  MacroSpacings();
  MacroSpacings(double halo_x,
                double halo_y,
                double channel_x,
                double channel_y);
  void setHalo(double halo_x, double halo_y);
  void setChannel(double channel_x, double channel_y);
  void setChannelX(double channel_x);
  void setChannelY(double channel_y);
  double getHaloX() const { return halo_x_; }
  double getHaloY() const { return halo_y_; }
  double getChannelX() const { return channel_x_; }
  double getChannelY() const { return channel_y_; }
  double getSpacingX() const;
  double getSpacingY() const;

 private:
  double halo_x_, halo_y_, channel_x_, channel_y_;
};

class PinParser {
  public:
    std::vector<odb::dbTechLayer*> horizontal_layers;
    std::vector<odb::dbTechLayer*> vertical_layers;
    std::vector<exclude_struct> exclude_list;
};

class Tutorial {
  public:
    Tutorial();
    ~Tutorial();
    void init(odb::dbDatabase* db, sta::dbSta* sta, utl::Logger* log, ppl::IOPlacer* placer);
    void setDebug(bool partitions);

    void setHalo(double halo_x, double halo_y);
    void setChannel(double channel_x, double channel_y);
    void setVerboseLevel(int verbose);
    void setFenceRegion(double lx, double ly, double ux, double uy);
    void setSnapLayer(odb::dbTechLayer* snap_layer);

    void placeMacrosCornerMinWL();
    void placeMacrosCornerMaxWl();

    void placeMacrosCornerMinWL2();
    void placeMacrosCornerMaxWl2();
    int getSolutionCount();

    // return weighted wire-length to get best solution
    double getWeightedWL();
    int weight(int idx11, int idx12);
    int macroIndex(odb::dbInst* inst);
    MacroSpacings& getSpacings(const Macro& macro);
    double paddedWidth(const Macro& macro);
    double paddedHeight(const Macro& macro);

    Macro& macro(int idx) { return macros_[idx]; }
    size_t macroCount() { return macros_.size(); }

    //Print Hello World
    void printHello();

    //Print all cell names
    void printCells();

    //Print all net names
    void printNets();

    //Print all pin names
    void printPins();

    //Traverse all nets printing the total HPWL
    void printHPWLs();

    void test();
    //Função TCC
    void cyclePlacers();

  private:
    bool findMacros();
    bool isMissingLiberty();

    bool init();
    // Update Macro Location from Partition info
    void updateMacroLocations(Partition& part);
    void updateDbInstLocations();
    void updateBTermsLocations();
    void makeMacroPartMap(const Partition& part,
                          MacroPartMap& macroPartMap) const;
    vector<pair<Partition, Partition>> getPartitions(const Layout& layout,
                                                    const Partition& partition,
                                                    bool isHorizontal);
    void cutRoundUp(const Layout& layout, double& cutLine, bool isHorizontal);
    void setDbInstLocations(Partition& partition);

    // graph based adjacencies
    void findAdjacencies();
    void seedFaninBfs(sta::BfsFwdIterator& bfs, VertexFaninMap& vertex_fanins);
    void findFanins(sta::BfsFwdIterator& bfs, VertexFaninMap& vertex_fanins);
    void copyFaninsAcrossRegisters(sta::BfsFwdIterator& bfs,
                                  VertexFaninMap& vertex_fanins);
    void findAdjWeights(VertexFaninMap& vertex_fanins, AdjWeightMap& adj_map);
    sta::Pin* findSeqOutPin(sta::Instance* inst, sta::LibertyPort* out_port);
    void fillMacroWeights(AdjWeightMap& adj_map);
    CoreEdge findNearestEdge(odb::dbBTerm* bTerm);
    string faninName(Macro* macro);
    int macroIndex(Macro* macro);
    bool macroIndexIsEdge(Macro* macro);
    bool macroIndexIsIO(Macro* macro);
    string macroIndexName(int index);

    void reportEdgePinCounts();

    void resetIoplacer();
    void unlockMacros();
    PinParser parserArguments(string filepath);

    ////////////////////////////////////////////////////////////////

    odb::dbDatabase* db_;
    sta::dbSta* sta_;
    utl::Logger* logger_;
    odb::dbTechLayer* snap_layer_;
    ppl::IOPlacer* pPlacer_;
    bool connection_driven_;
    bool first_time_ = true;

    // macro idx/idx pair -> give each
    vector<vector<int>> macro_weights_;
    // macro Information
    vector<Macro> macros_;
    // bterm Information
    vector<odb::dbBTerm> bterms_;
    // dbInst* --> macros_'s index
    unordered_map<odb::dbInst*, int> macro_inst_map_;

    MacroSpacings default_macro_spacings_;
    unordered_map<odb::dbInst*, MacroSpacings> macro_spacings_;

    double lx_, ly_, ux_, uy_;
    int verbose_;
    int solution_count_;

    bool gui_debug_;
    bool gui_debug_partitions_;
    // Number of register levels to look through for macro adjacency.
    static constexpr int reg_adjacency_depth_ = 3;
  };
  class Layout
  {
  public:
    Layout();
    Layout(double lx, double ly, double ux, double uy);
    Layout(Layout& orig, Partition& part);

    double lx() const { return lx_; }
    double ly() const { return ly_; }
    double ux() const { return ux_; }
    double uy() const { return uy_; }

    void setLx(double lx);
    void setLy(double ly);
    void setUx(double ux);
    void setUy(double uy);

  private:
    double lx_, ly_, ux_, uy_;
  };

}

