#include "rcm/MakeCellMoveRouter.h"
#include "rcm/CellMoveRouter.h"
#include "ord/OpenRoad.hh"
#include "sta/StaMain.hh"
#include "tcl.h"
#include "utl/decode.h"


extern "C" {
extern int Rcm_Init(Tcl_Interp* interp);
}

namespace rcm {

// Tcl files encoded into strings.
extern const char* rcm_tcl_inits[];

void initCellMoveRouter(Tcl_Interp* tcl_interp)
{
  // Define swig TCL commands.
  Rcm_Init(tcl_interp);
  utl::evalTclInit(tcl_interp, rcm::rcm_tcl_inits);
}

}  // namespace rcm
