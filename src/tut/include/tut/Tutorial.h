#pragma once

#include <vector>
#include <deque>
#include <stdint.h>

#include "odb/db.h"

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

    void init();

    void test();
    void tetris();
    void shuffle();
    void disturb();

  private:
    struct DebugData {
        int max_deque_size;
        int max_row_iter;
        int max_site_iter;

        int row_iter;
        int site_iter; 
        int cell_iter;

        int max_site_iter_site;
        std::vector<int> max_site_iter_last_placed_site;
        std::string max_site_iter_cell;
    };

    // methods
    int try_to_place_in_row(
        odb::dbRow* row, odb::dbInst* cell,
        int target_x,
        std::vector<odb::dbInst*> const& fixed_cells,
        std::deque<odb::dbInst*> const& last_placed
    );

    double dbu_to_microns(int64_t dbu);
    double microns_to_dbu(double microns);

    std::pair<double, double> xy_dbu_to_microns(int x, int y);
    std::pair<int, int> xy_microns_to_dbu(double x, double y);

    int get_width(odb::dbInst* cell);
    int get_height(odb::dbInst* cell);

    std::pair<int, int> get_pos(odb::dbInst* cell);
    void set_pos(odb::dbInst* cell, int x, int y);

    int row_to_y(odb::dbRow* row);
    int x_to_site(odb::dbRow* row, int x);

    bool collide(int pos1, int pos2, int dimens1, int dimens2);

    // attributes
    odb::dbDatabase* db;
    utl::Logger* logger;
    odb::dbBlock* block;

    DebugData debug_data;
  };
}
