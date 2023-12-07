#include "drw/MakeDrawing.h"
#include "drw/Drawing.h"
#include "ord/OpenRoad.hh"
#include "sta/StaMain.hh"

namespace sta {
// Tcl files encoded into strings.
extern const char* drw_tcl_inits[];
}  // namespace sta

//Rule: Drawing class -> Drawing_Init
//So, the module name in .i have to equal the Class name,
//although .i is considered case insensitive.
extern "C" {
extern int Drw_Init(Tcl_Interp* interp);
}

//All these three functions are being used in /src/OpenRoad.cc
namespace ord {

drw::Drawing * makeDrawing()
{
  return new drw::Drawing();
}

//This funcion will bind the calls between .tcl and .i files
void
initDrawing(OpenRoad *openroad)
{
  Tcl_Interp* tcl_interp = openroad->tclInterp();
  // Define swig TCL commands.
  Drw_Init(tcl_interp);
  sta::evalTclInit(tcl_interp, sta::drw_tcl_inits);
}

void
deleteDrawing(drw::Drawing *drawing)
{
  delete drawing;
}

}
