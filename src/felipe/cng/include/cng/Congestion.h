#pragma once

#include "odb/db.h"
#include "gui/heatMap.h"

namespace odb {
  class dbDatabase;
  class dbInst;
}

namespace utl {
  class Logger;
}

namespace cng {

class MyHeatMap : public gui::HeatMapDataSource {
public:
    MyHeatMap(utl::Logger* logger)
        : HeatMapDataSource(
            logger,
            "MyHeatMap",
            "MyCng",
            "MyHeatMap"
        )
    {}

    virtual double getGridXSize() const override {
        odb::dbBlock* block = getBlock();
        if (x_size == -1) {
            if (!block || matrix.numRows() == 0) {
                return HeatMapDataSource::getGridXSize();
            } else {
                return (block->getDieArea().dx() / 10) / block->getDbUnitsPerMicron();
            }
        } else {
            return (double)x_size / block->getDbUnitsPerMicron();
        }
    }

    virtual double getGridYSize() const override {
        odb::dbBlock* block = getBlock();
        if (y_size == -1) {
            if (!block || matrix.numCols() == 0) {
                return HeatMapDataSource::getGridXSize();
            } else {
                return (block->getDieArea().dy() / 10) / block->getDbUnitsPerMicron();
            }
        } else {
            return (double)y_size / block->getDbUnitsPerMicron();
        }
    }

    virtual bool populateMap() override {
        odb::dbBlock* block = getBlock();

        if (!block) return false;

        int curr_x_size = getGridXSize() * block->getDbUnitsPerMicron();
        int curr_y_size = getGridYSize() * block->getDbUnitsPerMicron();
  
        int x = 0;
        for (int x_i = 0; x_i < matrix.numRows(); x_i++) {
            int next_x = x + curr_x_size;
            int y = 0;
            for (int y_i = 0; y_i < matrix.numCols(); y_i++) {
                int next_y = y + curr_y_size;

                odb::Rect gcell_rect(x, y, next_x, next_y);

                if (matrix(x_i, y_i) < 0) continue;

                addToMap(gcell_rect, matrix(x_i, y_i));

                y = next_y;
            }
            x = next_x;
        }

        return true;
    }

    virtual void combineMapData(
        bool base_has_value,
        double& base,
        const double new_data,
        const double data_area,
        const double intersection_area,
        const double rect_area
    ) override {
        base += new_data;
    }

    // attributes
    odb::dbMatrix<double> matrix;
    int x_size{-1};
    int y_size{-1};
};

class Congestion {
public:
    // methods
    Congestion();
    ~Congestion();

    bool routing();
    bool placement();

    // attributes
    utl::Logger* logger;
    odb::dbDatabase* db;

    odb::dbMatrix<double> congestion;
    MyHeatMap heat_map;
};
}

