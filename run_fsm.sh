#!/usr/bin/env bash
# run_fsm.sh — wrapper-only PCPI state-machine exact-timing test.
#
# Verilates src/fpu_pcpi.sv (with the FPU datapath sources) as the top module
# and drives the PCPI bus directly from tb/tb_pcpi_fsm.cpp. Asserts the exact
# counter->ready cycle counts (FADD/FSUB/FMUL at counter==1, FDIV at counter==3)
# and the no-early-fire / single-pulse / no-re-trigger rules. No CPU/firmware.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

OBJDIR="obj_dir_pcpi_fsm"
BUILD_ONLY=0
[ "${1:-}" = "--build-only" ] && BUILD_ONLY=1

echo "==> Verilating fpu_pcpi (wrapper-only FSM timing test) ..."
verilator --cc --build -j \
    --top-module fpu_pcpi \
    --Mdir "$OBJDIR" \
    -Wno-TIMESCALEMOD \
    src/fpu_pcpi.sv src/fpu_test.sv src/fpu_FMUL.sv src/fpu_FADDSUB.sv \
    src/fpu_FDIV.sv src/fpu_modules.sv src/fdiv_datapath_blocks.sv \
    --exe tb/tb_pcpi_fsm.cpp

if [ "$BUILD_ONLY" -eq 1 ]; then
    echo "==> Build-only (skipping FSM timing verification) ..."
    exit 0
fi

echo "==> Running FSM timing verification ..."
"$OBJDIR/Vfpu_pcpi"
