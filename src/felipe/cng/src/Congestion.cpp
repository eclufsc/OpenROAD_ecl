#undef NDEBUG
#include <assert.h>
#include <algorithm>
#include <stdio.h>
#include <string>

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
    bool Congestion::update_routing_heatmap() {
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

    std::pair<int, int> Congestion::nets_Bboxes_median(
        std::vector<int> Xs, std::vector<int> Ys
    ) {

        int median_pos_X = std::floor(Xs.size()/2);
        std::sort(Xs.begin(), Xs.end());

        int median_pos_Y = std::floor(Ys.size()/2);
        std::sort(Ys.begin(), Ys.end());

        int xll = Xs[median_pos_X - 1];
        int xur = Xs[median_pos_X];
        int yll = Ys[median_pos_Y - 1];
        int yur = Ys[median_pos_Y];

        int x = (xll + xur)/2;
        int y = (yll + yur)/2;

        return std::pair<int, int> (x, y);
    }

    void Congestion::test(std::string name) {
        setbuf(stdout, 0);

        dbBlock* block = 0;
        dbChip* chip = db->getChip();
        if (chip) block = chip->getBlock();

        if (!block) {
            fprintf(stderr, "Failed to load block\n");
        }

        dbSet<dbInst> insts = block->getInsts();
        dbInst* moving_inst = 0;
        for (dbInst* i : insts) {
            if (i->getName() == name) {
                moving_inst = i;
            }
        }

        if (!moving_inst) {
            printf("%s", (name + " not found\n").c_str());
            return;
        }

        Rect src_rect = moving_inst->getBBox()->getBox();

        Rect dst_rect;
        std::vector<Rect> other_cells;
        {
            std::vector<int> nets_Bbox_Xs;
            std::vector<int> nets_Bbox_Ys;

            for (auto pin : moving_inst->getITerms()) {
                auto net = pin->getNet();
                if (!net) continue;

                if (net->getSigType() == odb::dbSigType::POWER ||
                        net->getSigType() == odb::dbSigType::GROUND) {
                    continue;
                }

                int xll = std::numeric_limits<int>::max();
                int yll = std::numeric_limits<int>::max();
                int xur = std::numeric_limits<int>::min();
                int yur = std::numeric_limits<int>::min();
                for (auto iterm : net->getITerms()) {
                    int x=0, y=0;

                    odb::dbInst* inst = iterm->getInst();
                    if (inst && (inst != moving_inst)) {
                        other_cells.push_back(inst->getBBox()->getBox());
                        inst->getLocation(x, y);
                        xur = std::max(xur, x);
                        yur = std::max(yur, y);
                        xll = std::min(xll, x);
                        yll = std::min(yll, y);
                    }
                }

                nets_Bbox_Xs.push_back(xur);
                nets_Bbox_Xs.push_back(xll);
                nets_Bbox_Ys.push_back(yur);
                nets_Bbox_Ys.push_back(yll);
            }

            std::pair<int, int> Optimal_Region = nets_Bboxes_median(nets_Bbox_Xs, nets_Bbox_Ys);
            dst_rect = Rect(
                Optimal_Region.first, Optimal_Region.second,
                Optimal_Region.first + src_rect.dx(), Optimal_Region.second + src_rect.dy()
            );
        }

        auto draw_rect = [&](Rect rect, gui::Painter::Color color) {
            renderer.sprites.push_back({
                "",
                drw::RECT,
                {rect.ll(), rect.ur()},
                color,
            });
        };

        auto draw_line = [&](Point p1, Point p2, gui::Painter::Color color) {
            renderer.sprites.push_back({
                "",
                drw::LINE,
                {p1, p2},
                color,
            });
        };

        // draw cells
        {

            for (Rect rect : other_cells) {
                draw_rect(rect, gui::Painter::Color(0, 0, 255, 255));
                draw_line(
                    rect.ll(),
                    src_rect.ll(),
                    gui::Painter::Color(0, 255, 255, 255)
                );
            }

            draw_rect(src_rect, gui::Painter::Color(255, 0, 0, 255));

            draw_rect(dst_rect, gui::Painter::Color(0, 255, 0, 255));
            draw_line(
                dst_rect.ll(),
                src_rect.ll(),
                gui::Painter::Color(0, 255, 0, 255)
            );

            renderer.redraw();
        }

        // congestion
        {
            int block_start = block->getDieArea().xMin();
            dbGCellGrid* grid = block->getGCellGrid();
            dbTechLayer* layer = 0;
            dbMatrix<dbGCellGrid::GCellData> congestion_data = grid->getCongestionMap(layer);

            std::vector<int> x_grid, y_grid;
            grid->getGridX(x_grid);
            grid->getGridY(y_grid);

            int x_size = x_grid[1] - x_grid[0];
            int y_size = y_grid[1] - y_grid[0];

            auto display = [&](Rect rect, gui::Painter::Color color) {
                int grid_x_min_i = (rect.xMin() - block_start) / x_size;
                int grid_x_max_i = (rect.xMax() - block_start - 1) / x_size;

                int grid_y_min_i = (rect.yMin() - block_start) / y_size;
                int grid_y_max_i = (rect.yMax() - block_start - 1) / y_size;

                Rect pos(
                    x_grid[grid_x_min_i], y_grid[grid_y_min_i],
                    x_grid[grid_x_max_i+1], y_grid[grid_y_max_i+1]
                );
                draw_rect(pos, color);

                double max_cong = -1;
                int total_usage = 0;
                int total_capacity = 0;
                double total_cong = 0;
                for (int x_i = grid_x_min_i; x_i <= grid_x_max_i; x_i++) {
                    for (int y_i = grid_y_min_i; y_i <= grid_y_max_i; y_i++) {
                        dbGCellGrid::GCellData data = congestion_data(x_i, y_i);

                        double hor = data.horizontal_capacity
                                     ? (double)data.horizontal_usage / data.horizontal_capacity
                                     : -1;
                        double vert = data.vertical_capacity
                                      ? (double)data.vertical_usage / data.vertical_capacity
                                      : -1;

                        if (hor > vert) {
                            total_usage    += data.horizontal_usage;
                            total_capacity += data.horizontal_capacity;
                            total_cong     += hor;
                            if (hor > max_cong) max_cong = hor;
                        } else {
                            total_usage    += data.vertical_usage;
                            total_capacity += data.vertical_capacity;
                            total_cong     += vert;
                            if (vert > max_cong) max_cong = vert;
                        }
                    }
                }

                printf("Metric 1: %f\n", (double)total_usage / total_capacity);
                printf("Metric 2: %f\n",
                    total_cong
                    / ((grid_x_max_i - grid_x_min_i + 1) * (grid_y_max_i - grid_y_min_i + 1))
                );
                printf("Max cong: %f\n", max_cong);
            };

            printf("src:\n");
            display(src_rect, gui::Painter::Color(255, 0, 0, 100));
            printf("\n");
            printf("dst:\n");
            display(dst_rect, gui::Painter::Color(0, 255, 0, 100));
            printf("\n");
        }
    }

    void Congestion::undraw() {
        renderer.sprites.clear();
        renderer.redraw();
    }
}

