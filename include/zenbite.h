/* zenbite.h -- Standard header for Zenbite C development.
 *
 * Include this in your .ZBX programs for access to all builtins:
 *   #include "zenbite.h"
 *
 * Or use the builtins directly without including (they're interpreted).
 * This header provides type definitions, constants, and helper macros
 * for use with the zbc interpreter.
 */
#ifndef ZENBITE_H
#define ZENBITE_H

/* ── Standard Library Headers ──────────────────────────────────────── */
/* Include these in your programs for full functionality:
 *   #include "zenbite.h"    -- types, constants, helpers
 *   #include "stdio.h"      -- file I/O, console I/O
 *   #include "stdlib.h"     -- memory, conversion, math
 *   #include "string.h"     -- string functions
 *   #include "math.h"       -- fixed-point math
 *   #include "time.h"       -- time functions
 *   #include "window.h"     -- windowing system
 */

/* ── Types ───────────────────────────────────────────────────────── */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    i8;
typedef signed short   i16;
typedef signed int     i32;
typedef int            size_t;
typedef int            bool;
typedef long           long64;
#define true  1
#define false 0
#define NULL  ((void*)0)

/* ── Constants ───────────────────────────────────────────────────── */
#define BLACK       0
#define BLUE        1
#define GREEN       2
#define CYAN        3
#define RED         4
#define MAGENTA     5
#define BROWN       6
#define LIGHT_GREY  7
#define DARK_GREY   8
#define LIGHT_BLUE  9
#define LIGHT_GREEN 10
#define LIGHT_CYAN  11
#define LIGHT_RED   12
#define LIGHT_MAGENTA 13
#define YELLOW      14
#define WHITE       15

/* Color helper: foreground + background */
#define COLOR(fg, bg) (((bg) << 4) | ((fg) & 0x0F))

/* Key codes */
#define KEY_ENTER   10
#define KEY_ESC     27
#define KEY_BACK    8
#define KEY_TAB     9
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_DEL     0x84
#define KEY_HOME    0x85
#define KEY_END     0x86
#define KEY_PGUP    0x87
#define KEY_PGDN    0x88
#define KEY_F1      0x90
#define KEY_F2      0x91
#define KEY_F3      0x92
#define KEY_F4      0x93
#define KEY_F5      0x94
#define KEY_F6      0x95
#define KEY_F7      0x96
#define KEY_F8      0x97
#define KEY_F9      0x98
#define KEY_F10     0x99

/* ── Screen dimensions ───────────────────────────────────────────── */
#define ROWS  25
#define COLS  80

/* ── Math helpers ────────────────────────────────────────────────── */
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* ── String helpers (for zbc) ────────────────────────────────────── */
/* zbc supports: puts, printf, putchar, getchar, waitkey, key */

/* ── Drawing helpers ─────────────────────────────────────────────── */
static void draw_box(int r, int c, int w, int h, int color) {
    int i;
    /* Top/bottom borders */
    for (i = c; i < c + w; i++) {
        putcell(r, i, '-', color);
        putcell(r + h - 1, i, '-', color);
    }
    /* Side borders */
    for (i = r; i < r + h; i++) {
        putcell(i, c, '|', color);
        putcell(i, c + w - 1, '|', color);
    }
    /* Corners */
    putcell(r, c, '+', color);
    putcell(r, c + w - 1, '+', color);
    putcell(r + h - 1, c, '+', color);
    putcell(r + h - 1, c + w - 1, '+', color);
}

static void draw_filled_box(int r, int c, int w, int h, int color, char fill) {
    int i, j;
    for (i = r; i < r + h; i++)
        for (j = c; j < c + w; j++)
            putcell(i, j, fill, color);
}

static void draw_text(int r, int c, int color, const char *s) {
    at_puts(r, c, color, s);
}

static void draw_centered(int r, int color, const char *s) {
    int len = 0;
    while (s[len]) len++;
    int c = (COLS - len) / 2;
    if (c < 0) c = 0;
    at_puts(r, c, color, s);
}

/* ── Timer helpers ───────────────────────────────────────────────── */
static int elapsed_ms(int start) {
    return (ticks() - start) * 10;  /* ticks are 10ms each */
}

/* ── Random number (simple LCG) ──────────────────────────────────── */
static u32 rng_state = 12345;
static u32 rand_next(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}
static int rand_range(int lo, int hi) {
    return lo + (rand_next() % (hi - lo + 1));
}

/* ── Quick Reference ────────────────────────────────────────────────
 *
 * STANDARD LIBRARY (include headers for full docs):
 *   stdio.h   - printf, puts, getchar, fopen, fclose, fread, fwrite
 *   stdlib.h  - malloc, free, atoi, itoa, abs, min, max, rand, sleep_ms
 *   string.h  - strlen, strcpy, strcmp, strcat, strstr, memset, memcpy
 *   math.h    - fp_mul, fp_div, fp_sin, fp_cos, fp_tan, fp_sqrt, fp_pow
 *   time.h    - clock, ticks, millis, seconds, get_time, date_str
 *   window.h  - win_create, win_destroy, win_show, win_puts, win_present
 *
 * BUILT-IN FUNCTIONS (always available):
 *   cls(color)           - clear screen
 *   putcell(r,c,ch,col)  - put character
 *   at_puts(r,c,col,str) - put string at position
 *   present()            - flush to screen
 *   key()                - non-blocking key (-1 if none)
 *   waitkey()            - blocking key wait
 *   frame(r,c,w,h,col)   - draw box outline
 *   button(r,c,w,col)    - draw button
 *   mouse_x/y/btn()      - mouse position/buttons
 *   ticks()              - system timer
 *   delay(n)             - sleep n ticks
 *   printf, puts, putchar, getchar - console I/O
 *   fopen, fcreate, fclose, fgetc, fputc - file I/O
 *   strlen, strcpy, strcat, memset, memcpy - string ops
 *
 * WINDOW SYSTEM (via window.h):
 *   win_create(r,c,w,h,title,col) - create window
 *   win_destroy(id)              - destroy window
 *   win_show(id) / win_hide(id)  - visibility
 *   win_move(id,r,c) / win_resize(id,w,h)
 *   win_clear(id,col)            - clear window
 *   win_putc(id,r,c,ch,col)      - draw character
 *   win_puts(id,r,c,col,str)     - draw string
 *   win_present(id)              - draw window
 *   win_get_event(id)            - get event
 *   win_create_button(id,r,c,w,text,col) - add button
 *   win_button_clicked(id)       - check click
 *
 * ──────────────────────────────────────────────────────────────────── */

#endif /* ZENBITE_H */
