// tb/firmware/fpu_emulator.c
//
// Software emulator for the standard Zhinx FP instructions that are NOT
// executed in hardware (the hardware cores cover FADD.H/FSUB.H/FMUL.H/FDIV.H
// via the PCPI wrapper). Called from the .irq_handler assembly stub at 0x800
// whenever PicoRV32 traps an unclaimed instruction to the ebreak IRQ.
//
// Contract with the assembly stub:
//   fpu_emulate(insn, regfile, resume_pc)
//     insn      : the faulting instruction word (RAM[P])
//     regfile   : pointer to a 32-word shadow of the GPR file captured at
//                 IRQ entry (regfile[i] == x_i); the stub has already stored
//                 the live values there, and writes regfile[rd] with the
//                 returned result before restoring.
//     resume_pc : P + 4 (used only to report unsupported instructions)
//   returns the 32-bit result to be written into GPR rd.
//
// Supported subset (emulator v1, RNE; RTZ/RDN/RUP/RMM also handled for the
// FCVT instructions, dynamic funct3=111 reads the frm shadow = RNE):
//   FCVT.H.W / FCVT.H.WU, FCVT.W.H / FCVT.WU.H, FMIN.H / FMAX.H,
//   FEQ.H / FLT.H / FLE.H, FSGNJ.H / FSGNJN.H / FSGNJX.H,
//   FMV.X.H / FMV.H.X, FCLASS.H, csrr/csrw of the fcsr/fflags/frm shadow.
//
// Zfh funct7 encodings (match clang -march=rv32im_zhinx output):
//   FADD.H 0x02 FSUB.H 0x06 FMUL.H 0x0A FDIV.H 0x0E FSQRT.H 0x2E
//   FSGNJ.H 0x12 FMIN/FMAX 0x16 FEQ/FLT/FLE 0x52
//   FCVT.H.W 0x6A FCVT.W.H 0x62 FMV.X.H/FCLASS 0x72 FMV.H.X 0x7A
//
// FSQRT.H and all FMAs are intentionally unsupported: the handler reports them
// and halts rather than fabricating a result.

#include <stdint.h>

// Marker written before halting on an unsupported instruction.
#define UNS_MARK 0x1C08  // instruction word
#define UNS_PC   0x1C0C  // resume PC of the faulting instruction

static void emu_halt(uint32_t insn, uint32_t resume_pc) {
    volatile uint32_t *m = (volatile uint32_t *)0x1C00;
    m[2] = insn;           // 0x1C08
    m[3] = resume_pc;      // 0x1C0C
    for (;;) { }
}

static int is_nan(uint32_t h) {
    return (h & 0x7C00) == 0x7C00 && (h & 0x03FF) != 0;
}

static uint32_t msb_pos(uint32_t u) {
    uint32_t p = 0;
    while (u > 1) { u >>= 1; p++; }
    return p;
}

// Rounding-mode field (funct3) of the FCVT instructions.
#define RM_RNE 0
#define RM_RTZ 1
#define RM_RDN 2
#define RM_RUP 3
#define RM_RMM 4
#define RM_DYN 7

// Common int32 -> half RNE path; "roundup" selects whether the magnitude is
// rounded up when the value is not exactly representable (RM_RNE/RM_RMM/RM_RDN
// for negatives / RM_RUP for positives). Returns the half bit pattern.
static uint32_t int_to_half(uint32_t mag, uint32_t sign, uint32_t mode) {
    if (mag == 0) return sign;
    uint32_t p = msb_pos(mag);
    if (p >= 16) return sign | 0x7C00;          // overflow -> +/-inf
    uint32_t exp = p + 15;
    uint32_t frac = mag - (1u << p);
    uint32_t mant;
    if (p <= 10) {
        mant = frac << (10 - p);                // exactly representable
    } else {
        uint32_t shift = p - 10;
        mant = frac >> shift;
        uint32_t dropped = frac & ((1u << shift) - 1);
        int round_up = 0;
        if (dropped) {
            if (mode == RM_RNE || mode == RM_DYN) {
                uint32_t rb = (dropped >> (shift - 1)) & 1;
                uint32_t sticky = dropped & ((1u << (shift - 1)) - 1);
                round_up = rb && (sticky || (mant & 1));
            } else if (mode == RM_RMM) {
                round_up = (dropped >= (1u << (shift - 1)));   // ties away
            } else if (mode == RM_RDN) {
                round_up = sign != 0;            // toward -inf
            } else if (mode == RM_RUP) {
                round_up = sign == 0;            // toward +inf
            }
        }
        if (round_up) {
            mant++;
            if (mant == 0x400) { mant = 0; exp++; }
        }
        if (exp >= 31) return sign | 0x7C00;    // rounded up to inf
    }
    return sign | (exp << 10) | mant;
}

// FCVT.H.W (signed int32 -> half) / FCVT.H.WU (unsigned -> half).
static uint32_t fcvt_h_w(int32_t x, uint32_t mode) {
    if (x == 0) return 0x0000;
    uint32_t sign = 0;
    uint32_t mag;
    if (x < 0) {
        sign = 0x8000;
        mag = (uint32_t)x;
        mag = ~mag + 1;                 // two's complement (wraps for INT_MIN)
    } else {
        mag = (uint32_t)x;
    }
    return int_to_half(mag, sign, mode);
}

static uint32_t fcvt_h_wu(uint32_t x, uint32_t mode) {
    return int_to_half(x, 0, mode);
}

// half -> signed int32, rounding per funct3 (truncate = RTZ; saturate inf/NaN).
static uint32_t fcvt_w_h(uint32_t h, uint32_t mode) {
    uint32_t sign = h & 0x8000;
    uint32_t exp = (h >> 10) & 31;
    uint32_t mant = h & 0x03FF;
    if (exp == 31) {
        if (mant == 0) return sign ? 0x80000000u : 0x7FFFFFFFu;  // +/-inf
        return 0x7FFFFFFFu;               // NaN -> invalid
    }
    if (exp == 0) return 0;               // zero / subnormal (< 1)
    int32_t e = (int32_t)exp - 15;        // biased exponent
    if (e >= 10) return sign ? (0u - ((0x400u | mant) << (e - 10)))
                             : ((0x400u | mant) << (e - 10));   // integer, exact
    // e < 10: integer part + fraction to round
    uint32_t shift = 10 - e;
    uint32_t mag = (0x400u | mant) >> shift;
    uint32_t rem = (0x400u | mant) & ((1u << shift) - 1);
    int round_up = 0;
    if (rem) {
        if (mode == RM_RNE || mode == RM_DYN) {
            uint32_t den = (1u << (shift - 1));
            if (rem > den) round_up = 1;
            else if (rem == den) round_up = mag & 1;    // tie to even
        } else if (mode == RM_RMM) {
            round_up = rem >= (1u << (shift - 1));
        } else if (mode == RM_RDN) {
            round_up = sign != 0;         // toward -inf: negative rounds down
        } else if (mode == RM_RUP) {
            round_up = sign == 0;         // toward +inf: positive rounds up
        }
    }
    if (round_up) mag++;
    return sign ? (0u - mag) : mag;
}

// half -> unsigned int32 (negative -> 0, inf/NaN -> 0xFFFFFFFF).
static uint32_t fcvt_wu_h(uint32_t h, uint32_t mode) {
    uint32_t sign = h & 0x8000;
    uint32_t exp = (h >> 10) & 31;
    uint32_t mant = h & 0x03FF;
    if (sign) return 0;
    if (exp == 31) return 0xFFFFFFFFu;    // +inf / NaN
    if (exp == 0) return 0;
    int32_t e = (int32_t)exp - 15;        // biased exponent
    if (e >= 10) {
        uint32_t v = (0x400u | mant) << (e - 10);
        if (v < (0x400u | mant)) return 0xFFFFFFFFu;   // overflowed
        return v;
    }
    uint32_t shift = 10 - e;
    uint32_t mag = (0x400u | mant) >> shift;
    uint32_t rem = (0x400u | mant) & ((1u << shift) - 1);
    int round_up = 0;
    if (rem) {
        if (mode == RM_RNE || mode == RM_DYN) {
            uint32_t den = (1u << (shift - 1));
            if (rem > den) round_up = 1;
            else if (rem == den) round_up = mag & 1;
        } else if (mode == RM_RMM) {
            round_up = rem >= (1u << (shift - 1));
        } else if (mode == RM_RUP) {
            round_up = 1;
        }
    }
    if (round_up) mag++;
    return mag;
}

// signed-magnitude half compare: (a < b), both operands known not NaN.
static int h_less(uint32_t a, uint32_t b) {
    uint32_t ma = a & 0x7FFF;
    uint32_t mb = b & 0x7FFF;
    uint32_t sa = a & 0x8000;
    if (sa != (b & 0x8000)) {
        if (sa) return ma != 0;   // a negative: less unless a == -0 (equal to +0)
        return 0;                 // a positive, b negative: not less
    }
    if (sa) return mb < ma;       // both negative: larger magnitude is smaller
    return ma < mb;               // both positive
}

static uint32_t fmin_h(uint32_t a, uint32_t b) {
    if (is_nan(a) && is_nan(b)) return a | 0x0200;
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if ((a & 0x7FFF) == 0 && (b & 0x7FFF) == 0) return 0x8000;  // both zero -> -0
    if (h_less(a, b)) return a;
    if (h_less(b, a)) return b;
    return a;
}

static uint32_t fmax_h(uint32_t a, uint32_t b) {
    if (is_nan(a) && is_nan(b)) return a | 0x0200;
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if ((a & 0x7FFF) == 0 && (b & 0x7FFF) == 0) return 0x0000;  // both zero -> +0
    if (h_less(a, b)) return b;
    if (h_less(b, a)) return a;
    return a;
}

static uint32_t fcmp(uint32_t funct3, uint32_t a, uint32_t b) {
    if (is_nan(a) || is_nan(b)) return 0;
    if (funct3 == 2) {                 // FEQ.H
        if (a == b) return 1;
        if ((a & 0x7FFF) == 0 && (b & 0x7FFF) == 0) return 1;  // -0 == +0
        return 0;
    }
    if (funct3 == 1)                   // FLT.H
        return h_less(a, b) ? 1 : 0;
    return h_less(b, a) ? 0 : 1;       // FLE.H : a <= b <=> !(b < a)
}

static uint32_t fsgnj(uint32_t funct3, uint32_t a, uint32_t b) {
    if (funct3 == 0) return (b & 0x8000) | (a & 0x7FFF);   // FSGNJ
    if (funct3 == 1) return (~b & 0x8000) | (a & 0x7FFF);  // FSGNJN
    return ((a ^ b) & 0x8000) | (a & 0x7FFF);              // FSGNJX
}

static uint32_t fclass_h(uint32_t h) {
    uint32_t sign = h & 0x8000;
    uint32_t exp = (h >> 10) & 31;
    uint32_t mant = h & 0x03FF;
    if (exp == 31) {
        if (mant == 0) return sign ? 0x001 : 0x080;        // -inf : +inf
        if (mant & 0x200) return 0x200;                    // qNaN
        return 0x100;                                      // sNaN
    }
    if (exp == 0) {
        if (mant == 0) return sign ? 0x008 : 0x010;        // -0 : +0
        return sign ? 0x004 : 0x020;                       // -sub : +sub
    }
    return sign ? 0x002 : 0x040;                           // -norm : +norm
}

// fcsr/fflags/frm shadow: reads return the RNE value (fcsr=0, frm=0, fflags=0);
// writes are accepted (the emulator never changes the rounding mode).
static uint32_t csr_emulate(uint32_t insn) {
    uint32_t funct3 = (insn >> 12) & 7;
    if (funct3 == 0) emu_halt(insn, 0);   // reserved encoding
    return 0;                             // ignore the write side
}

uint32_t fpu_emulate(uint32_t insn, uint32_t *regfile, uint32_t resume_pc) {
    uint32_t opcode = insn & 0x7F;
    if (opcode == 0x73) return csr_emulate(insn);          // system / CSR
    if (opcode != 0x53) emu_halt(insn, resume_pc);         // not an FP op

    uint32_t funct3 = (insn >> 12) & 7;
    uint32_t funct7 = insn >> 25;
    uint32_t rs1 = (insn >> 15) & 31;
    uint32_t rs2 = (insn >> 20) & 31;
    uint32_t a = regfile[rs1];
    uint32_t b = regfile[rs2];

    switch (funct7) {
    case 0x12: return fsgnj(funct3, a, b);                 // FSGNJ(.H)
    case 0x16:                                             // FMIN.H / FMAX.H
        return (funct3 == 0) ? fmin_h(a, b) : fmax_h(a, b);
    case 0x6A:                                             // FCVT.H.W / FCVT.H.WU
        return (rs2 == 0) ? fcvt_h_w((int32_t)a, funct3) : fcvt_h_wu(a, funct3);
    case 0x52: return fcmp(funct3, a, b);                  // FEQ/FLT/FLE
    case 0x62:                                             // FCVT.W.H / FCVT.WU.H
        return (rs2 == 0) ? fcvt_w_h(a, funct3) : fcvt_wu_h(a, funct3);
    case 0x72:                                             // FMV.X.H / FCLASS.H
        return (funct3 == 0) ? (uint32_t)(int32_t)(int16_t)a : fclass_h(a);
    case 0x7A: return a & 0xFFFF;                          // FMV.H.X
    default: emu_halt(insn, resume_pc);                    // FSQRT/FMA/unknown
    }
    return 0;                                              // unreachable
}
