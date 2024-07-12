#include "dev/Dev.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "grt/GlobalRouter.h"

#include <iostream>

namespace dev {
    Dev::Dev() :
      db{ord::OpenRoad::openRoad()->getDb()},
      logger{ord::OpenRoad::openRoad()->getLogger()}
    {
//        OpenRoad* openroad = ord::OpenRoad::openRoad();
    }

    void Dev::log() {
        logger->report("log");
    }

    void Dev::global_route_and_print_vias() {
        grt::GlobalRouter* global_router = ord::OpenRoad::openRoad()->getGlobalRouter();
        global_router->globalRoute(true, false, false);
        logger->report("{}", global_router->getViaCount());
    }

    Dev::~Dev() {}
}
