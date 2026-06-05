/* pty.c -- Pseudoterminal (PTY) for Zenbite
 *
 * Provides a PTY mechanism so the graphical terminal widget can
 * run programs inline without going fullscreen. The PTY acts as
 * a virtual terminal that programs write to, and the terminal
 * widget reads from.
 *
 * Architecture:
 *   [Program] -> writes to PTY master -> PTY buffer -> [Terminal Widget]
 *   [Terminal Widget] -> writes to PTY slave -> input buffer -> [Program]
 */

#include "kernel.h"
#include "kio.h"
#include "string.h"
#include "proc.h"
#include "tty.h"

/* ── PTY Constants ─────────────────────────────────────────────────── */

#define PTY_COUNT       4       /* Number of PTYs */
#define PTY_BUF_SIZE    8192    /* Output buffer size */
#define PTY_IN_SIZE     256     /* Input buffer size */
#define PTY_ROWS        25
#define PTY_COLS        80

/* ── PTY Structure ─────────────────────────────────────────────────── */

struct pty {
    int id;                         /* PTY number (0..PTY_COUNT-1) */
    int in_use;                     /* Is this PTY allocated? */
    
    /* Output buffer (program -> terminal) */
    char out_buf[PTY_BUF_SIZE];
    int out_head;                   /* Write position */
    int out_tail;                   /* Read position */
    int out_count;                  /* Bytes available */
    
    /* Input buffer (terminal -> program) */
    char in_buf[PTY_IN_SIZE];
    int in_head;
    int in_tail;
    int in_count;
    
    /* Virtual screen state */
    u16 cell_buf[PTY_ROWS * PTY_COLS];
    int cursor_row;
    int cursor_col;
    u8 fg;
    u8 bg;
    
    /* VT100 state */
    int escape_state;
    int escape_param;
    int escape_params[8];
    int escape_param_count;
    int saved_row, saved_col;
    
    /* Settings */
    int raw_mode;                   /* 0=cooked, 1=raw */
    int echo;                       /* 1=echo input */
    
    /* Process association */
    int owner_pid;                  /* PID of process using this PTY (-1=none) */
};

/* ── Global State ──────────────────────────────────────────────────── */

static struct pty ptys[PTY_COUNT];
static int pty_initialized = 0;

/* ── Helper Functions ──────────────────────────────────────────────── */

static inline u16 make_cell(char ch, u8 fg, u8 bg) {
    return ((u16)(u8)ch) | ((u16)((fg & 0x0F) | ((bg & 0x0F) << 4)) << 8);
}

/* ── PTY Initialization ────────────────────────────────────────────── */

void pty_init(void) {
    for (int i = 0; i < PTY_COUNT; i++) {
        struct pty *p = &ptys[i];
        p->id = i;
        p->in_use = 0;
        p->out_head = 0;
        p->out_tail = 0;
        p->out_count = 0;
        p->in_head = 0;
        p->in_tail = 0;
        p->in_count = 0;
        p->cursor_row = 0;
        p->cursor_col = 0;
        p->fg = 7;
        p->bg = 0;
        p->escape_state = 0;
        p->escape_param = 0;
        p->escape_param_count = 0;
        p->saved_row = 0;
        p->saved_col = 0;
        p->raw_mode = 0;
        p->echo = 1;
        p->owner_pid = -1;
        
        /* Clear cell buffer */
        for (int j = 0; j < PTY_ROWS * PTY_COLS; j++)
            p->cell_buf[j] = make_cell(' ', 7, 0);
    }
    pty_initialized = 1;
    kprintf("pty: %d pseudoterminals initialized\n", PTY_COUNT);
}

/* ── PTY Allocation ────────────────────────────────────────────────── */

int pty_alloc(void) {
    for (int i = 0; i < PTY_COUNT; i++) {
        if (!ptys[i].in_use) {
            ptys[i].in_use = 1;
            ptys[i].owner_pid = -1;
            return i;
        }
    }
    return -1;  /* No free PTYs */
}

void pty_free(int id) {
    if (id < 0 || id >= PTY_COUNT) return;
    ptys[id].in_use = 0;
    ptys[id].owner_pid = -1;
    ptys[id].out_count = 0;
    ptys[id].in_count = 0;
}

/* ── Output (Program -> Terminal) ──────────────────────────────────── */

void pty_putc(int id, char ch) {
    if (id < 0 || id >= PTY_COUNT) return;
    struct pty *p = &ptys[id];
    
    if (!p->in_use) return;
    
    /* Add to output buffer */
    if (p->out_count < PTY_BUF_SIZE) {
        p->out_buf[p->out_head] = ch;
        p->out_head = (p->out_head + 1) % PTY_BUF_SIZE;
        p->out_count++;
    }
    
    /* Also update virtual screen state */
    if (ch == '\033') {
        p->escape_state = 1;
        p->escape_param = 0;
        p->escape_param_count = 0;
    } else if (p->escape_state > 0) {
        /* Handle escape sequences */
        if (p->escape_state == 1 && ch == '[') {
            p->escape_state = 2;
        } else if (p->escape_state == 2) {
            if (ch >= '0' && ch <= '9') {
                p->escape_param = p->escape_param * 10 + (ch - '0');
            } else if (ch == ';') {
                if (p->escape_param_count < 8)
                    p->escape_params[p->escape_param_count++] = p->escape_param;
                p->escape_param = 0;
            } else {
                /* Execute sequence */
                int param = p->escape_param;
                switch (ch) {
                case 'A': p->cursor_row -= (param > 0 ? param : 1); break;
                case 'B': p->cursor_row += (param > 0 ? param : 1); break;
                case 'C': p->cursor_col += (param > 0 ? param : 1); break;
                case 'D': p->cursor_col -= (param > 0 ? param : 1); break;
                case 'H': case 'f':
                    if (p->escape_param_count >= 2) {
                        p->cursor_row = p->escape_params[0] - 1;
                        p->cursor_col = p->escape_params[1] - 1;
                    } else {
                        p->cursor_row = 0;
                        p->cursor_col = 0;
                    }
                    break;
                case 'J':
                    if (param == 0) {
                        /* Clear to end */
                        for (int c = p->cursor_col; c < PTY_COLS; c++)
                            p->cell_buf[p->cursor_row * PTY_COLS + c] = make_cell(' ', p->fg, p->bg);
                    } else if (param == 2) {
                        /* Clear screen */
                        for (int i = 0; i < PTY_ROWS * PTY_COLS; i++)
                            p->cell_buf[i] = make_cell(' ', p->fg, p->bg);
                        p->cursor_row = 0;
                        p->cursor_col = 0;
                    }
                    break;
                case 'K':
                    for (int c = p->cursor_col; c < PTY_COLS; c++)
                        p->cell_buf[p->cursor_row * PTY_COLS + c] = make_cell(' ', p->fg, p->bg);
                    break;
                case 'm':
                    if (p->escape_param_count == 0) {
                        p->fg = 7; p->bg = 0;
                    } else {
                        for (int i = 0; i < p->escape_param_count; i++) {
                            int p2 = p->escape_params[i];
                            if (p2 == 0) { p->fg = 7; p->bg = 0; }
                            else if (p2 >= 30 && p2 <= 37) p->fg = (u8)(p2 - 30);
                            else if (p2 >= 40 && p2 <= 47) p->bg = (u8)(p2 - 40);
                        }
                    }
                    break;
                case 's': p->saved_row = p->cursor_row; p->saved_col = p->cursor_col; break;
                case 'u': p->cursor_row = p->saved_row; p->cursor_col = p->saved_col; break;
                }
                p->escape_state = 0;
            }
        } else {
            p->escape_state = 0;
        }
    } else if (ch == '\n') {
        p->cursor_col = 0;
        p->cursor_row++;
        if (p->cursor_row >= PTY_ROWS) {
            /* Scroll up */
            for (int r = 0; r < PTY_ROWS - 1; r++)
                for (int c = 0; c < PTY_COLS; c++)
                    p->cell_buf[r * PTY_COLS + c] = p->cell_buf[(r + 1) * PTY_COLS + c];
            for (int c = 0; c < PTY_COLS; c++)
                p->cell_buf[(PTY_ROWS - 1) * PTY_COLS + c] = make_cell(' ', p->fg, p->bg);
            p->cursor_row = PTY_ROWS - 1;
        }
    } else if (ch == '\r') {
        p->cursor_col = 0;
    } else if (ch == '\t') {
        int spaces = 8 - (p->cursor_col % 8);
        for (int i = 0; i < spaces && p->cursor_col < PTY_COLS; i++) {
            p->cell_buf[p->cursor_row * PTY_COLS + p->cursor_col] = make_cell(' ', p->fg, p->bg);
            p->cursor_col++;
        }
    } else if (ch == '\b') {
        if (p->cursor_col > 0) {
            p->cursor_col--;
            p->cell_buf[p->cursor_row * PTY_COLS + p->cursor_col] = make_cell(' ', p->fg, p->bg);
        }
    } else if (ch >= ' ') {
        /* Printable character */
        if (p->cursor_col >= PTY_COLS) {
            p->cursor_col = 0;
            p->cursor_row++;
            if (p->cursor_row >= PTY_ROWS) {
                /* Scroll up */
                for (int r = 0; r < PTY_ROWS - 1; r++)
                    for (int c = 0; c < PTY_COLS; c++)
                        p->cell_buf[r * PTY_COLS + c] = p->cell_buf[(r + 1) * PTY_COLS + c];
                for (int c = 0; c < PTY_COLS; c++)
                    p->cell_buf[(PTY_ROWS - 1) * PTY_COLS + c] = make_cell(' ', p->fg, p->bg);
                p->cursor_row = PTY_ROWS - 1;
            }
        }
        p->cell_buf[p->cursor_row * PTY_COLS + p->cursor_col] = make_cell(ch, p->fg, p->bg);
        p->cursor_col++;
    }
    
    /* Clamp cursor position */
    if (p->cursor_row < 0) p->cursor_row = 0;
    if (p->cursor_row >= PTY_ROWS) p->cursor_row = PTY_ROWS - 1;
    if (p->cursor_col < 0) p->cursor_col = 0;
    if (p->cursor_col >= PTY_COLS) p->cursor_col = PTY_COLS - 1;
}

void pty_puts(int id, const char *s) {
    if (!s) return;
    while (*s) {
        pty_putc(id, *s++);
    }
}

/* ── Input (Terminal -> Program) ───────────────────────────────────── */

int pty_getc(int id) {
    if (id < 0 || id >= PTY_COUNT) return -1;
    struct pty *p = &ptys[id];
    
    if (p->in_count == 0) return -1;
    
    int ch = p->in_buf[p->in_tail];
    p->in_tail = (p->in_tail + 1) % PTY_IN_SIZE;
    p->in_count--;
    return ch;
}

int pty_getc_wait(int id) {
    if (id < 0 || id >= PTY_COUNT) return -1;
    struct pty *p = &ptys[id];
    
    while (p->in_count == 0) {
        proc_yield();
    }
    
    int ch = p->in_buf[p->in_tail];
    p->in_tail = (p->in_tail + 1) % PTY_IN_SIZE;
    p->in_count--;
    return ch;
}

void pty_putc_input(int id, char ch) {
    if (id < 0 || id >= PTY_COUNT) return;
    struct pty *p = &ptys[id];
    
    if (p->in_count >= PTY_IN_SIZE) return;
    
    p->in_buf[p->in_head] = ch;
    p->in_head = (p->in_head + 1) % PTY_IN_SIZE;
    p->in_count++;
    
    /* Echo if enabled */
    if (p->echo && !p->raw_mode) {
        if (ch == '\n') {
            pty_putc(id, '\r');
            pty_putc(id, '\n');
        } else if (ch == '\b') {
            pty_putc(id, '\b');
            pty_putc(id, ' ');
            pty_putc(id, '\b');
        } else if (ch >= ' ') {
            pty_putc(id, ch);
        }
    }
}

/* ── Screen Access ─────────────────────────────────────────────────── */

u16 *pty_get_screen(int id) {
    if (id < 0 || id >= PTY_COUNT) return NULL;
    if (!ptys[id].in_use) return NULL;
    return ptys[id].cell_buf;
}

int pty_get_cursor_row(int id) {
    if (id < 0 || id >= PTY_COUNT) return 0;
    return ptys[id].cursor_row;
}

int pty_get_cursor_col(int id) {
    if (id < 0 || id >= PTY_COUNT) return 0;
    return ptys[id].cursor_col;
}

/* ── Settings ──────────────────────────────────────────────────────── */

void pty_set_raw(int id, int raw) {
    if (id < 0 || id >= PTY_COUNT) return;
    ptys[id].raw_mode = raw;
}

void pty_set_echo(int id, int echo) {
    if (id < 0 || id >= PTY_COUNT) return;
    ptys[id].echo = echo;
}

int pty_has_output(int id) {
    if (id < 0 || id >= PTY_COUNT) return 0;
    return ptys[id].out_count > 0;
}

int pty_has_input(int id) {
    if (id < 0 || id >= PTY_COUNT) return 0;
    return ptys[id].in_count > 0;
}

void pty_clear(int id) {
    if (id < 0 || id >= PTY_COUNT) return;
    struct pty *p = &ptys[id];
    p->out_count = 0;
    p->in_count = 0;
    for (int i = 0; i < PTY_ROWS * PTY_COLS; i++)
        p->cell_buf[i] = make_cell(' ', 7, 0);
    p->cursor_row = 0;
    p->cursor_col = 0;
}
