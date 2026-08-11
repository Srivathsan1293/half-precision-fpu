# tb/firmware/fpu_unsup_main.S
#
# Stage E unsupported-op halt probe. Executes ONE genuinely unsupported
# standard-Zhinx instruction (FSQRT.H, funct7=0x2E) that is claimed by neither
# the PCPI wrapper (FADD/FSUB/FMUL/FDIV only) nor the software emulator
# (FSQRT/FMA deliberately unsupported). Expected behaviour:
#    1. the instruction traps to the 0x800 ebreak-IRQ handler
#    2. the emulator recognizes funct7=0x2E as unsupported
#    3. it writes the faulting instruction word to 0x1C08 and the resume PC
#       (P+4) to 0x1C0C, then halts (infinite loop)
# The harness (check_unsup, MAGIC_UNSUP == 0x5F505546) verifies that the
# captured instruction matches the word at the recorded probe PC and that the
# resume PC is exactly P+4. Reaching this probe proves an unsupported op cannot
# silently fabricate a result: the core stops instead.
#
# 0x1C00 = MAGIC_UNSUP      (written before the probe so the harness dispatches)
# 0x1C08 = faulting insn    (written by emu_halt)
# 0x1C0C = resume PC P+4    (written by emu_halt)
# 0x1C10 = probe PC P       (written here, before the probe)

    .section .text._start
    .globl  _start
_start:
    .word   0x0600000B          # maskirq x0, 0: unmask the ebreak IRQ

    # magic + probe-PC bookkeeping
    li      t0, 0x5F505546
    li      t1, 0x1C00
    sw      t0, 0(t1)           # TEST_MAGIC_ADDR = MAGIC_UNSUP
    la      t0, unsup_insn
    li      t1, 0x1C10
    sw      t0, 0(t1)           # PROBE_PC_ADDR = P (byte address)

    # an operand (value irrelevant: the op is never executed)
    li      a0, 0x3C00          # 1.0h

unsup_insn:
    .word   0x5C050653          # fsqrt.h x12, x10   (funct7=0x2E, unsupported)

    # ---- never reached: emulator halts inside the IRQ handler ----
    li      t0, 0xDEADBEEF
    li      t1, 0x1C04
    sw      t0, 0(t1)           # DONE_MAGIC (must NOT appear)
.Lhalt:
    j       .Lhalt
