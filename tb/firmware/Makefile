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

CFLAGS  := -march=$(MARCH) -mabi=$(MABI) -nostdlib -ffreestanding -fno-builtin \
           -O2 -Wall -Wextra -I.
LDFLAGS := -T link.ld -nostdlib

ifneq ($(FPU_TEST),)
  SRCS   := fpu_test_main.c
  CFLAGS += -DFPU_TEST
else
  SRCS   := test_main.c
endif

OBJS   := $(SRCS:.c=.o)
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

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(ELF) $(BIN) $(HEX)
