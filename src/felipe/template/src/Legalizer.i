%{
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "leg/Legalizer.h"

namespace ord {
leg::Legalizer* getLegalizer(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getLegalizer;
using leg::Legalizer;
%}

%inline %{

namespace leg {

void log()
{
    Legalizer* legalizer = getLegalizer();
    legalizer->log();
}
} // namespace

%} // inline
