#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "odb/dbTypes.h"
#include "odb/dbTransform.h"

#include <limits>
#include <algorithm>
#include <deque>
#include <fstream>
#include <chrono>
#include <string>

// todo: create function to highlight area in openroad (maybe create a dummy cell and add it to a group?

// todo: is it better for tetris to split rows or check fixed_cells?

// todo: make is_legalized faster

namespace tut {
    using namespace odb;
    using std::vector, std::pair, std::sort, std::lower_bound, std::upper_bound, std::move, std::string, std::make_pair, std::tuple;
    using std::numeric_limits;
    using std::chrono::high_resolution_clock, std::chrono::duration, std::chrono::duration_cast, std::chrono::milliseconds, std::chrono::nanoseconds;

    Tutorial::Tutorial() :
        logger{ord::OpenRoad::openRoad()->getLogger()},
        db{ord::OpenRoad::openRoad()->getDb()}
        {}

    Tutorial::~Tutorial() {}

    dbBlock* Tutorial::get_block() {
        dbChip* chip = db->getChip();
        if (chip) return chip->getBlock();
        else      return 0;
    }

    const char* Tutorial::error_message_from_get_block() {
        return "Block not available";
    }

    // note: both (get/set)(Location/Origin) operate at block coordinate system
    void Tutorial::test() {
        Rect rect(1, 1, 3, 3);
        rect.moveTo(2, 2);
        printf("(%d, %d), (%d, %d)\n", rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax());
    }

    bool Tutorial::move_x(std::string cell_name, int delta_x) {
        dbBlock* block = get_block();
        if (!block) {
            std::string reason = error_message_from_get_block();
            return false;
        }

        dbSet<dbInst> cells = block->getInsts();

        dbInst* cell = 0;
        for (dbInst* curr_cell : cells) {
            if (curr_cell->getName() == cell_name) {
                cell = curr_cell;
            }
        }

        if (!cell) {
            return false;
        }

        int x = cell->getBBox()->xMin();
        int y = cell->getBBox()->yMin();
        set_pos(cell, x + delta_x, y, false);

        return true;
    }

    // note: conditions:
    // fixed cells are already legalized
    // fixed cells can occupy many rows
    // non-fixed cells occupy only one row
    // rows cannot have the same y
    std::pair<bool, std::string> Tutorial::is_legalized() {
        dbBlock* block = get_block();
        if (!block) {
            std::string reason = error_message_from_get_block();
            return {false, reason};
        }

        int x_min = numeric_limits<int>::min();
        int y_min = numeric_limits<int>::min();
        int x_max = numeric_limits<int>::max();
        int y_max = numeric_limits<int>::max();

        return is_legalized(x_min, y_min, x_max, y_max);
    }

    std::pair<bool, std::string> Tutorial::is_legalized(int x1, int y1, int x2, int y2) {
        int area_x_min, area_x_max;
        if (x1 < x2) {
            area_x_min = x1;
            area_x_max = x2;
        } else {
            area_x_min = x2;
            area_x_max = x1;
        }

        int area_y_min, area_y_max;
        if (y1 < y2) {
            area_y_min = y1;
            area_y_max = y2;
        } else {
            area_y_min = y2;
            area_y_max = y1;
        }

        dbBlock* block = get_block();
        if (!block) {
            std::string reason = error_message_from_get_block();
            return {false, reason};
        }

        // rows
        vector<dbRow*> rows;
        dbSet<dbRow> rows_set = block->getRows();
        for (dbRow* row : rows_set) {
            rows.push_back(row);
        }
        std::sort(rows.begin(), rows.end(),
            [&](dbRow* a, dbRow* b) {
                return row_to_y(a) < row_to_y(b);
            }
        );

        vector<int> rows_y_min(rows.size());
        for (size_t i = 0; i < rows.size(); i++) {
            rows_y_min[i] = row_to_y(rows[i]);
        }

        // cells
        vector<Cell> cells_and_insts;
        {
            dbSet<dbInst> cells_set = block->getInsts();
            for (dbInst* cell : cells_set) {
                Rect rect = cell->getBBox()->getBox();
                cells_and_insts.push_back({rect, cell});
            }
            std::sort(cells_and_insts.begin(), cells_and_insts.end(),
                [&](Cell const& a, Cell const& b) {
                    return a.first.xMin() < b.first.xMin();
                }
            );
        }

        vector<dbInst*> block_cells(cells_and_insts.size());
        for (int i = 0; i < cells_and_insts.size(); i++) {
            auto [cell, p_cell] = cells_and_insts[i];
            block_cells[i] = p_cell;
        }

        // algorithm
        vector<vector<dbInst*>> cells_per_row(rows.size());

        // filter cells in area and check row and site
        for (dbInst* cell : block_cells) {
            int x_min = cell->getBBox()->xMin();
            int x_max = cell->getBBox()->xMax();
            int y_min = cell->getBBox()->yMin();
            int y_max = cell->getBBox()->yMax();
            //
            // check if it collides with area
            if (collide(x_min, x_max, area_x_min, area_x_max)
                && collide(y_min, y_max, area_y_min, area_y_max)
            ) {
                int lower_row_i = std::lower_bound(
                        rows_y_min.begin(), rows_y_min.end(), y_min
                    )
                    - rows_y_min.begin();

                if (cell->isFixed()) {
                    int upper_row_i_exc = std::lower_bound(
                            rows_y_min.begin(), rows_y_min.end(), y_max
                        )
                        - rows_y_min.begin();

                    for (int i = lower_row_i; i < upper_row_i_exc; i++) {
                        cells_per_row[i].push_back(cell);
                    }
                } else {
                    // check row
                    if (lower_row_i == rows.size() || rows_y_min[lower_row_i] != y_min) {
                        std::string reason = cell->getName() + " is not aligned with a row";
                        return {false, reason};
                    }

                    dbRow* row = rows[lower_row_i];

                    // check row limits
                    int row_x_min = row->getBBox().xMin();
                    int row_x_max = row->getBBox().xMax();

                    if (!(row_x_min <= x_min && x_max <= row_x_max)) {
                        std::string reason = cell->getName() + " is not totally within a row";
                        return {false, reason};
                    }

                    // check site
                    int site_width = row->getSite()->getWidth();
                    if ((x_min - row_x_min) % site_width != 0) {
                        std::string reason = cell->getName() + " is not aligned with a site";
                        return {false, reason};
                    }

                    cells_per_row[lower_row_i].push_back(cell);
                }
            }
        }

        for (int row_i = 0; row_i < rows.size(); row_i++) {
            std::vector<dbInst*> const& curr_cells = cells_per_row[row_i];
            for (int i = 1; i < curr_cells.size(); i++) {
                dbInst* last_cell = curr_cells[i-1];
                dbInst* cell = curr_cells[i];

                int x1_min = last_cell->getBBox()->xMin();
                int x1_max = last_cell->getBBox()->xMax();
                int x2_min = cell->getBBox()->xMin();
                int x2_max = cell->getBBox()->xMax();

                if (collide(x1_min, x1_max, x2_min, x2_max)) {
                    std::string reason = last_cell->getName() + " is colliding with " + cell->getName();
                    return {false, reason};
                }
            }
        }

        return {true, ""};
    }

    std::pair<bool, std::string> Tutorial::is_legalized_excluding_border(int x1, int y1, int x2, int y2) {
        int area_x_min, area_x_max;
        if (x1 < x2) {
            area_x_min = x1;
            area_x_max = x2;
        } else {
            area_x_min = x2;
            area_x_max = x1;
        }

        int area_y_min, area_y_max;
        if (y1 < y2) {
            area_y_min = y1;
            area_y_max = y2;
        } else {
            area_y_min = y2;
            area_y_max = y1;
        }

        dbBlock* block = get_block();
        if (!block) {
            std::string reason = error_message_from_get_block();
            return {false, reason};
        }

        // rows
        vector<dbRow*> rows;
        dbSet<dbRow> rows_set = block->getRows();
        for (dbRow* row : rows_set) {
            rows.push_back(row);
        }
        std::sort(rows.begin(), rows.end(),
            [&](dbRow* a, dbRow* b) {
                return row_to_y(a) < row_to_y(b);
            }
        );

        vector<int> rows_y_min(rows.size());
        for (size_t i = 0; i < rows.size(); i++) {
            rows_y_min[i] = row_to_y(rows[i]);
        }

        // cells
        vector<dbInst*> block_cells;
        dbSet<dbInst> cells_set = block->getInsts();
        for (dbInst* cell : cells_set) {
            block_cells.push_back(cell);
        }
        std::sort(block_cells.begin(), block_cells.end(),
            [&](dbInst* a, dbInst* b) {
                int x1 = a->getBBox()->xMin();
                int x2 = b->getBBox()->xMin();
                return x1 < x2;
            }
        );

        // algorithm
        vector<vector<dbInst*>> cells_per_row(rows.size());

        // filter cells in area and check row and site
        for (dbInst* cell : block_cells) {
            int x_min = cell->getBBox()->xMin();
            int x_max = cell->getBBox()->xMax();
            int y_min = cell->getBBox()->yMin();
            int y_max = cell->getBBox()->yMax();

            // check if it collides with area

            int half_width = (x_max - x_min)/2;
            int half_height = (y_max - y_min)/2;

            int x2_min = area_x_min + half_width;
            int x2_max = area_x_max - half_width;
            int y2_min = area_y_min + half_height;
            int y2_max = area_y_max - half_height;

            if (collide(x_min, x_max, x2_min, x2_max)
                && collide(y_min, y_max, y2_min, y2_max)
            ) {
                int lower_row_i = std::lower_bound(
                        rows_y_min.begin(), rows_y_min.end(), y_min
                    )
                    - rows_y_min.begin();

                if (cell->isFixed()) {
                    int upper_row_i_exc = std::lower_bound(
                            rows_y_min.begin(), rows_y_min.end(), y_max
                        )
                        - rows_y_min.begin();

                    for (int i = lower_row_i; i < upper_row_i_exc; i++) {
                        cells_per_row[i].push_back(cell);
                    }
                } else {
                    // check row
                    if (lower_row_i == rows.size() || rows_y_min[lower_row_i] != y_min) {
                        std::string reason = cell->getName() + " is not aligned with a row";
                        return {false, reason};
                    }

                    dbRow* row = rows[lower_row_i];

                    // check row limits
                    int row_x_min = row->getBBox().xMin();
                    int row_x_max = row->getBBox().xMax();

                    if (!(row_x_min <= x_min && x_max <= row_x_max)) {
                        std::string reason = cell->getName() + " is not totally within a row";
                        return {false, reason};
                    }

                    // check site
                    int site_width = row->getSite()->getWidth();
                    if ((x_min - row_x_min) % site_width != 0) {
                        std::string reason = cell->getName() + " is not aligned with a site";
                        return {false, reason};
                    }

                    cells_per_row[lower_row_i].push_back(cell);
                }
            }
        }

        for (int row_i = 0; row_i < rows.size(); row_i++) {
            std::vector<dbInst*> const& curr_cells = cells_per_row[row_i];
            for (int i = 1; i < curr_cells.size(); i++) {
                dbInst* last_cell = curr_cells[i-1];
                dbInst* cell = curr_cells[i];

                int x1_min = last_cell->getBBox()->xMin();
                int x1_max = last_cell->getBBox()->xMax();
                int x2_min = cell->getBBox()->xMin();
                int x2_max = cell->getBBox()->xMax();

                if (collide(x1_min, x1_max, x2_min, x2_max)) {
                    std::string reason = last_cell->getName() + " is colliding with " + cell->getName();
                    return {false, reason};
                }
            }
        }

        return {true, ""};
    }

    // todo: this function may cause crashes because the dbInst::destroy function invalidates the pointer, which could be stored in one of the attributes
    void Tutorial::destroy_cells_with_name_prefix(std::string prefix) {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        dbSet<dbInst> cells = block->getInsts();
        for (dbInst* cell : cells) {
            if (cell->getName().find(prefix) == 0) {
                dbInst::destroy(cell);
            }
        }
    }

    void Tutorial::disturb() {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        dbSet<dbInst> insts = block->getInsts();
        for (dbInst* inst : insts) {
            if (inst->isFixed()) continue;

            Rect rect = inst->getBBox()->getBox();

            int new_x = rect.xMin() + (rand() % rect.dx() - rect.dx()/2);
            int new_y = rect.yMin() + (rand() % rect.dy() - rect.dy()/2);

            set_pos(inst, new_x, new_y, false);
        }
    }

    void Tutorial::shuffle() {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        Rect core = block->getCoreArea();
        shuffle(core.xMin(), core.yMin(), core.xMax(), core.yMax());
    }

    void Tutorial::shuffle(int x1, int y1, int x2, int y2) {
        int x_min, x_max, y_min, y_max;
        if (x1 < x2) {
            x_min = x1;
            x_max = x2;
        } else {
            x_min = x2;
            x_max = x1;
        }
        if (y1 < y2) {
            y_min = y1;
            y_max = y2;
        } else {
            y_min = y2;
            y_max = y1;
        }
        int dx = x_max - x_min;
        int dy = y_max - y_min;

        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        dbSet<dbInst> insts = block->getInsts();
        for (dbInst* inst : insts) {
            if (inst->isFixed()) continue;

            Rect cell = inst->getBBox()->getBox();

            int new_x = x_min + rand() % (dx - cell.dx());
            int new_y = y_min + rand() % (dy - cell.dy());

            set_pos(inst, new_x, new_y, false);
        }
    }

    int Tutorial::max_clusters = 0;

    void Tutorial::abacus() {
        auto [rows, splits_per_rows, cells] = get_sorted_rows_splits_and_cells();
        abacus(move(rows), move(splits_per_rows), move(cells));
    }

    void Tutorial::abacus(int x1, int y1, int x2, int y2, bool include_boundary) {
        auto [rows, splits_per_rows, cells] =
            get_sorted_rows_splits_and_cells(
                x1, y1, x2, y2, include_boundary
            );
        abacus(move(rows), move(splits_per_rows), move(cells));
    }

    // todo: change && to const& for consistency (and because rows and cells should not be changed in this function
    // todo: check loop_x, loop_y and evaluate and change clusters and cells_per_row to consider split (solution: create new vector for row indices)
    void Tutorial::abacus(
        vector<Row>&& rows,
        vector<vector<Split>>&& splits_per_row,
        vector<Cell>&& cells
    ) {
        // todo: delete
        sort(cells.begin(), cells.end(),
            [&](Cell const& a, Cell const& b) {
                return a.first.xMin() < b.first.xMin();
            }
        );

        // todo: delete
        setbuf(stdout, 0);

        last_costs.clear();

        test_count = 0;
        // todo: delete
        int total_recursion_count = 0;

        // todo: delete
        auto start = high_resolution_clock::now();

        // algorithm
        vector<int> row_to_start_split(rows.size());
        int total_splits = 0;
        for (int row_i = 0; row_i < rows.size(); row_i++) {
            row_to_start_split[row_i] = total_splits;
            total_splits += splits_per_row[row_i].size();
        }

        vector<vector<int>> cells_per_accum_split(total_splits);
        vector<vector<AbacusCluster>> clusters_per_accum_split(total_splits);

        // todo: delete
        int last_percentage = 0;

        // todo: delete
        int max_row_iter = 0;

        int fail_counter = 0;

        clust_size.resize(100000);

        // todo: delete
        vector<int> recursion_counts(cells.size());

        for (int cell_i = 0; cell_i < cells.size(); cell_i++) {
            cell_index = cell_i;

            auto const& [global_pos, inst] = cells[cell_i];

            double best_cost = std::numeric_limits<double>::max();
            int best_row_i = -1;
            int best_split_i = -1;
            AbacusCluster best_new_cluster;
            int best_previous_i = 0;

            // todo: delete
            bool first_time = true;

            auto evaluate = [&](int row_i, double y_cost, int split_i) {
                auto const& [whole_row, site_width] = rows[row_i];
                auto [x_min, x_max] = splits_per_row[row_i][split_i];
                Rect row(x_min, whole_row.yMin(), x_max, whole_row.yMax());

                double weight = global_pos.dx()*global_pos.dy();
                AbacusCell cell = {cell_i, global_pos, weight};

                AbacusCluster new_cluster;
                int previous_i;

                recursion_count = 0;

                test_start = high_resolution_clock::now();

                int accum_split_i = row_to_start_split[row_i] + split_i;
                if (!abacus_try_add_cell(
                    row, site_width,
                    cell,
                    clusters_per_accum_split[accum_split_i],
                    &new_cluster, &previous_i
                )) {
                    test_end = high_resolution_clock::now();
                    test_count += duration_cast<nanoseconds>(test_end - test_start).count();
                    return;
                }
                test_end = high_resolution_clock::now();
                test_count += duration_cast<nanoseconds>(test_end - test_start).count();

                if (first_time && cell_index < 100000) {
                    clust_size[cell_index] = clusters_per_accum_split[accum_split_i].size();
                }

                int new_x = new_cluster.x + new_cluster.width - global_pos.dx();
                double sqrt_x_cost = new_x - global_pos.xMin();
                double x_cost = sqrt_x_cost*sqrt_x_cost;

                double curr_cost = x_cost + y_cost;

                if (curr_cost < best_cost) {
                    best_cost = curr_cost;
                    best_row_i = row_i;
                    best_split_i = split_i;
                    best_new_cluster = new_cluster;
                    best_previous_i = previous_i;
                }

                if (first_time) {
                    total_recursion_count += recursion_count;
                    first_time = false;
                }
            };

            auto loop_y = [&](int row_i) -> bool {
                Rect const& row_rect = rows[row_i].first;
                double sqrt_y_cost = row_rect.yMin() - global_pos.yMin();
                double y_cost = sqrt_y_cost*sqrt_y_cost;
                if (y_cost > best_cost) return false;

                vector<Split> const& splits = splits_per_row[row_i];

                auto loop_x = [&](int split_i, bool new_row, int comp_x) {
                    if (!new_row) {
                        double sqrt_x_cost = comp_x - global_pos.xMin();
                        double x_cost = sqrt_x_cost*sqrt_x_cost;
                        if (x_cost > best_cost) return false;
                    }

                    evaluate(row_i, y_cost, split_i);

                    return true;
                };

                int approx_split = std::lower_bound(splits.begin(), splits.end(),
                    make_pair(0, global_pos.xMin()),
                    [&](Split const& a, Split const& b) {
                        return a.second < b.second;
                    }
                ) - splits.begin();

                bool new_row = true;
                for (int split_i = approx_split; split_i < splits.size(); split_i++) {
                    int x_min = splits[split_i].first;
                    if (!loop_x(split_i, new_row, x_min)) break;
                    new_row = false;
                }
                for (int split_i = approx_split-1; split_i >= 0; split_i--) {
                    int x_max = splits[split_i].second;
                    if (!loop_x(split_i, new_row, x_max - global_pos.dx())) break;
                    new_row = false;
                }

                return true;
            };

            Rect dummy_rect = Rect(
                0, global_pos.yMin()-1,  // min
                1, global_pos.yMin()     // max
            );
            int approx_row = std::upper_bound(
                rows.begin(), rows.end(), make_pair(dummy_rect, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            ) - rows.begin();

            int row_iter = 0;
            for (int row_i = approx_row; row_i < rows.size(); row_i++) {
                row_iter++;
                if (!loop_y(row_i)) break;;
            }
            for (int row_i = approx_row-1; row_i >= 0; row_i--) {
                row_iter++;
                if (!loop_y(row_i)) break;
            }

            if (row_iter > max_row_iter) max_row_iter = row_iter;

            if (best_row_i == -1) {
                fail_counter++;
                fprintf(stderr, "ERROR: could not place cell\n");
            } else {
                last_costs.emplace_back(dbu_to_microns(sqrt(best_cost)), inst);

                int accum_split_i =
                    row_to_start_split[best_row_i] + best_split_i;
                cells_per_accum_split[accum_split_i]
                    .push_back(cell_i);
                clusters_per_accum_split[accum_split_i]
                    .resize(best_previous_i+1);
                clusters_per_accum_split[accum_split_i]
                    .push_back(best_new_cluster);
            }

            int curr_percentage = ((cell_i+1)*10 / (int)cells.size())*10;
            if (curr_percentage > last_percentage) {
                logger->report(std::to_string(curr_percentage) + "% of the cells processed");
                last_percentage = curr_percentage;
            }

            recursion_counts[cell_i] = recursion_count;
        }

        // todo: delete
        auto end = high_resolution_clock::now();
        logger->report("Time spent (ms): " + std::to_string(duration_cast<milliseconds>(end - start).count()));
        logger->report("Test time (ms): " + std::to_string((int)test_count / 1000000));
        
        // checking whether the cells preserved relative ordering
        {
            int order_changed_n = 0;
            int accum_split_i = 0;
            for (int row_i = 0; row_i < rows.size(); row_i++) {
                int last_cell_i = -1;
                for (
                    int split_i = 0;
                    split_i < splits_per_row[row_i].size();
                    split_i++
                ) {
                    for (int cell_i : cells_per_accum_split[accum_split_i]) {
                        if (cell_i < last_cell_i) {
                            if (order_changed_n == 0) {
                                printf("\nOrder changed:\n");
                                char const* name1 = cells[last_cell_i].second->getName().c_str();
                                char const* name2 = cells[cell_i].second->getName().c_str();
                                printf("%s and %s\n", name1, name2);
                            }
                            order_changed_n++;
                        }
                        last_cell_i = cell_i;
                    }
                    accum_split_i += 1;
                }
            }

            if (order_changed_n) {
                logger->report("Order changed of " + std::to_string(order_changed_n) + " cells");
            }
        }

        {
            int accum_split_i = 0;
            for (int row_i = 0; row_i < rows.size(); row_i++) {
                vector<Split> const& splits = splits_per_row[row_i];
                for (int split_i = 0; split_i < splits.size(); split_i++) {
                    vector<int> const& cells_in_row
                        = cells_per_accum_split[accum_split_i];
                    vector<AbacusCluster> const& clusters
                        = clusters_per_accum_split[accum_split_i];

                    int split_cell_i = 0;
                    for (AbacusCluster const& cluster : clusters) {
                        int x = cluster.x;
                        while (split_cell_i <= cluster.last_cell) {
                            int cell_i = cells_in_row[split_cell_i];
                            auto const& [cell, inst] = cells[cell_i];

                            Rect const& row = rows[row_i].first;

                            set_pos(inst, x, row.yMin(), true);

                            x += cell.dx();
                            split_cell_i++;
                        }
                    }

                    accum_split_i += 1;
                }
            }
        }

        if (fail_counter == 0) {
            logger->report("Placed all cells");
        } else {
            logger->report("Could not place " + std::to_string(fail_counter) + " cells");
        }

        {
            std::ofstream file("/home/felipe/ufsc/eda/temp/debug/recursion_counts.csv");
            for (int rec : recursion_counts) {
                file << rec << "\n";
            }
        }
        {
            std::ofstream file("/home/felipe/ufsc/eda/temp/debug/clust_size.csv");
            for (int c : clust_size) {
                file << c << "\n";
            }
        }

        printf("max_row_iter = %d\n", max_row_iter);
        printf("recursion_count = %d\n", total_recursion_count);
        printf("max_clusters = %d\n", max_clusters);
        printf("rows size = %lu\n", rows.size());
        printf("cells size = %lu\n", cells.size());
        printf("\n");
    }

    bool Tutorial::abacus_try_add_cell(
        Rect row, int site_width,
        AbacusCell const& cell,
        vector<AbacusCluster> const& clusters,
        AbacusCluster* new_cluster, int* previous_i
    ) {
        if (clusters.size() == 0
            || clusters.back().x + clusters.back().width < cell.global_pos.xMin()
        ) {
            // note: last_cell is decremented because it is incremented in the "add cell" code
            int last_cell_dec = -1;
            if (clusters.size() > 0) last_cell_dec = clusters.back().last_cell;

            *new_cluster = {0, 0, 0, cell.global_pos.xMin(), last_cell_dec};
            *previous_i = clusters.size() - 1;
        } else {
            *new_cluster = clusters.back();
            *previous_i = clusters.size() - 2;
        }

        {
            // add cell to cluster
            new_cluster->q      += cell.weight * (cell.global_pos.xMin() - new_cluster->width);
            new_cluster->weight += cell.weight;
            new_cluster->width  += cell.global_pos.dx();
            new_cluster->last_cell++;
        }

        if (!abacus_try_place_and_collapse(
            clusters,
            row, site_width,
            new_cluster, previous_i)
        ) {
            return false;
        }

        if (clusters.size() > max_clusters) max_clusters = clusters.size();

        return true;
    }

    bool Tutorial::abacus_try_place_and_collapse(
        vector<AbacusCluster> const& clusters,
        Rect row, int site_width,
        AbacusCluster* new_cluster, int* previous_i
    ) {
        recursion_count++;

        if (new_cluster->width > row.dx()) return false;

        new_cluster->x = new_cluster->q / new_cluster->weight;
        if ((new_cluster->x - row.xMin()) % site_width != 0) {
            new_cluster->x = (new_cluster->x - row.xMin()) / site_width * site_width + row.xMin();
        }

        new_cluster->x = std::clamp(
            new_cluster->x,
            row.xMin(),
            (row.xMax() - new_cluster->width)
        );

        if (*previous_i >= 0) {
            AbacusCluster const& previous = clusters[*previous_i];
            if (previous.x + previous.width > new_cluster->x) {
                {
                    // collapse cluster
                    new_cluster->q = previous.q + new_cluster->q - new_cluster->weight * previous.width;
                    new_cluster->weight = previous.weight + new_cluster->weight;
                    new_cluster->width  = previous.width  + new_cluster->width;

                    (*previous_i)--;
                }
                if (!abacus_try_place_and_collapse(
                    clusters, row, site_width,
                    new_cluster, previous_i)
                ) {
                    return false;
                }
            }
        }

        return true;
    }

    // todo: add splits_per_row to tetris
    void Tutorial::tetris() {
        auto [rows, splits_per_row, cells] = get_sorted_rows_splits_and_cells();
        tetris(move(rows), move(splits_per_row), move(cells));
    }

    void Tutorial::tetris(int x1, int y1, int x2, int y2, bool include_boundary) {
        auto [rows, splits_per_row, cells] = get_sorted_rows_splits_and_cells(x1, y1, x2, y2, include_boundary);
        tetris(move(rows), move(splits_per_row), move(cells));
    }

    void Tutorial::tetris(
        vector<Row>&& rows_and_sites,
        vector<vector<Split>>&& splits_per_row,
        vector<Cell>&& cells_and_insts
    ) {
        if (cells_and_insts.size() == 0) return;

        last_costs.clear();

        // todo: move this to a struct (or receive in the arguments)
        float left_factor = 1.0; 
        float width_factor = 0.5;
        float x_to_y_priority_ratio = 1.0f;

        auto effective_x = [&](Rect const& cell) -> double {
            // +width -> +priority
            return cell.xMin() - cell.dx() * width_factor;
        };
        std::sort(cells_and_insts.begin(), cells_and_insts.end(), 
            [&](Cell const& cell1, Cell const& cell2){
                return effective_x(cell1.first) < effective_x(cell2.first);
            }
        );

        // todo: delete
        using std::chrono::high_resolution_clock, std::chrono::duration, std::chrono::duration_cast, std::chrono::milliseconds;
        auto start = high_resolution_clock::now();

        int max_width = (*std::max_element(cells_and_insts.begin(), cells_and_insts.end(),
            [&](Cell cell1, Cell cell2) {
                return cell1.first.dx() < cell2.first.dx();
            }
        ))
            .first.dx();

        using std::deque;

        vector<int> row_to_start_split(rows_and_sites.size());
        int total_splits = 0;
        for (int row_i = 0; row_i < rows_and_sites.size(); row_i++) {
            row_to_start_split[row_i] = total_splits;
            total_splits += splits_per_row[row_i].size();
        }

        // note: using deque instead of queue because queue doesnt support iteration
        // todo: it is possible to substitute the queue for a vector with two indexes (WindowVector). All cells_and_insts in the current accum split would be added beforehand. The pop_front would increment the start index; the push_back would increment the end index. I believe this is a better alternative due to its simplicity
        vector<deque<Rect>> last_placed_per_accum_split(total_splits);

        int max_row_iter = 0;
        int max_split_iter = 0;
        int max_last_placed_size = 0;

        double total_row_iter = 0;
        double total_split_iter = 0;
        double total_last_placed_size = 0;

        int not_placed_n = 0;

        // note: cells_and_insts cannot move too much (like tetris). According to the analogy, the left factor determines the fall speed
        int delta_x = -left_factor * max_width;
        int last_percentage = 0;
        for (int cell_i = 0; cell_i < cells_and_insts.size(); cell_i++) {
            Rect& cell = cells_and_insts[cell_i].first;

            int orig_x = cell.xMin();
            int orig_y = cell.yMin();
            int target_x = orig_x + delta_x;
            int target_y = orig_y;

            double lowest_cost = std::numeric_limits<double>::max();
            int winning_row = 0;
            int winning_split = 0;
            int winning_site_x = 0;

            auto loop_y = [&](int row_i) -> bool {
                auto const& [whole_row, site_width] = rows_and_sites[row_i];
                double sqrt_cost_y = whole_row.yMin() - target_y;
                double cost_y = sqrt_cost_y*sqrt_cost_y;
                if (cost_y > lowest_cost) return false;

                vector<Split> const& splits = splits_per_row[row_i];

                int approx_split = std::lower_bound(splits.begin(), splits.end(),
                    make_pair(0, target_x),
                    [&](Split const& a, Split const& b) {
                        return a.second < b.second;
                    }
                ) - splits.begin();

                bool first_split = true;

                int split_iter = 0;
                for (int split_i = approx_split; split_i < splits.size(); split_i++) {
                    split_iter++;

                    auto [x_min, x_max] = splits[split_i];

                    if (!first_split) {
                        double sqrt_x_cost = x_min - target_x;
                        double x_cost = sqrt_x_cost*sqrt_x_cost;
                        if (x_cost > lowest_cost) break;
                    }
                    first_split = false;

                    Rect row(x_min, whole_row.yMin(), x_max, whole_row.yMax());

                    int accum_split_i = row_to_start_split[row_i] + split_i;

                    int site_x = tetris_try_to_place_in_row(
                        row, site_width,
                        cell, target_x,
                        last_placed_per_accum_split[accum_split_i]
                    );

                    if (site_x == row.xMax()) continue;

                    double sqrt_cost_x = site_x - target_x;
                    double cost_x = sqrt_cost_x*sqrt_cost_x;
                    double cost = cost_x + x_to_y_priority_ratio * cost_y;
                    if (cost < lowest_cost) {
                        lowest_cost = cost;
                        winning_row = row_i;
                        winning_split = split_i;
                        winning_site_x = site_x;
                    }

                    break;
                }

                if (split_iter > max_split_iter) max_split_iter = split_iter;
                total_split_iter += split_iter;

                return true;
            };

            Rect dummy_rect = Rect(
                0, target_y-1,  // min
                1, target_y     // max
            );
            int approx_row = std::upper_bound(rows_and_sites.begin(), rows_and_sites.end(), make_pair(dummy_rect, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            )
                - rows_and_sites.begin();

            int row_iter = 0;
            for (int row_i = approx_row; row_i < rows_and_sites.size(); row_i++) {
                row_iter++;
                if (!loop_y(row_i)) break;
            }
            for (int row_i = approx_row-1; row_i >= 0; row_i--) {
                row_iter++;
                if (!loop_y(row_i)) break;
            }

            if (row_iter > max_row_iter) max_row_iter = row_iter;
            total_row_iter += row_iter;

            int curr_percentage = ((cell_i+1)*10 / (int)cells_and_insts.size())*10;
            if (curr_percentage > last_percentage) {
                logger->report(std::to_string(curr_percentage) + "% of the cells processed");
                last_percentage = curr_percentage;
            }

            if (lowest_cost == std::numeric_limits<double>::max()) {
                not_placed_n++;

                fprintf(stderr, "ERROR: could not place cell\n");
                continue;
            }

            last_costs.emplace_back(dbu_to_microns(sqrt(lowest_cost)), cells_and_insts[cell_i].second);

            int new_x = winning_site_x;
            int new_y = rows_and_sites[winning_row].first.yMin();
            cell.moveTo(new_x, new_y);

            int accum_split_i = row_to_start_split[winning_row] + winning_split;
            deque<Rect>* last_placed = &last_placed_per_accum_split[accum_split_i];
            while (true) {
                if (last_placed->size() == 0) break;

                // x2 - left_factor * width2 >= x1 - left_factor * width1
                // x2 >= x1 - left_factor * width1
                // x2 == effective_x(x1)
                int curr_effective_x = orig_x - cell.dx() * width_factor;
                int lower_bound_of_next_x = curr_effective_x;
                int lower_bound_of_next_target_x = lower_bound_of_next_x + delta_x;

                Rect const& placed_cell = last_placed->front();
                int cell_max_x = placed_cell.xMax();

                if (cell_max_x <= lower_bound_of_next_target_x) {
                    last_placed->pop_front();
                } else {
                    break;
                }
            }
            last_placed->push_back(cell);

            if (last_placed->size() > max_last_placed_size) max_last_placed_size = last_placed->size();
            total_last_placed_size += last_placed->size();
        }

        // todo: delete
        auto end = high_resolution_clock::now();
        logger->report("Time spent (ms): " + std::to_string(duration_cast<milliseconds>(end - start).count()));

        for (auto const& [rect, p_cell] : cells_and_insts) {
            set_pos(p_cell, rect.xMin(), rect.yMin(), true);
        }

        if (not_placed_n == 0) {
            logger->report("Placed all cells");
        } else {
            logger->report("Could not place " + std::to_string(not_placed_n) + " cells");
        }

        printf("max_row_iter = %d\n", max_row_iter);
        printf("max_split_iter = %d\n", max_split_iter);
        printf("max_last_placed_size = %d\n", max_last_placed_size);
        printf("average_row_iter = %lf\n", total_row_iter / (int)cells_and_insts.size());
        printf("average_split_iter = %lf\n", total_split_iter / (int)total_row_iter);
        printf("average_last_placed_size = %lf\n", total_last_placed_size / (int)cells_and_insts.size());
        printf("\n");
    }

    int Tutorial::tetris_try_to_place_in_row(
        Rect const& row, int site_width,
        Rect const& cell, int target_x,
        std::deque<Rect> const& last_placed
    ) {
        int site_x = (target_x - row.xMin()) / site_width * site_width + row.xMin();
        if (site_x < row.xMin()) site_x = row.xMin();

        while (true) {
            Rect new_cell(site_x, row.yMin(), site_x + cell.dx(), row.yMax());

            if (new_cell.xMax() > row.xMax()) return row.xMax();

            bool collided = false;

            for (Rect const& other : last_placed) {
                if (collide(new_cell.xMin(), new_cell.xMax(), other.xMin(), other.xMax())) {
                    site_x = other.xMax();
                    collided = true;
                    break;
                }
            }

            if (collided) continue;

            return site_x;
        }
    }

    double Tutorial::dbu_to_microns(int64_t dbu) {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return 0;
        }

        return (double) dbu / block->getDbUnitsPerMicron();
    };

    int64_t Tutorial::microns_to_dbu(double microns) {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return 0;
        }

        return microns * block->getDbUnitsPerMicron();
    };

    std::pair<double, double> Tutorial::xy_dbu_to_microns(int x, int y) {
        return {dbu_to_microns(x), dbu_to_microns(y)};
    };

    std::pair<int, int> Tutorial::xy_microns_to_dbu(double x, double y) {
        return {microns_to_dbu(x), microns_to_dbu(y)};
    };

    void Tutorial::set_pos(dbInst* cell, int x, int y, bool legalizing) {
        if (legalizing) cells_legalized.insert(cell);
        else            cells_legalized.erase(cell);

        cell->setLocation(x, y);
    };

    int Tutorial::row_to_y(dbRow* row) {
        int x, y;
        row->getOrigin(x, y);
        return y;
    };

    bool Tutorial::collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max) {
        return pos1_min < pos2_max && pos2_min < pos1_max;
    };

    auto Tutorial::dummy_row_and_site(int y_min) -> Row {
        int int_max = numeric_limits<int>::max();
        return std::make_pair(Rect(0, y_min, int_max, int_max), 0);
    }

    void Tutorial::save_state() {
        {
            saved_state.pos.clear();

            dbBlock* block = get_block();
            if (!block) {
                std::string reason = error_message_from_get_block();
                return;
            }

            dbSet<dbInst> insts = block->getInsts();

            for (dbInst* inst : insts) {
                if (!inst->isFixed()) {
                    Rect cell = inst->getBBox()->getBox();
                    saved_state.pos.emplace_back(cell, inst);
                }
            }
        }
        saved_state.cells_legalized = cells_legalized;
    }

    void Tutorial::load_state() {
        for (auto const& [cell, inst] : saved_state.pos) {
            set_pos(inst, cell.xMin(), cell.yMin(), false);
        }
        cells_legalized = saved_state.cells_legalized;
    }

    void Tutorial::save_pos_to_file(string path) {
        save_state();

        std::ofstream file(path);
        for (auto const& [cell, inst] : saved_state.pos) {
            file << inst->getName() << " " << cell.xMin() << " " << cell.yMin() << "\n";
        }
    }

    void Tutorial::load_pos_from_file(string path) {
        saved_state.pos.clear();

        std::ifstream file(path);
        if (!file) {
            fprintf(stderr, "File not found\n");
            return;
        }

        vector<pair<string, dbInst*>> names;
        {
            dbBlock* block = get_block();
            if (!block) {
                std::string reason = error_message_from_get_block();
                return;
            }

            dbSet<dbInst> insts = block->getInsts();

            for (dbInst* inst : insts) {
                if (!inst->isFixed()) {
                    names.emplace_back(inst->getName(), inst);
                }
            }
            std::sort(names.begin(), names.end());
        }

        while (true) {
            string name;
            int x, y;
            file >> name >> x >> y;

            if (!file) break;

            pair<string, dbInst*> dummy_pair = std::make_pair(name, (dbInst*)0);
            auto iter = std::lower_bound(names.begin(), names.end(), dummy_pair);

            if (iter != names.end() && iter->first == name) {
                dbInst* inst = iter->second;
                Rect pos = inst->getBBox()->getBox();

                pos.moveTo(x, y);
                saved_state.pos.emplace_back(pos, inst);
            } else {
                fprintf(stderr, "The cell %s does not exist\n", name.c_str());
            }
        }

        load_state();
    }

    void Tutorial::save_costs_to_file(string path) {
        std::ofstream file(path);

        for (auto const& [cost, inst] : last_costs) {
            file << cost << "\n";
        }
    }

    void Tutorial::show_legalized_vector() {
        int count = 0;
        for (dbInst* inst : cells_legalized) {
            logger->report(inst->getName());
            count++;
        }
        logger->report(std::to_string(count) + " cells legalized");
    }

    auto Tutorial::sort_and_get_splits(
        vector<Row>* rows,
        vector<Rect> const& fixed_cells
    ) -> vector<vector<Split>> {
        sort(rows->begin(), rows->end(),
            [&](Row const& a, Row const& b) {
                return a.first.yMin() < b.first.yMin();
            }
        );

        // todo: maybe vector<set<Split>> and when returning converting to vector<vector<Split>> is faster? Because vector::erase is O(n)
        vector<vector<Split>> splits_per_row(rows->size());
        for (int row_i = 0; row_i < rows->size(); row_i++) {
            Rect const& rect = (*rows)[row_i].first;
            splits_per_row[row_i].emplace_back(rect.xMin(), rect.xMax());
        }

        for (Rect const& fixed_cell : fixed_cells) {
            int int_max = numeric_limits<int>::max();

            Rect dummy_row_min = Rect(
                0, fixed_cell.yMin(),
                int_max, int_max
            );
            int row_start = std::lower_bound(rows->begin(), rows->end(),
                make_pair(dummy_row_min, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMin() < b.first.yMin();
                }
            ) - rows->begin();

            Rect dummy_row_max = Rect(
                0, fixed_cell.yMax(),
                int_max, int_max
            );
            int row_end_exc = std::lower_bound(rows->begin(), rows->end(),
                make_pair(dummy_row_max, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMin() < b.first.yMin();
                }
            ) - rows->begin();

            for (int row_i = row_start; row_i < row_end_exc; row_i++) {
                vector<Split>* splits = &splits_per_row[row_i];

                auto split = lower_bound(splits->begin(), splits->end(),
                    make_pair(0, fixed_cell.xMin()),
                    [&](Split const& a, Split const& b) {
                        return a.second < b.second;
                    }
                );
                if (split == splits->end()) continue;

                auto [row_x_min, row_x_max] = *split;
                if (fixed_cell.xMax() <= row_x_min) continue;
                if (row_x_max <= fixed_cell.xMin()) continue;

                splits->erase(split);

                if (fixed_cell.xMax() < row_x_max) {
                    splits->insert(split, make_pair(fixed_cell.xMax(), row_x_max));
                }
                if (row_x_min < fixed_cell.xMin()) {
                    splits->insert(split, make_pair(row_x_min, fixed_cell.xMin()));
                }
            }
        }

        return splits_per_row;
    }

    auto Tutorial::get_sorted_rows_splits_and_cells()
        -> tuple<vector<Row>, vector<vector<Split>>, vector<Cell>>
    {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return {};
        }

        vector<Cell> cells;
        vector<Rect> fixed_cells;
        {
            dbSet<dbInst> cells_set = block->getInsts();
            for (dbInst* inst : cells_set) {
                Rect rect = inst->getBBox()->getBox();
                if (inst->isFixed()
//                    || cells_legalized.find(inst) != cells_legalized.end()
                ) {
                    fixed_cells.push_back(rect);
                    fixed_names.push_back(inst->getName());
                } else {
                    cells.push_back({rect, inst});
                }
            }

            sort(cells.begin(), cells.end(),
                [&](Cell const& a, Cell const& b) {
                    return a.first.xMin() < b.first.xMin();
                }
            );
        }

        vector<Row> rows;
        vector<vector<Split>> splits_per_row;
        {
            dbSet<dbRow> rows_set = block->getRows();
            for (dbRow* row : rows_set) {
                Rect rect = row->getBBox();
                int site_width = row->getSite()->getWidth();
                rows.push_back({rect, site_width});
            }

            splits_per_row = sort_and_get_splits(&rows, fixed_cells);
        }

        return {rows, splits_per_row, cells};
    }

    auto Tutorial::get_sorted_rows_splits_and_cells(int x1, int y1, int x2, int y2, bool include_boundary)
        -> tuple<vector<Row>, vector<vector<Split>>, vector<Cell>>
    {
        int area_x_min, area_x_max, area_y_min, area_y_max;
        if (x1 < x2) {
            area_x_min = x1;
            area_x_max = x2;
        } else {
            area_x_max = x1;
            area_x_min = x2;
        }
        if (y1 < y2) {
            area_y_min = y1;
            area_y_max = y2;
        } else {
            area_y_max = y1;
            area_y_min = y2;
        }

        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return {};
        }

        double cell_total_area = 0;
        vector<Cell> cells;
        vector<Rect> fixed_cells;
        {
            dbSet<dbInst> insts = block->getInsts();
            
            for (dbInst* inst : insts) {
                Rect rect = inst->getBBox()->getBox();

                if (!collide(rect.xMin(), rect.xMax(), area_x_min, area_x_max)
                    || !collide(rect.yMin(), rect.yMax(), area_y_min, area_y_max)) {
                    continue;
                }

                int cx = rect.xMin() + rect.dx()/2;
                int cy = rect.yMin() + rect.dy()/2;

                bool center_inside;
                if (include_boundary) {
                    center_inside = area_x_min <= cx && cx <= area_x_max
                                 && area_y_min <= cy && cy <= area_y_max;
                } else {
                    center_inside = area_x_min < cx && cx < area_x_max
                                 && area_y_min < cy && cy < area_y_max;
                }

                if (center_inside && !inst->isFixed()) {
                    cells.push_back({rect, inst});
                    cell_total_area += rect.dx() * rect.dy();
                } else {
                    if (cells_legalized.find(inst) != cells_legalized.end()) {
                        fixed_cells.push_back(rect);
                    }
                }
            }
        }

        vector<Row> rows;
        vector<vector<Split>> splits_per_row;
        {
            dbSet<dbRow> rows_set = block->getRows();
            for (dbRow* row : rows_set) {
                Rect rect = row->getBBox();
                if (collide(area_x_min, area_x_max, rect.xMin(), rect.xMax())
                    && collide(area_y_min, area_y_max, rect.yMin(), rect.yMax())
                ) {
                    int site_width = row->getSite()->getWidth();

                    int site_x_min = (area_x_min - rect.xMin()) / site_width * site_width + rect.xMin();
                    int site_x_max = (area_x_max - rect.xMin()) / site_width * site_width + rect.xMin();

                    rect.set_xlo(std::max(site_x_min, rect.xMin()));
                    rect.set_xhi(std::min(site_x_max, rect.xMax()));
                    rows.push_back({rect, site_width});
                }
            }

            splits_per_row = sort_and_get_splits(&rows, fixed_cells);
        }

        double row_total_area = 0;
        for (int i = 0; i < rows.size(); i++) {
            Rect const& rect = rows[i].first;
            row_total_area += rect.dx() * rect.dy();
        }
        
        if (cell_total_area > row_total_area) {
            logger->report("Impossible to legalize: cells total area is bigger than rows total area");
        }

        return {rows, splits_per_row, cells};
    }
}

