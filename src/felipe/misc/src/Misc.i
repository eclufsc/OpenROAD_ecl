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

void get_free_spaces(int x1, int y1, int x2, int y2) {
  Misc* misc = getMisc();
  std::vector<int> free_spaces = misc->get_free_spaces(x1, y1, x2, y2);

  for (int free_space : free_spaces) {
    misc->logger->report(std::to_string(free_space));
  }
}

void destroy_cells_with_name_prefix(const char* prefix) {
  Misc* misc = getMisc();
  misc->destroy_cells_with_name_prefix(prefix);
}

void translate(const char* cell_name, int delta_x, int delta_y) {
  Misc* misc = getMisc();

  if (!misc->translate(cell_name, delta_x, delta_y)) {
    misc->logger->report("Failed to move cell");
  }
}

/*
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

void shuffle_to(int x1, int y1, int x2, int y2) {
  Misc* misc = getMisc();
  misc->shuffle_to(x1, y1, x2, y2);
}

void shuffle_in(int x1, int y1, int x2, int y2) {
  Misc* misc = getMisc();
  misc->shuffle_in(x1, y1, x2, y2);
}

void disturb() {
  Misc* misc = getMisc();
  misc->disturb();
}

void save_state() {
  Misc* misc = getMisc();
  misc->save_state();
}

void load_state() {
  Misc* misc = getMisc();
  misc->load_state();
}

void save_pos_to_file(const char* path) {
  Misc* misc = getMisc();
  misc->save_pos_to_file(path);
}

void load_pos_from_file(const char* path) {
  Misc* misc = getMisc();
  misc->load_pos_from_file(path);
}


} // namespace

%} // inline
