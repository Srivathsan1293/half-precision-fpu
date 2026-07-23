#include <iostream>
#include <verilated.h>
#include "Vfpu_test.h" // Update to match your Verilated header if needed
#include <fstream>
#include <string>
#include <iomanip>
#include <cstdint>

// Golden Reference: Full IEEE-754 FP16 (Normals, Subnormals, RNTE, Special Cases)
uint16_t compute_golden_ieee(uint16_t a, uint16_t b) {
    uint16_t signA = (a >> 15) & 1;
    uint16_t signB = (b >> 15) & 1;
    int32_t expA   = (a >> 10) & 0x1F;
    int32_t expB   = (b >> 10) & 0x1F;
    uint32_t manA  = a & 0x03FF;
    uint32_t manB  = b & 0x03FF;

    uint16_t signAns = signA ^ signB;

    // --- 1. CLASSIFY INPUTS ---
    bool a_zero = (expA == 0 && manA == 0);
    bool a_inf  = (expA == 31 && manA == 0);
    bool a_nan  = (expA == 31 && manA != 0);
    bool b_zero = (expB == 0 && manB == 0);
    bool b_inf  = (expB == 31 && manB == 0);
    bool b_nan  = (expB == 31 && manB != 0);

    // --- 2. SPECIAL CASE OVERRIDES ---
    if (a_nan || b_nan) return (signAns << 15) | 0x7E00; // Quiet NaN
    if ((a_zero && b_inf) || (a_inf && b_zero)) return (signAns << 15) | 0x7E00; // Invalid Op -> NaN
    if (a_inf || b_inf) return (signAns << 15) | 0x7C00; // Infinity
    if (a_zero || b_zero) return (signAns << 15) | 0x0000; // Zero

    // --- 3. LZD PRE-PROCESSING (Normalize Subnormals) ---
    int32_t trueExpA = (expA == 0) ? -14 : expA - 15;
    int32_t trueExpB = (expB == 0) ? -14 : expB - 15;
    uint32_t normManA = (expA == 0) ? manA : (manA | 0x0400);
    uint32_t normManB = (expB == 0) ? manB : (manB | 0x0400);

    while ((normManA & 0x0400) == 0 && normManA != 0) { normManA <<= 1; trueExpA--; }
    while ((normManB & 0x0400) == 0 && normManB != 0) { normManB <<= 1; trueExpB--; }

    // --- 4. MULTIPLIER DATAPATH ---
    uint32_t prod = normManA * normManB;
    int32_t expAns = trueExpA + trueExpB + 15;

    // Normalize product
    uint32_t mantissa_adj = 0;
    if (prod & 0x200000) { // MSB is bit 21
        mantissa_adj = prod;
        expAns += 1;
    } else {
        mantissa_adj = prod << 1; // Shift to bit 21
    }

    // --- 5. OUTPUT POST-PROCESSING (Denormalization Shift) ---
    if (expAns <= 0) {
        int32_t shift_amt = 1 - expAns;

        // Total Underflow Check
        if (shift_amt > 13) {
            return (signAns << 15) | 0x0000;
        }

        // Shift while preserving the sticky bit (bitwise OR of all dropped bits)
        uint32_t sticky = 0;
        for (int i = 0; i < shift_amt; i++) {
            sticky |= (mantissa_adj & 1);
            mantissa_adj >>= 1;
        }
        mantissa_adj |= sticky;
        expAns = 0; // Force exponent to subnormal format
    }

    // --- 6. RNTE ROUNDING LOGIC ---
    bool G   = (mantissa_adj & 0x0400) != 0;
    bool R   = (mantissa_adj & 0x0200) != 0;
    bool S   = (mantissa_adj & 0x01FF) != 0;
    bool LSB = (mantissa_adj & 0x0800) != 0;

    uint32_t right_mantissa = (mantissa_adj >> 11) & 0x03FF;

    if (G && (R || S || LSB)) {
        right_mantissa += 1;
        if (right_mantissa & 0x0400) {
            right_mantissa = 0;
            expAns += 1;
        }
    }

    // --- 7. FINAL OVERFLOW CHECK ---
    if (expAns >= 31) {
        return (signAns << 15) | 0x7C00;
    }

    return (signAns << 15) | ((expAns & 0x1F) << 10) | (right_mantissa & 0x03FF);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_test* dut = new Vfpu_test;

    std::ofstream outfile("testing_results/fpu_full_ieee_log.txt");
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open output file." << std::endl;
        return 1;
    }

    uint64_t total_combinations = 4294967296ULL; // 65536 * 65536
    std::cout << "--- Starting Full IEEE-754 FP16 Test ---" << std::endl;
    std::cout << "Testing exactly 4,294,967,296 combinations..." << std::endl;
    std::cout << "This tests Normals, Subnormals, RNTE, and Special Cases." << std::endl;

    uint64_t total_tests = 0;
    uint64_t failed_tests = 0;
    uint64_t print_interval = total_combinations / 20; // Print every 5%

    // Exhaustive loops covering the entire 16-bit spectrum
    for (uint32_t i = 0; i <= 0xFFFF; i++) {
        for (uint32_t j = 0; j <= 0xFFFF; j++) {
            total_tests++;

            if (total_tests % print_interval == 0) {
                std::cout << "Progress: " << (total_tests * 100) / total_combinations << "% completed..." << std::endl;
            }

            uint16_t a = static_cast<uint16_t>(i);
            uint16_t b = static_cast<uint16_t>(j);

            dut->a = a;
            dut->b = b;
            dut->eval();

            uint16_t expected_ans = compute_golden_ieee(a, b);
            uint16_t hw_ans = dut->ans;

            if (hw_ans != expected_ans) {
                failed_tests++;

                std::stringstream err_ss;
                err_ss << "FAIL: a=0x" << std::hex << std::setfill('0') << std::setw(4) << a
                << " | b=0x" << std::setw(4) << b
                << " | Expected=0x" << std::setw(4) << expected_ans
                << " | Got=0x" << std::setw(4) << hw_ans;

                if (failed_tests <= 100) {
                    std::cout << err_ss.str() << std::endl;
                }
                outfile << err_ss.str() << std::endl;
            }
        }
    }

    std::cout << "-------------------------------------------" << std::endl;
    if (failed_tests == 0) {
        std::cout << "SUCCESS: All " << total_tests << " vectors passed flawlessly! Your FPU is complete." << std::endl;
        outfile   << "SUCCESS: All 4,294,967,296 vectors passed flawlessly." << std::endl;
    } else {
        std::cout << "FAILURE: " << failed_tests << " / " << total_tests << " cases failed." << std::endl;
        std::cout << "Check the 'testing_results/fpu_full_ieee_log.txt' file for details." << std::endl;
    }

    outfile.close();
    delete dut;
    return failed_tests == 0 ? 0 : 1;
}
