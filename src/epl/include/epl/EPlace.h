// Copyright (c) 2021, The Regents of the University of California
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <vector>

#include "Graphics.h"
#include "Nesterov.h"
#include "gpl/Replace.h"
#include "odb/db.h"

namespace gpl {
class Graphics;
}  // namespace gpl

namespace epl {
class EDensity;
class WAwirelength;
class NesterovOptimizer;

// class Nesterov;

class EPlace
{
 public:
  EPlace();
  ~EPlace();

  void init(odb::dbDatabase* db, utl::Logger* logger);
  void clear();

  void set_debug(bool draw_bins,
                 bool disable_wirelength,
                 bool disable_density,
                 int pause_interval);
  void place(int threads,
             float density,
             bool uniform_density,
             float dhpwl_ref,
             int iterations,
             float initial_density_penalty_mult,
             int pad_left,
             int pad_right,
             bool skip_io_mode,
             int info_interval,
             bool use_step_new);
  void randomPlace(int threads);
  void calcualteWaHPWL(float gamma);
  void updateGradient();

 private:
  bool initEPlace(float density,
                  bool uniform_density,
                  int pad_left,
                  int pad_right,
                  bool skip_io_mode);
  bool initPlacer(int pad_left, int pad_right, bool skip_io_mode);
  void updateDensityPenalty(float curr_hpwl, float last_hpwl, float dhpwl_ref);
  float updateGamma(float curr_overflow);
  void updateBest();
  void restoreBest();

 private:
  odb::dbDatabase* db_;
  utl::Logger* log_;
  std::unique_ptr<Graphics> gui_;

  std::shared_ptr<NesterovOptimizer> nesterov_;

  std::shared_ptr<WAwirelength> wa_wirelength_;
  std::vector<std::shared_ptr<EDensity>> e_density_vec_;

  std::shared_ptr<gpl::PlacerBaseCommon> pbc_;
  std::vector<std::shared_ptr<gpl::PlacerBase>> pbVec_;

  bool debug_ = false;
  bool draw_bins_ = false;
  bool disable_wirelength_ = false;
  bool disable_density_ = false;
  bool use_density_field_ = false;
  bool use_preconditioning_ = true;
  int pause_interval_ = -1;

  float cost_ = 0;
  float density_cost_ = 0;

  float density_penalty_ = 0;

  float total_density_gradient_ = 0;
  float total_wa_gradient_ = 0;
  int64_t last_hpwl_ = 0;
};

}  // namespace epl
