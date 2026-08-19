# STA Script to split FMUL latency into two stages:
# Stage 1: Inputs -> Accumulator Register (prod[19])
set start_node a[10]  ; # Assuming a[10] is the earliest input driver for timing analysis.
set end_node prod[19];

puts "=== STAGE-1: Inputs to Accumulator ==="
report_timing -from $start_node -to $end_node -path_delay max -format full -digits 3

# Stage 2: Accumulator Register -> Output (ans[9])
set start_node prod[19]; # Output of the accumulator register
set end_node ans[9];    # Final output port

puts "\n=== STAGE-2: Accumulator to Output ==="
report_timing -from $start_node -to $end_node -path_delay max -format full -digits 3