#include <string>

#include "drw/Drawing.h"

namespace drw {
    using namespace odb;
    using std::to_string;

    Drawing::Drawing() :
        db{ord::OpenRoad::openRoad()->getDb()},
        logger{ord::OpenRoad::openRoad()->getLogger()}
    {
        counter = 0;
    }

    Drawing::~Drawing() {}

    void Drawing::log() {
        logger->report("log");
    }

    void Drawing::draw_rect(int x1, int y1, int x2, int y2) {
        gui::Painter::Color color(0, 255, 0, 100);
        renderer.sprites.push_back({
            to_string(counter),
            RECT,
            {{x1, y1}, {x2, y2}},
            color,
        });
        renderer.redraw();
        counter++;
    }

    void Drawing::draw_line(int x1, int y1, int x2, int y2) {
        gui::Painter::Color color(255, 0, 0, 255);
        renderer.sprites.push_back({
            to_string(counter),
            LINE,
            {{x1, y1}, {x2, y2}},
            color,
        });
        renderer.redraw();
        counter++;
    }

    void Drawing::undraw(int id) {
        std::vector<Sprite>& sprites = renderer.sprites;

        decltype(sprites.begin()) to_erase = sprites.end();
        for (auto it = sprites.begin(); it != sprites.end(); it++) {
            if (it->name == to_string(id)) {
                to_erase = it;
            }
        }
        if (to_erase != sprites.end()) sprites.erase(to_erase);
        renderer.redraw();
    }

    void Drawing::undraw_all() {
        renderer.sprites.clear();
        renderer.redraw();
    }

}
