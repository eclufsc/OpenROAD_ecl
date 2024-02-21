#include "misc/Misc.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

namespace misc {
    using namespace odb;

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
        shuffle(core.xMin(), core.yMin(), core.xMax(), core.yMax());
    }

    void Misc::shuffle(int x1, int y1, int x2, int y2) {
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
}

