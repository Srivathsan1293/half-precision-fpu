#include <iostream>
#include <verilated.h>
#include "Vfpu_test.h" // Updated to match module DIV
#include <fstream>
#include <string>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cmath>

// Golden Reference: True IEEE-754 Native C++ Engine
uint16_t compute_golden_cpp_native(uint16_t a, uint16_t b) {
    _Float16 float_A, float_B, float_ans;

    // Safely copy the 16-bit integers into the native C++ FP16 types
    std::memcpy(&float_A, &a, sizeof(uint16_t));
    std::memcpy(&float_B, &b, sizeof(uint16_t));

    // Let the C++ compiler's FPU perform a mathematically perfect IEEE-754 division
    float_ans = float_A / float_B;

    // Extract the raw bits from the C++ answer
    uint16_t golden_bits;
    std::memcpy(&golden_bits, &float_ans, sizeof(uint16_t));

    // --- Hardware Alignment Overrides ---
    // C++ and SystemVerilog might generate slightly different NaN payloads (the bits below the exponent).
    // We force the C++ model to match your specific hardware NaN/Inf payloads to prevent false failures.
    uint16_t signA = (a >> 15) & 1;
    uint16_t signB = (b >> 15) & 1;
    uint16_t final_sign = signA ^ signB;

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

    // Override NaNs
    if (a_nan || b_nan || (a_zero && b_zero) || (a_inf && b_inf)) {
        return (final_sign << 15) | 0x7E00;
    }
    // Override Infs
    if (a_inf || b_zero) {
        return (final_sign << 15) | 0x7C00;
    }

    return golden_bits;
}

// Enum for classifying test cases
enum Category {
    CAT_NAN = 0,
    CAT_INF = 1,
    CAT_ZERO = 2,
    CAT_SUBNORM = 3,
    CAT_NORMAL = 4
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_test* dut = new Vfpu_test; // Updated object name

    std::ofstream fail_outfile("testing_results/fdiv_failed_log.txt");

    if (!fail_outfile.is_open()) {
        std::cerr << "Error: Could not open output file." << std::endl;
        delete dut;
        dut = NULL;
        return 1;
    }

    uint64_t total_combinations = 4294967296ULL;
    std::cout << "--- Starting Exact RTL Hardware Emulation Test ---" << std::endl;
    std::cout << "Testing exactly 4,294,967,296 combinations..." << std::endl;

    uint64_t total_tests = 0;
    uint64_t total_failed = 0;

    // Tracking Arrays for Summary [NaN, Inf, Zero, Subnorm, Normal]
    uint64_t cat_totals[5] = {0, 0, 0, 0, 0};
    uint64_t cat_fails[5]  = {0, 0, 0, 0, 0};

    uint64_t update_interval = total_combinations / 100;
    int bar_width = 50;

    for (uint32_t i = 0; i <= 0xFFFF; i++) {
        for (uint32_t j = 0; j <= 0xFFFF; j++) {
            total_tests++;

            // Progress Bar Render
            if (total_tests % update_interval == 0 || total_tests == total_combinations) {
                float progress = static_cast<float>(total_tests) / total_combinations;
                int pos = static_cast<int>(bar_width * progress);

                std::cout << "\r[";
                for (int k = 0; k < bar_width; ++k) {
                    if (k < pos) std::cout << "=";
                    else if (k == pos) std::cout << ">";
                    else std::cout << " ";
                }
                std::cout << "] " << static_cast<int>(progress * 100.0) << " %";
                std::cout.flush();
            }

            uint16_t a = static_cast<uint16_t>(i);
            uint16_t b = static_cast<uint16_t>(j);

            // --- CLASSIFY THE INPUTS ---
            uint16_t expA = (a >> 10) & 0x1F;
            uint16_t manA = a & 0x03FF;
            uint16_t expB = (b >> 10) & 0x1F;
            uint16_t manB = b & 0x03FF;

            bool a_zero = (expA == 0 && manA == 0);
            bool b_zero = (expB == 0 && manB == 0);
            bool a_sub  = (expA == 0 && manA != 0);
            bool b_sub  = (expB == 0 && manB != 0);
            bool a_inf  = (expA == 31 && manA == 0);
            bool b_inf  = (expB == 31 && manB == 0);
            bool a_nan  = (expA == 31 && manA != 0);
            bool b_nan  = (expB == 31 && manB != 0);

            Category current_cat;
            if (a_nan || b_nan) {
                current_cat = CAT_NAN;
            } else if (a_inf || b_inf) {
                current_cat = CAT_INF;
            } else if (a_zero || b_zero) {
                current_cat = CAT_ZERO;
            } else if (a_sub || b_sub) {
                current_cat = CAT_SUBNORM;
            } else {
                current_cat = CAT_NORMAL;
            }

            cat_totals[current_cat]++;

            // --- EVALUATE HARDWARE ---
            dut->a = a;
            dut->b = b;
            dut->eval();

            uint16_t expected_ans = compute_golden_cpp_native(a, b);

            // Updated to 'out' matching your module port definition
            uint16_t hw_ans = dut->ans;

            if (hw_ans != expected_ans) {
                total_failed++;
                cat_fails[current_cat]++;

                fail_outfile << "FAIL [" << current_cat << "]:"
                << " a=0x" << std::hex << std::setfill('0') << std::setw(4) << a
                << " | b=0x" << std::setw(4) << b
                << " | Expected=0x" << std::setw(4) << expected_ans
                << " | Got=0x" << std::setw(4) << hw_ans << "\n";
            }
        }
    }

    // --- PRINT FINAL DETAILED SUMMARY ---
    std::cout << std::endl << "\n===========================================" << std::endl;
    std::cout << "           FINAL FDIV HARDWARE SUMMARY       " << std::endl;
    std::cout << "===========================================\n" << std::endl;

    std::cout << "Total Combinations Tested : " << std::dec << total_tests << std::endl;
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
        << cat_totals[i] << " total "
        << std::fixed << std::setprecision(2) << "(" << fail_pct << "%)" << std::endl;
    }

    std::cout << "\n===========================================" << std::endl;

    if (total_failed == 0) {
        std::cout << "STATUS: VERIFIED. Your RTL pipeline exactly matches C++ native floating point execution!" << std::endl;
    } else {
        std::cout << "STATUS: FAILED. Check 'testing_results/fdiv_failed_log.txt' for details." << std::endl;
    }

    fail_outfile.close();
    delete dut;
    dut = NULL;

    return total_failed == 0 ? 0 : 1;
}
