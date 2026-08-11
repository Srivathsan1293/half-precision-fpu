# tb/firmware/demo_zhinx.S
#
# Phase 7 run-and-dump example: a hand-written standard-Zhinx program that
# mixes hardware FP ops (fadd.h / fmul.h), emulated FP ops (fcvt.w.h / fmin.h),
# and plain integer code. Results land at 0x1000; magic + done at 0x1C00/0x1C04.
#
#   ./run_cpu_test.sh run tb/firmware/demo_zhinx.S
#
# Zhinx holds FP values in the integer register file (no F registers).

    .section .text._start
    .globl _start
_start:
    # Unmask the ebreak IRQ so emulated FP ops can trap to the 0x800 handler
    # (PicoRV32 resets irq_mask = ~0).
    .word 0x0600000B                    # maskirq x0, 0

    li      t0, 0x3C00                  # 1.0
    li      t1, 0x4000                  # 2.0
    fadd.h  t2, t0, t1                  # hardware:  t2 = 3.0  (0x4200)
    fcvt.w.h t3, t0                     # emulated:  t3 = 1    (0x00000001)
    fmul.h  t2, t2, t1                  # hardware:  t2 = 6.0  (0x4600)
    fmin.h  t4, t0, t1                  # emulated:  t4 = 1.0  (0x3C00)

    addi    t5, t3, 41                  # integer:   t5 = 42

    li      t6, 0x1000                  # results base
    sw      t2, 0(t6)
    sw      t3, 4(t6)
    sw      t4, 8(t6)
    sw      t5, 12(t6)

    # test magic + done marker
    li      t0, 0x1C00
    li      t1, 0x5F50555D
    sw      t1, 0(t0)
    li      t1, 0xDEADBEEF
    sw      t1, 4(t0)
1:  j       1b
