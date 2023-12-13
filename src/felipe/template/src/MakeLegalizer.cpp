#include "leg/MakeLegalizer.h"
#include "leg/Legalizer.h"
#include "ord/OpenRoad.hh"
#include "sta/StaMain.hh"

namespace sta {
// Tcl files encoded into strings.
extern const char* leg_tcl_inits[];
}  // namespace sta

//Rule: Legalizer class -> Legalizer_Init
//So, the module name in .i have to equal the Class name,
//although .i is considered case insensitive.
extern "C" {
extern int Leg_Init(Tcl_Interp* interp);
}

//All these three functions are being used in /src/OpenRoad.cc
namespace ord {

leg::Legalizer * makeLegalizer()
{
  return new leg::Legalizer();
}

//This funcion will bind the calls between .tcl and .i files
void
initLegalizer(OpenRoad *openroad)
{
  Tcl_Interp* tcl_interp = openroad->tclInterp();
  // Define swig TCL commands.
  Leg_Init(tcl_interp);
  sta::evalTclInit(tcl_interp, sta::leg_tcl_inits);
}

void
deleteLegalizer(leg::Legalizer *legalizer)
{
  delete legalizer;
}

}
