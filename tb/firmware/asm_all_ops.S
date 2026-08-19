# tb/firmware/asm_all_ops.S
#
# Calls EVERY Zhinx instruction once with fixed operands (hardware FADD/FSUB/
# FMUL/FDIV plus the software-emulated FMIN/FMAX/FEQ/FLT/FLE/FCVT*/FSGNJ*/FCLASS),
# then stores the 17 results at 0x1000 + 4*i. The harness (tb_fpu_pcpi.cpp,
# MAGIC_ASM_ALL == 0x5F505541) recomputes the same table as a golden model and
# compares. This proves each instruction's encoding + execution path produces
# the correct answer.
#
    # Operands (must stay in lockstep with check_asm_all()'s asm_ops table):
    #   0  FADD.H   1.0+2.0          (0x3C00,0x4000)  -> 0x4200 (3.0)
    #   1  FSUB.H   2.0-1.0          (0x4000,0x3C00)  -> 0x3C00 (1.0)
    #   2  FMUL.H   2.0*3.0          (0x4000,0x4200)  -> 0x4600 (6.0)
    #   3  FDIV.H   3.0/2.0          (0x4200,0x4000)  -> 0x3E00 (1.5)
    #   4  FMIN.H  -2.0,1.0          (0xC000,0x3C00)  -> 0xC000 (-2.0)
    #   5  FMAX.H   1.0,2.0          (0x3C00,0x4000)  -> 0x4000 (2.0)
    #   6  FEQ.H    1.0,1.0          (0x3C00,0x3C00)  -> 1
    #   7  FLT.H    2.0,1.0          (0x4000,0x3C00)  -> 0
    #   8  FLE.H    1.0,1.0          (0x3C00,0x3C00)  -> 1
    #   9  FCVT.H.W 5                (rs1 = 0x00000005) -> 0x4500 (5.0)
    #   10 FCVT.H.WU 65535           (rs1 = 0x0000FFFF) -> 0x7C00 (+inf, 65535 > max half)
    #   11 FCVT.W.H 1.5              (rs1 = 0x3E00)   -> 2 (RNE of 1.5)
    #   12 FCVT.WU.H 96.0            (rs1 = 0x5600)   -> 96
    #   13 FSGNJ.H  2.0,-1.0         (0x4000,0xBC00)  -> 0xC000 (-2.0)
    #   14 FSGNJN.H 2.0,1.0          (0x4000,0x3C00)  -> 0xC000 (-2.0)
    #   15 FSGNJX.H -2.0,1.0         (0xC000,0x3C00)  -> 0xC000 (-2.0)
    #   16 FCLASS.H qNaN             (rs1 = 0x7E00)   -> 0x200 (quiet NaN)
#
# Note: only s7 is used as the store pointer; every other register holds a
# result. All result values survive the trap because the IRQ stub saves/restores
# the full GPR file around the emulator call.

    .section .text._start
    .globl  _start
_start:
    .word   0x0600000B          # maskirq x0, 0: unmask the ebreak IRQ

    # --- hardware ops (2-cycle FADD/FSUB/FMUL, variable-latency SRT FDIV) ---
    li      t0, 0x3C00          # 1.0
    li      t1, 0x4000          # 2.0
    fadd.h  s0, t0, t1          # r0 = 0x4200 (3.0)
    li      t2, 0x4000          # 2.0
    li      t3, 0x3C00          # 1.0
    fsub.h  s1, t2, t3          # r1 = 0x3C00 (1.0)
    li      t4, 0x4200          # 3.0
    fmul.h  s2, t2, t4          # r2 = 0x4600 (6.0)
    li      t5, 0x4200          # 3.0
    li      t6, 0x4000          # 2.0
    fdiv.h  s3, t5, t6          # r3 = 0x3E00 (1.5)

    # --- emulated ops (trap to 0x800 handler) ---
    li      t0, 0xC000          # -2.0
    li      t1, 0x3C00          # 1.0
    fmin.h  s4, t0, t1          # r4 = 0xC000 (-2.0)
    li      t2, 0x3C00          # 1.0
    li      t3, 0x4000          # 2.0
    fmax.h  s5, t2, t3          # r5 = 0x4000 (2.0)
    li      t4, 0x3C00          # 1.0
    li      t5, 0x3C00          # 1.0
    feq.h   s6, t4, t5          # r6 = 1
    li      t6, 0x4000          # 2.0
    li      a0, 0x3C00          # 1.0
    flt.h   a1, t6, a0          # r7 = 0 (2.0 < 1.0 false)
    li      a2, 0x3C00          # 1.0
    li      a3, 0x3C00          # 1.0
    fle.h   a4, a2, a3          # r8 = 1

    li      a5, 5
    fcvt.h.w a6, a5             # r9  = 0x4800 (5.0)
    li      a7, 0xFFFF
    fcvt.h.wu s8, a7            # r10 = 0x7BFF (65535.0)

    li      t0, 0x3E00          # 1.5
    fcvt.w.h t1, t0             # r11 = 2 (RNE half-to-even: 1.5→2)
    li      t2, 0x5600          # 96.0
    fcvt.wu.h t3, t2            # r12 = 96

    li      t4, 0x4000          # 2.0
    li      t5, 0xBC00          # -1.0
    fsgnj.h t6, t4, t5          # r13 = 0xC000 (-2.0)
    li      t4, 0x4000          # 2.0
    li      t5, 0x3C00          # 1.0
    fsgnjn.h a2, t4, t5         # r14 = 0xC000 (-2.0)
    li      t4, 0xC000          # -2.0
    li      t5, 0x3C00          # 1.0
    fsgnjx.h a5, t4, t5         # r15 = 0xC000 (-2.0)

    li      t4, 0x7E00          # qNaN
    fclass.h a7, t4             # r16 = 0x200 (quiet NaN)

    # --- store results at 0x1000 + 4*i ---
    li      s7, 0x1000
    sw      s0, 0(s7)           # FADD
    sw      s1, 4(s7)           # FSUB
    sw      s2, 8(s7)           # FMUL
    sw      s3, 12(s7)          # FDIV
    sw      s4, 16(s7)          # FMIN
    sw      s5, 20(s7)          # FMAX
    sw      s6, 24(s7)          # FEQ
    sw      a1, 28(s7)          # FLT
    sw      a4, 32(s7)          # FLE
    sw      a6, 36(s7)          # FCVT.H.W
    sw      s8, 40(s7)          # FCVT.H.WU
    sw      t1, 44(s7)          # FCVT.W.H
    sw      t3, 48(s7)          # FCVT.WU.H
    sw      t6, 52(s7)          # FSGNJ
    sw      a2, 56(s7)          # FSGNJN
    sw      a5, 60(s7)          # FSGNJX
    sw      a7, 64(s7)          # FCLASS

    # magic + done
    li      t0, 0x5F505541
    li      t1, 0x1C00
    sw      t0, 0(t1)
    li      t0, 0xDEADBEEF
    sw      t0, 4(t1)
.Lhalt:
    j       .Lhalt
