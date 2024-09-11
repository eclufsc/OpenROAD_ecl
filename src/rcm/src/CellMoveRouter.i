%{
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "rcm/CellMoveRouter.h"

namespace ord {
rcm::CellMoveRouter* getCellMoveRouter(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getCellMoveRouter;
using rcm::CellMoveRouter;
%}

%inline %{

namespace rcm {

void
helloWorld()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->helloWorld();
}

void
drawRect(int x1, int y1, int x2, int y2)
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->drawRectangle(x1, y1, x2, y2);
}

void
move_rerout()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->Cell_Move_Rerout();
}

void
test_init_cells_wight()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->InitCellsWeight();
}

void
run_cmro()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->RunCMRO();
}

void
select_cells_by_net()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->SelectCellsToMove();
}

void
test_revert()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->testRevertingRouting();
}

void
run_abacus()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->runAbacus();
}

void
shuffle()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->shuffleAbacus();
}

void
set_debug_cmd(bool debug)
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->set_debug(debug);
}

void
report_nets_pins()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->report_nets_pins();
}


} // namespace

%} // inline

