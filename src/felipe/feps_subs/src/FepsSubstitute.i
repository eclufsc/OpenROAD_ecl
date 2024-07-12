%{
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "feps_subs/FepsSubstitute.h"

namespace ord {
feps_subs::FepsSubstitute* getFepsSubstitute(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getFepsSubstitute;
using feps_subs::FepsSubstitute;
%}

%inline %{

namespace feps_subs {

void log()
{
    FepsSubstitute* feps_substitute = getFepsSubstitute();
    feps_substitute->log();
}
} // namespace

%} // inline
