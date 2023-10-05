#include "tut/Tutorial.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "odb/dbTypes.h"

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

    void Tutorial::test() {
        using std::vector;
        using odb::dbRow;
        using odb::dbRowDir;

        auto block = db_->getChip()->getBlock();

        auto dbuToMicrons = [&](int64_t dbu) -> double {
            return (double) dbu / (block->getDbUnitsPerMicron());
        };

        auto rows = block->getRows();
        vector<dbRow*> lines;
        vector<dbRow*> columns;
        for (auto row : rows) {
            if (row->getDirection().getValue() == dbRowDir::Value::HORIZONTAL) {
                lines.push_back(row);
            } else {
                columns.push_back(row);
            }
        }
        std::sort(lines.begin(), lines.end(),
            [&](dbRow* row1, dbRow* row2) {
                int x1, y1; 
                row1->getOrigin(x1, y1);
                int x2, y2; 
                row2->getOrigin(x2, y2);
                return y1 < y2; 
            }
        );
        std::sort(columns.begin(), columns.end(),
            [&](dbRow* row1, dbRow* row2) {
                int x1, y1; 
                row1->getOrigin(x1, y1);
                int x2, y2; 
                row2->getOrigin(x2, y2);
                return x1 < x2;
            }
        );

        std::cout << "size = " << rows.size() << "\n";

        printf("lines\n");
        for (dbRow* line : lines) {
            int x_i, y_i;
            line->getOrigin(x_i, y_i);
            double x = dbuToMicrons(x_i);
            double y = dbuToMicrons(y_i);
            printf("x, y = %lf, %lf\n", x, y);
        }
        printf("\n");

        printf("columns\n");
        for (dbRow* column : columns) {
            int x_i, y_i;
            column->getOrigin(x_i, y_i);
            double x = dbuToMicrons(x_i);
            double y = dbuToMicrons(y_i);
            printf("x, y = %lf, %lf\n", x, y);
        }
        printf("\n");

//        for (auto inst : block->getInsts()) {
//            int x_i, y_i;
//            inst->getOrigin(x_i, y_i);
//
//            
////            double x = dbuToMicrons(block, x_i);
////            double y = dbuToMicrons(block, y_i);
////
////            printf("%s\n", inst->getName().c_str());
////            printf("x, y = %lf, %lf\n", x, y);
//            
//        }
    }

    void Tutorial::shuffle() {
        using namespace odb;
        dbBlock* block = db_->getChip()->getBlock();
        dbSet<dbInst> cells = block->getInsts();
        for (odb::dbInst* cell : cells) {
            cell->setOrigin(rand()%70000, rand()%70000);
        }
    }

    void Tutorial::tetris() {
        using std::vector;
        using namespace odb;

        dbBlock* block = db_->getChip()->getBlock();
        vector<dbInst*> cells;
        dbSet<dbInst> cells_set = block->getInsts();
        for (dbInst* cell : cells_set) {
            cells.push_back(cell);
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
        // nos ispd 2018 (alguns deles, nao sei se todos) nao tem nenhum row vertical
//        std::sort(columns.begin(), columns.end(),
//            [&](dbRow* row1, dbRow* row2) {
//                int x1, y1; 
//                row1->getOrigin(x1, y1);
//                int x2, y2; 
//                row2->getOrigin(x2, y2);
//                return x1 < x2;
//            }
//        );

        printf("rows funcionaram\n");
        fflush(stdout);

        auto dbuToMicrons = [&](int64_t dbu) -> double {
            return (double) dbu / (block->getDbUnitsPerMicron());
        };

        auto get_width = [&](dbInst* cell) {
            return cell->getMaster()->getWidth();
        };

        auto get_pos = [&](dbInst* cell) -> Point {
            int x, y;
            cell->getOrigin(x, y);
            return Point(x, y);
        };

        // move to closest leftmost positions of the rows
        int widest_width = get_width(*std::max_element(cells.begin(), cells.end(),
            [&](dbInst* cell1, dbInst* cell2) {
                return get_width(cell1) < get_width(cell2);
            }
        ));

        auto effective_x = [&](dbInst* cell){
            // +width -> +priority
            return get_pos(cell).x() - get_width(cell) * width_factor;
        };
        std::sort(cells.begin(), cells.end(), 
            [&](dbInst* cell1, dbInst* cell2){
                return effective_x(cell1) < effective_x(cell2);
            }
        );

        int max_col = block->getBBox()->xMax();
        auto cell_can_fit_here = [&](dbInst* cell, int col, dbRow* row) -> bool {
            // check if cell is in bounds
            if (col + get_width(cell) > max_col) return false;

            // check if cell is not colliding with other placed cell
            for (dbInst* other = *cells.begin(); other != cell; other++) {
                if (get_pos(other).y() == row_to_y(row)) {
                    // same row
                    int x1 = col;
                    int x2 = get_pos(other).x();
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
            int target_x = get_pos(cell).x() - left_factor * widest_width;
            int target_y = get_pos(cell).y();
            int lowest_cost = INT_MAX;

            dbRow* winning_row = 0;
            int winning_col = 0;

            for (dbRow* row : rows) {
                for (int col = target_x; col < max_col; col++) {
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
            if (lowest_cost == INT_MAX) {
                fprintf(stderr, "ERROR: could not place cell\n");
                continue;
            }
            cell->setOrigin(winning_col, row_to_y(winning_row));
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


