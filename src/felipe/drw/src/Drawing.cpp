#include <string>

#include "drw/Drawing.h"

namespace drw {
    using namespace odb;
    using std::to_string;

    Drawing::Drawing() :
        db{ord::OpenRoad::openRoad()->getDb()},
        logger{ord::OpenRoad::openRoad()->getLogger()}
    {}

    Drawing::~Drawing() {}

    void Drawing::log() {
        logger->report("log");
    }

    void Drawing::draw_rect(int x1, int y1, int x2, int y2) {
        static int counter = 0;
        Rect rect(x1, y1, x2, y2);
        gui::Painter::Color color(0, 255, 0, 100);
        renderer.drawings.insert({to_string(counter), {rect, color}});
        renderer.redraw();
        counter++;
    }

    void Drawing::undraw_rect(int id) {
        renderer.drawings.erase(to_string(id));
        renderer.redraw();
    }

    void Drawing::undraw_all() {
        renderer.drawings.clear();
        renderer.redraw();
    }

}
