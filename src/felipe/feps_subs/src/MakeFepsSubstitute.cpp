#include "feps_subs/MakeFepsSubstitute.h"
#include "feps_subs/FepsSubstitute.h"
#include "ord/OpenRoad.hh"
#include "sta/StaMain.hh"

namespace sta {
// Tcl files encoded into strings.
extern const char* feps_subs_tcl_inits[];
}  // namespace sta

//Rule: FepsSubstitute class -> FepsSubstitute_Init
//So, the module name in .i have to equal the Class name,
//although .i is considered case insensitive.
extern "C" {
extern int FepsSubs_Init(Tcl_Interp* interp);
}

//All these three functions are being used in /src/OpenRoad.cc
namespace ord {

feps_subs::FepsSubstitute * makeFepsSubstitute()
{
  return new feps_subs::FepsSubstitute();
}

//This funcion will bind the calls between .tcl and .i files
void
initFepsSubstitute(OpenRoad *openroad)
{
  Tcl_Interp* tcl_interp = openroad->tclInterp();
  // Define swig TCL commands.
  FepsSubs_Init(tcl_interp);
  sta::evalTclInit(tcl_interp, sta::feps_subs_tcl_inits);
}

void
deleteFepsSubstitute(feps_subs::FepsSubstitute *feps_substitute)
{
  delete feps_substitute;
}

}
