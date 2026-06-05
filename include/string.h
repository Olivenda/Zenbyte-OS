/* string.h -- String Functions for Zenbite C Compiler
 *
 * Provides standard string manipulation functions.
 * Include this in .ZBX programs: #include "string.h"
 */
#ifndef ZENBITE_STRING_H
#define ZENBITE_STRING_H

#include "types.h"

/* ── String Length ─────────────────────────────────────────────────── */

/* Return length of string (excluding NUL) */
size_t strlen(const char *s);

/* Return length of string up to max characters */
size_t strnlen(const char *s, size_t max);

/* ── String Copy ───────────────────────────────────────────────────── */

/* Copy string src to dst (returns dst) */
char *strcpy(char *dst, const char *src);

/* Copy up to n characters from src to dst */
char *strncpy(char *dst, const char *src, size_t n);

/* Copy memory block (returns dst) */
void *memcpy(void *dst, const void *src, size_t n);

/* Copy overlapping memory block (returns dst) */
void *memmove(void *dst, const void *src, size_t n);

/* ── String Concatenation ──────────────────────────────────────────── */

/* Concatenate src to end of dst (returns dst) */
char *strcat(char *dst, const char *src);

/* Concatenate up to n characters from src to end of dst */
char *strncat(char *dst, const char *src, size_t n);

/* ── String Comparison ─────────────────────────────────────────────── */

/* Compare two strings (returns <0, 0, >0) */
int strcmp(const char *s1, const char *s2);

/* Compare up to n characters of two strings */
int strncmp(const char *s1, const char *s2, size_t n);

/* Compare strings case-insensitively */
int strcasecmp(const char *s1, const char *s2);

/* Compare up to n characters case-insensitively */
int strncasecmp(const char *s1, const char *s2, int n);

/* ── String Searching ──────────────────────────────────────────────── */

/* Find first occurrence of ch in string (returns pointer or NULL) */
char *strchr(const char *s, int ch);

/* Find last occurrence of ch in string (returns pointer or NULL) */
char *strrchr(const char *s, int ch);

/* Find first occurrence of substr in string (returns pointer or NULL) */
char *strstr(const char *haystack, const char *needle);

/* Find first occurrence of any character from accept in string */
char *strpbrk(const char *s, const char *accept);

/* Find first occurrence of any byte from accept in memory */
void *memchr(const void *s, int ch, size_t n);

/* ── Memory Operations ─────────────────────────────────────────────── */

/* Set memory to value (returns s) */
void *memset(void *s, int ch, size_t n);

/* Compare memory blocks (returns <0, 0, >0) */
int memcmp(const void *s1, const void *s2, size_t n);

/* Fill memory with zeros */
void bzero(void *s, size_t n);

/* ── String Conversion ─────────────────────────────────────────────── */

/* Check if character is a digit (0-9) */
int isdigit(int ch);

/* Check if character is a letter (a-z, A-Z) */
int isalpha(int ch);

/* Check if character is alphanumeric */
int isalnum(int ch);

/* Check if character is a whitespace (space, tab, newline, etc.) */
int isspace(int ch);

/* Convert character to uppercase */
int toupper(int ch);

/* Convert character to lowercase */
int tolower(int ch);

/* Convert string to integer */
int atoi(const char *s);

/* Convert string to long integer */
long strtol(const char *s, char **endp, int base);

/* Convert integer to string (base 10) */
void itoa(int val, char *buf, int base);

/* Convert string to uppercase (returns s) */
char *strupr(char *s);

/* Convert string to lowercase (returns s) */
char *strlwr(char *s);

/* ── Tokenizing ────────────────────────────────────────────────────── */

/* Tokenize string (returns next token, NULL when done) */
char *strtok(char *str, const char *delim);

/* ── String Building ───────────────────────────────────────────────── */

/* Reverse a string in place */
void strrev(char *s);

/* Format string to buffer */
int sprintf(char *buf, const char *fmt, ...);

/* ── Path Operations ───────────────────────────────────────────────── */

/* Get filename from path (pointer to last component) */
const char *basename(const char *path);

/* Get directory from path (copies to buffer) */
void dirname(char *buf, const char *path);

/* Check if path ends with extension */
int has_ext(const char *path, const char *ext);

/* ── Constants ─────────────────────────────────────────────────────── */

/* Return values for strcmp */
#define STR_EQ  0
#define STR_LT  (-1)
#define STR_GT  1

#endif /* ZENBITE_STRING_H */
