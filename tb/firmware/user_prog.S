# tb/firmware/user_prog.S
#
# Fresh user-program template for the PicoRV32 + FPU-PCPI SoC.
#
# Copy this file (e.g. `cp user_prog.S myprog.S`), fill in your code, and run:
#
#   ./run_cpu_test.sh run tb/firmware/myprog.S 100000   # dumps RAM+GPRs to testing_results/dump.txt
#
# What you get:
#   - Hardware FP ops (fadd.h/fsub.h/fmul.h/fdiv.h) run on the real RTL
#     datapaths via the PCPI coprocessor (2-cycle FADD/FSUB/FMUL, variable-latency SRT FDIV).
#   - Emulated FP ops (fmin/fmax/feq/flt/fle/fcvt*/fsgnj*/fclass) trap to the
#     0x800 IRQ handler, run in the C emulator, and resume transparently.
#   - Plain RV32I instructions run natively.
#
# Zhinx note: FP values live in the integer register file (no F registers).
#
# Memory layout (link.ld):
#   0x0000-0x07FF  your .text (must stay below 0x800)
#   0x0800-0x0FFF  IRQ stub + software emulator (do not place code here)
#   0x1000-0x1BFF  free scratch / results region
#   0x1C00         magic     (0x5F50555D  -> "done" recognized by the harness)
#   0x1C04         done marker (0xDEADBEEF)
#   0x2000         stack top (stack grows down)

    .section .text._start
    .globl  _start
_start:
    # Unmask the ebreak IRQ so emulated FP ops can trap to the 0x800 handler
    # (PicoRV32 resets irq_mask = ~0).
    .word 0x0600000B                    # maskirq x0, 0

    # ====================== USER CODE HERE ======================
    # Example: 1.0 + 2.0 in hardware, then convert the result to an int
    # in software (emulated). Delete and replace with your own program.
    li      t0, 0x3C00                  # 1.0
    li      t1, 0x4000                  # 2.0
    fadd.h  t2, t0, t1                  # hardware: t2 = 3.0 (0x4200)
    fcvt.w.h t3, t2                     # emulated: t3 = 3
    # ==================== END USER CODE ========================

    # Store results (harness dumps RAM after the done marker; look in
    # testing_results/dump.txt for the memory image + final GPRs).
    li      t6, 0x1000
    sw      t2, 0(t6)
    sw      t3, 4(t6)

    # Magic + done marker: tells the harness the program finished.
    li      t0, 0x1C00
    li      t1, 0x5F50555D              # MAGIC_ZHINX (arbitrary; run mode ignores it)
    sw      t1, 0(t0)
    li      t1, 0xDEADBEEF
    sw      t1, 4(t0)
1:  j       1b                          # halt loop
