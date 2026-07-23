#include <iostream>
#include <verilated.h>
#include "Vfpu_test.h"
#include <fstream>
#include <string>
#include <iomanip>
#include <cstdint>

// Golden Reference: Full IEEE-754 FP16 Add/Sub (Normals, Subnormals, RNTE, Special Cases)
uint16_t compute_golden_ieee(uint16_t a, uint16_t b, bool sub) {
    uint16_t signA = (a >> 15) & 1;
    uint16_t signB = (b >> 15) & 1;

    if (sub) signB = !signB;

    int32_t expA   = (a >> 10) & 0x1F;
    int32_t expB   = (b >> 10) & 0x1F;
    uint32_t manA  = a & 0x03FF;
    uint32_t manB  = b & 0x03FF;

    bool a_zero = (expA == 0 && manA == 0);
    bool a_inf  = (expA == 31 && manA == 0);
    bool a_nan  = (expA == 31 && manA != 0);
    bool b_zero = (expB == 0 && manB == 0);
    bool b_inf  = (expB == 31 && manB == 0);
    bool b_nan  = (expB == 31 && manB != 0);

    if (a_nan || b_nan) return 0x7E00;

    if (a_inf && b_inf) {
        if (signA != signB) return 0x7E00;
        return (signA << 15) | 0x7C00;
    }
    if (a_inf) return (signA << 15) | 0x7C00;
    if (b_inf) return (signB << 15) | 0x7C00;

    if (a_zero && b_zero) {
        if (signA == signB) return (signA << 15) | 0x0000;
        return 0x0000;
    }

    if (a_zero) return (signB << 15) | (expB << 10) | manB;
    if (b_zero) return (signA << 15) | (expA << 10) | manA;

    int32_t trueExpA = (expA == 0) ? -14 : expA - 15;
    int32_t trueExpB = (expB == 0) ? -14 : expB - 15;

    uint32_t normManA = (expA == 0) ? manA : (manA | 0x0400);
    uint32_t normManB = (expB == 0) ? manB : (manB | 0x0400);

    uint32_t m_A = normManA << 3;
    uint32_t m_B = normManB << 3;

    int32_t diff = trueExpA - trueExpB;
    uint32_t final_sign, m_larger, m_smaller;
    int32_t final_exp;

    if (trueExpA > trueExpB || (trueExpA == trueExpB && normManA >= normManB)) {
        final_sign = signA;
        final_exp = trueExpA;
        m_larger = m_A;

        if (diff >= 25) {
            m_smaller = (m_B != 0) ? 1 : 0;
        } else {
            uint32_t mask = (1 << diff) - 1;
            uint32_t sticky = (m_B & mask) ? 1 : 0;
            m_smaller = (m_B >> diff) | sticky;
        }
    } else {
        final_sign = signB;
        final_exp = trueExpB;
        m_larger = m_B;
        diff = -diff;

        if (diff >= 25) {
            m_smaller = (m_A != 0) ? 1 : 0;
        } else {
            uint32_t mask = (1 << diff) - 1;
            uint32_t sticky = (m_A & mask) ? 1 : 0;
            m_smaller = (m_A >> diff) | sticky;
        }
    }

    uint32_t alu_out;
    bool eff_sub = (signA != signB);

    if (eff_sub) {
        alu_out = m_larger - m_smaller;
        if (alu_out == 0) return 0x0000;
    } else {
        alu_out = m_larger + m_smaller;
    }

    if (alu_out & 0x4000) {
        uint32_t sticky = alu_out & 1;
        alu_out >>= 1;
        alu_out |= sticky;
        final_exp++;
    } else {
        while ((alu_out & 0x2000) == 0 && alu_out != 0) {
            alu_out <<= 1;
            final_exp--;
        }
    }

    if (final_exp < -14) {
        int shift = -14 - final_exp;
        if (shift >= 25) {
            alu_out = (alu_out != 0) ? 1 : 0;
        } else {
            uint32_t mask = (1 << shift) - 1;
            uint32_t sticky = (alu_out & mask) ? 1 : 0;
            alu_out >>= shift;
            alu_out |= sticky;
        }
        final_exp = -14;
    }

    bool G   = (alu_out & 4) != 0;
    bool R   = (alu_out & 2) != 0;
    bool S   = (alu_out & 1) != 0;
    bool LSB = (alu_out & 8) != 0;

    uint32_t final_mantissa = alu_out >> 3;

    if (G && (R || S || LSB)) {
        final_mantissa += 1;
        if (final_mantissa & 0x0800) {
            final_mantissa >>= 1;
            final_exp += 1;
        }
    }

    uint32_t out_exp;
    if (final_mantissa < 0x0400) {
        out_exp = 0;
    } else {
        out_exp = final_exp + 15;
    }

    if (out_exp >= 31) {
        return (final_sign << 15) | 0x7C00;
    }

    return (final_sign << 15) | ((out_exp & 0x1F) << 10) | (final_mantissa & 0x03FF);
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
    Vfpu_test* dut = new Vfpu_test;

    std::ofstream fail_outfile("testing_results/fpu_addsub_failed_log.txt");

    if (!fail_outfile.is_open()) {
        std::cerr << "Error: Could not open output file." << std::endl;
        delete dut;
        dut = NULL;
        return 1;
    }

    uint64_t total_combinations = 8589934592ULL;
    std::cout << "--- Starting Full IEEE-754 FP16 Add/Sub Test ---" << std::endl;
    std::cout << "Testing exactly 8,589,934,592 combinations..." << std::endl;

    uint64_t total_tests = 0;
    uint64_t total_failed = 0;

    // Tracking Arrays for Summary [NaN, Inf, Zero, Subnorm, Normal]
    uint64_t cat_totals[5] = {0, 0, 0, 0, 0};
    uint64_t cat_fails[5]  = {0, 0, 0, 0, 0};

    uint64_t update_interval = total_combinations / 100;
    int bar_width = 50;

    for (int sub_op = 0; sub_op < 2; sub_op++) {
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
                dut->sub = sub_op;
                dut->eval();

                uint16_t expected_ans = compute_golden_ieee(a, b, sub_op);
                uint16_t hw_ans = dut->ans;

                if (hw_ans != expected_ans) {
                    total_failed++;
                    cat_fails[current_cat]++;

                    // Unfiltered writing: dumps every single failure directly to the text file
                    fail_outfile << "FAIL [" << current_cat << "]: sub=" << sub_op
                    << " | a=0x" << std::hex << std::setfill('0') << std::setw(4) << a
                    << " | b=0x" << std::setw(4) << b
                    << " | Expected=0x" << std::setw(4) << expected_ans
                    << " | Got=0x" << std::setw(4) << hw_ans << "\n";
                }
            }
        }
    }

    // --- PRINT FINAL DETAILED SUMMARY ---
    std::cout << std::endl << "\n===========================================" << std::endl;
    std::cout << "           FINAL IEEE-754 SUMMARY            " << std::endl;
    std::cout << "===========================================\n" << std::endl;

    std::cout << "Total Combinations Tested : " << total_tests << std::endl;
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
        std::cout << "STATUS: VERIFIED. Your FADDSUB is IEEE-754 compliant!" << std::endl;
    } else {
        std::cout << "STATUS: FAILED. Check 'testing_results/fpu_addsub_failed_log.txt' for details." << std::endl;
    }

    fail_outfile.close();
    delete dut;
    dut = NULL;

    return total_failed == 0 ? 0 : 1;
}
