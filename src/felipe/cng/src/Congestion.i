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

// todo
void test() {
    Congestion* congestion = getCongestion();
    congestion->test("inst1");
}

void undraw() {
    Congestion* congestion = getCongestion();
    congestion->undraw();
}

} // namespace

%} // inline
