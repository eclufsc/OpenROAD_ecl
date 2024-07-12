%{
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "dev/Dev.h"

namespace ord {
dev::Dev* getDev(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getDev;
using dev::Dev;
%}

%inline %{

namespace dev {

void log()
{
    Dev* dev = getDev();
    dev->log();
}

void global_route_and_print_vias() {
    Dev* dev = getDev();
    dev->global_route_and_print_vias();
}
} // namespace

%} // inline
