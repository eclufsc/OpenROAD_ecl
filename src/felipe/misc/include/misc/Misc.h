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

        std::vector<int> get_free_spaces(int x1, int y1, int x2, int y2);

        void shuffle();
        void shuffle_to(int x1, int y1, int x2, int y2);
        void shuffle_in(int x1, int y1, int x2, int y2);
        void disturb();

        bool collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max);

        bool translate(std::string cell_name, int delta_x, int delta_y);

        // note: this function may cause crashes because the dbInst::destroy function invalidates the pointer, which could be stored in one of the attributes
        void destroy_cells_with_name_prefix(std::string prefix);

        // atributes
        odb::dbDatabase* db;
        utl::Logger* logger;

        struct SavedState {
            std::vector<std::pair<odb::Rect, odb::dbInst*>> pos;
            std::set<odb::dbInst*> cells_legalized;
        };

        // attributes
        SavedState saved_state;

        void save_state();
        void load_state();
        void save_pos_to_file(std::string path);
        void load_pos_from_file(std::string path);
    };
}
