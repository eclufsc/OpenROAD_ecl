// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2025, The OpenROAD Authors

#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gui/gui.h"
#include "gui/heatMap.h"
#include "odb/db.h"
#include "odb/geom.h"

namespace utl {
class Logger;
}

namespace gpl {
class PlacerBaseCommon;
class PlacerBase;
class Instance;
}  // namespace gpl

namespace epl {

class WAwirelength;
class EDensity;
class NesterovOptimizer;
class EPlace;

// This class draws debugging graphics on the layout
class Graphics : public gui::Renderer, public gui::HeatMapDataSource
{
 public:
  using LineSeg = std::pair<odb::Point, odb::Point>;
  using LineSegs = std::vector<LineSeg>;

  Graphics(utl::Logger* logger);
  ~Graphics() override;

  void debug(EPlace* ep,
             std::shared_ptr<gpl::PlacerBaseCommon> pbc,
             std::shared_ptr<WAwirelength> wa,
             std::shared_ptr<NesterovOptimizer> nesterov,
             std::vector<std::shared_ptr<gpl::PlacerBase>>& pbVec,
             std::vector<std::shared_ptr<EDensity>>& edVec,
             bool draw_bins);

  void addIter(int iter, double overflow);

  void status(std::string_view message);
  void setDrawFillers(bool draw) { draw_fillers_ = draw; };

  bool enabled();

  void setDebugOn(bool set_on) { debug_on_ = set_on; }

  void setDisplayControl(std::string_view name, bool value);

  int gifStart(std::string_view path);
  void deleteLabel(std::string_view label_name);
  void gifEnd(int key);

  // Draw the graphics; optionally pausing afterwards
  void cellPlot(bool pause = false) { cellPlotImpl(pause); }
  void addFrameLabel(const odb::Rect& bbox,
                     std::string_view label,
                     std::string_view label_name,
                     int image_width_px = 500)
  {
    addFrameLabelImpl(bbox, label, label_name, image_width_px);
  }
  void gifAddFrame(int key,
                   const odb::Rect& region = odb::Rect(),
                   int width_px = 0,
                   double dbu_per_pixel = 0,
                   std::optional<int> delay = std::nullopt)
  {
    gifAddFrameImpl(key, region, width_px, dbu_per_pixel, delay);
  }

  void saveLabeledImage(std::string_view path,
                        std::string_view label,
                        bool select_buffers,
                        std::string_view heatmap_control = "",
                        int image_width_px = 500)
  {
    saveLabeledImageImpl(
        path, label, select_buffers, heatmap_control, image_width_px);
  }

 protected:
  void cellPlotImpl(bool pause);

  void addFrameLabelImpl(const odb::Rect& bbox,
                         std::string_view label,
                         std::string_view label_name,
                         int image_width_px);
  void saveLabeledImageImpl(std::string_view path,
                            std::string_view label,
                            bool select_buffers,
                            std::string_view heatmap_control,
                            int image_width_px);
  void gifAddFrameImpl(int key,
                       const odb::Rect& region,
                       int width_px,
                       double dbu_per_pixel,
                       std::optional<int> delay);

 private:
  // From Renderer API
  void drawObjects(gui::Painter& painter) override;
  gui::SelectionSet select(odb::dbTechLayer* layer,
                           const odb::Rect& region) override;

  // From HeatMapDataSource
  bool canAdjustGrid() const override { return false; }
  double getGridXSize() const override;
  double getGridYSize() const override;
  odb::Rect getBounds() const override;
  bool populateMap() override;
  void combineMapData(bool base_has_value,
                      double& base,
                      double new_data,
                      double data_area,
                      double intersection_area,
                      double rect_area) override;
  void populateXYGrid() override;

  enum HeatMapType
  {
    Density,
    Overflow,
    OverflowMinMax
  };

  // These are used for coloring each instance based on its group
  std::vector<gui::Painter::Color> instances_colors_ = {
      gui::Painter::kDarkGreen,
      gui::Painter::kDarkBlue,
      gui::Painter::kBrown,
      gui::Painter::kDarkYellow,
  };

  // These are used for bin forces, fillers, and dummies (lighter) for each
  // region.
  std::vector<gui::Painter::Color> region_colors_ = {
      gui::Painter::kDarkMagenta,
      gui::Painter::kYellow,
      gui::Painter::kBlue,
      gui::Painter::kCyan,

  };

  void drawForce(gui::Painter& painter);
  void drawCells(const std::vector<gpl::Instance*>& cells,
                 gui::Painter& painter,
                 size_t nb_index);
  void drawFillerCells(const std::vector<gpl::Instance>& cells,
                       gui::Painter& painter,
                       size_t nb_index);
  void drawSingleGCell(const gpl::Instance* gCell,
                       gui::Painter& painter,
                       size_t nb_index = 0);

  std::shared_ptr<gpl::PlacerBaseCommon> pbc_;
  std::shared_ptr<WAwirelength> wa_;
  std::shared_ptr<NesterovOptimizer> nesterov_;
  std::vector<std::shared_ptr<gpl::PlacerBase>> pbVec_;
  std::vector<std::shared_ptr<EDensity>> edVec_;
  EPlace* ep_ = nullptr;
  static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
  size_t selected_ = kInvalidIndex;
  size_t nb_selected_index_ = kInvalidIndex;
  bool draw_bins_ = false;
  utl::Logger* logger_ = nullptr;
  HeatMapType heatmap_type_ = Density;
  LineSegs mbff_edges_;
  std::vector<odb::dbInst*> mbff_cluster_;
  gui::Chart* main_chart_{nullptr};
  gui::Chart* density_chart_{nullptr};
  gui::Chart* stepLength_chart_{nullptr};
  gui::Chart* routing_chart_{nullptr};
  bool debug_on_{false};
  bool draw_fillers_{true};

  void initHeatmap();
  void drawNesterov(gui::Painter& painter);
  void drawInitial(gui::Painter& painter);
  void drawBounds(gui::Painter& painter);
  void reportSelected();
};

}  // namespace epl
