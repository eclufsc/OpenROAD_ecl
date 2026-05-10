source helpers.tcl
set test_name simple01
read_lef ./Nangate45/Nangate45.lef
read_def ./designs/simple01.def

#set_debug_level EPL place 1
#set_debug_level EPL Nesterov 5
#set_debug_level EPL Nesterov::stepLength 5

epl::eplace_debug -pause_interval 0
# -draw_bins -pause_interval 1
epl::eplace_place -i 500 -info_interval 1 -density 0.7

set def_file [make_result_file $test_name.def]
write_def $def_file
diff_file $def_file $test_name.defok
source report_hpwl.tcl