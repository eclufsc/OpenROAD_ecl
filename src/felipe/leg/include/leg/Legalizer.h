#pragma once

#include <vector>
#include <deque>
#include <set>
#include <stdint.h>
#include <tuple>
#include <chrono>

#include "odb/db.h"

namespace odb {
  class dbDatabase;
  class dbInst;
}

namespace utl {
  class Logger;
}

namespace leg {

class Legalizer {
    using Cell = std::pair<odb::Rect, odb::dbInst*>;
    using Row = std::pair<odb::Rect, int>;
    using Split = std::pair<int, int>;

public:
    // methods
    Legalizer();
    ~Legalizer();

    std::pair<bool, std::string> is_legalized();
    std::pair<bool, std::string> is_legalized(int x1, int y1, int x2, int y2);
    // exclude from verification the cells that are in the border, colliding by less than half of its dimension in at least one of the axis
    std::pair<bool, std::string> is_legalized_excluding_border(int x1, int y1, int x2, int y2);

    void tetris();
    void tetris(
        int area_x1, int area_y1, int area_x2, int area_y2,
        bool include_boundary = true
    );
    void tetris(
        std::vector<Row>&& rows,
        std::vector<std::vector<Split>>&& splits_per_row,
        std::vector<Cell>&& cells
    );

    void dump_lowest_costs(std::string file_path);
    bool translate(std::string cell, int delta_x, int delta_y);

    std::pair<int, int> xy_microns_to_dbu(double x, double y);
    int64_t microns_to_dbu(double microns);

    void abacus_artur(int x1, int y1, int x2, int y2);

    void abacus();
    void abacus(int x1, int y1, int x2, int y2, bool include_boundary = true);
    void abacus(
        std::vector<Row>&& rows,
        std::vector<std::vector<Split>>&& splits_per_row,
        std::vector<Cell>&& cells
    );

    void save_state();
    void load_state();
    void save_pos_to_file(std::string path);
    void load_pos_from_file(std::string path);

    // todo: delete
    void show_legalized_vector();

    // attributes
    utl::Logger* logger;

private:
    std::pair<bool, std::string> is_legalized(
        std::vector<std::pair<odb::Rect, int>> rows_and_sites,
        std::vector<std::pair<odb::Rect, odb::dbInst*>> const& cells
    );

    // methods
    const char* error_message_from_get_block();
    odb::dbBlock* get_block();

    int tetris_try_to_place_in_row(
        odb::Rect const& row, int site_width,
        odb::Rect const& cell, int target_x,
        std::deque<odb::Rect> const& last_placed
    );

    std::pair<odb::Rect, int> dummy_row_and_site(int y_min);

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
    
    double dbu_to_microns(int64_t dbu);

    std::pair<double, double> xy_dbu_to_microns(int x, int y);

    void set_pos(odb::dbInst* cell, int x, int y, bool legalizing);

    int row_to_y(odb::dbRow* row);

    bool collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max);

    auto sort_and_get_splits(
        std::vector<Row>* rows,
        std::vector<odb::Rect> const& fixed_cells
    ) -> std::vector<std::vector<Split>>;

    auto get_sorted_rows_splits_and_cells()
        -> std::tuple<std::vector<Row>, std::vector<std::vector<Split>>, std::vector<Cell>>;

    auto get_sorted_rows_splits_and_cells(
        int x1, int y1, int x2, int y2, bool include_boundary
    ) -> std::tuple<std::vector<Row>, std::vector<std::vector<Split>>, std::vector<Cell>>;

    // attributes
    odb::dbDatabase* db;

    std::vector<std::pair<double, odb::dbInst*>> last_costs;
    std::set<odb::dbInst*> cells_legalized;

    struct SavedState {
        std::vector<std::pair<odb::Rect, odb::dbInst*>> pos;
        std::set<odb::dbInst*> cells_legalized;
    };

    SavedState saved_state;

    // todo: delete
    std::chrono::time_point<std::chrono::high_resolution_clock> test_start;
    std::chrono::time_point<std::chrono::high_resolution_clock> test_end;
    unsigned test_count;
    unsigned recursion_count;
    int cell_index;
    std::vector<int> clust_size;
};
}

