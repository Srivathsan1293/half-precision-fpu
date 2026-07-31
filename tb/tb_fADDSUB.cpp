#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <sys/stat.h>
#include <verilated.h>
#include "Vfpu_test.h"

enum Category {
    CAT_NAN = 0,
    CAT_INF = 1,
    CAT_ZERO = 2,
    CAT_SUBNORM = 3,
    CAT_NORMAL = 4,
    CAT_COUNT = 5
};

const char* cat_filenames[] = {
    "testing_results/faddsub_fail_nan.txt",
    "testing_results/faddsub_fail_inf.txt",
    "testing_results/faddsub_fail_zero.txt",
    "testing_results/faddsub_fail_subnormal.txt",
    "testing_results/faddsub_fail_normal.txt"
};

const char* cat_names[] = {
    "Involves NaN              ",
    "Involves Infinity         ",
    "Involves True Zero        ",
    "Involves Subnormal(s)     ",
    "Normal Arithmetic (Normal)"
};

// Golden Model: Matches addsub SystemVerilog specification
uint16_t compute_golden_cpp_native(uint16_t a, uint16_t b, uint8_t sub) {
    uint16_t expA = (a >> 10) & 0x1F;
    uint16_t expB = (b >> 10) & 0x1F;
    uint16_t manA = a & 0x03FF;
    uint16_t manB = b & 0x03FF;

    uint16_t signA = (a >> 15) & 1;
    // Hardware flips B's sign if subtracting
    uint16_t signB = ((b >> 15) & 1) ^ sub;

    bool a_nan  = (expA == 31 && manA != 0);
    bool b_nan  = (expB == 31 && manB != 0);
    bool a_zero = (expA == 0 && manA == 0);
    bool b_zero = (expB == 0 && manB == 0);
    bool a_inf  = (expA == 31 && manA == 0);
    bool b_inf  = (expB == 31 && manB == 0);

    bool subtract_op = signA ^ signB;

    // RTL hardware overrides (fpu_FADDSUB.sv)
    if (a_nan || b_nan) {
        return 0x7E00; // {1'b0, 5'b11111, 10'b1000000000}
    }
    if (a_inf && b_inf && subtract_op) {
        return 0x7E00; // Inf - Inf = NaN
    }
    if (a_inf) {
        return (signA << 15) | 0x7C00;
    }
    if (b_inf) {
        return (signB << 15) | 0x7C00;
    }
    if (a_zero && b_zero) {
        return ((signA & signB) << 15) | 0x0000;
    }

    // Use native IEEE FP16 addition mapping effective sign to operand B
    _Float16 float_A, float_B, float_ans;

    uint16_t effective_b = (b & 0x7FFF) | (signB << 15);

    std::memcpy(&float_A, &a, sizeof(uint16_t));
    std::memcpy(&float_B, &effective_b, sizeof(uint16_t));

    float_ans = float_A + float_B;

    uint16_t golden_bits;
    std::memcpy(&golden_bits, &float_ans, sizeof(uint16_t));
    return golden_bits;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_test* dut = new Vfpu_test;

    // Create results directory securely
    #if defined(_WIN32)
    _mkdir("testing_results");
    #else
    mkdir("testing_results", 0777);
    #endif

    std::ofstream log_files[CAT_COUNT];
    for (int i = 0; i < CAT_COUNT; ++i) {
        log_files[i].open(cat_filenames[i]);
        if (!log_files[i].is_open()) {
            std::cerr << "Error: Could not open output file " << cat_filenames[i] << std::endl;
            delete dut;
            return 1;
        }
        log_files[i] << "Category: " << cat_names[i] << "\n";
        log_files[i] << "Format: SUB | A | B | Expected | Got | Delta (Expected - Got)\n";
        log_files[i] << "-------------------------------------------------------------------\n";
    }

    // 2^16 * 2^16 * 2 = 8,589,934,592 combinations
    uint64_t total_combinations = 8589934592ULL;
    uint64_t total_tests = 0;
    uint64_t total_failed = 0;

    uint64_t cat_totals[CAT_COUNT] = {0};
    uint64_t cat_fails[CAT_COUNT]  = {0};

    uint64_t update_interval = total_combinations / 200; // Update every 0.5%
    int bar_width = 50;

    std::cout << "--- Starting ADDSUB Hardware Test Pipeline ---" << std::endl;
    std::cout << "Testing 8,589,934,592 combinations. This will take roughly 3-5 minutes...\n" << std::endl;

    for (uint32_t op = 0; op <= 1; op++) {
        for (uint32_t i = 0; i <= 0xFFFF; i++) {
            for (uint32_t j = 0; j <= 0xFFFF; j++) {
                total_tests++;

                // Progress Indicator
                if (total_tests % update_interval == 0 || total_tests == total_combinations) {
                    float progress = static_cast<float>(total_tests) / total_combinations;
                    int pos = static_cast<int>(bar_width * progress);

                    std::cout << "\r[";
                    for (int k = 0; k < bar_width; ++k) {
                        if (k < pos) std::cout << "=";
                        else if (k == pos) std::cout << ">";
                        else std::cout << " ";
                    }
                    std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << " %" << std::flush;
                }

                uint16_t a = static_cast<uint16_t>(i);
                uint16_t b = static_cast<uint16_t>(j);
                uint8_t sub = static_cast<uint8_t>(op);

                // Categorize inputs
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
                if (a_nan || b_nan)         current_cat = CAT_NAN;
                else if (a_inf || b_inf)    current_cat = CAT_INF;
                else if (a_zero || b_zero)  current_cat = CAT_ZERO;
                else if (a_sub || b_sub)    current_cat = CAT_SUBNORM;
                else                        current_cat = CAT_NORMAL;

                cat_totals[current_cat]++;

                // Clock pulse execution for 1-cycle RTL pipeline latency
                dut->a = a;
                dut->b = b;
                dut->sub = sub;

                dut->clk = 0;
                dut->eval();
                dut->clk = 1;
                dut->eval();

                uint16_t expected_ans = compute_golden_cpp_native(a, b, sub);
                uint16_t hw_ans = dut->ans;

                if (hw_ans != expected_ans) {
                    total_failed++;
                    cat_fails[current_cat]++;

                    int32_t delta = static_cast<int32_t>(expected_ans) - static_cast<int32_t>(hw_ans);

                    log_files[current_cat]
                    << "SUB=" << (int)sub
                    << " | a=0x" << std::hex << std::setfill('0') << std::setw(4) << a
                    << " | b=0x" << std::setw(4) << b
                    << " | Expected=0x" << std::setw(4) << expected_ans
                    << " | Got=0x" << std::setw(4) << hw_ans
                    << " | Delta=" << std::dec << delta << "\n";
                }
            }
        }
    }

    std::cout << "\n\n===========================================" << std::endl;
    std::cout << "         FINAL ADDSUB HARDWARE SUMMARY       " << std::endl;
    std::cout << "===========================================\n" << std::endl;

    for (int i = 0; i < CAT_COUNT; i++) {
        log_files[i].close();
        double fail_pct = (cat_totals[i] > 0) ? ((double)cat_fails[i] / cat_totals[i]) * 100.0 : 0.0;
        std::cout << cat_names[i] << " : "
        << cat_fails[i] << " failed / " << cat_totals[i] << " total ("
        << std::fixed << std::setprecision(2) << fail_pct << "%)\n";
    }

    delete dut;
    return (total_failed == 0) ? 0 : 1;
}
