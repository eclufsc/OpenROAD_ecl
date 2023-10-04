#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

#include <iostream>
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

    double dbuToMicrons(odb::dbBlock* block, int64_t dbu) {
        return (double) dbu / (block->getDbUnitsPerMicron());
    }

    void Tutorial::test() {
        auto block = db_->getChip()->getBlock();

        for (auto inst : block->getInsts()) {
            int x_i, y_i;
            inst->getOrigin(x_i, y_i);

            double x = dbuToMicrons(block, x_i);
            double y = dbuToMicrons(block, y_i);

            printf("%s\n", inst->getName().c_str());
            printf("x, y = %lf, %lf\n", x, y);
        }
    }

    void algorithm() {
        float left_factor = 1.0; 
        float width_factor = 0.5;

        float effective_x(cell) {
            // +width -> +priority
            return cell.x - cell.width * width_factor;
        }

        void eco_place(cells) {
            // move to closest leftmost positions of the rows
            int widest_width = std::max(cells.begin(), cells.end(),
                [&](cell1, cell2) {
                    return cell1.width < cell2.width;
                }
            );
            std::sort(cells.begin(), cells.end(), 
                [&](cell1, cell2){
                    return effective_x(cell1) < effective_x(cell2);
                }
            );
            for (auto cell : cells) {
                // cells cannot move too much (left_factor)
                int target_x = cell.optimal.x - left_factor * widest_width;
                int target_y = cell.optimal.y;
                int lowest_cost = INT_MAX;

                int winning_row = 0;
                int winning_col = 0;
                for (int row = 0; row <= top_row; row++) {
                    for (int col = x_to_col(target_x), col < rightmost_col; col++) {
                        if (!cell_can_fit_here(cell, col, row)) {
                            continue;
                        }

                        // todo: por que o custo esta sendo computado em relacao a target_x em vez de cell.x?
                        int sqrt_cost_x = site_x(col) - target_x;
                        int sqrt_cost_y = site_y(row) - target_y;
                        double cost = sqrt_cost_x*sqrt_cost_x
                            + X_TO_Y_PRIORITY_RATIO * sqrt_cost_y*sqrt_cost_y;
                        if (cost < lowest_cost) {
                            lowest_cost = cost;
                            winning_row = row;
                            winning_col = col;
                        }
                        break;
                    }
                }
                if (lowest_cost == INT_MAX) {
                    fprintf(stderr, "ERROR: could not place cell\n");
                    continue;
                }
                cell.x = site_to_x(col);
                cell.y = site_to_y(row);
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
}


