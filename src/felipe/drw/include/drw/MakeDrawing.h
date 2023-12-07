#pragma once

namespace drw{
class Drawing;
}

namespace odb{
class dbDatabase;
}

namespace ord {

class OpenRoad;

drw::Drawing* makeDrawing();

void initDrawing(OpenRoad *openroad);

void deleteDrawing(drw::Drawing *drawing);

}  // namespace ord

