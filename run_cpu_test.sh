#!/usr/bin/env bash
# run_cpu_test.sh — build & run the PicoRV32 + FPU-PCPI SoC integration test.
#
# Usage:
#   ./run_cpu_test.sh            baseline RV32I smoke test (no coprocessor)
#   ./run_cpu_test.sh fpu        FPU PCPI test (requires the fpu_pcpi wrapper:
#                                enables HAS_FPU_PCPI and the FPU src files)
#   ./run_cpu_test.sh stress     exhaustive FPU PCPI stress test (numeric sweep
#                                + CPU edge cases; same wrapper requirements)
#   ./run_cpu_test.sh bench      Week 2 SW-vs-HW cycle-count benchmark (soft-float
#                                vs PCPI FIR; same wrapper requirements)
#   ./run_cpu_test.sh benchmm    Week 2 SW-vs-HW cycle-count benchmark (4x4 fp16
#                                matrix multiply; same wrapper requirements)
#   ./run_cpu_test.sh benchdig   Week 2 SW-vs-HW cycle-count benchmark (5-tap
#                                fp16 digital FIR filter; same wrapper requirements)
#   ./run_cpu_test.sh benchdiv   Week 2 SW-vs-HW cycle-count benchmark (fp16
#                                vector divide; same wrapper requirements)
#   ./run_cpu_test.sh spike      validate the ebreak-IRQ resume-PC premise
#                                (no wrapper: an unclaimed FP instruction must
#                                trap to the 0x800 handler with P+4 in q-reg 0)
#   ./run_cpu_test.sh emu        software-emulator self-test (Zhinx asm driving
#                                only emulated ops through the 0x800 handler)
#   ./run_cpu_test.sh zhinx      standard-Zhinx integration (clang rv32im_zhinx:
#                                FADD/FSUB/FMUL/FDIV in hardware, rest emulated)
#   ./run_cpu_test.sh run [prog.S] [max_cycles]
#                                Phase 7: build the user's own program (assembly
#                                or C, default tb/firmware/demo_zhinx.S), run the
#                                sim, then dump RAM + GPRs to
#                                testing_results/dump.txt (no golden checks).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

MODE="${1:-baseline}"
OBJDIR="obj_dir_tb_picorv32"

MODEL=""
EXTRA_DEF=""
WRAPPER_SRC=""
DUMP_ARGS=""
if [ "$MODE" = "fpu" ]; then
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=1"
elif [ "$MODE" = "stress" ]; then
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=stress"
elif [ "$MODE" = "bench" ]; then
    # Week 2 SW-vs-HW cycle-count benchmark (soft-float vs PCPI FIR). The core
    # has the M extension so the SW phase is a generic real-world soft-float
    # baseline (see soc_fpu_top.sv).
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=bench"
elif [ "$MODE" = "benchmm" ]; then
    # Week 2 SW-vs-HW cycle-count benchmark (4x4 fp16 matrix multiply).
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=benchmm"
elif [ "$MODE" = "benchdig" ]; then
    # Week 2 SW-vs-HW cycle-count benchmark (5-tap fp16 digital FIR filter).
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=benchdig"
elif [ "$MODE" = "benchdiv" ]; then
    # Week 2 SW-vs-HW cycle-count benchmark (fp16 vector divide).
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=benchdiv"
elif [ "$MODE" = "spike" ]; then
    EXTRA_DEF=""
    WRAPPER_SRC=""
    MODEL="FPU_TEST=spike"
elif [ "$MODE" = "emu" ]; then
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=emu"
elif [ "$MODE" = "zhinx" ]; then
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    MODEL="FPU_TEST=zhinx"
elif [ "$MODE" = "run" ]; then
    # Phase 7: run-and-dump mode. Build the user's program + IRQ stub + emulator
    # so both hardware and emulated FP ops work, then dump RAM/GPRs (no golden).
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    PROG="${2:-tb/firmware/demo_zhinx.S}"
    CYCLES="${3:-10000}"
    PROG_ABS="$(realpath "$PROG")"
    MODEL=""
    DUMP_ARGS="--dump testing_results/dump.txt --maxcycles $CYCLES"
elif [ "$MODE" = "asmall" ]; then
    # asm_all_ops.S test with golden checks (no --dump, so MAGIC_ASM_ALL dispatches
    # to check_asm_all()). The .S file's register usage is fixed to avoid clobbering
    # result registers before the final store.
    EXTRA_DEF="-DHAS_FPU_PCPI"
    WRAPPER_SRC="src/fpu_pcpi.sv"
    PROG="${2:-tb/firmware/asm_all_ops.S}"
    CYCLES="${3:-10000}"
    PROG_ABS="$(realpath "$PROG")"
    MODEL=""
    DUMP_ARGS="--maxcycles $CYCLES"  # no --dump, golden checks will run
fi

echo "==> Building firmware ($MODE) ..."
make -C tb/firmware clean >/dev/null 2>&1 || true
if [ "$MODE" = "run" ] || [ "$MODE" = "asmall" ]; then
    # "run" and "asmall" both build the user's program + IRQ stub + emulator.
    # The only difference is the sim args: asmall has no --dump so the golden
    # checks (MAGIC_ASM_ALL) dispatch instead of dumping state.
    make -C tb/firmware FPU_TEST=run RUN_SRCS="$PROG_ABS fpu_irq_stub.S fpu_emulator.c"
else
    # shellcheck disable=SC2086
    make -C tb/firmware $MODEL
fi

echo "==> Verilating SoC top (soc_fpu_top) ..."
verilator --cc --trace --build -j \
    --top-module soc_fpu_top \
    --Mdir "$OBJDIR" \
    -Wno-TIMESCALEMOD \
    --public-flat-rw \
    $EXTRA_DEF \
    $WRAPPER_SRC \
    src/fpu_test.sv src/fpu_FMUL.sv src/fpu_FADDSUB.sv src/fpu_FDIV.sv \
    src/fpu_modules.sv src/fdiv_datapath_blocks.sv \
    third_party/picorv32.v \
    tb/soc_fpu_top.sv \
    --exe tb/tb_fpu_pcpi.cpp

echo "==> Running simulation ..."
# shellcheck disable=SC2086
"$OBJDIR/Vsoc_fpu_top" $DUMP_ARGS
