/* stdlib.h -- Standard Library for Zenbite C Compiler
 *
 * Provides standard C library functions for memory, conversion, and more.
 * Include this in .ZBX programs: #include "stdlib.h"
 */
#ifndef ZENBITE_STDLIB_H
#define ZENBITE_STDLIB_H

/* ── Memory Management ─────────────────────────────────────────────── */

/* Allocate n bytes, returns pointer (0 on failure) */
int malloc(int n);

/* Free previously allocated memory */
void free(int ptr);

/* Allocate array of n elements of size bytes each */
int calloc(int n, int size);

/* Reallocate memory to new size */
int realloc(int ptr, int new_size);

/* ── Conversion Functions ──────────────────────────────────────────── */

/* Convert string to integer (base 10) */
int atoi(const char *s);

/* Convert integer to string (buffer must be large enough) */
void itoa(int val, char *buf, int base);

/* Convert string to long integer */
long strtol(const char *s, char **endp, int base);

/* ── Random Numbers ────────────────────────────────────────────────── */

/* Seed the random number generator */
void srand(int seed);

/* Return random number 0 to RAND_MAX */
int rand(void);

/* Return random number in range [min, max] */
int rand_range(int min, int max);

/* ── Absolute Value ────────────────────────────────────────────────── */

/* Absolute value of integer */
int abs(int x);

/* Absolute value of long */
long labs(long x);

/* ── Min/Max ───────────────────────────────────────────────────────── */

/* Return minimum of two values */
int min(int a, int b);

/* Return maximum of two values */
int max(int a, int b);

/* Clamp value between min and max */
int clamp(int x, int lo, int hi);

/* ── Bit Operations ────────────────────────────────────────────────── */

/* Count set bits (population count) */
int popcount(int x);

/* Find first set bit (returns -1 if none) */
int ffs(int x);

/* Find highest set bit */
int fls(int x);

/* Rotate left */
int rotl(int x, int n);

/* Rotate right */
int rotr(int x, int n);

/* ── Integer Math ──────────────────────────────────────────────────── */

/* Integer square root (floor) */
int isqrt(int n);

/* Integer power */
int ipow(int base, int exp);

/* Greatest common divisor */
int gcd(int a, int b);

/* Least common multiple */
int lcm(int a, int b);

/* ── System Functions ──────────────────────────────────────────────── */

/* Get current system tick count */
int get_ticks(void);

/* Sleep for specified milliseconds */
void sleep_ms(int ms);

/* Get elapsed time since start */
int elapsed_ms(int start);

/* ── Process Functions ─────────────────────────────────────────────── */

/* Yield CPU to other processes */
void yield(void);

/* Sleep current process for ticks */
void proc_sleep(int ticks);

#endif /* ZENBITE_STDLIB_H */
