/* tty.c -- Virtual TTY subsystem for Zenbite
 *
 * Provides multiple virtual terminals with:
 * - Per-TTY cell buffers (80x25)
 * - TTY switching via Ctrl+Alt+F1-F4
 * - VT100 escape sequence parsing
 * - Line discipline (cooked/raw mode)
 * - Input/output queues
 *
 * The active TTY is composited into the VGA shadow buffer
 * before vga_present() is called.
 */

#include "kernel.h"
#include "kio.h"
#include "vga.h"
#include "string.h"
#include "proc.h"

/* Forward declarations */
void tty_putc(int id, char ch);
void tty_puts(int id, const char *s);

/* ── TTY Constants ─────────────────────────────────────────────────── */

#define TTY_COUNT       4       /* Number of virtual TTYs */
#define TTY_COLS        80
#define TTY_ROWS        25
#define TTY_IN_BUF_SIZE 256
#define TTY_OUT_BUF_SIZE 4096
#define TTY_SCROLLBACK  100    /* Lines of scrollback history */

/* ── TTY Structure ─────────────────────────────────────────────────── */

struct tty {
    int id;                         /* TTY number (0..TTY_COUNT-1) */
    int active;                     /* Is this TTY the foreground one? */
    
    /* Cell buffer: character + attribute packed into u16 */
    u16 cell_buf[TTY_ROWS * TTY_COLS];
    
    /* Cursor position */
    int cursor_row;
    int cursor_col;
    
    /* Current text attributes */
    u8 fg;                          /* Foreground color (0-15) */
    u8 bg;                          /* Background color (0-15) */
    
    /* Scrollback buffer */
    u16 scrollback[TTY_SCROLLBACK * TTY_COLS];
    int scrollback_lines;           /* Number of lines in scrollback */
    int scrollback_pos;             /* Current scroll position (0 = bottom) */
    
    /* Input buffer (line discipline) */
    char in_buf[TTY_IN_BUF_SIZE];
    int in_head;                    /* Write position */
    int in_tail;                    /* Read position */
    int in_count;                   /* Bytes available */
    
    /* Line discipline */
    int raw_mode;                   /* 0=cooked, 1=raw */
    int echo;                       /* 1=echo input, 0=no echo */
    int tab_width;                  /* Tab expansion width */
    
    /* Output buffer */
    char out_buf[TTY_OUT_BUF_SIZE];
    int out_head;
    int out_tail;
    int out_count;
    
    /* VT100 state */
    int escape_state;               /* 0=normal, 1=ESC received, 2='[' received */
    int escape_param;               /* Current escape parameter */
    int escape_params[8];           /* Multiple parameters */
    int escape_param_count;         /* Number of parameters */
    int saved_row, saved_col;       /* Saved cursor position */
    int wrap_pending;               /* Wrap at end of line */
};

/* ── Global State ──────────────────────────────────────────────────── */

static struct tty ttys[TTY_COUNT];
static int current_tty = 0;
static int tty_initialized = 0;

/* ── Helper Functions ──────────────────────────────────────────────── */

static inline u16 make_cell(char ch, u8 fg, u8 bg) {
    return ((u16)(u8)ch) | ((u16)((fg & 0x0F) | ((bg & 0x0F) << 4)) << 8);
}

static inline char cell_char(u16 cell) {
    return (char)(cell & 0xFF);
}

static inline u8 cell_fg(u16 cell) {
    return (u8)(cell >> 8) & 0x0F;
}

static inline u8 cell_bg(u16 cell) {
    return (u8)(cell >> 12) & 0x0F;
}

/* ── TTY Initialization ────────────────────────────────────────────── */

void tty_init(void) {
    for (int i = 0; i < TTY_COUNT; i++) {
        struct tty *t = &ttys[i];
        t->id = i;
        t->active = (i == 0);
        t->cursor_row = 0;
        t->cursor_col = 0;
        t->fg = 7;  /* Light grey */
        t->bg = 0;  /* Black */
        t->scrollback_lines = 0;
        t->scrollback_pos = 0;
        t->in_head = 0;
        t->in_tail = 0;
        t->in_count = 0;
        t->raw_mode = 0;
        t->echo = 1;
        t->tab_width = 8;
        t->out_head = 0;
        t->out_tail = 0;
        t->out_count = 0;
        t->escape_state = 0;
        t->escape_param = 0;
        t->escape_param_count = 0;
        t->saved_row = 0;
        t->saved_col = 0;
        t->wrap_pending = 0;
        
        /* Clear cell buffer */
        for (int j = 0; j < TTY_ROWS * TTY_COLS; j++)
            t->cell_buf[j] = make_cell(' ', 7, 0);
    }
    
    /* Set TTY 0 as active */
    current_tty = 0;
    ttys[0].active = 1;
    tty_initialized = 1;
    
    kprintf("tty: %d virtual terminals initialized\n", TTY_COUNT);
}

/* ── TTY Switching ─────────────────────────────────────────────────── */

void tty_switch(int id) {
    if (id < 0 || id >= TTY_COUNT) return;
    if (id == current_tty) return;
    
    ttys[current_tty].active = 0;
    ttys[id].active = 1;
    current_tty = id;
    
    /* Update VGA cursor position for new TTY */
    struct tty *t = &ttys[id];
    vga_set_cursor(t->cursor_row, t->cursor_col);
    
    /* Mark display as needing refresh */
    vga_invalidate();
}

int tty_get_current(void) {
    return current_tty;
}

/* ── Screen Management ─────────────────────────────────────────────── */

static void tty_scroll_up(struct tty *t) {
    /* Save top line to scrollback */
    if (t->scrollback_lines < TTY_SCROLLBACK) {
        for (int c = 0; c < TTY_COLS; c++)
            t->scrollback[t->scrollback_lines * TTY_COLS + c] = t->cell_buf[c];
        t->scrollback_lines++;
    } else {
        /* Shift scrollback up */
        for (int c = 0; c < TTY_COLS * (TTY_SCROLLBACK - 1); c++)
            t->scrollback[c] = t->scrollback[c + TTY_COLS];
        for (int c = 0; c < TTY_COLS; c++)
            t->scrollback[(TTY_SCROLLBACK - 1) * TTY_COLS + c] = t->cell_buf[c];
    }
    
    /* Shift screen up */
    for (int r = 0; r < TTY_ROWS - 1; r++)
        for (int c = 0; c < TTY_COLS; c++)
            t->cell_buf[r * TTY_COLS + c] = t->cell_buf[(r + 1) * TTY_COLS + c];
    
    /* Clear bottom line */
    for (int c = 0; c < TTY_COLS; c++)
        t->cell_buf[(TTY_ROWS - 1) * TTY_COLS + c] = make_cell(' ', t->fg, t->bg);
}

static void tty_clear_line(struct tty *t, int row, int start_col, int end_col) {
    for (int c = start_col; c < end_col; c++)
        t->cell_buf[row * TTY_COLS + c] = make_cell(' ', t->fg, t->bg);
}

static void tty_clear_screen(struct tty *t) {
    for (int i = 0; i < TTY_ROWS * TTY_COLS; i++)
        t->cell_buf[i] = make_cell(' ', t->fg, t->bg);
    t->cursor_row = 0;
    t->cursor_col = 0;
}

static void tty_clear_to_end(struct tty *t) {
    /* Clear from cursor to end of line */
    for (int c = t->cursor_col; c < TTY_COLS; c++)
        t->cell_buf[t->cursor_row * TTY_COLS + c] = make_cell(' ', t->fg, t->bg);
}

static void tty_clear_to_start(struct tty *t) {
    /* Clear from start of line to cursor */
    for (int c = 0; c <= t->cursor_col; c++)
        t->cell_buf[t->cursor_row * TTY_COLS + c] = make_cell(' ', t->fg, t->bg);
}

/* ── VT100 Escape Sequence Parser ──────────────────────────────────── */

static void tty_parse_escape(struct tty *t, char ch) {
    switch (t->escape_state) {
    case 0:  /* Normal character */
        if (ch == '\033') {
            t->escape_state = 1;
            t->escape_param = 0;
            t->escape_param_count = 0;
        } else {
            /* Regular character - just output it */
            tty_putc(t->id, ch);
        }
        break;
        
    case 1:  /* ESC received */
        if (ch == '[') {
            t->escape_state = 2;
        } else if (ch == '7') {
            /* DECSC - Save cursor */
            t->saved_row = t->cursor_row;
            t->saved_col = t->cursor_col;
            t->escape_state = 0;
        } else if (ch == '8') {
            /* DECRC - Restore cursor */
            t->cursor_row = t->saved_row;
            t->cursor_col = t->saved_col;
            t->escape_state = 0;
        } else {
            t->escape_state = 0;
        }
        break;
        
    case 2:  /* ESC [ received - CSI sequence */
        if (ch >= '0' && ch <= '9') {
            t->escape_param = t->escape_param * 10 + (ch - '0');
        } else if (ch == ';') {
            if (t->escape_param_count < 8) {
                t->escape_params[t->escape_param_count++] = t->escape_param;
            }
            t->escape_param = 0;
        } else {
            /* Final character - execute sequence */
            int param = t->escape_param;
            if (t->escape_param_count > 0) {
                t->escape_params[t->escape_param_count] = t->escape_param;
                t->escape_param_count++;
            }
            
            switch (ch) {
            case 'A':  /* Cursor Up */
                t->cursor_row -= (param > 0 ? param : 1);
                if (t->cursor_row < 0) t->cursor_row = 0;
                break;
                
            case 'B':  /* Cursor Down */
                t->cursor_row += (param > 0 ? param : 1);
                if (t->cursor_row >= TTY_ROWS) t->cursor_row = TTY_ROWS - 1;
                break;
                
            case 'C':  /* Cursor Right */
                t->cursor_col += (param > 0 ? param : 1);
                if (t->cursor_col >= TTY_COLS) t->cursor_col = TTY_COLS - 1;
                break;
                
            case 'D':  /* Cursor Left */
                t->cursor_col -= (param > 0 ? param : 1);
                if (t->cursor_col < 0) t->cursor_col = 0;
                break;
                
            case 'H':  /* Cursor Position (row;col) */
            case 'f':
                if (t->escape_param_count >= 2) {
                    t->cursor_row = t->escape_params[0] - 1;
                    t->cursor_col = t->escape_params[1] - 1;
                } else {
                    t->cursor_row = 0;
                    t->cursor_col = 0;
                }
                if (t->cursor_row < 0) t->cursor_row = 0;
                if (t->cursor_row >= TTY_ROWS) t->cursor_row = TTY_ROWS - 1;
                if (t->cursor_col < 0) t->cursor_col = 0;
                if (t->cursor_col >= TTY_COLS) t->cursor_col = TTY_COLS - 1;
                break;
                
            case 'J':  /* Erase in Display */
                if (param == 0) tty_clear_to_end(t);
                else if (param == 1) tty_clear_to_start(t);
                else if (param == 2) tty_clear_screen(t);
                break;
                
            case 'K':  /* Erase in Line */
                if (param == 0) tty_clear_line(t, t->cursor_row, t->cursor_col, TTY_COLS);
                else if (param == 1) tty_clear_line(t, t->cursor_row, 0, t->cursor_col + 1);
                else if (param == 2) tty_clear_line(t, t->cursor_row, 0, TTY_COLS);
                break;
                
            case 'm':  /* Set Graphics Rendition (SGR) */
                if (t->escape_param_count == 0) {
                    /* ESC[m = reset */
                    t->fg = 7;
                    t->bg = 0;
                } else {
                    for (int i = 0; i < t->escape_param_count; i++) {
                        int p = t->escape_params[i];
                        if (p == 0) { t->fg = 7; t->bg = 0; }
                        else if (p == 1) { /* Bold - brighten foreground */ }
                        else if (p >= 30 && p <= 37) t->fg = (u8)(p - 30);
                        else if (p >= 40 && p <= 47) t->bg = (u8)(p - 40);
                        else if (p >= 90 && p <= 97) t->fg = (u8)(p - 90 + 8);
                        else if (p >= 100 && p <= 107) t->bg = (u8)(p - 100 + 8);
                    }
                }
                break;
                
            case 's':  /* Save cursor position */
                t->saved_row = t->cursor_row;
                t->saved_col = t->cursor_col;
                break;
                
            case 'u':  /* Restore cursor position */
                t->cursor_row = t->saved_row;
                t->cursor_col = t->saved_col;
                break;
                
            case 'n':  /* Device Status Report */
                /* Response: ESC[0n (OK) or ESC[6n (cursor position) */
                break;
                
            default:
                /* Unknown sequence - ignore */
                break;
            }
            
            t->escape_state = 0;
        }
        break;
    }
}

/* ── Character Output ──────────────────────────────────────────────── */

void tty_putc(int id, char ch) {
    if (id < 0 || id >= TTY_COUNT) return;
    struct tty *t = &ttys[id];
    
    /* Handle VT100 escape sequences */
    if (t->escape_state > 0) {
        tty_parse_escape(t, ch);
        return;
    }
    
    /* Handle control characters */
    if (ch == '\033') {
        t->escape_state = 1;
        return;
    }
    
    if (ch == '\n') {
        t->cursor_col = 0;
        t->cursor_row++;
        if (t->cursor_row >= TTY_ROWS) {
            tty_scroll_up(t);
            t->cursor_row = TTY_ROWS - 1;
        }
        return;
    }
    
    if (ch == '\r') {
        t->cursor_col = 0;
        return;
    }
    
    if (ch == '\t') {
        int spaces = t->tab_width - (t->cursor_col % t->tab_width);
        for (int i = 0; i < spaces && t->cursor_col < TTY_COLS; i++) {
            t->cell_buf[t->cursor_row * TTY_COLS + t->cursor_col] = 
                make_cell(' ', t->fg, t->bg);
            t->cursor_col++;
        }
        if (t->cursor_col >= TTY_COLS) {
            t->cursor_col = 0;
            t->cursor_row++;
            if (t->cursor_row >= TTY_ROWS) {
                tty_scroll_up(t);
                t->cursor_row = TTY_ROWS - 1;
            }
        }
        return;
    }
    
    if (ch == '\b') {
        if (t->cursor_col > 0) {
            t->cursor_col--;
            t->cell_buf[t->cursor_row * TTY_COLS + t->cursor_col] = 
                make_cell(' ', t->fg, t->bg);
        }
        return;
    }
    
    /* Printable character */
    if (ch >= ' ') {
        if (t->cursor_col >= TTY_COLS) {
            t->cursor_col = 0;
            t->cursor_row++;
            if (t->cursor_row >= TTY_ROWS) {
                tty_scroll_up(t);
                t->cursor_row = TTY_ROWS - 1;
            }
        }
        
        t->cell_buf[t->cursor_row * TTY_COLS + t->cursor_col] = 
            make_cell(ch, t->fg, t->bg);
        t->cursor_col++;
    }
}

void tty_puts(int id, const char *s) {
    if (!s) return;
    while (*s) {
        tty_putc(id, *s++);
    }
}

/* ── Character Input ───────────────────────────────────────────────── */

int tty_getc(int id) {
    if (id < 0 || id >= TTY_COUNT) return -1;
    struct tty *t = &ttys[id];
    
    if (t->in_count == 0) return -1;
    
    int ch = t->in_buf[t->in_tail];
    t->in_tail = (t->in_tail + 1) % TTY_IN_BUF_SIZE;
    t->in_count--;
    return ch;
}

int tty_getc_wait(int id) {
    if (id < 0 || id >= TTY_COUNT) return -1;
    struct tty *t = &ttys[id];
    
    while (t->in_count == 0) {
        proc_yield();
    }
    
    int ch = t->in_buf[t->in_tail];
    t->in_tail = (t->in_tail + 1) % TTY_IN_BUF_SIZE;
    t->in_count--;
    return ch;
}

void tty_putc_input(int id, char ch) {
    if (id < 0 || id >= TTY_COUNT) return;
    struct tty *t = &ttys[id];
    
    if (t->in_count >= TTY_IN_BUF_SIZE) return;  /* Buffer full */
    
    t->in_buf[t->in_head] = ch;
    t->in_head = (t->in_head + 1) % TTY_IN_BUF_SIZE;
    t->in_count++;
}

/* ── Line Discipline ───────────────────────────────────────────────── */

void tty_set_raw(int id, int raw) {
    if (id < 0 || id >= TTY_COUNT) return;
    ttys[id].raw_mode = raw;
}

void tty_set_echo(int id, int echo) {
    if (id < 0 || id >= TTY_COUNT) return;
    ttys[id].echo = echo;
}

/* ── Screen Composition ────────────────────────────────────────────── */

void tty_compose(void) {
    if (!tty_initialized) return;
    
    struct tty *t = &ttys[current_tty];
    
    /* Copy TTY cell buffer to VGA shadow buffer using vga_put_cell */
    for (int r = 0; r < TTY_ROWS; r++) {
        for (int c = 0; c < TTY_COLS; c++) {
            u16 cell = t->cell_buf[r * TTY_COLS + c];
            char ch = (char)(cell & 0xFF);
            u8 fg = (cell >> 8) & 0x0F;
            u8 bg = (cell >> 12) & 0x0F;
            vga_put_cell(r, c, ch, fg, bg);
        }
    }
}

/* ── Keyboard Handling ─────────────────────────────────────────────── */

void tty_handle_key(int key) {
    /* Check for Ctrl+Alt+F1-F4 to switch TTYs */
    /* This is handled at a higher level in the keyboard handler */
    
    struct tty *t = &ttys[current_tty];
    
    if (t->raw_mode) {
        /* Raw mode: pass all characters directly */
        if (key >= ' ' && key <= '~') {
            tty_putc_input(current_tty, (char)key);
        } else if (key == '\n' || key == '\r') {
            tty_putc_input(current_tty, '\n');
        } else if (key == '\b') {
            tty_putc_input(current_tty, '\b');
        }
    } else {
        /* Cooked mode: line editing */
        if (key == '\n' || key == '\r') {
            /* Echo newline */
            if (t->echo) {
                tty_putc(current_tty, '\r');
                tty_putc(current_tty, '\n');
            }
            /* Send entire line */
            while (t->in_count > 0) {
                /* Line is already in buffer */
                break;
            }
            /* Add newline to input */
            tty_putc_input(current_tty, '\n');
        } else if (key == '\b' || key == 0x7F) {
            /* Backspace */
            if (t->in_count > 0) {
                t->in_head = (t->in_head - 1 + TTY_IN_BUF_SIZE) % TTY_IN_BUF_SIZE;
                t->in_count--;
                if (t->echo) {
                    /* Move cursor back, print space, move back again */
                    tty_putc(current_tty, '\b');
                    tty_putc(current_tty, ' ');
                    tty_putc(current_tty, '\b');
                }
            }
        } else if (key >= ' ' && key <= '~') {
            /* Printable character */
            tty_putc_input(current_tty, (char)key);
            if (t->echo) {
                tty_putc(current_tty, (char)key);
            }
        } else if (key == '\t') {
            /* Tab */
            tty_putc_input(current_tty, '\t');
            if (t->echo) {
                tty_putc(current_tty, '\t');
            }
        } else if (key == 3) {
            /* Ctrl+C - Interrupt */
            tty_putc_input(current_tty, '\003');  /* ETX */
            if (t->echo) {
                tty_putc(current_tty, '^');
                tty_putc(current_tty, 'C');
                tty_putc(current_tty, '\n');
            }
        } else if (key == 4) {
            /* Ctrl+D - EOF */
            tty_putc_input(current_tty, '\004');  /* EOT */
        } else if (key == 26) {
            /* Ctrl+Z - Suspend */
            tty_putc_input(current_tty, '\032');  /* SUB */
        }
    }
}

/* ── Query Functions ───────────────────────────────────────────────── */

int tty_is_active(int id) {
    if (id < 0 || id >= TTY_COUNT) return 0;
    return ttys[id].active;
}

const char *tty_get_name(int id) {
    if (id < 0 || id >= TTY_COUNT) return "???";
    static const char *names[] = { "tty1", "tty2", "tty3", "tty4" };
    return names[id];
}

int tty_has_input(int id) {
    if (id < 0 || id >= TTY_COUNT) return 0;
    return ttys[id].in_count > 0;
}

int tty_is_initialized(void) { return tty_initialized; }
