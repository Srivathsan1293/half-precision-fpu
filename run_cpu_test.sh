#!/usr/bin/env bash
# run_cpu_test.sh — build & run the PicoRV32 + FPU-PCPI SoC integration test.
#
# Usage:
#   ./run_cpu_test.sh            baseline RV32I smoke test (no coprocessor)
#   ./run_cpu_test.sh fpu        FPU PCPI test (requires the fpu_pcpi wrapper:
#                                enables HAS_FPU_PCPI and the FPU src files)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

MODE="${1:-baseline}"
OBJDIR="obj_dir_tb_picorv32"

EXTRA_DEF=""
WRAPPER_SRC=""
if [ "$MODE" = "fpu" ]; then
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
fi

echo "==> Building firmware ($MODE) ..."
make -C tb/firmware clean >/dev/null 2>&1 || true
if [ "$MODE" = "fpu" ]; then
    make -C tb/firmware FPU_TEST=1
else
    make -C tb/firmware
fi

echo "==> Verilating SoC top (soc_fpu_top) ..."
verilator --cc --trace --build -j \
    --top-module soc_fpu_top \
    --Mdir "$OBJDIR" \
    -Wno-TIMESCALEMOD \
    $EXTRA_DEF \
    $WRAPPER_SRC \
    src/fpu_test.sv src/fpu_FMUL.sv src/fpu_FADDSUB.sv src/fpu_FDIV.sv \
    src/fpu_modules.sv src/fdiv_datapath_blocks.sv \
    third_party/picorv32.v \
    tb/soc_fpu_top.sv \
    --exe tb/tb_fpu_pcpi.cpp

echo "==> Running simulation ..."
"$OBJDIR/Vsoc_fpu_top"
