#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "odb/dbTypes.h"
#include "odb/dbTransform.h"

#include <limits>

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

        auto p_cell = cells.begin();
        p_cell++;
        p_cell++;
        p_cell++;
        dbInst* cell = *p_cell;
        printf("%s\n", cell->getName().c_str());
        auto [x, y] = xy_microns_to_dbu(50, 25);
        cell->setLocation(x, y);
        {
            int x, y;
            cell->getLocation(x, y);
            auto [xd, yd] = xy_dbu_to_microns(x, y);
            printf("location: %lf, %lf\n", xd, yd);
        }
        {
            int x, y;
            cell->getOrigin(x, y);
            auto [xd, yd] = xy_dbu_to_microns(x, y);
            printf("origin: %lf, %lf\n", xd, yd);
        }

        /*
        for (dbInst* cell : cells) {
            {
                auto [x, y] = point_to_dbu(get_pos(cell));
                printf("%s\n", cell->getName().c_str());
                printf("old_pos: %lf, %lf\n", x, y);
            }

            {
                set_pos(cell, microns_to_dbu(50), microns_to_dbu(50));

                auto [x, y] = point_to_dbu(get_pos(cell));
                printf("new_pos: %lf, %lf\n", x, y);

                int x_l, y_l;
                cell->getLocation(x_l, y_l);
                printf("getLocation: %lf, %lf\n", dbu_to_microns(x), dbu_to_microns(x));
                printf("\n");
            }
        }
        */
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

        Rect rect = block->getDieArea();
        int min_x = rect.xMin();
        int max_x = rect.xMax();
        int min_y = rect.yMin();
        int max_y = rect.yMax();

        dbSet<dbInst> cells = block->getInsts();
        for (odb::dbInst* cell : cells) {
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

    // todo: crasha quando existem "FIRM instances"
    // todo: openroad "hangs" para circuito grande. Talvez substituir col para sites?
    void Tutorial::tetris() {
        using std::vector;
        using namespace odb;

        dbChip* chip = db_->getChip();
        if (!chip) {
            fprintf(stderr, "no circuit loaded\n");
            return;
        }
        dbBlock* block = chip->getBlock();

        vector<dbInst*> cells;
        dbSet<dbInst> cells_set = block->getInsts();
        for (dbInst* cell : cells_set) {
            cells.push_back(cell);
        }

        float left_factor = 0.5; 
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

        Rect rect = block->getDieArea();
        int min_x = rect.xMin();
        int max_x = rect.xMax();
        auto cell_can_fit_here = [&](dbInst* cell, int col, dbRow* row) -> bool {
            //////////////
            // check if cell is in bounds
            //////////////
            if (!(min_x < col && col + get_width(cell) < max_x)) return false;

            //////////////
            // check if cell is not colliding with other placed cell
            //////////////

            for (auto p_other = cells.begin(); *p_other != cell; p_other++) {
                dbInst* other = *p_other;
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

        for (dbInst* cell : cells) {
            // cells cannot move too much (left_factor)
            auto [x, y] = get_pos(cell);
            int target_x = x - left_factor * widest_width;
            int target_y = y;
            double lowest_cost = std::numeric_limits<double>::max();

            dbRow* winning_row = 0;
            int winning_col = 0;

            for (dbRow* row : rows) {
                // todo: iterar pelos sites em vez das colunas?
                for (int col = target_x; col < max_x; col++) {
                    if (!cell_can_fit_here(cell, col, row)) {
                        continue;
                    }

                    // todo: por que o custo esta sendo computado em relacao a target_x em vez de cell.x?
                    int sqrt_cost_x = col - target_x;
                    int sqrt_cost_y = row_to_y(row) - target_y;
                    double cost = sqrt_cost_x*sqrt_cost_x
                        + x_to_y_priority_ratio * sqrt_cost_y*sqrt_cost_y;
                    if (cost < lowest_cost) {
                        lowest_cost = cost;
                        winning_row = row;
                        winning_col = col;
                    }
                    break;
                }
            }
            if (lowest_cost == std::numeric_limits<double>::max()) {
                fprintf(stderr, "ERROR: could not place cell\n");
                continue;
            }
            int new_x = winning_col;
            int new_y = row_to_y(winning_row);
            set_pos(cell, new_x, new_y);
        }

//            // moves to col with lowest cost
//            // detailed placement?
//            std::sort(cells.begin(), cells.end(),
//                [&](cell1, cell2) {
//                    if (cell1.y != cell2.y) {
//                        return cell1.y < cell2.y;
//                    } else {
//                        return cell1.x > cell2.x;
//                    }
//                }
//            );
//            for (auto cell : cells) {
//                int row = y_to_row(cell.y);
//                for (int col = x_to_col(cell.x); col < num_cols; col++) {
//                    if (!cell_can_fit_here(cell, col, row)) {
//                        continue;
//                    }
//                    cost = cost_function(cell, col, row);
//                    if (cost < current_cost) {
//                        cell.x = col_to_x(col);
//                        current_cost = cost;
//                    }
//                }
//            }
//            // minimizing cost by rotating cell
//            int max_passes = 2;
//            for (pass = 0; pass < max_passes; pass++) {
//                std::sort(cells.begin(), cells.end(),
//                    [&](cell1, cell2) {
//                        if (cell1.y != cell2.y) {
//                            return cell1.y < cell2.y;
//                        } else {
//                            return cell1.x < cell2.x;
//                        }
//                    }
//                );
//                for (auto cell : cells) {
//                    double best_cost = wire_cost(cell, cell.orientation);
//                    original_orientation = cell.orientation;
//                    foreach (orientation) {
//                        if (cell_is_valid(cell, orientation)) {
//                            continue;
//                        }
//                        cost = wire_cost(cell, orientation);
//                        if (cost < best_cost) {
//                            cell.orientation = orientation;
//                        }
//                    }
//                }
//            }
    }
}


