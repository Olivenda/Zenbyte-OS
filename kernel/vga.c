#include "vga.h"
#include "io.h"
#include "string.h"

#define VGA_BUF   ((volatile u16 *)0xB8000)

/* Live text-mode height. vga_init() defaults this to 25; the user can
 * flip to 50 from the Settings widget via vga_set_rows(). Everything
 * else in the kernel reads VGA_ROWS, which is a macro that expands to
 * this variable. */
int vga_rows = 25;
int vga_cols_runtime = 80;

/* --- viewport (sub-window) state -------------------------------------
 * Used by the Terminal widget to host fullscreen apps inside its own
 * box. While a viewport is active, vga_put_cell / vga_fill / vga_write
 * / vga_set_cursor receive coordinates LOCAL to the viewport and
 * translate them to the underlying screen; out-of-view coords are
 * dropped. VGA_ROWS / VGA_COLS macros consult vga_view_rows /
 * _cols, so apps that compute their layout from those constants
 * (status line at VGA_ROWS-1, full-width fills at VGA_COLS, etc.)
 * automatically fit inside the viewport box. */
static int g_view_active;
static int g_view_r, g_view_c, g_view_rows, g_view_cols;

int  vga_view_active(void) { return g_view_active; }
int  vga_view_rows  (void) { return g_view_active ? g_view_rows : vga_rows; }
int  vga_view_cols  (void) { return g_view_active ? g_view_cols : vga_cols_runtime; }
void vga_view_set(int r, int c, int rows, int cols) {
    if (rows <= 0 || cols <= 0) return;
    g_view_r = r; g_view_c = c;
    g_view_rows = rows; g_view_cols = cols;
    g_view_active = 1;
}
void vga_view_clear(void) { g_view_active = 0; }

/* Translate local (row,col) -> absolute, returning 0 if out of view.
 * Always pass `vga_rows` / `vga_cols_runtime` (the real screen) into
 * buffer indexing, not the macros (which now return viewport size). */
static int view_translate(int *row, int *col) {
    if (!g_view_active) {
        if (*row < 0 || *row >= vga_rows) return 0;
        if (*col < 0 || *col >= vga_cols_runtime) return 0;
        return 1;
    }
    if (*row < 0 || *row >= g_view_rows) return 0;
    if (*col < 0 || *col >= g_view_cols) return 0;
    *row += g_view_r;
    *col += g_view_c;
    if (*row < 0 || *row >= vga_rows) return 0;
    if (*col < 0 || *col >= vga_cols_runtime) return 0;
    return 1;
}

/* BIOS-loaded 8x16 font, grabbed from VGA plane 2 at boot. The BGA
 * graphics-mode renderer uses it; in text mode the hardware character
 * generator does the rendering and this buffer just sits unused. */
static u8 saved_font[256 * 16];
static int saved_font_ok;
const u8 *vga_get_font(void) { return saved_font; }

static u16 cursor_row, cursor_col;
static u8  fg_colour = VGA_LIGHT_GREY;
static u8  bg_colour = VGA_BLACK;

/* Output redirection -- used by the Terminal desktop app. */
static char *redir_buf;
static u32   redir_cap;
static u32  *redir_len;

/* Shadow buffer for double-buffered drawing (eliminates flicker). Sized
 * for the maximum mode (80x50) so a runtime mode-switch never has to
 * realloc. */
static u16 shadow_buf[VGA_COLS_MAX * VGA_ROWS_MAX];
/* Last-presented buffer. vga_present() only writes the cells that
 * actually changed since the previous flush -- crucial on hypervisors
 * like VirtualBox/VMware that poll the VGA region at their own rate
 * and would otherwise catch a 2000-cell tight-loop copy mid-flight. */
static u16 prev_buf[VGA_COLS_MAX * VGA_ROWS_MAX];
static int  shadow_mode;

void vga_redirect(char *buf, u32 cap, u32 *len) {
    redir_buf = buf;
    redir_cap = cap;
    redir_len = len;
    if (len && buf) *len = 0;
}

void vga_shadow_enable(void) {
    shadow_mode = 1;
    /* Use the real screen dimensions, not VGA_COLS/VGA_ROWS which are
     * viewport-aware. The shadow buffer is sized for the full screen. */
    for (int i = 0; i < vga_cols_runtime * vga_rows; i++) {
        shadow_buf[i] = VGA_BUF[i];
        prev_buf[i]   = shadow_buf[i];
    }
}

void vga_shadow_flush(void) {
    for (int i = 0; i < vga_cols_runtime * vga_rows; i++) {
        VGA_BUF[i]  = shadow_buf[i];
        prev_buf[i] = shadow_buf[i];
    }
    shadow_mode = 0;
}

/* Spin until the VGA enters vertical retrace. Port 0x3DA bit 3 = 1
 * during vblank. Real CRT hardware (and most VirtualBox/VMware text
 * modes) commit framebuffer writes that land during retrace as a
 * single visual frame -- so a present that starts in vblank is tear-
 * and flicker-free. Bounded spins so we can't hang if the port is
 * dead (e.g. a serial-only console). */
static void wait_vblank(void) {
    /* If we're already in vblank, wait for it to end first so we
     * synchronize with the *start* of the next one. */
    for (int i = 0; i < 200000; i++)
        if (!(inb(0x3DA) & 0x08)) break;
    for (int i = 0; i < 200000; i++)
        if (inb(0x3DA) & 0x08) return;
}

extern int  bga_present_active(void);
extern void bga_draw_cell(int cell_row, int cell_col, u8 glyph, u8 attr);
extern void fb_present(void);

void vga_present(void) {
    if (!shadow_mode) return;
    /* Graphics-mode path: render every changed cell as an 8x16
     * bitmap glyph into the BGA framebuffer. The diff vs prev_buf
     * still keeps us from re-painting the whole screen each frame,
     * which matters because each cell is 128 framebuffer writes. */
    if (bga_present_active()) {
        for (int r = 0; r < VGA_ROWS; r++) {
            for (int c = 0; c < VGA_COLS; c++) {
                u16 s = shadow_buf[r * VGA_COLS + c];
                if (s != prev_buf[r * VGA_COLS + c]) {
                    bga_draw_cell(r, c, (u8)(s & 0xFF), (u8)((s >> 8) & 0xFF));
                    prev_buf[r * VGA_COLS + c] = s;
                }
            }
        }
        /* bga_draw_cell writes into the BGA back buffer now -- flush
         * the diff out to MMIO so the user actually sees the cell. */
        fb_present();
        return;
    }
    /* Text mode: copy changed cells to VGA memory at 0xB8000. */
    wait_vblank();
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        u16 s = shadow_buf[i];
        if (s != prev_buf[i]) {
            VGA_BUF[i] = s;
            prev_buf[i] = s;
        }
    }
}

void vga_invalidate(void) {
    /* Set prev_buf to a value guaranteed to differ from shadow_buf so
     * the next vga_present() writes every cell. We can't use 0x0000
     * because shadow may also be 0; flip a bit instead. */
    for (int i = 0; i < vga_cols_runtime * vga_rows; i++)
        prev_buf[i] = (u16)~shadow_buf[i];
}

static u16 entry(char c, u8 fg, u8 bg) {
    return (u16)c | ((u16)((bg << 4) | (fg & 0x0F)) << 8);
}

static void move_hw_cursor(void) {
    u16 pos = cursor_row * VGA_COLS + cursor_col;
    outb(0x3D4, 0x0F); outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

/* Disable VGA text-mode blink so attribute bit 7 means "bright
 * background colour" (16 BG colours) instead of "blink foreground".
 * BIOS defaults to blink-ON, which makes every cell with a bright
 * background (white = 15, light cyan = 11, etc.) blink at ~2 Hz.
 * That's the on/off/on/off "flicker" the user kept reporting -- it
 * was the hardware doing what BIOS asked, not a rendering bug.
 *
 * Procedure (VGA Attribute Controller, port 0x3C0):
 *   1. Read 0x3DA to reset the AC index/data flip-flop.
 *   2. Write the Mode Control register index (0x10) OR'd with 0x20
 *      so the palette stays connected to the display (otherwise the
 *      screen goes black for one frame).
 *   3. Read current value from 0x3C1.
 *   4. Clear bit 3 (Enable Blink), keep everything else.
 *   5. Write the new value back through 0x3C0.
 */
static void vga_disable_blink(void) {
    inb(0x3DA);
    outb(0x3C0, 0x10 | 0x20);
    u8 mode = inb(0x3C1);
    mode &= ~0x08;
    inb(0x3DA);
    outb(0x3C0, 0x10 | 0x20);
    outb(0x3C0, mode);
    inb(0x3DA);
    outb(0x3C0, 0x20);
}

/* Overwrite a CP437 glyph in the VGA font (plane 2). VGA stores
 * each glyph as 32 bytes -- the first 16 are the 8x16 bitmap, the
 * rest are padding. Procedure: switch the sequencer + GFX controller
 * to expose plane 2 at 0xA0000, write the bitmap at glyph*32,
 * restore the original plane configuration so text mode keeps
 * working. We only edit a single glyph at a time, so this is safe
 * to call repeatedly. */
void vga_set_glyph(u8 code, const u8 bitmap[16]) {
    /* Save current state of the registers we touch. */
    outb(0x3C4, 0x02); u8 seq_map     = inb(0x3C5);
    outb(0x3C4, 0x04); u8 seq_mode    = inb(0x3C5);
    outb(0x3CE, 0x04); u8 gfx_read    = inb(0x3CF);
    outb(0x3CE, 0x05); u8 gfx_mode    = inb(0x3CF);
    outb(0x3CE, 0x06); u8 gfx_misc    = inb(0x3CF);

    /* Map plane 2 (font plane) at 0xA0000 for read+write, no
     * odd/even chaining, no chain-4. */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04); /* write to plane 2 only */
    outb(0x3C4, 0x04); outb(0x3C5, 0x07); /* extended memory, no o/e */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02); /* read from plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00); /* write-mode 0, no o/e */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04); /* 0xA0000-0xAFFFF, no o/e */

    volatile u8 *font = (volatile u8 *)0xA0000;
    for (int i = 0; i < 16; i++) font[(u32)code * 32 + i] = bitmap[i];

    /* Restore. */
    outb(0x3C4, 0x02); outb(0x3C5, seq_map);
    outb(0x3C4, 0x04); outb(0x3C5, seq_mode);
    outb(0x3CE, 0x04); outb(0x3CF, gfx_read);
    outb(0x3CE, 0x05); outb(0x3CF, gfx_mode);
    outb(0x3CE, 0x06); outb(0x3CF, gfx_misc);
}

/* Install a Mac-/Windows-style arrow pointer at CP437 slot 0x01
 * (originally a smiley). The desktop's cursor renderer picks this
 * glyph when the "Arrow" cursor style is selected. */
static void install_arrow_glyph(void) {
    static const u8 arrow[16] = {
        0x80, /* #....... */
        0xC0, /* ##...... */
        0xA0, /* #.#..... */
        0x90, /* #..#.... */
        0x88, /* #...#... */
        0x84, /* #....#.. */
        0x82, /* #.....#. */
        0x81, /* #......# */
        0x82, /* #.....#. */
        0x86, /* #....##. */
        0x8A, /* #...#.#. */
        0xC8, /* ##..#... */
        0x44, /* .#...#.. */
        0x04, /* .....#.. */
        0x02, /* ......#. */
        0x02, /* ......#. */
    };
    vga_set_glyph(0x01, arrow);
}

/* Switch text mode between 80x25 (16-pixel glyph rows) and 80x50
 * (8-pixel rows -- doubles the usable height). The VGA hardware just
 * has to:
 *   1. Stop the sequencer (Reset register 0x00, value 0x01).
 *   2. Override the Max-Scan-Line register (CRTC index 0x09) -- low
 *      4 bits = scan-lines-per-glyph minus 1. 0x0F = 16-line font
 *      (25 rows), 0x07 = 8-line font (50 rows).
 *   3. For 50-row mode also load the BIOS 8x8 ROM font into plane 2
 *      so the glyphs themselves are tall enough. We approximate by
 *      down-sampling our existing 8x16 font -- skip every other row.
 *   4. Restart the sequencer and refresh the hardware cursor.
 * Returns 0 on success, -1 on bad input. */
static void install_8x8_font_downsampled(void);
static void install_arrow_glyph(void);
int vga_set_rows(int rows) {
    if (rows != 25 && rows != 50) return -1;
    if (rows == vga_rows) return 0;

    u8 max_scan = (rows == 50) ? 0x07 : 0x0F;

    /* CRTC index 0x09 (max scan line). Preserve the top 4 bits. */
    outb(0x3D4, 0x09);
    u8 cur = inb(0x3D5);
    outb(0x3D4, 0x09);
    outb(0x3D5, (cur & 0xE0) | max_scan);

    /* In 50-row mode we need a shorter font so the glyphs aren't
     * stretched. Patch the active 8x16 font down to 8x8. */
    if (rows == 50) install_8x8_font_downsampled();
    /* When dropping back to 25 rows the BIOS 8x16 font is already
     * loaded (we only ever overwrote the arrow at slot 0x01). */
    install_arrow_glyph();

    vga_rows = rows;
    if (cursor_row >= rows) cursor_row = rows - 1;
    /* Refresh the hardware cursor to a sensible position. */
    {
        u16 pos = cursor_row * VGA_COLS + cursor_col;
        outb(0x3D4, 0x0F); outb(0x3D5, (u8)(pos & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (u8)((pos >> 8) & 0xFF));
    }
    /* Clear so the user sees the new geometry immediately. */
    u16 blank = entry(' ', fg_colour, bg_colour);
    for (int i = 0; i < VGA_COLS * vga_rows; i++) VGA_BUF[i] = blank;
    for (int i = 0; i < VGA_COLS * VGA_ROWS_MAX; i++) {
        shadow_buf[i] = blank;
        prev_buf[i]   = blank;
    }
    return 0;
}

int vga_get_rows(void) { return vga_rows; }
int vga_get_cols(void) { return vga_cols_runtime; }

extern int  bga_init(int w, int h);
extern void bga_disable(void);

int vga_set_graphics(int pixel_w, int pixel_h) {
    if (!saved_font_ok) return -1;
    if (bga_init(pixel_w, pixel_h) < 0) return -1;
    vga_cols_runtime = pixel_w / 8;
    vga_rows         = pixel_h / 16;
    if (vga_cols_runtime > VGA_COLS_MAX) vga_cols_runtime = VGA_COLS_MAX;
    if (vga_rows         > VGA_ROWS_MAX) vga_rows         = VGA_ROWS_MAX;
    /* Force a full repaint on the next present. */
    u16 blank = entry(' ', 0, 0);
    for (int i = 0; i < VGA_COLS_MAX * VGA_ROWS_MAX; i++) {
        shadow_buf[i] = blank;
        prev_buf[i]   = (u16)~blank;
    }
    return 0;
}

void vga_set_text_mode(int rows) {
    if (bga_present_active()) {
        bga_disable();
        /* Restore text-mode rendering: arrow glyph + clear. The BGA
         * disable leaves us in the BIOS default 80x25 text mode. */
        install_arrow_glyph();
        vga_disable_blink();
    }
    vga_cols_runtime = 80;
    vga_set_rows(rows);
}

int vga_in_graphics_mode(void) { return bga_present_active(); }

/* Down-sample our active 8x16 font to 8x8 by copying every other row
 * into the first 8 bytes of each glyph slot. Source bytes 0,2,4,...,14
 * become destination bytes 0..7. Glyph slots 8..15 of each 32-byte
 * entry are left as-is (BIOS-patched memory). */
static void install_8x8_font_downsampled(void) {
    /* Re-use the plane-switch dance from vga_set_glyph; we read the
     * existing font from plane 2, build the new 8-byte form, write it
     * back, glyph by glyph. */
    outb(0x3C4, 0x02); u8 seq_map     = inb(0x3C5);
    outb(0x3C4, 0x04); u8 seq_mode    = inb(0x3C5);
    outb(0x3CE, 0x04); u8 gfx_read    = inb(0x3CF);
    outb(0x3CE, 0x05); u8 gfx_mode    = inb(0x3CF);
    outb(0x3CE, 0x06); u8 gfx_misc    = inb(0x3CF);
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);

    volatile u8 *font = (volatile u8 *)0xA0000;
    for (int g = 0; g < 256; g++) {
        u8 src[16];
        for (int i = 0; i < 16; i++) src[i] = font[(u32)g * 32 + i];
        for (int i = 0; i < 8; i++)  font[(u32)g * 32 + i] = src[i * 2];
    }

    outb(0x3C4, 0x02); outb(0x3C5, seq_map);
    outb(0x3C4, 0x04); outb(0x3C5, seq_mode);
    outb(0x3CE, 0x04); outb(0x3CF, gfx_read);
    outb(0x3CE, 0x05); outb(0x3CF, gfx_mode);
    outb(0x3CE, 0x06); outb(0x3CF, gfx_misc);
}

/* Snapshot the active BIOS 8x16 font from plane 2 into saved_font.
 * Done once at vga_init time so the BGA graphics renderer has glyph
 * data without us having to embed a 4 KiB hardcoded font. Reuses the
 * same plane-switch dance as vga_set_glyph but in the read direction. */
static void save_bios_font(void) {
    outb(0x3C4, 0x02); u8 seq_map     = inb(0x3C5);
    outb(0x3C4, 0x04); u8 seq_mode    = inb(0x3C5);
    outb(0x3CE, 0x04); u8 gfx_read    = inb(0x3CF);
    outb(0x3CE, 0x05); u8 gfx_mode    = inb(0x3CF);
    outb(0x3CE, 0x06); u8 gfx_misc    = inb(0x3CF);
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);
    volatile u8 *font = (volatile u8 *)0xA0000;
    for (int g = 0; g < 256; g++)
        for (int b = 0; b < 16; b++)
            saved_font[g * 16 + b] = font[g * 32 + b];
    outb(0x3C4, 0x02); outb(0x3C5, seq_map);
    outb(0x3C4, 0x04); outb(0x3C5, seq_mode);
    outb(0x3CE, 0x04); outb(0x3CF, gfx_read);
    outb(0x3CE, 0x05); outb(0x3CF, gfx_mode);
    outb(0x3CE, 0x06); outb(0x3CF, gfx_misc);
    saved_font_ok = 1;
}

void vga_init(void) {
    vga_disable_blink();
    save_bios_font();
    install_arrow_glyph();
    vga_clear();
}

void vga_clear(void) {
    /* Shadow-aware AND viewport-aware. When a viewport is active
     * (Terminal widget running a fullscreen app), `cls` only wipes
     * the widget's box, not the rest of the desktop. */
    if (g_view_active) {
        vga_fill(0, 0, vga_view_cols(), vga_view_rows(),
                 ' ', fg_colour, bg_colour);
        vga_set_cursor(0, 0);
        return;
    }
    u16 blank = entry(' ', fg_colour, bg_colour);
    u16 *target = shadow_mode ? shadow_buf : (u16 *)VGA_BUF;
    for (int i = 0; i < vga_cols_runtime * vga_rows; i++) target[i] = blank;
    cursor_row = cursor_col = 0;
    if (!shadow_mode) move_hw_cursor();
}

static void scroll_if_needed(void) {
    /* When inside a viewport (Terminal running a streaming command),
     * scrolling happens within the box: rows shift up inside the
     * widget interior, the bottom row clears. Outside the viewport
     * we scroll the whole screen as before. */
    int rows  = g_view_active ? g_view_rows : vga_rows;
    int cols  = g_view_active ? g_view_cols : vga_cols_runtime;
    int r_off = g_view_active ? g_view_r : 0;
    int c_off = g_view_active ? g_view_c : 0;
    if (cursor_row < r_off + rows) return;
    u16 *target = shadow_mode ? shadow_buf : (u16 *)VGA_BUF;
    for (int r = 0; r < rows - 1; r++)
        for (int c = 0; c < cols; c++)
            target[(r_off + r) * vga_cols_runtime + c_off + c]
              = target[(r_off + r + 1) * vga_cols_runtime + c_off + c];
    u16 blank = entry(' ', fg_colour, bg_colour);
    for (int c = 0; c < cols; c++)
        target[(r_off + rows - 1) * vga_cols_runtime + c_off + c] = blank;
    cursor_row = r_off + rows - 1;
}

void vga_putc(char ch) {
    if (redir_buf) {
        if (redir_len && *redir_len < redir_cap)
            redir_buf[(*redir_len)++] = ch;
        return;
    }
    /* In shadow mode (TUI / desktop) all writes go to shadow_buf, never
     * to VGA_BUF. A stray kprintf during a rescan or http_get would
     * otherwise leak boot-log text under the desktop windows -- the
     * diff-present has no way to know those cells became stale. */
    u16 *target = shadow_mode ? shadow_buf : (u16 *)VGA_BUF;
    /* Bounds for cursor motion. When a viewport is set (Terminal widget
     * running a streaming command), the cursor stays inside that box --
     * newlines wrap at the right edge of the box, scrolling shifts only
     * the box. */
    int left   = g_view_active ? g_view_c : 0;
    int right  = g_view_active ? g_view_c + g_view_cols : vga_cols_runtime;
    if (ch == '\n') {
        cursor_col = (u16)left;
        cursor_row++;
    } else if (ch == '\r') {
        cursor_col = (u16)left;
    } else if (ch == '\b') {
        if (cursor_col > left) cursor_col--;
        target[cursor_row * vga_cols_runtime + cursor_col] =
            entry(' ', fg_colour, bg_colour);
    } else if (ch == '\t') {
        cursor_col = (u16)(((cursor_col - left + 8) & ~7) + left);
        if (cursor_col >= right) { cursor_col = (u16)left; cursor_row++; }
    } else {
        target[cursor_row * vga_cols_runtime + cursor_col] =
            entry(ch, fg_colour, bg_colour);
        cursor_col++;
        if (cursor_col >= right) { cursor_col = (u16)left; cursor_row++; }
    }
    scroll_if_needed();
    if (!shadow_mode) move_hw_cursor();
}

void vga_puts(const char *s) {
    while (*s) vga_putc(*s++);
}

void vga_set_colour(u8 fg, u8 bg) {
    fg_colour = fg & 0x0F;
    bg_colour = bg & 0x0F;
}

void vga_get_colour(u8 *fg, u8 *bg) {
    if (fg) *fg = fg_colour;
    if (bg) *bg = bg_colour;
}

void vga_set_cursor(u16 row, u16 col) {
    int r = (int)row, c = (int)col;
    if (g_view_active) {
        /* Clamp to viewport, translate to absolute. */
        if (r >= g_view_rows) r = g_view_rows - 1;
        if (c >= g_view_cols) c = g_view_cols - 1;
        r += g_view_r; c += g_view_c;
    }
    cursor_row = (u16)r; cursor_col = (u16)c;
    move_hw_cursor();
}

void vga_get_cursor(u16 *row, u16 *col) {
    if (row) *row = cursor_row;
    if (col) *col = cursor_col;
}

/* --- direct cell write API for the TUI -------------------------------- */
/* Coordinates are LOCAL to the active viewport (or absolute if no
 * viewport is set). Buffer indexing uses the real screen width so
 * the shadow buffer layout stays consistent. */
void vga_put_cell(int row, int col, char c, u8 fg, u8 bg) {
    if (!view_translate(&row, &col)) return;
    u16 *target = shadow_mode ? shadow_buf : (u16 *)VGA_BUF;
    target[row * vga_cols_runtime + col] = (u16)(u8)c | ((u16)(((bg & 0x0F) << 4) | (fg & 0x0F)) << 8);
}

void vga_get_cell_raw(int row, int col, u8 *ch, u8 *attr) {
    if (!view_translate(&row, &col)) {
        if (ch)   *ch   = ' ';
        if (attr) *attr = 0x07;
        return;
    }
    u16 *source = shadow_mode ? shadow_buf : (u16 *)VGA_BUF;
    u16 v = source[row * vga_cols_runtime + col];
    if (ch)   *ch   = (u8)(v & 0xFF);
    if (attr) *attr = (u8)((v >> 8) & 0xFF);
}

void vga_fill(int row, int col, int w, int h, char c, u8 fg, u8 bg) {
    for (int r = row; r < row + h; r++)
        for (int x = col; x < col + w; x++)
            vga_put_cell(r, x, c, fg, bg);
}

void vga_write(int row, int col, const char *s, u8 fg, u8 bg) {
    int x = col;
    for (; *s && x < VGA_COLS; s++, x++) vga_put_cell(row, x, *s, fg, bg);
}

void vga_hide_cursor(void) {
    outb(0x3D4, 0x0A); outb(0x3D5, 0x20);
}

void vga_show_cursor(void) {
    outb(0x3D4, 0x0A); outb(0x3D5, 14);
    outb(0x3D4, 0x0B); outb(0x3D5, 15);
}
