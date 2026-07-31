read_liberty NangateOpenCellLibrary_typical.lib
read_verilog synth_outputs/FMUL_synth_nangate45.v
link_design FMUL
create_clock -name VCLK -period 1.0
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
