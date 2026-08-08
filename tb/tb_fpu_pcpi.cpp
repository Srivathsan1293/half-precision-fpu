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
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <verilated.h>
#include <verilated_vcd_c.h>

#include "Vsoc_fpu_top.h"
#include "Vsoc_fpu_top___024root.h"

static const uint32_t RESULT_BASE      = 0x100;
static const uint32_t TEST_MAGIC_ADDR  = 0x1FC;
static const uint32_t DONE_ADDR        = 0x200;
static const uint32_t DONE_MAGIC       = 0xDEADBEEF;
static const uint32_t MAGIC_BASELINE   = 0xBA51E000;
static const uint32_t MAGIC_FPU        = 0x5F50555A;

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

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Verilated::traceEverOn(true);
    VerilatedVcdC* tfp = new VerilatedVcdC;
    Vsoc_fpu_top* dut = new Vsoc_fpu_top;
    dut->trace(tfp, 99);
    tfp->open("testing_results/picorv32_fpu.vcd");

    bool vcd_on = false;
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--trace") vcd_on = true;

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
    for (cyc = 0; cyc < MAX_CYCLES; cyc++) {
        dut->clk = 0;
        dut->eval();
        if (vcd_on) tfp->dump(cyc * 2);
        dut->clk = 1;
        dut->eval();
        if (vcd_on) tfp->dump(cyc * 2 + 1);

        if (cyc > 16 && rd_ram(dut, DONE_ADDR) == DONE_MAGIC) {
            done = true;
            break;
        }
        if (dut->trap) {
            trapped = true;
            break;
        }
    }

    std::cout << "PicoRV32 + FPU-PCPI integration test\n";
    std::cout << "cycles run: " << cyc << (done ? " (done marker)" : trapped ? " (trap)" : " (timeout)") << "\n";

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
        } else {
            std::cout << "  unknown test magic 0x" << std::hex << magic << std::dec << "\n";
            failures++;
        }
    } else {
        std::cout << "  core did not reach the done marker.\n";
        if (trapped)
            std::cout << "  core trapped (illegal instruction). If running the FPU test,\n"
                         "  the PCPI wrapper must be connected (build with HAS_FPU_PCPI).\n";
        failures++;
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
