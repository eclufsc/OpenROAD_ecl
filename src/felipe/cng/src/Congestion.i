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

void update_routing_heatmap() {
    Congestion* congestion = getCongestion();
    congestion->update_routing_heatmap();
}

void test(char const* name) {
    Congestion* congestion = getCongestion();
    congestion->test(name);
}

void undraw() {
    Congestion* congestion = getCongestion();
    congestion->undraw();
}

} // namespace

%} // inline
