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
    bool Congestion::update_routing_heatmap(std::string layer_name) {
        dbBlock* block = db->getChip()->getBlock();

        dbGCellGrid* grid = block->getGCellGrid();
        if (!grid) return false;

        dbTechLayer* layer = 0;
        if (layer_name != "") {
            for (dbTechLayer* curr_layer : block->getDataBase()->getTech()->getLayers()) {
                if (curr_layer->getType() != dbTechLayerType::ROUTING) continue;

                if (curr_layer->getName() == layer_name) layer = curr_layer;
            }

            if (!layer) {
                logger->report("Layer not found");
                return false;
            }
        }
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

    void Congestion::undraw() {
        renderer.sprites.clear();
        renderer.redraw();
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

    // todo: add as member
    double get_congestion(dbGCellGrid::GCellData& data) {
        double hor = data.horizontal_capacity
                     ? (double)data.horizontal_usage / data.horizontal_capacity
                     : -1;
        double vert = data.vertical_capacity
                      ? (double)data.vertical_usage / data.vertical_capacity
                      : -1;

        if (hor > vert) return hor;
        else            return vert;
    }
    
    void Congestion::test(std::string name) {
        setbuf(stdout, 0);

        dbBlock* block = 0;
        {
            dbChip* chip = db->getChip();
            if (chip) block = chip->getBlock();

            if (!block) {
                fprintf(stderr, "Failed to load block\n");
            }
        }

        dbInst* moving_inst = 0;
        dbSet<dbInst> insts = block->getInsts();
        {
            for (dbInst* i : insts) {
                if (i->getName() == name) {
                    moving_inst = i;
                }
            }

            if (!moving_inst) {
                printf("%s", (name + " not found\n").c_str());
                return;
            }
        }

        Rect src_rect = moving_inst->getBBox()->getBox();
        Rect dst_rect;
        std::vector<Rect> other_cells;
        int max_delta = INT_MIN;
        for (dbInst* curr_moving_inst : insts) {
            if (curr_moving_inst->isFixed()) continue;

            Rect curr_src_rect = curr_moving_inst->getBBox()->getBox();
            Rect curr_dst_rect;
            std::vector<Rect> curr_other_cells;

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
                        curr_other_cells.push_back(inst->getBBox()->getBox());
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
            curr_dst_rect = Rect(
                Optimal_Region.first, Optimal_Region.second,
                Optimal_Region.first + curr_src_rect.dx(), Optimal_Region.second + curr_src_rect.dy()
            );

            auto abs_int = [&](int x) {
                if (x >= 0) return x;
                else        return -x;
            };

            int curr_delta = abs_int(curr_src_rect.xMin() - curr_dst_rect.xMin())
                           + abs_int(curr_src_rect.yMin() - curr_dst_rect.yMin());

            if (curr_delta > max_delta) {
                src_rect = curr_src_rect;
                dst_rect = curr_dst_rect;
                other_cells = curr_other_cells;
                max_delta = curr_delta;
            }
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

                assert(grid_x_min_i >= 0
                    && grid_x_max_i < x_grid.size()
                    && grid_y_min_i >= 0
                    && grid_y_max_i < y_grid.size()
                    && "Out of grid");

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

                        //printf("(%d %d %d)\n", data.horizontal_capacity, data.vertical_capacity, vert > hor);

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

    void Congestion::test2(std::string layer_name) {
        dbBlock* block = 0;
        {
            dbChip* chip = db->getChip();
            if (chip) block = chip->getBlock();

            if (!block) {
                fprintf(stderr, "Failed to load block\n");
            }
        }

        dbGCellGrid* grid = block->getGCellGrid();

        auto run = [&](dbTechLayer* layer) {
            dbMatrix<dbGCellGrid::GCellData> congestion_data = grid->getCongestionMap(layer);

            std::vector<int> x_grid, y_grid;
            grid->getGridX(x_grid);
            grid->getGridY(y_grid);

            assert(x_grid.size() == congestion_data.numRows());
            assert(y_grid.size() == congestion_data.numCols());

            int max_x = congestion_data.numRows()-2;
            int max_y = congestion_data.numCols()-2;

            bool first = true;
            double max = -1;
            int x_i_max_cong = -1;
            int y_i_max_cong = -1;
            for (int x_i = 0; x_i <= max_x; x_i++) {
                for (int y_i = 0; y_i <= max_y; y_i++) {
                    double cong;
                    {
                        dbGCellGrid::GCellData data = congestion_data(x_i, y_i);
                        double hor = data.horizontal_capacity
                                     ? (double)data.horizontal_usage / data.horizontal_capacity
                                     : -1;
                        double vert = data.vertical_capacity
                                      ? (double)data.vertical_usage / data.vertical_capacity
                                      : -1;

                        if (hor > vert) cong = hor;
                        else            cong = vert;

                        if (cong > 1) {
                            Rect rect_max_cong = Rect(
                                x_grid[x_i], y_grid[y_i],
                                x_grid[x_i+1], y_grid[y_i+1]
                            );

                            auto draw_rect = [&](Rect rect, gui::Painter::Color color) {
                                renderer.sprites.push_back({
                                    "",
                                    drw::RECT,
                                    {rect.ll(), rect.ur()},
                                    color,
                                });
                            };

                            draw_rect(rect_max_cong, gui::Painter::Color(0, 0, 255, 255));
                        }

                        if (cong > 100) {
                            if (first) {
                                logger->report("Congestion bigger than 100");
                                first = false;
                            }
                        }
                    }
                    if (cong > max) {
                        max = cong;
                        x_i_max_cong = x_i;
                        y_i_max_cong = y_i;
                    }
                }
            }
            renderer.redraw();

            logger->report("Congestionamento maximo: " + std::to_string(max));
        };

        dbTechLayer* layer = 0;
        if (layer_name != "") {
            for (dbTechLayer* curr_layer : block->getDataBase()->getTech()->getLayers()) {
                if (curr_layer->getType() != dbTechLayerType::ROUTING) continue;

                if (curr_layer->getName() == layer_name) layer = curr_layer;
            }

            if (!layer) {
                logger->report("Layer not found");
                return;
            }
        }

        undraw();
        update_routing_heatmap(layer_name);
        run(layer);
    }
}

