#undef NDEBUG
#include <assert.h>
#include <algorithm>
#include <stdio.h>

#include "cng/Congestion.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "odb/dbTypes.h"
#include "odb/dbTransform.h"

#include "gui/gui.h"

namespace cng {
    using namespace odb;

    Congestion::Congestion() :
        logger{ord::OpenRoad::openRoad()->getLogger()},
        db{ord::OpenRoad::openRoad()->getDb()},
        congestion(10, 10),
        heat_map(logger)
    {
        for (int i = 0; i < congestion.numRows(); i++) {
            for (int j = 0; j < congestion.numCols(); j++) {
                congestion(i, j) =
                    50/congestion.numRows()*((congestion.numRows()-i-1))
                    + 50/congestion.numCols()*(j+1);
            }
        }

        heat_map.matrix = congestion;
        heat_map.x_size = -1;
        heat_map.y_size = -1;

        heat_map.registerHeatMap();
    }

    Congestion::~Congestion() {}

    // note: adapted from RoutingCongestionDataSource
    bool Congestion::routing() {
        dbBlock* block = db->getChip()->getBlock();

        dbGCellGrid* grid = block->getGCellGrid();
        if (!grid) return false;

        dbTechLayer* layer = 0;
        dbMatrix<dbGCellGrid::GCellData> congestion_data = grid->getCongestionMap(layer);
        if (congestion_data.numElems() == 0) return false;

        std::vector<int> x_grid, y_grid;
        grid->getGridX(x_grid);
        grid->getGridY(y_grid);
        uint x_grid_sz = x_grid.size();
        uint y_grid_sz = y_grid.size();

        assert(congestion_data.numRows() == x_grid_sz);
        assert(congestion_data.numCols() == y_grid_sz);

        congestion.resize(x_grid_sz, y_grid_sz);
        for (uint x_i = 0; x_i < congestion.numRows(); x_i++) {
            for (uint y_i = 0; y_i < congestion.numCols(); y_i++) {
                congestion(x_i, y_i) = 0;
            }
        }

        for (uint x_i = 0; x_i < congestion_data.numRows(); x_i++) {
            for (uint y_i = 0; y_i < congestion_data.numCols(); y_i++) {
                uint hor_capacity, hor_usage, vert_capacity, vert_usage;
                {
                    dbGCellGrid::GCellData const& data = congestion_data(x_i, y_i);
                    hor_capacity = data.horizontal_capacity;
                    hor_usage = data.horizontal_usage;
                    vert_capacity = data.vertical_capacity;
                    vert_usage = data.vertical_usage;
                }

                // -1 indicates capacity is not well defined
                double hor_congestion, vert_congestion;
                if (hor_capacity != 0) {
                    hor_congestion = (double)hor_usage / hor_capacity;
                } else {
                    hor_congestion = -1;
                }
                if (vert_capacity != 0) {
                    vert_congestion = (double)vert_usage / vert_capacity;
                } else {
                    vert_congestion = -1;
                }

                enum Direction {
                    All,
                    Horizontal,
                    Vertical
                };

                Direction direction = All;

                double value;
                if (direction == All) {
                    value = std::max(hor_congestion, vert_congestion);
                } else if (direction == Horizontal) {
                    value = hor_congestion;
                } else {
                    value = vert_congestion;
                }
                value *= 100.0;

                congestion(x_i, y_i) = value;
            }
        }

        heat_map.matrix = congestion;
        heat_map.x_size = x_grid[1] - x_grid[0];
        heat_map.y_size = y_grid[1] - y_grid[0];

        heat_map.update();

        return true;
    }

    bool placement() {
        // todo
    }
}

