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

void update_routing_heatmap(char const* layer_name) {
    Congestion* congestion = getCongestion();
    congestion->update_routing_heatmap(layer_name);
}

// todo
void test() {
    Congestion* congestion = getCongestion();
    congestion->test("inst1");
}

void test2(char const* layer_name) {
    Congestion* congestion = getCongestion();
    congestion->test2(layer_name);
}

void undraw() {
    Congestion* congestion = getCongestion();
    congestion->undraw();
}

} // namespace

%} // inline
