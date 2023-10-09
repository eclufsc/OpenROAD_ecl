#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "odb/dbTypes.h"
#include "odb/dbTransform.h"

#include <limits>
#include <algorithm>
#include <deque>

namespace tut {
    using namespace odb;

    Tutorial::Tutorial() :
        db{ord::OpenRoad::openRoad()->getDb()},
        logger{ord::OpenRoad::openRoad()->getLogger()},
        block{0},
        debug_data{}
        {}

    Tutorial::~Tutorial() {}

    void Tutorial::init() {
        dbChip* chip = db->getChip();
        if (chip) block = chip->getBlock();
        else      fprintf(stderr, "no circuit loaded\n");
    }

    // both (get/set)(Location/Origin) operate at block coordinate system
    void Tutorial::test() {
        if (!block) {
            fprintf(stderr, "init not called succesfully\n");
            return;
        }

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
        if (!block) {
            fprintf(stderr, "init not called succesfully\n");
            return;
        }

        dbSet<dbInst> cells = block->getInsts();
        for (dbInst* cell : cells) {
            if (cell->isFixed()) continue;

            auto [x, y] = get_pos(cell);

            int new_x = x + (rand() % get_width(cell) - get_width(cell)/2);
            int new_y = y + (rand() % get_height(cell) - get_height(cell)/2);

            set_pos(cell, new_x, new_y);
        }
    }

    void Tutorial::shuffle() {
        if (!block) {
            fprintf(stderr, "init not called succesfully\n");
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

    void Tutorial::tetris() {
        if (!block) {
            fprintf(stderr, "init not called succesfully\n");
            return;
        }

        printf("starting tetris\n");

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
        printf("fixed_cells.size() = %lu\n", fixed_cells.size());

        // todo: move this to a struct
        float left_factor = 1.0; 
        float width_factor = 0.5;
        float x_to_y_priority_ratio = 1.0f;

        // sort cells by x
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
        vector<deque<dbInst*>> last_placed_per_row(rows.size());

        vector<vector<dbInst*>> fixed_per_row(rows.size());
        for (dbInst* fixed : fixed_cells) {
            for (int i = 0; i < rows.size(); i++) {
                auto [x1, y1] = get_pos(fixed);
                int height1 = get_height(fixed);

                int y2 = rows[i]->getBBox().yMin();
                int height2 = rows[i]->getBBox().yMax() - y2;

                if (collide(y1, y2, height1, height2)) {
                    fixed_per_row[i].push_back(fixed);
                }
            }
        }

        // todo: delete
        std::size_t max_fixed_per_row = std::max_element(
            fixed_per_row.begin(), fixed_per_row.end(),
            [&](vector<dbInst*>& a, vector<dbInst*>& b) {
                return a.size() < b.size();
            }
        )->size();
        printf("max_fixed_per_row = %lu\n", max_fixed_per_row);

        // debug_data
        debug_data.max_deque_size = 0;
        debug_data.max_row_iter = 0;
        debug_data.max_site_iter = 0;

        debug_data.cell_iter = 0;

        debug_data.max_site_iter_site = 0;
        debug_data.max_site_iter_last_placed_site = vector<int>();
        debug_data.max_site_iter_cell.clear();

        // note: cells cannot move too much (like tetris). The left factor determines the fall speed
        int delta_x = -left_factor * max_width;
        for (dbInst* cell : cells) {
//            printf("%s\n", cell->getName().c_str());

            debug_data.cell_iter++;
            debug_data.row_iter = 0;

            auto [orig_x, orig_y] = get_pos(cell);
            int target_x = orig_x + delta_x;
            int target_y = orig_y;

            double lowest_cost = std::numeric_limits<double>::max();
            int winning_row = 0;
            int winning_site_x = 0;

            int approx_row;
            auto iter = std::lower_bound(rows_y.begin(), rows_y.end(), target_y);
            if (iter == rows_y.end()) approx_row = 0;
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


            /*
            printf("cell_iter = %d\n", debug_data.cell_iter);
            printf("max_row_iter = %d\n", debug_data.max_row_iter);
            printf("max_site_iter = %d\n", debug_data.max_site_iter);
            printf("max_deque_size = %d\n", debug_data.max_deque_size);
            printf("\n");
            */
        }

        int site_width = rows.back()->getSite()->getWidth();

        printf("max_cell = %s\n", debug_data.max_site_iter_cell.c_str());
        printf("max_site_iter = %d\n", debug_data.max_site_iter);
        printf("max_site_iter_site = %d\n", debug_data.max_site_iter_site);
        for (int curr_site : debug_data.max_site_iter_last_placed_site) {
            printf("%d ", curr_site);
        }
        printf("\n\n");

        printf("max_row_iter = %d\n", debug_data.max_row_iter);
        printf("max_site_iter = %d\n", debug_data.max_site_iter);
        printf("max_deque_size = %d\n", debug_data.max_deque_size);
        printf("max sites = %d\n", delta_x / site_width);
        printf("delta_x = %d\n", delta_x);
        printf("max_width_in_sites = %d\n", max_width/site_width);
        printf("site_width = %d\n", site_width);
        printf("left_factor = %lf\n", left_factor);
        printf("width_factor = %lf\n", width_factor);
        printf("max_width = %d\n", max_width);

        printf("finished tetris\n");
        printf("\n");
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
            int height1 = get_height(cell);

            int x1 = site_x;
            int y1 = row_to_y(row);

            if (x1 + width1 > row_end) return row_end;

            bool collided = false;
            for (dbInst* other : fixed_cells) {
                auto [x2, y2] = get_pos(other);
                int width2 = get_width(other);
                int height2 = get_height(other);

                if (collide(x1, x2, width1, width2)
                    && collide(y1, y2, height1, height2)
                ) {
                    site_x = x2 + width2;
                    collided = true;
                    break;
                }
            }

            if (collided) continue;

            for (dbInst* other : last_placed) {
                auto [x2, y2] = get_pos(other);
                int width2 = get_width(other);
                if (collide(x1, x2, width1, width2)) {
                    site_x = x2 + width2;
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

        //                printf("%d\n", debug_data.site_iter);
    }

    double Tutorial::dbu_to_microns(int64_t dbu) {
        if (!block) {
            fprintf(stderr, "init not called succesfully\n");
            return 0;
        }

        return (double) dbu / block->getDbUnitsPerMicron();
    };

    double Tutorial::microns_to_dbu(double microns) {
        if (!block) {
            fprintf(stderr, "init not called succesfully\n");
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

    std::pair<int, int> Tutorial::get_pos(dbInst* cell) {
        int x, y;
        cell->getLocation(x, y);
        return {x, y};
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

    bool Tutorial::collide(int pos1, int pos2, int dimens1, int dimens2) {
        return pos1 < pos2 + dimens2 && pos2 < pos1 + dimens1;
    };
}

