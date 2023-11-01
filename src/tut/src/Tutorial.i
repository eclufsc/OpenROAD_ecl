%{
#include "odb/db.h"
#include "utl/Logger.h"
#include "ord/OpenRoad.hh"
#include "tut/Tutorial.h"

namespace ord {
tut::Tutorial* getTutorial(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getTutorial;
using tut::Tutorial;
%}

%inline %{

namespace tut {

void destroy_cells_with_name_prefix(const char* prefix) {
  Tutorial* tutorial = getTutorial();
  tutorial->destroy_cells_with_name_prefix(prefix);
}

void dump_lowest_costs(const char* file_path) {
  Tutorial* tutorial = getTutorial();
  tutorial->dump_lowest_costs(file_path);
}

void move_x(const char* cell_name, int delta_x) {
  Tutorial* tutorial = getTutorial();
  if (!tutorial->move_x(cell_name, delta_x)) {
    tutorial->logger->report("Failed to move cell");
  }
}

void xy_microns_to_dbu(double x, double y) {
  Tutorial* tutorial = getTutorial();
  auto [dbu_x, dbu_y] = tutorial->xy_microns_to_dbu(x, y);
  tutorial->logger->report(std::to_string(dbu_x) + " " + std::to_string(dbu_y));
}

void abacus() {
  Tutorial* tutorial = getTutorial();
  tutorial->abacus();
}

void abacus(int x1, int y1, int x2, int y2) {
  Tutorial* tutorial = getTutorial();
  tutorial->abacus(x1, y1, x2, y2);
}


void is_legalized() {
  Tutorial* tutorial = getTutorial();
  auto [ok, reason] = tutorial->is_legalized();

  if (ok) {
    tutorial->logger->report("The area is legalized");
  } else {
    tutorial->logger->report("The area is not legalized: " + reason);
  }
}

void is_legalized(int x1, int y1, int x2, int y2) {
  Tutorial* tutorial = getTutorial();
  auto [ok, reason] = tutorial->is_legalized(x1, y1, x2, y2);

  if (ok) {
    tutorial->logger->report("The area is legalized");
  } else {
    tutorial->logger->report("The area is not legalized: " + reason);
  }
}

void is_legalized_excluding_border(int x1, int y1, int x2, int y2) {
  Tutorial* tutorial = getTutorial();
  auto [ok, reason] = tutorial->is_legalized_excluding_border(x1, y1, x2, y2);

  if (ok) {
    tutorial->logger->report("The area is legalized");
  } else {
    tutorial->logger->report("The area is not legalized: " + reason);
  }
}

void test() {
  Tutorial* tutorial = getTutorial();
  tutorial->test();
}

void shuffle() {
  Tutorial* tutorial = getTutorial();
  tutorial->shuffle();
}

void disturb() {
  Tutorial* tutorial = getTutorial();
  tutorial->disturb();
}

void tetris() {
  Tutorial* tutorial = getTutorial();
  tutorial->tetris();
}

void tetris(int area_x1, int area_y1, int area_x2, int area_y2) {
  Tutorial* tutorial = getTutorial();
  tutorial->tetris(area_x1, area_y1, area_x2, area_y2);
}

} // namespace

%} // inline
