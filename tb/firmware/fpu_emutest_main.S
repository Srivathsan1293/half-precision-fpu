// tb/firmware/fpu_emutest_main.S
//
// Phase 4 self-test for the software FPU emulator. Exercises ONLY ops that are
// emulated (not the hardware FADD/FSUB/FMUL/FDIV): FCVT, FMIN/FMAX, FEQ/FLT/FLE,
// FSGNJ*, FMV, FCLASS. Each one traps to the 0x800 IRQ handler; results are
// written back to their destination registers, stored at 0x1000+, then magic
// 0x5F505345 @ 0x1C00 and done @ 0x1C04.
//
// Expected results (r[0..13]):
//   fcvt.h.w   1    -> 0x3C00
//   fcvt.h.w   2    -> 0x4000
//   fmin.h  1.0,2.0 -> 0x3C00
//   fmax.h  1.0,2.0 -> 0x4000
//   feq.h   1.0,2.0 -> 0
//   flt.h   1.0,2.0 -> 1
//   fle.h   1.0,2.0 -> 1
//   fsgnj.h 2.0,1.0 -> 0x4000
//   fsgnjn.h 2.0,1.0 -> 0xC000
//   fclass.h 1.0   -> 0x40   (+normal)
//   fmv.x.h 2.0    -> 0x00004000 (sign-extended)
//   fmv.h.x (0x3C00) -> 0x3C00
//   fcvt.w.h 1.0   -> 1
//   fcvt.wu.h 2.0  -> 2

    .section .text._start
    .globl  _start
_start:
    .word   0x0600000B          // maskirq x0, 0: unmask the ebreak IRQ

    li      a0, 1
    fcvt.h.w t0, a0             // r0 = 0x3C00
    li      a1, 2
    fcvt.h.w t1, a1             // r1 = 0x4000
    fmin.h  t2, t0, t1          // r2 = 0x3C00
    fmax.h  t3, t0, t1          // r3 = 0x4000
    feq.h   t4, t0, t1          // r4 = 0
    flt.h   t5, t0, t1          // r5 = 1
    fle.h   t6, t0, t1          // r6 = 1
    fsgnj.h s0, t1, t0          // r7 = 0x4000
    fsgnjn.h s1, t1, t0         // r8 = 0xC000
    fclass.h s2, t0             // r9 = 0x40
    .word   0xE40309D3          // fmv.x.h s3, t1   (Zfh 0x72 f3=000) -> sign-ext
    .word   0xF4028A53          // fmv.h.x s4, t0   (Zfh 0x7A f3=000)
    fcvt.w.h s5, t0             // r12 = 1
    fcvt.wu.h s6, t1            // r13 = 2

    li      s7, 0x1000
    sw      t0, 0(s7)
    sw      t1, 4(s7)
    sw      t2, 8(s7)
    sw      t3, 12(s7)
    sw      t4, 16(s7)
    sw      t5, 20(s7)
    sw      t6, 24(s7)
    sw      s0, 28(s7)
    sw      s1, 32(s7)
    sw      s2, 36(s7)
    sw      s3, 40(s7)
    sw      s4, 44(s7)
    sw      s5, 48(s7)
    sw      s6, 52(s7)

    // magic + done
    li      t0, 0x5F505345
    li      t1, 0x1C00
    sw      t0, 0(t1)
    li      t0, 0xDEADBEEF
    sw      t0, 4(t1)
.Lhalt:
    j       .Lhalt
