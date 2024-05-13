#include "misc/Misc.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

namespace misc {
    using namespace odb;
    using std::string, std::vector, std::pair;

    Misc::Misc() :
        db{ord::OpenRoad::openRoad()->getDb()},
        logger{ord::OpenRoad::openRoad()->getLogger()}
        {}

    Misc::~Misc() {}

    dbBlock* Misc::get_block() {
        dbChip* chip = db->getChip();
        if (chip) return chip->getBlock();
        else      return 0;
    }

    const char* Misc::error_message_from_get_block() {
        return "Block not available";
    }

    std::vector<int> Misc::get_free_spaces(int x1, int y1, int x2, int y2) {
        int area_x_min, area_x_max, area_y_min, area_y_max;
        if (x1 < x2) {
            area_x_min = x1;
            area_x_max = x2;
        } else {
            area_x_min = x2;
            area_x_max = x1;
        }
        if (y1 < y2) {
            area_y_min = y1;
            area_y_max = y2;
        } else {
            area_y_min = y2;
            area_y_max = y1;
        }

        odb::dbBlock* block = db->getChip()->getBlock();

        auto overlap = [&](int d1_min, int d1_max, int d2_min, int d2_max) -> bool {
            return d1_min < d2_max && d2_min < d1_max;
        };

        std::vector<std::tuple<odb::Rect, int>> rows;
        odb::dbSet<odb::dbRow> rows_set = block->getRows();
        for (odb::dbRow* row : rows_set) {
            odb::Rect rect = row->getBBox();

            if (   !overlap(rect.xMin(), rect.xMax(), area_x_min, area_x_max)
                || !overlap(rect.yMin(), rect.yMax(), area_y_min, area_y_max)
            ) {
                continue;
            }

            rows.emplace_back(row->getBBox(), row->getSite()->getWidth());
        }

        std::sort(rows.begin(), rows.end(),
            [&](std::tuple<odb::Rect, int>& a_, std::tuple<odb::Rect, int>& b_) {
                odb::Rect& a = std::get<0>(a_);
                odb::Rect& b = std::get<0>(b_);

                if (a.yMin() != b.yMin()) return a.yMin() < b.yMin();
                else                      return a.xMin() < b.xMin();
            }
        );

        auto get_limits = [&](std::tuple<odb::Rect, int>& row_and_site_width) -> std::tuple<int, int, int, int> {
            auto& [row, site_width] = row_and_site_width;

            int x_min_without_site_correction = std::max<int>(area_x_min, row.xMin());
            int x_min = (x_min_without_site_correction - row.xMin() + site_width - 1) / site_width * site_width + row.xMin();
            int x_max = std::min<int>(area_x_max, row.xMax());

            int y_min = std::max<int>(area_y_min, row.yMin());
            int y_max = std::min<int>(area_y_max, row.yMax());

            return {x_min, x_max, y_min, y_max};
        };

        std::vector<int> free_spaces;
        for (auto& row_and_site_width : rows) {
            auto [x_min, x_max, y_min, y_max] = get_limits(row_and_site_width);
            free_spaces.push_back(x_max - x_min);
        }

        odb::dbSet<odb::dbInst> insts_set = block->getInsts();
        for (odb::dbInst* inst : insts_set) {
            odb::Rect cell = inst->getBBox()->getBox();

            if (   !overlap(cell.xMin(), cell.xMax(), area_x_min, area_x_max)
                || !overlap(cell.yMin(), cell.yMax(), area_y_min, area_y_max)
            ) {
                continue;
            }

            for (int i = 0; i < rows.size(); i++) {
                auto [x_min, x_max, y_min, y_max] = get_limits(rows[i]);

                if (!overlap(cell.yMin(), cell.yMax(), y_min, y_max)) continue;

                int overlap = std::max<int>(
                    0,
                    std::min<int>(cell.xMax(), x_max) - std::max<int>(cell.xMin(), x_min)
                );

                free_spaces[i] -= overlap;
            }
        }

        return free_spaces;
    }

    std::tuple<Misc::Row, int, bool> Misc::find_available_pos(int moving_cell_width, int x1, int y1, int x2, int y2) {
        int area_x_min, area_x_max, area_y_min, area_y_max;
        if (x1 < x2) {
            area_x_min = x1;
            area_x_max = x2;
        } else {
            area_x_min = x2;
            area_x_max = x1;
        }
        if (y1 < y2) {
            area_y_min = y1;
            area_y_max = y2;
        } else {
            area_y_min = y2;
            area_y_max = y1;
        }

        odb::dbBlock* block = db->getChip()->getBlock();

        auto overlap = [&](int d1_min, int d1_max, int d2_min, int d2_max) -> bool {
            return d1_min < d2_max && d2_min < d1_max;
        };

        std::vector<Row> rows;
        odb::dbSet<odb::dbRow> rows_set = block->getRows();
        for (odb::dbRow* row : rows_set) {
            odb::Rect row_rect = row->getBBox();

            if (   !overlap(row_rect.xMin(), row_rect.xMax(), area_x_min, area_x_max)
                || !overlap(row_rect.yMin(), row_rect.yMax(), area_y_min, area_y_max)
            ) {
                continue;
            }

            int x_min = std::max<int>(area_x_min, row_rect.xMin());
            int y_min = std::max<int>(area_y_min, row_rect.yMin());
            int x_max = std::min<int>(area_x_max, row_rect.xMax());
            int y_max = std::min<int>(area_y_max, row_rect.yMax());

            Rect rect = Rect(x_min, y_min, x_max, y_max);
            int site_width = row->getSite()->getWidth();

            rows.emplace_back(rect, site_width);
        }

        odb::dbSet<odb::dbInst> insts_set = block->getInsts();
        vector<odb::Rect> fixed_cells;
        vector<odb::Rect> cells;
        for (odb::dbInst* inst : insts_set) {
            Rect rect = inst->getBBox()->getBox();
            if (inst->isFixed()) fixed_cells.push_back(rect);
            else                 cells.push_back(rect);
        }

        // Step 1: split by fixed_cells
        vector<Row> segments;
        {
            vector<vector<Split>> splits_per_row = sort_and_get_splits(&rows, fixed_cells);

            // Flatten list
            for (int i = 0; i < rows.size(); i++) {
                for (Split& split : splits_per_row[i]) {
                    Rect rect = {
                        split.first,
                        rows[i].first.yMin(),
                        split.second,
                        rows[i].first.yMax()
                    };
                    int site_width = rows[i].second;

                    segments.emplace_back(rect, site_width);
                }
            }
        }

        // Step 2: split by cells
        vector<vector<Split>> free_spaces_per_segment = sort_and_get_splits(&segments, cells);

        // Step 3: find largest free space
        int best_space = -1;
        int best_segment = -1;
        int best_x = -1;
        for (int i = 0; i < segments.size(); i++) {
            for (Split& free_space : free_spaces_per_segment[i]) {
                int curr_space = free_space.second - free_space.first;
                if (curr_space > best_space) {
                    best_space = curr_space;
                    best_segment = i;
                    best_x = free_space.first;
                }
            }
        }

        // Step 4: choose free space
        if (best_space >= moving_cell_width) {
            // Cell can fit in free space without legalizing
            return {segments[best_segment], best_x, true};
        } else {
            // Cell cannot fit in free space without legalizing
            // Check if is possible to place with legalization
            // (This assumes that the segment with the largest individual free space
            // probably has the biggest total free space)
            int total_space = 0;
            for (Split& free_space : free_spaces_per_segment[best_segment]) {
                total_space += free_space.second - free_space.first;
            }

            if (total_space >= moving_cell_width) {
                // NOTE: the best_x position probably moves the minimum amount of cells
                return {segments[best_segment], best_x, false};
            } else {
                // Error
                return {{}, 0, false};
            }
        }
    }

    auto Misc::sort_and_get_splits(
        vector<Row>* rows,
        vector<Rect> const& fixed_cells
    ) -> vector<vector<Split>> {
        sort(rows->begin(), rows->end(),
            [&](Row const& a, Row const& b) {
                return a.first.yMin() < b.first.yMin();
            }
        );

        vector<vector<Split>> splits_per_row(rows->size());
        for (int row_i = 0; row_i < rows->size(); row_i++) {
            Rect const& rect = (*rows)[row_i].first;
            splits_per_row[row_i].emplace_back(rect.xMin(), rect.xMax());
        }

        for (Rect const& fixed_cell : fixed_cells) {
            int int_min = std::numeric_limits<int>::min();
            int int_max = std::numeric_limits<int>::max();

            Rect dummy_row1 = Rect(
                int_min, int_min,
                int_max, fixed_cell.yMin()
            );
            int row_start = std::upper_bound(rows->begin(), rows->end(),
                std::make_pair(dummy_row1, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            ) - rows->begin();

            Rect dummy_row2 = Rect(
                int_min, int_min,
                int_max, fixed_cell.yMax()
            );
            int row_end_exc = std::upper_bound(rows->begin(), rows->end(),
                std::make_pair(dummy_row2, 0),
                [&](Row const& a, Row const& b) {
                    return a.first.yMax() < b.first.yMax();
                }
            ) - rows->begin();

            if (row_start == 0 && fixed_cell.yMax() <= rows->begin()->first.yMin()) continue;

            for (int row_i = row_start; row_i < row_end_exc; row_i++) {
                vector<Split>* splits = &splits_per_row[row_i];
                auto const& [row, site_width] = (*rows)[row_i];

                auto split = lower_bound(splits->begin(), splits->end(),
                    std::make_pair(0, fixed_cell.xMin()),
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
                    splits->insert(split, std::make_pair(new_x_max, old_x_max));
                }
                if (old_x_min < new_x_min) {
                    splits->insert(split, std::make_pair(old_x_min, new_x_min));
                }
            }
        }

        return splits_per_row;
    }
    
    void Misc::shuffle() {
        dbBlock* block = get_block();
        if (!block) {
            fprintf(stderr, "%s\n", error_message_from_get_block());
            return;
        }

        Rect core = block->getCoreArea();
        shuffle_to(core.xMin(), core.yMin(), core.xMax(), core.yMax());
    }

    void Misc::shuffle_in(int x1, int y1, int x2, int y2) {
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

            if (cell.xMin() < x_min || cell.xMax() > x_max
                || cell.yMin() < y_min || cell.yMax() > y_max) continue;

            int new_x = x_min + rand() % (dx - cell.dx());
            int new_y = y_min + rand() % (dy - cell.dy());

            inst->setLocation(new_x, new_y);
        }
    }

    void Misc::shuffle_to(int x1, int y1, int x2, int y2) {
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

            inst->setLocation(new_x, new_y);
        }
    }

    void Misc::disturb() {
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

            inst->setLocation(new_x, new_y);
        }
    }

    bool Misc::collide(int pos1_min, int pos1_max, int pos2_min, int pos2_max) {
        return pos1_min < pos2_max && pos2_min < pos1_max;
    };

    void Misc::destroy_cells_with_name_prefix(std::string prefix) {
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

    bool Misc::translate(std::string cell_name, int delta_x, int delta_y) {
        dbBlock* block = get_block();
        if (!block) return false;

        dbSet<dbInst> cells = block->getInsts();

        dbInst* cell = 0;
        for (dbInst* curr_cell : cells) {
            if (curr_cell->getName() == cell_name) {
                cell = curr_cell;
            }
        }

        if (!cell) return false;

        int x = cell->getBBox()->xMin();
        int y = cell->getBBox()->yMin();
        cell->setLocation(x + delta_x, y + delta_y);

        return true;
    }

    void Misc::save_state() {
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
    }

    void Misc::load_state() {
        for (auto const& [cell, inst] : saved_state.pos) {
            inst->setLocation(cell.xMin(), cell.yMin());
        }
    }

    void Misc::save_pos_to_file(string path) {
        save_state();

        std::ofstream file(path);
        for (auto const& [cell, inst] : saved_state.pos) {
            file << inst->getName() << " " << cell.xMin() << " " << cell.yMin() << "\n";
        }
    }

    void Misc::load_pos_from_file(string path) {
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

}

