%{
#include "odb/db.h"
#include "gui/gui.h"
#include "utl/Logger.h"
#include "ord/OpenRoad.hh"
#include "leg/Legalizer.h"

namespace ord {
leg::Legalizer* getLegalizer(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getLegalizer;
using leg::Legalizer;
%}

%inline %{

namespace leg {

void destroy_cells_with_name_prefix(const char* prefix) {
  Legalizer* legalizer = getLegalizer();
  legalizer->destroy_cells_with_name_prefix(prefix);
}

void translate_mi(const char* cell_name, int delta_x_mi, int delta_y_mi) {
  Legalizer* legalizer = getLegalizer();

  int delta_x = legalizer->microns_to_dbu(delta_x_mi);
  int delta_y = legalizer->microns_to_dbu(delta_y_mi);

  if (!legalizer->translate(cell_name, delta_x, delta_y)) {
    legalizer->logger->report("Failed to move cell");
  }
}

void xy_microns_to_dbu(double x, double y) {
  Legalizer* legalizer = getLegalizer();
  auto [dbu_x, dbu_y] = legalizer->xy_microns_to_dbu(x, y);
  legalizer->logger->report(std::to_string(dbu_x) + " " + std::to_string(dbu_y));
}

void abacus() {
  Legalizer* legalizer = getLegalizer();
  legalizer->abacus();
}

void abacus_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Legalizer* legalizer = getLegalizer();

  int x1 = legalizer->microns_to_dbu(mi_x1);
  int y1 = legalizer->microns_to_dbu(mi_y1);
  int x2 = legalizer->microns_to_dbu(mi_x2);
  int y2 = legalizer->microns_to_dbu(mi_y2);

  legalizer->abacus(x1, y1, x2, y2);
}

void abacus_artur_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Legalizer* legalizer = getLegalizer();

  int x1 = legalizer->microns_to_dbu(mi_x1);
  int y1 = legalizer->microns_to_dbu(mi_y1);
  int x2 = legalizer->microns_to_dbu(mi_x2);
  int y2 = legalizer->microns_to_dbu(mi_y2);

  legalizer->abacus_artur(x1, y1, x2, y2);
}

void is_legalized() {
  Legalizer* legalizer = getLegalizer();
  auto [ok, reason] = legalizer->is_legalized();

  if (ok) {
    legalizer->logger->report("The area is legalized");
  } else {
    legalizer->logger->report("The area is not legalized: " + reason);
  }
}

void is_legalized_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Legalizer* legalizer = getLegalizer();

  int x1 = legalizer->microns_to_dbu(mi_x1);
  int y1 = legalizer->microns_to_dbu(mi_y1);
  int x2 = legalizer->microns_to_dbu(mi_x2);
  int y2 = legalizer->microns_to_dbu(mi_y2);

  auto [ok, reason] = legalizer->is_legalized(x1, y1, x2, y2);

  if (ok) {
    legalizer->logger->report("The area is legalized");
  } else {
    legalizer->logger->report("The area is not legalized: " + reason);
  }
}

void is_legalized_excluding_border_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Legalizer* legalizer = getLegalizer();

  int x1 = legalizer->microns_to_dbu(mi_x1);
  int y1 = legalizer->microns_to_dbu(mi_y1);
  int x2 = legalizer->microns_to_dbu(mi_x2);
  int y2 = legalizer->microns_to_dbu(mi_y2);

  auto [ok, reason] = legalizer->is_legalized_excluding_border(x1, y1, x2, y2);

  if (ok) {
    legalizer->logger->report("The area is legalized");
  } else {
    legalizer->logger->report("The area is not legalized: " + reason);
  }
}

void test() {
  Legalizer* legalizer = getLegalizer();
  legalizer->test();
}

void shuffle() {
  Legalizer* legalizer = getLegalizer();
  legalizer->shuffle();
}

void shuffle_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Legalizer* legalizer = getLegalizer();
  
  int x1 = legalizer->microns_to_dbu(mi_x1);
  int y1 = legalizer->microns_to_dbu(mi_y1);
  int x2 = legalizer->microns_to_dbu(mi_x2);
  int y2 = legalizer->microns_to_dbu(mi_y2);

  legalizer->shuffle(x1, y1, x2, y2);
}


void disturb() {
  Legalizer* legalizer = getLegalizer();
  legalizer->disturb();
}

void tetris() {
  Legalizer* legalizer = getLegalizer();
  legalizer->tetris();
}

void tetris_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Legalizer* legalizer = getLegalizer();

  int x1 = legalizer->microns_to_dbu(mi_x1);
  int y1 = legalizer->microns_to_dbu(mi_y1);
  int x2 = legalizer->microns_to_dbu(mi_x2);
  int y2 = legalizer->microns_to_dbu(mi_y2);

  legalizer->tetris(x1, y1, x2, y2);
}

void save_state() {
  Legalizer* legalizer = getLegalizer();
  legalizer->save_state();
}

void load_state() {
  Legalizer* legalizer = getLegalizer();
  legalizer->load_state();
}

void save_pos_to_file(const char* path) {
  Legalizer* legalizer = getLegalizer();
  legalizer->save_pos_to_file(path);
}

void load_pos_from_file(const char* path) {
  Legalizer* legalizer = getLegalizer();
  legalizer->load_pos_from_file(path);
}

void save_costs_to_file(const char* path) {
  Legalizer* legalizer = getLegalizer();
  legalizer->save_costs_to_file(path);
}

void show_legalized_vector() {
  Legalizer* legalizer = getLegalizer();
  legalizer->show_legalized_vector();
}

} // namespace

%} // inline
