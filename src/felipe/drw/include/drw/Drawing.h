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

class MyRenderer : public gui::Renderer {
public:
    // attributes
    std::map<std::string, std::pair<odb::Rect, gui::Painter::Color>> drawings;

    // methods
    MyRenderer() {
        gui::Gui::get()->registerRenderer(this);
    }

    virtual void drawObjects(gui::Painter& painter) override {
        for (auto const& [key, value] : drawings) {
            auto const& [rect, color] = value;
            painter.setBrush(color);
            painter.drawRect(rect);
        }
    }
};

class Drawing {
public:
    Drawing();
    ~Drawing();

    void log();

    void draw_rect(int x1, int y1, int x2, int y2);
    void undraw_rect(int id);
    void undraw_all();

    // attributes
    odb::dbDatabase* db;
    utl::Logger* logger;
    MyRenderer renderer;
};

}

