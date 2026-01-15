/*
 * test/test_fixed_point.c
 *
 * Quick-and-dirty test runner. Not a full framework -- just EXPECT macros
 * and a pass/fail count. Keeps the binary small for on-target testing.
 *
 * Run natively with: make
 * On target: flash test_runner.elf and read UART output
 */

#include "../fixed_point.h"
#include <stdio.h>
#include <stdint.h>

static int passed = 0;
static int failed = 0;

#define EXPECT_EQ(label, got, expected)                                     \
    do {                                                                    \
        if ((got) == (expected)) {                                          \
            passed++;                                                       \
        } else {                                                            \
            failed++;                                                       \
            printf("FAIL  %s\n      got 0x%08X  want 0x%08X\n",           \
                   (label), (unsigned)(got), (unsigned)(expected));        \
        }                                                                   \
    } while (0)

/* allow results within ±1 ULP -- floating point reference isn't exact */
#define EXPECT_NEAR(label, got, expected)                                   \
    do {                                                                    \
        int64_t diff = (int64_t)(got) - (int64_t)(expected);              \
        if (diff >= -1 && diff <= 1) {                                     \
            passed++;                                                       \
        } else {                                                            \
            failed++;                                                       \
            printf("FAIL  %s\n      got %d  want %d  (diff %lld)\n",      \
                   (label), (int)(got), (int)(expected), (long long)diff); \
        }                                                                   \
    } while (0)

/* ---- Q16 helpers -------------------------------------------------------- */
/* build a Q16 literal from integer and fractional parts (both positive).
 * e.g. Q16_LIT(3, 5) ~ 3.5  (frac is out of 65536) */
#define Q16(i, frac)  ((q16_t)(((int32_t)(i) << 16) | (uint16_t)(frac)))


static void test_q16_add(void)
{
    EXPECT_EQ("q16 add: 1.0 + 2.0 = 3.0",
              q16_add(int_to_q16(1), int_to_q16(2)), int_to_q16(3));

    EXPECT_EQ("q16 add: -1.0 + 1.0 = 0",
              q16_add(int_to_q16(-1), int_to_q16(1)), 0);

    /* 0.5 + 0.25 = 0.75 */
    EXPECT_EQ("q16 add: 0.5 + 0.25 = 0.75",
              q16_add(0x00008000, 0x00004000), 0x0000C000);
}

static void test_q16_sub(void)
{
    EXPECT_EQ("q16 sub: 3.0 - 1.0 = 2.0",
              q16_sub(int_to_q16(3), int_to_q16(1)), int_to_q16(2));

    EXPECT_EQ("q16 sub: 0 - 1.0 = -1.0",
              q16_sub(0, int_to_q16(1)), int_to_q16(-1));
}

static void test_q16_mul(void)
{
    /* 2.0 * 3.0 = 6.0 */
    EXPECT_EQ("q16 mul: 2*3=6 (trunc)",
              q16_mul(int_to_q16(2), int_to_q16(3), TRUNC), int_to_q16(6));

    /* 0.5 * 0.5 = 0.25 */
    EXPECT_EQ("q16 mul: 0.5*0.5=0.25 (trunc)",
              q16_mul(0x00008000, 0x00008000, TRUNC), 0x00004000);

    /* negative: -2 * 3 = -6 */
    EXPECT_EQ("q16 mul: -2*3=-6",
              q16_mul(int_to_q16(-2), int_to_q16(3), TRUNC), int_to_q16(-6));

    /* rounding: something that differs between TRUNC and RHU */
    /* 1/3 * 3 -- should round back to 1 with RHU, may be off by 1 ULP with TRUNC */
    q16_t third = q16_div(int_to_q16(1), int_to_q16(3), TRUNC);
    EXPECT_NEAR("q16 mul: (1/3)*3 ~= 1.0 (rhu)",
                q16_mul(third, int_to_q16(3), ROUND_HALF_UP), int_to_q16(1));
}

static void test_q16_div(void)
{
    EXPECT_EQ("q16 div: 6/2=3",
              q16_div(int_to_q16(6), int_to_q16(2), TRUNC), int_to_q16(3));

    EXPECT_EQ("q16 div: 1/4=0.25",
              q16_div(int_to_q16(1), int_to_q16(4), TRUNC), 0x00004000);

    /* 1/3 ~ 0.333... -- check it's close */
    EXPECT_NEAR("q16 div: 1/3 ~= 0x5555",
                q16_div(int_to_q16(1), int_to_q16(3), TRUNC), 0x00005555);

    /* divide by zero should saturate not crash */
    EXPECT_EQ("q16 div: x/0 saturates to Q16_MAX",
              q16_div(int_to_q16(1), 0, TRUNC), Q16_MAX);

    EXPECT_EQ("q16 div: -x/0 saturates to Q16_MIN",
              q16_div(int_to_q16(-1), 0, TRUNC), Q16_MIN);
}

static void test_q16_sqrt(void)
{
    EXPECT_EQ("q16 sqrt: sqrt(4)=2",
              q16_sqrt(int_to_q16(4), TRUNC), int_to_q16(2));

    EXPECT_EQ("q16 sqrt: sqrt(9)=3",
              q16_sqrt(int_to_q16(9), TRUNC), int_to_q16(3));

    /* sqrt(2) ~ 1.41421 -- 0x00016A0A in Q16 */
    EXPECT_NEAR("q16 sqrt: sqrt(2) ~= 0x16A0A",
                q16_sqrt(int_to_q16(2), TRUNC), 0x00016A0A);

    EXPECT_EQ("q16 sqrt: sqrt(0)=0", q16_sqrt(0, TRUNC), 0);

    /* negative input returns 0 */
    EXPECT_EQ("q16 sqrt: sqrt(-1)=0", q16_sqrt(int_to_q16(-1), TRUNC), 0);
}

/* ---- boundary / edge cases --------------------------------------------- */

static void test_edge_cases(void)
{
    /* multiplying two large values -- result should wrap or saturate,
     * just make sure it doesn't crash / hang */
    q16_t big = Q16_MAX;
    (void)q16_mul(big, big, TRUNC);   /* we just want no crash */

    /* sqrt of max representable value */
    (void)q16_sqrt(Q16_MAX, TRUNC);

    /* chained ops: (a + b) * c / c should get back close to a + b */
    q16_t a = int_to_q16(7);
    q16_t b = int_to_q16(3);
    q16_t c = int_to_q16(5);
    q16_t result = q16_div(q16_mul(q16_add(a, b), c, TRUNC), c, TRUNC);
    EXPECT_NEAR("q16 chain: (7+3)*5/5 ~= 10", result, int_to_q16(10));
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    printf("=== fixed_point test runner ===\n\n");

    test_q16_add();
    test_q16_sub();
    test_q16_mul();
    test_q16_div();
    test_q16_sqrt();
    test_edge_cases();

    /* TODO: add Q32 tests -- skipping for now since Q32 mul is still FIXME */

    printf("\n%d passed, %d failed\n", passed, failed);
    return (failed > 0) ? 1 : 0;
}
