#pragma once

#include "odb/db.h"
#include "ord/OpenRoad.hh"

// note: both (get/set)(Location/Origin) operate at block coordinate system

namespace odb {
  class dbDatabase;
  class dbInst;
}

namespace utl {
  class Logger;
}

namespace misc {
    using namespace odb;

    class Misc {
    public:
        Misc();
        ~Misc();

        dbBlock* get_block();
        const char* error_message_from_get_block();

        void shuffle();
        void shuffle(int x1, int y1, int x2, int y2);
        void disturb();

        // note: this function may cause crashes because the dbInst::destroy function invalidates the pointer, which could be stored in one of the attributes
        void destroy_cells_with_name_prefix(std::string prefix);

        // atributes
        odb::dbDatabase* db;
        utl::Logger* logger;
    };
}
