// Copyright (c) 2021, The Regents of the Federal University of Santa Catarina
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

%{
#include "epl/EPlace.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"

namespace ord {
  OpenRoad* getOpenRoad();
  epl::EPlace* getEPlace();
}

using epl::EPlace;
using ord::getEPlace;
using ord::getOpenRoad;

%}

%inline%{
void eplace_place_cmd(float density,
                      bool uniform_density,
                      float dhpwl_ref,
                      int iterations,
                      float initial_density_penalty_mult,
                      int pad_left,
                      int pad_right,
                      bool skip_io_mode,
                      int info_interval,
                      bool use_step_new)
{
  EPlace* eplace = getEPlace();
  int threads = ord::OpenRoad::openRoad()->getThreadCount();
  eplace->place(threads,
                density,
                uniform_density,
                dhpwl_ref,
                iterations,
                initial_density_penalty_mult,
                pad_left,
                pad_right,
                skip_io_mode,
                info_interval,
                use_step_new);
}

void eplace_debug_cmd(bool draw_bins,
                      bool disable_wirelength,
                      bool disable_density,
                      int pause_interval)
{
  EPlace* eplace = getEPlace();
  eplace->set_debug(
      draw_bins, disable_wirelength, disable_density, pause_interval);
}

void calcualte_WaHPWL_cmd(float gamma)
{
  EPlace* eplace = getEPlace();
  eplace->calcualteWaHPWL(gamma);
}

void eplace_random_placement_cmd()
{
  EPlace* eplace = getEPlace();
  int threads = ord::OpenRoad::openRoad()->getThreadCount();
  eplace->randomPlace(threads);
}

%}  // inline
