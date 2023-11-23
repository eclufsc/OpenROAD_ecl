%{
#include "odb/db.h"
#include "gui/gui.h"
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

void translate_mi(const char* cell_name, int delta_x_mi, int delta_y_mi) {
  Tutorial* tutorial = getTutorial();

  int delta_x = tutorial->microns_to_dbu(delta_x_mi);
  int delta_y = tutorial->microns_to_dbu(delta_y_mi);

  if (!tutorial->translate(cell_name, delta_x, delta_y)) {
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

void abacus_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Tutorial* tutorial = getTutorial();

  int x1 = tutorial->microns_to_dbu(mi_x1);
  int y1 = tutorial->microns_to_dbu(mi_y1);
  int x2 = tutorial->microns_to_dbu(mi_x2);
  int y2 = tutorial->microns_to_dbu(mi_y2);

  tutorial->abacus(x1, y1, x2, y2);
}

void draw_rect_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Tutorial* tutorial = getTutorial();

  int x1 = tutorial->microns_to_dbu(mi_x1);
  int y1 = tutorial->microns_to_dbu(mi_y1);
  int x2 = tutorial->microns_to_dbu(mi_x2);
  int y2 = tutorial->microns_to_dbu(mi_y2);

  tutorial->draw_rect(x1, y1, x2, y2);
}

void undraw_rect(int id) {
  Tutorial* tutorial = getTutorial();
  tutorial->undraw_rect(id);
}

void undraw_all() {
  Tutorial* tutorial = getTutorial();
  tutorial->undraw_all();
}

void abacus_artur_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Tutorial* tutorial = getTutorial();

  int x1 = tutorial->microns_to_dbu(mi_x1);
  int y1 = tutorial->microns_to_dbu(mi_y1);
  int x2 = tutorial->microns_to_dbu(mi_x2);
  int y2 = tutorial->microns_to_dbu(mi_y2);

  tutorial->abacus_artur(x1, y1, x2, y2);
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

void is_legalized_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Tutorial* tutorial = getTutorial();

  int x1 = tutorial->microns_to_dbu(mi_x1);
  int y1 = tutorial->microns_to_dbu(mi_y1);
  int x2 = tutorial->microns_to_dbu(mi_x2);
  int y2 = tutorial->microns_to_dbu(mi_y2);

  auto [ok, reason] = tutorial->is_legalized(x1, y1, x2, y2);

  if (ok) {
    tutorial->logger->report("The area is legalized");
  } else {
    tutorial->logger->report("The area is not legalized: " + reason);
  }
}

void is_legalized_excluding_border_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Tutorial* tutorial = getTutorial();

  int x1 = tutorial->microns_to_dbu(mi_x1);
  int y1 = tutorial->microns_to_dbu(mi_y1);
  int x2 = tutorial->microns_to_dbu(mi_x2);
  int y2 = tutorial->microns_to_dbu(mi_y2);

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

void shuffle_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Tutorial* tutorial = getTutorial();
  
  int x1 = tutorial->microns_to_dbu(mi_x1);
  int y1 = tutorial->microns_to_dbu(mi_y1);
  int x2 = tutorial->microns_to_dbu(mi_x2);
  int y2 = tutorial->microns_to_dbu(mi_y2);

  tutorial->shuffle(x1, y1, x2, y2);
}


void disturb() {
  Tutorial* tutorial = getTutorial();
  tutorial->disturb();
}

void tetris() {
  Tutorial* tutorial = getTutorial();
  tutorial->tetris();
}

void tetris_mi(double mi_x1, double mi_y1, double mi_x2, double mi_y2) {
  Tutorial* tutorial = getTutorial();

  int x1 = tutorial->microns_to_dbu(mi_x1);
  int y1 = tutorial->microns_to_dbu(mi_y1);
  int x2 = tutorial->microns_to_dbu(mi_x2);
  int y2 = tutorial->microns_to_dbu(mi_y2);

  tutorial->tetris(x1, y1, x2, y2);
}

void save_state() {
  Tutorial* tutorial = getTutorial();
  tutorial->save_state();
}

void load_state() {
  Tutorial* tutorial = getTutorial();
  tutorial->load_state();
}

void save_pos_to_file(const char* path) {
  Tutorial* tutorial = getTutorial();
  tutorial->save_pos_to_file(path);
}

void load_pos_from_file(const char* path) {
  Tutorial* tutorial = getTutorial();
  tutorial->load_pos_from_file(path);
}

void save_costs_to_file(const char* path) {
  Tutorial* tutorial = getTutorial();
  tutorial->save_costs_to_file(path);
}

void show_legalized_vector() {
  Tutorial* tutorial = getTutorial();
  tutorial->show_legalized_vector();
}

} // namespace

%} // inline
