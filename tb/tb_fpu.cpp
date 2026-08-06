#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>
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

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_test* dut = new Vfpu_test;

    std::ofstream fail_outfile("testing_results/fpu_failed_log.txt");
    if (!fail_outfile.is_open()) {
        std::cerr << "Error: Could not open output file." << std::endl;
        delete dut;
        return 1;
    }

    uint64_t total_combinations = (uint64_t)0xFFFF + 1;
    uint64_t combinations_per_op = total_combinations * (uint64_t)0xFFFF + 1;
    
    std::cout << "--- Starting Exact RTL FPU Combined Hardware Emulation Test ---" << std::endl;
    std::cout << "Testing all operations: ADD, SUB, MUL, DIV" << std::endl;
    std::cout << "Combinations per operation: " << combinations_per_op << std::endl;

    uint64_t total_tests = 0;
    uint64_t total_failed = 0;

    // Tracking Arrays for Summary [NaN, Inf, Zero, Subnorm, Normal]
    uint64_t cat_totals[5] = {0, 0, 0, 0, 0};
    uint64_t cat_fails[5]  = {0, 0, 0, 0, 0};

    uint64_t update_interval = combinations_per_op / 100;
    int bar_width = 50;

    for (uint32_t a_idx = 0; a_idx <= 0xFFFF; a_idx++) {
        for (uint32_t b_idx = 0; b_idx <= 0xFFFF; b_idx++) {
            uint16_t a = static_cast<uint16_t>(a_idx);
            uint16_t b = static_cast<uint16_t>(b_idx);

            // Classify inputs
            uint16_t expA = (a >> 10) & 0x1F;
            uint16_t manA = a & 0x03FF;
            uint16_t expB = (b >> 10) & 0x1F;
            uint16_t manB = b & 0x03FF;

            bool a_nan  = (expA == 31 && manA != 0);
            bool b_nan  = (expB == 31 && manB != 0);
            bool a_zero = (expA == 0 && manA == 0);
            bool b_zero = (expB == 0 && manB == 0);
            bool a_inf  = (expA == 31 && manA == 0);
            bool b_inf  = (expB == 31 && manB == 0);

            Category current_cat;
            if (a_nan || b_nan) {
                current_cat = CAT_NAN;
            } else if (a_inf || b_inf) {
                current_cat = CAT_INF;
            } else if (a_zero || b_zero) {
                current_cat = CAT_ZERO;
            } else if ((expA == 0 && manA != 0) || (expB == 0 && manB != 0)) {
                current_cat = CAT_SUBNORM;
            } else {
                current_cat = CAT_NORMAL;
            }

            cat_totals[current_cat]++;

            // Test all 4 operations
            for (int op_idx = 0; op_idx < 4; op_idx++) {
                total_tests++;
                
                uint16_t expected_ans;
                if (op_idx == 0) {    // ADD
                    dut->a = a; dut->b = b; dut->op = static_cast<uint8_t>(op_idx);
                    expected_ans = compute_golden_add(a, b);
                } else if (op_idx == 1) {  // SUB
                    dut->a = a; dut->b = b; dut->op = static_cast<uint8_t>(op_idx);
                    expected_ans = compute_golden_sub(a, b);
                } else if (op_idx == 2) { // MUL
                    dut->a = a; dut->b = b; dut->op = static_cast<uint8_t>(op_idx);
                    expected_ans = compute_golden_mul(a, b);
                } else {    // DIV
                    dut->a = a; dut->b = b; dut->op = static_cast<uint8_t>(op_idx);
                    expected_ans = compute_golden_div(a, b);
                }

                dut->clk = 0; dut->eval(); dut->clk = 1; dut->eval();
                dut->clk = 0; dut->eval(); dut->clk = 1; dut->eval();

                uint16_t hw_ans = dut->ans;

                // NaN sign/payload is unspecified by IEEE 754 - accept any NaN.
                bool both_nan = is_nan16(expected_ans) && is_nan16(hw_ans);
                if (!both_nan && hw_ans != expected_ans) {
                    total_failed++;
                    cat_fails[current_cat]++;

                    fail_outfile << "FAIL [" << current_cat << "] OP=" << op_idx 
                                 << " a=0x" << std::hex << a
                                 << " | b=0x" << b
                                 << " | Expected=0x" << expected_ans
                                 << " | Got=0x" << hw_ans << std::dec << "\n";
                }

                // Progress bar (sampled every 10k tests)
                if ((total_tests % 10000) == 0 || total_tests == combinations_per_op * 4) {
                    float progress = static_cast<float>(total_tests) / combinations_per_op;
                    int pos = std::min(static_cast<int>(bar_width * progress), bar_width - 1);
                    for (int k = 0; k < bar_width; ++k) {
                        if (k <= pos) std::cout << "="; else std::cout << " ";
                    }
                    std::cout << "\r" << static_cast<int>(progress * 100.0) << "% " << total_tests 
                                 << "/" << combinations_per_op << std::flush;
                }
            }
        }
    }

    // Print summary
    std::cout << "\n\n===========================================" << std::endl;
    std::cout << "           FINAL FPU COMBINED HARDWARE SUMMARY" << std::endl;
    std::cout << "===========================================\n" << std::endl;
    
    uint64_t ops_tested = total_combinations * 4;
    std::cout << "Total Combinations Tested : " << ops_tested << std::endl;
    std::cout << "Total Global Failures     : " << total_failed << "\n" << std::endl;

    std::cout << "--- Breakdown by Classification ---" << std::endl;
    const char* cat_names[] = {
        "Involves NaN              ",
        "Involves Infinity         ",
        "Involves True Zero        ",
        "Involves Subnormal(s)     ",
        "Normal Arithmetic (Normal)"
    };

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
