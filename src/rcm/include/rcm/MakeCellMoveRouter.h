#pragma once

#include "tcl.h"

namespace rcm{
class CellMoveRouter;
}

namespace odb{
class dbDatabase;
}

namespace rcm {


void initCellMoveRouter(Tcl_Interp* tcl_interp);

//void deleteCellMoveRouter(rcm::CellMoveRouter *cellMoveRouter);
}  // namespace ord
