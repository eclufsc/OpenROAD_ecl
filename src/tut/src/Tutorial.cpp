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

    void Tutorial::test() {
        std::cout<<"Printing all cell names:"<<std::endl;
        logger_->report("Printing all cell names:");

        auto block = db_->getChip()->getBlock();

        for (auto inst : block->getInsts()) {
            std::cout << inst->getName() << std::endl;
            logger_->report(inst->getName());
        }
    }
}
