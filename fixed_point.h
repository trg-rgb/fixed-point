#pragma once
#include <stdint.h>

/* Q16 = 16.16 fixed-point, Q32 = 32.32 */
typedef int32_t  q16_t;
typedef int64_t  q32_t;

#define Q16_MAX  INT32_MAX
#define Q16_MIN  INT32_MIN
#define Q32_MAX  INT64_MAX
#define Q32_MIN  INT64_MIN

typedef enum { TRUNC = 0, ROUND_HALF_UP } rounding_t;

/* Q16 ops */
q16_t q16_add(q16_t a, q16_t b);
q16_t q16_sub(q16_t a, q16_t b);
q16_t q16_mul(q16_t a, q16_t b, rounding_t rnd);
q16_t q16_div(q16_t a, q16_t b, rounding_t rnd);
q16_t q16_sqrt(q16_t x, rounding_t rnd);

/* Q32 ops */
q32_t q32_add(q32_t a, q32_t b);
q32_t q32_sub(q32_t a, q32_t b);
q32_t q32_mul(q32_t a, q32_t b, rounding_t rnd);
q32_t q32_div(q32_t a, q32_t b, rounding_t rnd);
q32_t q32_sqrt(q32_t x, rounding_t rnd);

/* conversions */
q16_t   int_to_q16(int32_t x);
q32_t   int_to_q32(int64_t x);
int32_t q16_to_int(q16_t x, rounding_t rnd);
q32_t   q16_to_q32(q16_t x);
q16_t   q32_to_q16(q32_t x, rounding_t rnd);
