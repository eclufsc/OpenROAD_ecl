#pragma once

#include <vector>

namespace odb {
  class dbDatabase;
  class dbInst;
}

namespace utl {
  class Logger;
}

namespace tut {

class Tutorial {
  public:
    Tutorial();
    ~Tutorial();

    void test();
    void tetris();
    void shuffle();

  private:
    odb::dbDatabase* db_;
    utl::Logger* logger_;
  };
}

