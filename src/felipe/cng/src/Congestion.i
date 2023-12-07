%{
#include "odb/db.h"
#include "gui/gui.h"
#include "utl/Logger.h"
#include "ord/OpenRoad.hh"
#include "cng/Congestion.h"

namespace ord {
cng::Congestion* getCongestion(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getCongestion;
using cng::Congestion;
%}

%inline %{

namespace cng {

void routing() {
    Congestion* congestion = getCongestion();
    congestion->routing();
}

} // namespace

%} // inline
