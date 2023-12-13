#pragma once

namespace odb {
  class dbDatabase;
}

namespace utl {
  class Logger;
}

namespace leg {

class Legalizer {
public:
    Legalizer();
    ~Legalizer();

    void log();

    // attributes
    odb::dbDatabase* db;
    utl::Logger* logger;
};

}

