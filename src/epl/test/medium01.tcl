source helpers.tcl
set test_name medium01
read_lef ./Nangate45/Nangate45.lef
read_def ./designs/$test_name.def

#epl::eplace_debug -pause_interval 0
#epl::eplace_debug -draw_bins -pause_interval 1 -disable_density
epl::eplace_place -i 500 -density 0.7
#-dhpwl_ref 446000000 -info_interval 1 -initial_density_penalty_mult 0.00008

set def_file [make_result_file $test_name.def]
write_def $def_file
diff_file $def_file $test_name.defok
source report_hpwl.tcl
