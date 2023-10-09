%{
#include "odb/db.h"
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

void init() {
  Tutorial* tutorial = getTutorial();
  tutorial->init();
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

} // namespace

%} // inline
