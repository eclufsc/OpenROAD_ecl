%{
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "drw/Drawing.h"

namespace ord {
drw::Drawing* getDrawing(); // Defined in OpenRoad.i
OpenRoad *getOpenRoad(); // Defined in OpenRoad.i
}  // namespace ord

using ord::getDrawing;
using drw::Drawing;
%}

%inline %{

namespace drw {

void log()
{
    Drawing* drawing = getDrawing();
    drawing->log();
}

void draw_rect(int x1, int y1, int x2, int y2) {
  Drawing* drawing = getDrawing();
  drawing->draw_rect(x1, y1, x2, y2);
}

void undraw_rect(int id) {
  Drawing* drawing = getDrawing();
  drawing->undraw_rect(id);
}

void undraw_all() {
  Drawing* drawing = getDrawing();
  drawing->undraw_all();
}


} // namespace

%} // inline
