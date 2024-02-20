#pragma once

#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "gui/gui.h"

namespace odb {
  class dbDatabase;
}

namespace utl {
  class Logger;
}

namespace drw {

enum Type {
    LINE,
    RECT,
};

struct Sprite {
    std::string name;  // todo: delete?
    Type type;
    odb::Point points[2];
    gui::Painter::Color color;
};

class MyRenderer : public gui::Renderer {
public:
    // attributes
    std::vector<Sprite> sprites;

    // methods
    MyRenderer() {
        gui::Gui::get()->registerRenderer(this);
    }

    virtual void drawObjects(gui::Painter& painter) override {
        for (Sprite const& sprite : sprites) {
            if (sprite.type == RECT) {
                painter.setBrush(sprite.color);
                painter.drawRect(odb::Rect(sprite.points[0], sprite.points[1]));
            } else if (sprite.type == LINE) {
                painter.setPen(sprite.color, false, 200);
                painter.drawLine(
                    sprite.points[0],
                    sprite.points[1]
                );
                painter.setPen(sprite.color, false, 0);
            }
        }
    }
};

class Drawing {
public:
    int counter;

    Drawing();
    ~Drawing();

    void log();

    void draw_rect(int x1, int y1, int x2, int y2);
    void draw_line(int x1, int y1, int x2, int y2);
    void undraw(int id);
    void undraw_all();

    // attributes
    odb::dbDatabase* db;
    utl::Logger* logger;
    MyRenderer renderer;
};

}

