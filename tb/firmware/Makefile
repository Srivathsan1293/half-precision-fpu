# tb/firmware/Makefile
#
# Builds the bare-metal RV32I firmware image for the PicoRV32 SoC simulation.
#
# Toolchain selection (auto-detected):
#   - clang + ld.lld + llvm-objcopy (riscv32-unknown-elf target) when the GNU
#     RISC-V toolchain is not installed.
#   - riscv64-unknown-elf-gcc when it is available.
#
# Targets:
#   make            build firmware.hex (baseline integer smoke test)
#   make FPU_TEST=1 build firmware.hex (FPU PCPI test, needs the wrapper)
#   make clean
#
# Outputs:
#   firmware.elf / firmware.bin / firmware.hex (loaded by tb/soc_fpu_top.sv)

GCC := $(shell command -v riscv64-unknown-elf-gcc 2>/dev/null)

ifeq ($(strip $(GCC)),)
  CC       := clang --target=riscv32-unknown-elf
  LD       := ld.lld -m elf32lriscv
  OBJCOPY  := llvm-objcopy
  TOOLCHAIN := clang/lld
else
  CC       := $(GCC)
  LD       := $(GCC)
  OBJCOPY  := riscv64-unknown-elf-objcopy
  TOOLCHAIN := riscv64-unknown-elf-gcc
endif

MARCH  := rv32i
MABI   := ilp32

# Bench modes compile the software soft-float phase as rv32im (hardware MUL),
# so the SW-vs-HW comparison uses a generic real-world soft-float baseline.
ifneq ($(filter bench benchmm benchdig benchdiv benchai,$(FPU_TEST)),)
  MARCH := rv32im
endif

ifeq ($(FPU_TEST),stress)
  SRCS   := fpu_stress_main.c
else ifeq ($(FPU_TEST),bench)
  # Week 2 SW-vs-HW cycle-count benchmark: identical FIR run through the
  # software soft-float reference and the hardware FPU PCPI instructions.
  SRCS   := fpu_bench_main.c
else ifeq ($(FPU_TEST),benchmm)
  # Week 2 SW-vs-HW cycle-count benchmark: 4x4 fp16 matrix multiply.
  SRCS   := fpu_bench_mm_main.c
else ifeq ($(FPU_TEST),benchdig)
  # Week 2 SW-vs-HW cycle-count benchmark: 5-tap fp16 digital FIR filter.
  SRCS   := fpu_bench_dig_main.c
else ifeq ($(FPU_TEST),benchdiv)
  # Week 2 SW-vs-HW cycle-count benchmark: fp16 vector divide (software
  # division is the FPU's strongest, most honest case).
  SRCS   := fpu_bench_div_main.c
else ifeq ($(FPU_TEST),benchai)
  # Week 3 SW-vs-HW cycle-count benchmark: Micro AI Dense Layer + softmax
  # normalization (Y = W*X + B, then Y/sum(Y)), exercising FADD, FMUL, FDIV.
  SRCS   := fpu_bench_ai_layer_main.c
else ifeq ($(FPU_TEST),spike)
  SRCS   := fpu_spike_main.S
else ifeq ($(FPU_TEST),emu)
  # emulator self-test: software FPU emulator (IRQ handler + C core) driven by
  # a hand-written Zhinx assembly test that exercises only emulated ops.
  SRCS   := fpu_emutest_main.S fpu_irq_stub.S fpu_emulator.c
  MARCH  := rv32im_zhinx
else ifeq ($(FPU_TEST),zhinx)
  # standard-Zhinx integration: clang -march=rv32im_zhinx real-encoding test;
  # ops 0..3 run in hardware, ops 4..16 trap to the software emulator.
  SRCS   := fpu_zhinx_main.c fpu_irq_stub.S fpu_emulator.c
  MARCH  := rv32im_zhinx
else ifeq ($(FPU_TEST),run)
  # Phase 7 generic run-and-dump mode: build the user's own program (assembly
  # or C) alongside the IRQ stub + emulator so emulated FP ops also work. The
  # caller supplies RUN_SRCS (e.g. "myprog.S fpu_irq_stub.S fpu_emulator.c").
  SRCS   := $(RUN_SRCS)
  MARCH  := rv32im_zhinx
else ifneq ($(FPU_TEST),)
  SRCS   := fpu_test_main.c
else
  SRCS   := test_main.c
endif

CFLAGS  := -march=$(MARCH) -mabi=$(MABI) -nostdlib -ffreestanding -fno-builtin \
           -O2 -Wall -Wextra -I. -fno-unroll-loops

# The AI-layer bench links no IRQ/emulator but needs all three soft-float
# routines, so it uses link_ai.ld with the main-code budget relaxed to 0x1000.
ifeq ($(FPU_TEST),benchai)
  LINK_SCRIPT := link_ai.ld
else
  LINK_SCRIPT := link.ld
endif
LDFLAGS := -T $(LINK_SCRIPT) -nostdlib

# The emulator's code lives in the .irq_handler section (0x800..0x1000, beside
# the IRQ stub) so it never consumes the 0x0..0x800 main-code window that the
# linker script caps at 0x800. Built with -Oz so stub + emulator stay under the
# 2048-byte IRQ window, then .text is renamed to .irq_handler so the linker
# places it there.
EMU_CFLAGS := -Oz

OBJS   := $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(SRCS)))
EMU_OBJ := fpu_emulator.o
ELF    := firmware.elf
BIN    := firmware.bin
HEX    := firmware.hex

.PHONY: all clean

all: $(HEX)

$(HEX): $(BIN) bin2hex.py
	python3 bin2hex.py $(BIN) $(HEX)

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

# Shared headers: force a rebuild of any object that includes them so the
# firmware vector table can never drift from the harness copy.
FPU_HEADERS := fpu_vectors.h fpu_macros.h fpu_bench.h soft_half.h

%.o: %.c $(FPU_HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Emulator object: compile with -Oz for size and put its code in .irq_handler.
$(EMU_OBJ): fpu_emulator.c
	$(CC) $(CFLAGS) $(EMU_CFLAGS) -c $< -o $@
	$(OBJCOPY) --rename-section .text=.irq_handler $@ $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(ELF) $(BIN) $(HEX)
