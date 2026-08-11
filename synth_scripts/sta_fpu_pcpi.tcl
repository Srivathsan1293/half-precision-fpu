read_liberty sky130_fd_sc_hd__tt_025C_1v80.lib
read_verilog synth_outputs/fpu_pcpi_synth.v
link_design fpu_pcpi
create_clock -name VCLK -period 10.0 [get_ports clk]
puts "=========================================================="
puts "      fpu_pcpi CRITICAL PATH TIMING (LTP)                "
puts "=========================================================="
report_checks -path_delay max -format full -digits 3
puts "=========================================================="
puts "              fpu_pcpi POWER ESTIMATION                  "
puts "=========================================================="
set_power_activity -input -activity 0.1
report_power
