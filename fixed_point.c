/*
 * fixed_point.c
 * 16.16 and 32.32 fixed-point arithmetic for RISC-V targets w/o FPU
 *
 * Started this because softfp was pulling in 12KB of libgcc and the
 * bootloader has a 16KB budget. Fixed-point gets us down to ~2KB.
 *
 * Notation: Q16 = 16.16 (stored in int32_t), Q32 = 32.32 (stored in int64_t)
 * Scale factors: Q16 values are scaled by 2^16 = 65536
 *                Q32 values are scaled by 2^32
 *
 * Rounding: TRUNC just drops bits, RHU = round half up (add 0.5 before trunc)
 */

#include "fixed_point.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Q16 (16.16)  -- workhorse format, fits in a regular 32-bit register
 * ------------------------------------------------------------------------- */

q16_t q16_add(q16_t a, q16_t b)
{
    /* addition is just integer addition -- overflow is caller's problem for now
     * TODO: add a saturating variant, the motor controller guys asked for it */
    return a + b;
}

q16_t q16_sub(q16_t a, q16_t b)
{
    return a - b;
}

q16_t q16_mul(q16_t a, q16_t b, rounding_t rnd)
{
    /* promote to 64-bit so the full product fits before we shift back down.
     * two Q16 numbers multiplied give a Q32 result, so we shift right by 16
     * to land back in Q16. */
    int64_t prod = (int64_t)a * (int64_t)b;

    if (rnd == ROUND_HALF_UP) {
        /* add half an ULP (0x8000) before truncating */
        prod += (1 << 15);
    }

    return (q16_t)(prod >> 16);
}

q16_t q16_div(q16_t a, q16_t b, rounding_t rnd)
{
    if (b == 0)
        return (a >= 0) ? Q16_MAX : Q16_MIN;   /* saturate, don't trap */

    /* shift numerator up before dividing so we don't lose the fractional bits.
     * basically: (a * 2^16) / b gives the result still in Q16 format */
    int64_t num = (int64_t)a << 16;

    if (rnd == ROUND_HALF_UP) {
        /* bias by half the divisor to get round-half-up behavior */
        if (num >= 0)
            num += (b > 0) ? (b >> 1) : -(b >> 1);
        else
            num -= (b > 0) ? (b >> 1) : -(b >> 1);
        /* NOTE: this is slightly off for negative b, revisit -- rgk 2026-01-14 */
    }

    return (q16_t)(num / b);
}

q16_t q16_sqrt(q16_t x, rounding_t rnd)
{
    if (x < 0)
        return 0;   /* undefined, return 0 rather than NaN nonsense */
    if (x == 0)
        return 0;

    /* Newton-Raphson: x_{n+1} = (x_n + S/x_n) / 2
     * S = x << 16, so sqrt(S) lands directly in Q16 format -- no shift needed
     * at the end. previous version did >> 16 which was wrong. */
    int64_t S = (int64_t)x << 16;

    /* seed the guess using bit position rather than S/2 -- S/2 starts so far
     * from the true value that 8 iterations isn't enough to converge */
    int bits = 64 - __builtin_clzll((uint64_t)S);
    int64_t guess = 1LL << ((bits + 1) / 2);

    for (int i = 0; i < 8; i++) {
        int64_t next = (guess + S / guess) >> 1;
        if (next == guess)
            break;
        guess = next;
    }

    q16_t result = (q16_t)guess;   /* sqrt(S) is already in Q16, no shift */

    /* nudge up if rounding asks for it and we're below the true value */
    if (rnd == ROUND_HALF_UP) {
        q16_t r2 = q16_mul(result, result, TRUNC);
        if (r2 < x)
            result++;
    }

    return result;
}


/* -------------------------------------------------------------------------
 * Q32 (32.32)  -- higher precision, use when Q16 accumulation error matters
 * ------------------------------------------------------------------------- */

q32_t q32_add(q32_t a, q32_t b)
{
    return a + b;
}

q32_t q32_sub(q32_t a, q32_t b)
{
    return a - b;
}

q32_t q32_mul(q32_t a, q32_t b, rounding_t rnd)
{
    /* Q32 * Q32 needs 128-bit intermediate -- no __int128 on all targets so
     * we split into high/low 32-bit halves and do it manually.
     *
     * a = ah*2^32 + al
     * b = bh*2^32 + bl
     * a*b = ah*bh*2^64 + (ah*bl + al*bh)*2^32 + al*bl
     *
     * we only keep the middle 64 bits (the Q32 result), so:
     *   result = ah*bh (but this overflows Q32 range -- clamp it) +
     *            (ah*bl + al*bh) >> 32 +
     *            al*bl >> 64 (rounds to zero, ignored)
     */
    int64_t ah = a >> 32;
    uint32_t al = (uint32_t)(a & 0xFFFFFFFF);
    int64_t bh = b >> 32;
    uint32_t bl = (uint32_t)(b & 0xFFFFFFFF);

    int64_t mid = ah * (int64_t)bl + (int64_t)al * bh;
    int64_t lo_contrib = (int64_t)((uint64_t)al * bl >> 32);

    if (rnd == ROUND_HALF_UP)
        lo_contrib += ((uint64_t)al * bl >> 31) & 1;

    /* ah*bh gives the integer*integer part -- if this overflows int64 we're
     * already broken at the caller level, just let it wrap for now */
    return ah * bh * (int64_t)(1LL << 32) + mid + lo_contrib;
    /* FIXME: the ah*bh * 2^32 term will overflow for large values.
     * need to clamp to Q32_MAX/MIN. haven't hit this in practice yet
     * but it'll bite someone eventually. */
}

q32_t q32_div(q32_t a, q32_t b, rounding_t rnd)
{
    if (b == 0)
        return (a >= 0) ? Q32_MAX : Q32_MIN;

    /* same trick as Q16 but we can't just shift 32 into a 64-bit value
     * without losing the top bits -- use __int128 here, it's the cleanest
     * option and RISC-V GCC supports it even without hardware divide */
    __int128 num = (__int128)a << 32;

    if (rnd == ROUND_HALF_UP) {
        __int128 half = b > 0 ? b >> 1 : -(b >> 1);
        num = (num >= 0) ? num + half : num - half;
    }

    return (q32_t)(num / b);
}

q32_t q32_sqrt(q32_t x, rounding_t rnd)
{
    if (x <= 0)
        return 0;

    /* same Newton-Raphson as Q16 but iteration in __int128 */
    __int128 S = (__int128)x;
    __int128 guess = S >> 1;

    if (guess == 0)
        guess = 1;

    for (int i = 0; i < 16; i++) {   /* more iters needed at higher precision */
        __int128 next = (guess + S / guess) >> 1;
        if (next == guess)
            break;
        guess = next;
    }

    q32_t result = (q32_t)guess;

    if (rnd == ROUND_HALF_UP) {
        /* check if result^2 < x -- but result^2 overflows q32 easily.
         * compare (result+1)^2 <= x instead, promote to __int128 */
        __int128 r1 = (__int128)(result + 1) * (result + 1) >> 32;
        if (r1 <= (__int128)x)
            result++;
    }

    return result;
}


/* -------------------------------------------------------------------------
 * Conversion helpers
 * ------------------------------------------------------------------------- */

q16_t int_to_q16(int32_t x)  { return (q16_t)x << 16; }
q32_t int_to_q32(int64_t x)  { return (q32_t)x << 32; }

int32_t q16_to_int(q16_t x, rounding_t rnd)
{
    if (rnd == ROUND_HALF_UP)
        return (int32_t)((x + (1 << 15)) >> 16);
    return (int32_t)(x >> 16);
}

/* promote/demote between formats */
q32_t q16_to_q32(q16_t x) { return (q32_t)x << 16; }
q16_t q32_to_q16(q32_t x, rounding_t rnd)
{
    if (rnd == ROUND_HALF_UP)
        return (q16_t)((x + (1 << 15)) >> 16);
    return (q16_t)(x >> 16);
}
