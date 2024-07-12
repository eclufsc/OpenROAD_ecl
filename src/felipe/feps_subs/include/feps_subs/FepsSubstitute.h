#pragma once

namespace odb {
  class dbDatabase;
}

namespace utl {
  class Logger;
}

namespace feps_subs {

class FepsSubstitute {
public:
    FepsSubstitute();
    ~FepsSubstitute();

    void log();

    // attributes
    odb::dbDatabase* db;
    utl::Logger* logger;
};

}

