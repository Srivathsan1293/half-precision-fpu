# sta_bench_common.tcl — shared PPA measurement body for all FPU tops.
# Sourced by the per-top STA scripts (sta_<top>.tcl) which set, before sourcing:
#     set TOP    <module name as linked>
#     set NETLIST <path to synthesized netlist>
#     set CLK_NS  <clock period in ns>
# Run from the repo root (liberties + synth_outputs are relative paths).
#
# Emits (in addition to the standard reports) parseable output consumed by
# tools/extract_metrics.py:
#   BENCH top=... netlist=... clk_ns=... activity=...
#   report_wns ->  "wns <value>"

set LIB_PATH sky130_fd_sc_hd__tt_025C_1v80.lib

if {![info exists CLK_NS]}    { set CLK_NS 10.0 }
if {![info exists ACTIVITY]}  { set ACTIVITY 0.1 }

read_liberty $LIB_PATH
read_verilog $NETLIST
link_design $TOP
create_clock -name VCLK -period $CLK_NS [get_ports clk]

set_input_delay -clock VCLK 0.0 [all_inputs]
set_output_delay -clock VCLK 0.0 [all_outputs]

set_power_activity -input -activity $ACTIVITY

puts "=========================================================="
puts "              CRITICAL PATH TIMING (LTP)                  "
puts "=========================================================="
report_checks -path_delay max -format full -digits 3

puts "=========================================================="
puts "                   POWER ESTIMATION                       "
puts "=========================================================="
report_power

puts "BENCH top=$TOP netlist=$NETLIST clk_ns=$CLK_NS activity=$ACTIVITY"