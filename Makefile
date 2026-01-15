# Makefile for fixed_point library
#
# Targets:
#   make              -- build + run tests natively (for dev)
#   make riscv        -- cross-compile for RISC-V (needs riscv64 toolchain)
#   make validate     -- run Python ULP validation (needs Python 3 + decimal)
#   make clean
#
# Cross-compiler: assumes riscv64-unknown-elf-gcc on PATH.
# On Ubuntu: sudo apt install gcc-riscv64-unknown-elf
# On macOS:  brew install riscv-gnu-toolchain

CC      = gcc
RISCV_CC = riscv64-unknown-elf-gcc

CFLAGS  = -Wall -Wextra -std=c11 -O2
# -march and -mabi for a generic RV64 soft-float target (no FPU)
RISCV_CFLAGS = -march=rv64imac -mabi=lp64 -msoft-float \
               -Wall -Wextra -std=c11 -O2 -ffreestanding

SRC     = fixed_point.c
TEST    = test/test_fixed_point.c
OUT     = test_runner
RISCV_OUT = test_runner.elf

.PHONY: all riscv validate clean

all: $(OUT)
	./$(OUT)

$(OUT): $(SRC) $(TEST) fixed_point.h
	$(CC) $(CFLAGS) -I. $(SRC) $(TEST) -o $(OUT)

riscv: $(SRC) $(TEST) fixed_point.h
	$(RISCV_CC) $(RISCV_CFLAGS) -I. $(SRC) $(TEST) -o $(RISCV_OUT)
	@echo "Built $(RISCV_OUT) -- flash to target or run under spike/qemu"

validate:
	python3 test/validate.py

clean:
	rm -f $(OUT) $(RISCV_OUT)
