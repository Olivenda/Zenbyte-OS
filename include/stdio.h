/* stdio.h -- Standard I/O for Zenbite C Compiler
 *
 * Provides file I/O, console I/O, and formatting functions.
 * Include this in .ZBX programs: #include "stdio.h"
 */
#ifndef ZENBITE_STDIO_H
#define ZENBITE_STDIO_H

/* ── File Operations ───────────────────────────────────────────────── */

/* Open file, returns file descriptor or -1 */
int fopen(const char *path);

/* Create new file, returns file descriptor or -1 */
int fcreate(const char *path);

/* Read n bytes from file */
int fread(int fd, void *buf, int n);

/* Write n bytes to file */
int fwrite(int fd, const void *buf, int n);

/* Close file */
int fclose(int fd);

/* Read a character from file */
int fgetc(int fd);

/* Write a character to file */
int fputc(int fd, int ch);

/* Read line from file (returns length, -1 on error) */
int fgets(int fd, char *buf, int max);

/* Write string to file */
int fputs(int fd, const char *s);

/* Get file position */
int ftell(int fd);

/* Seek to position */
int fseek(int fd, int offset, int whence);

/* ── Console I/O ───────────────────────────────────────────────────── */

/* Print formatted string to console */
int printf(const char *fmt, ...);

/* Print string to console */
int puts(const char *s);

/* Read string from console (max len) */
int gets(char *buf, int max);

/* Get single character (blocking) */
int getchar(void);

/* Get single character (non-blocking, -1 if none) */
int kb_hit(void);

/* Put character to console */
int putchar(int ch);

/* Print string with position and color */
int at_puts(int row, int col, int color, const char *s);

/* ── String Formatting ─────────────────────────────────────────────── */

/* Format string to buffer */
int sprintf(char *buf, const char *fmt, ...);

/* Format string to file */
int fprintf(int fd, const char *fmt, ...);

/* Scanf (basic: reads integers from console) */
int scanf(const char *fmt, ...);

/* ── Console Control ───────────────────────────────────────────────── */

/* Clear screen with color */
void cls(int color);

/* Set cursor position */
void gotoxy(int row, int col);

/* Clear to end of line */
void clreol(void);

/* Set text color */
void textcolor(int color);

/* Set background color */
void textbackground(int color);

/* ── String Constants ──────────────────────────────────────────────── */

#define EOF     (-1)
#define NULL    ((void*)0)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif /* ZENBITE_STDIO_H */
