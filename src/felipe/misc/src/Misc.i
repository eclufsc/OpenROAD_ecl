%{
#include "odb/db.h"
#include "gui/gui.h"
#include "utl/Logger.h"
#include "ord/OpenRoad.hh"
#include "misc/Misc.h"

namespace ord {
misc::Misc* getMisc(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getMisc;
using misc::Misc;
%}

%inline %{

namespace misc {

void destroy_cells_with_name_prefix(const char* prefix) {
  Misc* misc = getMisc();
  misc->destroy_cells_with_name_prefix(prefix);
}

/*
void translate_mi(const char* cell_name, int delta_x_mi, int delta_y_mi) {
  Misc* misc = getMisc();

  int delta_x = misc->microns_to_dbu(delta_x_mi);
  int delta_y = misc->microns_to_dbu(delta_y_mi);

  if (!misc->translate(cell_name, delta_x, delta_y)) {
    misc->logger->report("Failed to move cell");
  }
}

void xy_microns_to_dbu(double x, double y) {
  Misc* misc = getMisc();
  auto [dbu_x, dbu_y] = misc->xy_microns_to_dbu(x, y);
  misc->logger->report(std::to_string(dbu_x) + " " + std::to_string(dbu_y));
}
*/

void shuffle() {
  Misc* misc = getMisc();
  misc->shuffle();
}

void shuffle(int x1, int y1, int x2, int y2) {
  Misc* misc = getMisc();
  misc->shuffle(x1, y1, x2, y2);
}

void disturb() {
  Misc* misc = getMisc();
  misc->disturb();
}

} // namespace

%} // inline
