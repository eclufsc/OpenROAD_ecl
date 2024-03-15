#pragma once

namespace odb {
  class dbDatabase;
  class dbNet;
  class dbInst; //maybe not needed
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

namespace tut {

class Tutorial {
  public:
    Tutorial();
    ~Tutorial();

    //Print Hello World
    void printHello();

    stt::Tree buildSteinerTree(odb::dbNet * net);

    //Print all cell names
    void printCells();

    //Print all net names
    void printNets();

    //Print all pin names
    void printPins();

    //Traverse all nets printing the total HPWL
    void printHPWLs();

    int calc_HPWL(odb::dbNet* net);

  private:
    odb::dbDatabase* db_;
    utl::Logger* logger_;
    grt::GlobalRouter *grt_;
    //odb::dbBlock* block_;
    stt::SteinerTreeBuilder *stt_;
  };
}

