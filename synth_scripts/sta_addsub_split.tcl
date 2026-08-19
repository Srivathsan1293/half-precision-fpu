read_liberty sky130_fd_sc_hd__ss_100C_1v60.lib
read_verilog synth_outputs/ADDSUB_synth_ss.v
link_design addsub
create_clock -name VCLK -period 10.0 [get_ports clk]
set_input_delay -clock VCLK 0.0 [all_inputs]
set_output_delay -clock VCLK 0.0 [all_outputs]

set pnm [get_nets -filter {name =~ *pre_norm_man*}]
puts "=== STAGE-1: inputs -> addsub_man/pre_norm_man D ==="
set am [get_nets -filter {name =~ *addsub_man*}]
report_checks -through $am -path_delay max -format full -digits 3

puts "=== STAGE-2: pre_norm_man -> ans ==="
set pnm_pins [get_pins -of_objects $pnm]
set q_pins {}
foreach p $pnm_pins {
    set dir [get_property $p direction]
    if {$dir eq "output"} { lappend q_pins $p }
}
puts "Q pins: [llength $q_pins]"
report_checks -from $q_pins -path_delay max -format full -digits 3