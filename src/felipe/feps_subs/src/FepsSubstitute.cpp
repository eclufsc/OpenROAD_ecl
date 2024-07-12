#include "feps_subs/FepsSubstitute.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

#include <iostream>

namespace feps_subs {

FepsSubstitute::FepsSubstitute() :
  db{ord::OpenRoad::openRoad()->getDb()},
  logger{ord::OpenRoad::openRoad()->getLogger()}
{}

FepsSubstitute::~FepsSubstitute() {}

void FepsSubstitute::log() {
    logger->report("log");
}

}
