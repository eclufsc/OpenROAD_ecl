#include "cng/MakeCongestion.h"
#include "cng/Congestion.h"
#include "ord/OpenRoad.hh"
#include "sta/StaMain.hh"

namespace sta {
// Tcl files encoded into strings.
extern const char* cng_tcl_inits[];
}  // namespace sta

//Rule: Congestion class -> Congestion_Init
//So, the module name in .i have to equal the Class name,
//although .i is considered case insensitive.
extern "C" {
extern int Cng_Init(Tcl_Interp* interp);
}

//All these three functions are being used in /src/OpenRoad.cc
namespace ord {

cng::Congestion * makeCongestion()
{
  return new cng::Congestion();
}

//This funcion will bind the calls between .tcl and .i files
void
initCongestion(OpenRoad *openroad)
{
  Tcl_Interp* tcl_interp = openroad->tclInterp();
  // Define swig TCL commands.
  Cng_Init(tcl_interp);
  sta::evalTclInit(tcl_interp, sta::cng_tcl_inits);
}

void
deleteCongestion(cng::Congestion *congestion)
{
  delete congestion;
}

}
