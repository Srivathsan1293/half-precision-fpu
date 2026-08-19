read_liberty sky130_fd_sc_hd__ss_100C_1v60.lib
read_verilog synth_outputs/DIV_synth_ss.v
link_design DIV
create_clock -name VCLK -period 10.0 [get_ports clk]
set_input_delay -clock VCLK 0.0 [all_inputs]
set_output_delay -clock VCLK 0.0 [all_outputs]

puts "=== SRT REG-TO-REG LOOP (R registers) ==="
set regs [get_pins -hierarchical "*srt*.R*" -filter {direction eq "output"}]
puts "Found [llength $regs] register outputs"
# Probe the longest reg->reg loop through QSL+subtractor
report_timing -from $regs -to $regs -path_delay max -format full -digits 3 | sort -k2 -rn | head -6