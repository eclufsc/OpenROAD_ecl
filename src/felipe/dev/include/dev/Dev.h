#pragma once

namespace odb {
  class dbDatabase;
}

namespace utl {
  class Logger;
}

namespace dev {

class Dev {
public:
    Dev();
    ~Dev();

    void log();
    void global_route_and_print_vias();

    // attributes
    odb::dbDatabase* db;
    utl::Logger* logger;
//    MakeWireParasitics parasitics;
};

}

