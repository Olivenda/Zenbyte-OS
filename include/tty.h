/* tty.h -- Virtual TTY subsystem for Zenbite
 *
 * Provides multiple virtual terminals with VT100 support.
 * Include this in kernel code: #include "tty.h"
 */
#ifndef ZENBITE_TTY_H
#define ZENBITE_TTY_H

#include "types.h"

/* ── TTY Constants ─────────────────────────────────────────────────── */

#define TTY_COUNT       4       /* Number of virtual TTYs */
#define TTY_COLS        80
#define TTY_ROWS        25

/* ── TTY API ───────────────────────────────────────────────────────── */

/* Initialize the TTY subsystem */
void tty_init(void);

/* Switch to a different TTY (0..TTY_COUNT-1) */
void tty_switch(int id);

/* Get the current active TTY number */
int tty_get_current(void);

/* Check if a TTY is the active one */
int tty_is_active(int id);

/* Get TTY name (tty1, tty2, etc.) */
const char *tty_get_name(int id);

/* ── Character Output ──────────────────────────────────────────────── */

/* Write a character to a TTY (handles VT100 sequences) */
void tty_putc(int id, char ch);

/* Write a string to a TTY */
void tty_puts(int id, const char *s);

/* ── Character Input ───────────────────────────────────────────────── */

/* Get a character from TTY input buffer (non-blocking, -1 if empty) */
int tty_getc(int id);

/* Get a character from TTY input buffer (blocking, yields while waiting) */
int tty_getc_wait(int id);

/* Put a character into TTY input buffer */
void tty_putc_input(int id, char ch);

/* Check if TTY has input available */
int tty_has_input(int id);

/* ── Line Discipline ───────────────────────────────────────────────── */

/* Set TTY to raw mode (1) or cooked mode (0) */
void tty_set_raw(int id, int raw);

/* Enable/disable echo (1=on, 0=off) */
void tty_set_echo(int id, int echo);

/* ── Screen Management ─────────────────────────────────────────────── */

/* Compose the active TTY's cell buffer into the VGA shadow buffer */
void tty_compose(void);

/* ── Keyboard Handling ─────────────────────────────────────────────── */

/* Handle a keyboard key press (checks Ctrl+Alt+F1-F4 for TTY switching) */
void tty_handle_key(int key);

#endif /* ZENBITE_TTY_H */
