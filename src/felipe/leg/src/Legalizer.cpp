#if defined NDEBUG
#undef NDEBUG
#endif

#include "leg/Legalizer.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "odb/dbTypes.h"
#include "odb/dbTransform.h"

#include "gui/gui.h"

#include <limits>
#include <algorithm>
#include <deque>
#include <fstream>
#include <chrono>
#include <string>

// todo: is it better for tetris to split rows or check fixed_cells?

// todo: make is_legalized faster

namespace leg {
    using namespace odb;
    using std::vector, std::pair, std::sort, std::lower_bound, std::upper_bound, std::move, std::string, std::make_pair, std::tuple;
    using std::to_string;
    using std::numeric_limits;
    using std::chrono::high_resolution_clock, std::chrono::duration, std::chrono::duration_cast, std::chrono::milliseconds, std::chrono::nanoseconds;

    Legalizer::Legalizer() :
        logger{ord::OpenRoad::openRoad()->getLogger()},
        db{ord::OpenRoad::openRoad()->getDb()}
        {}

    Legalizer::~Legalizer() {}

    dbBlock* Legalizer::get_block() {
        dbChip* chip = db->getChip();
        if (chip) return chip->getBlock();
        else      return 0;
    }

    const char* Legalizer::error_message_from_get_block() {
        return "Block not available";
    }

    // note: conditions:
    // fixed cells are already legalized
    // fixed cells can occupy many rows
    // non-fixed cells occupy only one row
    // rows cannot have the same y
    std::pair<bool, std::string> Legalizer::is_legalized() {
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

    std::pair<bool, std::string> Legalizer::is_legalized(int x1, int y1, int x2, int y2) {
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

    std::pair<bool, std::string> Legalizer::is_legalized_excluding_border(int x1, int y1, int x2, int y2) {
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

    auto Legalizer::split_rows(
        vector<Row> const& rows,
        vector<Rect> const& fixed_cells
    ) -> vector<vector<Split>> {
        assert(is_sorted(rows.begin(), rows.end(),
            [&](Row const& a, Row const& b) {
                return a.first.yMin() < b.first.yMin();
            }
        ));

        assert(is_sorted(fixed_cells.begin(), fixed_cells.end(),
            [&](Rect const& a, Rect const& b) {
                return a.xMin() < b.xMin();
            }
        ));

        vector<vector<Split>> splits_per_row(rows.size());
        vector<Split> remaining_splits;
        for (auto const& [rect, site_width] : rows) {
            remaining_splits.emplace_back(rect.xMin(), rect.xMax());
        }

        for (Rect const& fixed_cell : fixed_cells) {
            int int_min = numeric_limits<int>::min();
            int int_max = numeric_limits<int>::max();

            Rect dummy_row1 = Rect(
                int_min, int_min,
                int_max, fixed_cell.yMin()
            );
            int row_start = std::upper_bound(rows.begin(), rows.end(),
                make_pair(dummy_row1, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            ) - rows.begin();

            Rect dummy_row2 = Rect(
                int_min, int_min,
                int_max, fixed_cell.yMax()
            );
            int row_end = std::lower_bound(rows.begin(), rows.end(),
                make_pair(dummy_row2, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            ) - rows.begin();

            if (row_end == rows.size()) row_end--;
            if (row_start == 0 && rows[row_start].first.yMin() >= fixed_cell.yMax()) continue;

            for (int row_i = row_start; row_i <= row_end; row_i++) {
                auto [old_x_min, old_x_max] = remaining_splits[row_i];

                if (old_x_max <= fixed_cell.xMin()) continue;

                auto const& [row, site_width] = rows[row_i];

                int mid_x_min =
                    (fixed_cell.xMin() - row.xMin())
                    / site_width * site_width + row.xMin();
                int mid_x_max = 
                    ((fixed_cell.xMax() - row.xMin()) + site_width-1)
                    / site_width * site_width + row.xMin();

                if (old_x_min < mid_x_min) {
                    splits_per_row[row_i].emplace_back(old_x_min, mid_x_min);
                }
                if (mid_x_max <= old_x_max) {
                    remaining_splits[row_i] = make_pair(mid_x_max, old_x_max);
                }
            }
        }

        for (int row_i = 0; row_i < rows.size(); row_i++) {
            Split const& split = remaining_splits[row_i];
            if (split.second - split.first > 0) {
                splits_per_row[row_i].push_back(split);
            }
        }

        return splits_per_row;
    }

    auto Legalizer::sort_and_get_splits(
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
            int int_min = numeric_limits<int>::min();
            int int_max = numeric_limits<int>::max();

            Rect dummy_row1 = Rect(
                int_min, int_min,
                int_max, fixed_cell.yMin()
            );
            int row_start = std::upper_bound(rows->begin(), rows->end(),
                make_pair(dummy_row1, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            ) - rows->begin();

            Rect dummy_row2 = Rect(
                int_min, int_min,
                int_max, fixed_cell.yMax()
            );
            int row_end = std::lower_bound(rows->begin(), rows->end(),
                make_pair(dummy_row2, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            ) - rows->begin();

            if (row_end == rows->size()) row_end--;

            for (int row_i = row_start; row_i <= row_end; row_i++) {
                vector<Split>* splits = &splits_per_row[row_i];
                auto const& [row, site_width] = (*rows)[row_i];

                auto split = lower_bound(splits->begin(), splits->end(),
                    make_pair(0, fixed_cell.xMin()),
                    [&](Split const& a, Split const& b) {
                        return a.second < b.second;
                    }
                );
                if (split == splits->end()) continue;

                auto [old_x_min, old_x_max] = *split;

                if (!collide(fixed_cell.xMin(), fixed_cell.xMax(), old_x_min, old_x_max)) {
                    continue;
                }

                int new_x_min =
                    (fixed_cell.xMin() - row.xMin())
                    / site_width * site_width + row.xMin();
                int new_x_max = 
                    ((fixed_cell.xMax() - row.xMin()) + site_width-1)
                    / site_width * site_width + row.xMin();

                splits->erase(split);

                if (new_x_max < old_x_max) {
                    splits->insert(split, make_pair(new_x_max, old_x_max));
                }
                if (old_x_min < new_x_min) {
                    splits->insert(split, make_pair(old_x_min, new_x_min));
                }
            }
        }

        return splits_per_row;
    }

    auto Legalizer::get_sorted_rows_splits_and_cells()
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
                if (inst->isFixed()) {
                    fixed_cells.push_back(rect);
                } else {
                    cells.push_back({rect, inst});
                }
            }

            sort(cells.begin(), cells.end(),
                [&](Cell const& a, Cell const& b) {
                    return a.first.xMin() < b.first.xMin();
                }
            );

            sort(fixed_cells.begin(), fixed_cells.end(),
                [&](Rect const& a, Rect const& b) {
                    return a.xMin() < b.xMin();
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

            sort(rows.begin(), rows.end(),
                [&](Row const& a, Row const& b) {
                    return a.first.yMin() < b.first.yMin();
                }
            );

            splits_per_row = split_rows(rows, fixed_cells);
        }

        return {rows, splits_per_row, cells};
    }

    auto Legalizer::get_sorted_rows_splits_and_cells(int x1, int y1, int x2, int y2, bool include_boundary)
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
                    fixed_cells.push_back(rect);
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

    void Legalizer::abacus() {
        auto [rows, splits_per_rows, cells] = get_sorted_rows_splits_and_cells();
        abacus(move(rows), move(splits_per_rows), move(cells));
    }

    void Legalizer::abacus(int x1, int y1, int x2, int y2, bool include_boundary) {
        auto [rows, splits_per_rows, cells] =
            get_sorted_rows_splits_and_cells(
                x1, y1, x2, y2, include_boundary
            );
        abacus(move(rows), move(splits_per_rows), move(cells));
    }

    void Legalizer::abacus_artur(int x1, int y1, int x2, int y2) {
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

                if (area_x_min <= rect.xMin() && rect.xMax() <= area_x_max
                    && area_y_min <= rect.yMin() && rect.yMax() <= area_y_max
                ) {
                    cells.push_back(make_pair(rect, inst));
                } else {
                    fixed_cells.push_back(rect);
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

        abacus(move(rows), move(splits_per_row), move(cells));
    }

    // todo: change && to const& for consistency (and because rows and cells should not be changed in this function
    // todo: check loop_x, loop_y and evaluate and change clusters and cells_per_row to consider split (solution: create new vector for row indices)
    void Legalizer::abacus(
        vector<Row>&& rows,
        vector<vector<Split>>&& splits_per_row,
        vector<Cell>&& cells
    ) {
        sort(cells.begin(), cells.end(),
            [&](Cell const& a, Cell const& b) {
                return a.first.xMin() < b.first.xMin();
            }
        );

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

        int fail_counter = 0;

        for (int cell_i = 0; cell_i < cells.size(); cell_i++) {
            auto const& [global_pos, inst] = cells[cell_i];

            double best_cost = std::numeric_limits<double>::max();
            int best_row_i = -1;
            int best_split_i = -1;
            AbacusCluster best_new_cluster;
            int best_previous_i = 0;

            auto evaluate = [&](int row_i, double y_cost, int split_i) {
                auto const& [whole_row, site_width] = rows[row_i];
                auto [x_min, x_max] = splits_per_row[row_i][split_i];
                Rect row(x_min, whole_row.yMin(), x_max, whole_row.yMax());

                double weight = global_pos.dx()*global_pos.dy();
                AbacusCell cell = {cell_i, global_pos, weight};

                AbacusCluster new_cluster;
                int previous_i;

                int accum_split_i = row_to_start_split[row_i] + split_i;
                if (!abacus_try_add_cell(
                    row, site_width,
                    cell,
                    clusters_per_accum_split[accum_split_i],
                    &new_cluster, &previous_i
                )) {
                    return;
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

            if (best_row_i == -1) {
                fail_counter++;
                fprintf(stderr, "ERROR: could not place cell %s\n", inst->getName().c_str());
            } else {
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
        }

        // todo: maybe delete
        auto end = high_resolution_clock::now();
        logger->report("Time spent (ms): " + std::to_string(duration_cast<milliseconds>(end - start).count()));
        
        // set_pos
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
        printf("\n");
    }

    bool Legalizer::abacus_try_add_cell(
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

        return true;
    }

    bool Legalizer::abacus_try_place_and_collapse(
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

    // todo: add splits_per_row to tetris
    void Legalizer::tetris() {
        auto [rows, splits_per_row, cells] = get_sorted_rows_splits_and_cells();
        tetris(move(rows), move(splits_per_row), move(cells));
    }

    void Legalizer::tetris(int x1, int y1, int x2, int y2, bool include_boundary) {
        auto [rows, splits_per_row, cells] = get_sorted_rows_splits_and_cells(x1, y1, x2, y2, include_boundary);
        tetris(move(rows), move(splits_per_row), move(cells));
    }

    void Legalizer::tetris(
        vector<Row>&& rows_and_sites,
        vector<vector<Split>>&& splits_per_row,
        vector<Cell>&& cells_and_insts
    ) {
        if (cells_and_insts.size() == 0) return;

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

            int curr_percentage = ((cell_i+1)*10 / (int)cells_and_insts.size())*10;
            if (curr_percentage > last_percentage) {
                logger->report(std::to_string(curr_percentage) + "% of the cells processed");
                last_percentage = curr_percentage;
            }

            if (lowest_cost == std::numeric_limits<double>::max()) {
                not_placed_n++;
                dbInst* inst = cells_and_insts[cell_i].second;
                fprintf(stderr, "ERROR: could not place cell %s\n", inst->getName().c_str());
                continue;
            }

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
    }

    int Legalizer::tetris_try_to_place_in_row(
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

    void Legalizer::set_pos(dbInst* cell, int x, int y, bool legalizing) {
        cell->setLocation(x, y);
    };

    int Legalizer::row_to_y(dbRow* row) {
        int x, y;
        row->getOrigin(x, y);
        return y;
    };

    bool Legalizer::collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max) {
        return pos1_min < pos2_max && pos2_min < pos1_max;
    };

}

