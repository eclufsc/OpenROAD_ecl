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

// todo: create function to highlight area in openroad (maybe create a dummy cell and add it to a group?

// todo: maybe remove row_to_y(?) and x_to_site
// todo: maybe change collide definition to receive pos1_min, pos1_max, pos2_min, pos2_max

namespace tut {
    using namespace odb;
    using std::vector;

    Tutorial::Tutorial() :
        logger{ord::OpenRoad::openRoad()->getLogger()},
        db{ord::OpenRoad::openRoad()->getDb()},
        debug_data{}
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
        cell->setLocation(x + delta_x, y);

        return true;
    }

    // conditions:
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

        Rect rect = block->getCoreArea();
        int x_min = rect.xMin();
        int y_min = rect.yMin();
        int x_max = rect.xMax();
        int y_max = rect.yMax();

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

    // both (get/set)(Location/Origin) operate at block coordinate system
    void Tutorial::test() {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        dbSet<dbInst> cells = block->getInsts();
        for (dbInst* cell : cells) {
            std::string name = cell->getName();
            const char* compare = "FILL";
            size_t size = strlen(compare);
            if (name.size() >= size && memcmp(name.c_str(), compare, size) == 0) {
                printf("%s\n", name.c_str());
                dbInst::destroy(cell);
            }
        }
    }

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

        dbSet<dbInst> cells = block->getInsts();
        for (dbInst* cell : cells) {
            if (cell->isFixed()) continue;

            int x = cell->getBBox()->xMin();
            int y = cell->getBBox()->yMin();

            int new_x = x + (rand() % get_width(cell) - get_width(cell)/2);
            int new_y = y + (rand() % get_height(cell) - get_height(cell)/2);

            set_pos(cell, new_x, new_y);
        }
    }

    void Tutorial::shuffle() {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        Rect rect = block->getCoreArea();
        int min_x = rect.xMin();
        int max_x = rect.xMax();
        int min_y = rect.yMin();
        int max_y = rect.yMax();

        dbSet<dbInst> cells = block->getInsts();
        for (dbInst* cell : cells) {
            if (cell->isFixed()) continue;

            int x;
            int y;
            
            while (true) {
                x = rand()%(max_x-min_x) + min_x;
                y = rand()%(max_y-min_y) + min_y;

                if (x + get_width(cell) <= max_x
                    && y + get_height(cell) <= max_y
                ) {
                    break;
                }
            }

            cell->setLocation(x, y);
        }
    }

    void Tutorial::dump_lowest_costs(std::string file_path) {
        std::ofstream file(file_path);

        file << debug_data.lowest_costs.size() << "\n";
        for (double cost : debug_data.lowest_costs) {
            if (abs(cost) < 1e-10) cost = 0;
            file << cost << " ";
        }
    }

    void Tutorial::tetris(bool show_progress) {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        using std::vector;
        using std::deque;

        // separate fixed from non-fixed
        vector<dbInst*> fixed_cells;
        vector<dbInst*> cells;
        dbSet<dbInst> cells_set = block->getInsts();
        for (dbInst* cell : cells_set) {
            if (cell->isFixed()) fixed_cells.push_back(cell);
            else                 cells.push_back(cell);
        }

        // todo: move this to a struct (or receive in the arguments)
        float left_factor = 1.0; 
        float width_factor = 0.5;
        float x_to_y_priority_ratio = 1.0f;

        // sort cells by x
        auto effective_x = [&](dbInst* cell){
            // +width -> +priority
            int x = cell->getBBox()->xMin();
            return x - get_width(cell) * width_factor;
        };
        std::sort(cells.begin(), cells.end(), 
                [&](dbInst* cell1, dbInst* cell2){
                return effective_x(cell1) < effective_x(cell2);
                }
                );

        // sort rows by y
        vector<dbRow*> rows;
        dbSet<dbRow> rows_set = block->getRows();
        for (dbRow* row : rows_set) {
            rows.push_back(row);
        }
        std::sort(rows.begin(), rows.end(),
                [&](dbRow* row1, dbRow* row2) {
                return row_to_y(row1) < row_to_y(row2);
                }
                );

        vector<int> rows_y(rows.size());
        for (int i = 0; i < rows.size(); i++) {
            rows_y[i] = row_to_y(rows[i]);
        }

        // max_width
        int max_width = get_width(*std::max_element(cells.begin(), cells.end(),
                    [&](dbInst* cell1, dbInst* cell2) {
                    return get_width(cell1) < get_width(cell2);
                    }
                    ));

        // initialize last_placed_per_row and fixed_per_row
        // note: using deque instead of queue because queue doesnt support iteration
        // note: it is possible to substitute the queue for a vector with two indexes (WindowVector). All cells in the current row would be added beforehand. The pop_front would increment the start index; the push_back would increment the end index. I believe this is a better alternative due to its simplicity. Maybe it's possible to merge last_placed_per_row and fixed_per_row in a single data structure
        vector<deque<dbInst*>> last_placed_per_row(rows.size());
        vector<vector<dbInst*>> fixed_per_row(rows.size());
        for (dbInst* fixed : fixed_cells) {
            int y_min = fixed->getBBox()->yMin();
            int y_max = fixed->getBBox()->yMax();

            int lower_bound_row_i = std::lower_bound(
                    rows_y.begin(), rows_y.end(), y_min
                    )
                - rows_y.begin();

            int upper_bound_row_i = std::lower_bound(
                    rows_y.begin(), rows_y.end(), y_max
                    )
                - rows_y.begin();

            for (int i = lower_bound_row_i; i < upper_bound_row_i; i++) {
                fixed_per_row[i].push_back(fixed);
            }
        }

        // debug_data
        debug_data.max_deque_size = 0;
        debug_data.max_row_iter = 0;
        debug_data.max_site_iter = 0;

        debug_data.cell_iter = 0;

        debug_data.max_site_iter_site = 0;
        debug_data.max_site_iter_last_placed_site = vector<int>();
        debug_data.max_site_iter_cell.clear();

        debug_data.lowest_costs.resize(cells.size());

        // note: cells cannot move too much (like tetris). The left factor determines the fall speed
        int delta_x = -left_factor * max_width;
        int last_percentage = 0;
        for (int i = 0; i < cells.size(); i++) {
            dbInst* cell = cells[i];

            debug_data.cell_iter++;
            debug_data.row_iter = 0;

            int orig_x = cell->getBBox()->xMin();
            int orig_y = cell->getBBox()->yMin();
            int target_x = orig_x + delta_x;
            int target_y = orig_y;

            double lowest_cost = std::numeric_limits<double>::max();

            int winning_row = 0;
            int winning_site_x = 0;

            int approx_row;
            auto iter = std::lower_bound(rows_y.begin(), rows_y.end(), target_y);
            if (iter == rows_y.end()) approx_row = rows.size()-1;
            else                      approx_row = iter - rows_y.begin();

            for (int row_i = approx_row; row_i < rows.size(); row_i++) {
                dbRow* row = rows[row_i];

                int sqrt_cost_y = row_to_y(row) - target_y;
                int cost_y = sqrt_cost_y*sqrt_cost_y;

                if (cost_y > lowest_cost) break;

                int site_x = try_to_place_in_row(
                        row, cell,
                        target_x,
                        fixed_per_row[row_i],
                        last_placed_per_row[row_i]
                        );

                if (site_x == row->getBBox().xMax()) continue;

                int sqrt_cost_x = site_x - target_x;
                int cost_x = sqrt_cost_x*sqrt_cost_x;
                double cost = cost_x
                    + x_to_y_priority_ratio * cost_y;
                if (cost < lowest_cost) {
                    lowest_cost = cost;
                    winning_row = row_i;
                    winning_site_x = site_x;
                }
            }
            for (int row_i = approx_row-1; row_i >= 0; row_i--) {
                dbRow* row = rows[row_i];

                int sqrt_cost_y = row_to_y(row) - target_y;
                int cost_y = sqrt_cost_y*sqrt_cost_y;

                if (cost_y > lowest_cost) break;

                int site_x = try_to_place_in_row(
                        row, cell,
                        target_x,
                        fixed_per_row[row_i],
                        last_placed_per_row[row_i]
                        );

                if (site_x == row->getBBox().xMax()) continue;

                int sqrt_cost_x = site_x - target_x;
                int cost_x = sqrt_cost_x*sqrt_cost_x;
                double cost = cost_x
                    + x_to_y_priority_ratio * cost_y;
                if (cost < lowest_cost) {
                    lowest_cost = cost;
                    winning_row = row_i;
                    winning_site_x = site_x;
                }
            }

            int curr_percentage = ((i+1)*10 / (int)cells.size())*10;
            if (curr_percentage > last_percentage) {
                if (show_progress) {
                    logger->report(std::to_string(curr_percentage) + "% of the cells processed");
                }
                last_percentage = curr_percentage;
            }

            debug_data.lowest_costs[i] = dbu_to_microns(sqrt(lowest_cost));

            if (lowest_cost == std::numeric_limits<double>::max()) {
                fprintf(stderr, "ERROR: could not place cell\n");
                continue;
            }

            int new_x = winning_site_x;
            int new_y = row_to_y(rows[winning_row]);
            set_pos(cell, new_x, new_y);

            deque<dbInst*>* last_placed = &last_placed_per_row[winning_row];
            while (true) {
                if (last_placed->size() == 0) break;

                // x2 - left_factor * width2 >= x1 - left_factor * width1
                // x2 >= x1 - left_factor * width1
                // x2 == effective_x(x1)
                int curr_effective_x = orig_x - get_width(cell) * width_factor;
                int lower_bound_of_next_x = curr_effective_x;
                int lower_bound_of_next_target_x = lower_bound_of_next_x + delta_x;

                dbInst* placed_cell = last_placed->front();
                int cell_max_x = placed_cell->getBBox()->xMax();

                if (cell_max_x <= lower_bound_of_next_target_x) {
                    last_placed->pop_front();
                } else {
                    break;
                }
            }
            last_placed->push_back(cell);

            if (last_placed->size() > debug_data.max_deque_size) {
                debug_data.max_deque_size = last_placed->size();
            }
            if (debug_data.row_iter > debug_data.max_row_iter) {
                debug_data.max_row_iter = debug_data.row_iter;
            }
        }
    }

    int Tutorial::try_to_place_in_row(
        dbRow* row, dbInst* cell,
        int target_x,
        std::vector<dbInst*> const& fixed_cells,
        std::deque<dbInst*> const& last_placed
    ) {
        debug_data.row_iter++;
        debug_data.site_iter = 0; 

        int site_width = row->getSite()->getWidth();

        int row_start = row->getBBox().xMin();
        int row_end = row->getBBox().xMax();

        int approx_site_x = x_to_site(row, target_x) * site_width + row_start;

        int site_x = std::max(row_start, approx_site_x);

        while (true) {
            debug_data.site_iter++;

            int width1 = get_width(cell);
            int x1_min = site_x;
            int x1_max = x1_min + width1;

            int height1 = get_height(cell);
            int y1_min = row_to_y(row);
            int y1_max = y1_min + height1;

            if (x1_max > row_end) return row_end;

            bool collided = false;
            for (dbInst* other : fixed_cells) {
                int x2_min = other->getBBox()->xMin();
                int x2_max = other->getBBox()->xMax();
                int y2_min = other->getBBox()->yMin();
                int y2_max = other->getBBox()->yMax();

                if (collide(x1_min, x1_max, x2_min, x2_max)
                    && collide(y1_min, y1_max, y2_min, y2_max)
                ) {
                    site_x = x2_max;
                    collided = true;
                    break;
                }
            }

            if (collided) continue;

            for (dbInst* other : last_placed) {
                int x2_min = other->getBBox()->xMin();
                int x2_max = other->getBBox()->xMax();
                if (collide(x1_min, x1_max, x2_min, x2_max)) {
                    site_x = x2_max;
                    collided = true;
                    break;
                }
            }

            if (collided) continue;

            if (debug_data.site_iter > debug_data.max_site_iter) {
                debug_data.max_site_iter = debug_data.site_iter;
                debug_data.max_site_iter_site = x_to_site(row, target_x);
                debug_data.max_site_iter_cell = cell->getName();

                debug_data.max_site_iter_last_placed_site.clear();
                for (dbInst* curr_cell : last_placed) {
                    int x = curr_cell->getBBox()->xMax();
                    debug_data.max_site_iter_last_placed_site.push_back(x_to_site(row, x));
                }
            }

            return site_x;
        }
    }

    void Tutorial::tetris(int area_x1, int area_y1, int area_x2, int area_y2, bool show_progress) {
        int area_x_min, area_x_max, area_y_min, area_y_max;
        if (area_x1 < area_x2) {
            area_x_min = area_x1;
            area_x_max = area_x2;
        } else {
            area_x_max = area_x1;
            area_x_min = area_x2;
        }
        if (area_y1 < area_y2) {
            area_y_min = area_y1;
            area_y_max = area_y2;
        } else {
            area_y_max = area_y1;
            area_y_min = area_y2;
        }

        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        using std::vector;
        using std::deque;

        // separate fixed from non-fixed
        vector<dbInst*> fixed_cells;
        vector<dbInst*> cells;
        dbSet<dbInst> cells_set = block->getInsts();
        for (dbInst* cell : cells_set) {
            // include only if collides with more than half of the cell

            dbBox* box = cell->getBBox();
            int x_min = box->xMin();
            int x_max = box->xMax();
            int y_min = box->yMin();
            int y_max = box->yMax();
            int half_width = (x_max - x_min)/2;
            int half_height = (y_max - y_min)/2;

            int x2_min = area_x_min + half_width;
            int x2_max = area_x_max - half_width;
            int y2_min = area_y_min + half_height;
            int y2_max = area_y_max - half_height;

            if (collide(x_min, x_max, x2_min, x2_max)
                && collide(y_min, y_max, y2_min, y2_max)
            ) {
                if (cell->isFixed()) fixed_cells.push_back(cell);
                else                 cells.push_back(cell);
            }
        }

        // todo: move this to a struct (or receive in the arguments)
        float left_factor = 1.0; 
        float width_factor = 0.5;
        float x_to_y_priority_ratio = 1.0f;

        // sort cells by x
        auto effective_x = [&](dbInst* cell){
            // +width -> +priority
            int x = cell->getBBox()->xMin();
            return x - get_width(cell) * width_factor;
        };
        std::sort(cells.begin(), cells.end(), 
            [&](dbInst* cell1, dbInst* cell2){
                return effective_x(cell1) < effective_x(cell2);
            }
        );

        // sort rows by y
        vector<dbRow*> rows;
        dbSet<dbRow> rows_set = block->getRows();
        for (dbRow* row : rows_set) {
            Rect box = row->getBBox();
            int x_min = box.xMin();
            int x_max = box.xMax();
            int y_min = box.yMin();
            int y_max = box.yMax();
            if (collide(x_min, x_max, area_x_min, area_x_max)
                && collide(y_min, y_max, area_y_min, area_y_max)
            ) {
                rows.push_back(row);
            }
        }
        std::sort(rows.begin(), rows.end(),
            [&](dbRow* row1, dbRow* row2) {
                return row_to_y(row1) < row_to_y(row2);
            }
        );

        vector<int> rows_y(rows.size());
        for (int i = 0; i < rows.size(); i++) {
            rows_y[i] = row_to_y(rows[i]);
        }

        // max_width
        auto iter = std::max_element(cells.begin(), cells.end(),
            [&](dbInst* cell1, dbInst* cell2) {
                return get_width(cell1) < get_width(cell2);
            }
        );
        int max_width;
        if (iter != cells.end()) max_width = get_width(*iter);
        else                     max_width = 1;

        std::vector<dbInst*> cells_not_placed;

        // initialize last_placed_per_row and fixed_per_row
        // note: using deque instead of queue because queue doesnt support iteration
        // note: it is possible to substitute the queue for a vector with two indexes (WindowVector). All cells in the current row would be added beforehand. The pop_front would increment the start index; the push_back would increment the end index. I believe this is a better alternative due to its simplicity. Maybe it's possible to merge last_placed_per_row and fixed_per_row in a single data structure
        vector<deque<dbInst*>> last_placed_per_row(rows.size());
        vector<vector<dbInst*>> fixed_per_row(rows.size());
        for (dbInst* fixed : fixed_cells) {
            int y_min = fixed->getBBox()->yMin();
            int y_max = fixed->getBBox()->yMax();

            int lower_bound_row_i = std::lower_bound(
                    rows_y.begin(), rows_y.end(), y_min
                    )
                - rows_y.begin();

            int upper_bound_row_i = std::lower_bound(
                    rows_y.begin(), rows_y.end(), y_max
                    )
                - rows_y.begin();

            for (int i = lower_bound_row_i; i < upper_bound_row_i; i++) {
                fixed_per_row[i].push_back(fixed);
            }
        }

        // debug_data
        debug_data.max_deque_size = 0;
        debug_data.max_row_iter = 0;
        debug_data.max_site_iter = 0;

        debug_data.cell_iter = 0;

        debug_data.max_site_iter_site = 0;
        debug_data.max_site_iter_last_placed_site = vector<int>();
        debug_data.max_site_iter_cell.clear();

        debug_data.lowest_costs.resize(cells.size());

        // note: cells cannot move too much (like tetris). The left factor determines the fall speed
        int delta_x = -left_factor * max_width;
        int last_percentage = 0;
        for (int i = 0; i < cells.size(); i++) {
            dbInst* cell = cells[i];

            debug_data.cell_iter++;
            debug_data.row_iter = 0;

            int orig_x = cell->getBBox()->xMin();
            int orig_y = cell->getBBox()->yMin();
            int target_x = orig_x + delta_x;
            int target_y = orig_y;

            double lowest_cost = std::numeric_limits<double>::max();

            int winning_row = 0;
            int winning_site_x = 0;

            int approx_row;
            auto iter = std::lower_bound(rows_y.begin(), rows_y.end(), target_y);
            if (iter == rows_y.end()) approx_row = rows.size()-1;
            else                      approx_row = iter - rows_y.begin();

            for (int row_i = approx_row; row_i < rows.size(); row_i++) {
                dbRow* row = rows[row_i];

                int sqrt_cost_y = row_to_y(row) - target_y;
                int cost_y = sqrt_cost_y*sqrt_cost_y;

                if (cost_y > lowest_cost) break;

                int site_x = try_to_place_in_row(
                    row, area_x_min, area_x_max,
                    cell,
                    target_x,
                    fixed_per_row[row_i],
                    last_placed_per_row[row_i]
                );

                if (site_x == area_x_max) continue;

                int sqrt_cost_x = site_x - target_x;
                int cost_x = sqrt_cost_x*sqrt_cost_x;
                double cost = cost_x
                    + x_to_y_priority_ratio * cost_y;
                if (cost < lowest_cost) {
                    lowest_cost = cost;
                    winning_row = row_i;
                    winning_site_x = site_x;
                }
            }
            for (int row_i = approx_row-1; row_i >= 0; row_i--) {
                dbRow* row = rows[row_i];

                int sqrt_cost_y = row_to_y(row) - target_y;
                int cost_y = sqrt_cost_y*sqrt_cost_y;

                if (cost_y > lowest_cost) break;

                int site_x = try_to_place_in_row(
                    row, area_x_min, area_x_max,
                    cell,
                    target_x,
                    fixed_per_row[row_i],
                    last_placed_per_row[row_i]
                );

                if (site_x == area_x_max) continue;

                int sqrt_cost_x = site_x - target_x;
                int cost_x = sqrt_cost_x*sqrt_cost_x;
                double cost = cost_x
                    + x_to_y_priority_ratio * cost_y;
                if (cost < lowest_cost) {
                    lowest_cost = cost;
                    winning_row = row_i;
                    winning_site_x = site_x;
                }
            }

            int curr_percentage = ((i+1)*10 / (int)cells.size())*10;
            if (curr_percentage > last_percentage) {
                if (show_progress) {
                    logger->report(std::to_string(curr_percentage) + "% of the cells processed");
                }
                last_percentage = curr_percentage;
            }

            debug_data.lowest_costs[i] = dbu_to_microns(sqrt(lowest_cost));

            if (lowest_cost == std::numeric_limits<double>::max()) {
                fprintf(stderr, "ERROR: could not place cell\n");
                cells_not_placed.push_back(cell);
                continue;
            }

            int new_x = winning_site_x;
            int new_y = row_to_y(rows[winning_row]);
            set_pos(cell, new_x, new_y);

            deque<dbInst*>* last_placed = &last_placed_per_row[winning_row];
            while (true) {
                if (last_placed->size() == 0) break;

                // x2 - left_factor * width2 >= x1 - left_factor * width1
                // x2 >= x1 - left_factor * width1
                // x2 == effective_x(x1)
                int curr_effective_x = orig_x - get_width(cell) * width_factor;
                int lower_bound_of_next_x = curr_effective_x;
                int lower_bound_of_next_target_x = lower_bound_of_next_x + delta_x;

                dbInst* placed_cell = last_placed->front();
                int cell_max_x = placed_cell->getBBox()->xMax();

                if (cell_max_x <= lower_bound_of_next_target_x) {
                    last_placed->pop_front();
                } else {
                    break;
                }
            }
            last_placed->push_back(cell);

            if (last_placed->size() > debug_data.max_deque_size) {
                debug_data.max_deque_size = last_placed->size();
            }
            if (debug_data.row_iter > debug_data.max_row_iter) {
                debug_data.max_row_iter = debug_data.row_iter;
            }
        }

        if (cells_not_placed.size() > 0) {
            logger->report("Could not place " + std::to_string(cells_not_placed.size()) + " cells:");
            std::string names;
            for (dbInst* cell : cells_not_placed) {
                names += cell->getName() + " ";
            }
            logger->report(names);
        }
    }


    int Tutorial::try_to_place_in_row(
        dbRow* row, int row_x_min, int row_x_max,
        dbInst* cell,
        int target_x,
        std::vector<dbInst*> const& fixed_cells,
        std::deque<dbInst*> const& last_placed
    ) {
        debug_data.row_iter++;
        debug_data.site_iter = 0; 

        int site_width = row->getSite()->getWidth();

        int row_start = row->getBBox().xMin();

        // todo: x_start is an approximation. It probably should be ceil instead of floor
        int x_start = (row_x_min - row_start)/site_width*site_width + row_start;
        int x_end = row_x_max;

        int approx_site_x = (target_x - row_start)/site_width*site_width + row_start;

        int site_x = std::max(x_start, approx_site_x);

        while (true) {
            debug_data.site_iter++;

            int width1 = get_width(cell);
            int x1_min = site_x;
            int x1_max = x1_min + width1;

            int height1 = get_height(cell);
            int y1_min = row_to_y(row);
            int y1_max = y1_min + height1;

            if (x1_max > x_end) return x_end;

            bool collided = false;
            for (dbInst* other : fixed_cells) {
                int x2_min = other->getBBox()->xMin();
                int x2_max = other->getBBox()->xMax();
                int y2_min = other->getBBox()->yMin();
                int y2_max = other->getBBox()->yMax();

                if (collide(x1_min, x1_max, x2_min, x2_max)
                    && collide(y1_min, y1_max, y2_min, y2_max)
                ) {
                    site_x = x2_max;
                    collided = true;
                    break;
                }
            }

            if (collided) continue;

            for (dbInst* other : last_placed) {
                int x2_min = other->getBBox()->xMin();
                int x2_max = other->getBBox()->xMax();
                if (collide(x1_min, x1_max, x2_min, x2_max)) {
                    site_x = x2_max;
                    collided = true;
                    break;
                }
            }

            if (collided) continue;

            if (debug_data.site_iter > debug_data.max_site_iter) {
                debug_data.max_site_iter = debug_data.site_iter;
                debug_data.max_site_iter_site = x_to_site(row, target_x);
                debug_data.max_site_iter_cell = cell->getName();

                debug_data.max_site_iter_last_placed_site.clear();
                for (dbInst* curr_cell : last_placed) {
                    int x = curr_cell->getBBox()->xMax();
                    debug_data.max_site_iter_last_placed_site.push_back(x_to_site(row, x));
                }
            }

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

    double Tutorial::microns_to_dbu(double microns) {
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

    int Tutorial::get_width(dbInst* cell) {
        dbBox* box = cell->getBBox();
        return box->xMax() - box->xMin();
    };

    int Tutorial::get_height(dbInst* cell) {
        dbBox* box = cell->getBBox();
        return box->yMax() - box->yMin();
    };

    void Tutorial::set_pos(dbInst* cell, int x, int y) {
        cell->setLocation(x, y);
    };

    int Tutorial::row_to_y(dbRow* row) {
        int x, y;
        row->getOrigin(x, y);
        return y;
    };

    int Tutorial::x_to_site(dbRow* row, int x) {
        int row_start = row->getBBox().xMin();
        int site_width = row->getSite()->getWidth();
        return (x - row_start)/site_width;
    }

    bool Tutorial::collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max) {
        return pos1_min < pos2_max && pos2_min < pos1_max;
    };
}

