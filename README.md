# fixed_point

Portable C library implementing 16.16 and 32.32 fixed-point arithmetic, targeting RISC-V microcontrollers without a hardware FPU.

## Why

Soft-float from libgcc works but costs ~12 KB of flash. On a bootloader budget that's too much. Fixed-point gets the same work done in ~2 KB and runs faster on cores without an FPU extension.

## Formats

| Type | Alias | Bits | Integer | Fraction | Range |
|------|-------|------|---------|----------|-------|
| `q16_t` | 16.16 | 32 | 16 | 16 | ±32767.99998 |
| `q32_t` | 32.32 | 64 | 32 | 32 | ±2.1e9 |

Both are signed. The underlying types are `int32_t` and `int64_t` respectively — no structs, no overhead.

## Operations

- Addition, subtraction
- Multiplication (with 64/128-bit intermediate to avoid precision loss)
- Division (pre-scaled numerator)
- Square root (Newton-Raphson iteration)

All operations that lose bits (mul, div, sqrt) accept a `rounding_t` argument:

```c
typedef enum { TRUNC = 0, ROUND_HALF_UP } rounding_t;
```

## Usage

```c
#include "fixed_point.h"

// convert
q16_t a = int_to_q16(3);       // 3.0 in Q16
q16_t b = 0x00008000;          // 0.5 in Q16 (raw)

// arithmetic
q16_t sum  = q16_add(a, b);                  // 3.5
q16_t prod = q16_mul(a, b, TRUNC);           // 1.5
q16_t quot = q16_div(a, int_to_q16(2), ROUND_HALF_UP);  // 1.5
q16_t root = q16_sqrt(int_to_q16(9), TRUNC); // 3.0

// back to int
int32_t result = q16_to_int(prod, TRUNC);    // 1
```

## Building

```
# native (development / testing)
make

# RISC-V cross-compile (needs riscv64-unknown-elf-gcc)
make riscv

# run Python ULP validation
make validate
```

Cross-compiler install:
```
# Ubuntu/Debian
sudo apt install gcc-riscv64-unknown-elf

# macOS
brew install riscv-gnu-toolchain
```

## Testing

`make` runs the C test suite natively. Each operation is tested against known values with `±1 ULP` tolerance where the result is irrational.

`make validate` compiles a small probe, runs it, and compares every output against Python's `decimal` module at 50-digit precision. This is the primary correctness check — the C tests just catch regressions fast.

## Known issues / TODO

- `q32_mul`: the `ah*bh * 2^32` term overflows for large operands. Needs saturation clamping. Hasn't been a problem in practice but it's not correct for the full Q32 range.
- `q16_div` with negative divisor and `ROUND_HALF_UP`: rounding direction may be off. Marked in source, needs a proper fix with test cases.
- Saturating add/sub variants (`q16_add_sat`) requested for motor control use case — not yet implemented.
- Q32 test coverage is thin. Currently only validated through `make validate`; the C test suite skips Q32 until the mul FIXME is resolved.

## License

MIT
