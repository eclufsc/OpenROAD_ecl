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
rcm_time_move()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->RcmTimeMove();
}

void
report_cells_slack()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->reportCellSlack();
}

void
report_wl_2_slack_ratio(const char * file_name)
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->reportWl2SlackRatio(file_name);
}

void
report_candidate_cells(const char * file_name)
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->reportCandidateCells(file_name);
}

void
move_specific_cell()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->moveSpecificCell();
}

void
test_init_cells_wight()
{
  CellMoveRouter* cellMoveRouter = getCellMoveRouter();
  cellMoveRouter->InitCellsWeight();
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

} // namespace

%} // inline

