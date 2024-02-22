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

