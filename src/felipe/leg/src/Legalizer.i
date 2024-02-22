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

void abacus() {
  Legalizer* legalizer = getLegalizer();
  legalizer->abacus();
}

void abacus(int x1, int y1, int x2, int y2) {
  Legalizer* legalizer = getLegalizer();
  legalizer->abacus(x1, y1, x2, y2);
}

void abacus_artur(int x1, int y1, int x2, int y2) {
  Legalizer* legalizer = getLegalizer();
  legalizer->abacus_artur(x1, y1, x2, y2);
}

void tetris() {
  Legalizer* legalizer = getLegalizer();
  legalizer->tetris();
}

void tetris(double x1, double y1, double x2, double y2) {
  Legalizer* legalizer = getLegalizer();
  legalizer->tetris(x1, y1, x2, y2);
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

void is_legalized(int x1, int y1, int x2, int y2) {
  Legalizer* legalizer = getLegalizer();

  auto [ok, reason] = legalizer->is_legalized(x1, y1, x2, y2);

  if (ok) {
    legalizer->logger->report("The area is legalized");
  } else {
    legalizer->logger->report("The area is not legalized: " + reason);
  }
}

void is_legalized_excluding_border(int x1, int y1, int x2, int y2) {
  Legalizer* legalizer = getLegalizer();

  auto [ok, reason] = legalizer->is_legalized_excluding_border(x1, y1, x2, y2);

  if (ok) {
    legalizer->logger->report("The area is legalized");
  } else {
    legalizer->logger->report("The area is not legalized: " + reason);
  }
}


} // namespace

%} // inline
