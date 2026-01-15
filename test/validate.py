#!/usr/bin/env python3
"""
test/validate.py

Validates the C fixed-point library output against Python's decimal module.
Builds a small C driver that prints results to stdout, then compares them
to high-precision reference values computed here.

Requires: gcc on PATH, Python 3.4+
Usage: python3 test/validate.py  (or just: make validate)

The 1-ULP tolerance is relative to Q16 precision: 1 ULP = 1/65536 ~ 0.0000153
"""

import subprocess
import sys
import os
from decimal import Decimal, getcontext, ROUND_HALF_UP, ROUND_FLOOR

getcontext().prec = 50   # way more than we need, but cheap

SCALE_Q16 = Decimal(65536)   # 2^16

passed = 0
failed = 0


def q16_to_dec(raw: int) -> Decimal:
    """Convert a raw Q16 integer (as printed by C) to a Decimal."""
    # treat as signed 32-bit
    if raw >= 2**31:
        raw -= 2**32
    return Decimal(raw) / SCALE_Q16


def dec_to_q16(d: Decimal, rounding="trunc") -> int:
    """Convert a Decimal to a raw Q16 integer."""
    scaled = d * SCALE_Q16
    if rounding == "rhu":
        scaled = scaled.to_integral_value(rounding=ROUND_HALF_UP)
    else:
        scaled = scaled.to_integral_value(rounding=ROUND_FLOOR)
    return int(scaled)


def check(label: str, got_raw: int, expected_dec: Decimal, rounding="trunc"):
    global passed, failed
    expected_raw = dec_to_q16(expected_dec, rounding)
    diff = abs(got_raw - expected_raw)
    if diff <= 1:
        passed += 1
    else:
        failed += 1
        got_dec = q16_to_dec(got_raw)
        exp_dec = expected_dec
        print(f"FAIL  {label}")
        print(f"      got  {got_raw:#010x} ({float(got_dec):.8f})")
        print(f"      want {expected_raw:#010x} ({float(exp_dec):.8f})")
        print(f"      diff {diff} ULP(s)")


# ---------------------------------------------------------------------------
# Compile and run the C probe that just prints raw Q16 values
# ---------------------------------------------------------------------------

PROBE_SRC = """\
#include <stdio.h>
#include "fixed_point.h"

int main(void) {
    /* format: label raw_hex */
    printf("add_pos    %d\\n", q16_add(int_to_q16(3), int_to_q16(4)));
    printf("add_neg    %d\\n", q16_add(int_to_q16(-5), int_to_q16(2)));
    printf("sub_basic  %d\\n", q16_sub(int_to_q16(10), int_to_q16(3)));
    printf("mul_int    %d\\n", q16_mul(int_to_q16(6), int_to_q16(7), 0));
    printf("mul_frac   %d\\n", q16_mul(0x00008000, 0x00008000, 0));  /* 0.5*0.5 */
    printf("mul_rhu    %d\\n", q16_mul(0x00008001, 0x00008000, 1));  /* just over 0.5*0.5 */
    printf("div_exact  %d\\n", q16_div(int_to_q16(7), int_to_q16(2), 0));
    printf("div_third  %d\\n", q16_div(int_to_q16(1), int_to_q16(3), 0));
    printf("div_neg    %d\\n", q16_div(int_to_q16(-5), int_to_q16(2), 0));
    printf("sqrt_4     %d\\n", q16_sqrt(int_to_q16(4), 0));
    printf("sqrt_2     %d\\n", q16_sqrt(int_to_q16(2), 0));
    printf("sqrt_half  %d\\n", q16_sqrt(0x00008000, 0));  /* sqrt(0.5) */
    printf("sqrt_rhu   %d\\n", q16_sqrt(int_to_q16(2), 1));
    return 0;
}
"""

def build_and_run_probe():
    probe_path = "/tmp/_fp_validate_probe.c"
    exe_path   = "/tmp/_fp_validate_probe"

    with open(probe_path, "w") as f:
        f.write(PROBE_SRC)

    # build from repo root (assumes validate.py lives in test/)
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    result = subprocess.run(
        ["gcc", "-std=c11", "-O2", f"-I{repo_root}",
         f"{repo_root}/fixed_point.c", probe_path, "-o", exe_path],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("Build failed:\n", result.stderr)
        sys.exit(1)

    out = subprocess.run([exe_path], capture_output=True, text=True)
    if out.returncode != 0:
        print("Probe crashed:\n", out.stderr)
        sys.exit(1)

    values = {}
    for line in out.stdout.strip().splitlines():
        parts = line.split()
        if len(parts) == 2:
            values[parts[0]] = int(parts[1])
    return values


def main():
    print("Building validation probe...")
    v = build_and_run_probe()
    print(f"Got {len(v)} values from C, comparing against decimal reference...\n")

    check("add: 3 + 4 = 7",           v["add_pos"],   Decimal("7"))
    check("add: -5 + 2 = -3",         v["add_neg"],   Decimal("-3"))
    check("sub: 10 - 3 = 7",          v["sub_basic"], Decimal("7"))
    check("mul: 6 * 7 = 42",          v["mul_int"],   Decimal("42"))
    check("mul: 0.5 * 0.5 = 0.25",    v["mul_frac"],  Decimal("0.25"))
    check("mul: ~0.5001 * 0.5 (rhu)", v["mul_rhu"],
          Decimal(0x8001) / SCALE_Q16 * Decimal("0.5"), rounding="rhu")

    check("div: 7 / 2 = 3.5",         v["div_exact"], Decimal("3.5"))
    check("div: 1 / 3",               v["div_third"], Decimal(1) / Decimal(3))
    check("div: -5 / 2 = -2.5",       v["div_neg"],   Decimal("-2.5"))

    check("sqrt: sqrt(4) = 2",        v["sqrt_4"],    Decimal("2"))
    check("sqrt: sqrt(2)",            v["sqrt_2"],    Decimal(2).sqrt())
    check("sqrt: sqrt(0.5)",          v["sqrt_half"], (Decimal("0.5")).sqrt())
    check("sqrt: sqrt(2) rhu",        v["sqrt_rhu"],  Decimal(2).sqrt(), rounding="rhu")

    print(f"\n{passed} passed, {failed} failed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
