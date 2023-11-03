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

namespace tut {
    using namespace odb;
    using std::vector, std::pair, std::sort, std::lower_bound, std::upper_bound, std::move, std::string, std::make_pair;
    using std::numeric_limits;

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
        vector<pair<Rect, dbInst*>> cells_and_insts;
        {
            dbSet<dbInst> cells_set = block->getInsts();
            for (dbInst* cell : cells_set) {
                Rect rect = cell->getBBox()->getBox();
                cells_and_insts.push_back({rect, cell});
            }
            std::sort(cells_and_insts.begin(), cells_and_insts.end(),
                [&](pair<Rect, dbInst*> const& a, pair<Rect, dbInst*> const& b) {
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

        dbSet<dbInst> insts = block->getInsts();
        for (dbInst* inst : insts) {
            if (inst->isFixed()) continue;

            Rect cell = inst->getBBox()->getBox();
            
            while (true) {
                cell.moveTo(
                    core.xMin() + rand() % core.dx(),
                    core.yMin() + rand() % core.dy()
                );

                if (cell.xMax() <= core.xMax()
                    && cell.yMax() <= core.yMax()
                ) {
                    break;
                }
            }

            set_pos(inst, cell.xMin(), cell.yMin(), false);
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

    int Tutorial::max_clusters = 0;

    void Tutorial::abacus() {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        vector<pair<Rect, dbInst*>> cells;
        {
            dbSet<dbInst> cells_set = block->getInsts();
            for (dbInst* cell : cells_set) {
                Rect rect = cell->getBBox()->getBox();
                cells.push_back({rect, cell});
            }
        }

        vector<pair<Rect, int>> rows;
        {
            dbSet<dbRow> rows_set = block->getRows();
            for (dbRow* row : rows_set) {
                Rect rect = row->getBBox();
                int site_width = row->getSite()->getWidth();
                rows.push_back({rect, site_width});
            }
        }

        abacus(move(rows), move(cells));
    }

    void Tutorial::abacus(int x1, int y1, int x2, int y2) {
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
            return;
        }

        vector<pair<Rect, int>> rows;
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
        }

        double total_area = 0;
        vector<pair<Rect, dbInst*>> cells;
        {
            dbSet<dbInst> cells_set = block->getInsts();
            for (dbInst* cell : cells_set) {
                Rect rect = cell->getBBox()->getBox();

                int x_min = rect.xMin();
                int x_max = rect.xMax();
                int y_min = rect.yMin();
                int y_max = rect.yMax();

                int x2_min = area_x_min + rect.dx()/2;
                int x2_max = area_x_max - rect.dx()/2;
                int y2_min = area_y_min + rect.dy()/2;
                int y2_max = area_y_max - rect.dy()/2;

                if (collide(x_min, x_max, x2_min, x2_max)
                    && collide(y_min, y_max, y2_min, y2_max)
                ) {
                    cells.push_back({rect, cell});
                    total_area += rect.dx() * rect.dy();
                }
            }
        }

        abacus(move(rows), move(cells));
    }

    void Tutorial::abacus(
        vector<pair<Rect, int>> rows_and_sites,
        vector<pair<Rect, dbInst*>> cells_and_insts
    ) {
        last_costs.clear();

        // todo: delete
        using std::chrono::high_resolution_clock, std::chrono::duration, std::chrono::duration_cast, std::chrono::milliseconds;
        auto start = high_resolution_clock::now();

        // cells
        vector<Rect> cells;
        vector<Rect> fixed_cells;
        vector<dbInst*> p_cells;
        {
            std::sort(
                cells_and_insts.begin(), cells_and_insts.end(),
                [&](pair<Rect, dbInst*> const& a, pair<Rect, dbInst*> const& b) {
                    return a.first.xMin() < b.first.xMin();
                }
            );

            for (int i = 0; i < cells_and_insts.size(); i++) {
                auto [cell, p_cell] = cells_and_insts[i];

                if (p_cell->isFixed()) {
                    fixed_cells.push_back(cell);
                } else {
                    cells.push_back(cell);
                    p_cells.push_back(p_cell);
                }
            }
        }

        // rows
        vector<Rect> rows(rows_and_sites.size());
        vector<int> sites_width(rows_and_sites.size());
        vector<int> rows_y(rows_and_sites.size());
        {
            sort(rows_and_sites.begin(), rows_and_sites.end(),
                [&](pair<Rect, int> const& a, pair<Rect, int> const& b) {
                    return a.first.yMin() < b.first.yMin();
                }
            );

            for (int i = 0; i < rows_and_sites.size(); i++) {
                Rect rect = rows_and_sites[i].first;
                int site_width = rows_and_sites[i].second;
                rows[i] = rect;
                sites_width[i] = site_width;
                rows_y[i] = rect.yMin();
            }

            // split rows colliding with fixed cells
            for (Rect fixed_cell : fixed_cells) {
                int row_start = std::lower_bound(rows_y.begin(), rows_y.end(), fixed_cell.yMin())
                    - rows_y.begin();

                int row_end_exc = std::lower_bound(rows_y.begin(), rows_y.end(), fixed_cell.yMax())
                    - rows_y.begin();

                int i = row_start;
                for (int count = row_start; count < row_end_exc; count++) {
                    Rect row = rows[i];
                    if (collide(fixed_cell.xMin(), fixed_cell.xMax(), row.xMin(), row.xMax())) {
                        Rect old_row = rows[i];
                        int site_width = sites_width[i];
                        int row_y = rows_y[i];

                        rows.erase(rows.begin() + i);
                        sites_width.erase(sites_width.begin() + i);
                        rows_y.erase(rows_y.begin() + i);

                        if (fixed_cell.xMax() < old_row.xMax()) {
                            Rect row_right(
                                fixed_cell.xMax(), old_row.yMin(),
                                row.xMax(), old_row.yMax()
                            );

                            rows.insert(rows.begin() + i, row_right);
                            sites_width.insert(sites_width.begin() + i, site_width);
                            rows_y.insert(rows_y.begin() + i, row_y);

                            i += 1;
                        }
                        if (old_row.xMin() < fixed_cell.xMin()) {
                            Rect row_left(
                                old_row.xMin(), old_row.yMin(),
                                fixed_cell.xMin(), old_row.yMax()
                            );

                            rows.insert(rows.begin() + i, row_left);
                            sites_width.insert(sites_width.begin() + i, site_width);
                            rows_y.insert(rows_y.begin() + i, row_y);

                            i += 1;
                        }
                    } else {
                        i += 1;
                    }
                }
            }
        }

        // algorithm
        vector<vector<int>> cells_per_row(rows.size());
        vector<vector<AbacusCluster>> clusters_per_row(rows.size());

        // todo: delete
        int last_percentage = 0;

        // todo: delete
        int max_loop_iter = 0;

        int fail_counter = 0;

        for (int cell_i = 0; cell_i < cells.size(); cell_i++) {
            Rect global_pos = cells[cell_i];

            int approx_row = std::lower_bound(
                rows_y.begin(), rows_y.end(), global_pos.yMin()
            )
            - rows_y.begin();

            if (approx_row == rows.size()) approx_row -= 1;

            double best_cost = std::numeric_limits<double>::max();
            int best_row_i = -1;
            AbacusCluster best_new_cluster;
            int best_previous_i = 0;

            auto loop_body = [&](int row_i) -> bool {
                Rect row = rows[row_i];

                double weight = global_pos.dx()*global_pos.dy();
                AbacusCell cell = {cell_i, global_pos, weight};

                AbacusCluster new_cluster;
                int previous_i;

                int site_width = sites_width[row_i];
                if (!abacus_try_add_cell(
                    row, site_width,
                    cell,
                    clusters_per_row[row_i],
                    &new_cluster, &previous_i
                )) {
                    return true;
                }

                int new_x = new_cluster.x + new_cluster.width - global_pos.dx();
                int new_y = row.yMin();

                double sqrt_x_cost = new_x - global_pos.xMin();
                double x_cost = sqrt_x_cost*sqrt_x_cost;
                double sqrt_y_cost = new_y - global_pos.yMin();
                double y_cost = sqrt_y_cost*sqrt_y_cost;

                if (y_cost > best_cost) return false;

                double curr_cost = x_cost + y_cost;

                if (curr_cost < best_cost) {
                    best_cost = curr_cost;
                    best_row_i = row_i;
                    best_new_cluster = new_cluster;
                    best_previous_i = previous_i;
                }

                return true;
            };

            // todo: delete
            int curr_iter = 0;

            for (int row_i = approx_row; row_i < rows.size(); row_i++) {
                if (!loop_body(row_i)) break;
                curr_iter++;
            }
            for (int row_i = approx_row-1; row_i >= 0; row_i--) {
                if (!loop_body(row_i)) break;
                curr_iter++;
            }

            if (curr_iter > max_loop_iter) max_loop_iter = curr_iter;

            if (best_row_i == -1) {
                fail_counter++;
                fprintf(stderr, "ERROR: could not place cell\n");
            } else {
                last_costs.emplace_back(dbu_to_microns(sqrt(best_cost)), p_cells[cell_i]);

                cells_per_row[best_row_i].push_back(cell_i);

                clusters_per_row[best_row_i].resize(best_previous_i+1);
                clusters_per_row[best_row_i].push_back(best_new_cluster);
            }

            int curr_percentage = ((cell_i+1)*10 / (int)cells.size())*10;
            if (curr_percentage > last_percentage) {
                logger->report(std::to_string(curr_percentage) + "% of the cells processed");
                last_percentage = curr_percentage;
            }

        }

        // todo: delete
        auto end = high_resolution_clock::now();
        logger->report("Time spent (ms): " + std::to_string(duration_cast<milliseconds>(end - start).count()));
        
        // checking whether the cells preserved relative ordering
        for (vector<int> const& row : cells_per_row) {
            int last_i = -1;
            for (int cell_i : row) {
                if (cell_i < last_i) {
                    logger->report("Order changed");
                }
                last_i = cell_i;
            }
        }

        for (int row_i = 0; row_i < cells_per_row.size(); row_i++) {
            vector<int> const& cells_in_row = cells_per_row[row_i];
            vector<AbacusCluster> const& clusters_in_row = clusters_per_row[row_i];

            int cell_i = 0;
            for (int cluster_i = 0; cluster_i < clusters_in_row.size(); cluster_i++) {
                AbacusCluster const& cluster = clusters_in_row[cluster_i];

                int x = cluster.x;
                while (cell_i <= cluster.last_cell) {
                    int p_cell_i = cells_in_row[cell_i];
                    set_pos(p_cells[p_cell_i], x, rows[row_i].yMin(), true);
                    x += cells[p_cell_i].dx();

                    cell_i++;
                }
            }
        }

        if (fail_counter == 0) {
            logger->report("Placed all cells");
        } else {
            logger->report("Could not place " + std::to_string(fail_counter) + " cells");
        }

        printf("max_loop_iter = %d\n", max_loop_iter);
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
            || clusters.back().x + clusters.back().width <= cell.global_pos.xMin()
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

    void Tutorial::tetris() {
        // todo: delete
        setbuf(stdout, 0);

        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        vector<pair<Rect, dbInst*>> cells;
        vector<Rect> fixed_cells;
        {
            dbSet<dbInst> cells_set = block->getInsts();
            for (dbInst* inst : cells_set) {
                Rect rect = inst->getBBox()->getBox();
                if (inst->isFixed()
                    || cells_legalized.find(inst) != cells_legalized.end()
                ) {
                    fixed_cells.push_back(rect);
                } else {
                    cells.push_back({rect, inst});
                }
            }
        }

        vector<pair<Rect, int>> rows;
        {
            dbSet<dbRow> rows_set = block->getRows();
            for (dbRow* row : rows_set) {
                Rect rect = row->getBBox();
                int site_width = row->getSite()->getWidth();
                rows.push_back({rect, site_width});
            }

            sort_and_split_rows(&rows, fixed_cells);
        }

        tetris(move(rows), move(cells));
    }

    void Tutorial::tetris(int x1, int y1, int x2, int y2, bool include_boundary) {
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
            return;
        }

        vector<pair<Rect, dbInst*>> cells;
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
                } else {
                    if (cells_legalized.find(inst) != cells_legalized.end()) {
                        fixed_cells.push_back(rect);
                    }
                }
            }
        }

        vector<pair<Rect, int>> rows;
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

            sort_and_split_rows(&rows, fixed_cells);
        }

        tetris(move(rows), move(cells));
    }

    void Tutorial::tetris(
        vector<pair<Rect, int>> rows_and_sites,
        vector<pair<Rect, dbInst*>> cells_and_insts
    ) {
        // todo: delete
        using std::chrono::high_resolution_clock, std::chrono::duration, std::chrono::duration_cast, std::chrono::milliseconds;
        auto start = high_resolution_clock::now();

        last_costs.clear();

        // todo: delete
        setbuf(stdout, 0);

        using std::deque;

        // todo: move this to a struct (or receive in the arguments)
        float left_factor = 1.0; 
        float width_factor = 0.5;
        float x_to_y_priority_ratio = 1.0f;

        // sort cells_and_insts by x
        auto effective_x = [&](Rect const& cell) -> double {
            // +width -> +priority
            return cell.xMin() - cell.dx() * width_factor;
        };
        std::sort(cells_and_insts.begin(), cells_and_insts.end(), 
            [&](pair<Rect, dbInst*> const& cell1, pair<Rect, dbInst*> const& cell2){
                return effective_x(cell1.first) < effective_x(cell2.first);
            }
        );

        // sort rows_and_sites by y
        std::sort(rows_and_sites.begin(), rows_and_sites.end(),
            [&](pair<Rect, int> const& a, pair<Rect, int> const& b) {
                return a.first.yMin() < b.first.yMin();
            }
        );

        // max_width
        int max_width = (*std::max_element(cells_and_insts.begin(), cells_and_insts.end(),
            [&](pair<Rect, dbInst*> cell1, pair<Rect, dbInst*> cell2) {
                return cell1.first.dx() < cell2.first.dx();
            }
        ))
            .first.dx();

        // initialize last_placed_per_row
        // note: using deque instead of queue because queue doesnt support iteration
        // note: it is possible to substitute the queue for a vector with two indexes (WindowVector). All cells_and_insts in the current row would be added beforehand. The pop_front would increment the start index; the push_back would increment the end index. I believe this is a better alternative due to its simplicity. Maybe it's possible to merge last_placed_per_row and fixed_per_row in a single data structure
        vector<deque<Rect>> last_placed_per_row(rows_and_sites.size());

        int not_placed_n = 0;

        // debug_data
        debug_data.max_deque_size = 0;
        debug_data.max_row_iter = 0;
        debug_data.max_site_iter = 0;

        debug_data.cell_iter = 0;

        debug_data.max_site_iter_site = 0;
        debug_data.max_site_iter_last_placed_site = vector<int>();
        debug_data.max_site_iter_cell.clear();

        debug_data.lowest_costs.resize(cells_and_insts.size());

        // note: cells_and_insts cannot move too much (like tetris). The left factor determines the fall speed
        int delta_x = -left_factor * max_width;
        int last_percentage = 0;
        for (int cell_i = 0; cell_i < cells_and_insts.size(); cell_i++) {
            Rect& cell = cells_and_insts[cell_i].first;

            debug_data.curr_cell_name = cells_and_insts[cell_i].second->getName();

            debug_data.cell_iter++;
            debug_data.row_iter = 0;

            int orig_x = cell.xMin();
            int orig_y = cell.yMin();
            int target_x = orig_x + delta_x;
            int target_y = orig_y;

            double lowest_cost = std::numeric_limits<double>::max();

            int winning_row = 0;
            int winning_site_x = 0;

            auto loop_iter = [&](int row_i) -> bool {
                auto const& row_and_site = rows_and_sites[row_i];
                Rect const& row = row_and_site.first;
                int site_width = row_and_site.second;

                double sqrt_cost_y = row.yMin() - target_y;
                double cost_y = sqrt_cost_y*sqrt_cost_y;

                if (cost_y > lowest_cost) return false;

                int site_x = tetris_try_to_place_in_row(
                    row, site_width,
                    cell, target_x,
                    last_placed_per_row[row_i]
                );

                if (site_x == row.xMax()) return true;

                double sqrt_cost_x = site_x - target_x;
                double cost_x = sqrt_cost_x*sqrt_cost_x;
                double cost = cost_x + x_to_y_priority_ratio * cost_y;
                if (cost < lowest_cost) {
                    lowest_cost = cost;
                    winning_row = row_i;
                    winning_site_x = site_x;
                }

                return true;
            };

            int approx_row = std::lower_bound(rows_and_sites.begin(), rows_and_sites.end(), dummy_row_and_site(target_y),
                [&](pair<Rect, int> const& a, pair<Rect, int> const& b) {
                    return a.first.yMin() < b.first.yMin();
                }
            )
                - rows_and_sites.begin();

            if (approx_row == rows_and_sites.size()) approx_row--;

            for (int row_i = approx_row; row_i < rows_and_sites.size(); row_i++) {
                if (!loop_iter(row_i)) break;
            }
            for (int row_i = approx_row-1; row_i >= 0; row_i--) {
                if (!loop_iter(row_i)) break;
            }

            int curr_percentage = ((cell_i+1)*10 / (int)cells_and_insts.size())*10;
            if (curr_percentage > last_percentage) {
                logger->report(std::to_string(curr_percentage) + "% of the cells_and_insts processed");
                last_percentage = curr_percentage;
            }

            debug_data.lowest_costs[cell_i] = dbu_to_microns(sqrt(lowest_cost));

            if (lowest_cost == std::numeric_limits<double>::max()) {
                not_placed_n++;

                fprintf(stderr, "ERROR: could not place cell\n");
                continue;
            }

            last_costs.emplace_back(dbu_to_microns(sqrt(lowest_cost)), cells_and_insts[cell_i].second);

            int new_x = winning_site_x;
            int new_y = rows_and_sites[winning_row].first.yMin();
            cell.moveTo(new_x, new_y);

            deque<Rect>* last_placed = &last_placed_per_row[winning_row];
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

            if (last_placed->size() > debug_data.max_deque_size) {
                debug_data.max_deque_size = last_placed->size();
            }
            if (debug_data.row_iter > debug_data.max_row_iter) {
                debug_data.max_row_iter = debug_data.row_iter;
            }
        }

        // todo: delete
        auto end = high_resolution_clock::now();
        logger->report(std::to_string(duration_cast<milliseconds>(end - start).count()));

        for (auto const& [rect, p_cell] : cells_and_insts) {
            set_pos(p_cell, rect.xMin(), rect.yMin(), true);
        }

        if (not_placed_n == 0) {
            logger->report("Placed all cells_and_insts");
        } else {
            logger->report("Could not place " + std::to_string(not_placed_n) + " cells_and_insts");
        }
    }

    int Tutorial::tetris_try_to_place_in_row(
        Rect const& row, int site_width,
        Rect const& cell, int target_x,
        std::deque<Rect> const& last_placed
    ) {
        debug_data.row_iter++;
        debug_data.site_iter = 0; 

        int site_x = (target_x - row.xMin()) / site_width * site_width + row.xMin();
        if (site_x < row.xMin()) site_x = row.xMin();

        while (true) {
            debug_data.site_iter++;

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

            if (debug_data.site_iter > debug_data.max_site_iter) {
                debug_data.max_site_iter = debug_data.site_iter;
                debug_data.max_site_iter_site = (target_x - row.xMin()) / site_width;
                debug_data.max_site_iter_cell = debug_data.curr_cell_name;

                debug_data.max_site_iter_last_placed_site.clear();
                for (Rect const& curr_cell : last_placed) {
                    int x = curr_cell.xMax();
                    int curr_site_x = (x - row.xMin()) / site_width;
                    debug_data.max_site_iter_last_placed_site.push_back(curr_site_x);
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

    std::pair<Rect, int> Tutorial::dummy_row_and_site(int y_min) {
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

    void Tutorial::sort_and_split_rows(vector<pair<Rect, int>>* rows, vector<Rect> const& fixed_cells) {
        sort(rows->begin(), rows->end(),
            [&](pair<Rect, int> const& a, pair<Rect, int> const& b) {
                return a.first.yMin() < b.first.yMin();
            }
        );

        for (Rect const& fixed_cell : fixed_cells) {
            int row_start = std::lower_bound(rows->begin(), rows->end(), dummy_row_and_site(fixed_cell.yMin()),
                [&](pair<Rect, int> const& a, pair<Rect, int> const& b) {
                    return a.first.yMin() < b.first.yMin();
                }
            ) - rows->begin();

            int row_end_exc = std::lower_bound(rows->begin(), rows->end(), dummy_row_and_site(fixed_cell.yMax()),
                [&](pair<Rect, int> const& a, pair<Rect, int> const& b) {
                    return a.first.yMin() < b.first.yMin();
                }
            ) - rows->begin();

            int i = row_start;
            for (int count = row_start; count < row_end_exc; count++) {
                auto [row, site_width] = (*rows)[i];
                if (collide(fixed_cell.xMin(), fixed_cell.xMax(), row.xMin(), row.xMax())) {
                    rows->erase(rows->begin() + i);

                    if (row.xMin() < fixed_cell.xMin()) {
                        Rect row_left(
                            row.xMin(), row.yMin(),
                            fixed_cell.xMin(), row.yMax()
                        );
                        rows->insert(rows->begin() + i, {row_left, site_width});

                        i += 1;
                    }
                    if (fixed_cell.xMax() < row.xMax()) {
                        Rect row_right(
                            fixed_cell.xMax(), row.yMin(),
                            row.xMax(), row.yMax()
                        );
                        rows->insert(rows->begin() + i, {row_right, site_width});

                        i += 1;
                    }
                } else {
                    i += 1;
                }
            }
        }
    }
}

