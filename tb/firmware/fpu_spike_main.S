// tb/firmware/fpu_spike_main.S
//
// Phase 0 spike: validate the PicoRV32 ebreak-IRQ premise used by the Zhinx
// emulator, namely that after an unclaimed (PCPI-timeout) instruction at PC P
// the IRQ handler receives the resume address P+4 in q-register 0.
//
// Memory protocol (spike only):
//   0x1C00  magic        0x5F50535F
//   0x1C04  done         0xDEADBEEF
//   0x1C10  expected resume PC (computed by main = &fault + 4)
//   0x1C14  resume PC actually stored by the IRQ handler
//   0x1C18  compare result (0 = PASS, 1 = FAIL)
//
// The handler is linked at 0x800 (PROGADDR_IRQ). It reads q-register 0 with
// getq, stores it to 0x1C14, and returns with retirq.
//
// Encodings used (picorv32 custom0 / standard encodings):
//   getq  t0, q0  = 0x0010028B
//   retirq t0     = 0x0405000B
//   maskirq x0, 0 = 0x0600000B  (unmask all IRQs; reset sets irq_mask = ~0)
//   FADD.H x1,x2,x3 (faulting instruction, no wrapper in this build)
//                = 0x043100D3

    .section .text._start
    .globl  _start
_start:
    // Unmask IRQs: picorv32 resets irq_mask to all-ones, which would mask the
    // ebreak IRQ and force a plain trap instead.
    .word   0x0600000B          // maskirq x0, 0

    // expected = &fault + 4
    la      t0, fault
    addi    t0, t0, 4
    li      t1, 0x1C10
    sw      t0, 0(t1)

    // Trigger an unclaimed FP instruction: with no PCPI wrapper connected,
    // pcpi_valid stays asserted until pcpi_timeout -> ebreak IRQ -> handler.
fault:
    .word   0x043100D3          // FADD.H x1, x2, x3 (never claimed here)

    // After the handler returns: compare handler-stored resume PC vs expected.
    lw      t2, 0(t1)           // expected
    lw      t3, 4(t1)           // resume_pc from handler
    li      t5, 1
    bne     t2, t3, .Lfail
    li      t5, 0
.Lfail:
    sw      t5, 8(t1)           // compare result @0x1C18

    // magic + done
    li      t0, 0x5F50535F
    li      t1, 0x1C00
    sw      t0, 0(t1)
    li      t0, 0xDEADBEEF
    sw      t0, 4(t1)
.Lhalt:
    j       .Lhalt

    .section .irq_handler,"ax",@progbits
    .globl  irq_handler
irq_handler:
    .word   0x0010028B          // getq t0, q0   -> t0 = resume PC (P+4)
    li      t1, 0x1C14
    sw      t0, 0(t1)
    .word   0x0405000B          // retirq t0     -> branch to resume PC
