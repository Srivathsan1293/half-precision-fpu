read_liberty sky130_fd_sc_hd__tt_025C_1v80.lib
read_verilog synth_outputs/ADDSUB_synth.v
link_design addsub
create_clock -name VCLK -period 10.0
set_input_delay -clock VCLK 0.0 [all_inputs]
set_output_delay -clock VCLK 0.0 [all_outputs]
puts "=========================================================="
puts "                CRITICAL PATH TIMING (LTP)                "
puts "=========================================================="
report_checks -path_delay max -format full -digits 3
puts "=========================================================="
puts "                     POWER ESTIMATION                     "
puts "=========================================================="
set_power_activity -input -activity 0.1
report_power
