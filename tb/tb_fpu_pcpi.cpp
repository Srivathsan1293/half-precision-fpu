// tb/tb_fpu_pcpi.cpp
//
// Verilator harness for the PicoRV32 + FPU-PCPI SoC (tb/soc_fpu_top.sv).
//
// Loads tb/firmware/firmware.hex (via $readmemh in the SoC top), resets the
// core, runs until the firmware writes its done marker, then reads the
// result words back through the public RAM array and compares them against a
// golden model.
//
// The firmware stores a test-type magic at 0x1FC so the harness auto-detects
// which test ran:
//   - baseline integer smoke test (tb/firmware/test_main.c)
//   - FPU PCPI test (tb/firmware/fpu_test_main.c) — only produces correct
//     results when the FPU PCPI wrapper is connected (HAS_FPU_PCPI build).

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <verilated.h>
#include <verilated_vcd_c.h>

#include "Vsoc_fpu_top.h"
#include "Vsoc_fpu_top___024root.h"

#include "firmware/fpu_vectors.h"
#include "firmware/fpu_bench.h"

static const uint32_t RESULT_BASE      = 0x1000;
static const uint32_t TEST_MAGIC_ADDR  = 0x1C00;
static const uint32_t DONE_ADDR        = 0x1C04;
static const uint32_t DONE_MAGIC       = 0xDEADBEEF;
static const uint32_t MAGIC_BASELINE   = 0xBA51E000;
static const uint32_t MAGIC_FPU        = 0x5F50555A;
static const uint32_t MAGIC_STRESS     = 0x5F50555C;
static const uint32_t MAGIC_EDGE       = 0x5F505545;
static const uint32_t MAGIC_SPIKE      = 0x5F50535F;
static const uint32_t MAGIC_EMU        = 0x5F505345;
static const uint32_t MAGIC_ZHINX      = 0x5F50555D;
static const uint32_t MAGIC_UNSUP      = 0x5F505546;
// Unsupported-op probe addresses (see tb/firmware/fpu_unsup_main.S):
// 0x1C08 = faulting instruction word (written by emu_halt)
// 0x1C0C = resume PC P+4            (written by emu_halt)
// 0x1C10 = probe PC P               (written by the firmware before the probe)
static const uint32_t UNS_MARK_ADDR   = 0x1C08;
static const uint32_t UNS_PC_ADDR     = 0x1C0C;
static const uint32_t PROBE_PC_ADDR   = 0x1C10;
static const uint32_t MAGIC_ASM_ALL    = 0x5F505541;
static const uint32_t MAGIC_BENCH      = 0x5F50555E;
static const uint32_t MAGIC_BENCH_MM   = 0x5F50555F;
static const uint32_t MAGIC_BENCH_DIG  = 0x5F505560;
static const uint32_t MAGIC_BENCH_DIV  = 0x5F505561;
static const uint32_t MAGIC_BENCH_AI   = 0x41494E4E; /* "AINN" */

static const uint64_t MAX_CYCLES = 200000;

static inline uint32_t rd_ram(Vsoc_fpu_top* dut, uint32_t byte_addr) {
    return dut->rootp->soc_fpu_top__DOT__ram[byte_addr >> 2];
}

// -------------------------------------------------------------------------
// IEEE-754 golden model for half-precision ops (mirrors tb/tb_fpu.cpp)
// -------------------------------------------------------------------------
static uint16_t ieee_quiet_nan() { return 0x7E00; }

static bool is_nan16(uint16_t x) {
    return ((x >> 10) & 0x1F) == 31 && (x & 0x03FF) != 0;
}
static bool is_inf16(uint16_t x) {
    return ((x >> 10) & 0x1F) == 31 && (x & 0x03FF) == 0;
}
static bool is_zero16(uint16_t x) {
    return ((x >> 10) & 0x1F) == 0 && (x & 0x03FF) == 0;
}

static uint16_t golden_fadd(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    if (is_inf16(a) && is_inf16(b)) {
        if (((a ^ b) & 0x8000) != 0) return ieee_quiet_nan();
        return a;
    }
    _Float16 fa, fb, fr;
    std::memcpy(&fa, &a, sizeof(uint16_t));
    std::memcpy(&fb, &b, sizeof(uint16_t));
    fr = fa + fb;
    uint16_t r;
    std::memcpy(&r, &fr, sizeof(uint16_t));
    return r;
}

static uint16_t golden_fsub(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    if (is_inf16(a) && is_inf16(b)) {
        if (((a ^ b) & 0x8000) == 0) return ieee_quiet_nan();
        return a;
    }
    _Float16 fa, fb, fr;
    std::memcpy(&fa, &a, sizeof(uint16_t));
    std::memcpy(&fb, &b, sizeof(uint16_t));
    fr = fa - fb;
    uint16_t r;
    std::memcpy(&r, &fr, sizeof(uint16_t));
    return r;
}

static uint16_t golden_fmul(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    _Float16 fa, fb, fr;
    std::memcpy(&fa, &a, sizeof(uint16_t));
    std::memcpy(&fb, &b, sizeof(uint16_t));
    fr = fa * fb;
    uint16_t r;
    std::memcpy(&r, &fr, sizeof(uint16_t));
    return r;
}

static uint16_t golden_fdiv(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    if ((is_zero16(a) && is_zero16(b)) || (is_inf16(a) && is_inf16(b)))
        return ieee_quiet_nan();
    if (is_inf16(a) || is_zero16(b))
        return 0x7C00 | ((a ^ b) & 0x8000);
    if (is_zero16(a) || is_inf16(b))
        return (a ^ b) & 0x8000;
    _Float16 fa, fb, fr;
    std::memcpy(&fa, &a, sizeof(uint16_t));
    std::memcpy(&fb, &b, sizeof(uint16_t));
    fr = fa / fb;
    uint16_t r;
    std::memcpy(&r, &fr, sizeof(uint16_t));
    return r;
}

static uint16_t golden(int op, uint16_t a, uint16_t b) {
    switch (op) {
        case 0: return golden_fadd(a, b);
        case 1: return golden_fsub(a, b);
        case 2: return golden_fmul(a, b);
        default: return golden_fdiv(a, b);
    }
}

// -------------------------------------------------------------------------
// FPU test vectors: {op, a, b} in lockstep with tb/firmware/fpu_test_main.c
// -------------------------------------------------------------------------
struct FPVec { int op; uint16_t a; uint16_t b; };
static const FPVec fpu_vectors[] = {
    {0, 0x3C00, 0x4000},  // 1.0 + 2.0  =  3.0
    {1, 0x4000, 0x3C00},  // 2.0 - 1.0  =  1.0
    {2, 0x4200, 0x4000},  // 3.0 * 2.0  =  6.0
    {3, 0x3C00, 0x4000},  // 1.0 / 2.0  =  0.5
    {0, 0x3C00, 0x7C00},  // 1.0 + inf  =  inf
    {1, 0x7C00, 0x7C00},  // inf - inf  =  NaN
    {2, 0x0000, 0x7C00},  // 0.0 * inf  =  NaN
    {3, 0x3C00, 0x0000},  // 1.0 / 0.0  =  inf
    {3, 0x0000, 0x0000},  // 0.0 / 0.0  =  NaN
    {0, 0x3C00, 0xC000},  // 1.0 + -2.0 = -1.0
    {2, 0xC000, 0x4200},  // -2.0 * 3.0 = -6.0
    {1, 0x3800, 0x3C00},  // 0.5 - 1.0  = -0.5
    {3, 0x4200, 0x4000},  // 3.0 / 2.0  =  1.5
    {0, 0x7E00, 0x3C00},  // NaN + 1.0  =  NaN
    {3, 0x3C00, 0x7E00},  // 1.0 / NaN  =  NaN
};
static const int fpu_vectors_count = sizeof(fpu_vectors) / sizeof(fpu_vectors[0]);

static const char* op_names[] = { "FADD", "FSUB", "FMUL", "FDIV" };

// -------------------------------------------------------------------------
// Stress test golden checks (firmware: fpu_stress_main.c)
// -------------------------------------------------------------------------
static uint16_t f16_bits(_Float16 f) {
    uint16_t r;
    std::memcpy(&r, &f, sizeof(uint16_t));
    return r;
}
static _Float16 f16_from(uint16_t x) {
    _Float16 f;
    std::memcpy(&f, &x, sizeof(uint16_t));
    return f;
}

static int check_stress(Vsoc_fpu_top* dut) {
    int failures = 0;
    unsigned vi = 0;

    auto print = [&](const char* tag, unsigned idx, uint16_t got, uint16_t exp) -> bool {
        bool ok = (is_nan16(exp) && is_nan16(got)) || (exp == got);
        std::cout << "  " << tag << "[" << std::dec << idx << "] = 0x" << std::hex
                  << std::setw(4) << std::setfill('0') << got
                  << " (expect 0x" << std::setw(4) << exp << ") "
                  << (ok ? "PASS" : "FAIL") << std::dec << "\n";
        if (!ok) failures++;
        return ok;
    };
    auto rd16 = [&](unsigned idx) -> uint16_t {
        return rd_ram(dut, RESULT_BASE + 4u * idx) & 0xFFFF;
    };

    std::cout << "  --- A: numeric sweep (" << std::dec << fpu_sweep_count
              << " vectors) ---\n";
    for (unsigned v = 0; v < fpu_sweep_count; v++, vi++) {
        uint16_t exp = golden(fpu_sweep_vectors[v].op, fpu_sweep_vectors[v].a, fpu_sweep_vectors[v].b);
        print("vec", v, rd16(vi), exp);
    }

    std::cout << "  --- B: back-to-back FADD x8 ---\n";
    {
        uint16_t exp = golden(0, STRESS_B2B_A, STRESS_B2B_B);
        for (unsigned k = 0; k < STRESS_B2B_COUNT; k++, vi++)
            print("b2b", k, rd16(vi), exp);
    }

    std::cout << "  --- C: accumulator rd==rs1 (x1 += 0.5) x8 ---\n";
    {
        _Float16 acc = f16_from((uint16_t)STRESS_ACC1_INIT);
        for (unsigned k = 0; k < STRESS_ACC1_COUNT; k++, vi++) {
            acc = acc + f16_from((uint16_t)STRESS_ACC1_B);
            print("acc1", k, rd16(vi), f16_bits(acc));
        }
    }

    std::cout << "  --- D: accumulator rd==x1,rs2==x1 (x1 += 2.0) x8 ---\n";
    {
        _Float16 acc = f16_from((uint16_t)STRESS_ACC2_INIT);
        for (unsigned k = 0; k < STRESS_ACC2_COUNT; k++, vi++) {
            acc = acc + f16_from((uint16_t)STRESS_ACC2_A);
            print("acc2", k, rd16(vi), f16_bits(acc));
        }
    }

    std::cout << "  --- E: loop (4 iters x 4 ops) ---\n";
    {
        unsigned it, op;
        for (it = 0; it < STRESS_LOOP_ITERS; it++) {
            for (op = 0; op < STRESS_LOOP_OPS; op++, vi++) {
                uint16_t exp = golden(op, fpu_sweep_vectors[it].a, fpu_sweep_vectors[it].b);
                print("loop", it * STRESS_LOOP_OPS + op, rd16(vi), exp);
            }
        }
    }

    std::cout << "  --- F: discard (rd=x0) then fresh FADD ---\n";
    {
        uint16_t exp = golden(0, fpu_sweep_vectors[0].a, fpu_sweep_vectors[0].b);
        print("discard+recheck", 0, rd16(vi), exp);
        vi++;
    }

    return failures;
}

// Week 2 SW-vs-HW cycle-count benchmark. The firmware ran an identical FIR
// filter twice (software soft-float reference and hardware PCPI) and stored:
//   [0, N)   SW output window (N = BENCH_N_SAMPLES)
//   [N, 2N)  HW output window
//   [2N]     SW final accumulator
//   [2N+1]   HW final accumulator
// The outputs must be bit-identical; the harness additionally reports the
// cycle counts captured at the phase markers (see fpu_bench.h). `label`
// names the workload in the per-sample printout.
static int check_bench(Vsoc_fpu_top* dut, const char* label, uint32_t n,
                       uint64_t sw_start, uint64_t sw_end,
                       uint64_t hw_start, uint64_t total) {
    int failures = 0;
    const uint32_t N = n;

    for (uint32_t i = 0; i < N; i++) {
        uint16_t sw = rd_ram(dut, RESULT_BASE + 4 * i) & 0xFFFF;
        uint16_t hw = rd_ram(dut, RESULT_BASE + 4 * (N + i)) & 0xFFFF;
        bool ok = (sw == hw);
        std::cout << "  " << label << "[" << std::dec << i << "] sw=0x" << std::hex
                  << std::setw(4) << std::setfill('0') << sw
                  << " hw=0x" << std::setw(4) << hw
                  << (ok ? " match" : " MISMATCH") << std::dec << "\n";
        if (!ok) failures++;
    }
    uint16_t sw_acc = rd_ram(dut, RESULT_BASE + 4 * (2 * N)) & 0xFFFF;
    uint16_t hw_acc = rd_ram(dut, RESULT_BASE + 4 * (2 * N + 1)) & 0xFFFF;
    bool acc_ok = (sw_acc == hw_acc);
    std::cout << "  acc(final) sw=0x" << std::hex << sw_acc << " hw=0x"
              << hw_acc << (acc_ok ? " match" : " MISMATCH") << std::dec << "\n";
    if (!acc_ok) failures++;

    // Cycles: [sw_start, sw_end) for SW, [hw_start, total) for HW.
    uint64_t sw_cyc = sw_end - sw_start;
    uint64_t hw_cyc = total - hw_start;
    std::cout << "  cycles: SW soft-float phase = " << std::dec << sw_cyc
              << "  HW PCPI phase = " << hw_cyc
              << "  speedup = " << (hw_cyc ? (double)sw_cyc / (double)hw_cyc : 0.0)
              << "x\n";
    return failures;
}

// Phase 0 spike: verify the ebreak-IRQ premise. Firmware computes the expected
// resume PC (faulting instruction + 4) and the handler stores the q-reg 0 value
// it actually received.
static int check_spike(Vsoc_fpu_top* dut) {
    const uint32_t EXPECT_ADDR = 0x1C10;
    const uint32_t GOT_ADDR    = 0x1C14;
    const uint32_t OK_ADDR     = 0x1C18;

    uint32_t expected = rd_ram(dut, EXPECT_ADDR);
    uint32_t got      = rd_ram(dut, GOT_ADDR);
    uint32_t okword   = rd_ram(dut, OK_ADDR);

    std::cout << "  expected resume pc: 0x" << std::hex << expected << std::dec << "\n";
    std::cout << "  got resume pc     : 0x" << std::hex << got << std::dec << "\n";

    int failures = 0;
    if (expected != got) {
        std::cout << "  MISMATCH: handler did not receive the resume PC\n";
        failures++;
    }
    if (okword != 0) {
        std::cout << "  firmware compare reported FAIL\n";
        failures++;
    }
    if (failures == 0)
        std::cout << "  IRQ premise confirmed (q-reg 0 == P+4)\n";
    return failures;
}

// Phase 4: emulator self-test. Hand-written Zhinx asm drives only emulated ops;
// the harness compares the stored results against the expected constants.
static int check_emu(Vsoc_fpu_top* dut) {
    const uint32_t expect[] = {
        0x3C00, 0x4000, 0x3C00, 0x4000, 0, 1, 1,
        0x4000, 0xC000, 0x40, 0x4000, 0x3C00, 1, 2,
    };
    const int n = sizeof(expect) / sizeof(expect[0]);

    int failures = 0;
    for (int i = 0; i < n; i++) {
        uint32_t got = rd_ram(dut, RESULT_BASE + 4 * i);
        bool ok = (got == expect[i]);
        std::cout << "  emu[" << i << "] = 0x" << std::hex << std::setw(8)
                  << std::setfill('0') << got << " (expect 0x"
                  << expect[i] << ") " << (ok ? "PASS" : "FAIL") << std::dec << "\n";
        if (!ok) failures++;
    }
    return failures;
}

// Reused golden model for zhinx ops (ops 0-3 hardware, 4-16 emulator).
// Takes the full 32-bit operands: FCVT.H.W/WU consume the whole word, every
// other op only the low 16 bits. Callers must compare HW-op (0-3) NaN results
// NaN-tolerantly (the hardware always emits 0x7E00).
static uint32_t zh_fcvt_w_h(uint16_t h);
static uint32_t zh_fcvt_wu_h(uint16_t h);

static uint32_t zh_golden(unsigned op, uint32_t a, uint32_t b) {
    uint16_t ha = a & 0xFFFF;
    uint16_t hb = b & 0xFFFF;
    switch (op) {
        case 0: case 1: case 2: case 3: return golden(op, ha, hb);
        case 4: { // FMIN.H
            if (is_nan16(ha) && is_nan16(hb)) return ha | 0x0200;
            else if (is_nan16(ha)) return hb;
            else if (is_nan16(hb)) return ha;
            else if ((ha & 0x7FFF) == 0 && (hb & 0x7FFF) == 0) return 0x8000;
            else return (f16_from(ha) < f16_from(hb)) ? ha : hb;
        }
        case 5: { // FMAX.H
            if (is_nan16(ha) && is_nan16(hb)) return ha | 0x0200;
            else if (is_nan16(ha)) return hb;
            else if (is_nan16(hb)) return ha;
            else if ((ha & 0x7FFF) == 0 && (hb & 0x7FFF) == 0) return 0x0000;
            else return (f16_from(ha) < f16_from(hb)) ? hb : ha;
        }
        case 6: { // FEQ.H
            if (is_nan16(ha) || is_nan16(hb)) return 0;
            else return ((ha == hb) || ((ha & 0x7FFF) == 0 && (hb & 0x7FFF) == 0)) ? 1 : 0;
        }
        case 7: { // FLT.H
            if (is_nan16(ha) || is_nan16(hb)) return 0;
            else return (f16_from(ha) < f16_from(hb)) ? 1 : 0;
        }
        case 8: { // FLE.H
            if (is_nan16(ha) || is_nan16(hb)) return 0;
            else return (f16_from(ha) <= f16_from(hb)) ? 1 : 0;
        }
        case 9: return f16_bits((_Float16)(int32_t)a);   // FCVT.H.W (full word)
        case 10: return f16_bits((_Float16)a);          // FCVT.H.WU (full word)
        case 11: return zh_fcvt_w_h(ha);                // FCVT.W.H
        case 12: return zh_fcvt_wu_h(ha);               // FCVT.WU.H
        case 13: return (hb & 0x8000) | (ha & 0x7FFF);  // FSGNJ.H
        case 14: return (~hb & 0x8000) | (ha & 0x7FFF); // FSGNJN.H
        case 15: return ((ha ^ hb) & 0x8000) | (ha & 0x7FFF);  // FSGNJX.H
        default: { // 16: FCLASS.H
            uint32_t s = ha & 0x8000, e = (ha >> 10) & 31, m = ha & 0x3FF;
            return (e == 31) ? (m == 0 ? (s ? 0x001 : 0x080) : (m & 0x200 ? 0x200 : 0x100))
                : (e == 0) ? (m == 0 ? (s ? 0x008 : 0x010) : (s ? 0x004 : 0x020))
                : (s ? 0x002 : 0x040);
        }
    }
}

// RNE conversion of a half-precision float to an int32 (FCVT.W.H golden).
static uint32_t zh_fcvt_w_h(uint16_t h) {
    uint32_t sign = h & 0x8000;
    uint32_t exp = (h >> 10) & 31;
    uint32_t mant = h & 0x03FF;
    if (exp == 31) {
        if (mant == 0) return sign ? 0x80000000u : 0x7FFFFFFFu;
        return 0x7FFFFFFFu;
    }
    if (exp == 0) return 0;
    int e = (int)exp - 15;
    if (e >= 10) return sign ? (0u - ((0x400u | mant) << (e - 10)))
                             : ((0x400u | mant) << (e - 10));
    uint32_t shift = 10 - e;
    uint32_t mag = (0x400u | mant) >> shift;
    uint32_t rem = (0x400u | mant) & ((1u << shift) - 1);
    if (rem) {
        uint32_t den = (1u << (shift - 1));
        if (rem > den) mag++;
        else if (rem == den && (mag & 1)) mag++;
    }
    return sign ? (0u - mag) : mag;
}

static uint32_t zh_fcvt_wu_h(uint16_t h) {
    uint32_t sign = h & 0x8000;
    uint32_t exp = (h >> 10) & 31;
    uint32_t mant = h & 0x03FF;
    if (sign) return 0;
    if (exp == 31) return 0xFFFFFFFFu;
    if (exp == 0) return 0;
    int e = (int)exp - 15;
    if (e >= 10) {
        uint32_t v = (0x400u | mant) << (e - 10);
        if (v < (0x400u | mant)) return 0xFFFFFFFFu;
        return v;
    }
    uint32_t shift = 10 - e;
    uint32_t mag = (0x400u | mant) >> shift;
    uint32_t rem = (0x400u | mant) & ((1u << shift) - 1);
    if (rem) {
        uint32_t den = (1u << (shift - 1));
        if (rem > den) mag++;
        else if (rem == den && (mag & 1)) mag++;
    }
    return mag;
}

// Phase 5: standard-Zhinx integration. The firmware (fpu_zhinx_main.c, built
// with clang -march=rv32im_zhinx) executes the shared zhinx_vectors table:
// ops 0..3 through the hardware PCPI wrapper, ops 4..16 through the software
// emulator. Per-op golden uses the full 32-bit operands (FCVT.H.W/WU consume
// the whole word, everything else only the low 16 bits).
//
// Generic table checker shared by check_zhinx (Stage B/C/F zhinx sweep) and
// check_edge (Stage E trap-hunt boundary vectors). Both must use the exact
// same golden logic so the tables can never disagree with the model.
static int check_zhinx_table(Vsoc_fpu_top* dut, const zhinx_vec_t* vec,
                             unsigned count, const char* label) {
    const char* zop_names[] = {
        "FADD", "FSUB", "FMUL", "FDIV", "FMIN", "FMAX",
        "FEQ", "FLT", "FLE", "FCVT.H.W", "FCVT.H.WU",
        "FCVT.W.H", "FCVT.WU.H", "FSGNJ", "FSGNJN", "FSGNJX", "FCLASS",
    };

    int failures = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned op = vec[i].op;
        uint32_t a = vec[i].a;
        uint32_t b = vec[i].b;
        uint16_t ha = a & 0xFFFF;
        uint16_t hb = b & 0xFFFF;
        uint32_t exp, got;

        switch (op) {
        case 0: case 1: case 2: case 3: {
            // NaN results: IEEE-754 leaves the sign/payload of an invalid-op
            // NaN implementation-defined; the hardware always emits 0x7E00 while
            // the host _Float16 path may set the sign bit (e.g. 0xFE00 for
            // 0*inf). Compare NaN results as NaN (like check_stress).
            exp = golden(op, ha, hb);
            break;
        }
        case 4: {  // FMIN.H
            if (is_nan16(ha) && is_nan16(hb)) exp = ha | 0x0200;
            else if (is_nan16(ha)) exp = hb;
            else if (is_nan16(hb)) exp = ha;
            else if ((ha & 0x7FFF) == 0 && (hb & 0x7FFF) == 0) exp = 0x8000;
            else exp = (f16_from(ha) < f16_from(hb)) ? ha : hb;
            break;
        }
        case 5: {  // FMAX.H
            if (is_nan16(ha) && is_nan16(hb)) exp = ha | 0x0200;
            else if (is_nan16(ha)) exp = hb;
            else if (is_nan16(hb)) exp = ha;
            else if ((ha & 0x7FFF) == 0 && (hb & 0x7FFF) == 0) exp = 0x0000;
            else exp = (f16_from(ha) < f16_from(hb)) ? hb : ha;
            break;
        }
        case 6: {  // FEQ.H
            if (is_nan16(ha) || is_nan16(hb)) exp = 0;
            else exp = ((ha == hb) || ((ha & 0x7FFF) == 0 && (hb & 0x7FFF) == 0)) ? 1 : 0;
            break;
        }
        case 7: {  // FLT.H
            if (is_nan16(ha) || is_nan16(hb)) exp = 0;
            else exp = (f16_from(ha) < f16_from(hb)) ? 1 : 0;
            break;
        }
        case 8: {  // FLE.H
            if (is_nan16(ha) || is_nan16(hb)) exp = 0;
            else exp = (f16_from(ha) <= f16_from(hb)) ? 1 : 0;
            break;
        }
        case 9:  exp = f16_bits((_Float16)(int32_t)a); break;   // FCVT.H.W
        case 10: exp = f16_bits((_Float16)a);          break;   // FCVT.H.WU
        case 11: exp = zh_fcvt_w_h(ha);  break;                  // FCVT.W.H
        case 12: exp = zh_fcvt_wu_h(ha); break;                 // FCVT.WU.H
        case 13: exp = (hb & 0x8000) | (ha & 0x7FFF);  break;   // FSGNJ.H
        case 14: exp = (~hb & 0x8000) | (ha & 0x7FFF); break;   // FSGNJN.H
        case 15: exp = ((ha ^ hb) & 0x8000) | (ha & 0x7FFF); break;  // FSGNJX.H
        default: {  // 16: FCLASS.H
            uint32_t s = ha & 0x8000, e = (ha >> 10) & 31, m = ha & 0x3FF;
            exp = (e == 31) ? (m == 0 ? (s ? 0x001 : 0x080) : (m & 0x200 ? 0x200 : 0x100))
                : (e == 0) ? (m == 0 ? (s ? 0x008 : 0x010) : (s ? 0x004 : 0x020))
                : (s ? 0x002 : 0x040);
            break;
        }
        }

        got = rd_ram(dut, RESULT_BASE + 4u * i);
        bool ok = (op <= 3) ? ((got == exp) || (is_nan16(got & 0xFFFF) && is_nan16(exp & 0xFFFF)))
                            : (got == exp);
        std::cout << "  " << label << "[" << std::dec << i << "] " << zop_names[op]
                  << " 0x" << std::hex << std::setw(8)
                  << std::setfill('0') << a << " 0x" << std::setw(8) << b
                  << " -> 0x" << std::setw(8) << got << " (expect 0x"
                  << std::setw(8) << exp << ") " << (ok ? "PASS" : "FAIL")
                  << std::dec << "\n";
        if (!ok) failures++;
    }
    return failures;
}

static int check_zhinx(Vsoc_fpu_top* dut) {
    return check_zhinx_table(dut, zhinx_vectors, zhinx_count, "zhinx");
}

// Phase 6 (Stage E): edge-case / trap-hunt vectors. Same golden logic as the
// zhinx sweep; the firmware (fpu_edge_main.c) runs edge_vectors through every
// standard-Zhinx encoding. A passing run also proves none of the boundary
// operands trap the core (the harness would otherwise stop on dut->trap).
static int check_edge(Vsoc_fpu_top* dut) {
    return check_zhinx_table(dut, edge_vectors, edge_count, "edge");
}

// Reused golden model for the "asm_all_ops.S" test. Same logic as check_zhinx.
// Phase 6 (Stage E): unsupported-op probe. The firmware (fpu_unsup_main.S)
// executes an FSQRT.H that neither the wrapper nor the emulator supports; the
// emulator must record the exact faulting instruction (UNS_MARK) and resume PC
// (UNS_PC = P+4) and halt, instead of fabricating a result. This check runs
// when the run loop saw the marker but no DONE (i.e. the core was halted in
// the emulator's spin loop).
static int check_unsup(Vsoc_fpu_top* dut) {
    int failures = 0;
    uint32_t magic      = rd_ram(dut, TEST_MAGIC_ADDR);
    uint32_t probe_pc   = rd_ram(dut, PROBE_PC_ADDR);
    uint32_t insn_at_p  = rd_ram(dut, probe_pc);
    uint32_t mark       = rd_ram(dut, UNS_MARK_ADDR);
    uint32_t uns_pc     = rd_ram(dut, UNS_PC_ADDR);

    std::cout << "  unsupported-op probe: magic=0x" << std::hex << std::setw(8)
              << std::setfill('0') << magic
              << " probe_pc=0x" << std::setw(8) << probe_pc
              << " insn@P=0x" << std::setw(8) << insn_at_p
              << " uns_mark=0x" << std::setw(8) << mark
              << " uns_pc=0x" << std::setw(8) << uns_pc << std::dec << "\n";

    bool ok_magic = (magic == MAGIC_UNSUP);
    std::cout << "  unsupported: test magic dispatched                "
              << (ok_magic ? "PASS" : "FAIL") << "\n";
    if (!ok_magic) failures++;

    bool ok_mark = (mark == insn_at_p);
    std::cout << "  unsupported: emulator recorded exact faulting insn "
              << (ok_mark ? "PASS" : "FAIL") << "\n";
    if (!ok_mark) failures++;

    bool ok_pc = (uns_pc == probe_pc + 4);
    std::cout << "  unsupported: resume_pc == P+4                     "
              << (ok_pc ? "PASS" : "FAIL") << "\n";
    if (!ok_pc) failures++;

    // Both unsupported-op classes (FSQRT and FMA) must hit the same path.
    bool ok_never_done = (rd_ram(dut, DONE_ADDR) != DONE_MAGIC);
    std::cout << "  unsupported: core halted (no DONE, no fabricated result) "
              << (ok_never_done ? "PASS" : "FAIL") << "\n";
    if (!ok_never_done) failures++;

    return failures;
}

static int check_asm_all(Vsoc_fpu_top* dut) {
    struct AsmOp { unsigned op; uint32_t a; uint32_t b; };
    static const AsmOp asm_ops[] = {
        {0, 0x3C00, 0x4000},   // FADD.H 1.0+2.0
        {1, 0x4000, 0x3C00},   // FSUB.H 2.0-1.0
        {2, 0x4000, 0x4200},   // FMUL.H 2.0*3.0
        {3, 0x4200, 0x4000},   // FDIV.H 3.0/2.0
        {4, 0xC000, 0x3C00},   // FMIN.H -2.0,1.0
        {5, 0x3C00, 0x4000},   // FMAX.H 1.0,2.0
        {6, 0x3C00, 0x3C00},   // FEQ.H
        {7, 0x4000, 0x3C00},   // FLT.H
        {8, 0x3C00, 0x3C00},   // FLE.H
        {9, 0x00000005, 0},    // FCVT.H.W 5
        {10, 0x0000FFFF, 0},   // FCVT.H.WU 65535
        {11, 0x3E00, 0},       // FCVT.W.H 1.5
        {12, 0x5600, 0},       // FCVT.WU.H
        {13, 0x4000, 0xBC00},  // FSGNJ.H
        {14, 0x4000, 0x3C00},  // FSGNJN.H
        {15, 0xC000, 0x3C00},  // FSGNJX.H
        {16, 0x7E00, 0},       // FCLASS.H qNaN -> 0x200
    };

    int failures = 0;
    const char* zop_names[] = { "FADD", "FSUB", "FMUL", "FDIV", "FMIN", "FMAX",
                                "FEQ", "FLT", "FLE", "FCVT.H.W", "FCVT.H.WU",
                                "FCVT.W.H", "FCVT.WU.H", "FSGNJ", "FSGNJN", "FSGNJX", "FCLASS" };
    uint32_t RESULT_BASE = 0x1000;

    for (unsigned i = 0; i < sizeof(asm_ops)/sizeof(asm_ops[0]); i++) {
        unsigned op = asm_ops[i].op;
        uint32_t a = asm_ops[i].a, b = asm_ops[i].b;

        uint32_t exp = zh_golden(op, a, b);
        uint32_t got = rd_ram(dut, RESULT_BASE + 4u * i);
        bool ok = (got == exp) || (is_nan16(got & 0xFFFF) && is_nan16(exp & 0xFFFF));

        std::cout << "  asm_all[" << i << "] " << zop_names[op]
                  << " 0x" << std::hex << std::setw(4) << std::setfill('0') << (a & 0xFFFF)
                  << " 0x" << std::setw(4) << (b & 0xFFFF)
                  << " -> 0x" << std::setw(8) << got << " (expect 0x"
                  << std::setw(8) << exp << ") " << (ok ? "PASS" : "FAIL")
                  << std::dec << "\n";
        if (!ok) failures++;
    }

    return failures;
}

// -------------------------------------------------------------------------
// Phase 7: run-and-dump mode. No golden checks -- just run a user program,
// then dump the RAM and the final GPR register file for inspection.
// -------------------------------------------------------------------------
static const char* gpr_names[32] = {
    "x0(zero)", "x1(ra)", "x2(sp)", "x3(gp)", "x4(tp)", "x5(t0)", "x6(t1)", "x7(t2)",
    "x8(s0)", "x9(s1)", "x10(a0)", "x11(a1)", "x12(a2)", "x13(a3)", "x14(a4)", "x15(a5)",
    "x16(a6)", "x17(a7)", "x18(s2)", "x19(s3)", "x20(s4)", "x21(s5)", "x22(s6)", "x23(s7)",
    "x24(s8)", "x25(s9)", "x26(s10)", "x27(s11)", "x28(t3)", "x29(t4)", "x30(t5)", "x31(t6)"
};

static void dump_state(Vsoc_fpu_top* dut, const char* path, uint64_t cyc,
                       bool done, bool trapped) {
    auto& gprs = dut->rootp->soc_fpu_top__DOT__u_cpu__DOT__cpuregs;
    auto& ram = dut->rootp->soc_fpu_top__DOT__ram;

    std::ofstream f(path);
    f << "PicoRV32 + FPU-PCPI run-and-dump (no golden checks)\n";
    f << "cycles: " << cyc << " ("
      << (done ? "done marker" : trapped ? "trap" : "timeout") << ")\n";
    f << "done marker @0x1C04 = 0x" << std::hex << rd_ram(dut, DONE_ADDR)
      << std::dec << "\n";

    f << "\n--- GPRs (x0..x31) ---\n";
    for (int r = 0; r < 32; r++) {
        f << "  " << gpr_names[r] << " = 0x" << std::hex << std::setw(8)
          << std::setfill('0') << gprs[r] << std::setfill(' ') << std::dec << "\n";
    }

    f << "\n--- RAM (0x0000..0x3FFC, 8 words per line) ---\n";
    for (int w = 0; w < 4096; w += 8) {
        f << "0x" << std::hex << std::setw(4) << std::setfill('0') << (w * 4)
          << std::setfill(' ') << ": ";
        for (int j = 0; j < 8; j++) {
            f << std::hex << std::setw(8) << std::setfill('0') << ram[w + j]
              << std::setfill(' ') << std::dec << (j < 7 ? " " : "\n");
        }
    }
    f.close();
    std::cout << "state dumped to " << path << "\n";
}

// -------------------------------------------------------------------------

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Verilated::traceEverOn(true);
    VerilatedVcdC* tfp = new VerilatedVcdC;
    Vsoc_fpu_top* dut = new Vsoc_fpu_top;
    dut->trace(tfp, 99);
    tfp->open("testing_results/picorv32_fpu.vcd");

    bool vcd_on = false;
    const char* dump_path = nullptr;
    uint64_t max_cycles = MAX_CYCLES;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--trace") vcd_on = true;
        else if (std::string(argv[i]) == "--dump" && i + 1 < argc)
            dump_path = argv[++i];
        else if (std::string(argv[i]) == "--maxcycles" && i + 1 < argc)
            max_cycles = std::strtoull(argv[++i], nullptr, 10);
    }

    // ---- reset ----
    dut->clk = 0;
    dut->resetn = 0;
    for (int i = 0; i < 16; i++) {
        dut->eval();
        dut->clk = 1;
        dut->eval();
        dut->clk = 0;
    }
    dut->resetn = 1;

    // ---- run until done marker / trap / timeout ----
    bool done = false, trapped = false;
    uint64_t cyc;
    uint64_t bench_sw_start = 0, bench_sw_end = 0, bench_hw_start = 0;
    for (cyc = 0; cyc < max_cycles; cyc++) {
        dut->clk = 0;
        dut->eval();
        if (vcd_on) tfp->dump(cyc * 2);
        dut->clk = 1;
        dut->eval();
        if (vcd_on) tfp->dump(cyc * 2 + 1);

        // Capture Week 2 benchmark cycle markers (single-shot latches).
        if (cyc > 16) {
            if (!bench_sw_start && rd_ram(dut, BENCH_SW_START_ADDR) == BENCH_SW_START_MAGIC)
                bench_sw_start = cyc;
            if (!bench_sw_end && rd_ram(dut, BENCH_SW_END_ADDR) == BENCH_SW_END_MAGIC)
                bench_sw_end = cyc;
            if (!bench_hw_start && rd_ram(dut, BENCH_HW_START_ADDR) == BENCH_HW_START_MAGIC)
                bench_hw_start = cyc;
        }

        if (cyc > 16 && rd_ram(dut, DONE_ADDR) == DONE_MAGIC) {
            done = true;
            break;
        }
        // Unsupported-op probe: emu_halt() writes the faulting instruction to
        // UNS_MARK_ADDR (cycle N) and then the resume PC to UNS_PC_ADDR
        // (cycle N+1). Require BOTH markers before stopping, otherwise we can
        // catch the state between the two stores and read a stale resume PC.
        if (cyc > 16 && rd_ram(dut, UNS_MARK_ADDR) != 0 &&
            rd_ram(dut, UNS_PC_ADDR) != 0) {
            done = false;
            trapped = false;
            break;
        }
        if (dut->trap) {
            trapped = true;
            break;
        }
    }

    std::cout << "PicoRV32 + FPU-PCPI integration test\n";
    std::cout << "cycles run: " << cyc << (done ? " (done marker)" : trapped ? " (trap)" : " (timeout)") << "\n";

    if (dump_path) {
        // Phase 7 run-and-dump mode: no golden checks, just observe.
        dump_state(dut, dump_path, cyc, done, trapped);
        tfp->close();
        delete dut;
        delete tfp;
        std::cout << "STATUS: PASS (dump mode)\n";
        return 0;
    }

    int failures = 0;
    if (done) {
        uint32_t magic = rd_ram(dut, TEST_MAGIC_ADDR);

        if (magic == MAGIC_BASELINE) {
            const uint32_t expect[] = { 3, 9, 13, 7 };
            for (int i = 0; i < 4; i++) {
                uint32_t got = rd_ram(dut, RESULT_BASE + 4 * i);
                bool ok = (got == expect[i]);
                std::cout << "  int[" << i << "] = 0x" << std::hex << std::setw(8)
                          << std::setfill('0') << got << " (expect 0x"
                          << expect[i] << ") " << (ok ? "PASS" : "FAIL") << std::dec << "\n";
                if (!ok) failures++;
            }
        } else if (magic == MAGIC_FPU) {
            for (int i = 0; i < fpu_vectors_count; i++) {
                uint16_t a = fpu_vectors[i].a;
                uint16_t b = fpu_vectors[i].b;
                uint16_t exp = golden(fpu_vectors[i].op, a, b);
                uint32_t got32 = rd_ram(dut, RESULT_BASE + 4 * i);
                uint16_t got = got32 & 0xFFFF;

                bool ok = (is_nan16(exp) && is_nan16(got)) || (exp == got);
                std::cout << "  " << op_names[fpu_vectors[i].op]
                          << " 0x" << std::hex << std::setw(4) << std::setfill('0') << a
                          << " 0x" << std::setw(4) << b
                          << " -> 0x" << std::setw(4) << got
                          << " (expect 0x" << std::setw(4) << exp << ") "
                          << (ok ? "PASS" : "FAIL") << std::dec << "\n";
                if (!ok) failures++;
            }
        } else if (magic == MAGIC_STRESS) {
            failures = check_stress(dut);
        } else if (magic == MAGIC_SPIKE) {
            failures = check_spike(dut);
        } else if (magic == MAGIC_EMU) {
            failures = check_emu(dut);
        } else if (magic == MAGIC_ZHINX) {
            failures = check_zhinx(dut);
        } else if (magic == MAGIC_EDGE) {
            failures = check_edge(dut);
        } else if (magic == MAGIC_ASM_ALL) {
            failures = check_asm_all(dut);
        } else if (magic == MAGIC_BENCH) {
            failures = check_bench(dut, "fir", BENCH_N_SAMPLES, bench_sw_start, bench_sw_end,
                                   bench_hw_start, cyc);
        } else if (magic == MAGIC_BENCH_MM) {
            failures = check_bench(dut, "mm", BENCH_N_SAMPLES, bench_sw_start, bench_sw_end,
                                   bench_hw_start, cyc);
        } else if (magic == MAGIC_BENCH_DIG) {
            failures = check_bench(dut, "dig", BENCH_N_SAMPLES, bench_sw_start, bench_sw_end,
                                   bench_hw_start, cyc);
        } else if (magic == MAGIC_BENCH_DIV) {
            failures = check_bench(dut, "div", BENCH_N_SAMPLES, bench_sw_start, bench_sw_end,
                                   bench_hw_start, cyc);
        } else if (magic == MAGIC_BENCH_AI) {
            // Week 3 AI-layer bench: OUT_DIM=4 outputs (see fpu_bench_ai_layer_main.c).
            failures = check_bench(dut, "ai", 4, bench_sw_start, bench_sw_end,
                                   bench_hw_start, cyc);
        } else {
            std::cout << "  unknown test magic 0x" << std::hex << magic << std::dec << "\n";
            failures++;
        }
    } else {
        uint32_t mark = rd_ram(dut, UNS_MARK_ADDR);
        if (mark != 0) {
            // Unsupported-op probe halt (emulator's emu_halt spin loop).
            failures = check_unsup(dut);
        } else {
            std::cout << "  core did not reach the done marker.\n";
            if (trapped)
                std::cout << "  core trapped (illegal instruction). If running the FPU test,\n"
                             "  the PCPI wrapper must be connected (build with HAS_FPU_PCPI).\n";
            failures++;
        }
    }

    tfp->close();
    delete dut;
    delete tfp;

    if (failures == 0) {
        std::cout << "STATUS: PASS\n";
        return 0;
    }
    std::cout << "STATUS: FAIL (" << failures << " mismatch(es))\n";
    return 1;
}
