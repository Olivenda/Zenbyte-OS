#ifndef ZENBITE_VGA_H
#define ZENBITE_VGA_H

#include "types.h"

enum vga_colour {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW        = 14,
    VGA_WHITE         = 15,
};

void vga_init(void);
void vga_set_glyph(u8 code, const u8 bitmap[16]);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_set_colour(u8 fg, u8 bg);
void vga_get_colour(u8 *fg, u8 *bg);
void vga_set_cursor(u16 row, u16 col);
void vga_get_cursor(u16 *row, u16 *col);

/* Direct cell access for the TUI (setup wizard, full-screen UIs).
 *
 * VGA_COLS is fixed at 80 (the standard text width); VGA_ROWS is now
 * runtime-variable so the user can switch between 80x25 (16-pixel
 * font, BIOS default) and 80x50 (8-pixel font, double the rows). Code
 * uses `VGA_ROWS` like before -- the macro expands to the live
 * `vga_rows` global. Static buffers are sized for the larger mode. */
/* Max cell-grid dimensions. Sized for the BGA 1280x720 mode
 * (160x45 cells) plus a margin. Static buffers (shadow_buf,
 * prev_buf, etc.) reserve VGA_COLS_MAX * VGA_ROWS_MAX cells so
 * mode switches need no realloc. */
#define VGA_COLS_MAX 160
#define VGA_ROWS_MAX 50
extern int vga_rows;
extern int vga_cols_runtime;

/* Active viewport. When `vga_view_active()` is non-zero, every
 * vga_put_cell / vga_fill / vga_write / vga_set_cursor call treats
 * (row, col) as local to the viewport, translates it to the
 * underlying screen, and clips to the viewport's bounds. VGA_ROWS
 * and VGA_COLS then report the viewport's size, so apps that
 * consult them (like the editor's bottom status line at
 * VGA_ROWS-1) automatically render inside the box.
 *
 * Used by the Terminal widget to host fullscreen apps (evi, etc.)
 * inside its content area instead of letting them take over the
 * whole desktop. */
int  vga_view_active(void);
int  vga_view_rows(void);
int  vga_view_cols(void);
void vga_view_set(int r, int c, int rows, int cols);
void vga_view_clear(void);

#define VGA_COLS (vga_view_cols())
#define VGA_ROWS (vga_view_rows())

/* Switch the active text-mode height. `rows` must be 25 or 50.
 * Reprograms the CRTC Max-Scan-Line register (0x09) and clears the
 * screen. Returns 0 on success, -1 on bad input. */
int  vga_set_rows(int rows);
int  vga_get_rows(void);
int  vga_get_cols(void);

/* Switch to a real graphics framebuffer mode via Bochs VBE (BGA).
 * Returns 0 on success. The shadow text-cell buffer is reinterpreted
 * as a (w/8) x (h/16) grid and present-time bitmap-rendered through
 * the saved BIOS 8x16 font. After this call VGA_COLS / VGA_ROWS take
 * on the new cell dimensions (so 1280x720 = 160x45 cells). */
int  vga_set_graphics(int pixel_w, int pixel_h);
void vga_set_text_mode(int rows);  /* drop graphics mode if active */
int  vga_in_graphics_mode(void);
void vga_put_cell(int row, int col, char c, u8 fg, u8 bg);
void vga_fill   (int row, int col, int w, int h, char c, u8 fg, u8 bg);
void vga_write  (int row, int col, const char *s, u8 fg, u8 bg);
void vga_get_cell_raw(int row, int col, u8 *ch, u8 *attr);
void vga_hide_cursor(void);
void vga_show_cursor(void);

/* Double-buffered drawing: enable shadow mode, draw everything, then flush. */
void vga_shadow_enable(void);
void vga_shadow_flush(void);
/* Copy the shadow buffer to VGA memory atomically but keep shadow mode on.
 * Use this at the end of each frame to show what was drawn without tearing. */
void vga_present(void);
/* Force the next vga_present() to write every cell, not just the diff.
 * Use after anything that wrote directly to VGA behind the shadow
 * buffer's back (kprintf during a rescan, mode switch, ...) so the
 * stale cells get repainted from shadow_buf. */
void vga_invalidate(void);

/* Redirect kputs/kprintf into a caller-supplied buffer instead of the
 * VGA framebuffer. Used by the Terminal desktop app to capture shell
 * output. Pass buf=NULL to restore normal output. */
void vga_redirect(char *buf, u32 cap, u32 *len);

#endif
