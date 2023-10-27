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
    // methods
    Tutorial();
    ~Tutorial();

    void destroy_cells_with_name_prefix(std::string prefix);

    std::pair<bool, std::string> is_legalized();
    std::pair<bool, std::string> is_legalized(int x1, int y1, int x2, int y2);
    // exclude from verification the cells that are in the border, colliding by less than half of its dimension in at least one of the axis
    std::pair<bool, std::string> is_legalized_excluding_border(int x1, int y1, int x2, int y2);

    void tetris(bool show_progress = false);
    void tetris(int area_x1, int area_y1, int area_x2, int area_y2, bool show_progress);

    void test();
    void dump_lowest_costs(std::string file_path);
    bool move_x(std::string cell_name, int delta_x);

    void shuffle();
    void disturb();
    std::pair<int, int> xy_microns_to_dbu(double x, double y);

    void abacus();
    void abacus(int x1, int y1, int x2, int y2);
    void abacus(
        std::vector<std::pair<odb::Rect, int>> rows_and_sites,
        std::vector<std::pair<odb::Rect, odb::dbInst*>> cells_and_insts
    );

    // attributes
    utl::Logger* logger;
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

        std::vector<double> lowest_costs;
    };

    // methods
    const char* error_message_from_get_block();
    odb::dbBlock* get_block();

    int tetris_try_to_place_in_row(
        odb::dbRow* row, odb::dbInst* cell,
        int target_x,
        std::vector<odb::dbInst*> const& fixed_cells,
        std::deque<odb::dbInst*> const& last_placed
    );

    int tetris_try_to_place_in_row(
        odb::dbRow* row, int row_x_min, int row_x_max,
        odb::dbInst* cell,
        int target_x,
        std::vector<odb::dbInst*> const& fixed_cells,
        std::deque<odb::dbInst*> const& last_placed
    );

    struct AbacusCluster {
        double q;
        double weight;
        int width;
        int x;
        int last_cell;
    };

    struct AbacusCell {
        int id;
        odb::Rect global_pos;
        double weight;
    };

    bool abacus_try_add_cell(
        odb::Rect row, int site_width,
        AbacusCell const& cell,
        std::vector<AbacusCluster> const& clusters,
        AbacusCluster* new_cluster, int* previous_i
    );
    bool abacus_try_place_and_collapse(
        std::vector<AbacusCluster> const& clusters,
        odb::Rect row, int site_width,
        AbacusCluster* new_cluster, int* previous_i
    );

    // todo: delete
    static int max_clusters;
    
    double dbu_to_microns(int64_t dbu);
    double microns_to_dbu(double microns);

    std::pair<double, double> xy_dbu_to_microns(int x, int y);

    int get_width(odb::dbInst* cell);
    int get_height(odb::dbInst* cell);

    void set_pos(odb::dbInst* cell, int x, int y);

    int row_to_y(odb::dbRow* row);
    int x_to_site(odb::dbRow* row, int x);

    bool collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max);

    // attributes
    odb::dbDatabase* db;

    DebugData debug_data;
  };
}

