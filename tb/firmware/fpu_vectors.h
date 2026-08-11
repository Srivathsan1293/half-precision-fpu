/* tb/firmware/fpu_vectors.h
 *
 * Shared half-precision FPU test vectors + CPU-stress parameters.
 * Included by BOTH the stress firmware (fpu_stress_main.c) and the Verilator
 * harness (tb/tb_fpu_pcpi.cpp) so the operand sets can never drift apart.
 *
 * op codes match fpu_macros.h: 0=FADD 1=FSUB 2=FMUL 3=FDIV.
 */
#ifndef FPU_VECTORS_H
#define FPU_VECTORS_H

#include <stdint.h>

typedef struct {
    unsigned char op;
    uint16_t a;
    uint16_t b;
} fpu_vec_t;

static const fpu_vec_t fpu_sweep_vectors[] = {
    /* --- basic normals --- */
    {0, 0x3C00, 0x4000},   /*  1.0 + 2.0  =  3.0     */
    {0, 0x3C00, 0x3800},   /*  1.0 + 0.5  =  1.5     */
    {0, 0x4000, 0xC000},   /*  2.0 + -2.0 =  0.0     */
    {0, 0xC000, 0x3C00},   /* -2.0 + 1.0  = -1.0     */
    {1, 0x4000, 0x3C00},   /*  2.0 - 1.0  =  1.0     */
    {1, 0x3C00, 0x4000},   /*  1.0 - 2.0  = -1.0     */
    {1, 0x3C00, 0x3C00},   /*  1.0 - 1.0  =  0.0     */
    {1, 0x3800, 0x3800},   /*  0.5 - 0.5  =  0.0     */
    {2, 0x4000, 0x4200},   /*  2.0 * 3.0  =  6.0     */
    {2, 0xC000, 0x4200},   /* -2.0 * 3.0  = -6.0     */
    {2, 0x4200, 0xC000},   /*  3.0 * -2.0 = -6.0     */
    {2, 0xBC00, 0x4000},   /* -1.0 * 2.0  = -2.0     */
    {3, 0x3C00, 0x4000},   /*  1.0 / 2.0  =  0.5     */
    {3, 0x4200, 0x4000},   /*  3.0 / 2.0  =  1.5     */
    {3, 0xC000, 0x4000},   /* -2.0 / 2.0  = -1.0     */
    {3, 0x4000, 0x4200},   /*  2.0 / 3.0  =  0.6666.. */

    /* --- denormals / exponent extremes --- */
    {0, 0x0001, 0x0001},   /* min sub + min sub = 2*min sub */
    {0, 0x03FF, 0x03FF},   /* max sub + max sub           */
    {0, 0x0400, 0x03FF},   /* min normal + max sub         */
    {1, 0x0001, 0x0002},   /* sub - sub (negative sub)     */
    {2, 0x0002, 0x4000},   /* sub * 2.0                    */
    {2, 0x0001, 0x0001},   /* sub * sub -> 0 (underflow)   */
    {3, 0x03FF, 0x03FF},   /* max sub / max sub = 1.0      */
    {3, 0x0001, 0x0001},   /* min sub / min sub = 1.0      */
    {3, 0x7BFF, 0x03FF},   /* max normal / max sub -> big   */
    {3, 0x3C00, 0x7BFF},   /* 1.0 / max normal -> tiny      */
    {0, 0x7BFF, 0x7BFF},   /* max normal + max normal -> inf*/
    {0, 0x7BFF, 0x0001},   /* max normal + min sub (rounds) */
    {2, 0x7BFF, 0x7BFF},   /* max normal * max normal -> inf*/
    {2, 0x7BFF, 0x3800},   /* max normal * 0.5              */
    {3, 0x7BFF, 0x0001},   /* max normal / min sub -> inf   */
    {3, 0x0400, 0x7BFF},   /* min normal / max normal -> 0  */

    /* --- specials: inf, zero, NaN --- */
    {0, 0x7C00, 0x3C00},   /*  inf + 1.0   =  inf   */
    {0, 0x7C00, 0x7C00},   /*  inf + inf   =  inf   */
    {0, 0x7C00, 0xFC00},   /*  inf + -inf  =  NaN   */
    {0, 0x3C00, 0xFC00},   /*  1.0 + -inf  = -inf   */
    {1, 0x7C00, 0x7C00},   /*  inf - inf   =  NaN   */
    {1, 0xFC00, 0x7C00},   /* -inf - inf   = -inf   */
    {2, 0x0000, 0x7C00},   /*  0.0 * inf   =  NaN   */
    {2, 0x7C00, 0x4000},   /*  inf * 2.0   =  inf   */
    {2, 0xFC00, 0x4000},   /* -inf * 2.0   = -inf   */
    {3, 0x3C00, 0x0000},   /*  1.0 / 0.0   =  inf   */
    {3, 0x0000, 0x3C00},   /*  0.0 / 1.0   =  0.0   */
    {3, 0x0000, 0x0000},   /*  0.0 / 0.0   =  NaN   */
    {3, 0x7C00, 0x4000},   /*  inf / 2.0   =  inf   */
    {3, 0x3C00, 0x7C00},   /*  1.0 / inf   =  0.0   */
    {3, 0x7C00, 0x7C00},   /*  inf / inf   =  NaN   */
    {0, 0x7E00, 0x3C00},   /*  NaN + 1.0   =  NaN   */
    {2, 0x7E00, 0x3C00},   /*  NaN * 1.0   =  NaN   */
    {3, 0x3C00, 0x7E00},   /*  1.0 / NaN   =  NaN   */

    /* --- signed-zero sign preservation (IEEE 754-2019 6.3) --- */
    {2, 0xBC00, 0x0000},   /* -1.0 * +0.0 = -0.0    */
    {2, 0x3C00, 0x8000},   /* +1.0 * -0.0 = -0.0    */
    {1, 0x0000, 0x0000},   /* +0.0 - +0.0 = +0.0    */
    {1, 0x8000, 0x0000},   /* -0.0 - +0.0 = -0.0    */
    {0, 0x8000, 0x8000},   /* -0.0 + -0.0 = -0.0    */
    {0, 0x0000, 0x8000},   /* +0.0 + -0.0 = +0.0    */

    /* --- RNE half-way (exact tie) rounding --- */
    {0, 0x3C00, 0x3C01},   /*  1.0 + (1+2^-10) = 2 + 0.5ulp tie -> 2.0    */
    {0, 0x4000, 0x4001},   /*  2.0 + (2+2^-9)  = 4 + 0.5ulp tie -> 4.0    */
    {2, 0x3C02, 0x3D00},   /* (1+2^-9)*(1+2^-8) = 1+258.5/1024 tie -> even */
};

static const unsigned fpu_sweep_count = sizeof(fpu_sweep_vectors) / sizeof(fpu_sweep_vectors[0]);

/* --- CPU-stress section parameters --- */

/* B: back-to-back FADD, STRESS_B2B_COUNT consecutive custom insns. */
#define STRESS_B2B_COUNT  8u
#define STRESS_B2B_A      0x4000u   /* 2.0 */
#define STRESS_B2B_B      0x4200u   /* 3.0 */

/* C: accumulator with rd == rs1 == x1 (x1 += STRESS_ACC1_B each step). */
#define STRESS_ACC1_COUNT 8u
#define STRESS_ACC1_INIT  0x3C00u   /* 1.0 */
#define STRESS_ACC1_B     0x3800u   /* 0.5 */

/* D: accumulator with rd == x1, rs2 == x1 (x1 += STRESS_ACC2_A each step). */
#define STRESS_ACC2_COUNT 8u
#define STRESS_ACC2_INIT  0x3C00u   /* 1.0 */
#define STRESS_ACC2_A     0x4000u   /* 2.0 */

/* E: loop over STRESS_LOOP_ITERS iterations x 4 ops on the first vectors. */
#define STRESS_LOOP_OPS   4u
#define STRESS_LOOP_ITERS 4u

/* --- Zhinx (standard-encoding) vector table --- */
/* op codes (match tb_fpu_pcpi.cpp check_zhinx and fpu_zhinx_main.c):
 *   0..3   FADD/FSUB/FMUL/FDIV.H   (hardware, via the PCPI wrapper)
 *   4,5    FMIN/FMAX.H             (emulated)
 *   6,7,8  FEQ/FLT/FLE.H           (emulated)
 *   9,10   FCVT.H.W / FCVT.H.WU    (emulated)
 *   11,12  FCVT.W.H / FCVT.WU.H    (emulated)
 *   13,14  FSGNJ.H / FSGNJN.H      (emulated)
 *   15,16  FABS.H(FSGNJX) / FCLASS.H (emulated)
 *
 * Operands are full 32-bit words: the low 16 bits hold the binary16 value for
 * the FP ops, while op 9/10 use the full int32 (op 10 uses the full uint32).
 * The inline-asm driver in fpu_zhinx_main.c moves these verbatim into rs1/rs2,
 * so only the low 16 bits ever reach the hardware/emulator FP logic.
 */
typedef struct {
    unsigned char op;
    uint32_t a;
    uint32_t b;
} zhinx_vec_t;

static const zhinx_vec_t zhinx_vectors[] = {
    /* --- hardware path: FADD/FSUB/FMUL/FDIV.H --- */
    {0, 0x3C00, 0x4000},   {0, 0x3800, 0x3800},   {0, 0x3C00, 0x7C00},
    {0, 0x7E00, 0x3C00},   {1, 0x4000, 0x3C00},   {1, 0x3C00, 0x4000},
    {1, 0x7C00, 0x7C00},   {2, 0x4000, 0x4200},   {2, 0xC000, 0x4200},
    {2, 0x0000, 0x7C00},   {2, 0x0001, 0x0001},   {3, 0x3C00, 0x4000},
    {3, 0x4200, 0x4000},   {3, 0x3C00, 0x0000},   {3, 0x0000, 0x0000},

    /* --- signed-zero sign preservation --- */
    {2, 0xBC00, 0x0000},   {2, 0x3C00, 0x8000},   {1, 0x0000, 0x0000},
    {1, 0x8000, 0x0000},   {0, 0x8000, 0x8000},   {0, 0x0000, 0x8000},

    /* --- RNE half-way (exact tie) rounding --- */
    {0, 0x3C00, 0x3C01},   {0, 0x4000, 0x4001},   {2, 0x3C02, 0x3D00},

    /* --- emulated: FMIN/FMAX.H --- */
    {4, 0x3C00, 0x4000},   {4, 0xC000, 0x3C00},   {4, 0x3800, 0x3400},
    {4, 0x8000, 0x0000},   {4, 0x7E00, 0x3C00},   {4, 0x4000, 0x7E00},
    {5, 0x3C00, 0x4000},   {5, 0xC000, 0x3C00},   {5, 0x8000, 0x0000},
    {5, 0x7E00, 0x3C00},

    /* --- emulated: FEQ/FLT/FLE.H --- */
    {6, 0x3C00, 0x4000},   {6, 0x3C00, 0x3C00},   {6, 0x8000, 0x0000},
    {6, 0x7E00, 0x3C00},   {7, 0x3C00, 0x4000},   {7, 0x4000, 0x3C00},
    {7, 0xC000, 0x3C00},   {7, 0x7E00, 0x3C00},   {8, 0x3C00, 0x3C00},
    {8, 0x4000, 0x3C00},   {8, 0x3800, 0x3800},

    /* --- emulated: FCVT.H.W / FCVT.H.WU (full 32-bit operands) --- */
    {9, 1, 0},             {9, 2, 0},             {9, 255, 0},
    {9, 2049, 0},          {9, 3073, 0},          {9, 0xFFFFFFFD, 0},
    {9, 0xFFFFF7FF, 0},    {9, 0xFFFF8000, 0},
    {10, 3, 0},            {10, 2049, 0},         {10, 0xFFFF, 0},
    {10, 0, 0},

    /* --- emulated: FCVT.W.H / FCVT.WU.H --- */
    {11, 0x3E00, 0},       {11, 0x4600, 0},       {11, 0xC100, 0},
    {11, 0x5600, 0},       {11, 0x3800, 0},       {11, 0xB800, 0},
    {12, 0x3E00, 0},       {12, 0x5600, 0},       {12, 0x3800, 0},

    /* --- emulated: FSGNJ/FSGNJN/FABS(FSGNJX)/FCLASS.H --- */
    {13, 0x4000, 0xBC00},  {13, 0xC000, 0x3C00},  {13, 0x7E00, 0xBC00},
    {14, 0x4000, 0x3C00},  {14, 0xC000, 0x3C00},  {15, 0xC000, 0x3C00},
    {15, 0x4000, 0x3C00},  {16, 0x3C00, 0},       {16, 0xC000, 0},
    {16, 0x0001, 0},       {16, 0x0000, 0},       {16, 0x8000, 0},
    {16, 0x7C00, 0},       {16, 0xFC00, 0},       {16, 0x7E00, 0},
    {16, 0x7D00, 0},       {16, 0x7C01, 0},       {16, 0x7F01, 0},
};

static const unsigned zhinx_count = sizeof(zhinx_vectors) / sizeof(zhinx_vectors[0]);

/* --- Edge-case / trap-hunt vectors (Stage E of run_exhaustive_tests.sh) ---
 *
 * Same zhinx_vec_t layout and op codes as zhinx_vectors, but aimed at
 * trap-prone and boundary inputs that the basic zhinx sweep does not cover:
 *   - sNaN / qNaN with varying payloads (FMIN/FMAX must quiet NaN, FEQ/FLT/
 *     FLE must return 0, FCLASS must distinguish 0x100 vs 0x200)
 *   - +-Inf, +-0, signed-zero equality (FEQ -0 == +0)
 *   - min/max subnormal and min/max normal exponent extremes
 *   - FCVT.H.W/WU at INT_MIN / INT_MAX / UINT32_MAX / negative boundaries
 *   - FCVT.W.H / FCVT.WU.H saturation on Inf/NaN and RNE tie cases
 *   - FSGNJ sign-corner cases with NaN operand
 *   - FCLASS every category bit position
 * Executed by tb/firmware/fpu_edge_main.c (magic 0x5F505545) and checked by
 * check_edge() in tb/tb_fpu_pcpi.cpp using the same per-op golden model.
 */
static const zhinx_vec_t edge_vectors[] = {
    /* --- hardware ops 0..3: NaN/Inf/zero/subnormal corners --- */
    {0, 0x7E00, 0x3C00},   {0, 0x7C00, 0xFC00},   {0, 0x03FF, 0x0001},
    {0, 0x7BFF, 0x7BFF},   {0, 0x8000, 0x0000},   {0, 0x7D00, 0x3C00},
    {1, 0x7E00, 0x3C00},   {1, 0x7C00, 0x7C00},   {1, 0x0001, 0x0002},
    {1, 0x8000, 0x0000},   {2, 0x7E00, 0x0000},   {2, 0x0000, 0x7C00},
    {2, 0x0001, 0x0001},   {2, 0x7BFF, 0x4000},   {2, 0xFC00, 0x3C00},
    {3, 0x7E00, 0x3C00},   {3, 0x0000, 0x0000},   {3, 0x7C00, 0x7C00},
    {3, 0x0001, 0x0001},   {3, 0x7BFF, 0x0001},   {3, 0x0400, 0x7BFF},
    {3, 0x3C00, 0x0000},   {3, 0x0000, 0x3C00},

    /* --- FMIN/FMAX: quiet-NaN propagation + signed-zero rules --- */
    {4, 0x7E00, 0x7E00},   {4, 0x7D00, 0x7E00},   {4, 0x7E00, 0x0000},
    {4, 0x8000, 0x0000},   {4, 0xBC00, 0x3C00},   {4, 0x7C00, 0xFC00},
    {5, 0x7E00, 0x7E00},   {5, 0x7E00, 0x0000},   {5, 0x8000, 0x0000},
    {5, 0x3C00, 0xBC00},   {5, 0xFC00, 0x7C00},   {5, 0x7C01, 0x3C00},

    /* --- FEQ/FLT/FLE: NaN -> 0, -0 == +0, boundary compares --- */
    {6, 0x7E00, 0x7E00},   {6, 0x8000, 0x0000},   {6, 0x7BFF, 0x7BFF},
    {6, 0x3C00, 0x3C00},   {7, 0x7E00, 0x3C00},   {7, 0x0001, 0x0002},
    {7, 0x7BFF, 0x0400},   {7, 0x8000, 0x0000},   {8, 0x7E00, 0x3C00},
    {8, 0x3C00, 0x3C00},   {8, 0xFC00, 0x7C00},

    /* --- FCVT.H.W / FCVT.H.WU: int32 boundaries + RNE ties --- */
    {9, 0x7FFFFFFFu, 0},   {9, 0x80000000u, 0},   {9, 0xFFFFFFFFu, 0},
    {9, 0x0000FFFFu, 0},   {9, 2049u, 0},         {9, 2050u, 0},
    {9, 3073u, 0},         {9, 0xFFFFF7FFu, 0},   {9, 0x00000001u, 0},
    {9, 0x00000000u, 0},   {9, 0x00008000u, 0},   {9, 0xFFFF8000u, 0},
    {10, 0xFFFFFFFFu, 0},  {10, 0x0000FFFFu, 0},  {10, 0x80000000u, 0},
    {10, 65535u, 0},       {10, 2049u, 0},        {10, 0x00000000u, 0},

    /* --- FCVT.W.H / FCVT.WU.H: saturation on Inf/NaN + RNE ties --- */
    {11, 0x7C00, 0},       {11, 0xFC00, 0},       {11, 0x7E00, 0},
    {11, 0x3E00, 0},       {11, 0xB800, 0},       {11, 0x5800, 0},
    {11, 0x57FF, 0},       {11, 0x03FF, 0},       {11, 0x0400, 0},
    {11, 0x8000, 0},       {11, 0x0000, 0},       {11, 0x4600, 0},
    {12, 0x7C00, 0},       {12, 0x7E00, 0},       {12, 0xFC00, 0},
    {12, 0x3E00, 0},       {12, 0x5800, 0},       {12, 0x0000, 0},
    {12, 0x8000, 0},       {12, 0x4600, 0},

    /* --- FSGNJ/FSGNJN/FSGNJX: sign-corner + NaN sign preservation --- */
    {13, 0x7E00, 0xBC00},  {13, 0x3C00, 0x8000},  {13, 0xFC00, 0x3C00},
    {13, 0x8000, 0x8000},  {14, 0x7E00, 0x3C00},  {14, 0x3C00, 0x8000},
    {14, 0xFC00, 0x3C00},  {15, 0x7E00, 0x8000},  {15, 0x3C00, 0x8000},
    {15, 0xBC00, 0x3C00},  {15, 0xFC00, 0xBC00},

    /* --- FCLASS: every category bit position --- */
    {16, 0x7C00, 0},       {16, 0xFC00, 0},       {16, 0x0000, 0},
    {16, 0x8000, 0},       {16, 0x0001, 0},       {16, 0x8001, 0},
    {16, 0x3C00, 0},       {16, 0xBC00, 0},       {16, 0x7E00, 0},
    {16, 0x7D00, 0},       {16, 0x7F00, 0},       {16, 0x7BFF, 0},
    {16, 0xFBFF, 0},
};

static const unsigned edge_count = sizeof(edge_vectors) / sizeof(edge_vectors[0]);

#endif /* FPU_VECTORS_H */
