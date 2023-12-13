#include "leg/Legalizer.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

#include <iostream>

namespace leg {

Legalizer::Legalizer() :
  db{ord::OpenRoad::openRoad()->getDb()},
  logger{ord::OpenRoad::openRoad()->getLogger()}
{}

Legalizer::~Legalizer() {}

void Legalizer::log() {
    logger->report("log");
}

}
