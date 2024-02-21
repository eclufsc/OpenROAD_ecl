#include "misc/MakeMisc.h"
#include "misc/Misc.h"
#include "ord/OpenRoad.hh"
#include "sta/StaMain.hh"

namespace sta {
// Tcl files encoded into strings.
extern const char* misc_tcl_inits[];
}  // namespace sta

//Rule: Misc class -> Misc_Init
//So, the module name in .i have to equal the Class name,
//although .i is considered case insensitive.
extern "C" {
extern int Misc_Init(Tcl_Interp* interp);
}

//All these three functions are being used in /src/OpenRoad.cc
namespace ord {

misc::Misc * makeMisc()
{
  return new misc::Misc();
}

//This funcion will bind the calls between .tcl and .i files
void
initMisc(OpenRoad *openroad)
{
  Tcl_Interp* tcl_interp = openroad->tclInterp();
  // Define swig TCL commands.
  Misc_Init(tcl_interp);
  sta::evalTclInit(tcl_interp, sta::misc_tcl_inits);
}

void
deleteMisc(misc::Misc *misc)
{
  delete misc;
}

}
