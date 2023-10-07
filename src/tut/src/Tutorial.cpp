#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "odb/dbTypes.h"
#include "odb/dbTransform.h"

#include <limits>
#include <algorithm>
#include <deque>

/*
 * How to use:
 * Most of the ODB functionaly can be understood
 * looking at "odb/db.h" File
*/

namespace tut {
    Tutorial::Tutorial() :
        db_{ord::OpenRoad::openRoad()->getDb()},
        logger_{ord::OpenRoad::openRoad()->getLogger()}
    {}

    Tutorial::~Tutorial() {}

    // both (get/set)(Location/Origin) operate at block coordinate system
    void Tutorial::test() {
        using namespace odb;

        dbChip* chip = db_->getChip();
        if (!chip) {
            fprintf(stderr, "no circuit loaded\n");
            return;
        }
        dbBlock* block = chip->getBlock();

        dbSet<dbInst> cells = block->getInsts();

        auto dbu_to_microns = [&](int64_t dbu) -> double {
            return (double) dbu / block->getDbUnitsPerMicron();
        };

        auto microns_to_dbu = [&](double microns) -> double {
            return microns * block->getDbUnitsPerMicron();
        };

        auto xy_dbu_to_microns = [&](int x, int y) -> std::pair<double, double> {
            return {dbu_to_microns(x), dbu_to_microns(y)};
        };

        auto xy_microns_to_dbu = [&](double x, double y) -> std::pair<int, int> {
            return {microns_to_dbu(x), microns_to_dbu(y)};
        };

        auto get_pos = [&](dbInst* cell) -> Point {
            if (!cell) {
                fprintf(stderr, "cell null\n");
                return Point(1.0f, 1.0f);
            }
            dbBox* box = cell->getBBox();
            return Point(box->xMin(), box->yMin());
        };

        auto set_pos = [&](dbInst* cell, int x, int y) {
            cell->setOrigin(x, y);
        };

        std::vector<dbRow*> rows;
        dbSet<dbRow> rows_set = block->getRows();
        for (dbRow* row : rows_set) {
            rows.push_back(row);
        }

        printf("rows:\n");
        for (dbRow* row : rows) {
            printf("\n%s\n", row->getName().c_str());
            {
                Rect rect = row->getBBox();

                auto [x_min, y_min] = xy_dbu_to_microns(rect.xMin(), rect.yMin());
                auto [x_max, y_max] = xy_dbu_to_microns(rect.xMax(), rect.yMax());
                printf("box: min = (%lf, %lf); max = (%lf, %lf)\n", x_min, y_min, x_max, y_max);
                printf("site count = %d\n", row->getSiteCount());
                printf("site width = %u\n", row->getSite()->getWidth());
                printf("row width = %d\n", rect.xMax() - rect.xMin());
                printf("count * width = %u\n", row->getSiteCount()*row->getSite()->getWidth());
            }
        }
    }

    void Tutorial::disturb() {
        using namespace odb;

        dbChip* chip = db_->getChip();
        if (!chip) {
            fprintf(stderr, "no circuit loaded\n");
            return;
        }
        dbBlock* block = chip->getBlock();
        
        auto get_width = [&](dbInst* cell) {
            dbBox* box = cell->getBBox();
            return box->xMax() - box->xMin();
        };

        auto get_height = [&](dbInst* cell) {
            dbBox* box = cell->getBBox();
            return box->yMax() - box->yMin();
        };

        auto get_pos = [&](dbInst* cell) -> std::pair<int, int> {
            int x, y;
            cell->getLocation(x, y);
            return {x, y};
        };

        auto set_pos = [&](dbInst* cell, int x, int y) {
            cell->setLocation(x, y);
        };

        dbSet<dbInst> cells = block->getInsts();
        for (odb::dbInst* cell : cells) {
            if (cell->isFixed()) continue;

            auto [x, y] = get_pos(cell);

            int new_x = x + rand() % get_width(cell);
            int new_y = y + rand() % get_height(cell);

            set_pos(cell, new_x, new_y);
        }
    }

    void Tutorial::shuffle() {
        using namespace odb;

        dbChip* chip = db_->getChip();
        if (!chip) {
            fprintf(stderr, "no circuit loaded\n");
            return;
        }
        dbBlock* block = chip->getBlock();

        auto microns_to_dbu = [&](double microns) -> double {
            return (double) microns * block->getDbUnitsPerMicron();
        };
        
        auto get_width = [&](dbInst* cell) {
            dbBox* box = cell->getBBox();
            return box->xMax() - box->xMin();
        };

        auto get_height = [&](dbInst* cell) {
            dbBox* box = cell->getBBox();
            return box->yMax() - box->yMin();
        };

        Rect rect = block->getCoreArea();
        int min_x = rect.xMin();
        int max_x = rect.xMax();
        int min_y = rect.yMin();
        int max_y = rect.yMax();

        dbSet<dbInst> cells = block->getInsts();
        for (odb::dbInst* cell : cells) {
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

    void Tutorial::tetris() {
        using std::vector;
        using std::deque;
        using namespace odb;

        dbChip* chip = db_->getChip();
        if (!chip) {
            fprintf(stderr, "no circuit loaded\n");
            return;
        }
        dbBlock* block = chip->getBlock();

        vector<dbInst*> fixed_cells;
        vector<dbInst*> cells;
        dbSet<dbInst> cells_set = block->getInsts();
        for (dbInst* cell : cells_set) {
            if (cell->isFixed()) fixed_cells.push_back(cell);
            else                 cells.push_back(cell);
        }

        float left_factor = 1.0; 
        float width_factor = 0.5;
        float x_to_y_priority_ratio = 1.0f;

        vector<dbRow*> rows;
        dbSet<dbRow> rows_set = block->getRows();
        for (dbRow* row : rows_set) {
            rows.push_back(row);
        }

        auto row_to_y = [&](dbRow* row) -> int {
            int x, y;
            row->getOrigin(x, y);
            return y;
        };

        std::sort(rows.begin(), rows.end(),
            [&](dbRow* row1, dbRow* row2) {
                return row_to_y(row1) < row_to_y(row2);
            }
        );

        auto dbu_to_microns = [&](int64_t dbu) -> double {
            return (double) dbu / block->getDbUnitsPerMicron();
        };

        auto microns_to_dbu = [&](double microns) -> double {
            return (double) microns * block->getDbUnitsPerMicron();
        };

        auto xy_dbu_to_microns = [&](int x, int y) -> std::pair<double, double> {
            return {dbu_to_microns(x), dbu_to_microns(y)};
        };

        auto xy_microns_to_dbu = [&](double x, double y) -> std::pair<int, int> {
            return {microns_to_dbu(x), microns_to_dbu(y)};
        };

        auto get_width = [&](dbInst* cell) {
            dbBox* box = cell->getBBox();
            return box->xMax() - box->xMin();
        };

        auto get_pos = [&](dbInst* cell) -> std::pair<int, int> {
            int x, y;
            cell->getLocation(x, y);
            return {x, y};
        };

        auto set_pos = [&](dbInst* cell, int x, int y) {
            cell->setLocation(x, y);
        };

        int widest_width = get_width(*std::max_element(cells.begin(), cells.end(),
            [&](dbInst* cell1, dbInst* cell2) {
                return get_width(cell1) < get_width(cell2);
            }
        ));

        auto effective_x = [&](dbInst* cell){
            // +width -> +priority
            auto [x, y] = get_pos(cell);
            return x - get_width(cell) * width_factor;
        };
        std::sort(cells.begin(), cells.end(), 
            [&](dbInst* cell1, dbInst* cell2){
                return effective_x(cell1) < effective_x(cell2);
            }
        );

        // using deque instead of queue because queue doesnt support iteration
        // max deque size = 8 (by crude experimentation with ispd18_test4)
        vector<deque<dbInst*>> last_fixed_per_row(rows.size());

        Rect rect = block->getCoreArea();
        int min_x = rect.xMin();
        int max_x = rect.xMax();
        auto cell_can_fit_here = [&](dbInst* cell, int col, int row_i) -> bool {
            //////////////
            // check if cell is in bounds
            //////////////
            if (!(min_x <= col && col + get_width(cell) <= max_x)) return false;

            dbRow* row = rows[row_i];

            //////////////
            // check if cell is not colliding with other placed cell
            //////////////
            for (dbInst* other : fixed_cells) {
                int x1 = col;
                int y1 = row_to_y(row);
                auto [x2, y2] = get_pos(other);
                if (y1 == y2) {
                    if ((x2 <= x1 && x1 < x2 + get_width(other))
                        || (x1 <= x2 && x2 < x1 + get_width(cell))) {
                        return false;
                    }
                }
            }
            for (dbInst* other : last_fixed_per_row[row_i]) {
                int x1 = col;
                int y1 = row_to_y(row);
                auto [x2, y2] = get_pos(other);
                if (y1 == y2) {
                    if ((x2 <= x1 && x1 < x2 + get_width(other))
                        || (x1 <= x2 && x2 < x1 + get_width(cell))) {
                        return false;
                    }
                }
            }

            return true;
        };

        vector<int> rows_y(rows.size());
        for (int i = 0; i < rows.size(); i++) {
            rows_y[i] = row_to_y(rows[i]);
        }

        for (dbInst* cell : cells) {
            // cells cannot move too much (left_factor)
            auto [x, y] = get_pos(cell);
            int target_x = x - left_factor * widest_width;
            int target_y = y;
            double lowest_cost = std::numeric_limits<double>::max();

//            printf("%s\n", cell->getName().c_str());

            int winning_row = 0;
            int winning_col = 0;

            auto iter = std::lower_bound(rows_y.begin(), rows_y.end(), target_y);
            int approx_row;
            if (iter == rows_y.end()) approx_row = 0;
            else                      approx_row = iter - rows_y.begin();
            for (int row_i = approx_row; row_i < rows.size(); row_i++) {
                dbRow* row = rows[row_i];

                int sqrt_cost_y = row_to_y(row) - target_y;
                int cost_y = sqrt_cost_y*sqrt_cost_y;

                if (cost_y > lowest_cost) break;

                int site_n = row->getSiteCount(); 
                int site_width = row->getSite()->getWidth();
                int row_start = row->getBBox().xMin();

                int start_site_x = (target_x - row_start)/site_width * site_width
                                    + row_start;
                int end_site_x = row->getBBox().xMax();

                for (int site_x = start_site_x; site_x < end_site_x; site_x += site_width) {
                    if (!cell_can_fit_here(cell, site_x, row_i)) {
                        continue;
                    }

                    int sqrt_cost_x = site_x - target_x;
                    int cost_x = sqrt_cost_x*sqrt_cost_x;
                    double cost = cost_x
                        + x_to_y_priority_ratio * cost_y;
                    if (cost < lowest_cost) {
                        lowest_cost = cost;
                        winning_row = row_i;
                        winning_col = site_x;
                    }
                    break;
                }
            }
            for (int row_i = approx_row-1; row_i >= 0; row_i--) {
                dbRow* row = rows[row_i];

                int sqrt_cost_y = row_to_y(row) - target_y;
                int cost_y = sqrt_cost_y*sqrt_cost_y;

                if (cost_y > lowest_cost) break;

                int site_n = row->getSiteCount(); 
                int site_width = row->getSite()->getWidth();
                int row_start = row->getBBox().xMin();

                int start_site_x = (target_x - row_start)/site_width * site_width
                                    + row_start;
                int end_site_x = row->getBBox().xMax();

                for (int site_x = start_site_x; site_x < end_site_x; site_x += site_width) {
                    if (!cell_can_fit_here(cell, site_x, row_i)) {
                        continue;
                    }

                    int sqrt_cost_x = site_x - target_x;
                    int cost_x = sqrt_cost_x*sqrt_cost_x;
                    double cost = cost_x
                        + x_to_y_priority_ratio * cost_y;
                    if (cost < lowest_cost) {
                        lowest_cost = cost;
                        winning_row = row_i;
                        winning_col = site_x;
                    }
                    break;
                }
            }
            if (lowest_cost == std::numeric_limits<double>::max()) {
                fprintf(stderr, "ERROR: could not place cell\n");
                continue;
            }
            int new_x = winning_col;
            int new_y = row_to_y(rows[winning_row]);
            set_pos(cell, new_x, new_y);

            while (true) {
                deque<dbInst*>* last_fixed = &last_fixed_per_row[winning_row];

                if (last_fixed->size() == 0) break;

                dbInst* cell = last_fixed->front();

                // x2 - left_factor * width2 >= x1 - left_factor * width1
                // x2 >= x1 - left_factor * width1
                // x2 == effective_x(x1)
                int curr_effective_x = x - get_width(cell) * width_factor;
                int lower_bound_of_next_target_x = curr_effective_x - left_factor * widest_width;

                int cell_max_x = cell->getBBox()->xMax();

                if (cell_max_x <= lower_bound_of_next_target_x) {
                    last_fixed->pop_front();
                } else {
                    break;
                }
            }

            last_fixed_per_row[winning_row].push_back(cell);
        }
    }
}


