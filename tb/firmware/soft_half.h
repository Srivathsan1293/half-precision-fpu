/* tb/firmware/soft_half.h
 *
 * Pure-integer software implementation of binary16 (IEEE-754 half) add/sub/mul
 * for the RV32IM PicoRV32 core (no FPU, no libgcc), built to match what a
 * generic embedded soft-float library does: a single hardware MUL for the 11x11
 * significand product and CLZ-driven single-shot normalization (no bit-by-bit
 * loops). This is the "CPU software-emulated floating-point math" baseline for
 * the Week 2 SW-vs-HW cycle-count benchmark: the same kernel is run once
 * through these integer routines and once through the hardware FPU PCPI
 * custom instructions, and the harness measures the cycle difference.
 *
 * Only RNE rounding is implemented (matches the hardware). The FIR workload
 * data stays in the normal range so subnormals are not exercised, but zero/
 * inf/NaN and subnormal packing are handled defensively.
 *
 * soft_fdiv uses restore long division over the 11-bit significands; software
 * division is inherently ~10x costlier than mul/add and is where the hardware
 * FDIV shows its real, honest advantage.
 */

#ifndef SOFT_HALF_H
#define SOFT_HALF_H

#include <stdint.h>

static inline uint32_t h_exp(uint16_t h) { return (h >> 10) & 31; }
static inline uint32_t h_man(uint16_t h) { return h & 0x3FF; }
static inline int h_is_nan(uint16_t h) { return h_exp(h) == 31 && h_man(h) != 0; }
static inline int h_is_inf(uint16_t h) { return h_exp(h) == 31 && h_man(h) == 0; }
static inline int h_is_zero(uint16_t h) { return h_exp(h) == 0 && h_man(h) == 0; }

/* shift a 14-bit field right with sticky bit collection (n bits to drop) */
static inline uint32_t shr_sticky(uint32_t u, uint32_t n) {
    if (n >= 14) return (u != 0);
    uint32_t s = u >> n;
    if (u & ((1u << n) - 1)) s |= 1;
    return s;
}

/* pack (sign BIT 0/1, UNBIASED exponent, 10-bit mantissa) into an fp16 pattern.
 * Subnormals: biased exp field 0 holds m directly (value = m * 2^-24).
 * Keep sign on the flush-to-zero path so the FIR results stay symmetric. */
static inline uint16_t pack_h(uint32_t sign, int32_t e, uint32_t m) {
    uint32_t sb = sign ? 0x8000u : 0u;
    int32_t be = e + 15;                                      /* bias */
    if (be >= 31) return (uint16_t)(sb | 0x7C00);             /* +/- inf */
    if (be <= 0) {                                            /* subnormal */
        if (be < 0) return (uint16_t)sb;                      /* flush to zero */
        return (uint16_t)(sb | (m & 0x3FF));                  /* exp field 0 */
    }
    return (uint16_t)(sb | ((uint32_t)be << 10) | (m & 0x3FF));
}

/* count leading zeros (RV32I-only shifts); replaces the bit-by-bit normalize
 * loop with a single-shot shift, as a real soft-float library would do. */
static inline uint32_t h_clz32(uint32_t v) {
    uint32_t n = 0;
    if (!(v >> 16)) { n += 16; v <<= 16; }
    if (!(v >> 24)) { n += 8;  v <<= 8; }
    if (!(v >> 28)) { n += 4;  v <<= 4; }
    if (!(v >> 30)) { n += 2;  v <<= 2; }
    if (!(v >> 31)) { n += 1; }
    return n;
}

static inline uint16_t soft_fadd(uint16_t a, uint16_t b) {
    uint32_t sa = a >> 15, sb = b >> 15;
    int32_t ea = (int32_t)h_exp(a), eb = (int32_t)h_exp(b);

    if (h_is_nan(a) || h_is_nan(b)) return 0x7E00;
    if (h_is_inf(a) && h_is_inf(b)) return (sa == sb) ? a : 0x7E00;
    if (h_is_inf(a)) return a;
    if (h_is_inf(b)) return b;
    if (h_is_zero(a) && h_is_zero(b)) return (sa && sb) ? 0x8000 : 0x0000;
    if (h_is_zero(a)) return b;
    if (h_is_zero(b)) return a;

    /* 11-bit significands (implicit bit) with unbiased exponents */
    uint32_t ma = ea ? (h_man(a) | 0x400) : h_man(a);
    uint32_t mb = eb ? (h_man(b) | 0x400) : h_man(b);
    int32_t ua = ea ? (ea - 15) : -14;
    int32_t ub = eb ? (eb - 15) : -14;

    /* widen to 14-bit fields (implicit bit at bit 13, 3 guard bits low) */
    uint32_t A = ma << 3, B = mb << 3;
    int32_t ef = (ua > ub) ? ua : ub;
    if (ua >= ub) B = shr_sticky(B, (uint32_t)(ua - ub));
    else          A = shr_sticky(A, (uint32_t)(ub - ua));

    uint32_t sign, v;
    if (sa == sb) {
        sign = sa;
        v = A + B;
        if (v & 0x4000) { v = (v >> 1) | (v & 1); ef++; }  /* carry out bit 14 */
    } else {
        if (A >= B) { v = A - B; sign = sa; }
        else        { v = B - A; sign = sb; }
        /* normalize: shift left until implicit bit (bit 13) is set */
        if (v) {
            uint32_t sh = 13u - (31u - h_clz32(v));
            if (sh) { v <<= sh; ef -= (int32_t)sh; }
        }
    }
    if (v == 0) return (uint16_t)(sign ? 0x8000 : 0x0000);

    /* RNE: 10-bit mantissa at bits 12..3, guard=bit2, round=bit1, sticky=bit0 */
    uint32_t m = (v >> 3) & 0x3FF;
    uint32_t g = (v >> 2) & 1, r = (v >> 1) & 1, s = v & 1;
    if (g && (r || s || (m & 1))) {
        m++;
        if (m == 0x400) { m = 0; ef++; }
    }
    return pack_h(sign, ef, m);
}

static inline uint16_t soft_fsub(uint16_t a, uint16_t b) {
    return soft_fadd(a, b ^ 0x8000);
}

static inline uint16_t soft_fmul(uint16_t a, uint16_t b) {
    uint32_t sa = a >> 15, sb = b >> 15;
    int32_t ea = (int32_t)h_exp(a), eb = (int32_t)h_exp(b);

    if (h_is_nan(a) || h_is_nan(b)) return 0x7E00;
    /* 0 * inf is NaN; inf * inf is inf (sign = xor); inf * finite is inf */
    if (h_is_inf(a) && (h_is_zero(b) || h_is_nan(b))) return 0x7E00;
    if (h_is_inf(b) && (h_is_zero(a) || h_is_nan(a))) return 0x7E00;
    if (h_is_inf(a) || h_is_inf(b)) return (uint16_t)((sa ^ sb) << 15 | 0x7C00);
    if (h_is_zero(a) || h_is_zero(b)) return (uint16_t)((sa ^ sb) << 15);

    uint32_t ma = ea ? (h_man(a) | 0x400) : h_man(a);
    uint32_t mb = eb ? (h_man(b) | 0x400) : h_man(b);
    int32_t ua = ea ? (ea - 15) : -14;
    int32_t ub = eb ? (eb - 15) : -14;

    /* 11x11 product in one hardware MUL, then a single-shot CLZ shift to put
     * the implicit bit at bit 20 (as a generic soft-float library does). */
    uint32_t p = ma * mb;
    int32_t lead = 20 - (int32_t)(31u - h_clz32(p));
    int32_t ef = ua + ub - lead;
    if (lead < 0) { p = (p >> -lead) | ((p & ((1u << -lead) - 1)) ? 1 : 0); }
    else          p <<= lead;
    /* p in [0x100000,0x200000): 10-bit mantissa at 19..10, guard=9, round=8, sticky bits 7..0 */
    uint32_t m = (p >> 10) & 0x3FF;
    uint32_t g = (p >> 9) & 1, r = (p >> 8) & 1, s = p & 0xFF;
    if (g && (r || s || (m & 1))) {
        m++;
        if (m == 0x400) { m = 0; ef++; }
    }
    return pack_h(sa ^ sb, ef, m);
}

static inline uint16_t soft_fdiv(uint16_t a, uint16_t b) {
    uint32_t sa = a >> 15, sb = b >> 15;
    int32_t ea = (int32_t)h_exp(a), eb = (int32_t)h_exp(b);

    if (h_is_nan(a) || h_is_nan(b)) return 0x7E00;
    if (h_is_inf(a) && h_is_inf(b)) return 0x7E00;
    if (h_is_zero(b)) return (uint16_t)((sa ^ sb) << 15 | 0x7C00);  /* x/0 = inf */
    if (h_is_inf(b))  return (uint16_t)((sa ^ sb) << 15);           /* x/inf = +-0 */
    if (h_is_zero(a)) return (uint16_t)((sa ^ sb) << 15);           /* 0/x = +-0 */

    uint32_t ma = ea ? (h_man(a) | 0x400) : h_man(a);
    uint32_t mb = eb ? (h_man(b) | 0x400) : h_man(b);
    int32_t ua = ea ? (ea - 15) : -14;
    int32_t ub = eb ? (eb - 15) : -14;

    /* align the significands so ma/mb lands in [1,2): shift the dividend left
     * when the quotient would be < 1, shift the divisor left when it would be
     * >= 2 (handles subnormal operands where the raw ratio can exceed 2). */
    uint32_t m = ma, d = mb;
    int32_t ef = ua - ub;
    while (m < d) { m <<= 1; ef--; }
    while (m >= (d << 1)) { d <<= 1; ef++; }

    /* restore long division: q = floor(m*2^12 / d) in [0x1000, 0x2000). */
    m <<= 12;
    uint32_t q = 0;
    int32_t i;
    for (i = 12; i >= 0; i--) {
        if (m >= (d << i)) {
            m -= d << i;
            q |= (1u << i);
        }
    }
    /* bit 12 integer; bits 11..2 mantissa, bit1 guard, bit0 round, rem sticky */
    uint32_t mant = (q >> 2) & 0x3FF;
    uint32_t g = (q >> 1) & 1, r = q & 1, s = (m != 0);
    if (g && (r || s || (mant & 1))) {
        mant++;
        if (mant == 0x400) { mant = 0; ef++; }
    }
    return pack_h(sa ^ sb, ef, mant);
}

#endif /* SOFT_HALF_H */
