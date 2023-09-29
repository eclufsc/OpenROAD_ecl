#pragma once

namespace odb {
  class dbDatabase;
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

  private:
    odb::dbDatabase* db_;
    utl::Logger* logger_;
  };
}

