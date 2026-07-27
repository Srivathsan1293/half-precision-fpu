#!/bin/bash
# Run PPA for all 3 FPU modules

TOP=$1
LIB="sky130_fd_sc_hd__tt_025C_1v80.lib"

yosys -Q -l synth_outputs/synth_report_${TOP}.txt -p "
# 1. Read all SystemVerilog source files
read_verilog -sv src/fpu_modules.sv

# 2. Read the module-specific file
case ${TOP} in
    FMUL | MUL)
        read_verilog -sv src/fpu_FMUL.sv
        ;;
    FADDSUB | addsub)
        read_verilog -sv src/fpu_FADDSUB.sv
        ;;
    FDIV | DIV)
        read_verilog -sv src/fpu_FDIV.sv
        ;;
esac

# 3. High-level synthesis with automatic flattening
synth -top ${TOP} -flatten

# 4. Read the SkyWater 130nm Liberty file for technology mapping
read_liberty -lib $LIB

# 5. Map flip-flops and gate logic to Sky130 standard cells
dfflibmap -liberty $LIB
abc -liberty $LIB

# 6. Clean up dangling wires
clean

# 7. Print Area & Gate Count Statistics
echo \"==========================================\"
echo \"         AREA AND GATE COUNT              \"
echo \"==========================================\"
stat -liberty $LIB

# 8. Internal STA (combinational delay estimation)
echo \"==========================================\"
echo \"         TIMING ANALYSIS                  \"
echo \"==========================================\"
setundef -zero
splitnets
sta -top ${TOP} -liberty $LIB
"
