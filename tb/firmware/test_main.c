/* tb/firmware/test_main.c
 *
 * Baseline RV32I smoke test for the PicoRV32 SoC. Exercises instruction
 * fetch, ALU, the memory interface and the RAM without needing any
 * coprocessor. Results are stored to a fixed region and a done marker is
 * written so the Verilator harness can detect completion.
 */

#include <stdint.h>

#define RESULTS_BASE     0x100u
#define TEST_MAGIC_ADDR  0x1FCu
#define DONE_ADDR        0x200u
#define DONE_MAGIC       0xDEADBEEFu
#define TEST_MAGIC_BASELINE 0xBA51E000u

volatile uint32_t *const results = (volatile uint32_t *)RESULTS_BASE;

int main(void);

void _start(void) __attribute__((section(".text._start"), noreturn));
void _start(void) {
    (void)main();
    for (;;) ;
}

int main(void) {
    uint32_t a = 1, b = 2, c = 3, d = 4;

    results[0] = a + b;              /* 3  */
    results[1] = (a + b) * c;        /* 9  */
    results[2] = d * d - (a + b);    /* 13 */
    results[3] = results[0] ^ results[1] ^ results[2]; /* 7 */

    *(volatile uint32_t *)TEST_MAGIC_ADDR = TEST_MAGIC_BASELINE;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}
