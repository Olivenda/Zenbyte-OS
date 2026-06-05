/* math.h -- Fixed-Point Math Library for Zenbite C Compiler
 *
 * All math is done using fixed-point integers (16.16 format).
 * To convert: fixed = int_val << 16; float_val = fixed / 65536.0;
 *
 * Include this in .ZBX programs: #include "math.h"
 */
#ifndef ZENBITE_MATH_H
#define ZENBITE_MATH_H

/* ── Fixed-Point Constants ─────────────────────────────────────────── */

#define FP_SHIFT   16
#define FP_ONE     (1 << FP_SHIFT)           /* 1.0 */
#define FP_HALF    (1 << (FP_SHIFT - 1))     /* 0.5 */
#define FP_PI      (351695)                  /* 3.14159... */
#define FP_E       (178145)                  /* 2.71828... */
#define FP_SQRT2   (92682)                   /* 1.41421... */
#define FP_DEG     (1144)                    /* 1 degree in fixed-point */
#define FP_RAD     (181)                     /* 1 radian in fixed-point */

/* ── Conversion Macros ─────────────────────────────────────────────── */

/* Convert integer to fixed-point */
#define INT_TO_FP(x)   ((x) << FP_SHIFT)

/* Convert fixed-point to integer (truncates) */
#define FP_TO_INT(x)   ((x) >> FP_SHIFT)

/* Convert fixed-point to integer (rounds) */
#define FP_TO_INT_R(x) (((x) + FP_HALF) >> FP_SHIFT)

/* ── Basic Arithmetic ──────────────────────────────────────────────── */

/* Multiply two fixed-point numbers */
int fp_mul(int a, int b);

/* Divide two fixed-point numbers */
int fp_div(int a, int b);

/* ── Square Root ───────────────────────────────────────────────────── */

/* Integer square root */
int isqrt(int n);

/* Fixed-point square root */
int fp_sqrt(int x);

/* ── Trigonometric Functions (fixed-point) ─────────────────────────── */

/* Sine of angle (radians in fixed-point) */
int fp_sin(int angle);

/* Cosine of angle (radians in fixed-point) */
int fp_cos(int angle);

/* Tangent of angle (radians in fixed-point) */
int fp_tan(int angle);

/* Arc sine */
int fp_asin(int x);

/* Arc cosine */
int fp_acos(int x);

/* Arc tangent */
int fp_atan(int x);

/* Arc tangent of y/x (handles quadrants) */
int fp_atan2(int y, int x);

/* ── Exponential / Logarithmic ─────────────────────────────────────── */

/* e^x */
int fp_exp(int x);

/* 2^x */
int fp_exp2(int x);

/* Natural logarithm */
int fp_log(int x);

/* Base-2 logarithm */
int fp_log2(int x);

/* Base-10 logarithm */
int fp_log10(int x);

/* ── Power Functions ───────────────────────────────────────────────── */

/* x^y (fixed-point exponent) */
int fp_pow(int base, int exp);

/* Integer power */
int ipow(int base, int exp);

/* ── Rounding / Modulo ─────────────────────────────────────────────── */

/* Floor (round toward -inf) */
int fp_floor(int x);

/* Ceil (round toward +inf) */
int fp_ceil(int x);

/* Round to nearest integer */
int fp_round(int x);

/* Fixed-point modulo */
int fp_mod(int a, int b);

/* ── Angle Conversion ──────────────────────────────────────────────── */

/* Degrees to radians (fixed-point) */
int deg_to_rad(int degrees);

/* Radians to degrees (fixed-point) */
int rad_to_deg(int radians);

/* ── Vector Math ───────────────────────────────────────────────────── */

/* 2D vector structure */
typedef struct {
    int x, y;
} vec2_t;

/* Create 2D vector */
vec2_t vec2_create(int x, int y);

/* Vector addition */
vec2_t vec2_add(vec2_t a, vec2_t b);

/* Vector subtraction */
vec2_t vec2_sub(vec2_t a, vec2_t b);

/* Vector scalar multiplication */
vec2_t vec2_scale(vec2_t v, int s);

/* Dot product */
int vec2_dot(vec2_t a, vec2_t b);

/* Cross product (returns scalar) */
int vec2_cross(vec2_t a, vec2_t b);

/* Vector length (fixed-point) */
int vec2_length(vec2_t v);

/* Normalize vector */
vec2_t vec2_normalize(vec2_t v);

#endif /* ZENBITE_MATH_H */
