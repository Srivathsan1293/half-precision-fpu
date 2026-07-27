read_liberty sky130_fd_sc_hd__tt_025C_1v80.lib
read_verilog synth_outputs/block2_datapath_synth.v
link_design fdiv_div_datapath
create_clock -name VCLK -period 10.0
set_input_delay -clock VCLK 0.0 [all_inputs]
set_output_delay -clock VCLK 0.0 [all_outputs]
report_checks -path_delay max -format full -digits 3
set_power_activity -input -activity 0.1
report_power
