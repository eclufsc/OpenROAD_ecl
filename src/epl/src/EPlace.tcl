# Copyright (c) 2021, The Regents of the University of California
# All rights reserved.
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.


# Put helper functions in a separate namespace so they are not visible
# too users in the global namespace.
namespace eval epl {

sta::define_cmd_args "eplace_place" { \
    [-density target_density] \
    [-iterations max_iterations] \
    [-dhpwl_ref dhpwl_ref] \
    [-initial_density_penalty_mult initial_density_penalty_mult] \
    [-info_interval info_interval] \
    [-nesterov_step step|step_new]
}

proc eplace_place { args } {
  sta::parse_key_args "global_placement" args \
    keys {-density -iterations -dhpwl_ref -initial_density_penalty_mult -info_interval -nesterov_step} \
    flags {}
  
  # density settings
  set target_density 0
  set uniform_mode 1

  if { [info exists keys(-density)] } {
    set target_density $keys(-density)
  }

  if { $target_density == "uniform" } {
    set uniform_mode 1
  } else {
    set uniform_mode 0
    sta::check_positive_float "-density" $target_density
    if { $target_density > 1.0 } {
      utl::error EPL 10 "Target density must be in \[0, 1\]."
    }
    if { $target_density == 0 } {
      set uniform_mode 1
    }
  }

  set iterations 100
  if { [info exists keys(-iterations)] } {
    set iterations $keys(-iterations)
  }

  set dhpwl_ref 446000000
  if { [info exists keys(-dhpwl_ref)] } {
    set dhpwl_ref $keys(-dhpwl_ref)
  }

  set initial_density_penalty_mult 0.00008
  if { [info exists keys(-initial_density_penalty_mult)] } {
    set initial_density_penalty_mult $keys(-initial_density_penalty_mult)
  }

  set info_interval 10
  if { [info exists keys(-info_interval)] } {
    set info_interval $keys(-info_interval)
  }

  set use_step_new 0
  if { [info exists keys(-nesterov_step)] } {
    set nesterov_step $keys(-nesterov_step)
    if { $nesterov_step == "step_new" } {
      set use_step_new 1
    } elseif { $nesterov_step != "step" } {
      utl::error EPL 22 "Invalid -nesterov_step value '$nesterov_step'. Use 'step' or 'step_new'."
    }
  }

  epl::eplace_place_cmd $target_density $uniform_mode $dhpwl_ref $iterations $initial_density_penalty_mult $info_interval $use_step_new
}

sta::define_cmd_args "eplace_debug" { \
    [-draw_bins] \
    [-disable_wirelength] \
    [-disable_density]
    [-pause_interval pause_interval]
}

proc eplace_debug { args } {
  sta::parse_key_args "global_placement" args \
    keys {-pause_interval} \
    flags {-draw_bins \
      -disable_wirelength \
      -disable_density}

  set draw_bins [info exists flags(-draw_bins)]
  set disable_wirelength [info exists flags(-disable_wirelength)]
  set disable_density [info exists flags(-disable_density)]
  if { $disable_wirelength && $disable_density } {
    utl::error EPL 14 "Cannot disable wirelength and density at the same time"
  }

  set pause_interval 10
  if { [info exists keys(-pause_interval)] } {
    set pause_interval $keys(-pause_interval)
  }

  eplace_debug_cmd $draw_bins $disable_wirelength $disable_density $pause_interval
}

sta::define_cmd_args "calcualte_WaHPWL" { \
    [-gamma gamma]
}
proc calcualte_WaHPWL { args } {
  sta::parse_key_args "calcualte_WaHPWL" args \
    keys {-gamma} \
    flags {}
  
  set gamma 1
  if { [info exists keys(-gamma)] } {
    set gamma $keys(-gamma)
  }

  epl::calcualte_WaHPWL_cmd $gamma
}

sta::define_cmd_args "eplace_random_placement" {}
proc eplace_random_placement { args } {
  epl::eplace_random_placement_cmd
}


sta::define_cmd_args "test_epl" {}
proc test_epl { args } {
  puts "EPL working"
}

}

