# 1. Read the SkyWater 130nm Liberty file
read_liberty sky130_fd_sc_hd__tt_025C_1v80.lib

# 2. Read the synthesized structural netlist from Yosys
read_verilog FMUL_synth.v

# 3. Link the design to the top module
link_design FMUL

# 4. Create a virtual clock for purely combinational timing
# We set a hypothetical target of 10.0 ns (100 MHz).
# OpenSTA will report the Slack. If Slack is -2.34 ns, your actual delay is 12.34 ns.
create_clock -name VCLK -period 10.0
set_input_delay -clock VCLK 0.0 [all_inputs]
set_output_delay -clock VCLK 0.0 [all_outputs]

# 5. Report the critical path (Setup Time / Maximum Delay)
puts "=========================================================="
puts "                CRITICAL PATH TIMING (LTP)                "
puts "=========================================================="
report_checks -path_delay max -format full -digits 3

# 6. Estimate Power Consumption (Assuming a 10% toggle rate on inputs)
puts "=========================================================="
puts "                     POWER ESTIMATION                     "
puts "=========================================================="
set_power_activity -input -activity 0.1
report_power
