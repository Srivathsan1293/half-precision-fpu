read_liberty sky130_fd_sc_hd__ss_100C_1v60.lib
read_verilog synth_outputs/fpu_pcpi_synth_ss.v
link_design fpu_pcpi
create_clock -name VCLK -period 10.0 [get_ports clk]
set_input_delay -clock VCLK 0.0 [all_inputs]
set_output_delay -clock VCLK 0.0 [all_outputs]

set pnm [get_nets -filter {name =~ *pre_norm_man*}]
set pnm_pins [get_pins -of_objects $pnm]
puts "pins on pre_norm_man nets: [llength $pnm_pins]"
set q_pins {}
set d_pins {}
foreach p $pnm_pins {
    set dir [get_property $p direction]
    if {$dir eq "output"} { lappend q_pins $p }
}
puts "Q pins: [llength $q_pins]"
foreach p [lrange $q_pins 0 3] { puts "  $p" }

puts "=== STAGE-2 worst: pre_norm_man Q flops -> ans/pcpi (normalize+round+mux) ==="
report_checks -from $q_pins -path_delay max -format full -digits 3

puts "=== STAGE-1 worst: inputs -> pre_norm_man D flops (align+add/sub+sign) ==="
set d_pins {}
foreach q $q_pins {
    set cell [get_property $q cell]
    set d [get_pins -of_objects $cell -filter {direction == input && name =~ *D*}]
    lappend d_pins $d
}
report_checks -through $d_pins -path_delay max -format full -digits 3