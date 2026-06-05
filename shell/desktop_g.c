/* Zenbite "Slate" graphical desktop.
 *
 * Pixel-rendered desktop with Zenbite's own identity -- not a
 * Win95 clone. Runs when the BGA framebuffer is up (1280x720,
 * 32 bpp). Coexists with the classic cell-based desktop in
 * desktop.c -- launch this one from the shell with `gdesk`.
 *
 * Theme: "Slate"
 *   * Deep slate-blue background (#1E2A38).
 *   * Window body in cool off-white (#E8ECF1).
 *   * Title bar gradient indigo -> violet (#2C2152 -> #6447B0),
 *     amber title text (#FFC447). Inactive bars go cool grey.
 *   * 3D bevels keep the chunky retro feel but in muted slate
 *     greys instead of Win95 system grey.
 *   * Taskbar at the bottom is the same dark slate as the desktop
 *     with a thin amber accent stripe along the top edge.
 *   * Start-equivalent button reads "Zenbite" and shows a
 *     hand-drawn pixel Z + four amber accent squares -- the
 *     Zenbite mark, not a Microsoft flag.
 *
 * Mouse cursor: 11x16 white-fill + black-outline arrow sprite.
 *
 * App CONTENT inside windows still renders as 8x16 BIOS glyphs
 * (fb_blit_glyph). Pixel-porting the complex apps is staged work.
 */

#include "kernel.h"
#include "kio.h"
#include "vga.h"
#include "string.h"
#include "fs.h"

#include "net.h"
#include "disk.h"

extern void shell_run_line(const char *src);
extern void vga_redirect(char *buf, u32 cap, u32 *len);

/* Async shell (multitasking). */
extern int  shell_run_async(const char *line);
extern int  shell_async_is_done(void);
extern const char *shell_async_get_output(u32 *len);
extern void shell_async_kill(void);
extern int  shell_async_busy(void);

/* PTY declarations */
extern int pty_initialized;
extern int pty_alloc(void);
extern void pty_free(int id);
extern void pty_putc(int id, char ch);
extern void pty_puts(int id, const char *s);
extern u16 *pty_get_screen(int id);
extern int pty_get_cursor_row(int id);
extern int pty_get_cursor_col(int id);
extern void pty_clear(int id);

/* External primitives from kernel/drv/bga.c */
extern int   bga_present_active(void);
extern int   fb_w(void);
extern int   fb_h(void);
extern void  fb_pixel(int x, int y, u32 color);
extern void  fb_fill_rect(int x, int y, int w, int h, u32 color);
extern void  fb_hline(int x, int y, int w, u32 color);
extern void  fb_vline(int x, int y, int h, u32 color);
extern void  fb_blit_glyph(int x, int y, u8 glyph, u32 fg, u32 bg);
extern void  fb_draw_text(int x, int y, const char *s, u32 fg, u32 bg);
extern void  fb_blit_sprite(int x, int y, int w, int h,
                            const u8 *data, const u32 *palette);
extern void  fb_bevel_raised(int x, int y, int w, int h, u32 light, u32 dark);
extern void  fb_bevel_sunken(int x, int y, int w, int h, u32 light, u32 dark);
extern void  fb_hgradient(int x, int y, int w, int h, u32 left, u32 right);
extern void  fb_present(void);    /* diff-flush back buffer to MMIO */
#define FB_TRANSPARENT 0xFF000000u

extern int   vga_set_graphics(int pixel_w, int pixel_h);
extern void  vga_set_text_mode(int rows);
extern int   kb_trygetc(void);
extern int   kb_getc(void);
extern void  mouse_get(int *col, int *row, int *buttons);
extern void  mouse_set_bounds(int max_col, int max_row);
extern u32   pit_ticks(void);

/* === Zenbite "Classic" palette — Win95 / retro era =================== */
#define ZB_BG               0x008080   /* desktop: Win95 teal */
#define ZB_ACCENT           0xFFFF00   /* yellow selection highlight */
#define ZB_PANEL            0xC0C0C0   /* system grey window surface */
#define ZB_PANEL_LIGHT      0xFFFFFF   /* bevel highlight */
#define ZB_PANEL_DARK       0x808080   /* bevel shadow */
#define ZB_PANEL_DARKER     0x404040   /* deep shadow / border */
#define ZB_BORDER           0x000000   /* outer hairline */
#define ZB_TITLE_LEFT       0x000080   /* navy, Win95 active title left */
#define ZB_TITLE_RIGHT      0x1084D0   /* bright blue, active title right */
#define ZB_TITLE_TEXT       0xFFFFFF   /* white title text */
#define ZB_TITLE_INACT_L    0x808080   /* grey inactive title */
#define ZB_TITLE_INACT_R    0xA0A0A0
#define ZB_TITLE_INACT_TXT  0xC8C8C8
#define ZB_BLACK            0x000000
#define ZB_TASKBAR          0xC0C0C0   /* taskbar: system grey */
#define ZB_TASKBAR_TEXT     0x000000   /* black text on taskbar */

/* === Mouse cursor (12x17 arrow, 1-bpp + outline + drop shadow) ======== */
/* 0 = transparent, 1 = white, 2 = black, 3 = semi-dark shadow */
static const u8 cursor_arrow[17 * 12] = {
    2,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,0,0,0,0,0,0,0,
    2,1,2,0,0,0,0,0,0,0,0,0,
    2,1,1,2,0,0,0,0,0,0,0,0,
    2,1,1,1,2,0,0,0,0,0,0,0,
    2,1,1,1,1,2,0,0,0,0,0,0,
    2,1,1,1,1,1,2,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,
    2,1,1,1,1,1,1,1,1,2,0,0,
    2,1,1,1,1,1,1,1,1,1,2,0,
    2,1,1,1,1,1,2,2,2,2,2,3,
    2,1,1,2,1,1,2,0,0,3,3,0,
    2,1,2,0,2,1,1,2,0,0,3,0,
    2,2,0,0,2,1,1,2,0,0,0,0,
    0,0,0,0,0,2,1,1,2,3,0,0,
    0,0,0,0,0,2,2,2,2,3,0,0,
};
static const u32 cursor_palette[4] = { 0, 0xFFFFFF, 0x000000, 0x303040 };

/* === Window descriptor =================================================
 * Mirrors the cell-based widget table but in pixel coords.
 * state: 0 normal, 1 minimised (hidden from screen, lives in taskbar),
 *        2 maximised (filling the work area).
 * rx/ry/rw/rh capture the pre-maximise / pre-snap geometry so we can
 * restore the window when the user un-maxes or un-snaps it. */
#define G_MAX_WIN 8
#define GWIN_NORMAL 0
#define GWIN_MIN    1
#define GWIN_MAX    2
struct gwin {
    int  used;
    int  x, y, w, h;            /* outer pixel rect */
    int  rx, ry, rw, rh;        /* restore rect */
    int  state;
    int  z;                     /* z-order */
    int  focused;
    int  closable;
    int  kind;                  /* GWK_*, used by layout save/load */
    char title[32];
    void (*paint)(struct gwin *w);  /* draws the body */
};

/* Window kinds. Must stay stable across builds because they get
 * serialised into CONFIG.TXT for persistent layout. */
#define GWK_WELCOME  0
#define GWK_ABOUT    1
#define GWK_FILES    2
#define GWK_TERM     3
#define GWK_CALC     4
#define GWK_CLOCK    5
#define GWK_SYSMON   6
#define GWK_SETTINGS 7
#define GWK_SNAKE    8
#define GWK_NOTES    9
#define GWK_ACLOCK   10
#define GWK_CALENDAR 11
#define GWK_MINES    12
#define GWK_TETRIS   13
#define GWK_NETWORK  14
#define GWK_DISKS    15
#define GWK_BROWSER  16
static struct gwin g_wins[G_MAX_WIN];
static int g_next_z = 1;

/* Wallpaper style: 0 solid (legacy), 1 vertical gradient, 2 diagonal */
static int g_wallpaper_style = 1;
int  gdesk_get_wallpaper_style(void)        { return g_wallpaper_style; }
void gdesk_set_wallpaper_style(int v)       { g_wallpaper_style = v; }

/* Layout persistence: a compact string serialised into CONFIG.TXT.
 * Format: "k:x:y:w:h:s,k:x:y:w:h:s,..." -- one window per comma.
 * Empty means "no saved layout, spawn the welcome window". */
static char g_last_layout[384];
int gdesk_get_last_layout(char *out, int sz) {
    int n = 0;
    while (g_last_layout[n] && n < sz - 1) { out[n] = g_last_layout[n]; n++; }
    out[n] = '\0';
    return n;
}
void gdesk_set_last_layout(const char *s) {
    int n = 0;
    while (s && s[n] && n < (int)sizeof g_last_layout - 1) {
        g_last_layout[n] = s[n]; n++;
    }
    g_last_layout[n] = '\0';
}

static int gwin_at(int px, int py) {
    int hit = -1, hz = -1;
    for (int i = 0; i < G_MAX_WIN; i++) {
        if (!g_wins[i].used) continue;
        if (g_wins[i].state == GWIN_MIN) continue;   /* minimised: not on screen */
        struct gwin *w = &g_wins[i];
        if (px < w->x || px >= w->x + w->w) continue;
        if (py < w->y || py >= w->y + w->h) continue;
        if (w->z > hz) { hz = w->z; hit = i; }
    }
    return hit;
}

static void gwin_focus(int idx) {
    for (int i = 0; i < G_MAX_WIN; i++) g_wins[i].focused = (i == idx);
    if (idx >= 0) g_wins[idx].z = g_next_z++;
}

/* === Window chrome =================================================== */
#define TITLE_H 20
#define BORDER  2

/* Forward decls. */
static void draw_zenbite_mark(int x, int y, u32 fg);
static void paint_files   (struct gwin *w);
static void paint_term    (struct gwin *w);
static void paint_calc    (struct gwin *w);
static void paint_clock   (struct gwin *w);
static void paint_sysmon  (struct gwin *w);
static void paint_settings(struct gwin *w);
static void paint_snake   (struct gwin *w);
static void paint_notes   (struct gwin *w);
static void paint_aclock  (struct gwin *w);
static void paint_calendar(struct gwin *w);
static void paint_mines   (struct gwin *w);
static void paint_tetris  (struct gwin *w);
static void paint_network (struct gwin *w);
static void paint_disks   (struct gwin *w);
static void paint_browser (struct gwin *w);
static void paint_activity(struct gwin *w);

static void draw_window_chrome(struct gwin *w) {
    int x = w->x, y = w->y, ww = w->w, hh = w->h;
    /* Classic Win95 raised window: double bevel on a system-grey body,
     * gradient title bar, square sysmenu icon on the left, square
     * close button on the right. Keep the Zenbite indigo/violet for
     * the title so it stays "Zenbite", not Microsoft. */
    fb_fill_rect(x, y, ww, hh, ZB_PANEL);
    fb_bevel_raised(x, y, ww, hh, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    fb_bevel_raised(x + 1, y + 1, ww - 2, hh - 2, ZB_PANEL, ZB_PANEL_DARK);
    /* Title bar gradient. */
    u32 lt = w->focused ? ZB_TITLE_LEFT  : ZB_TITLE_INACT_L;
    u32 rt = w->focused ? ZB_TITLE_RIGHT : ZB_TITLE_INACT_R;
    fb_hgradient(x + 3, y + 3, ww - 6, TITLE_H - 3, lt, rt);
    /* Sysmenu icon: small raised square holding the Zenbite Z mark.
     * Win95 puts this on the left of every titlebar. */
    if (w->closable) {
        int sx = x + 5, sy = y + 4;
        fb_fill_rect(sx, sy, 16, 14, ZB_PANEL);
        fb_bevel_raised(sx, sy, 16, 14, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        draw_zenbite_mark(sx + 2, sy + 1, ZB_TITLE_LEFT);
    }
    fb_draw_text(x + 26, y + 5, w->title,
                 w->focused ? ZB_TITLE_TEXT : ZB_TITLE_INACT_TXT,
                 FB_TRANSPARENT);
    /* Win95 three-button row: [_] minimize  [□] maximize  [X] close */
    if (w->closable) {
        /* Minimize button — underscore glyph */
        int bx = x + ww - 56, by = y + 4;
        fb_fill_rect(bx, by, 16, 14, ZB_PANEL);
        fb_bevel_raised(bx, by, 16, 14, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        fb_fill_rect(bx + 4, by + 9, 8, 2, ZB_BLACK);  /* _ */
        /* Maximize button — square-outline glyph */
        bx = x + ww - 38;
        fb_fill_rect(bx, by, 16, 14, ZB_PANEL);
        fb_bevel_raised(bx, by, 16, 14, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        fb_hline(bx + 3, by + 3, 10, ZB_BLACK);   /* top edge (double thick) */
        fb_hline(bx + 3, by + 4, 10, ZB_BLACK);
        fb_hline(bx + 3, by + 11, 10, ZB_BLACK);  /* bottom edge */
        fb_vline(bx + 3, by + 3,  9, ZB_BLACK);   /* left edge */
        fb_vline(bx + 12, by + 3, 9, ZB_BLACK);   /* right edge */
        /* Close [X] button */
        bx = x + ww - 20;
        fb_fill_rect(bx, by, 16, 14, ZB_PANEL);
        fb_bevel_raised(bx, by, 16, 14, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        for (int i = 0; i < 6; i++) {
            fb_pixel(bx + 5 + i, by + 4 + i, ZB_BLACK);
            fb_pixel(bx + 10 - i, by + 4 + i, ZB_BLACK);
        }
    }
}


/* === Taskbar ========================================================= */
/* Taller, gradient-backed bar with a richer tray. Not a Win95 row of
 * grey buttons -- darker, flat, with amber accents and a clear
 * focused-window pill. */
#define TASKBAR_H 40
static int start_button_w = 110;

/* Hand-pixelled Zenbite Z mark: 12 wide x 12 tall. Inside the start
 * button. The wedge curves drawn from a 1-bpp stencil; rendered in
 * amber on indigo to match the title-bar palette. */
static const u8 zenbite_z_mark[12 * 12] = {
    0,1,1,1,1,1,1,1,1,1,1,0,
    0,1,1,1,1,1,1,1,1,1,1,0,
    0,0,0,0,0,0,0,0,0,1,1,0,
    0,0,0,0,0,0,0,0,1,1,0,0,
    0,0,0,0,0,0,0,1,1,0,0,0,
    0,0,0,0,0,0,1,1,0,0,0,0,
    0,0,0,0,0,1,1,0,0,0,0,0,
    0,0,0,0,1,1,0,0,0,0,0,0,
    0,0,0,1,1,0,0,0,0,0,0,0,
    0,0,1,1,0,0,0,0,0,0,0,0,
    0,1,1,1,1,1,1,1,1,1,1,1,
    0,1,1,1,1,1,1,1,1,1,1,1,
};

static void draw_zenbite_mark(int x, int y, u32 fg) {
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12; c++)
            if (zenbite_z_mark[r * 12 + c]) fb_pixel(x + c, y + r, fg);
}

static void draw_taskbar(void) {
    int W = fb_w(), H = fb_h();
    int by = H - TASKBAR_H;
    /* Classic Win95 taskbar: system-grey body with a raised bevel. */
    fb_fill_rect(0, by, W, TASKBAR_H, ZB_PANEL);
    fb_bevel_raised(0, by, W, TASKBAR_H, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    /* Zenbite start button: raised system-grey button with the Z mark
     * + "Zenbite" wordmark. Functionally the Start button, visually
     * the Zenbite identity. */
    int bw = start_button_w, bh = TASKBAR_H - 8;
    fb_fill_rect(4, by + 4, bw, bh, ZB_PANEL);
    fb_bevel_raised(4, by + 4, bw, bh, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    draw_zenbite_mark(12, by + 14, ZB_TITLE_LEFT);
    fb_draw_text(34, by + 16, "Zenbite", ZB_BLACK, FB_TRANSPARENT);
    /* Window buttons: raised when inactive, sunken when focused -- the
     * Win95 taskbar look. */
    int chip_x = start_button_w + 12;
    int tray_w = 156;
    for (int i = 0; i < G_MAX_WIN; i++) {
        if (!g_wins[i].used) continue;
        int chip_w = 156;
        if (chip_x + chip_w > W - tray_w - 12) break;
        fb_fill_rect(chip_x, by + 4, chip_w, bh, ZB_PANEL);
        if (g_wins[i].focused)
            fb_bevel_sunken(chip_x, by + 4, chip_w, bh,
                            ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        else
            fb_bevel_raised(chip_x, by + 4, chip_w, bh,
                            ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        /* Tiny coloured icon dot inside the button. */
        u32 tile = 0x6447B0;
        if (g_wins[i].paint == paint_files)       tile = 0xFFA831;
        else if (g_wins[i].paint == paint_term)   tile = 0x2E3B52;
        else if (g_wins[i].paint == paint_calc)   tile = 0x4FB37A;
        else if (g_wins[i].paint == paint_clock || g_wins[i].paint == paint_aclock)
                                                  tile = 0xE85A8C;
        else if (g_wins[i].paint == paint_sysmon) tile = 0x47A6D4;
        else if (g_wins[i].paint == paint_activity) tile = 0xD44747;
        else if (g_wins[i].paint == paint_browser)  tile = 0x47A6D4;
        else if (g_wins[i].paint == paint_settings) tile = 0x8A93A6;
        else if (g_wins[i].paint == paint_mines || g_wins[i].paint == paint_tetris ||
                 g_wins[i].paint == paint_snake) tile = 0x4FB37A;
        else if (g_wins[i].paint == paint_calendar) tile = 0xE85A8C;
        else if (g_wins[i].paint == paint_network)  tile = 0x47A6D4;
        else if (g_wins[i].paint == paint_disks)    tile = 0x8A93A6;
        int off = g_wins[i].focused ? 1 : 0;
        fb_fill_rect(chip_x + 8 + off, by + 12 + off, 14, 14, tile);
        fb_draw_text(chip_x + 28 + off, by + 14 + off, g_wins[i].title,
                     ZB_BLACK, FB_TRANSPARENT);
        chip_x += chip_w + 4;
    }
    /* System tray: sunken Win95-style panel with the clock. */
    int tray_x = W - tray_w - 4;
    fb_fill_rect(tray_x, by + 4, tray_w, bh, ZB_PANEL);
    fb_bevel_sunken(tray_x, by + 4, tray_w, bh,
                    ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    /* Status icons on the left of the tray. */
    fb_fill_rect(tray_x + 8,  by + 14, 12, 12, 0x4FB37A);   /* net OK */
    fb_fill_rect(tray_x + 24, by + 14, 12, 12, 0xFFA831);   /* disk */
    fb_fill_rect(tray_x + 40, by + 14, 12, 12, 0x47A6D4);   /* sound */
    struct rtc_time t; rtc_read(&t);
    char tm[8];
    ksnprintf(tm, sizeof tm, "%02u:%02u", (u32)t.hour, (u32)t.min);
    fb_draw_text(tray_x + 60, by + 8,  tm, ZB_BLACK, FB_TRANSPARENT);
    char dt[12];
    ksnprintf(dt, sizeof dt, "%02u/%02u/%02u",
              (u32)t.day, (u32)t.month, (u32)(t.year % 100));
    fb_draw_text(tray_x + 60, by + 22, dt, ZB_BLACK, FB_TRANSPARENT);
}

/* === Desktop background + icons ====================================== */
struct gicon {
    const char *label;
    u32 color;
    int x, y;
    int app;                    /* matches APP_* in desktop.c */
};
static struct gicon g_icons[] = {
    { "Files",     0xFFA831,  16,  0, 0 },
    { "Editor",    0x6447B0,  16,  0, 3 },
    { "Terminal",  0x2E3B52,  16,  0, 2 },
    { "Calc",      0x4FB37A,  16,  0, 4 },
    { "Clock",     0xE85A8C,  16,  0, 5 },
    { "SysMon",    0x47A6D4,  16,  0, 6 },
    { "Settings",  0x8A93A6,  16,  0, 8 },
    { "About",     0x2C2152,  16,  0, 9 },
};
#define G_ICON_COUNT (int)(sizeof g_icons / sizeof g_icons[0])
#define G_ICON_W 56
#define G_ICON_H 64

/* Compute icon Y positions dynamically based on screen height.
 * Called once at desktop start and on resolution changes. */
static void layout_icons(void) {
    int H = fb_h();
    int taskbar_h = 40;
    int avail = H - taskbar_h - 16;   /* space for icons */
    if (avail < G_ICON_COUNT * (G_ICON_H + 8))
        avail = G_ICON_COUNT * (G_ICON_H + 8);
    int spacing = (avail - G_ICON_COUNT * G_ICON_H) / (G_ICON_COUNT > 1 ? G_ICON_COUNT - 1 : 1);
    if (spacing < 8) spacing = 8;
    int y = 8;
    for (int i = 0; i < G_ICON_COUNT && y + G_ICON_H < H - taskbar_h; i++) {
        g_icons[i].y = y;
        y += G_ICON_H + spacing;
    }
    /* For remaining icons that don't fit, stack at bottom */
    for (int i = 0; i < G_ICON_COUNT; i++) {
        if (g_icons[i].y == 0) g_icons[i].y = y;
    }
}

static void draw_icon(struct gicon *ic, int selected) {
    int ix = ic->x, iy = ic->y;
    /* Selection halo: subtle slate ring, not Win95 inverse-video. */
    if (selected) {
        fb_fill_rect(ix - 2, iy - 2, G_ICON_W + 4, G_ICON_H + 4, 0x2E3C50);
        fb_bevel_sunken(ix - 2, iy - 2, G_ICON_W + 4, G_ICON_H + 4,
                        0x506478, ZB_BLACK);
    }
    /* 32x32 Zenbite badge: solid accent body with a darker
     * gradient lower-right, scaled-down Z mark in the centre,
     * tiny amber accent dot at the top-right corner. */
    int tx = ix + (G_ICON_W - 32) / 2;
    int ty = iy;
    fb_fill_rect(tx, ty, 32, 32, ic->color);
    /* Gradient shadow: lower-right gets darker for depth. */
    for (int r = 16; r < 32; r++) {
        u32 shade = (ic->color & 0xFEFEFE) >> 1;        /* 50% */
        fb_fill_rect(tx + r, ty + r, 32 - r, 1, shade);
    }
    fb_bevel_raised(tx, ty, 32, 32, ZB_PANEL_LIGHT, ZB_BLACK);
    /* Centred Z mark in amber on dark, with a thin frame. */
    fb_fill_rect(tx + 4, ty + 4, 24, 24, ZB_TITLE_LEFT);
    fb_bevel_sunken(tx + 4, ty + 4, 24, 24, 0x5546A8, ZB_BLACK);
    draw_zenbite_mark(tx + 10, ty + 10, ZB_ACCENT);
    /* Accent dot. */
    fb_fill_rect(tx + 27, ty + 3, 3, 3, ZB_ACCENT);
    /* Label: amber with a 1-pixel black shadow so it reads on
     * the slate desktop without being shouty. */
    int lx = ix + 4, ly = ty + 36;
    fb_draw_text(lx + 1, ly + 1, ic->label, ZB_BLACK, FB_TRANSPARENT);
    fb_draw_text(lx,     ly,     ic->label, ZB_ACCENT, FB_TRANSPARENT);
}

static int icon_hit(int px, int py) {
    for (int i = 0; i < G_ICON_COUNT; i++) {
        if (px < g_icons[i].x || px >= g_icons[i].x + G_ICON_W) continue;
        if (py < g_icons[i].y || py >= g_icons[i].y + G_ICON_H) continue;
        return i;
    }
    return -1;
}

/* === Built-in window painters ======================================= */
/* Welcome quick-start: gradient header + four big shortcut tiles
 * + a small hint row. Lives at icon row 1 in the icon table so
 * mouse click coords map cleanly to "open <app>" via the same
 * code path the desktop icons use. */
static void paint_welcome(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
    /* Gradient hero band. */
    fb_hgradient(bx + 6, by + 6, bw - 12, 64,
                 ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
    fb_bevel_raised(bx + 6, by + 6, bw - 12, 64,
                    0x7460C8, 0x18102E);
    draw_zenbite_mark(bx + 18, by + 28, ZB_ACCENT);
    fb_draw_text(bx + 50, by + 16, "Welcome to Zenbite 3.1",
                 ZB_ACCENT, FB_TRANSPARENT);
    fb_draw_text(bx + 50, by + 36, "Slate edition -- pick a shortcut below",
                 0xE8D8F4, FB_TRANSPARENT);
    /* Four shortcut tiles, evenly spaced. Each is a raised system-
     * grey button with a coloured square icon + label. */
    static const struct { const char *label; u32 color; } tiles[4] = {
        { "Files",    0xFFA831 },
        { "Editor",   0x6447B0 },
        { "Terminal", 0x2E3B52 },
        { "Settings", 0x8A93A6 },
    };
    int tw = (bw - 12 - 16 * 3) / 4;
    int th = bh - 96;
    if (th > 132) th = 132;
    for (int i = 0; i < 4; i++) {
        int tx = bx + 6 + i * (tw + 16);
        int ty = by + 80;
        fb_fill_rect(tx, ty, tw, th, ZB_PANEL);
        fb_bevel_raised(tx, ty, tw, th, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        /* Coloured Zenbite badge centered. */
        int ix = tx + (tw - 48) / 2;
        int iy = ty + 14;
        fb_fill_rect(ix, iy, 48, 48, tiles[i].color);
        fb_bevel_raised(ix, iy, 48, 48, ZB_PANEL_LIGHT, ZB_BLACK);
        fb_fill_rect(ix + 8, iy + 8, 32, 32, ZB_TITLE_LEFT);
        draw_zenbite_mark(ix + 18, iy + 18, ZB_ACCENT);
        int lbllen = (int)strlen(tiles[i].label);
        fb_draw_text(tx + (tw - lbllen * 8) / 2, ty + 72,
                     tiles[i].label, ZB_BLACK, FB_TRANSPARENT);
        fb_draw_text(tx + (tw - 14 * 8) / 2, ty + 96,
                     "click to open", 0x70808A, FB_TRANSPARENT);
    }
    /* Footer hint. */
    fb_draw_text(bx + 12, by + bh - 22,
                 "Drag title to move  -  Win key opens menu  -  ESC exits",
                 0x70808A, FB_TRANSPARENT);
}

static void paint_about(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
    /* Hero gradient banner across the top. */
    fb_hgradient(bx + 6, by + 6, bw - 12, 96,
                 ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
    fb_bevel_raised(bx + 6, by + 6, bw - 12, 96, 0x7460C8, 0x18102E);
    /* Large Z mark (scaled 5x = 60x60) in the banner. */
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12; c++)
            if (zenbite_z_mark[r * 12 + c])
                fb_fill_rect(bx + 22 + c * 5, by + 24 + r * 5,
                             5, 5, ZB_ACCENT);
    fb_draw_text(bx + 100, by + 22, "Zenbite 3.1",
                 ZB_ACCENT, FB_TRANSPARENT);
    fb_draw_text(bx + 100, by + 44, "Slate -- graphical edition",
                 0xE8D8F4, FB_TRANSPARENT);
    fb_draw_text(bx + 100, by + 66, "32-bit retro OS, from scratch",
                 0xC8D4E0, FB_TRANSPARENT);
    /* Info card below. */
    int cy = by + 116;
    fb_draw_text(bx + 22, cy + 8,
                 "Kernel:   freestanding i686, ~264 KiB",
                 ZB_BLACK, FB_TRANSPARENT);
    fb_draw_text(bx + 22, cy + 28,
                 "Display:  Bochs VBE 32bpp framebuffer",
                 ZB_BLACK, FB_TRANSPARENT);
    fb_draw_text(bx + 22, cy + 48,
                 "Font:     BIOS 8x16, saved at boot",
                 ZB_BLACK, FB_TRANSPARENT);
    fb_draw_text(bx + 22, cy + 68,
                 "Storage:  FAT12/16/32 + MBR view-disks",
                 ZB_BLACK, FB_TRANSPARENT);
    fb_draw_text(bx + 22, cy + 88,
                 "Network:  e1000 + NE2000, IPv4 / TCP / HTTP",
                 ZB_BLACK, FB_TRANSPARENT);
    /* Footer signature. */
    fb_fill_rect(bx + 6, by + bh - 28, bw - 12, 22, ZB_TITLE_LEFT);
    fb_draw_text(bx + 16, by + bh - 25,
                 "(c) 2026 Zenbite contributors -- MIT License",
                 ZB_ACCENT, FB_TRANSPARENT);
}

/* === Pixel-ported app: Calculator ===================================== */
static int  calc_a, calc_b, calc_op_active;
static char calc_input[16];
static int  calc_input_len;
static int  calc_op;     /* '+','-','*','/' */
static int  calc_result;
static int  calc_show_result;

static int parse_calc_int(const char *s) {
    int v = 0, sign = 1, i = 0;
    if (s[0] == '-') { sign = -1; i = 1; }
    for (; s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
    return v * sign;
}

static void calc_key(int k) {
    if (k >= '0' && k <= '9') {
        if (calc_input_len < (int)sizeof calc_input - 1) {
            calc_input[calc_input_len++] = (char)k;
            calc_input[calc_input_len] = 0;
        }
        calc_show_result = 0;
        return;
    }
    if (k == 8 && calc_input_len > 0) {                    /* Backspace */
        calc_input[--calc_input_len] = 0;
        return;
    }
    if (k == 'c' || k == 'C' || k == 27) {                 /* Clear / ESC */
        calc_input_len = 0; calc_input[0] = 0;
        calc_op_active = 0; calc_show_result = 0;
        return;
    }
    if (k == '+' || k == '-' || k == '*' || k == '/' || k == '%') {
        calc_a = parse_calc_int(calc_input);
        calc_op = k;
        calc_op_active = 1;
        calc_input_len = 0; calc_input[0] = 0;
        calc_show_result = 0;
        return;
    }
    if (k == '=' || k == 10 || k == 13) {
        if (!calc_op_active) return;
        calc_b = parse_calc_int(calc_input);
        switch (calc_op) {
        case '+': calc_result = calc_a + calc_b; break;
        case '-': calc_result = calc_a - calc_b; break;
        case '*': calc_result = calc_a * calc_b; break;
        case '/': calc_result = calc_b ? calc_a / calc_b : 0; break;
        case '%': calc_result = calc_b ? calc_a % calc_b : 0; break;
        }
        calc_show_result = 1;
        calc_op_active = 0;
        calc_input_len = 0; calc_input[0] = 0;
        return;
    }
}

/* 4x5 button grid layout. The keys are addressable by row,col so a
 * click in the body area can map back to a key press. */
static const char calc_keys[5][4] = {
    { 'C', '/', '*', 8 },
    { '7', '8', '9', '-' },
    { '4', '5', '6', '+' },
    { '1', '2', '3', '=' },
    { '0', '0', '%', '=' },   /* 0 spans two cols, % = modulo */
};

static void paint_calc(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
    /* Display: black LCD-like panel with the current input in amber. */
    int dx = bx + 10, dy = by + 10, dw = bw - 20, dh = 50;
    fb_fill_rect(dx, dy, dw, dh, 0x0A1420);
    fb_bevel_sunken(dx, dy, dw, dh, 0x33495E, ZB_BLACK);
    char buf[24];
    if (calc_show_result) {
        ksnprintf(buf, sizeof buf, "= %d", calc_result);
    } else if (calc_op_active) {
        ksnprintf(buf, sizeof buf, "%c %s",
                  (char)calc_op,
                  calc_input[0] ? calc_input : "");
    } else if (calc_input[0]) {
        ksnprintf(buf, sizeof buf, "%s", calc_input);
    } else {
        ksnprintf(buf, sizeof buf, "0");
    }
    int n = (int)strlen(buf);
    fb_draw_text(dx + dw - 16 - n * 8, dy + 22, buf,
                 ZB_ACCENT, FB_TRANSPARENT);
    /* Button grid. 4 cols x 5 rows. */
    int gx = bx + 10, gy = by + 70;
    int gw = bw - 20, gh = bh - 80;
    int cw = gw / 4;
    int chh = gh / 5;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            char k = calc_keys[r][c];
            int rx = gx + c * cw + 2;
            int ry = gy + r * chh + 2;
            int rw = cw - 4, rhh = chh - 4;
            /* Colour code: ops amber, equals violet, C pink. */
            u32 bg = ZB_PANEL;
            u32 fg = ZB_BLACK;
            if (k == '=' )                  { bg = 0x6447B0; fg = 0xFFFFFF; }
            else if (k == 'C')              { bg = 0xE85A8C; fg = 0xFFFFFF; }
            else if (k == '+' || k == '-' ||
                     k == '*' || k == '/' ||
                     k == '%')              { bg = 0xFFA831; }
            else if (k == 8)                { bg = 0xC8D4E0; }
            fb_fill_rect(rx, ry, rw, rhh, bg);
            fb_bevel_raised(rx, ry, rw, rhh, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
            /* Glyph: most keys are their character, BACKSPACE shows <-. */
            char lbl[4];
            if (k == 8)         { lbl[0] = '<'; lbl[1] = '-'; lbl[2] = 0; }
            else                { lbl[0] = k;   lbl[1] = 0; }
            int llen = (int)strlen(lbl);
            fb_draw_text(rx + (rw - llen * 8) / 2,
                         ry + (rhh - 16) / 2,
                         lbl, fg, FB_TRANSPARENT);
        }
    }
}

/* Called from the main loop when a left click lands inside a calc
 * window's body. Translates pixel coords to a calc_keys entry and
 * routes through calc_key() so mouse + keyboard share state. */
static void calc_click(struct gwin *w, int mx, int my) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    int gx = bx + 10, gy = by + 70;
    int gw = bw - 20, gh = bh - 80;
    int cw = gw / 4, chh = gh / 5;
    if (mx < gx || mx >= gx + gw || my < gy || my >= gy + gh) return;
    int c = (mx - gx) / cw;
    int r = (my - gy) / chh;
    if (r < 0 || r > 4 || c < 0 || c > 3) return;
    char k = calc_keys[r][c];
    /* '.' was replaced with '%' (modulo) which is a valid operator. */
    calc_key((int)(u8)k);
}

/* === Pixel-ported app: Notes (simple scratchpad) ===================== */
static char notes_buf[512];
static int  notes_len;
static char notes_filename[128];   /* current open file; empty = NOTES.TXT */
static void notes_save(void);
static void notes_load(void);
static int  notes_saved_msg_until;
static int  settings_prompt(const char *prompt, char *out, int outsz); /* fwd */
static int  gwin_spawn(int x, int y, int w, int h, const char *title,
                       void (*paint)(struct gwin *));  /* fwd */
static void gdesk_notify(const char *title, const char *body, int kind);  /* fwd */
static void mark_wallpaper_dirty(void); /* fwd */
static void lock_run(void);             /* fwd */

/* Undo ring: snapshot the buffer before each edit. Up to 8 snapshots
 * = 4 KiB, plenty for a scratchpad. */
#define NOTES_UNDO 8
static char notes_undo_buf[NOTES_UNDO][512];
static int  notes_undo_len[NOTES_UNDO];
static int  notes_undo_head;
static int  notes_undo_count;

static void notes_push_undo(void) {
    int slot = notes_undo_head;
    for (int i = 0; i < notes_len; i++) notes_undo_buf[slot][i] = notes_buf[i];
    notes_undo_len[slot] = notes_len;
    notes_undo_head = (notes_undo_head + 1) % NOTES_UNDO;
    if (notes_undo_count < NOTES_UNDO) notes_undo_count++;
}
static void notes_pop_undo(void) {
    if (notes_undo_count == 0) return;
    notes_undo_head = (notes_undo_head + NOTES_UNDO - 1) % NOTES_UNDO;
    notes_undo_count--;
    int slot = notes_undo_head;
    for (int i = 0; i < notes_undo_len[slot]; i++)
        notes_buf[i] = notes_undo_buf[slot][i];
    notes_len = notes_undo_len[slot];
    notes_buf[notes_len] = '\0';
}

static void notes_key(int k) {
    if (k == 0x13) {                            /* Ctrl+S = save */
        notes_save();
        notes_saved_msg_until = (int)pit_ticks() + 2000;
        const char *fn = notes_filename[0] ? notes_filename : "NOTES.TXT";
        char msg[80]; ksnprintf(msg, sizeof msg, "Saved %s (%d bytes)", fn, notes_len);
        gdesk_notify("Editor", msg, 1);
        return;
    }
    if (k == 0x0C) { notes_load(); return; }   /* Ctrl+L = reload */
    if (k == 0x0F) {               /* Ctrl+O = open file by path */
        char path[128]; path[0] = '\0';
        if (settings_prompt("Open file (e.g. A:\\FOO.TXT):", path, sizeof path) && path[0]) {
            int j = 0; while (path[j]) { notes_filename[j] = path[j]; j++; }
            notes_filename[j] = '\0';
            notes_load();
            notes_undo_count = 0; notes_undo_head = 0;
        }
        return;
    }
    if (k == 0x1A) {               /* Ctrl+Z = undo */
        if (notes_undo_count == 0) {
            gdesk_notify("Editor", "Nothing to undo", 2);
        } else {
            notes_pop_undo();
            gdesk_notify("Editor", "Undid last change", 0);
        }
        return;
    }
    if (k == 0x06) {               /* Ctrl+F = find */
        char needle[64]; needle[0] = '\0';
        if (!settings_prompt("Find:", needle, sizeof needle) || !needle[0]) return;
        int nl = 0; while (needle[nl]) nl++;
        for (int i = 0; i + nl <= notes_len; i++) {
            int match = 1;
            for (int j = 0; j < nl; j++) {
                if (notes_buf[i + j] != needle[j]) { match = 0; break; }
            }
            if (match) {
                char msg[80];
                ksnprintf(msg, sizeof msg, "Found at byte %d", i);
                gdesk_notify("Editor", msg, 1);
                return;
            }
        }
        gdesk_notify("Editor", "Not found", 2);
        return;
    }
    if (k == 0x03) {               /* Ctrl+C = copy whole buffer */
        clipboard_set(notes_buf, notes_len);
        char msg[40];
        ksnprintf(msg, sizeof msg, "%d bytes to clipboard", notes_len);
        gdesk_notify("Editor", msg, 0);
        return;
    }
    if (k == 0x16) {               /* Ctrl+V = paste at end */
        notes_push_undo();
        char tmp[512];
        int n = clipboard_get(tmp, sizeof tmp);
        for (int i = 0; i < n && notes_len + 1 < (int)sizeof notes_buf; i++) {
            notes_buf[notes_len++] = tmp[i];
        }
        notes_buf[notes_len] = '\0';
        return;
    }
    if (k == 8) {                               /* BS deletes last char */
        if (notes_len > 0) {
            notes_push_undo();
            notes_buf[--notes_len] = 0;
        }
        return;
    }
    if (k == 27) {                              /* ESC = do nothing (avoid
                                                 * accidental buffer clear) */
        return;
    }
    if (k == 10 || k == 13) {
        if (notes_len + 1 < (int)sizeof notes_buf) {
            notes_push_undo();
            notes_buf[notes_len++] = '\n';
            notes_buf[notes_len] = 0;
        }
        return;
    }
    if (k >= 32 && k < 127 && notes_len + 1 < (int)sizeof notes_buf) {
        notes_push_undo();
        notes_buf[notes_len++] = (char)k;
        notes_buf[notes_len] = 0;
    }
}
static void paint_notes(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
    /* Pad area: white inside a sunken border, like a real text field. */
    fb_fill_rect(bx + 8, by + 8, bw - 16, bh - 36, ZB_PANEL_LIGHT);
    fb_bevel_sunken(bx + 8, by + 8, bw - 16, bh - 36,
                    0xC4C8CC, ZB_BLACK);
    /* Wrap-render the text. 8px per char, 16px per line. */
    int cw = (bw - 24) / 8;
    int x = bx + 12, y = by + 12;
    for (int i = 0; i < notes_len; i++) {
        char c = notes_buf[i];
        if (c == '\n') { x = bx + 12; y += 16; continue; }
        fb_blit_glyph(x, y, (u8)c, ZB_BLACK, FB_TRANSPARENT);
        x += 8;
        if (x >= bx + 12 + cw * 8) { x = bx + 12; y += 16; }
        if (y >= by + bh - 32) break;
    }
    /* Cursor (underscore at the next write position). */
    fb_fill_rect(x, y + 14, 7, 2, ZB_TITLE_LEFT);
    /* Status line: show filename + key hints. */
    char status[120];
    int saved = ((int)pit_ticks() < notes_saved_msg_until);
    const char *fn = notes_filename[0] ? notes_filename : "NOTES.TXT";
    ksnprintf(status, sizeof status,
              "%s  %d/%d  ^S save ^Z undo ^F find ^C/^V clip%s",
              fn, notes_len, (int)sizeof notes_buf - 1,
              saved ? "  [saved]" : "");
    fb_draw_text(bx + 12, by + bh - 20, status,
                 saved ? 0x2C7A2C : ZB_TITLE_LEFT, FB_TRANSPARENT);
}


/* === Pixel-ported app: Files ========================================== */
/* Browses the current drive's working directory. Up/Down moves the
 * selection; ENTER on a directory chdirs into it (".." goes up). The
 * fs layer's CWD is shared with the shell, so navigating here also
 * changes the prompt the next time the user opens Terminal. */
#define FILES_MAX 64
static struct fs_dirent files_list[FILES_MAX];
static int  files_count;
static int  files_sel;
static int  files_scroll;
static char files_cwd[FS_PATH_MAX];

static void files_refresh(void) {
    files_count = 0;
    files_sel = 0;
    files_scroll = 0;
    int h = fs_opendir(".");
    if (h < 0) return;
    struct fs_dirent de;
    while (files_count < FILES_MAX && fs_readdir(h, &de) > 0) {
        if (strcmp(de.name, ".") == 0) continue;   /* skip self-ref */
        files_list[files_count++] = de;
    }
    fs_closedir(h);
    /* Stash current drive + cwd for the header. */
    const char *cwd = fs_cwd();
    char drv = fs_get_drive();
    int n = 0;
    if (drv != '?') {
        files_cwd[n++] = drv;
        files_cwd[n++] = ':';
    }
    for (int i = 0; cwd[i] && n < (int)sizeof files_cwd - 1; i++)
        files_cwd[n++] = cwd[i];
    files_cwd[n] = '\0';
}

static void files_key(int k) {
    if (files_count == 0) return;
    if (k == KB_UP) {
        if (files_sel > 0) files_sel--;
    } else if (k == KB_DOWN) {
        if (files_sel + 1 < files_count) files_sel++;
    } else if (k == KB_PGUP) {
        files_sel -= 10; if (files_sel < 0) files_sel = 0;
    } else if (k == KB_PGDN) {
        files_sel += 10;
        if (files_sel >= files_count) files_sel = files_count - 1;
    } else if (k == KB_HOME) {
        files_sel = 0;
    } else if (k == KB_END) {
        files_sel = files_count - 1;
    } else if (k == 10 || k == 13) {
        struct fs_dirent *e = &files_list[files_sel];
        if (e->attr & FS_ATTR_DIR) {
            if (fs_chdir(e->name) == 0) files_refresh();
        } else {
            /* Open file in Editor: build full path, load it, spawn editor. */
            int j = 0;
            const char *p = files_cwd;
            while (*p && j < (int)sizeof notes_filename - 2) notes_filename[j++] = *p++;
            notes_filename[j++] = '\\';
            p = e->name;
            while (*p && j < (int)sizeof notes_filename - 1) notes_filename[j++] = *p++;
            notes_filename[j] = '\0';
            notes_load();
            gwin_spawn(160, 110, 560, 360, "Editor", paint_notes);
        }
    } else if (k == 8 || k == 0x7F) {
        /* Backspace: navigate up one level. */
        if (fs_chdir("..") == 0) files_refresh();
    } else if (k == KB_DEL) {
        /* Delete the selected entry. Refuses on directories (FAT
         * doesn't recursively unlink) and on "..". Toasts confirm. */
        struct fs_dirent *e = &files_list[files_sel];
        if (strcmp(e->name, "..") == 0) {
            gdesk_notify("Files", "Can't delete '..'", 2);
            return;
        }
        if (e->attr & FS_ATTR_DIR) {
            gdesk_notify("Files",
                         "Use shell RD to remove directories", 2);
            return;
        }
        char nm[FS_PATH_MAX]; int j = 0;
        while (e->name[j] && j < (int)sizeof nm - 1) {
            nm[j] = e->name[j]; j++;
        }
        nm[j] = '\0';
        if (fs_unlink(nm) == 0) {
            char msg[80];
            ksnprintf(msg, sizeof msg, "Deleted %s", nm);
            gdesk_notify("Files", msg, 1);
            files_refresh();
            if (files_sel >= files_count) files_sel = files_count - 1;
            if (files_sel < 0) files_sel = 0;
        } else {
            gdesk_notify("Files", "Delete failed", 3);
        }
    } else if (k == 'n' || k == 'N') {
        /* N = new empty file at the current dir. Prompts for name. */
        char nm[64]; nm[0] = '\0';
        if (settings_prompt("New file name:", nm, sizeof nm) && nm[0]) {
            if (fs_create(nm) == 0) {
                char msg[80];
                ksnprintf(msg, sizeof msg, "Created %s", nm);
                gdesk_notify("Files", msg, 1);
                files_refresh();
            } else {
                gdesk_notify("Files", "Create failed", 3);
            }
        }
    } else if (k == 'm' || k == 'M') {
        /* M = make directory. Prompts for name. */
        char nm[64]; nm[0] = '\0';
        if (settings_prompt("New folder name:", nm, sizeof nm) && nm[0]) {
            if (fs_mkdir(nm) == 0) {
                char msg[80];
                ksnprintf(msg, sizeof msg, "Created %s", nm);
                gdesk_notify("Files", msg, 1);
                files_refresh();
            } else {
                gdesk_notify("Files", "Mkdir failed", 3);
            }
        }
    } else if (k == 'r' || k == 'R') {
        /* R = rename. Old name comes from selection. */
        struct fs_dirent *e = &files_list[files_sel];
        if (strcmp(e->name, "..") == 0) return;
        char oldn[64]; int j = 0;
        while (e->name[j] && j < (int)sizeof oldn - 1) {
            oldn[j] = e->name[j]; j++;
        }
        oldn[j] = '\0';
        char newn[64]; newn[0] = '\0';
        if (settings_prompt("New name:", newn, sizeof newn) && newn[0]) {
            if (fs_rename(oldn, newn) == 0) {
                gdesk_notify("Files", "Renamed", 1);
                files_refresh();
            } else {
                gdesk_notify("Files", "Rename failed", 3);
            }
        }
    } else if (k == 'c' || k == 'C') {
        /* C = copy selected file to a chosen destination on the same
         * drive. Reads the source, creates the target, dumps bytes. */
        struct fs_dirent *e = &files_list[files_sel];
        if (e->attr & FS_ATTR_DIR) {
            gdesk_notify("Files",
                         "Copying directories not supported", 2);
            return;
        }
        char src[64]; int j = 0;
        while (e->name[j] && j < (int)sizeof src - 1) {
            src[j] = e->name[j]; j++;
        }
        src[j] = '\0';
        char dst[64]; dst[0] = '\0';
        if (!settings_prompt("Copy to:", dst, sizeof dst) || !dst[0]) return;
        int hs = fs_open(src);
        if (hs < 0) { gdesk_notify("Files", "Source open failed", 3); return; }
        fs_unlink(dst);
        if (fs_create(dst) < 0) {
            fs_close(hs); gdesk_notify("Files", "Create dest failed", 3); return;
        }
        int hd = fs_open(dst);
        if (hd < 0) {
            fs_close(hs); gdesk_notify("Files", "Dest open failed", 3); return;
        }
        char buf[512]; int total = 0; int n;
        while ((n = fs_read(hs, buf, sizeof buf)) > 0) {
            fs_write(hd, buf, n);
            total += n;
        }
        fs_close(hs); fs_close(hd);
        char msg[80];
        ksnprintf(msg, sizeof msg, "Copied %d bytes to %s", total, dst);
        gdesk_notify("Files", msg, 1);
        files_refresh();
    }
}

static void paint_files(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
    /* Header strip with the cwd. */
    fb_fill_rect(bx + 6, by + 6, bw - 12, 20, ZB_TITLE_LEFT);
    fb_bevel_sunken(bx + 6, by + 6, bw - 12, 20, 0x5546A8, ZB_BLACK);
    char hdr[96];
    ksnprintf(hdr, sizeof hdr, "Path: %s   (%d entries)",
              files_cwd[0] ? files_cwd : "(no drive)", files_count);
    fb_draw_text(bx + 12, by + 10, hdr, ZB_ACCENT, FB_TRANSPARENT);
    /* List area: white card. */
    int lx = bx + 6, ly = by + 32;
    int lw = bw - 12, lh = bh - 60;
    fb_fill_rect(lx, ly, lw, lh, ZB_PANEL_LIGHT);
    fb_bevel_sunken(lx, ly, lw, lh, 0xC4C8CC, ZB_BLACK);
    int row_h = 16;
    int visible = (lh - 8) / row_h;
    /* Keep selection on-screen by adjusting the scroll. */
    if (files_sel < files_scroll) files_scroll = files_sel;
    if (files_sel >= files_scroll + visible)
        files_scroll = files_sel - visible + 1;
    if (files_scroll < 0) files_scroll = 0;
    /* Rows. */
    for (int i = 0; i < visible && files_scroll + i < files_count; i++) {
        struct fs_dirent *e = &files_list[files_scroll + i];
        int ry = ly + 4 + i * row_h;
        int is_sel = (files_scroll + i == files_sel);
        if (is_sel) {
            fb_fill_rect(lx + 2, ry, lw - 4, row_h, ZB_TITLE_LEFT);
        }
        u32 fg = is_sel ? ZB_ACCENT : ZB_BLACK;
        /* Type tag: [DIR] amber, [FILE] grey. */
        const char *tag = (e->attr & FS_ATTR_DIR) ? "DIR " : "    ";
        u32 tagfg = (e->attr & FS_ATTR_DIR)
                  ? (is_sel ? ZB_ACCENT : ZB_TITLE_LEFT) : fg;
        fb_draw_text(lx + 8,  ry + 2, tag, tagfg, FB_TRANSPARENT);
        fb_draw_text(lx + 48, ry + 2, e->name, fg, FB_TRANSPARENT);
        /* Right-aligned size for files. */
        if (!(e->attr & FS_ATTR_DIR)) {
            char sz[16];
            if (e->size < 1024) ksnprintf(sz, sizeof sz, "%u B", (u32)e->size);
            else                ksnprintf(sz, sizeof sz, "%u KiB", (u32)(e->size / 1024));
            int len = 0; while (sz[len]) len++;
            fb_draw_text(lx + lw - 8 - len * 8, ry + 2, sz, fg, FB_TRANSPARENT);
        }
    }
    /* Footer hint. */
    fb_draw_text(bx + 8, by + bh - 18,
                 "ENTER open  BS up  DEL del  N new  M mkdir  R ren  C copy",
                 ZB_TITLE_LEFT, FB_TRANSPARENT);
}

/* === Virtual Shell ================================================== */
/* A proper shell that runs inside the pixel desktop terminal widget.
 * Handles fullscreen apps by switching between graphics/text modes,
 * captures command output, supports history and autocomplete. */
#define TERM_OUT_CAP   4096
#define TERM_IN_CAP    128
#define TERM_HIST_MAX  32
static char term_out[TERM_OUT_CAP];
static u32  term_out_len;
static char term_in[TERM_IN_CAP];
static int  term_in_len;
static int  term_initted;
static char term_hist[TERM_HIST_MAX][TERM_IN_CAP];
static int  term_hist_count;
static int  term_hist_idx;
/* Saved framebuffer state for mode switching */
static int  term_saved_w, term_saved_h;

static void term_init(void) {
    term_out_len = 0;
    term_in_len = 0;
    term_in[0] = 0;
    term_initted = 1;
    term_hist_count = 0;
    term_hist_idx = -1;
    term_saved_w = fb_w();
    term_saved_h = fb_h();
    const char *banner =
        "Zenbite Shell v" ZENBITE_VERSION "\n"
        "Commands: ls, cd, cat, run, evi, app, help\n"
        "Arrow Up/Down = history, Tab = autocomplete\n\n";
    for (int i = 0; banner[i]; i++)
        term_out[term_out_len++] = banner[i];
    term_out[term_out_len] = 0;
}

static void term_append_char(char c) {
    if (term_out_len + 1 >= TERM_OUT_CAP) {
        int drop = 512;
        for (u32 i = drop; i < term_out_len; i++)
            term_out[i - drop] = term_out[i];
        term_out_len -= drop;
    }
    term_out[term_out_len++] = c;
    term_out[term_out_len] = 0;
}

static void term_append_str(const char *s) {
    while (*s) term_append_char(*s++);
}

static void term_build_prompt(char *out, int max) {
    char drv = fs_get_drive();
    const char *cwd = fs_cwd();
    if (cwd[0] == '\\') cwd++;
    ksnprintf(out, max, "%c:\\%s> ", drv, cwd);
}

static void term_hist_push(void) {
    if (term_in_len == 0) return;
    if (term_hist_count > 0 &&
        strcmp(term_hist[(term_hist_count - 1) % TERM_HIST_MAX], term_in) == 0)
        return;
    int slot = term_hist_count % TERM_HIST_MAX;
    ksnprintf(term_hist[slot], TERM_IN_CAP, "%s", term_in);
    term_hist_count++;
    term_hist_idx = -1;
}

static void term_hist_nav(int dir) {
    if (term_hist_count == 0) return;
    if (dir < 0) {
        if (term_hist_idx < 0) term_hist_idx = term_hist_count - 1;
        else if (term_hist_idx > 0) term_hist_idx--;
    } else {
        if (term_hist_idx < 0) return;
        if (term_hist_idx >= term_hist_count - 1) {
            term_hist_idx = -1;
            term_in[0] = '\0';
            term_in_len = 0;
            return;
        }
        term_hist_idx++;
    }
    if (term_hist_idx >= 0) {
        int slot = term_hist_idx % TERM_HIST_MAX;
        ksnprintf(term_in, TERM_IN_CAP, "%s", term_hist[slot]);
        term_in_len = (int)strlen(term_in);
    }
}

static void term_autocomplete(void) {
    if (term_in_len == 0) return;
    int tok_start = 0;
    for (int i = term_in_len - 1; i >= 0; i--) {
        if (term_in[i] == ' ' || term_in[i] == '\\') { tok_start = i + 1; break; }
    }
    char prefix[64];
    int plen = 0;
    for (int i = tok_start; i < term_in_len && plen < 63; i++)
        prefix[plen++] = term_in[i];
    prefix[plen] = '\0';
    if (plen == 0) return;

    char drive = fs_get_drive();
    char path[FS_PATH_MAX];
    const char *cwd = fs_cwd();
    if (cwd[0] == '\\') cwd++;
    ksnprintf(path, sizeof path, "%c:\\%s", drive, cwd);
    int dh = fs_opendir(path);
    if (dh < 0) return;

    char matches[8][32];
    int nmatch = 0;
    struct fs_dirent e;
    while (fs_readdir(dh, &e) && nmatch < 8) {
        int ok = 1;
        for (int i = 0; i < plen; i++) {
            char a = prefix[i], b = e.name[i];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) { ok = 0; break; }
        }
        if (ok) ksnprintf(matches[nmatch++], 32, "%s", e.name);
    }
    fs_closedir(dh);

    if (nmatch == 1) {
        for (int j = 0; matches[0][j] && term_in_len < TERM_IN_CAP - 2; j++)
            term_in[term_in_len++] = matches[0][j];
        term_in[term_in_len] = '\0';
    } else if (nmatch > 1) {
        term_append_str("\n");
        for (int i = 0; i < nmatch; i++) {
            term_append_str("  ");
            term_append_str(matches[i]);
            term_append_str("\n");
        }
    }
    char prompt[128];
    term_build_prompt(prompt, sizeof prompt);
    term_append_str(prompt);
    term_append_str(term_in);
}

/* Check if a command is a fullscreen app that needs the whole screen. */
static int is_fullscreen_cmd(const char *name) {
    return (strcmp(name, "evi") == 0 || strcmp(name, "edit") == 0 ||
            strcmp(name, "run") == 0 || strcmp(name, "cc") == 0 ||
            strcmp(name, "zbc") == 0 || strcmp(name, "asm") == 0 ||
            strcmp(name, "elf") == 0 || strcmp(name, "install") == 0 ||
            strcmp(name, "flash") == 0);
}

/* Terminal command execution state. */
static int term_cmd_running;     /* 1 while a background command executes */
static int term_cmd_fullscreen;  /* 1 if the running cmd is fullscreen */

/* Run a fullscreen app: still blocks (needs mode switch), but we yield
 * so the desktop at least processes input between yields. */
static void term_run_fullscreen(const char *line) {
    term_cmd_running    = 1;
    term_cmd_fullscreen = 1;
    /* Run synchronously -- fullscreen apps need text mode. */
    shell_run_line(line);
    term_cmd_running    = 0;
    term_cmd_fullscreen = 0;
}

/* Run a normal command asynchronously in a background process. */
static void term_run_captured(const char *line) {
    if (shell_run_async(line) < 0) {
        term_append_str("[error: could not start process]\n");
        return;
    }
    term_cmd_running    = 1;
    term_cmd_fullscreen = 0;
    /* The actual output is collected in paint_term() when shell_async_is_done(). */
}

/* Poll the async shell: if the command finished, collect output. */
static void term_poll_async(void) {
    if (!term_cmd_running) return;
    if (!shell_async_is_done()) return;
    /* Command finished -- grab output. */
    u32 cap_len = 0;
    const char *cap = shell_async_get_output(&cap_len);
    for (u32 i = 0; i < cap_len; i++) term_append_char(cap[i]);
    if (cap_len == 0 || (cap_len > 0 && cap[cap_len - 1] != '\n'))
        term_append_char('\n');
    term_cmd_running = 0;
    /* Show prompt. */
    char prompt[128];
    term_build_prompt(prompt, sizeof prompt);
    term_append_str(prompt);
}

static void term_run(const char *line) {
    char prompt[128];
    term_build_prompt(prompt, sizeof prompt);
    term_append_str(prompt);
    term_append_str(line);
    term_append_char('\n');

    /* Built-in shell commands */
    if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
        term_append_str("[type 'shutdown' to power off, or close this window]\n");
        term_build_prompt(prompt, sizeof prompt);
        term_append_str(prompt);
        return;
    }
    if (strcmp(line, "cls") == 0 || strcmp(line, "clear") == 0) {
        term_out_len = 0;
        term_out[0] = 0;
        term_build_prompt(prompt, sizeof prompt);
        term_append_str(prompt);
        return;
    }
    if (strcmp(line, "help") == 0) {
        term_append_str("Shell commands:\n");
        term_append_str("  ls [dir]       list directory\n");
        term_append_str("  cd <dir>       change directory\n");
        term_append_str("  cat <file>     print file contents\n");
        term_append_str("  evi <file>     edit file (fullscreen)\n");
        term_append_str("  run <file.zbx> run program (fullscreen)\n");
        term_append_str("  app list       list installed apps\n");
        term_append_str("  app install    install an app\n");
        term_append_str("  mem            show memory usage\n");
        term_append_str("  cls / clear    clear screen\n");
        term_append_str("  help           show this help\n");
        term_append_str("  exit           close this terminal\n\n");
        term_build_prompt(prompt, sizeof prompt);
        term_append_str(prompt);
        return;
    }

    /* Extract command name for fullscreen check */
    char cmd_copy[TERM_IN_CAP];
    ksnprintf(cmd_copy, sizeof cmd_copy, "%s", line);
    char *cmd_name = cmd_copy;
    while (*cmd_name == ' ') cmd_name++;
    char *space = cmd_name;
    while (*space && *space != ' ') space++;
    *space = '\0';

    /* Run the command */
    if (is_fullscreen_cmd(cmd_name)) {
        term_run_fullscreen(line);
        term_append_str("[app exited]\n");
        char prompt[128];
        term_build_prompt(prompt, sizeof prompt);
        term_append_str(prompt);
    } else {
        term_run_captured(line);
        /* Prompt will be shown by term_poll_async() when command finishes. */
    }
}

static void term_key(int k) {
    if (!term_initted) term_init();
    /* While a background command is running, only allow Ctrl+C. */
    if (term_cmd_running) {
        if (k == 0x03) {   /* Ctrl+C */
            shell_async_kill();
            term_append_str("^C\n");
            term_cmd_running = 0;
            char prompt[128];
            term_build_prompt(prompt, sizeof prompt);
            term_append_str(prompt);
        }
        return;
    }
    if (k == 10 || k == 13) {
        term_in[term_in_len] = 0;
        if (term_in_len > 0) {
            term_hist_push();
            term_run(term_in);
            term_in_len = 0;
            term_in[0] = 0;
            term_hist_idx = -1;
        } else {
            term_append_char('\n');
            char prompt[128];
            term_build_prompt(prompt, sizeof prompt);
            term_append_str(prompt);
        }
        return;
    }
    if (k == 8 || k == 0x7F) {
        if (term_in_len > 0) term_in[--term_in_len] = 0;
        return;
    }
    if (k == KB_UP)   { term_hist_nav(-1); return; }
    if (k == KB_DOWN) { term_hist_nav(1);  return; }
    if (k == '\t') { term_autocomplete(); return; }
    if (k == 27) {
        term_in_len = 0;
        term_in[0] = 0;
        term_hist_idx = -1;
        return;
    }
    if (k >= 32 && k < 127 && term_in_len + 1 < TERM_IN_CAP) {
        term_in[term_in_len++] = (char)k;
        term_in[term_in_len] = 0;
    }
}

static void paint_term(struct gwin *w) {
    if (!term_initted) term_init();
    /* Poll for async command completion. */
    term_poll_async();
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
    /* Black VT-style screen inside a sunken bevel. */
    int sx = bx + 6, sy = by + 6;
    int sw = bw - 12, sh = bh - 12;
    fb_fill_rect(sx, sy, sw, sh, ZB_BLACK);
    fb_bevel_sunken(sx, sy, sw, sh, 0x5546A8, ZB_BLACK);
    /* Render the scrollback, wrapping on 8px columns. Drop earliest
     * lines when we'd overflow the visible area. */
    int cols = (sw - 8) / 8;
    int rows = (sh - 8) / 16;
    /* Two-pass: count lines, skip the first N so the tail fits. */
    int line_count = 1, col = 0;
    for (u32 i = 0; i < term_out_len; i++) {
        char c = term_out[i];
        if (c == '\n') { line_count++; col = 0; continue; }
        col++;
        if (col >= cols) { line_count++; col = 0; }
    }
    /* +1 for the live input row. */
    int skip_lines = line_count + 1 - rows;
    if (skip_lines < 0) skip_lines = 0;
    int x = sx + 4, y = sy + 4;
    int skipped = 0;
    int line_started = 1;
    for (u32 i = 0; i < term_out_len; i++) {
        char c = term_out[i];
        if (line_started && skipped < skip_lines) {
            /* Skip glyphs until the line breaks. */
            if (c == '\n') { skipped++; }
            else {
                col = 0;
                while (i < term_out_len && term_out[i] != '\n') {
                    col++;
                    if (col >= cols) {
                        skipped++;
                        if (skipped >= skip_lines) { i++; break; }
                        col = 0;
                    }
                    i++;
                }
                if (i < term_out_len && term_out[i] == '\n') skipped++;
            }
            line_started = 1;
            continue;
        }
        line_started = 0;
        if (c == '\n') {
            x = sx + 4; y += 16; line_started = 1; continue;
        }
        fb_blit_glyph(x, y, (u8)c, 0x9CE89C, FB_TRANSPARENT);
        x += 8;
        if (x >= sx + 4 + cols * 8) { x = sx + 4; y += 16; line_started = 1; }
        if (y >= sy + sh - 16) break;
    }
    /* Prompt + live input on the bottom-most row. */
    char prompt[128];
    term_build_prompt(prompt, sizeof prompt);
    int prompt_len = (int)strlen(prompt);
    int py = sy + sh - 18;
    if (term_cmd_running) {
        /* Show running indicator instead of editable prompt. */
        fb_draw_text(sx + 4, py, "[running...]  Ctrl+C to cancel", 0xFF8844, FB_TRANSPARENT);
    } else {
        fb_draw_text(sx + 4, py, prompt, ZB_ACCENT, FB_TRANSPARENT);
        fb_draw_text(sx + 4 + prompt_len * 8, py, term_in, 0x9CE89C, FB_TRANSPARENT);
        /* Cursor block */
        int caret_x = sx + 4 + (prompt_len + term_in_len) * 8;
        fb_fill_rect(caret_x, py, 7, 14, 0x9CE89C);
    }
}

/* === Pixel app: digital Clock ======================================= */
static void paint_clock(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    /* Modern flat background -- darker slate card with no DOS bevel. */
    fb_fill_rect(bx, by, bw, bh, 0x14202C);
    struct rtc_time t; rtc_read(&t);
    /* Big digits drawn as 4x rectangles -- "scaled glyph" effect.
     * We render HH:MM:SS via the 8x16 font, then bloom it 4x by
     * blitting each pixel as a 4x4 block via fb_blit_glyph isn't
     * scalable, so we paint segmented blocks manually. */
    char hms[12];
    ksnprintf(hms, sizeof hms, "%02u:%02u:%02u",
              (u32)t.hour, (u32)t.min, (u32)t.sec);
    /* Centered scaled text: each char is 32 wide. */
    int chw = 32, chh = 48;
    int total = (int)strlen(hms) * chw;
    int x0 = bx + (bw - total) / 2;
    int y0 = by + (bh - chh) / 2 - 12;
    for (int i = 0; hms[i]; i++) {
        char c = hms[i];
        /* Draw the glyph 4x by repeatedly blitting through a temp
         * approach: we just blit four overlapping copies offset by
         * 1px -- gives a thick scaled feel even with 8x16 source. */
        for (int dy = 0; dy < 4; dy++)
            for (int dx = 0; dx < 4; dx++)
                fb_blit_glyph(x0 + i * chw + 4 + dx, y0 + 8 + dy,
                              (u8)c, 0x4FB37A, FB_TRANSPARENT);
    }
    /* Date row. */
    char date[24];
    ksnprintf(date, sizeof date, "%04u-%02u-%02u",
              (u32)t.year, (u32)t.month, (u32)t.day);
    int dlen = (int)strlen(date);
    fb_draw_text(bx + (bw - dlen * 8) / 2, y0 + chh + 24,
                 date, 0xC8D4E0, FB_TRANSPARENT);
    fb_draw_text(bx + (bw - 22 * 8) / 2, by + bh - 24,
                 "Zenbite CMOS RTC + PIT",
                 0x70808A, FB_TRANSPARENT);
}

/* === Pixel app: System Monitor ======================================= */
static u32 sysmon_history[80];
static int sysmon_history_pos;
static u32 sysmon_last_tick;
static void paint_sysmon(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0x12202E);
    /* Memory info: total RAM from PMM, kernel heap usage */
    u32 ram_total = pmm_total_kib();
    u32 ram_free  = pmm_free_kib();
    u32 ram_used  = ram_total - ram_free;
    u32 heap_used = kheap_used_kib();
    u32 heap_tot  = kheap_total_kib();
    if (heap_tot == 0) heap_tot = 1;
    u32 ram_pct   = ram_total ? (ram_used * 100 / ram_total) : 0;
    u32 heap_pct  = (heap_used * 100) / heap_tot;
    /* Sample once a second. */
    u32 now = pit_ticks();
    if (now - sysmon_last_tick > 100) {
        sysmon_history[sysmon_history_pos] = ram_pct;
        sysmon_history_pos = (sysmon_history_pos + 1) % 80;
        sysmon_last_tick = now;
    }
    /* Header. */
    fb_draw_text(bx + 12, by + 10, "System Monitor", 0xFFFFFF, FB_TRANSPARENT);
    char line[80];
    /* Total RAM */
    ksnprintf(line, sizeof line, "RAM: %u / %u KiB  (%u%% used)",
              ram_used, ram_total, ram_pct);
    fb_draw_text(bx + 12, by + 28, line, 0x47A6D4, FB_TRANSPARENT);
    /* Kernel heap */
    ksnprintf(line, sizeof line, "Heap: %u / %u KiB  (%u%% used)",
              heap_used, heap_tot, heap_pct);
    fb_draw_text(bx + 12, by + 44, line, 0x4FB37A, FB_TRANSPARENT);
    /* Uptime */
    ksnprintf(line, sizeof line, "Uptime: %u sec  (%u ticks)",
              pit_ticks() / 1000, pit_ticks());
    fb_draw_text(bx + 12, by + 60, line, 0xC8D4E0, FB_TRANSPARENT);
    /* Live bar chart of RAM usage. */
    int gx = bx + 12, gy = by + 80;
    int gw = bw - 24, gh = bh - 110;
    fb_fill_rect(gx, gy, gw, gh, 0x0A1420);
    fb_bevel_sunken(gx, gy, gw, gh, 0x33495E, ZB_BLACK);
    int bar_w = gw / 80;
    if (bar_w < 1) bar_w = 1;
    for (int i = 0; i < 80; i++) {
        int idx = (sysmon_history_pos + i) % 80;
        u32 p = sysmon_history[idx];
        if (p > 100) p = 100;
        int bh_px = (int)(p * (gh - 4) / 100);
        int bx_px = gx + 2 + i * bar_w;
        if (bx_px + bar_w >= gx + gw) break;
        u32 col = (p > 80) ? 0xE85A8C : (p > 50 ? 0xFFA831 : 0x4FB37A);
        fb_fill_rect(bx_px, gy + gh - 2 - bh_px, bar_w - 1, bh_px, col);
    }
    fb_draw_text(bx + 12, by + bh - 18,
                 "Live 1s sampling of RAM usage",
                 0x70808A, FB_TRANSPARENT);
}

/* === Activity Monitor ==================================================
 * Shows all running processes with PID, name, state, priority, CPU
 * usage.  Keyboard: Up/Down = select, K = kill, +/- = priority. */
#include "proc.h"
static int actmon_sel;      /* selected row */
static void paint_activity(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0x12202E);
    fb_draw_text(bx + 12, by + 10, "Activity Monitor", 0xFFFFFF, FB_TRANSPARENT);

    struct proc_info info[PROC_MAX];
    int count = proc_enumerate(info, PROC_MAX);

    /* Column header. */
    int lx = bx + 12, ly = by + 28;
    fb_draw_text(lx, ly, "PID   Name              State      Pri  CPU Ticks", 0x70808A, FB_TRANSPARENT);
    ly += 16;

    static const char *state_names[] = { "free", "ready", "running", "sleep", "zombie", "killed" };
    static const u32   state_cols[]  = { 0x808080, 0x4FB37A, 0x47A6D4, 0xFFA831, 0xE85A8C, 0xD44747 };

    for (int i = 0; i < count && ly < by + bh - 30; i++) {
        int is_sel = (i == actmon_sel);
        u32 bg = is_sel ? 0x2C2152 : FB_TRANSPARENT;
        u32 fg = is_sel ? 0xFFFFFF : 0xC8D4E0;
        if (is_sel) fb_fill_rect(bx + 8, ly - 2, bw - 16, 16, bg);

        char line[80];
        const char *sn = (info[i].state <= 5) ? state_names[info[i].state] : "?";
        u32 sc = (info[i].state <= 5) ? state_cols[info[i].state] : 0x808080;
        ksnprintf(line, sizeof line, "%-5u %-18s", info[i].pid, info[i].name);
        fb_draw_text(lx, ly, line, fg, FB_TRANSPARENT);
        /* State coloured. */
        int state_x = lx + (5 + 18) * 8;
        fb_draw_text(state_x, ly, sn, sc, FB_TRANSPARENT);
        /* Priority. */
        static const char *prio_names[] = { "low", "normal", "high", "rt" };
        const char *pn = (info[i].priority <= 3) ? prio_names[info[i].priority] : "?";
        char pbuf[12]; ksnprintf(pbuf, sizeof pbuf, "%-7s", pn);
        fb_draw_text(state_x + 7 * 8, ly, pbuf, 0xC8D4E0, FB_TRANSPARENT);
        /* CPU ticks. */
        char tbuf[12]; ksnprintf(tbuf, sizeof tbuf, "%u", info[i].cpu_ticks);
        fb_draw_text(state_x + 14 * 8, ly, tbuf, 0x9CE89C, FB_TRANSPARENT);
        ly += 16;
    }
    if (count == 0)
        fb_draw_text(lx, ly, "(no processes)", 0x70808A, FB_TRANSPARENT);

    /* Status bar. */
    fb_draw_text(bx + 12, by + bh - 16,
                 "K=Kill  +/-=Priority  Up/Down=Select",
                 0x70808A, FB_TRANSPARENT);
}

static void activity_key(int k) {
    struct proc_info info[PROC_MAX];
    int count = proc_enumerate(info, PROC_MAX);
    if (count == 0) return;
    if (k == KB_UP)   actmon_sel = (actmon_sel > 0) ? actmon_sel - 1 : count - 1;
    if (k == KB_DOWN) actmon_sel = (actmon_sel < count - 1) ? actmon_sel + 1 : 0;
    if (k == 'k' || k == 'K') {
        /* Kill selected process (never PID 0 = desktop). */
        if (actmon_sel < count && info[actmon_sel].pid != 0)
            proc_kill(info[actmon_sel].pid);
        actmon_sel = 0;
    }
    if (k == '+' || k == '=') {
        if (actmon_sel < count && info[actmon_sel].pid != 0) {
            u32 p = info[actmon_sel].priority;
            if (p < PRIO_REALTIME) proc_set_priority(info[actmon_sel].pid, p + 1);
        }
    }
    if (k == '-' || k == '_') {
        if (actmon_sel < count && info[actmon_sel].pid != 0) {
            u32 p = info[actmon_sel].priority;
            if (p > PRIO_LOW) proc_set_priority(info[actmon_sel].pid, p - 1);
        }
    }
}

/* === Pixel app: Settings ============================================ */
extern int  desktop_get_theme(void);
extern void desktop_set_theme(int);
extern int  desktop_get_start_gdesk(void);
extern void desktop_set_start_gdesk(int);
extern int  desktop_get_keymap(char *out, int outsz);
extern void desktop_set_keymap(const char *s);
extern int  desktop_get_lock_password(char *out, int outsz);
extern void desktop_set_lock_password(const char *s);
extern int  mouse_get_speed(void);
extern void mouse_set_speed(int s);
extern void config_save(void);
extern int  kb_get_repeat_delay(void);
extern int  kb_get_repeat_rate(void);
extern void kb_set_repeat(int delay_ms, int rate_cps);
extern int  g_sound_enabled;
extern int  g_sound_vol;
extern void speaker_beep(u32 freq, u32 ms);
/* Settings-owned: target gdesk resolution. Persisted in CONFIG.TXT so
 * the chosen size survives reboots. Read by g_desktop_main on boot. */
static int g_res_w = 800;
static int g_res_h = 600;
int  gdesk_get_res_w(void) { return g_res_w; }
int  gdesk_get_res_h(void) { return g_res_h; }
void gdesk_set_res(int w, int h) { g_res_w = w; g_res_h = h; }

/* Brightness: 60..140%. Applied by dimming ZB_BG on the desktop fill. */
static int g_brightness = 100;
int  gdesk_get_brightness(void) { return g_brightness; }
void gdesk_set_brightness(int v) {
    if (v < 60) v = 60; if (v > 140) v = 140;
    g_brightness = v;
}

/* === Settings widget ==================================================
 * Sidebar-style preferences pane. Six categories on the left, content
 * card on the right. Each control is a real visual widget (toggle,
 * slider, button) and can be driven by either mouse or keyboard --
 * the cycle-on-ENTER pattern from the original list view is still
 * supported via the same settings_key dispatch. */
static int settings_tab;       /* current tab 0..5 */
static int settings_sel;       /* current row inside the tab */
#define SETTINGS_TAB_COUNT 6
static const char *settings_tab_labels[SETTINGS_TAB_COUNT] = {
    "Appearance", "System", "Input", "Sound", "Time", "Security"
};
/* Tile colours match the desktop icon palette. */
static const u32 settings_tab_colors[SETTINGS_TAB_COUNT] = {
    0x6447B0, 0x8A93A6, 0x4FB37A, 0x47A6D4, 0xE85A8C, 0xFFA831
};
/* Per-tab row counts. Keep in sync with the row layout below. */
static const int settings_tab_rows[SETTINGS_TAB_COUNT] = {
    4,  /* Appearance: Theme, Wallpaper, Brightness, Resolution */
    3,  /* System: Start gdesk, Clear layout, Save now */
    4,  /* Input: Keymap, Mouse speed, Kbd repeat delay, Kbd repeat rate */
    3,  /* Sound: Enabled, Volume, Test beep */
    1,  /* Time: Set date/time */
    2,  /* Security: Password, Lock now */
};

/* Modal text-entry helper used by the Settings password row. Draws a
 * centred box; returns 1 if the user pressed ENTER, 0 on ESC. The
 * input is written into out (zero-terminated) up to outsz - 1 chars.
 * Pre-fills with the current value so blanking it requires a few
 * backspaces. */
static int settings_prompt(const char *label, char *out, int outsz) {
    int W = fb_w(), H = fb_h();
    int bw = 400, bh = 120;
    int bx = (W - bw) / 2, by = (H - bh) / 2;
    int len = 0;
    while (out[len] && len < outsz - 1) len++;
    for (;;) {
        fb_fill_rect(bx, by, bw, bh, 0xE8ECF1);
        fb_hline(bx, by, bw, 0x4A5466);
        fb_hline(bx, by + bh - 1, bw, 0x101820);
        fb_vline(bx, by, bh, 0x4A5466);
        fb_vline(bx + bw - 1, by, bh, 0x101820);
        fb_hgradient(bx + 1, by + 1, bw - 2, 22, ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
        fb_draw_text(bx + 10, by + 4, label, ZB_TITLE_TEXT, FB_TRANSPARENT);
        fb_fill_rect(bx + 16, by + 50, bw - 32, 28, 0xFFFFFF);
        fb_hline(bx + 16, by + 78, bw - 32, 0xC4C8CC);
        fb_draw_text(bx + 22, by + 56, out, 0x1E2A38, FB_TRANSPARENT);
        if ((pit_ticks() / 500) & 1)
            fb_fill_rect(bx + 22 + len * 8, by + 56, 7, 14, 0x6447B0);
        fb_draw_text(bx + 16, by + bh - 22,
                     "ENTER to save, ESC to cancel",
                     0x70808A, FB_TRANSPARENT);
        fb_present();
        int k = kb_getc();
        if (k == 27) {
            mark_wallpaper_dirty();   /* modal painted over WP */
            return 0;
        }
        if (k == 10 || k == 13) {
            out[len] = '\0';
            mark_wallpaper_dirty();
            return 1;
        }
        if ((k == 8 || k == 0x7F) && len > 0) { out[--len] = '\0'; continue; }
        if (k >= 32 && k < 127 && len + 1 < outsz) {
            out[len++] = (char)k;
            out[len] = '\0';
        }
    }
}

/* Open the RTC editor modal. Pulled out of settings_key so the
 * Time tab can call it from a button click. */
static void settings_rtc_modal(void) {
    extern void rtc_write(const struct rtc_time *t);
    extern void rtc_read(struct rtc_time *t);
    struct rtc_time t; rtc_read(&t);
    int W = fb_w(), H = fb_h();
    int bw = 340, bh = 200;
    int bx = (W - bw) / 2, by = (H - bh) / 2;
    int fields[6]; int fi = 0;
    fields[0] = t.year; fields[1] = t.month; fields[2] = t.day;
    fields[3] = t.hour; fields[4] = t.min;   fields[5] = t.sec;
    static const char *flabels[6] = { "Year","Mon","Day","Hour","Min","Sec" };
    for (;;) {
        fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
        fb_bevel_raised(bx, by, bw, bh, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
        fb_hgradient(bx + 2, by + 2, bw - 4, 22,
                     ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
        fb_draw_text(bx + 10, by + 5, "Set Date / Time",
                     ZB_TITLE_TEXT, FB_TRANSPARENT);
        for (int i = 0; i < 6; i++) {
            int fx = bx + 20 + (i % 3) * 100;
            int fy = by + 40 + (i / 3) * 56;
            u32 bg = (i == fi) ? ZB_TITLE_LEFT : ZB_PANEL_LIGHT;
            u32 fg = (i == fi) ? ZB_TITLE_TEXT : ZB_BLACK;
            fb_fill_rect(fx, fy, 80, 40, bg);
            fb_bevel_sunken(fx, fy, 80, 40, ZB_PANEL_LIGHT, ZB_PANEL_DARK);
            fb_draw_text(fx + 4, fy + 4, flabels[i],
                         (i == fi) ? 0xC0D8FF : ZB_PANEL_DARK,
                         FB_TRANSPARENT);
            char val[8];
            ksnprintf(val, sizeof val, "%d", fields[i]);
            fb_draw_text(fx + 4, fy + 20, val, fg, FB_TRANSPARENT);
        }
        fb_draw_text(bx + 16, by + bh - 24,
                     "Tab/arrows change field, +/- adjust, ENTER save, ESC cancel",
                     ZB_PANEL_DARK, FB_TRANSPARENT);
        fb_present();
        int kk = kb_getc();
        if (kk == 27) { mark_wallpaper_dirty(); break; }
        if (kk == '\n' || kk == '\r') {
            struct rtc_time nt;
            nt.year  = (u16)fields[0]; nt.month = (u8)fields[1];
            nt.day   = (u8)fields[2];  nt.hour  = (u8)fields[3];
            nt.min   = (u8)fields[4];  nt.sec   = (u8)fields[5];
            rtc_write(&nt);
            mark_wallpaper_dirty();
            gdesk_notify("Time", "RTC updated", 1);
            break;
        }
        if (kk == '\t' || kk == (int)(u8)KB_RIGHT) fi = (fi + 1) % 6;
        if (kk == (int)(u8)KB_LEFT)                fi = (fi + 5) % 6;
        if (kk == '+' || kk == (int)(u8)KB_UP)    fields[fi]++;
        if (kk == '-' || kk == (int)(u8)KB_DOWN)  fields[fi]--;
        if (fields[0] < 2000) fields[0] = 2000;
        if (fields[1] < 1)    fields[1] = 1;
        if (fields[1] > 12)   fields[1] = 12;
        if (fields[2] < 1)    fields[2] = 1;
        if (fields[2] > 31)   fields[2] = 31;
        if (fields[3] < 0)    fields[3] = 0;
        if (fields[3] > 23)   fields[3] = 23;
        if (fields[4] < 0)    fields[4] = 0;
        if (fields[4] > 59)   fields[4] = 59;
        if (fields[5] < 0)    fields[5] = 0;
        if (fields[5] > 59)   fields[5] = 59;
    }
}

/* Cycle a row: advances the row's value to the next legal state.
 * delta is +1 (next) or -1 (previous). Knows about each control. */
static void settings_cycle(int tab, int row, int delta) {
    switch (tab) {
    case 0: /* Appearance */
        if (row == 0) {
            int t = desktop_get_theme() + delta;
            if (t < 0) t = 4; if (t > 4) t = 0;
            desktop_set_theme(t);
        } else if (row == 1) {
            int s = gdesk_get_wallpaper_style() + delta;
            if (s < 0) s = 2; if (s > 2) s = 0;
            gdesk_set_wallpaper_style(s);
            mark_wallpaper_dirty();
        } else if (row == 2) {
            int b = gdesk_get_brightness() + delta * 10;
            if (b < 60)  b = 60;
            if (b > 140) b = 140;
            gdesk_set_brightness(b);
            mark_wallpaper_dirty();
        } else if (row == 3) {
            int w = gdesk_get_res_w();
            int idx = (w >= 1280) ? 3 : (w >= 1024) ? 2 : (w >= 800) ? 1 : 0;
            idx = (idx + delta + 4) % 4;
            static const int res[4][2] = {
                {640,480}, {800,600}, {1024,768}, {1280,720}
            };
            gdesk_set_res(res[idx][0], res[idx][1]);
        }
        break;
    case 1: /* System */
        if (row == 0) {
            desktop_set_start_gdesk(!desktop_get_start_gdesk());
        } else if (row == 1) {
            gdesk_set_last_layout("");
            gdesk_notify("Settings", "Saved layout cleared", 1);
        } else if (row == 2) {
            config_save();
            gdesk_notify("Settings", "Settings saved", 1);
        }
        break;
    case 2: /* Input */
        if (row == 0) {
            char km[8]; desktop_get_keymap(km, sizeof km);
            desktop_set_keymap(km[0] == 'd' ? "us" : "de");
        } else if (row == 1) {
            int s = mouse_get_speed() + delta * 4;
            if (s < 4)  s = 4;
            if (s > 24) s = 24;
            mouse_set_speed(s);
        } else if (row == 2) {
            int d = kb_get_repeat_delay() + delta * 250;
            if (d < 250)  d = 250;
            if (d > 1000) d = 1000;
            kb_set_repeat(d, kb_get_repeat_rate());
        } else if (row == 3) {
            int r = kb_get_repeat_rate() + delta * 2;
            if (r < 2)  r = 2;
            if (r > 30) r = 30;
            kb_set_repeat(kb_get_repeat_delay(), r);
        }
        break;
    case 3: /* Sound */
        if (row == 0) {
            g_sound_enabled = !g_sound_enabled;
            if (g_sound_enabled) speaker_beep(880, 80);
        } else if (row == 1) {
            int v = g_sound_vol + delta * 2;
            if (v < 0)  v = 0;
            if (v > 16) v = 16;
            g_sound_vol = v;
        } else if (row == 2) {
            speaker_beep(880, 120);
        }
        break;
    case 4: /* Time */
        if (row == 0) settings_rtc_modal();
        break;
    case 5: /* Security */
        if (row == 0) {
            char pw[24];
            desktop_get_lock_password(pw, sizeof pw);
            if (settings_prompt("Lock password (blank = disable)", pw, sizeof pw)) {
                desktop_set_lock_password(pw);
                gdesk_notify("Security",
                             pw[0] ? "Password set" : "Password cleared", 1);
            }
        } else if (row == 1) {
            lock_run();
            mark_wallpaper_dirty();
        }
        break;
    }
}

static void settings_key(int k) {
    int rows = settings_tab_rows[settings_tab];
    /* Page-jump shortcuts: 1..6 jump straight to a tab. */
    if (k >= '1' && k <= '0' + SETTINGS_TAB_COUNT) {
        settings_tab = k - '1'; settings_sel = 0;
        return;
    }
    if (k == '\t') {
        settings_tab = (settings_tab + 1) % SETTINGS_TAB_COUNT;
        settings_sel = 0;
        return;
    }
    if (k == KB_UP)    settings_sel = (settings_sel + rows - 1) % rows;
    if (k == KB_DOWN)  settings_sel = (settings_sel + 1) % rows;
    if (k == KB_LEFT)  settings_cycle(settings_tab, settings_sel, -1);
    if (k == KB_RIGHT) settings_cycle(settings_tab, settings_sel, +1);
    if (k == 10 || k == 13 || k == ' ')
        settings_cycle(settings_tab, settings_sel, +1);
}

/* Slider: thin sunken track with a raised handle at value/max along the
 * bar. The track and handle live inside (x, y, w, h). Returns the
 * handle's centre x for hit-testing. */
static void draw_slider(int x, int y, int w, int h,
                        int value, int vmin, int vmax) {
    /* Track. */
    int ty = y + h / 2 - 3;
    fb_fill_rect(x, ty, w, 6, ZB_PANEL_DARK);
    fb_bevel_sunken(x, ty, w, 6, ZB_PANEL_DARK, ZB_PANEL_LIGHT);
    /* Handle. */
    int span = vmax - vmin;
    if (span <= 0) span = 1;
    int hx = x + (value - vmin) * (w - 12) / span;
    if (hx < x) hx = x;
    if (hx > x + w - 12) hx = x + w - 12;
    fb_fill_rect(hx, y, 12, h, ZB_PANEL);
    fb_bevel_raised(hx, y, 12, h, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    /* Centre groove on the handle for the retro feel. */
    fb_vline(hx + 5, y + 4, h - 8, ZB_PANEL_DARK);
    fb_vline(hx + 6, y + 4, h - 8, ZB_PANEL_LIGHT);
}

/* Pill toggle: ON shows the green half lit; OFF the grey half lit. */
static void draw_toggle(int x, int y, int on) {
    int w = 44, h = 20;
    fb_fill_rect(x, y, w, h, ZB_PANEL);
    fb_bevel_sunken(x, y, w, h, ZB_PANEL_DARK, ZB_PANEL_LIGHT);
    int kx = on ? (x + w/2) : (x + 2);
    u32 kc = on ? 0x4FB37A : ZB_PANEL_DARK;
    fb_fill_rect(kx, y + 2, w/2 - 2, h - 4, kc);
    fb_bevel_raised(kx, y + 2, w/2 - 2, h - 4, ZB_PANEL_LIGHT, ZB_BLACK);
    fb_draw_text(x + 6,           y + 2, "OFF",
                 on ? ZB_PANEL_DARK : ZB_BLACK, FB_TRANSPARENT);
    fb_draw_text(x + 6 + w/2,     y + 2, "ON ",
                 on ? ZB_BLACK : ZB_PANEL_DARK, FB_TRANSPARENT);
}

/* Cycle button: "< value >" -- shows current value with arrow hints. */
static void draw_cycler(int x, int y, int w, int h, const char *value) {
    fb_fill_rect(x, y, w, h, ZB_PANEL);
    fb_bevel_raised(x, y, w, h, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    fb_draw_text(x + 6, y + (h - 16) / 2, "<", ZB_TITLE_LEFT, FB_TRANSPARENT);
    fb_draw_text(x + w - 14, y + (h - 16) / 2, ">", ZB_TITLE_LEFT, FB_TRANSPARENT);
    int len = (int)strlen(value);
    fb_draw_text(x + (w - len * 8) / 2, y + (h - 16) / 2,
                 value, ZB_BLACK, FB_TRANSPARENT);
}

/* Push button. */
static void draw_pushbtn(int x, int y, int w, int h, const char *label,
                         u32 accent) {
    fb_fill_rect(x, y, w, h, ZB_PANEL);
    fb_bevel_raised(x, y, w, h, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    /* Coloured strip on the left edge. */
    fb_fill_rect(x + 2, y + 2, 6, h - 4, accent);
    int len = (int)strlen(label);
    fb_draw_text(x + 14 + (w - 16 - len * 8) / 2, y + (h - 16) / 2,
                 label, ZB_BLACK, FB_TRANSPARENT);
}

/* Per-row rect helper. Each tab uses the same right-pane layout: a
 * stack of 64-pixel-tall rows starting at content_y. */
static int settings_row_y(int by, int row) { return by + 56 + row * 64; }

static void paint_settings_appearance(int rx, int ry, int rw) {
    fb_draw_text(rx + 10, ry + 10, "Appearance", ZB_TITLE_LEFT, FB_TRANSPARENT);
    fb_draw_text(rx + 10, ry + 28,
                 "Visual style, wallpaper, brightness, resolution.",
                 ZB_PANEL_DARK, FB_TRANSPARENT);
    static const char *themes[] = { "Slate", "Classic", "Dark", "Ocean", "Sunset" };
    int t = desktop_get_theme(); if (t < 0 || t > 4) t = 0;
    static const char *wp[] = { "Solid", "Gradient", "Diagonal" };
    int wps = gdesk_get_wallpaper_style();
    if (wps < 0 || wps > 2) wps = 1;
    char res[20];
    ksnprintf(res, sizeof res, "%dx%d", gdesk_get_res_w(), gdesk_get_res_h());
    static const char *labels[4] = {
        "Theme", "Wallpaper", "Brightness", "Resolution"
    };
    for (int r = 0; r < 4; r++) {
        int row_y = settings_row_y(ry, r);
        int is_sel = (settings_sel == r && settings_tab == 0);
        if (is_sel) {
            fb_fill_rect(rx, row_y - 4, rw, 48, 0xD8DCE4);
            fb_bevel_sunken(rx, row_y - 4, rw, 48,
                            ZB_PANEL_LIGHT, ZB_PANEL_DARK);
        }
        fb_draw_text(rx + 12, row_y + 8, labels[r], ZB_BLACK, FB_TRANSPARENT);
        switch (r) {
        case 0: draw_cycler(rx + rw - 200, row_y, 184, 32, themes[t]); break;
        case 1: draw_cycler(rx + rw - 200, row_y, 184, 32, wp[wps]);   break;
        case 2: {
            int b = gdesk_get_brightness();
            draw_slider(rx + rw - 220, row_y + 6, 160, 20, b, 60, 140);
            char val[8]; ksnprintf(val, sizeof val, "%d%%", b);
            fb_draw_text(rx + rw - 50, row_y + 8, val,
                         ZB_BLACK, FB_TRANSPARENT);
            break;
        }
        case 3: draw_cycler(rx + rw - 200, row_y, 184, 32, res);       break;
        }
    }
}

static void paint_settings_system(int rx, int ry, int rw) {
    fb_draw_text(rx + 10, ry + 10, "System", ZB_TITLE_LEFT, FB_TRANSPARENT);
    fb_draw_text(rx + 10, ry + 28,
                 "Boot autostart and persisted layout.",
                 ZB_PANEL_DARK, FB_TRANSPARENT);
    static const char *labels[3] = {
        "Start graphical desktop on boot",
        "Clear saved window layout",
        "Save settings to disk now"
    };
    for (int r = 0; r < 3; r++) {
        int row_y = settings_row_y(ry, r);
        int is_sel = (settings_sel == r && settings_tab == 1);
        if (is_sel) {
            fb_fill_rect(rx, row_y - 4, rw, 48, 0xD8DCE4);
            fb_bevel_sunken(rx, row_y - 4, rw, 48,
                            ZB_PANEL_LIGHT, ZB_PANEL_DARK);
        }
        fb_draw_text(rx + 12, row_y + 8, labels[r], ZB_BLACK, FB_TRANSPARENT);
        switch (r) {
        case 0: draw_toggle(rx + rw - 60, row_y + 6, desktop_get_start_gdesk()); break;
        case 1: draw_pushbtn(rx + rw - 180, row_y, 164, 32, "Clear layout", 0xE85A8C); break;
        case 2: draw_pushbtn(rx + rw - 180, row_y, 164, 32, "Save now",     0x4FB37A); break;
        }
    }
}

static void paint_settings_input(int rx, int ry, int rw) {
    fb_draw_text(rx + 10, ry + 10, "Input", ZB_TITLE_LEFT, FB_TRANSPARENT);
    fb_draw_text(rx + 10, ry + 28,
                 "Keyboard layout, repeat, and mouse tracking speed.",
                 ZB_PANEL_DARK, FB_TRANSPARENT);
    char km[8]; desktop_get_keymap(km, sizeof km);
    static const char *labels[4] = {
        "Keyboard layout", "Mouse speed",
        "Keyboard repeat delay", "Keyboard repeat rate"
    };
    for (int r = 0; r < 4; r++) {
        int row_y = settings_row_y(ry, r);
        int is_sel = (settings_sel == r && settings_tab == 2);
        if (is_sel) {
            fb_fill_rect(rx, row_y - 4, rw, 48, 0xD8DCE4);
            fb_bevel_sunken(rx, row_y - 4, rw, 48,
                            ZB_PANEL_LIGHT, ZB_PANEL_DARK);
        }
        fb_draw_text(rx + 12, row_y + 8, labels[r], ZB_BLACK, FB_TRANSPARENT);
        switch (r) {
        case 0: draw_cycler(rx + rw - 200, row_y, 184, 32,
                            km[0] == 'd' ? "DE QWERTZ" : "US QWERTY"); break;
        case 1: {
            int s = mouse_get_speed();
            draw_slider(rx + rw - 220, row_y + 6, 160, 20, s, 4, 24);
            char val[8]; ksnprintf(val, sizeof val, "%d", s);
            fb_draw_text(rx + rw - 50, row_y + 8, val, ZB_BLACK, FB_TRANSPARENT);
            break;
        }
        case 2: {
            int d = kb_get_repeat_delay();
            draw_slider(rx + rw - 220, row_y + 6, 160, 20, d, 250, 1000);
            char val[10]; ksnprintf(val, sizeof val, "%dms", d);
            fb_draw_text(rx + rw - 60, row_y + 8, val, ZB_BLACK, FB_TRANSPARENT);
            break;
        }
        case 3: {
            int r2 = kb_get_repeat_rate();
            draw_slider(rx + rw - 220, row_y + 6, 160, 20, r2, 2, 30);
            char val[10]; ksnprintf(val, sizeof val, "%d/s", r2);
            fb_draw_text(rx + rw - 50, row_y + 8, val, ZB_BLACK, FB_TRANSPARENT);
            break;
        }
        }
    }
}

static void paint_settings_sound(int rx, int ry, int rw) {
    fb_draw_text(rx + 10, ry + 10, "Sound", ZB_TITLE_LEFT, FB_TRANSPARENT);
    fb_draw_text(rx + 10, ry + 28,
                 "PC speaker output and notification volume.",
                 ZB_PANEL_DARK, FB_TRANSPARENT);
    static const char *labels[3] = {
        "Sound enabled", "Volume", "Test beep"
    };
    for (int r = 0; r < 3; r++) {
        int row_y = settings_row_y(ry, r);
        int is_sel = (settings_sel == r && settings_tab == 3);
        if (is_sel) {
            fb_fill_rect(rx, row_y - 4, rw, 48, 0xD8DCE4);
            fb_bevel_sunken(rx, row_y - 4, rw, 48,
                            ZB_PANEL_LIGHT, ZB_PANEL_DARK);
        }
        fb_draw_text(rx + 12, row_y + 8, labels[r], ZB_BLACK, FB_TRANSPARENT);
        switch (r) {
        case 0: draw_toggle(rx + rw - 60, row_y + 6, g_sound_enabled); break;
        case 1: {
            int v = g_sound_vol;
            draw_slider(rx + rw - 220, row_y + 6, 160, 20, v, 0, 16);
            char val[10]; ksnprintf(val, sizeof val, "%d/16", v);
            fb_draw_text(rx + rw - 50, row_y + 8, val, ZB_BLACK, FB_TRANSPARENT);
            break;
        }
        case 2: draw_pushbtn(rx + rw - 180, row_y, 164, 32, "Play beep", 0x47A6D4); break;
        }
    }
}

static void paint_settings_time(int rx, int ry, int rw) {
    extern void rtc_read(struct rtc_time *t);
    struct rtc_time t; rtc_read(&t);
    fb_draw_text(rx + 10, ry + 10, "Time", ZB_TITLE_LEFT, FB_TRANSPARENT);
    fb_draw_text(rx + 10, ry + 28,
                 "Real-time clock value. Persists in CMOS.",
                 ZB_PANEL_DARK, FB_TRANSPARENT);
    /* Big display: HH:MM:SS on top, DD/MM/YYYY below. Bevelled card. */
    int cx = rx + 16, cy = ry + 56, cw = rw - 32, ch = 80;
    fb_fill_rect(cx, cy, cw, ch, 0x0A1420);
    fb_bevel_sunken(cx, cy, cw, ch, 0x33495E, ZB_BLACK);
    char hms[16];
    ksnprintf(hms, sizeof hms, "%02u:%02u:%02u",
              (u32)t.hour, (u32)t.min, (u32)t.sec);
    /* Stretch 8x16 glyphs to 2x = 16x32 by drawing each glyph twice. */
    int gx = cx + 24, gy = cy + 14;
    for (int i = 0; hms[i]; i++) {
        for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++)
                fb_blit_glyph(gx + i * 16 + dx * 8, gy + dy * 16,
                              (u8)hms[i], ZB_ACCENT, FB_TRANSPARENT);
    }
    char ymd[20];
    ksnprintf(ymd, sizeof ymd, "%02u/%02u/%u",
              (u32)t.day, (u32)t.month, (u32)t.year);
    fb_draw_text(cx + 24, cy + ch - 22, ymd, 0xE8D8F4, FB_TRANSPARENT);
    /* Edit button. */
    int row_y = settings_row_y(ry, 0) + 96;
    int is_sel = (settings_sel == 0 && settings_tab == 4);
    if (is_sel) {
        fb_fill_rect(rx, row_y - 4, rw, 48, 0xD8DCE4);
        fb_bevel_sunken(rx, row_y - 4, rw, 48, ZB_PANEL_LIGHT, ZB_PANEL_DARK);
    }
    fb_draw_text(rx + 12, row_y + 8, "Set date / time", ZB_BLACK, FB_TRANSPARENT);
    draw_pushbtn(rx + rw - 180, row_y, 164, 32, "Open editor", 0xE85A8C);
}

static void paint_settings_security(int rx, int ry, int rw) {
    fb_draw_text(rx + 10, ry + 10, "Security", ZB_TITLE_LEFT, FB_TRANSPARENT);
    fb_draw_text(rx + 10, ry + 28,
                 "Screen-lock password protects the desktop.",
                 ZB_PANEL_DARK, FB_TRANSPARENT);
    char pw[24]; desktop_get_lock_password(pw, sizeof pw);
    static const char *labels[2] = { "Lock password", "Lock now" };
    for (int r = 0; r < 2; r++) {
        int row_y = settings_row_y(ry, r);
        int is_sel = (settings_sel == r && settings_tab == 5);
        if (is_sel) {
            fb_fill_rect(rx, row_y - 4, rw, 48, 0xD8DCE4);
            fb_bevel_sunken(rx, row_y - 4, rw, 48,
                            ZB_PANEL_LIGHT, ZB_PANEL_DARK);
        }
        fb_draw_text(rx + 12, row_y + 8, labels[r], ZB_BLACK, FB_TRANSPARENT);
        switch (r) {
        case 0: draw_pushbtn(rx + rw - 180, row_y, 164, 32,
                             pw[0] ? "Change ..." : "Set ...", 0xFFA831); break;
        case 1: draw_pushbtn(rx + rw - 180, row_y, 164, 32,
                             "Lock screen", 0x6447B0); break;
        }
    }
}

static void paint_settings(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, ZB_PANEL);
    /* Sidebar: 130 px wide column with a tab per category. Each tab
     * is a coloured tile on a slate background; the active tab gets
     * a sunken bevel + amber accent stripe. */
    int sb_w = 140;
    fb_fill_rect(bx + 6, by + 6, sb_w, bh - 12, 0x2C3340);
    fb_bevel_sunken(bx + 6, by + 6, sb_w, bh - 12,
                    0x4A5466, ZB_BLACK);
    /* Header on the sidebar. */
    fb_hgradient(bx + 8, by + 8, sb_w - 4, 28,
                 ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
    draw_zenbite_mark(bx + 12, by + 16, ZB_ACCENT);
    fb_draw_text(bx + 30, by + 14, "Settings",
                 ZB_ACCENT, FB_TRANSPARENT);
    for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
        int ty = by + 44 + t * 42;
        int active = (t == settings_tab);
        int tx = bx + 12;
        int tw = sb_w - 12;
        if (active) {
            fb_fill_rect(tx, ty, tw, 36, ZB_PANEL);
            fb_bevel_sunken(tx, ty, tw, 36, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
            fb_fill_rect(tx, ty, 4, 36, ZB_ACCENT);  /* amber accent */
        } else {
            fb_fill_rect(tx, ty, tw, 36, 0x3C4555);
            fb_bevel_raised(tx, ty, tw, 36, 0x5A6478, ZB_BLACK);
        }
        /* Coloured icon tile. */
        fb_fill_rect(tx + 10, ty + 8, 20, 20, settings_tab_colors[t]);
        fb_bevel_raised(tx + 10, ty + 8, 20, 20,
                        ZB_PANEL_LIGHT, ZB_BLACK);
        fb_draw_text(tx + 38, ty + 10, settings_tab_labels[t],
                     active ? ZB_BLACK : ZB_ACCENT, FB_TRANSPARENT);
    }
    /* Footer hint on the sidebar. */
    fb_draw_text(bx + 12, by + bh - 30,
                 "Tab: next pane",
                 0x9CA8B8, FB_TRANSPARENT);
    fb_draw_text(bx + 12, by + bh - 18,
                 "1-6: jump",
                 0x9CA8B8, FB_TRANSPARENT);
    /* Right pane container. */
    int rx = bx + sb_w + 16, ry = by + 8;
    int rw = bw - sb_w - 24, rh = bh - 16;
    fb_fill_rect(rx, ry, rw, rh, ZB_PANEL_LIGHT);
    fb_bevel_sunken(rx, ry, rw, rh, ZB_PANEL_LIGHT, ZB_PANEL_DARK);
    switch (settings_tab) {
    case 0: paint_settings_appearance(rx, ry, rw); break;
    case 1: paint_settings_system    (rx, ry, rw); break;
    case 2: paint_settings_input     (rx, ry, rw); break;
    case 3: paint_settings_sound     (rx, ry, rw); break;
    case 4: paint_settings_time      (rx, ry, rw); break;
    case 5: paint_settings_security  (rx, ry, rw); break;
    }
    /* Footer hint across the bottom of the right pane. */
    fb_draw_text(rx + 12, ry + rh - 22,
                 "Up/Down select  Left/Right change  ENTER activate  ESC close",
                 0x70808A, FB_TRANSPARENT);
}

/* Mouse routing: a click inside a settings window either selects a tab
 * (sidebar) or activates a row's control (right pane). Slider clicks
 * map their x position to the new value. */
static void settings_click(struct gwin *w, int mx, int my) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8;
    int sb_w = 140;
    /* Sidebar tabs. */
    for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
        int ty = by + 44 + t * 42;
        int tx = bx + 12;
        int tw = sb_w - 12;
        if (mx >= tx && mx < tx + tw && my >= ty && my < ty + 36) {
            if (settings_tab != t) { settings_tab = t; settings_sel = 0; }
            return;
        }
    }
    /* Right pane row hit-test. */
    int rx = bx + sb_w + 16, ry = by + 8;
    int rw = bw - sb_w - 24;
    int rows = settings_tab_rows[settings_tab];
    for (int r = 0; r < rows; r++) {
        int row_y = settings_row_y(ry, r);
        int row_h = 44;
        /* Time tab has the big clock above the row, shift it down. */
        if (settings_tab == 4) row_y += 96;
        if (my < row_y - 4 || my >= row_y + row_h - 4) continue;
        if (mx < rx || mx >= rx + rw) continue;
        settings_sel = r;
        /* Slider rows: map x to value. */
        int slider_x = rx + rw - 220;
        int slider_w = 160;
        if (mx >= slider_x && mx < slider_x + slider_w) {
            int rel = mx - slider_x;
            int span = slider_w - 12;
            if (rel < 0) rel = 0;
            if (rel > span) rel = span;
            if (settings_tab == 0 && r == 2) {           /* brightness */
                int v = 60 + rel * 80 / span;
                gdesk_set_brightness(v);
                mark_wallpaper_dirty();
                return;
            }
            if (settings_tab == 2 && r == 1) {           /* mouse speed */
                int v = 4 + rel * 20 / span;
                mouse_set_speed(v);
                return;
            }
            if (settings_tab == 2 && r == 2) {           /* kbd delay */
                int v = 250 + rel * 750 / span;
                kb_set_repeat(v, kb_get_repeat_rate());
                return;
            }
            if (settings_tab == 2 && r == 3) {           /* kbd rate */
                int v = 2 + rel * 28 / span;
                kb_set_repeat(kb_get_repeat_delay(), v);
                return;
            }
            if (settings_tab == 3 && r == 1) {           /* volume */
                int v = rel * 16 / span;
                g_sound_vol = v;
                return;
            }
        }
        /* Cycler buttons: < value > -- left third decreases,
         * right third increases, middle = next. */
        int cyc_x = rx + rw - 200;
        int cyc_w = 184;
        if (mx >= cyc_x && mx < cyc_x + cyc_w) {
            int rel = mx - cyc_x;
            int delta = (rel < cyc_w / 3) ? -1
                      : (rel > 2 * cyc_w / 3) ? +1 : +1;
            settings_cycle(settings_tab, r, delta);
            return;
        }
        /* Default: activate (push buttons, toggles). */
        settings_cycle(settings_tab, r, +1);
        return;
    }
}

/* === Pixel app: Snake =============================================== */
#define SNK_W 32
#define SNK_H 18
#define SNK_MAX (SNK_W * SNK_H)
static int snk_x[SNK_MAX], snk_y[SNK_MAX];
static int snk_len;
static int snk_dir; /* 0=R 1=D 2=L 3=U */
static int snk_food_x, snk_food_y;
static u32 snk_last_tick;
static int snk_dead;
static int snk_score;
static u32 snk_rng = 0x12345;

static int snk_rand(int mod) {
    snk_rng = snk_rng * 1103515245 + 12345;
    return (int)((snk_rng >> 16) & 0x7FFF) % mod;
}

static void snk_place_food(void) {
    for (int tries = 0; tries < 200; tries++) {
        int fx = snk_rand(SNK_W);
        int fy = snk_rand(SNK_H);
        int hit = 0;
        for (int i = 0; i < snk_len; i++)
            if (snk_x[i] == fx && snk_y[i] == fy) { hit = 1; break; }
        if (!hit) { snk_food_x = fx; snk_food_y = fy; return; }
    }
}

static void snk_reset(void) {
    snk_len = 4;
    for (int i = 0; i < snk_len; i++) { snk_x[i] = 8 - i; snk_y[i] = SNK_H / 2; }
    snk_dir = 0;
    snk_dead = 0;
    snk_score = 0;
    snk_last_tick = pit_ticks();
    snk_rng ^= pit_ticks();
    snk_place_food();
}

static void snk_step(void) {
    if (snk_dead) return;
    int nx = snk_x[0], ny = snk_y[0];
    switch (snk_dir) {
    case 0: nx++; break; case 1: ny++; break;
    case 2: nx--; break; case 3: ny--; break;
    }
    if (nx < 0 || nx >= SNK_W || ny < 0 || ny >= SNK_H) { snk_dead = 1; return; }
    for (int i = 0; i < snk_len; i++)
        if (snk_x[i] == nx && snk_y[i] == ny) { snk_dead = 1; return; }
    int grew = (nx == snk_food_x && ny == snk_food_y);
    int new_len = grew ? snk_len + 1 : snk_len;
    if (new_len > SNK_MAX) new_len = SNK_MAX;
    for (int i = new_len - 1; i > 0; i--) { snk_x[i] = snk_x[i-1]; snk_y[i] = snk_y[i-1]; }
    snk_x[0] = nx; snk_y[0] = ny;
    snk_len = new_len;
    if (grew) { snk_score++; snk_place_food(); }
}

static void snk_key(int k) {
    if (k == KB_RIGHT && snk_dir != 2) snk_dir = 0;
    else if (k == KB_DOWN  && snk_dir != 3) snk_dir = 1;
    else if (k == KB_LEFT  && snk_dir != 0) snk_dir = 2;
    else if (k == KB_UP    && snk_dir != 1) snk_dir = 3;
    else if (k == 'r' || k == 'R' || (snk_dead && (k == 10 || k == 13))) snk_reset();
}

static void paint_snake(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0x0E1B2A);
    if (snk_len == 0) snk_reset();
    /* Auto-step every ~150 ms. */
    u32 now = pit_ticks();
    if (!snk_dead && now - snk_last_tick > 150) {
        snk_step();
        snk_last_tick = now;
    }
    int cell = 16;
    int gw = SNK_W * cell, gh = SNK_H * cell;
    int gx = bx + (bw - gw) / 2;
    int gy = by + 36;
    fb_fill_rect(gx, gy, gw, gh, 0x07101C);
    fb_bevel_sunken(gx, gy, gw, gh, 0x33495E, ZB_BLACK);
    /* Food (pink dot). */
    fb_fill_rect(gx + snk_food_x * cell + 3, gy + snk_food_y * cell + 3,
                 cell - 6, cell - 6, 0xE85A8C);
    /* Snake body (green gradient). */
    for (int i = 0; i < snk_len; i++) {
        u32 col = (i == 0) ? 0x9CE89C : 0x4FB37A;
        fb_fill_rect(gx + snk_x[i] * cell + 1, gy + snk_y[i] * cell + 1,
                     cell - 2, cell - 2, col);
    }
    char hdr[48];
    ksnprintf(hdr, sizeof hdr, "Score: %d   %s",
              snk_score, snk_dead ? "[DEAD - press ENTER to restart]" : "");
    fb_draw_text(bx + 12, by + 12, hdr, 0xFFFFFF, FB_TRANSPARENT);
    fb_draw_text(bx + 12, by + bh - 18,
                 "Arrow keys steer.  ESC closes the window.",
                 0x70808A, FB_TRANSPARENT);
}

/* === Lock screen ==================================================== */
static int  lock_active;
static char lock_input[24];
static int  lock_input_len;
static int  lock_wrong;

static void paint_lock(void) {
    int W = fb_w(), H = fb_h();
    /* Full-screen indigo wash with the Zenbite mark + a password box. */
    fb_fill_rect(0, 0, W, H, 0x14182C);
    /* Decorative mark. */
    int mx = W / 2 - 24, my = H / 2 - 160;
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12; c++)
            if (zenbite_z_mark[r * 12 + c])
                fb_fill_rect(mx + c * 4, my + r * 4, 4, 4, ZB_ACCENT);
    fb_draw_text(W / 2 - 7 * 8 / 2, my + 64,
                 "Zenbite", ZB_ACCENT, FB_TRANSPARENT);
    fb_draw_text(W / 2 - 7 * 8 / 2 - 24, my + 84,
                 "system locked", 0xC8D4E0, FB_TRANSPARENT);
    /* Password box. */
    int bx = W / 2 - 140, by = H / 2;
    fb_fill_rect(bx, by, 280, 44, 0x202C40);
    fb_bevel_raised(bx, by, 280, 44, 0x3C4860, ZB_BLACK);
    fb_draw_text(bx + 12, by + 6, "Password:", 0xC8D4E0, FB_TRANSPARENT);
    /* Mask input as bullets. */
    char mask[20]; int n = 0;
    for (int i = 0; i < lock_input_len && n < 18; i++) mask[n++] = '*';
    mask[n] = 0;
    fb_draw_text(bx + 100, by + 6, mask, ZB_ACCENT, FB_TRANSPARENT);
    /* Underline. */
    fb_fill_rect(bx + 100, by + 26, 168, 2, 0x6447B0);
    if (lock_wrong) {
        fb_draw_text(W / 2 - 13 * 8 / 2, by + 56,
                     "wrong password", 0xE85A8C, FB_TRANSPARENT);
    }
    {
        char stored[24];
        desktop_get_lock_password(stored, sizeof stored);
        if (stored[0] == '\0') {
            fb_draw_text(W / 2 - 32 * 8 / 2, H - 40,
                         "no password set -- press ENTER to unlock",
                         0x70808A, FB_TRANSPARENT);
        } else {
            fb_draw_text(W / 2 - 28 * 8 / 2, H - 40,
                         "set the password in Settings -> Password",
                         0x70808A, FB_TRANSPARENT);
        }
    }
}

/* Blocks the desktop loop until the right password is typed. */
static void lock_run(void) {
    lock_active = 1;
    lock_input_len = 0;
    lock_input[0] = 0;
    lock_wrong = 0;
    paint_lock();
    fb_present();
    while (lock_active) {
        int k = kb_getc();
        if (k == 10 || k == 13) {
            lock_input[lock_input_len] = 0;
            char stored[24];
            desktop_get_lock_password(stored, sizeof stored);
            /* Empty stored password disables the lock -- ENTER lets
             * you back in immediately. Same behaviour as desktops
             * with no login configured. */
            if (stored[0] == '\0' || strcmp(lock_input, stored) == 0) {
                lock_active = 0;
                break;
            }
            lock_wrong = 1;
            lock_input_len = 0; lock_input[0] = 0;
        } else if (k == 8 || k == 0x7F) {
            if (lock_input_len > 0) lock_input[--lock_input_len] = 0;
            lock_wrong = 0;
        } else if (k == 27) {
            /* ESC just clears the field -- you can't escape the lock. */
            lock_input_len = 0; lock_input[0] = 0;
            lock_wrong = 0;
        } else if (k >= 32 && k < 127 &&
                   lock_input_len + 1 < (int)sizeof lock_input) {
            lock_input[lock_input_len++] = (char)k;
            lock_input[lock_input_len] = 0;
        }
        paint_lock();
        fb_present();
    }
}

/* === Pixel app: Analog Clock ======================================== */
/* Draws a clock face + hour/minute/second hands from the RTC. Re-renders
 * every paint, so the desktop's normal repaint cadence drives it. */
static void paint_aclock(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0x101C2A);
    int cx = bx + bw / 2;
    int cy = by + bh / 2;
    int r = (bw < bh ? bw : bh) / 2 - 16;
    /* Face: filled disc + ring. */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r) {
                u32 c = (d2 > (r - 4) * (r - 4)) ? ZB_ACCENT : 0xE8ECF1;
                fb_pixel(cx + dx, cy + dy, c);
            }
        }
    }
    /* Hour marks: 12 short ticks. */
    for (int i = 0; i < 12; i++) {
        /* Lookup tables for sin/cos at 30deg steps -- avoid floats. */
        static const int sx[12] = { 0, 5, 9,10, 9, 5, 0,-5,-9,-10,-9,-5 };
        static const int sy[12] = {-10,-9,-5, 0, 5, 9,10, 9, 5, 0,-5,-9 };
        int x1 = cx + sx[i] * (r - 4) / 10;
        int y1 = cy + sy[i] * (r - 4) / 10;
        int x2 = cx + sx[i] * (r - 12) / 10;
        int y2 = cy + sy[i] * (r - 12) / 10;
        /* Bresenham-ish line. */
        int dx = x2 - x1, dy = y2 - y1;
        int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                  ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        if (steps == 0) steps = 1;
        for (int s = 0; s <= steps; s++) {
            int px = x1 + dx * s / steps;
            int py = y1 + dy * s / steps;
            fb_fill_rect(px - 1, py - 1, 3, 3, 0x1E2A38);
        }
    }
    struct rtc_time t; rtc_read(&t);
    /* Hands. 60 ticks → angle index. We use 60-step LUTs scaled to r. */
    static const int sin60[60] = {
          0, 10, 21, 31, 41, 50, 59, 67, 74, 81,
         87, 92, 95, 98, 99,100, 99, 98, 95, 92,
         87, 81, 74, 67, 59, 50, 41, 31, 21, 10,
          0,-10,-21,-31,-41,-50,-59,-67,-74,-81,
        -87,-92,-95,-98,-99,-100,-99,-98,-95,-92,
        -87,-81,-74,-67,-59,-50,-41,-31,-21,-10
    };
    #define COS60(i) sin60[((i) + 15) % 60]
    int hh = t.hour % 12;
    int mm = t.min  % 60;
    int ss = t.sec  % 60;
    int hi = (hh * 5 + mm / 12) % 60;
    /* Draw hand at index `i` from centre to length `len`, colour c. */
    #define HAND(i, len, c) do {                                       \
        int hx = cx + sin60[i] * (len) / 100;                          \
        int hy = cy - COS60(i) * (len) / 100;                          \
        int dx2 = hx - cx, dy2 = hy - cy;                              \
        int adx = dx2 < 0 ? -dx2 : dx2;                                \
        int ady = dy2 < 0 ? -dy2 : dy2;                                \
        int st = adx > ady ? adx : ady;                                \
        if (st == 0) st = 1;                                           \
        for (int s = 0; s <= st; s++) {                                \
            int px = cx + dx2 * s / st;                                \
            int py = cy + dy2 * s / st;                                \
            fb_fill_rect(px - 1, py - 1, 3, 3, (c));                   \
        }                                                              \
    } while (0)
    HAND(hi, r - 32, 0x1E2A38);    /* hour */
    HAND(mm, r - 18, 0x2C2152);    /* minute */
    HAND(ss, r - 12, 0xE85A8C);    /* second */
    fb_fill_rect(cx - 3, cy - 3, 6, 6, 0x1E2A38);
    /* Date strip at the bottom. */
    char date[24];
    ksnprintf(date, sizeof date, "%04u-%02u-%02u   %02u:%02u",
              (u32)t.year, (u32)t.month, (u32)t.day,
              (u32)t.hour, (u32)t.min);
    fb_draw_text(bx + (bw - 19 * 8) / 2, by + bh - 22,
                 date, 0xC8D4E0, FB_TRANSPARENT);
}

/* === Pixel app: Calendar ============================================ */
static int  cal_year, cal_month;       /* 1..12 */
static int  cal_initted;

static int cal_is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}
static int cal_days_in_month(int y, int m) {
    static const int t[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && cal_is_leap(y)) return 29;
    return t[m - 1];
}
/* Zeller's congruence -> weekday (0 = Sunday). */
static int cal_weekday(int y, int m, int d) {
    if (m < 3) { m += 12; y--; }
    int K = y % 100;
    int J = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    /* Zeller h=0 means Saturday; convert to Sunday=0. */
    return (h + 6) % 7;
}

static void cal_init(void) {
    struct rtc_time t; rtc_read(&t);
    cal_year = t.year;
    cal_month = t.month;
    if (cal_month < 1 || cal_month > 12) cal_month = 1;
    cal_initted = 1;
}

static void cal_key(int k) {
    if (!cal_initted) cal_init();
    if (k == KB_LEFT) {
        cal_month--;
        if (cal_month < 1) { cal_month = 12; cal_year--; }
    } else if (k == KB_RIGHT) {
        cal_month++;
        if (cal_month > 12) { cal_month = 1; cal_year++; }
    } else if (k == KB_UP)   { cal_year++; }
    else if (k == KB_DOWN) { cal_year--; }
    else if (k == 'h' || k == 'H' || k == 10 || k == 13) {
        struct rtc_time t; rtc_read(&t);
        cal_year = t.year; cal_month = t.month;
    }
}

static void paint_calendar(struct gwin *w) {
    if (!cal_initted) cal_init();
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0xF0F2F6);
    static const char *mon[] = { "January","February","March","April","May","June",
                                 "July","August","September","October","November","December" };
    char hdr[64];
    ksnprintf(hdr, sizeof hdr, "%s %d", mon[cal_month - 1], cal_year);
    /* Header bar. */
    fb_hgradient(bx + 8, by + 8, bw - 16, 30, ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
    int hlen = (int)strlen(hdr);
    fb_draw_text(bx + (bw - hlen * 8) / 2, by + 16, hdr, ZB_TITLE_TEXT, FB_TRANSPARENT);
    /* Day-of-week row. */
    static const char *dow[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
    int cell_w = (bw - 24) / 7;
    int cell_h = (bh - 80) / 7;
    if (cell_h < 28) cell_h = 28;
    int gx = bx + 12, gy = by + 50;
    for (int i = 0; i < 7; i++) {
        u32 fg = (i == 0 || i == 6) ? 0xE85A8C : 0x2C2152;
        fb_draw_text(gx + i * cell_w + (cell_w - 3 * 8) / 2, gy,
                     dow[i], fg, FB_TRANSPARENT);
    }
    /* Days. */
    int dim = cal_days_in_month(cal_year, cal_month);
    int first_dow = cal_weekday(cal_year, cal_month, 1);
    struct rtc_time today; rtc_read(&today);
    for (int d = 1; d <= dim; d++) {
        int pos = first_dow + d - 1;
        int col = pos % 7;
        int row = pos / 7;
        int x = gx + col * cell_w;
        int y = gy + 20 + row * cell_h;
        int is_today = (d == today.day &&
                        cal_month == today.month &&
                        cal_year == today.year);
        if (is_today) {
            fb_fill_rect(x + 2, y, cell_w - 4, cell_h - 4, ZB_ACCENT);
        }
        u32 fg = is_today ? 0x1E2A38
              : ((col == 0 || col == 6) ? 0xE85A8C : 0x1E2A38);
        char num[4];
        ksnprintf(num, sizeof num, "%d", d);
        int nlen = (int)strlen(num);
        fb_draw_text(x + cell_w - 4 - nlen * 8, y + 4, num, fg, FB_TRANSPARENT);
    }
    fb_draw_text(bx + 12, by + bh - 22,
                 "Left/Right month   Up/Down year   H=today",
                 0x70808A, FB_TRANSPARENT);
}

/* === Pixel app: Minesweeper ========================================== */
#define MS_W 16
#define MS_H 12
#define MS_MINES 24
static u8  ms_mine [MS_W * MS_H];
static u8  ms_open [MS_W * MS_H];
static u8  ms_flag [MS_W * MS_H];
static int ms_initted;
static int ms_dead;
static int ms_won;
static int ms_cur_x, ms_cur_y;
static u32 ms_rng2 = 0xC0FFEE;

static int ms_rand(int mod) {
    ms_rng2 = ms_rng2 * 1103515245 + 12345;
    return (int)((ms_rng2 >> 16) & 0x7FFF) % mod;
}

static void ms_reset(void) {
    for (int i = 0; i < MS_W * MS_H; i++) {
        ms_mine[i] = ms_open[i] = ms_flag[i] = 0;
    }
    int placed = 0;
    ms_rng2 ^= pit_ticks();
    while (placed < MS_MINES) {
        int idx = ms_rand(MS_W * MS_H);
        if (!ms_mine[idx]) { ms_mine[idx] = 1; placed++; }
    }
    ms_initted = 1;
    ms_dead = ms_won = 0;
    ms_cur_x = ms_cur_y = 0;
}

static int ms_count_around(int x, int y) {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (dx == 0 && dy == 0) continue;
            if (nx < 0 || nx >= MS_W || ny < 0 || ny >= MS_H) continue;
            if (ms_mine[ny * MS_W + nx]) n++;
        }
    return n;
}

static void ms_flood(int x, int y) {
    if (x < 0 || x >= MS_W || y < 0 || y >= MS_H) return;
    int i = y * MS_W + x;
    if (ms_open[i] || ms_flag[i]) return;
    ms_open[i] = 1;
    if (ms_count_around(x, y) != 0) return;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) ms_flood(x + dx, y + dy);
}

static void ms_check_win(void) {
    int covered = 0;
    for (int i = 0; i < MS_W * MS_H; i++)
        if (!ms_open[i] && !ms_mine[i]) { covered = 1; break; }
    if (!covered) ms_won = 1;
}

static void ms_key(int k) {
    if (!ms_initted) ms_reset();
    if (k == 'r' || k == 'R' || ((ms_dead || ms_won) && (k == 10 || k == 13))) {
        ms_reset(); return;
    }
    if (ms_dead || ms_won) return;
    if (k == KB_LEFT  && ms_cur_x > 0)        ms_cur_x--;
    if (k == KB_RIGHT && ms_cur_x < MS_W - 1) ms_cur_x++;
    if (k == KB_UP    && ms_cur_y > 0)        ms_cur_y--;
    if (k == KB_DOWN  && ms_cur_y < MS_H - 1) ms_cur_y++;
    int idx = ms_cur_y * MS_W + ms_cur_x;
    if (k == ' ') {
        if (ms_flag[idx]) return;
        if (ms_mine[idx]) { ms_open[idx] = 1; ms_dead = 1; return; }
        ms_flood(ms_cur_x, ms_cur_y);
        ms_check_win();
    } else if (k == 'f' || k == 'F') {
        if (!ms_open[idx]) ms_flag[idx] = !ms_flag[idx];
    }
}

static void paint_mines(struct gwin *w) {
    if (!ms_initted) ms_reset();
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0x171F2A);
    int flags = 0;
    for (int i = 0; i < MS_W * MS_H; i++) if (ms_flag[i]) flags++;
    char hdr[64];
    ksnprintf(hdr, sizeof hdr, "Mines: %d/%d   Flags: %d   %s",
              MS_MINES, MS_W * MS_H, flags,
              ms_dead ? "BOOM -- ENTER to restart"
                      : (ms_won ? "WON! ENTER to play again" : ""));
    fb_draw_text(bx + 12, by + 12, hdr, 0xFFFFFF, FB_TRANSPARENT);
    int cell = 24;
    int gw = MS_W * cell;
    int gx = bx + (bw - gw) / 2;
    int gy = by + 40;
    for (int y = 0; y < MS_H; y++) {
        for (int x = 0; x < MS_W; x++) {
            int idx = y * MS_W + x;
            int rx = gx + x * cell, ry = gy + y * cell;
            int sel = (x == ms_cur_x && y == ms_cur_y);
            u32 bg = ms_open[idx] ? 0x33495E : 0x1E2A38;
            if (sel) bg = ms_open[idx] ? 0x5C7088 : 0x394C66;
            fb_fill_rect(rx + 1, ry + 1, cell - 2, cell - 2, bg);
            if (sel)
                fb_hline(rx, ry, cell, ZB_ACCENT);
            if (ms_open[idx]) {
                if (ms_mine[idx]) {
                    fb_fill_rect(rx + 6, ry + 6, cell - 12, cell - 12, 0xE85A8C);
                } else {
                    int n = ms_count_around(x, y);
                    if (n > 0) {
                        char d[2] = { (char)('0' + n), 0 };
                        u32 nc[9] = {0,0x9CD0FF,0x9CE89C,0xFFA831,
                                     0xE85A8C,0x6447B0,0x47A6D4,0xFFFFFF,0xE85A8C };
                        fb_draw_text(rx + (cell - 8) / 2,
                                     ry + (cell - 16) / 2 + 1,
                                     d, nc[n], FB_TRANSPARENT);
                    }
                }
            } else if (ms_flag[idx]) {
                fb_fill_rect(rx + 6, ry + 6, cell - 12, cell - 12, ZB_ACCENT);
            }
        }
    }
    fb_draw_text(bx + 12, by + bh - 22,
                 "Arrows move  SPACE open  F flag  R restart",
                 0x70808A, FB_TRANSPARENT);
}

/* === Pixel app: Tetris =============================================== */
#define TT_W 10
#define TT_H 20
static u8  tt_grid[TT_W * TT_H];
static int tt_initted;
static int tt_piece;     /* 0..6 */
static int tt_rot;       /* 0..3 */
static int tt_px, tt_py;
static u32 tt_last_tick;
static int tt_dead;
static int tt_score;
static u32 tt_rng3 = 0xBADBEEF;

static int tt_rand7(void) {
    tt_rng3 = tt_rng3 * 1103515245 + 12345;
    return (int)((tt_rng3 >> 16) & 0x7FFF) % 7;
}

/* Tetromino shapes, 4 rotations, 4 cells each (col, row). */
static const int tt_shapes[7][4][4][2] = {
 /* I */ { {{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}}, {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}} },
 /* O */ { {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}} },
 /* T */ { {{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}} },
 /* S */ { {{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}} },
 /* Z */ { {{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}} },
 /* J */ { {{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}} },
 /* L */ { {{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}} },
};
static const u32 tt_colors[7] = {
    0x47A6D4, 0xFFA831, 0x6447B0, 0x4FB37A,
    0xE85A8C, 0x2C2152, 0xFFA831
};

static int tt_fits(int piece, int rot, int px, int py) {
    for (int i = 0; i < 4; i++) {
        int x = px + tt_shapes[piece][rot][i][0];
        int y = py + tt_shapes[piece][rot][i][1];
        if (x < 0 || x >= TT_W || y >= TT_H) return 0;
        if (y >= 0 && tt_grid[y * TT_W + x]) return 0;
    }
    return 1;
}

static void tt_lock(void) {
    for (int i = 0; i < 4; i++) {
        int x = tt_px + tt_shapes[tt_piece][tt_rot][i][0];
        int y = tt_py + tt_shapes[tt_piece][tt_rot][i][1];
        if (y >= 0 && y < TT_H && x >= 0 && x < TT_W)
            tt_grid[y * TT_W + x] = (u8)(tt_piece + 1);
    }
    /* Clear full rows. */
    int cleared = 0;
    for (int y = TT_H - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < TT_W; x++) if (!tt_grid[y * TT_W + x]) { full = 0; break; }
        if (full) {
            for (int yy = y; yy > 0; yy--)
                for (int x = 0; x < TT_W; x++)
                    tt_grid[yy * TT_W + x] = tt_grid[(yy - 1) * TT_W + x];
            for (int x = 0; x < TT_W; x++) tt_grid[x] = 0;
            cleared++; y++;
        }
    }
    static const int row_score[5] = { 0, 100, 300, 500, 800 };
    if (cleared > 4) cleared = 4;
    tt_score += row_score[cleared];
}

static void tt_new_piece(void) {
    tt_piece = tt_rand7();
    tt_rot = 0;
    tt_px = (TT_W / 2) - 2;
    tt_py = 0;
    if (!tt_fits(tt_piece, tt_rot, tt_px, tt_py)) tt_dead = 1;
}

static void tt_reset(void) {
    for (int i = 0; i < TT_W * TT_H; i++) tt_grid[i] = 0;
    tt_initted = 1;
    tt_dead = 0;
    tt_score = 0;
    tt_last_tick = pit_ticks();
    tt_rng3 ^= pit_ticks();
    tt_new_piece();
}

static void tt_step(void) {
    if (tt_dead) return;
    if (tt_fits(tt_piece, tt_rot, tt_px, tt_py + 1)) {
        tt_py++;
    } else {
        tt_lock();
        tt_new_piece();
    }
}

static void tt_key(int k) {
    if (!tt_initted) tt_reset();
    if (tt_dead) {
        if (k == 10 || k == 13 || k == 'r' || k == 'R') tt_reset();
        return;
    }
    if (k == KB_LEFT  && tt_fits(tt_piece, tt_rot, tt_px - 1, tt_py)) tt_px--;
    else if (k == KB_RIGHT && tt_fits(tt_piece, tt_rot, tt_px + 1, tt_py)) tt_px++;
    else if (k == KB_DOWN  && tt_fits(tt_piece, tt_rot, tt_px, tt_py + 1)) tt_py++;
    else if (k == KB_UP || k == 'x' || k == 'X') {
        int nr = (tt_rot + 1) % 4;
        if (tt_fits(tt_piece, nr, tt_px, tt_py)) tt_rot = nr;
    } else if (k == ' ') {
        while (tt_fits(tt_piece, tt_rot, tt_px, tt_py + 1)) tt_py++;
        tt_step();
    }
}

static void paint_tetris(struct gwin *w) {
    if (!tt_initted) tt_reset();
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0x0E1B2A);
    /* Tick the gravity. */
    u32 now = pit_ticks();
    if (!tt_dead && now - tt_last_tick > 400) {
        tt_step();
        tt_last_tick = now;
    }
    int cell = 22;
    int gw = TT_W * cell, gh = TT_H * cell;
    int gx = bx + 16;
    int gy = by + 16;
    fb_fill_rect(gx - 4, gy - 4, gw + 8, gh + 8, 0x040A12);
    fb_hline(gx - 4, gy - 4, gw + 8, 0x33495E);
    fb_hline(gx - 4, gy + gh + 3, gw + 8, 0x33495E);
    fb_vline(gx - 4, gy - 4, gh + 8, 0x33495E);
    fb_vline(gx + gw + 3, gy - 4, gh + 8, 0x33495E);
    /* Settled cells. */
    for (int y = 0; y < TT_H; y++) {
        for (int x = 0; x < TT_W; x++) {
            u8 v = tt_grid[y * TT_W + x];
            if (!v) continue;
            fb_fill_rect(gx + x * cell + 1, gy + y * cell + 1,
                         cell - 2, cell - 2, tt_colors[v - 1]);
        }
    }
    /* Active piece. */
    if (!tt_dead) {
        for (int i = 0; i < 4; i++) {
            int x = tt_px + tt_shapes[tt_piece][tt_rot][i][0];
            int y = tt_py + tt_shapes[tt_piece][tt_rot][i][1];
            if (y < 0) continue;
            fb_fill_rect(gx + x * cell + 1, gy + y * cell + 1,
                         cell - 2, cell - 2, tt_colors[tt_piece]);
        }
    }
    /* Side panel. */
    int sx = gx + gw + 24;
    fb_draw_text(sx, gy,        "TETRIS",          ZB_ACCENT,       FB_TRANSPARENT);
    char buf[32];
    ksnprintf(buf, sizeof buf, "Score: %d", tt_score);
    fb_draw_text(sx, gy + 36,   buf,              0xFFFFFF,         FB_TRANSPARENT);
    fb_draw_text(sx, gy + 80,   "Left/Right",      0xC8D4E0,        FB_TRANSPARENT);
    fb_draw_text(sx, gy + 96,   "Up = rotate",     0xC8D4E0,        FB_TRANSPARENT);
    fb_draw_text(sx, gy + 112,  "Down = soft drop",0xC8D4E0,        FB_TRANSPARENT);
    fb_draw_text(sx, gy + 128,  "Space = drop",    0xC8D4E0,        FB_TRANSPARENT);
    if (tt_dead) {
        fb_draw_text(sx, gy + 180, "GAME OVER", 0xE85A8C, FB_TRANSPARENT);
        fb_draw_text(sx, gy + 196, "ENTER = new game", 0xC8D4E0, FB_TRANSPARENT);
    }
}

/* === Pixel app: Web Browser =============================================
 * A minimal HTTP browser.  The URL bar accepts http://A.B.C.D/path URLs.
 * http_get fetches the response body to a temp file; we read it back and
 * render it as plain text in the content area.  The fetch runs through
 * shell_run_async so the desktop stays responsive. */
#define BR_URL_MAX  128
#define BR_BODY_MAX 8192
static char  br_url[BR_URL_MAX] = "http://10.0.2.2/";
static int   br_url_len = 16;        /* cursor position in URL bar */
static int   br_url_focus = 1;       /* 1 = URL bar has focus */
static char  br_body[BR_BODY_MAX];
static int   br_body_len;
static int   br_scroll;
static int   br_loading;             /* 0=idle, 1=fetching */
static int   br_status_code;         /* HTTP status from last fetch */

/* Temp file for http_get output. */
#define BR_TMP_PATH "\\SYSTEM\\BROWSER.TMP"

/* bg_* helpers for async fetch (runs in a background process). */
static int   br_bg_pid = -1;
static char  br_bg_url[BR_URL_MAX];

static void br_bg_entry(void) {
    extern void vga_redirect(char *buf, u32 cap, u32 *len);
    vga_redirect(NULL, 0, NULL);   /* don't capture kernel prints */
    /* Fetch the URL to a temp file. */
    br_status_code = http_get(br_bg_url, BR_TMP_PATH);
    /* Read the response body back. */
    int h = fs_open(BR_TMP_PATH);
    br_body_len = 0;
    if (h >= 0) {
        int n;
        while (br_body_len < BR_BODY_MAX - 1 &&
               (n = fs_read(h, br_body + br_body_len, BR_BODY_MAX - 1 - br_body_len)) > 0)
            br_body_len += n;
        fs_close(h);
    }
    br_body[br_body_len] = '\0';
    br_scroll = 0;
    br_loading = 0;
    /* Mark ourselves zombie. */
    extern struct proc procs[];
    extern int current_pid;
    procs[current_pid].state = PROC_ZOMBIE;
    extern void proc_yield(void);
    proc_yield();
    for (;;) __asm__ volatile("cli; hlt");
}

static void br_start_fetch(void) {
    if (br_loading) return;
    br_loading = 1;
    br_body_len = 0;
    br_body[0] = '\0';
    br_status_code = 0;
    strncpy(br_bg_url, br_url, BR_URL_MAX - 1);
    br_bg_url[BR_URL_MAX - 1] = '\0';
    /* Use the shell async mechanism: just shell_run_async a fetch command. */
    /* Simpler: spawn our own process. */
    extern int proc_create(const char *name, void (*entry)(void), u32 prio);
    br_bg_pid = proc_create("br-fetch", br_bg_entry, 1 /* PRIO_NORMAL */);
}

static void paint_browser(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0xF0F2F6);

    /* URL bar. */
    int url_y = by + 8;
    fb_fill_rect(bx + 8, url_y, bw - 80, 22, 0xFFFFFF);
    fb_bevel_sunken(bx + 8, url_y, bw - 80, 22, 0xC4C8CC, ZB_BLACK);
    fb_draw_text(bx + 12, url_y + 5, br_url, ZB_BLACK, FB_TRANSPARENT);
    /* Cursor in URL bar. */
    if (br_url_focus) {
        int cx = bx + 12 + br_url_len * 8;
        fb_fill_rect(cx, url_y + 4, 2, 14, ZB_BLACK);
    }
    /* Go button. */
    int go_x = bx + bw - 64, go_y = url_y;
    fb_fill_rect(go_x, go_y, 56, 22, br_loading ? 0x8A93A6 : ZB_TITLE_LEFT);
    fb_bevel_raised(go_x, go_y, 56, 22, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    fb_draw_text(go_x + 16, go_y + 5, "GO!", br_loading ? 0xC8C8C8 : 0xFFFFFF, FB_TRANSPARENT);

    /* Status bar. */
    int st_y = by + bh - 20;
    fb_fill_rect(bx + 4, st_y, bw - 8, 16, 0xE0E4EA);
    fb_bevel_sunken(bx + 4, st_y, bw - 8, 16, 0xC4C8CC, ZB_BLACK);
    char status[80];
    if (br_loading)
        ksnprintf(status, sizeof status, "Fetching %s ...", br_url);
    else if (br_status_code)
        ksnprintf(status, sizeof status, "%s  (%d bytes)  [scroll: Up/Down]",
                  br_status_code >= 0 ? "Done" : "Error",
                  br_body_len);
    else
        ksnprintf(status, sizeof status, "Enter a URL and press GO or Enter");
    fb_draw_text(bx + 10, st_y + 3, status, 0x1E2A38, FB_TRANSPARENT);

    /* Content area. */
    int cx = bx + 4, cy = url_y + 28;
    int cw = bw - 8, ch = st_y - cy - 4;
    fb_fill_rect(cx, cy, cw, ch, 0xFFFFFF);
    fb_bevel_sunken(cx, cy, cw, ch, 0xC4C8CC, ZB_BLACK);

    if (br_body_len == 0 && !br_loading) {
        fb_draw_text(cx + 12, cy + 12,
                     "Web Browser - Zenbite v" ZENBITE_VERSION,
                     0x70808A, FB_TRANSPARENT);
        fb_draw_text(cx + 12, cy + 32,
                     "Enter an http:// URL above and press GO.",
                     0x70808A, FB_TRANSPARENT);
        fb_draw_text(cx + 12, cy + 52,
                     "Note: Only plain-text display (no HTML rendering).",
                     0x9AA0AA, FB_TRANSPARENT);
        fb_draw_text(cx + 12, cy + 72,
                     "Try: http://10.0.2.2/  (QEMU host)",
                     0x9AA0AA, FB_TRANSPARENT);
    } else if (br_body_len > 0) {
        /* Render body as plain text, line by line, with scrolling. */
        int line_h = 14;
        int visible = ch / line_h;
        /* Count total lines. */
        int total_lines = 1;
        for (int i = 0; i < br_body_len; i++)
            if (br_body[i] == '\n') total_lines++;
        if (br_scroll > total_lines - visible) br_scroll = total_lines - visible;
        if (br_scroll < 0) br_scroll = 0;
        int row = 0, line_start = 0;
        for (int i = 0; i <= br_body_len && row - br_scroll < visible; i++) {
            if (i == br_body_len || br_body[i] == '\n' || br_body[i] == '\r') {
                if (row >= br_scroll && row - br_scroll < visible) {
                    int disp_y = cy + 4 + (row - br_scroll) * line_h;
                    /* Truncate long lines to fit content width. */
                    int chars = i - line_start;
                    int max_chars = (cw - 16) / 8;
                    if (chars > max_chars) chars = max_chars;
                    char tmp[128];
                    if (chars > (int)sizeof tmp - 1) chars = (int)sizeof tmp - 1;
                    for (int j = 0; j < chars; j++)
                        tmp[j] = br_body[line_start + j];
                    tmp[chars] = '\0';
                    fb_draw_text(cx + 8, disp_y, tmp, ZB_BLACK, FB_TRANSPARENT);
                }
                row++;
                if (br_body[i] == '\r' && i + 1 < br_body_len && br_body[i + 1] == '\n')
                    i++;
                line_start = i + 1;
            }
        }
        /* Scroll indicator. */
        if (total_lines > visible) {
            char scrl[24];
            ksnprintf(scrl, sizeof scrl, "%d/%d", br_scroll + 1, total_lines);
            fb_draw_text(cx + cw - 60, cy + 4, scrl, 0x70808A, FB_TRANSPARENT);
        }
    }
}

/* Browser key handler -- called when the browser window is focused. */
static void browser_key(struct gwin *w, int key) {
    if (br_url_focus) {
        if (key == '\n' || key == '\r') {
            br_start_fetch();
        } else if (key == '\b') {
            if (br_url_len > 0) { br_url_len--; br_url[br_url_len] = '\0'; }
        } else if (key == '\t') {
            br_url_focus = 0;   /* toggle to content scroll */
        } else if (key >= 32 && key < 127 && br_url_len < BR_URL_MAX - 1) {
            br_url[br_url_len++] = (char)key;
            br_url[br_url_len] = '\0';
        }
    } else {
        /* Content scroll mode. */
        if (key == '\t' || key == 27) {
            br_url_focus = 1;   /* back to URL bar */
        } else if (key == KB_UP || key == 'k') {
            br_scroll--;
        } else if (key == KB_DOWN || key == 'j') {
            br_scroll++;
        } else if (key == ' ') {
            br_scroll += 10;
        } else if (key == '\n' || key == '\r') {
            br_start_fetch();
        }
    }
    (void)w;
}

/* === Pixel app: Network (ifconfig + ARP-known peers) ================== */
static void paint_network(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0xF0F2F6);
    extern struct net_iface *net_iface(void);
    struct net_iface *iface = net_iface();
    fb_hgradient(bx + 8, by + 8, bw - 16, 30, ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
    fb_draw_text(bx + 18, by + 16, "Network", ZB_TITLE_TEXT, FB_TRANSPARENT);
    int x = bx + 16, y = by + 50;
    char line[80];
    if (!iface) {
        fb_draw_text(x, y, "No network interface present.",
                     0x1E2A38, FB_TRANSPARENT);
        return;
    }
    char ip[16], gw[16], nm[16];
    extern void ip4_format(ip4_addr_t a, char *out);
    ip4_format(iface->ip, ip);
    ip4_format(iface->gateway, gw);
    ip4_format(iface->netmask, nm);
    ksnprintf(line, sizeof line, "MAC:     %02x:%02x:%02x:%02x:%02x:%02x",
              iface->mac[0], iface->mac[1], iface->mac[2],
              iface->mac[3], iface->mac[4], iface->mac[5]);
    fb_draw_text(x, y, line, 0x1E2A38, FB_TRANSPARENT); y += 22;
    ksnprintf(line, sizeof line, "IP:      %s", ip);
    fb_draw_text(x, y, line, 0x1E2A38, FB_TRANSPARENT); y += 22;
    ksnprintf(line, sizeof line, "Netmask: %s", nm);
    fb_draw_text(x, y, line, 0x1E2A38, FB_TRANSPARENT); y += 22;
    ksnprintf(line, sizeof line, "Gateway: %s", gw);
    fb_draw_text(x, y, line, 0x1E2A38, FB_TRANSPARENT); y += 22;
    fb_draw_text(x, y + 12, "Connection: ", 0x70808A, FB_TRANSPARENT);
    fb_fill_rect(x + 96, y + 14, 12, 12, 0x4FB37A);
    fb_draw_text(x + 114, y + 12, "up", 0x4FB37A, FB_TRANSPARENT);
    fb_draw_text(bx + 16, by + bh - 22,
                 "Live snapshot from e1000 / NE2000 + ARP cache",
                 0x70808A, FB_TRANSPARENT);
}

/* === Pixel app: Disks ================================================= */
static void paint_disks(struct gwin *w) {
    int bx = w->x + 4, by = w->y + TITLE_H + 4;
    int bw = w->w - 8, bh = w->h - TITLE_H - 8;
    fb_fill_rect(bx, by, bw, bh, 0xF0F2F6);
    fb_hgradient(bx + 8, by + 8, bw - 16, 30, ZB_TITLE_LEFT, ZB_TITLE_RIGHT);
    fb_draw_text(bx + 18, by + 16, "Disks", ZB_TITLE_TEXT, FB_TRANSPARENT);
    /* Column headers. */
    int x = bx + 16, y = by + 50;
    fb_draw_text(x +   0, y, "ID", 0x70808A, FB_TRANSPARENT);
    fb_draw_text(x +  56, y, "Name", 0x70808A, FB_TRANSPARENT);
    fb_draw_text(x + 168, y, "Sectors", 0x70808A, FB_TRANSPARENT);
    fb_draw_text(x + 288, y, "Size", 0x70808A, FB_TRANSPARENT);
    fb_draw_text(x + 384, y, "Type", 0x70808A, FB_TRANSPARENT);
    fb_hline(x, y + 16, bw - 32, 0xC4C8CC);
    y += 24;
    extern struct disk *disk_get(int id);
    int present = 0;
    for (int id = 0; id < DISK_MAX; id++) {
        struct disk *d = disk_get(id);
        if (!d || !d->present) continue;
        char idstr[8], sec[16], sz[16];
        ksnprintf(idstr, sizeof idstr, "%d", id);
        ksnprintf(sec, sizeof sec, "%u", (u32)d->sectors);
        if (d->sectors >= 2048) {
            u32 mib = d->sectors / 2048;
            ksnprintf(sz, sizeof sz, "%u MiB", mib);
        } else {
            ksnprintf(sz, sizeof sz, "%u KiB", (u32)(d->sectors / 2));
        }
        const char *kind = id < 4 ? "ATA"
                         : id < 8 ? "AHCI"
                         : id < 10 ? "Floppy"
                         : id < 12 ? "USB UHCI"
                         : id < 16 ? "USB EHCI"
                         : "Partition";
        u32 bg = (present & 1) ? 0xFFFFFF : 0xE6ECF2;
        fb_fill_rect(x, y, bw - 32, 22, bg);
        fb_draw_text(x +   0, y + 4, idstr, 0x1E2A38, FB_TRANSPARENT);
        fb_draw_text(x +  56, y + 4, d->name, 0x1E2A38, FB_TRANSPARENT);
        fb_draw_text(x + 168, y + 4, sec, 0x1E2A38, FB_TRANSPARENT);
        fb_draw_text(x + 288, y + 4, sz, 0x1E2A38, FB_TRANSPARENT);
        fb_draw_text(x + 384, y + 4, kind, 0x6447B0, FB_TRANSPARENT);
        y += 22;
        present++;
        if (y + 22 >= by + bh - 28) break;
    }
    char hdr[64];
    ksnprintf(hdr, sizeof hdr, "%d disk(s) present", present);
    fb_draw_text(bx + 16, by + bh - 22, hdr, 0x70808A, FB_TRANSPARENT);
}

/* === Notes save (extend existing paint_notes/notes_key) ================
 * Persist the scratchpad to A:\NOTES.TXT when the user presses Ctrl+S.
 * Re-use the same key-route; the dispatcher feeds raw bytes here, so
 * Ctrl+S arrives as 0x13 (the keyboard driver does the Ctrl-letter
 * translation). */
static void notes_save(void) {
    const char *fn = notes_filename[0] ? notes_filename : "\\NOTES.TXT";
    fs_unlink(fn);
    if (fs_create(fn) < 0) return;
    int h = fs_open(fn);
    if (h < 0) return;
    fs_write(h, notes_buf, (size_t)notes_len);
    fs_close(h);
}

static void notes_load(void) {
    const char *fn = notes_filename[0] ? notes_filename : "\\NOTES.TXT";
    int h = fs_open(fn);
    if (h < 0) { notes_len = 0; notes_buf[0] = 0; return; }
    int n = fs_read(h, notes_buf, sizeof notes_buf - 1);
    if (n < 0) n = 0;
    notes_len = n;
    notes_buf[notes_len] = 0;
    fs_close(h);
}

/* === Window open animation ========================================== */
/* No-op: open animation removed -- the 5-frame busy-wait stall
 * (~5*200k iterations) was the dominant source of "feels slow"
 * when opening apps. Bringing it back needs a non-blocking
 * implementation tied to pit_ticks so the desktop stays responsive
 * while the ghost grows. */
static void anim_open(int fx, int fy, int fw, int fh) {
    (void)fx; (void)fy; (void)fw; (void)fh;
}

/* Map a paint function to its persistent kind id. The kind survives
 * a reboot via CONFIG.TXT; the paint pointer doesn't. */
static int paint_to_kind(void (*p)(struct gwin *)) {
    if (p == paint_welcome)  return GWK_WELCOME;
    if (p == paint_about)    return GWK_ABOUT;
    if (p == paint_files)    return GWK_FILES;
    if (p == paint_term)     return GWK_TERM;
    if (p == paint_calc)     return GWK_CALC;
    if (p == paint_clock)    return GWK_CLOCK;
    if (p == paint_sysmon)   return GWK_SYSMON;
    if (p == paint_settings) return GWK_SETTINGS;
    if (p == paint_snake)    return GWK_SNAKE;
    if (p == paint_notes)    return GWK_NOTES;
    if (p == paint_aclock)   return GWK_ACLOCK;
    if (p == paint_calendar) return GWK_CALENDAR;
    if (p == paint_mines)    return GWK_MINES;
    if (p == paint_tetris)   return GWK_TETRIS;
    if (p == paint_network)  return GWK_NETWORK;
    if (p == paint_disks)    return GWK_DISKS;
    if (p == paint_browser)  return GWK_BROWSER;
    return GWK_WELCOME;
}

static int gwin_spawn(int x, int y, int w, int h, const char *title,
                      void (*paint)(struct gwin *)) {
    /* Clamp dimensions and position to the usable screen area so windows
     * never overflow on low-res displays (640x480 etc.). */
    int W = fb_w(), H = fb_h();
    if (w > W - 4)              w = W - 4;
    if (h > H - TASKBAR_H - 4) h = H - TASKBAR_H - 4;
    if (x + w > W - 2)         x = W - 2 - w;
    if (x < 2)                  x = 2;
    if (y + h > H - TASKBAR_H - 2) y = H - TASKBAR_H - 2 - h;
    if (y < 2)                  y = 2;
    for (int i = 1; i < G_MAX_WIN; i++) {
        if (g_wins[i].used) continue;
        g_wins[i].used = 1;
        g_wins[i].x = x; g_wins[i].y = y; g_wins[i].w = w; g_wins[i].h = h;
        g_wins[i].rx = x; g_wins[i].ry = y; g_wins[i].rw = w; g_wins[i].rh = h;
        g_wins[i].state = GWIN_NORMAL;
        g_wins[i].z = g_next_z++;
        g_wins[i].focused = 1;
        g_wins[i].closable = 1;
        g_wins[i].paint = paint;
        g_wins[i].kind  = paint_to_kind(paint);
        int j = 0;
        while (title[j] && j < (int)sizeof g_wins[i].title - 1) {
            g_wins[i].title[j] = title[j]; j++;
        }
        g_wins[i].title[j] = '\0';
        gwin_focus(i);
        return i;
    }
    return -1;
}

/* Forward declarations for cursor functions */
static void cursor_save_under(int px, int py);
static void cursor_restore(void);
static void cursor_draw(int px, int py);

/* === Start menu (simple vertical list) ============================== */
static const char *start_items[] = {
    "About Zenbite",
    "Welcome",
    "Calculator",
    "Calendar",
    "Analog Clock",
    "Network",
    "Disks",
    "Web Browser",
    "Activity Monitor",
    "Minesweeper",
    "Tetris",
    "Snake",
    "Lock screen",
    "Exit to shell",
    "Reboot",
    "Power off",
};
#define START_ITEM_COUNT (int)(sizeof start_items / sizeof start_items[0])

/* Start-menu item -> small icon colour. Index matches start_items. */
static const u32 start_item_colors[16] = {
    0x2C2152,   /* About */
    0x6447B0,   /* Welcome */
    0x4FB37A,   /* Calculator */
    0xE85A8C,   /* Calendar */
    0xE85A8C,   /* Analog Clock */
    0x47A6D4,   /* Network */
    0x8A93A6,   /* Disks */
    0x47A6D4,   /* Web Browser */
    0xD44747,   /* Activity Monitor */
    0x4FB37A,   /* Minesweeper */
    0x4FB37A,   /* Tetris */
    0x4FB37A,   /* Snake */
    0xFFA831,   /* Lock screen */
    0xC4C8CC,   /* Exit to shell */
    0xE85A8C,   /* Reboot */
    0x2E3B52,   /* Power off */
};

static int show_start_menu(int x, int y) {
    /* Win9x-style menu: dark vertical accent stripe on the left with
     * "Zenbite" written sideways, then a column of items each with
     * a tiny icon dot on the left edge. */
    int w = 230, ih = 22;
    int h = START_ITEM_COUNT * ih + 6;
    if (x + w > fb_w()) x = fb_w() - w;
    if (y - h < 0) y = h;
    int yt = y - h;
    int strip_w = 24;
    fb_fill_rect(x, yt, w, h, ZB_PANEL);
    fb_bevel_raised(x, yt, w, h, ZB_PANEL_LIGHT, ZB_PANEL_DARKER);
    /* Left stripe: dark slate with a tiny "Z" mark. */
    fb_fill_rect(x + 2, yt + 2, strip_w, h - 4, ZB_TITLE_LEFT);
    draw_zenbite_mark(x + 6, yt + h - 22, ZB_ACCENT);
    int sel = 0;
    int prev_menu_mx = -1, prev_menu_my = -1;
    /* Draw initial cursor at current mouse position */
    extern void mouse_get(int *col, int *row, int *btn);
    int mc2, mr2, mb2;
    mouse_get(&mc2, &mr2, &mb2);
    cursor_draw(mc2, mr2);
    for (;;) {
        for (int i = 0; i < START_ITEM_COUNT; i++) {
            int iy = yt + 3 + i * ih;
            u32 bg = (i == sel) ? ZB_TITLE_LEFT : ZB_PANEL;
            u32 fg = (i == sel) ? ZB_ACCENT     : ZB_BLACK;
            fb_fill_rect(x + strip_w + 4, iy, w - strip_w - 8, ih, bg);
            /* Item icon: little coloured square. */
            u32 ic = (i < 16) ? start_item_colors[i] : 0x6447B0;
            fb_fill_rect(x + strip_w + 8, iy + 4, 14, 14, ic);
            fb_bevel_raised(x + strip_w + 8, iy + 4, 14, 14,
                            ZB_PANEL_LIGHT, ZB_BLACK);
            fb_draw_text(x + strip_w + 28, iy + 4, start_items[i],
                         fg, FB_TRANSPARENT);
        }
        fb_present();
        /* Poll both keyboard and mouse */
        int k = kb_getc();
        mouse_get(&mc2, &mr2, &mb2);
        /* Handle mouse movement - update selection based on hover */
        if (mc2 != prev_menu_mx || mr2 != prev_menu_my) {
            cursor_restore();
            /* Check if mouse is over a menu item */
            if (mc2 >= x + strip_w + 4 && mc2 < x + w - 4) {
                for (int i = 0; i < START_ITEM_COUNT; i++) {
                    int iy = yt + 3 + i * ih;
                    if (mr2 >= iy && mr2 < iy + ih) {
                        sel = i;
                        break;
                    }
                }
            }
            cursor_draw(mc2, mr2);
            prev_menu_mx = mc2;
            prev_menu_my = mr2;
        }
        /* Handle mouse clicks */
        if (mb2 & 1) {
            /* Left click - check if on a menu item */
            if (mc2 >= x + strip_w + 4 && mc2 < x + w - 4) {
                for (int i = 0; i < START_ITEM_COUNT; i++) {
                    int iy = yt + 3 + i * ih;
                    if (mr2 >= iy && mr2 < iy + ih) {
                        cursor_restore();
                        while (mb2 & 1) mouse_get(NULL, NULL, &mb2);
                        return i;
                    }
                }
            }
            /* Click outside menu - close it */
            if (mc2 < x || mc2 >= x + w || mr2 < yt || mr2 >= y) {
                cursor_restore();
                while (mb2 & 1) mouse_get(NULL, NULL, &mb2);
                return -1;
            }
        }
        /* Handle right-click to cancel */
        if (mb2 & 2) {
            cursor_restore();
            while (mb2 & 2) mouse_get(NULL, NULL, &mb2);
            return -1;
        }
        if (k == 27) { cursor_restore(); return -1; }
        if (k == '\n' || k == '\r') { cursor_restore(); return sel; }
        if (k == KB_UP || k == 'k') sel = (sel + START_ITEM_COUNT - 1) % START_ITEM_COUNT;
        if (k == KB_DOWN || k == 'j') sel = (sel + 1) % START_ITEM_COUNT;
    }
}

/* === Cursor save / restore =========================================== */
#define CURSOR_W 12
#define CURSOR_H 17
static u32 cursor_save[CURSOR_W * CURSOR_H];
static int cursor_save_x = -1, cursor_save_y = -1;

static void cursor_save_under(int px, int py) {
    if (px < 0 || py < 0) return;
    u32 *fb = (void *)0;
    extern u32 *fb_addr(void); fb = fb_addr();
    if (!fb) return;
    int W = fb_w(), H = fb_h();
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++) {
            int x = px + c, y = py + r;
            if (x < 0 || x >= W || y < 0 || y >= H) continue;
            cursor_save[r * CURSOR_W + c] = fb[y * W + x];
        }
    cursor_save_x = px; cursor_save_y = py;
}

static void cursor_restore(void) {
    if (cursor_save_x < 0) return;
    u32 *fb = (void *)0;
    extern u32 *fb_addr(void); fb = fb_addr();
    if (!fb) return;
    int W = fb_w(), H = fb_h();
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++) {
            int x = cursor_save_x + c, y = cursor_save_y + r;
            if (x < 0 || x >= W || y < 0 || y >= H) continue;
            fb[y * W + x] = cursor_save[r * CURSOR_W + c];
        }
    cursor_save_x = -1;
}

static void cursor_draw(int px, int py) {
    cursor_save_under(px, py);
    fb_blit_sprite(px, py, CURSOR_W, CURSOR_H, cursor_arrow, cursor_palette);
}

/* === Notifications (toasts) ==========================================
 * Compact bottom-right popups that auto-fade after a few seconds.
 * notify(title, msg) is exposed via a forward decl so any app code
 * (Editor save, Files delete, install done, ...) can drop a toast
 * without owning a window. A short PC-speaker chirp accompanies the
 * popup so the user notices it even if their eyes are elsewhere. */
#define NOTIF_MAX 4
struct notif { char title[20]; char body[60]; u32 expires; int kind; };
static struct notif g_notifs[NOTIF_MAX];

static void gdesk_notify(const char *title, const char *body, int kind) {
    /* Find an expired slot, or evict the oldest. */
    u32 now = pit_ticks();
    int slot = -1;
    u32 oldest = (u32)-1;
    for (int i = 0; i < NOTIF_MAX; i++) {
        if (g_notifs[i].expires <= now) { slot = i; break; }
        if (g_notifs[i].expires < oldest) { oldest = g_notifs[i].expires; slot = i; }
    }
    if (slot < 0) slot = 0;
    int j = 0;
    while (title && title[j] && j < (int)sizeof g_notifs[slot].title - 1) {
        g_notifs[slot].title[j] = title[j]; j++;
    }
    g_notifs[slot].title[j] = '\0';
    j = 0;
    while (body && body[j] && j < (int)sizeof g_notifs[slot].body - 1) {
        g_notifs[slot].body[j] = body[j]; j++;
    }
    g_notifs[slot].body[j] = '\0';
    g_notifs[slot].expires = now + 4000;     /* 4s at PIT 1000 Hz */
    g_notifs[slot].kind = kind;
    /* Audio cue. kind: 0 info, 1 success, 2 warn, 3 error. Different
     * pitch for each so the speaker tells you what happened without
     * looking. Silent if the user toggled sound off in Settings. */
    extern int g_sound_enabled;
    if (g_sound_enabled) {
        u32 freq;
        switch (kind) {
        case 1: freq = 880; break;     /* success: A5 */
        case 2: freq = 440; break;     /* warn: A4 */
        case 3: freq = 220; break;     /* error: A3 (low / serious) */
        default: freq = 660; break;    /* info: E5 */
        }
        speaker_beep(freq, 60);
    }
}

static void draw_notifications(void) {
    int W = fb_w(), H = fb_h();
    u32 now = pit_ticks();
    int slot_y = H - TASKBAR_H - 12;
    for (int i = 0; i < NOTIF_MAX; i++) {
        if (g_notifs[i].expires <= now) continue;
        if (!g_notifs[i].title[0] && !g_notifs[i].body[0]) continue;
        int nw = 280, nh = 56;
        int nx = W - nw - 12;
        int ny = slot_y - nh;
        if (ny < 8) break;
        /* Body + accent stripe on the left whose colour reflects kind. */
        u32 stripe;
        switch (g_notifs[i].kind) {
        case 1: stripe = 0x4FB37A; break;
        case 2: stripe = 0xFFA831; break;
        case 3: stripe = 0xE85A8C; break;
        default: stripe = 0x47A6D4; break;
        }
        fb_fill_rect(nx, ny, nw, nh, ZB_PANEL);
        fb_bevel_raised(nx, ny, nw, nh, ZB_PANEL_LIGHT, ZB_BLACK);
        fb_fill_rect(nx + 2, ny + 2, 6, nh - 4, stripe);
        fb_draw_text(nx + 14, ny +  8, g_notifs[i].title,
                     ZB_TITLE_LEFT, FB_TRANSPARENT);
        fb_draw_text(nx + 14, ny + 28, g_notifs[i].body,
                     ZB_BLACK, FB_TRANSPARENT);
        slot_y = ny - 6;
    }
}

/* === Main loop ======================================================= */
/* Mix two colours: lerp at t/255. Used by the wallpaper gradient. */
static u32 mix_color(u32 a, u32 b, int t) {
    if (t < 0) t = 0; if (t > 255) t = 255;
    u32 ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    u32 br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    u32 r  = (ar * (255 - t) + br * t) / 255;
    u32 g  = (ag * (255 - t) + bg * t) / 255;
    u32 c  = (ab * (255 - t) + bb * t) / 255;
    return (r << 16) | (g << 8) | c;
}
static u32 scale_brightness(u32 c) {
    if (g_brightness == 100) return c;
    u32 r = ((c >> 16) & 0xFF) * (u32)g_brightness / 100;
    u32 g = ((c >>  8) & 0xFF) * (u32)g_brightness / 100;
    u32 b = ( c        & 0xFF) * (u32)g_brightness / 100;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}

/* Wallpaper + icons dirty flag. Most frames just refresh the active
 * windows + taskbar/notifications -- the wallpaper underneath is
 * untouched. Painting the gradient + icons every frame costs ~480k
 * MMIO writes on 800x600 and produces visible tearing/flicker on
 * BGA hardware where MMIO is slow. We only repaint the wallpaper
 * when something actually disturbed it (window moved/closed, drag
 * release, restore). */
static int g_wallpaper_dirty = 1;
static void mark_wallpaper_dirty(void) { g_wallpaper_dirty = 1; }

static void paint_wallpaper(void) {
    int W = fb_w(), H = fb_h();
    int work_h = H - TASKBAR_H;
    u32 top = scale_brightness(0x1B3A5C);    /* deep slate blue */
    u32 bot = scale_brightness(0x06121F);    /* near-black slate */
    if (g_wallpaper_style == 0) {
        fb_fill_rect(0, 0, W, work_h, scale_brightness(ZB_BG));
    } else {
        for (int y = 0; y < work_h; y++) {
            int t = (y * 255) / (work_h > 0 ? work_h : 1);
            fb_hline(0, y, W, mix_color(top, bot, t));
        }
        if (g_wallpaper_style == 2) {
            for (int y = 0; y < work_h; y++) {
                int x = (y * W) / (work_h > 0 ? work_h : 1);
                u32 c = scale_brightness(0x355576);
                for (int dx = -3; dx <= 3; dx++) {
                    if ((unsigned)(x + dx) < (unsigned)W) fb_pixel(x + dx, y, c);
                }
            }
        }
    }
    for (int i = 0; i < G_ICON_COUNT; i++)
        draw_icon(&g_icons[i], 0);
}

static int repaint_all(void) {
    if (g_wallpaper_dirty) {
        paint_wallpaper();
        g_wallpaper_dirty = 0;
    }
    /* Windows z-sorted: low z first. Minimised windows skipped --
     * they live only in the taskbar until restored. */
    int order[G_MAX_WIN]; int n = 0;
    for (int i = 0; i < G_MAX_WIN; i++)
        if (g_wins[i].used && g_wins[i].state != GWIN_MIN) order[n++] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (g_wins[order[i]].z > g_wins[order[j]].z) {
                int t = order[i]; order[i] = order[j]; order[j] = t;
            }
    for (int i = 0; i < n; i++) {
        struct gwin *w = &g_wins[order[i]];
        draw_window_chrome(w);
        if (w->paint) w->paint(w);
    }
    draw_taskbar();
    draw_notifications();
    return 0;
}

/* Repaint just the focused window's body + chrome -- not the rest of
 * the desktop. Use this when a keystroke or mouse event only changed
 * the focused window's content (typing in Editor, clicking in Calc,
 * sliding a Settings slider). Repainting every window + taskbar +
 * wallpaper for each keystroke produces visible flicker when typing
 * fast; this restricted path writes ~10x less to the framebuffer. */
static void repaint_focused(int focused) {
    if (focused < 0 || focused >= G_MAX_WIN) return;
    if (!g_wins[focused].used) return;
    if (g_wins[focused].state == GWIN_MIN) return;
    struct gwin *w = &g_wins[focused];
    draw_window_chrome(w);
    if (w->paint) w->paint(w);
    /* Notifications need to stay on top of the window body. */
    draw_notifications();
}

/* Any active notification still drawn? Used to keep the frame timer
 * alive while a toast is fading. */
static int has_active_notif(void) {
    u32 now = pit_ticks();
    for (int i = 0; i < NOTIF_MAX; i++)
        if (g_notifs[i].expires > now &&
            (g_notifs[i].title[0] || g_notifs[i].body[0]))
            return 1;
    return 0;
}

/* Hit test: is (px, py) within the title bar of widget i? */
static int title_hit(struct gwin *w, int px, int py) {
    int tx = w->x + 3, ty = w->y + 3;
    int tw = w->w - 6;
    return (py >= ty && py < ty + TITLE_H - 3 &&
            px >= tx && px < tx + tw);
}
/* close-button hit (the Win95 raised [X] at top-right). */
static int close_hit(struct gwin *w, int px, int py) {
    if (!w->closable) return 0;
    int bx = w->x + w->w - 20, by = w->y + 4;
    return (px >= bx && px < bx + 16 && py >= by && py < by + 14);
}
static int max_hit(struct gwin *w, int px, int py) {
    if (!w->closable) return 0;
    int bx = w->x + w->w - 38, by = w->y + 4;
    return (px >= bx && px < bx + 16 && py >= by && py < by + 14);
}
static int min_hit(struct gwin *w, int px, int py) {
    if (!w->closable) return 0;
    int bx = w->x + w->w - 56, by = w->y + 4;
    return (px >= bx && px < bx + 16 && py >= by && py < by + 14);
}

/* Taskbar chip hit-test: returns the window index whose chip contains
 * (px, py), or -1. Geometry must match draw_taskbar(). */
static int taskbar_chip_hit(int px, int py) {
    int W = fb_w(), H = fb_h();
    int by = H - TASKBAR_H;
    if (py < by + 4 || py >= by + TASKBAR_H - 4) return -1;
    int chip_x = start_button_w + 12;
    int tray_w = 156;
    int chip_w = 156;
    for (int i = 0; i < G_MAX_WIN; i++) {
        if (!g_wins[i].used) continue;
        if (chip_x + chip_w > W - tray_w - 12) break;
        if (px >= chip_x && px < chip_x + chip_w) return i;
        chip_x += chip_w + 4;
    }
    return -1;
}

/* Save current geometry as the restore rect, then swap to a new rect.
 * Idempotent across max/un-max cycles. */
static void gwin_save_restore(struct gwin *w) {
    w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
}
static void gwin_apply_restore(struct gwin *w) {
    if (w->rw <= 0 || w->rh <= 0) return;
    w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
}
static void gwin_maximise(struct gwin *w) {
    int W = fb_w(), H = fb_h();
    if (w->state == GWIN_NORMAL) gwin_save_restore(w);
    w->x = 0; w->y = 0; w->w = W; w->h = H - TASKBAR_H;
    w->state = GWIN_MAX;
    mark_wallpaper_dirty();
}
static void gwin_minimise(struct gwin *w) {
    if (w->state == GWIN_NORMAL) gwin_save_restore(w);
    w->state = GWIN_MIN;
    w->focused = 0;
    mark_wallpaper_dirty();
}
static void gwin_restore(struct gwin *w) {
    gwin_apply_restore(w);
    w->state = GWIN_NORMAL;
    mark_wallpaper_dirty();
}
static void gwin_toggle_max(struct gwin *w) {
    if (w->state == GWIN_MAX) gwin_restore(w);
    else                       gwin_maximise(w);
}

/* Snap-to-edge: drop the window into a half/full-screen slot. The
 * pre-snap geometry is saved in the restore rect just like maximise,
 * so dragging away from the edge restores it (Win7-style aero snap).
 * dir: 0 left half, 1 right half, 2 full (== max), 3 top half. */
static void gwin_snap(struct gwin *w, int dir) {
    int W = fb_w(), H = fb_h(), work_h = H - TASKBAR_H;
    if (w->state == GWIN_NORMAL) gwin_save_restore(w);
    switch (dir) {
    case 0: w->x = 0;        w->y = 0; w->w = W / 2;     w->h = work_h; break;
    case 1: w->x = W - W/2;  w->y = 0; w->w = W / 2;     w->h = work_h; break;
    case 2: gwin_maximise(w); return;
    case 3: w->x = 0;        w->y = 0; w->w = W;         w->h = work_h / 2; break;
    }
    w->state = GWIN_MAX;   /* treat snapped as max so restore works */
    mark_wallpaper_dirty();
}

/* Spawn a window of the given persistent kind. Used by layout restore.
 * Returns the new window index or -1. */
static int gwin_spawn_kind(int kind, int x, int y, int w, int h) {
    int idx = -1;
    switch (kind) {
    case GWK_WELCOME:
        idx = gwin_spawn(x, y, w, h, "Welcome", paint_welcome); break;
    case GWK_ABOUT:
        idx = gwin_spawn(x, y, w, h, "About", paint_about); break;
    case GWK_FILES:
        files_refresh();
        idx = gwin_spawn(x, y, w, h, "Files", paint_files); break;
    case GWK_TERM:
        term_init();
        idx = gwin_spawn(x, y, w, h, "Terminal", paint_term); break;
    case GWK_CALC:
        idx = gwin_spawn(x, y, w, h, "Calculator", paint_calc); break;
    case GWK_CLOCK:
        idx = gwin_spawn(x, y, w, h, "Clock", paint_clock); break;
    case GWK_SYSMON:
        idx = gwin_spawn(x, y, w, h, "System Monitor", paint_sysmon); break;
    case GWK_SETTINGS:
        idx = gwin_spawn(x, y, w, h, "Settings", paint_settings); break;
    case GWK_SNAKE:
        snk_len = 0;
        idx = gwin_spawn(x, y, w, h, "Snake", paint_snake); break;
    case GWK_NOTES:
        notes_load();
        idx = gwin_spawn(x, y, w, h, "Editor", paint_notes); break;
    case GWK_ACLOCK:
        idx = gwin_spawn(x, y, w, h, "Analog Clock", paint_aclock); break;
    case GWK_CALENDAR:
        cal_init();
        idx = gwin_spawn(x, y, w, h, "Calendar", paint_calendar); break;
    case GWK_MINES:
        ms_reset();
        idx = gwin_spawn(x, y, w, h, "Minesweeper", paint_mines); break;
    case GWK_TETRIS:
        tt_reset();
        idx = gwin_spawn(x, y, w, h, "Tetris", paint_tetris); break;
    case GWK_NETWORK:
        idx = gwin_spawn(x, y, w, h, "Network", paint_network); break;
    case GWK_DISKS:
        idx = gwin_spawn(x, y, w, h, "Disks", paint_disks); break;
    case GWK_BROWSER:
        idx = gwin_spawn(x, y, w, h, "Web Browser", paint_browser); break;
    }
    return idx;
}

/* Tiny tokenizer for the layout string: writes the integer at *p and
 * advances *p past the next colon or comma. */
static int parse_layout_int(const char **p) {
    int v = 0;
    while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; }
    if (**p == ':' || **p == ',') (*p)++;
    return v;
}

/* Walk the persisted layout string and spawn each window. */
static int restore_layout(void) {
    if (!g_last_layout[0]) return 0;
    const char *p = g_last_layout;
    int spawned = 0;
    while (*p) {
        int k  = parse_layout_int(&p);
        int x  = parse_layout_int(&p);
        int y  = parse_layout_int(&p);
        int w  = parse_layout_int(&p);
        int h  = parse_layout_int(&p);
        int s  = parse_layout_int(&p);
        if (w <= 0 || h <= 0) break;
        int idx = gwin_spawn_kind(k, x, y, w, h);
        if (idx >= 0) {
            if (s == GWIN_MAX) gwin_maximise(&g_wins[idx]);
            else if (s == GWIN_MIN) gwin_minimise(&g_wins[idx]);
            spawned++;
        }
        if (spawned >= G_MAX_WIN) break;
    }
    return spawned;
}

/* Serialise the current desktop into g_last_layout. Skip the Welcome
 * slot (index 0) -- it's always re-spawned on a fresh boot when no
 * persisted layout exists. */
static void save_layout(void) {
    char out[sizeof g_last_layout];
    int pos = 0;
    int first = 1;
    for (int i = 0; i < G_MAX_WIN; i++) {
        if (!g_wins[i].used) continue;
        struct gwin *w = &g_wins[i];
        /* Use the restore rect for max/min so we remember the
         * "real" size, not the temporary maximised geometry. */
        int sx = w->x, sy = w->y, sw = w->w, sh = w->h;
        if (w->state != GWIN_NORMAL && w->rw > 0 && w->rh > 0) {
            sx = w->rx; sy = w->ry; sw = w->rw; sh = w->rh;
        }
        char buf[64];
        int n = ksnprintf(buf, sizeof buf, "%s%d:%d:%d:%d:%d:%d",
                          first ? "" : ",",
                          w->kind, sx, sy, sw, sh, w->state);
        if (pos + n >= (int)sizeof out - 1) break;
        for (int j = 0; j < n; j++) out[pos++] = buf[j];
        first = 0;
    }
    out[pos] = '\0';
    gdesk_set_last_layout(out);
    extern void config_save(void);
    config_save();
}

int g_desktop_main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* Try a series of common VBE resolutions, smallest first. Many
     * real displays + UTM configurations can't sync on 1280x720;
     * 800x600 is the universal "Win95 era" baseline and 640x480 is
     * the absolute fallback. The user can bump up via Settings if
     * they have the headroom. */
    if (!bga_present_active()) {
        /* Try the persisted resolution first, then fall back through
         * the standard chain so machines whose monitor can't sync the
         * persisted size still come up. */
        int g = vga_set_graphics(g_res_w, g_res_h);
        if (g < 0) g = vga_set_graphics(800, 600);
        if (g < 0) g = vga_set_graphics(640, 480);
        if (g < 0) g = vga_set_graphics(1024, 768);
        if (g < 0) g = vga_set_graphics(1280, 720);
        if (g < 0 || fb_w() <= 0 || fb_h() <= 0) {
            kputs("gdesk: no usable graphics mode; using text desktop\n");
            kputs("       (in UTM: set Display device to 'VGA' / 'std-vga')\n");
            extern int desktop_main(int, char **);
            return desktop_main(argc, argv);
        }
    }
    int W = fb_w(), H = fb_h();
    /* Mouse bounds go to the very edge so the user can actually reach
     * the taskbar (start button + tray live below H - TASKBAR_H).
     * The earlier clamp at H - TASKBAR_H - 1 made the start menu
     * un-clickable. */
    mouse_set_bounds(W - 1, H - 1);
    /* Compute icon positions for this resolution. */
    layout_icons();
    /* Reset state. */
    for (int i = 0; i < G_MAX_WIN; i++) g_wins[i].used = 0;
    g_next_z = 1;
    cursor_save_x = -1;

    /* Restore the last session's window layout if one was persisted.
     * Falls back to spawning the Welcome window on first boot. */
    int restored = restore_layout();
    if (restored == 0)
        gwin_spawn(180, 110, 520, 260, "Welcome", paint_welcome);
    repaint_all();

    int prev_mx = -1, prev_my = -1;
    int dragging = -1;
    int drag_off_x = 0, drag_off_y = 0;
    int lwas = 0, rwas = 0;

    for (;;) {
        int mc, mr, mb;
        mouse_get(&mc, &mr, &mb);
        /* In graphics mode mouse_get returns CELL coords because the
         * shared mouse code clamps to mouse_set_bounds. We set bounds
         * to pixel-sized rectangles above so mc/mr are pixel coords. */
        int mx = mc, my = mr;
        if (mx != prev_mx || my != prev_my) {
            cursor_restore();
            cursor_draw(mx, my);
            prev_mx = mx; prev_my = my;
        }
        int lpressed = (mb & 1) != 0;
        int rpressed = (mb & 2) != 0;
        /* Right-click context menu: opens at the cursor. Two cases --
         * over a window, or on the desktop background. */
        if (rpressed && !rwas) {
            int hit = gwin_at(mx, my);
            const char *items[6];
            int n;
            if (hit >= 0) {
                items[0] = "Bring to front";
                items[1] = "Close window";
                n = 2;
            } else {
                items[0] = "Refresh";
                items[1] = "Snake";
                items[2] = "Lock screen";
                items[3] = "About Zenbite";
                items[4] = "Exit to shell";
                n = 5;
            }
            cursor_restore();
            int w = 180, ih = 22;
            int h = n * ih + 4;
            int mx2 = mx, my2 = my;
            if (mx2 + w > W) mx2 = W - w;
            if (my2 + h > H - TASKBAR_H) my2 = H - TASKBAR_H - h;
            fb_fill_rect(mx2, my2, w, h, ZB_PANEL);
            fb_bevel_raised(mx2, my2, w, h, ZB_PANEL_LIGHT, ZB_BLACK);
            int sel = 0;
            int prev_ctx_mx = mx, prev_ctx_my = my;
            cursor_draw(mx, my);
            for (;;) {
                for (int i = 0; i < n; i++) {
                    int iy = my2 + 2 + i * ih;
                    u32 bg = (i == sel) ? ZB_TITLE_LEFT : ZB_PANEL;
                    u32 fg = (i == sel) ? ZB_ACCENT     : ZB_BLACK;
                    fb_fill_rect(mx2 + 2, iy, w - 4, ih, bg);
                    fb_draw_text(mx2 + 12, iy + 4, items[i], fg, FB_TRANSPARENT);
                }
                fb_present();
                /* Poll both keyboard and mouse */
                int kk = kb_getc();
                int mc3, mr3, mb3;
                mouse_get(&mc3, &mr3, &mb3);
                /* Handle mouse movement */
                if (mc3 != prev_ctx_mx || mr3 != prev_ctx_my) {
                    cursor_restore();
                    if (mc3 >= mx2 + 2 && mc3 < mx2 + w - 2) {
                        for (int i = 0; i < n; i++) {
                            int iy = my2 + 2 + i * ih;
                            if (mr3 >= iy && mr3 < iy + ih) {
                                sel = i;
                                break;
                            }
                        }
                    }
                    cursor_draw(mc3, mr3);
                    prev_ctx_mx = mc3;
                    prev_ctx_my = mr3;
                }
                /* Handle mouse clicks */
                if (mb3 & 1) {
                    if (mc3 >= mx2 + 2 && mc3 < mx2 + w - 2) {
                        for (int i = 0; i < n; i++) {
                            int iy = my2 + 2 + i * ih;
                            if (mr3 >= iy && mr3 < iy + ih) {
                                cursor_restore();
                                while (mb3 & 1) mouse_get(NULL, NULL, &mb3);
                                break;
                            }
                        }
                        break;
                    }
                    /* Click outside - close */
                    if (mc3 < mx2 || mc3 >= mx2 + w || mr3 < my2 || mr3 >= my2 + h) {
                        cursor_restore();
                        while (mb3 & 1) mouse_get(NULL, NULL, &mb3);
                        sel = -1;
                        break;
                    }
                }
                /* Right-click cancels */
                if (mb3 & 2) {
                    cursor_restore();
                    while (mb3 & 2) mouse_get(NULL, NULL, &mb3);
                    sel = -1;
                    break;
                }
                if (kk == 27) { sel = -1; break; }
                if (kk == 0x48 || kk == 'k') sel = (sel + n - 1) % n;
                if (kk == 0x50 || kk == 'j') sel = (sel + 1) % n;
                if (kk == '\n' || kk == '\r') break;
            }
            if (sel >= 0) {
                if (hit >= 0) {
                    if (sel == 0) gwin_focus(hit);
                    else if (sel == 1 && hit != 0) {
                        g_wins[hit].used = 0;
                        mark_wallpaper_dirty();
                    }
                } else {
                    if (sel == 0) { /* refresh */ }
                    else if (sel == 1) {
                        snk_len = 0;       /* force snk_reset() */
                        anim_open(140, 90, 600, 380);
                        gwin_spawn(140, 90, 600, 380, "Snake", paint_snake);
                    }
                    else if (sel == 2) { lock_run(); }
                    else if (sel == 3)
                        gwin_spawn(220, 140, 460, 280, "About", paint_about);
                    else if (sel == 4) {
                        save_layout();
                        vga_set_text_mode(25);
                        return 0;
                    }
                }
            }
            mark_wallpaper_dirty();    /* right-click menu painted over WP */
            repaint_all();
            cursor_draw(mx, my);
            prev_mx = mx; prev_my = my;
            while (mb & 2) mouse_get(NULL, NULL, &mb);
            rwas = 0;
            continue;
        }
        rwas = rpressed;
        if (lpressed && !lwas) {
            /* Start button click? */
            int by = H - TASKBAR_H;
            if (my >= by && mx >= 2 && mx < 2 + start_button_w) {
                cursor_restore();
                int it = show_start_menu(2, by - 2);
                switch (it) {
                case 0:  gwin_spawn(220,140,460,280,"About",paint_about); break;
                case 1:  gwin_spawn(180,110,520,240,"Welcome",paint_welcome); break;
                case 2:
                    calc_input_len = 0; calc_input[0] = 0;
                    calc_op_active = 0; calc_show_result = 0;
                    gwin_spawn(220,140,360,280,"Calculator",paint_calc); break;
                case 3:  cal_init();    gwin_spawn(160,90,720,480,"Calendar",paint_calendar); break;
                case 4:  gwin_spawn(180,90,520,440,"Analog Clock",paint_aclock); break;
                case 5:  gwin_spawn(180,120,560,320,"Network",paint_network); break;
                case 6:  gwin_spawn(120,90,800,440,"Disks",paint_disks); break;
                case 7:  gwin_spawn(100,80,700,480,"Web Browser",paint_browser); break;
                case 8:  gwin_spawn(100,80,680,440,"Activity Monitor",paint_activity); break;
                case 9:  ms_reset();    gwin_spawn(120,90,720,460,"Minesweeper",paint_mines); break;
                case 10: tt_reset();    gwin_spawn(160,40,540,560,"Tetris",paint_tetris); break;
                case 11: snk_len = 0;   gwin_spawn(140,90,600,380,"Snake",paint_snake); break;
                case 12: lock_run(); mark_wallpaper_dirty(); repaint_all(); break;
                case 13: save_layout(); cursor_restore(); vga_set_text_mode(25); return 0;
                case 14: { extern void reboot(void); reboot(); } break;
                case 15: { extern void shutdown(void); shutdown(); } break;
                }
                mark_wallpaper_dirty();   /* start menu painted over WP */
                repaint_all();
                prev_mx = prev_my = -1;
                while (mb & 1) mouse_get(NULL, NULL, &mb);
                lwas = 0;
                continue;
            }
            /* Taskbar chip click: restore minimised, focus normal,
             * or minimise the currently-focused one (Win7 toggle). */
            if (my >= H - TASKBAR_H) {
                int chip = taskbar_chip_hit(mx, my);
                if (chip >= 0) {
                    struct gwin *w = &g_wins[chip];
                    if (w->state == GWIN_MIN) {
                        gwin_restore(w);
                        gwin_focus(chip);
                    } else if (w->focused) {
                        gwin_minimise(w);
                    } else {
                        gwin_focus(chip);
                    }
                    repaint_all();
                    prev_mx = prev_my = -1;
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    lwas = 0;
                    continue;
                }
            }
            /* Window hit? */
            int hit = gwin_at(mx, my);
            if (hit >= 0) {
                struct gwin *w = &g_wins[hit];
                if (close_hit(w, mx, my)) {
                    w->used = 0;
                    mark_wallpaper_dirty();
                    repaint_all();
                    prev_mx = prev_my = -1;
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    lwas = 0;
                    continue;
                }
                if (max_hit(w, mx, my)) {
                    gwin_toggle_max(w);
                    repaint_all();
                    prev_mx = prev_my = -1;
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    lwas = 0;
                    continue;
                }
                if (min_hit(w, mx, my)) {
                    gwin_minimise(w);
                    repaint_all();
                    prev_mx = prev_my = -1;
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    lwas = 0;
                    continue;
                }
                gwin_focus(hit);
                /* Calculator: route clicks to the button grid. */
                if (w->paint == paint_calc) {
                    calc_click(w, mx, my);
                }
                /* Settings: route clicks to tabs / sliders / buttons. */
                if (w->paint == paint_settings) {
                    settings_click(w, mx, my);
                }
                /* Welcome shortcut tiles: hit-test the four tile rects
                 * laid out in paint_welcome. Clicking a tile spawns
                 * the matching app, same as the desktop icons. */
                if (w->paint == paint_welcome) {
                    int bx2 = w->x + 4, by2 = w->y + TITLE_H + 4;
                    int bw2 = w->w - 8, bh2 = w->h - TITLE_H - 8;
                    int tw  = (bw2 - 12 - 16 * 3) / 4;
                    int th  = bh2 - 96; if (th > 132) th = 132;
                    int ty  = by2 + 80;
                    for (int i = 0; i < 4; i++) {
                        int tx = bx2 + 6 + i * (tw + 16);
                        if (mx >= tx && mx < tx + tw &&
                            my >= ty && my < ty + th) {
                            switch (i) {
                            case 0: files_refresh();
                                    gwin_spawn(140,100,560,380,"Files",paint_files); break;
                            case 1: notes_load();
                                    gwin_spawn(160,110,560,360,"Editor",paint_notes); break;
                            case 2: term_init();
                                    gwin_spawn(160,120,640,400,"Terminal",paint_term); break;
                            case 3: settings_sel = 0;
                                    gwin_spawn(120, 70, 720, 500, "Settings", paint_settings); break;
                            }
                            break;
                        }
                    }
                }
                /* Alt+anywhere drag (Win-style "Alt+drag to move from
                 * anywhere in the window"), or click on the title bar.
                 * A maximised window pops back to its restore rect
                 * before drag begins so the user can move it again. */
                if (title_hit(w, mx, my) || kb_alt_held()) {
                    if (w->state == GWIN_MAX) gwin_restore(w);
                    dragging = hit;
                    drag_off_x = mx - w->x;
                    drag_off_y = my - w->y;
                }
                repaint_all();
                prev_mx = prev_my = -1;
            } else {
                int ih = icon_hit(mx, my);
                if (ih >= 0) {
                    /* Map icon -> spawned window. Files/Editor/Terminal
                     * fall back to a "coming soon" Welcome until the
                     * cell-app port lands; Calc and About have real
                     * pixel paint callbacks. */
                    switch (ih) {
                    case 0:
                        files_refresh();
                        anim_open(140, 100, 560, 380);
                        gwin_spawn(140, 100, 560, 380, "Files", paint_files);
                        break;
                    case 1:
                        notes_load();
                        anim_open(160, 110, 560, 360);
                        gwin_spawn(160, 110, 560, 360, "Editor", paint_notes);
                        break;
                    case 2:
                        term_init();
                        anim_open(160, 120, 640, 400);
                        gwin_spawn(160, 120, 640, 400, "Terminal", paint_term);
                        break;
                    case 3:
                        calc_input_len = 0; calc_input[0] = 0;
                        calc_op_active = 0; calc_show_result = 0;
                        anim_open(220, 140, 360, 280);
                        gwin_spawn(220, 140, 360, 280, "Calculator", paint_calc);
                        break;
                    case 4:
                        anim_open(200, 130, 480, 280);
                        gwin_spawn(200, 130, 480, 280, "Clock", paint_clock);
                        break;
                    case 5:
                        anim_open(140, 100, 600, 380);
                        gwin_spawn(140, 100, 600, 380, "System Monitor", paint_sysmon);
                        break;
                    case 6:
                        settings_sel = 0;
                        anim_open(180, 100, 600, 400);
                        gwin_spawn(120, 70, 720, 500, "Settings", paint_settings);
                        break;
                    case 7:
                        anim_open(220, 140, 460, 280);
                        gwin_spawn(220, 140, 460, 280, "About", paint_about);
                        break;
                    }
                    repaint_all();
                    prev_mx = prev_my = -1;
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    lwas = 0;
                    continue;
                }
            }
        }
        if (dragging >= 0 && lpressed) {
            struct gwin *w = &g_wins[dragging];
            int nx = mx - drag_off_x;
            int ny = my - drag_off_y;
            if (nx != w->x || ny != w->y) {
                w->x = nx; w->y = ny;
                mark_wallpaper_dirty();    /* old window pos exposed */
                repaint_all();
                prev_mx = prev_my = -1;
            }
        }
        if (!lpressed && lwas && dragging >= 0) {
            /* Drag release: snap to the nearest edge if the cursor is
             * within 4 px of one. Left edge = left half, right = right
             * half, top = maximise. Snapped windows remember their
             * original rect via gwin_save_restore so a subsequent
             * un-snap (drag away) puts them back. */
            struct gwin *w = &g_wins[dragging];
            if (mx <= 2)               gwin_snap(w, 0);
            else if (mx >= W - 3)      gwin_snap(w, 1);
            else if (my <= 2)          gwin_snap(w, 2);
            repaint_all();
            prev_mx = prev_my = -1;
        }
        if (!lpressed && lwas) dragging = -1;
        lwas = lpressed; rwas = rpressed;
        /* Keyboard: dispatch to the focused window's app if it owns
         * one (Editor / Calculator). ESC closes the focused app
         * window first; only when no app window is focused does it
         * quit the desktop. */
        int k = kb_trygetc();
        if (k >= 0) {
            /* Windows / Super key: open the Zenbite start menu just
             * above the start button, same code path as clicking it. */
            if (k == (int)(u8)KB_WIN) {
                cursor_restore();
                int by = H - TASKBAR_H;
                int it = show_start_menu(2, by - 2);
                switch (it) {
                case 0:  gwin_spawn(220,140,460,280,"About",paint_about); break;
                case 1:  gwin_spawn(180,110,520,240,"Welcome",paint_welcome); break;
                case 2:
                    calc_input_len = 0; calc_input[0] = 0;
                    calc_op_active = 0; calc_show_result = 0;
                    gwin_spawn(220,140,360,280,"Calculator",paint_calc); break;
                case 3:  cal_init();   gwin_spawn(160,90,720,480,"Calendar",paint_calendar); break;
                case 4:  gwin_spawn(180,90,520,440,"Analog Clock",paint_aclock); break;
                case 5:  gwin_spawn(180,120,560,320,"Network",paint_network); break;
                case 6:  gwin_spawn(120,90,800,440,"Disks",paint_disks); break;
                case 7:  gwin_spawn(100,80,700,480,"Web Browser",paint_browser); break;
                case 8:  gwin_spawn(100,80,680,440,"Activity Monitor",paint_activity); break;
                case 9:  ms_reset();   gwin_spawn(120,90,720,460,"Minesweeper",paint_mines); break;
                case 10: tt_reset();   gwin_spawn(160,40,540,560,"Tetris",paint_tetris); break;
                case 11: snk_len = 0;  gwin_spawn(140,90,600,380,"Snake",paint_snake); break;
                case 12: lock_run(); break;
                case 13: save_layout(); vga_set_text_mode(25); return 0;
                case 14: { extern void reboot(void); reboot(); } break;
                case 15: { extern void shutdown(void); shutdown(); } break;
                }
                mark_wallpaper_dirty();   /* start menu painted over WP */
                repaint_all();
                if (prev_mx >= 0 && prev_my >= 0)
                    cursor_draw(prev_mx, prev_my);
                continue;
            }
            int focused = -1;
            int hz = -1;
            for (int i = 0; i < G_MAX_WIN; i++) {
                if (g_wins[i].used && g_wins[i].focused && g_wins[i].z > hz) {
                    focused = i; hz = g_wins[i].z;
                }
            }
            /* Alt+Tab: cycle focus through visible windows by z-order.
             * The next-up-in-z window comes to the front, the previous
             * focus drops behind it. Skips minimised windows because
             * they're already off-screen. */
            if (k == '\t' && kb_alt_held()) {
                int best = -1, best_z = -1;
                for (int i = 0; i < G_MAX_WIN; i++) {
                    if (!g_wins[i].used || g_wins[i].state == GWIN_MIN) continue;
                    if (g_wins[i].focused) continue;
                    if (g_wins[i].z > best_z) { best_z = g_wins[i].z; best = i; }
                }
                if (best >= 0) gwin_focus(best);
                cursor_restore();
                repaint_all();
                if (prev_mx >= 0) cursor_draw(prev_mx, prev_my);
                continue;
            }
            /* Win+D / Ctrl+M: minimise everything (show desktop).
             * Pressing again restores the previously-minimised set.
             * Static state tracks which windows we hid so we only
             * un-hide our own batch -- user-minimised windows stay
             * minimised across the toggle. */
            static int wind_show_desktop;
            static int wind_was_min[G_MAX_WIN];
            if (k == 'd' && kb_ctrl_held() && kb_alt_held()) {
                if (!wind_show_desktop) {
                    for (int i = 0; i < G_MAX_WIN; i++) {
                        wind_was_min[i] = (g_wins[i].used &&
                                           g_wins[i].state != GWIN_MIN);
                        if (wind_was_min[i]) gwin_minimise(&g_wins[i]);
                    }
                    wind_show_desktop = 1;
                } else {
                    for (int i = 0; i < G_MAX_WIN; i++)
                        if (wind_was_min[i] && g_wins[i].used &&
                            g_wins[i].state == GWIN_MIN)
                            gwin_restore(&g_wins[i]);
                    wind_show_desktop = 0;
                }
                cursor_restore();
                repaint_all();
                if (prev_mx >= 0) cursor_draw(prev_mx, prev_my);
                continue;
            }
            /* Alt+F4: close focused window without prompting (DOS-era
             * Alt+F4 reflex from Win9x lives on). */
            if (k == (int)(u8)KB_F4 && kb_alt_held()) {
                if (focused >= 0 && focused != 0) {
                    g_wins[focused].used = 0;
                    mark_wallpaper_dirty();
                    cursor_restore();
                    repaint_all();
                    if (prev_mx >= 0) cursor_draw(prev_mx, prev_my);
                }
                continue;
            }
            int handled = 0;
            if (focused >= 0) {
                void (*p)(struct gwin *) = g_wins[focused].paint;
                if      (p == paint_calc)     { calc_key(k); handled = 1; }
                else if (p == paint_notes)    { notes_key(k); handled = 1; }
                else if (p == paint_files)    { files_key(k); handled = 1; }
                else if (p == paint_term)     { term_key(k); handled = 1; }
                else if (p == paint_settings) { settings_key(k); handled = 1; }
                else if (p == paint_snake)    { snk_key(k); handled = 1; }
                else if (p == paint_calendar) { cal_key(k); handled = 1; }
                else if (p == paint_mines)    { ms_key(k); handled = 1; }
                else if (p == paint_tetris)   { tt_key(k); handled = 1; }
                else if (p == paint_browser)  { browser_key(&g_wins[focused], k); handled = 1; }
                else if (p == paint_activity) { activity_key(k); handled = 1; }
            }
            if (handled) {
                /* Keystroke handled by the focused app -- only its
                 * body changed. Skip the full desktop repaint so the
                 * framebuffer doesn't get hammered on every keypress.
                 * Exception: if the key changed something that
                 * affects the wallpaper (Settings brightness slider,
                 * wallpaper style, etc.) we need the full path. */
                cursor_restore();
                if (g_wallpaper_dirty) repaint_all();
                else                   repaint_focused(focused);
                cursor_draw(prev_mx, prev_my);
            } else if (k == 27) {
                /* ESC: close focused window; if none, exit desktop. */
                if (focused >= 0 && focused != 0) {
                    g_wins[focused].used = 0;
                    mark_wallpaper_dirty();
                    cursor_restore();
                    repaint_all();
                    cursor_draw(prev_mx, prev_my);
                } else {
                    save_layout();
                    cursor_restore();
                    vga_set_text_mode(25);
                    return 0;
                }
            }
        }
        /* Frame-rate limiter: repaint at ~30 Hz (every 33 ms at 1000 Hz
         * PIT) ONLY when an animated widget needs it. 62 Hz hammered
         * the BGA framebuffer (full-window MMIO writes per frame) and
         * showed up as wild flicker -- 30 Hz halves the MMIO load and
         * still feels smooth for Snake (game step 150 ms) and Tetris
         * (400 ms). Terminal is deliberately NOT in this list: it only
         * needs a paint when the user types or the shell prints, which
         * the event path already handles. Calculator, Files, Editor,
         * Settings, About also redraw on input only. */
        {
            static u32 last_frame;
            static int prev_had_notif;
            u32 now = pit_ticks();
            if (now - last_frame >= 33) {
                int has_anim = 0;
                for (int i = 0; i < G_MAX_WIN; i++) {
                    if (!g_wins[i].used) continue;
                    if (g_wins[i].state == GWIN_MIN) continue;
                    void (*p)(struct gwin *) = g_wins[i].paint;
                    if (p == paint_sysmon || p == paint_clock ||
                        p == paint_aclock || p == paint_tetris ||
                        p == paint_snake) {
                        has_anim = 1;
                        break;
                    }
                }
                int now_notif = has_active_notif();
                if (now_notif) has_anim = 1;
                /* When a toast just expired, the area it occupied still
                 * shows the toast pixels until the wallpaper underneath
                 * gets repainted. Mark dirty + force one extra frame. */
                if (!now_notif && prev_had_notif) {
                    mark_wallpaper_dirty();
                    has_anim = 1;
                }
                prev_had_notif = now_notif;
                /* Terminal needs continuous repaint while a command runs
                 * so the "[running...]" indicator and eventual output
                 * appear without requiring user input. */
                if (term_cmd_running) has_anim = 1;
                if (has_anim) {
                    cursor_restore();
                    repaint_all();
                    if (prev_mx >= 0) cursor_draw(prev_mx, prev_my);
                }
                last_frame = now;
            }
        }
        /* Yield to let background shell processes execute. */
        extern void proc_yield(void);
        proc_yield();
        /* Cursor-visibility guard: if a modal popup wiped the cursor
         * out and no mouse motion has happened since, draw it at the
         * current mouse position so it never just vanishes. */
        if (cursor_save_x < 0) {
            int mc, mr;
            mouse_get(&mc, &mr, NULL);
            if (mc >= 0 && mr >= 0) {
                cursor_draw(mc, mr);
                prev_mx = mc; prev_my = mr;
            }
        }
        /* Diff-flush the back buffer to MMIO. Only pixels that
         * changed since the previous present hit the framebuffer,
         * which is how typing in Editor stops looking like the
         * whole window is being repainted -- in reality only the
         * new glyph + caret-position pixels actually changed. */
        fb_present();
        /* Slight sleep to keep CPU calm. */
        __asm__ volatile("hlt");
    }
    return 0;
}
