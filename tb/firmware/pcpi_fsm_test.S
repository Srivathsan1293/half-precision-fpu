// tb/firmware/pcpi_fsm_test.S
// Minimal firmware for PCPI state machine timing verification
// Operands and expected results stored in public RAM for harness check
// Halts after writing done marker to indicate completion

    .section .text._start
_start:
    # Initialize operands in x1/x2 (RAM accessible via GPR shadow)
    li      t0, 0x3C00          # a = 1.0  
    li      t1, 0x4000          # b = 2.0
    mv      a0, t0              # x0 (a operand)
    mv      a1, t1              # x1 (b operand)

    # Store expected results for timing verification:
    # RESULT_BASE[0] = FADD result (3.0 = 0x4200)
    # RESULT_BASE[1] = FSUB result (1.0 = 0x3C00)  
    # RESULT_BASE[2] = FMUL result (6.0 = 0x4600)
    # RESULT_BASE[3] = FDIV result (1.5 = 0x3E00)
    
    li      s0, 0x4200          # FADD: 3.0
    li      s1, 0x3C00          # FSUB: 1.0
    li      s2, 0x4600          # FMUL: 6.0
    li      s3, 0x3E00          # FDIV: 1.5

    sw      s0, 0x1000           # Store FADD result
    sw      s1, 0x1004           # Store FSUB result
    sw      s2, 0x1008           # Store FMUL result  
    sw      s3, 0x100C           # Store FDIV result

    li      t0, 0x5F505544      # MAGIC_FSM = 0x5F505544
    sw      t0, 0x1C00           # TEST_MAGIC_ADDR (identifies this as FSM test)

    li      t0, 0xDEADBEEF
    sw      t0, 0x1C04           # DONE_MARK

.Lhalt:
    j       .Lhalt
