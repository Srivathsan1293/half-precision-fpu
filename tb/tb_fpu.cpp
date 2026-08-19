#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <verilated.h>
#include "Vfpu_test.h"

// Golden Reference: True IEEE-754 Native C++ Engine for all 4 operations.
// IEEE-754 aware: 0/0, inf/inf, inf-inf are NaN (sign/payload unspecified),
// regardless of the _Float16 soft-float quirk that returns -Inf for these.
uint16_t ieee_quiet_nan() { return 0x7E00; }

bool is_nan16(uint16_t x) {
    return ((x >> 10) & 0x1F) == 31 && (x & 0x03FF) != 0;
}

bool is_inf16(uint16_t x) {
    return ((x >> 10) & 0x1F) == 31 && (x & 0x03FF) == 0;
}

bool is_zero16(uint16_t x) {
    return ((x >> 10) & 0x1F) == 0 && (x & 0x03FF) == 0;
}

uint16_t compute_golden_add(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    if (is_inf16(a) && is_inf16(b)) {
        // IEEE 754: +inf + -inf = NaN; same-sign inf sums to that inf.
        // (Raw _Float16 returns -Inf for inf-inf on some platforms.)
        if (((a ^ b) & 0x8000) != 0) return ieee_quiet_nan();
        return a;
    }
    _Float16 float_A, float_B, float_ans;
    std::memcpy(&float_A, &a, sizeof(uint16_t));
    std::memcpy(&float_B, &b, sizeof(uint16_t));
    float_ans = float_A + float_B;
    uint16_t result;
    std::memcpy(&result, &float_ans, sizeof(uint16_t));
    return result;
}

uint16_t compute_golden_sub(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    if (is_inf16(a) && is_inf16(b)) {
        // IEEE 754: inf - inf is NaN only for same-sign operands.
        // +Inf - (-Inf) = +Inf, -Inf - (+Inf) = -Inf (sign of a).
        if (((a ^ b) & 0x8000) == 0) return ieee_quiet_nan();
        return a;
    }
    _Float16 float_A, float_B, float_ans;
    std::memcpy(&float_A, &a, sizeof(uint16_t));
    std::memcpy(&float_B, &b, sizeof(uint16_t));
    float_ans = float_A - float_B;
    uint16_t result;
    std::memcpy(&result, &float_ans, sizeof(uint16_t));
    return result;
}

uint16_t compute_golden_mul(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    _Float16 float_A, float_B, float_ans;
    std::memcpy(&float_A, &a, sizeof(uint16_t));
    std::memcpy(&float_B, &b, sizeof(uint16_t));
    float_ans = float_A * float_B;
    uint16_t result;
    std::memcpy(&result, &float_ans, sizeof(uint16_t));
    return result;
}

uint16_t compute_golden_div(uint16_t a, uint16_t b) {
    if (is_nan16(a) || is_nan16(b)) return ieee_quiet_nan();
    if ((is_zero16(a) && is_zero16(b)) || (is_inf16(a) && is_inf16(b)))
        return ieee_quiet_nan(); // 0/0, inf/inf = NaN
    if (is_inf16(a) || is_zero16(b))
        return 0x7C00 | (a ^ b) & 0x8000; // a/0, inf/b = +/-Inf
    if (is_zero16(a) || is_inf16(b))
        return (a ^ b) & 0x8000; // 0/b, a/inf = +/-0
    _Float16 float_A, float_B, float_ans;
    std::memcpy(&float_A, &a, sizeof(uint16_t));
    std::memcpy(&float_B, &b, sizeof(uint16_t));
    float_ans = float_A / float_B;
    uint16_t result;
    std::memcpy(&result, &float_ans, sizeof(uint16_t));
    return result;
}

enum Category {
    CAT_NAN = 0,
    CAT_INF = 1,
    CAT_ZERO = 2,
    CAT_SUBNORM = 3,
    CAT_NORMAL = 4
};

const char* cat_names[] = {
    "Involves NaN              ",
    "Involves Infinity         ",
    "Involves True Zero        ",
    "Involves Subnormal(s)     ",
    "Normal Arithmetic (Normal)"
};

const char* op_names[] = { "ADD", "SUB", "MUL", "DIV" };

uint16_t compute_golden(int op, uint16_t a, uint16_t b) {
    switch (op) {
        case 0: return compute_golden_add(a, b);
        case 1: return compute_golden_sub(a, b);
        case 2: return compute_golden_mul(a, b);
        default: return compute_golden_div(a, b);
    }
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_test* dut = new Vfpu_test;

    std::ofstream fail_outfile("testing_results/fpu_failed_log.txt");
    if (!fail_outfile.is_open()) {
        std::cerr << "Error: Could not open output file." << std::endl;
        delete dut;
        return 1;
    }

    // Optional bounded "quick mode": ./Vfpu_test <bound>
    // Iterates a,b in [0, bound) for fast validation. Default: full 0..0xFFFF.
    uint32_t bound = 0x10000;
    if (argc > 1) {
        long b = std::strtol(argv[1], nullptr, 10);
        if (b >= 1 && b <= 0x10000) bound = static_cast<uint32_t>(b);
    }

    uint64_t total_tests = 0;
    uint64_t total_failed = 0;

    // Tracking Arrays for Summary [NaN, Inf, Zero, Subnorm, Normal]
    uint64_t cat_totals[5] = {0, 0, 0, 0, 0};
    uint64_t cat_fails[5]  = {0, 0, 0, 0, 0};

    uint64_t per_op_count = (uint64_t)bound * (uint64_t)bound;

    std::cout << "--- Starting Exact RTL FPU Combined Hardware Emulation Test ---" << std::endl;
    std::cout << "Testing all operations: ADD, SUB, MUL, DIV" << std::endl;
    std::cout << "Bound = " << std::hex << (bound - 1) << " (a,b each in [0," << std::hex << (bound - 1) << "])" << std::endl;
    std::cout << "Combinations per operation: " << std::dec << per_op_count << std::endl;
    std::cout << "FADD/FSUB/FMUL latency: 1 cycle (streaming); FDIV: fixed 12-cycle start-gated SRT\n" << std::endl;

    // Each operation runs as its own pass with op held constant. The output
    // mux selects on the *current* op, so op must not change while a result is
    // in flight. FADD/FSUB/FMUL are single-cycle registered datapaths (checked
    // every cycle against the input presented 1 cycle ago). FDIV is a
    // start-gated sequential SRT core: each input is presented with a one-cycle
    // `start` pulse and checked 12 cycles later (11-cycle SRT schedule + one
    // capture register).
    auto do_check = [&](int op, uint16_t check_a, uint16_t check_b, uint16_t hw_ans) {
        total_tests++;
        uint16_t expA = (check_a >> 10) & 0x1F;
        uint16_t manA = check_a & 0x03FF;
        uint16_t expB = (check_b >> 10) & 0x1F;
        uint16_t manB = check_b & 0x03FF;

        bool a_nan  = (expA == 31 && manA != 0);
        bool b_nan  = (expB == 31 && manB != 0);
        bool a_zero = (expA == 0 && manA == 0);
        bool b_zero = (expB == 0 && manB == 0);
        bool a_inf  = (expA == 31 && manA == 0);
        bool b_inf  = (expB == 31 && manB == 0);

        Category current_cat;
        if (a_nan || b_nan)           current_cat = CAT_NAN;
        else if (a_inf || b_inf)      current_cat = CAT_INF;
        else if (a_zero || b_zero)    current_cat = CAT_ZERO;
        else if ((expA == 0 && manA != 0) || (expB == 0 && manB != 0))
                                      current_cat = CAT_SUBNORM;
        else                          current_cat = CAT_NORMAL;

        cat_totals[current_cat]++;

        uint16_t expected_ans = compute_golden(op, check_a, check_b);

        // NaN sign/payload is unspecified by IEEE 754 - accept any NaN.
        bool both_nan = is_nan16(expected_ans) && is_nan16(hw_ans);
        if (!both_nan && hw_ans != expected_ans) {
            total_failed++;
            cat_fails[current_cat]++;

            if (total_failed <= 50) {
                fail_outfile << "FAIL OP=" << op_names[op]
                             << " a=0x" << std::hex << check_a
                             << " | b=0x" << check_b
                             << " | Expected=0x" << expected_ans
                             << " | Got=0x" << hw_ans << std::dec << "\n";
            }
        }
    };

    for (int op = 0; op < 4; op++) {
        uint64_t op_tested = 0;

        dut->op = static_cast<uint8_t>(op);

        if (op == 3) {
            // ---- FDIV: fixed 12-cycle start-gated SRT ----
            // Present each input with a one-cycle `start` pulse. The SRT
            // emits `done` 11 cycles after the start edge, at which point the
            // completed quotient is captured into the output register, so the
            // answer is stable on `ans` the following (12th) cycle. One test
            // completes every 12 cycles.
            for (uint64_t idx = 0; idx < per_op_count; idx++) {
                uint16_t a = static_cast<uint16_t>(idx / bound);
                uint16_t b = static_cast<uint16_t>(idx % bound);

                dut->a = a;
                dut->b = b;

                // accept edge with start high (operands latched)
                dut->clk = 0;
                dut->eval();
                dut->start = 1;
                dut->clk = 1;
                dut->eval();
                dut->start = 0;

                // 11 more edges: `done` pulses at the 10th, the quotient is
                // captured at the 11th, so `ans` is stable on the next cycle.
                for (int i = 0; i < 11; i++) {
                    dut->clk = 0;
                    dut->eval();
                    dut->clk = 1;
                    dut->eval();
                }
                dut->clk = 0;
                dut->eval();

                do_check(op, a, b, dut->ans);
                op_tested++;
            }
        } else {
            // ---- FADD/FSUB/FMUL: single-cycle streaming ----
            uint16_t hist_A[1] = {0};
            uint16_t hist_B[1] = {0};

            for (uint32_t a_idx = 0; a_idx < bound; a_idx++) {
                for (uint32_t b_idx = 0; b_idx < bound; b_idx++) {
                    uint16_t a = static_cast<uint16_t>(a_idx);
                    uint16_t b = static_cast<uint16_t>(b_idx);

                    dut->a = a;
                    dut->b = b;

                    // Evaluate combinational output before the posedge
                    dut->clk = 0;
                    dut->eval();

                    // CHECK: the result on the bus corresponds to the input
                    // that entered the 1-stage datapath 1 cycle ago (hist[0]).
                    if (op_tested >= 1) {
                        do_check(op, hist_A[0], hist_B[0], dut->ans);
                    }
                    op_tested++;

                    // Advance pipeline
                    dut->clk = 1;
                    dut->eval();

                    hist_A[0] = a;
                    hist_B[0] = b;
                }
            }

            // Flush the final input left in the single-cycle datapath
            dut->clk = 0;
            dut->eval();
            do_check(op, hist_A[0], hist_B[0], dut->ans);
            op_tested++;
            dut->clk = 1;
            dut->eval();
        }

        std::cout << "Op " << op_names[op] << ": done (" << per_op_count << " inputs checked)" << std::endl;
    }

    // Print summary
    std::cout << "\n\n===========================================" << std::endl;
    std::cout << "           FINAL FPU COMBINED HARDWARE SUMMARY" << std::endl;
    std::cout << "===========================================\n" << std::endl;

    std::cout << "Total Combinations Tested : " << total_tests << std::endl;
    std::cout << "Total Global Failures     : " << total_failed << "\n" << std::endl;

    std::cout << "--- Breakdown by Classification ---" << std::endl;
    for (int i = 0; i < 5; i++) {
        double fail_pct = 0.0;
        if (cat_totals[i] > 0) {
            fail_pct = (static_cast<double>(cat_fails[i]) / cat_totals[i]) * 100.0;
        }
        std::cout << cat_names[i] << " : "
                  << cat_fails[i] << " failed / "
                  << cat_totals[i] << " total ("
                  << std::fixed << std::setprecision(2) << fail_pct << "%)" << std::endl;
    }

    std::cout << "\n===========================================" << std::endl;
    if (total_failed == 0) {
        std::cout << "STATUS: VERIFIED. RTL pipeline exactly matches C++ native floating point execution!" << std::endl;
    } else {
        std::cout << "STATUS: FAILED. Check 'testing_results/fpu_failed_log.txt' for details." << std::endl;
    }

    fail_outfile.close();
    delete dut;
    return total_failed == 0 ? 0 : 1;
}
