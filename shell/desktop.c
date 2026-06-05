/* Zenbite Desktop -- top-bar menu, wallpaper, no dock.
 *
 * Layout:
 *   row 0      : menu bar  Zenbite | File | View | Help                hh:mm
 *   rows 1..23 : wallpaper. The focused app's window opens here.
 *   row 24     : status hint.
 *
 * Apps launch from the "Zenbite" dropdown (click it, or press F10).
 * F1..F4 are kept as keyboard shortcuts. ESC quits the desktop.
 *
 * Rendering: pure incremental writes to the VGA framebuffer. Each
 * cell is a single 16-bit MMIO word so there's nothing to tear. The
 * cursor occupies a single cell drawn on top; we save/restore the
 * underlying cell whenever the mouse moves. Other surfaces (clock,
 * menu, status) are only re-rendered when their state actually
 * changes.
 */

#include "kernel.h"
#include "kio.h"
#include "vga.h"
#include "string.h"
#include "fs.h"
#include "tui.h"
#include "net.h"
#include "disk.h"
#include "mbr.h"

extern void shell_run_line(const char *line);
extern void vga_redirect(char *buf, u32 cap, u32 *len);
extern void vga_get_cell_raw(int row, int col, u8 *ch, u8 *attr);
extern int  edit_main(const char *path);
extern u32  kheap_used_kib(void);
extern u32  kheap_total_kib(void);

/* --- App table ------------------------------------------------------- */
enum {
    APP_FILES = 0,
    APP_WEB,
    APP_TERMINAL,
    APP_EDITOR,
    APP_CALC,
    APP_CLOCK,
    APP_SYSMON,
    APP_NOTES,
    APP_SETTINGS,
    APP_ABOUT,
    APP_DISKMGR,
    APP_CALENDAR,
    APP_SNAKE,
    APP_TETRIS,
    APP_MINES,
    APP_ANALOG,
    APP_NETSCAN,
    APP_PARTMGR,
    APP_PROGRAMS,
    APP_REBOOT,
    APP_SHUTDOWN,
    APP_COUNT,
};

enum app_category {
    CAT_PROD = 0,
    CAT_GAMES,
    CAT_NETWORK,
    CAT_SYSTEM,
    CAT_COUNT,
};

struct app_def {
    const char *label;
    u8          category;
};

/* Master app table -- the Zenbite menu walks this in two passes:
 * first the category list, then the apps in the chosen category. */
static const struct app_def app_defs[APP_COUNT] = {
    [APP_FILES]    = { "Files",          CAT_PROD    },
    [APP_EDITOR]   = { "Editor",         CAT_PROD    },
    [APP_NOTES]    = { "Notes",          CAT_PROD    },
    [APP_CALC]     = { "Calculator",     CAT_PROD    },
    [APP_CALENDAR] = { "Calendar",       CAT_PROD    },
    [APP_PROGRAMS] = { "Programs",       CAT_PROD    },

    [APP_SNAKE]    = { "Snake",          CAT_GAMES   },
    [APP_TETRIS]   = { "Tetris",         CAT_GAMES   },
    [APP_MINES]    = { "Minesweeper",    CAT_GAMES   },

    [APP_WEB]      = { "Web (Google)",   CAT_NETWORK },
    [APP_NETSCAN]  = { "Net Scanner",    CAT_NETWORK },

    [APP_TERMINAL] = { "Terminal",       CAT_SYSTEM  },
    [APP_SYSMON]   = { "System Monitor", CAT_SYSTEM  },
    [APP_DISKMGR]  = { "Disk Manager",   CAT_SYSTEM  },
    [APP_PARTMGR]  = { "Partitions",     CAT_SYSTEM  },
    [APP_CLOCK]    = { "Clock (digital)",CAT_SYSTEM  },
    [APP_ANALOG]   = { "Clock (analog)", CAT_SYSTEM  },
    [APP_SETTINGS] = { "Settings",       CAT_SYSTEM  },
    [APP_ABOUT]    = { "About",          CAT_SYSTEM  },
    [APP_REBOOT]   = { "Reboot",         CAT_SYSTEM  },
    [APP_SHUTDOWN] = { "Shutdown",       CAT_SYSTEM  },
};

static const char *cat_label[CAT_COUNT] = {
    "Productivity >",
    "Games >",
    "Network >",
    "System >",
};

/* --- Theme: live state that the Settings app edits ------------------- */
/* Wallpaper. */
static u8 dt_fg = 9;          /* light blue stipple */
static u8 dt_bg = 1;          /* blue background    */
static int wallpaper_style = 1;  /* 0=solid 1=stipple 2=dots 3=grid */
/* 0 = inverted-block (keeps glyph under cursor visible)
 * 1 = arrow pointer (custom glyph at CP437 0x01, redefined in vga.c). */
static int cursor_style = 1;
static const char *cursor_style_label[2] = { "Block (inverted)", "Arrow" };

/* Window chrome / menu bar. */
static u8 mb_bg = 7,  mb_fg = 0;     /* menu bar */
static u8 mb_hi_bg = 0, mb_hi_fg = 15; /* highlighted menu title */
static u8 tb_bg = 7,  tb_fg = 0;     /* window title bar */
static u8 win_bg = 15, win_fg = 0;   /* window body */
static u8 menu_bg = 15, menu_fg = 0;
static u8 menu_sel_bg = 1, menu_sel_fg = 15;

/* Theme presets the Settings app cycles through. */
enum { THEME_LUNA = 0, THEME_CLASSIC, THEME_DARK, THEME_OCEAN, THEME_SUNSET, THEME_COUNT };
static int current_theme = THEME_LUNA;
static const char *theme_label[THEME_COUNT] = {
    "Luna    (XP-style)",
    "Classic (grey/white)",
    "Dark    (greys)",
    "Ocean   (cyan)",
    "Sunset  (red/yellow)",
};
static void apply_theme(int t) {
    current_theme = t;
    switch (t) {
        case THEME_LUNA:
            /* Windows-XP-Luna styling translated to 16-colour text.
             * Bottom taskbar: blue (1) with white (15) text.
             * Window title bars: blue (1) with white (15) text, so
             *   active windows have a strong header. Inactive title
             *   bars get the muted treatment via mb_hi_*.
             * Body: white (15) on black-text (0) like XP windows. */
            mb_bg=1;  mb_fg=15; mb_hi_bg=15; mb_hi_fg=1;
            tb_bg=1;  tb_fg=15;
            win_bg=15;win_fg=0;
            menu_bg=15;menu_fg=0; menu_sel_bg=1; menu_sel_fg=15;
            break;
        case THEME_CLASSIC: /* defaults */
            mb_bg=7;  mb_fg=0;  mb_hi_bg=0;  mb_hi_fg=15;
            tb_bg=7;  tb_fg=0;
            win_bg=15;win_fg=0;
            menu_bg=15;menu_fg=0; menu_sel_bg=1; menu_sel_fg=15;
            break;
        case THEME_DARK:
            mb_bg=8;  mb_fg=15; mb_hi_bg=15; mb_hi_fg=0;
            tb_bg=8;  tb_fg=15;
            win_bg=7; win_fg=0;
            menu_bg=8; menu_fg=15; menu_sel_bg=15; menu_sel_fg=0;
            break;
        case THEME_OCEAN:
            mb_bg=3;  mb_fg=15; mb_hi_bg=15; mb_hi_fg=1;
            tb_bg=3;  tb_fg=15;
            win_bg=11;win_fg=0;
            menu_bg=11;menu_fg=0; menu_sel_bg=3; menu_sel_fg=15;
            break;
        case THEME_SUNSET:
            mb_bg=4;  mb_fg=14; mb_hi_bg=14; mb_hi_fg=4;
            tb_bg=4;  tb_fg=14;
            win_bg=14;win_fg=4;
            menu_bg=14;menu_fg=4; menu_sel_bg=4; menu_sel_fg=14;
            break;
    }
}

static const char *wallpaper_label[6] = {
    "Solid", "Stipple", "Dots", "Grid", "Image (WALL.TXT)", "Bliss (XP)"
};
#define WALLPAPER_STYLE_COUNT 6
static const char *bg_color_label[6] = {
    "Blue", "Cyan", "Green", "Magenta", "Grey", "Black"
};
static const u8 bg_color_value[6] = { 1, 3, 2, 5, 8, 0 };
static int current_bg_color = 0;

/* Per-app autostart flag. config.c reads/writes this array via the
 * desktop_get_autostart / desktop_set_autostart accessors below. */
static u8 autostart_apps[APP_COUNT];
/* If non-zero, kmain skips the shell prompt and launches the desktop
 * directly. Persisted in CONFIG.TXT as `start_desktop=1`. */
static u8 autostart_desktop;

/* --- Persistence accessors (called by shell/config.c) ----------------- */
extern void config_save(void);
extern void config_load(void);

int desktop_get_keymap(char *out, int outsz) {
    extern const char *kb_get_layout(void);
    const char *s = kb_get_layout();
    int i = 0;
    while (s[i] && i < outsz - 1) { out[i] = s[i]; i++; }
    out[i] = '\0';
    return i;
}
void desktop_set_keymap(const char *s) {
    extern void kb_set_layout(const char *);
    kb_set_layout(s);
}
int  desktop_get_theme    (void) { return current_theme; }
void desktop_set_theme    (int t){ if (t >= 0 && t < THEME_COUNT) apply_theme(t); }
int  desktop_get_wallpaper(void) { return wallpaper_style; }
void desktop_set_wallpaper(int v){ if (v >= 0 && v < WALLPAPER_STYLE_COUNT) wallpaper_style = v; }
int  desktop_get_bgcolor  (void) { return current_bg_color; }
void desktop_set_bgcolor  (int v){
    if (v >= 0 && v < 6) {
        current_bg_color = v;
        dt_bg = bg_color_value[v];
    }
}
int  desktop_get_cursor   (void) { return cursor_style; }
void desktop_set_cursor   (int v){ if (v >= 0 && v < 2) cursor_style = v; }
int  desktop_get_rows     (void) {
    /* Encode graphics mode as -1 so config.c can persist the three
     * distinct states (25, 50, graphics) in one int. */
    return vga_in_graphics_mode() ? -1 : vga_get_rows();
}
void desktop_set_rows     (int v){
    if (v == -1) {
        if (vga_set_graphics(1280, 720) < 0) vga_set_text_mode(25);
    } else if (v == 25 || v == 50) {
        vga_set_text_mode(v);
    }
}

int  desktop_get_autostart(int idx) {
    if (idx < 0 || idx >= APP_COUNT) return 0;
    return autostart_apps[idx] ? 1 : 0;
}
void desktop_set_autostart(int idx, int on) {
    if (idx >= 0 && idx < APP_COUNT) autostart_apps[idx] = on ? 1 : 0;
}
int  desktop_get_start_desktop(void) { return autostart_desktop ? 1 : 0; }
void desktop_set_start_desktop(int v){ autostart_desktop = v ? 1 : 0; }

/* Pixel-desktop autostart. Default ON: kmain launches gdesk first
 * and gdesk transparently falls back to the cell desktop if BGA
 * can't bring up a usable framebuffer, so the user always gets a
 * working UI. The config loader overrides this on disks that
 * explicitly set start_gdesk=0. */
static u8 autostart_gdesk = 1;
int  desktop_get_start_gdesk(void) { return autostart_gdesk ? 1 : 0; }
void desktop_set_start_gdesk(int v){ autostart_gdesk = v ? 1 : 0; }

/* Lock-screen password. Persisted in CONFIG.TXT as
 * lock_password=<plain>. Stored plaintext for now -- the threat
 * model here is "kid sister at the keyboard", not real attackers,
 * and we don't have a crypto stack yet. Default keeps the desktop
 * accessible until the user sets one in Settings. */
static char lock_password[24] = "zenbite";
int  desktop_get_lock_password(char *out, int outsz) {
    int i = 0;
    while (i < outsz - 1 && lock_password[i]) { out[i] = lock_password[i]; i++; }
    out[i] = '\0';
    return i;
}
void desktop_set_lock_password(const char *s) {
    int i = 0;
    while (i < (int)sizeof lock_password - 1 && s[i]) {
        lock_password[i] = s[i]; i++;
    }
    lock_password[i] = '\0';
}

/* === Security / behaviour flags ======================================
 * Plain on/off toggles surfaced in Settings -> Security and persisted
 * in CONFIG.TXT. Index order is stable (used by config.c). Defaults
 * are "safe" -- nothing is locked down out of the box, but the user
 * can opt into stricter behaviour without editing the config file. */
static u8 security_flags[5] = { 0, 1, 0, 0, 0 };
static const char *security_labels[5] = {
    "Hide files starting with '.'",          /* HIDE_DOT */
    "Confirm before delete",                  /* CONFIRM_DELETE (on by default) */
    "Disable network at boot",                /* NET_OFF */
    "Lock desktop on Welcome close",          /* LOCK_ON_EXIT (placeholder) */
    "Read-only filesystem (no writes)",       /* RO_FS (placeholder) */
};
int  security_count(void)               { return 5; }
int  security_get(int i)                { return (i>=0&&i<5) ? security_flags[i] : 0; }
void security_set(int i, int v)         { if (i>=0&&i<5) security_flags[i] = v ? 1 : 0; }
const char *security_label(int i)       { return (i>=0&&i<5) ? security_labels[i] : ""; }
int  desktop_app_count_pub(void) { return APP_COUNT; }
int  desktop_app_name(int idx, char *out, int outsz) {
    if (idx < 0 || idx >= APP_COUNT || !app_defs[idx].label) return -1;
    /* First word of the label as the persistence key. */
    const char *s = app_defs[idx].label;
    int i = 0;
    while (s[i] && s[i] != ' ' && i < outsz - 1) { out[i] = s[i]; i++; }
    out[i] = '\0';
    return i;
}
int  desktop_app_lookup(const char *name) {
    char tmp[24];
    for (int i = 0; i < APP_COUNT; i++) {
        if (desktop_app_name(i, tmp, sizeof tmp) > 0 &&
            strcasecmp(tmp, name) == 0)
            return i;
    }
    return -1;
}

/* --- Menu titles in the top bar ------------------------------------- */
static const char *bar_titles[] = { "Zenbite", "File", "View", "Help" };
#define BAR_COUNT  (int)(sizeof bar_titles / sizeof bar_titles[0])

/* x-positions of the menu bar entries; computed by draw_bar(). */
static int bar_x[BAR_COUNT];
static int bar_w[BAR_COUNT];

static void draw_window(int r, int c, int w, int h, const char *title);

/* --- Multi-window desktop ------------------------------------------- */
/* A small window manager: each visible "widget" is an entry in a slot
 * table with a z-order. Modal apps (Editor, Files, Web, ...) still take
 * over the screen, but the persistent widgets (Welcome, Clock, Sysmon,
 * Mini-Calc) coexist on the desktop. Click a window to raise it; grab
 * its title bar to drag it -- all with frame-accurate updates so the
 * movement tracks the cursor instead of teleporting on release. */
enum widget_kind {
    WK_WELCOME = 0,
    WK_CLOCK,
    WK_SYSMON,
    WK_MINICALC,
    WK_ABOUT,
    WK_NOTES,
    WK_FILES,
    WK_SETTINGS,
    WK_TERMINAL,
    WK_WEB,
    WK_DISKMGR,    /* All disk slots in one view */
    WK_CALENDAR,   /* Month-view calendar */
    WK_SNAKE,      /* Snake mini-game */
    WK_TETRIS,     /* Falling-block game */
    WK_MINES,      /* Minesweeper */
    WK_ANALOG,     /* Analog clock face */
    WK_NETSCAN,    /* IP-range ping sweep */
    WK_PARTMGR,    /* Partition manager (MBR editor) */
    WK_PROGRAMS,   /* .ZBX executable launcher */
    WK_KIND_COUNT,
};
struct widget {
    int  used;
    enum widget_kind kind;
    int  r, c, w, h;
    int  z;                 /* higher = on top */
    char title[24];

    /* Live-update widgets only repaint when this differs from t.sec,
     * eliminating per-frame text rewrites that produce flicker on real
     * hardware. */
    int  prev_sec;

    /* Input line shared by all keyboard-driven widgets (calculator
     * expression, notes single-char input, settings entry, etc.). */
    int  input_len;
    char input[256];

    /* Larger document buffer (notes body, file-list scratch, ...). */
    /* Backing store for Notes / Files preview / Web rendered text /
     * Terminal scrollback. Sized to fit a small Wikipedia article
     * after the HTML stripper runs. */
    char content[8192];
    int  content_len;

    /* Generic list cursor + scroll position (files, settings tabs). */
    int  sel;
    int  top;
    int  tab;          /* settings: active pane */
    int  row;          /* settings: focused row inside pane */

    /* True if this widget has keyboard focus. */
    int  focused;
    /* Minimized: skip rendering + hit-testing. The widget appears as
     * an entry on the taskbar instead; click there to restore. */
    int  minimized;
    /* Dirty bit -- something changed and the widget needs a fresh
     * render even if its kind isn't normally per-frame. */
    int  dirty;
};
#define MAX_WIDGETS 8
static struct widget widgets[MAX_WIDGETS];
static int next_z = 1;

static void add_welcome_widget(void) {
    widgets[0].used = 1;
    widgets[0].kind = WK_WELCOME;
    widgets[0].r = 3; widgets[0].c = 4;
    widgets[0].w = 38; widgets[0].h = 9;
    widgets[0].z = next_z++;
    const char *t = "Welcome";
    int i = 0; while (t[i] && i < (int)sizeof widgets[0].title - 1) {
        widgets[0].title[i] = t[i]; i++; }
    widgets[0].title[i] = '\0';
}

static void set_title(struct widget *w, const char *t) {
    int j = 0;
    while (t[j] && j < (int)sizeof w->title - 1) { w->title[j] = t[j]; j++; }
    w->title[j] = '\0';
}

static void focus_only(int idx) {
    for (int i = 0; i < MAX_WIDGETS; i++) widgets[i].focused = (i == idx);
}

static int spawn_widget(enum widget_kind kind) {
    /* Singleton kinds: raise the existing one instead of stacking copies. */
    int singleton =
        kind == WK_NOTES   || kind == WK_SETTINGS || kind == WK_FILES   ||
        kind == WK_ABOUT   || kind == WK_TERMINAL || kind == WK_WEB     ||
        kind == WK_DISKMGR || kind == WK_CALENDAR || kind == WK_TETRIS  ||
        kind == WK_MINES   || kind == WK_ANALOG   || kind == WK_NETSCAN ||
        kind == WK_PARTMGR || kind == WK_PROGRAMS;
    if (singleton) {
        for (int i = 0; i < MAX_WIDGETS; i++) {
            if (widgets[i].used && widgets[i].kind == kind) {
                widgets[i].z = next_z++;
                focus_only(i);
                return i;
            }
        }
    }
    for (int i = 1; i < MAX_WIDGETS; i++) {   /* slot 0 reserved for welcome */
        if (widgets[i].used) continue;
        widgets[i].used = 1;
        widgets[i].kind = kind;
        widgets[i].z = next_z++;
        widgets[i].prev_sec   = -1;
        widgets[i].input_len  = 0;
        widgets[i].input[0]   = '\0';
        widgets[i].content_len = 0;
        widgets[i].content[0]  = '\0';
        widgets[i].sel = 0; widgets[i].top = 0;
        widgets[i].tab = 0; widgets[i].row = 0;
        widgets[i].focused = 1;
        widgets[i].dirty   = 1;
        switch (kind) {
            case WK_CLOCK:
                widgets[i].r = 4 + i; widgets[i].c = 38 + i;
                widgets[i].w = 36; widgets[i].h = 8;
                set_title(&widgets[i], "Clock");
                widgets[i].focused = 0;       /* read-only widget */
                break;
            case WK_SYSMON:
                widgets[i].r = 12; widgets[i].c = 10 + i*2;
                widgets[i].w = 50; widgets[i].h = 12;
                set_title(&widgets[i], "System Monitor");
                widgets[i].focused = 0;
                break;
            case WK_MINICALC:
                widgets[i].r = 6 + i; widgets[i].c = 20 + i*2;
                widgets[i].w = 36; widgets[i].h = 6;
                set_title(&widgets[i], "Calculator");
                break;
            case WK_ABOUT:
                widgets[i].r = 4; widgets[i].c = 14;
                widgets[i].w = 52; widgets[i].h = 12;
                set_title(&widgets[i], "About Zenbite");
                widgets[i].focused = 0;
                break;
            case WK_NOTES: {
                widgets[i].r = 3; widgets[i].c = 8;
                widgets[i].w = 60; widgets[i].h = 18;
                set_title(&widgets[i], "Notes");
                /* Preload existing notes file. */
                int fh = fs_open("NOTES.TXT");
                if (fh >= 0) {
                    int n = fs_read(fh, widgets[i].content,
                                    (int)sizeof widgets[i].content - 1);
                    if (n < 0) n = 0;
                    widgets[i].content_len = n;
                    widgets[i].content[n] = '\0';
                    fs_close(fh);
                }
                break;
            }
            case WK_FILES: {
                widgets[i].r = 2; widgets[i].c = 2;
                widgets[i].w = 76; widgets[i].h = 21;
                set_title(&widgets[i], "Files (Commander)");
                widgets[i].content_len = 0;   /* state inited on first render */
                break;
            }
            case WK_SETTINGS:
                widgets[i].r = 3; widgets[i].c = 8;
                widgets[i].w = 60; widgets[i].h = 18;
                set_title(&widgets[i], "Settings");
                break;
            case WK_TERMINAL:
                widgets[i].r = 3; widgets[i].c = 4;
                widgets[i].w = 70; widgets[i].h = 18;
                set_title(&widgets[i], "Terminal");
                break;
            case WK_WEB:
                widgets[i].r = 3; widgets[i].c = 4;
                widgets[i].w = 72; widgets[i].h = 18;
                set_title(&widgets[i], "Web");
                break;
            case WK_DISKMGR:
                widgets[i].r = 3; widgets[i].c = 8;
                widgets[i].w = 64; widgets[i].h = 18;
                set_title(&widgets[i], "Disk Manager");
                widgets[i].focused = 0;
                break;
            case WK_CALENDAR:
                widgets[i].r = 4; widgets[i].c = 22;
                widgets[i].w = 36; widgets[i].h = 12;
                set_title(&widgets[i], "Calendar");
                /* tab encodes (month-1)*12 + year offset from current */
                widgets[i].tab = 0;
                break;
            case WK_SNAKE:
                widgets[i].r = 3; widgets[i].c = 14;
                widgets[i].w = 52; widgets[i].h = 18;
                set_title(&widgets[i], "Snake");
                /* Reset game state on spawn. */
                widgets[i].sel = 0;       /* score */
                widgets[i].top = 0;       /* dir: 0=right 1=down 2=left 3=up */
                widgets[i].row = 0;       /* game state: 0=playing 1=over */
                widgets[i].content_len = 0;
                widgets[i].prev_sec = -1;
                break;
            case WK_TETRIS:
                widgets[i].r = 2; widgets[i].c = 22;
                widgets[i].w = 36; widgets[i].h = 22;
                set_title(&widgets[i], "Tetris");
                widgets[i].prev_sec = -1;
                widgets[i].sel = 0;       /* score */
                widgets[i].top = 0;       /* lines cleared */
                widgets[i].row = 0;       /* state: 0=playing 1=over 2=paused */
                widgets[i].content_len = 0;
                break;
            case WK_MINES:
                widgets[i].r = 2; widgets[i].c = 14;
                widgets[i].w = 52; widgets[i].h = 22;
                set_title(&widgets[i], "Minesweeper");
                widgets[i].sel = 0;       /* cursor row */
                widgets[i].top = 0;       /* cursor col */
                widgets[i].row = 0;       /* state: 0=playing 1=won 2=lost */
                widgets[i].content_len = 0;
                widgets[i].prev_sec = -1;
                break;
            case WK_ANALOG:
                widgets[i].r = 3; widgets[i].c = 36;
                widgets[i].w = 24; widgets[i].h = 12;
                set_title(&widgets[i], "Analog Clock");
                widgets[i].focused = 0;
                widgets[i].prev_sec = -1;
                break;
            case WK_NETSCAN:
                widgets[i].r = 3; widgets[i].c = 10;
                widgets[i].w = 60; widgets[i].h = 19;
                set_title(&widgets[i], "Net Scanner");
                widgets[i].sel = 0;       /* current octet during scan */
                widgets[i].top = 0;       /* found count */
                widgets[i].row = 0;       /* state: 0=idle 1=scanning 2=done */
                widgets[i].content_len = 0;
                widgets[i].prev_sec = -1;
                break;
            case WK_PARTMGR:
                widgets[i].r = 3; widgets[i].c = 8;
                widgets[i].w = 66; widgets[i].h = 18;
                set_title(&widgets[i], "Partitions");
                widgets[i].row = -1;     /* selected raw disk -- resolved on render */
                widgets[i].sel = 0;      /* slot 0..3 */
                widgets[i].top = 0;      /* unused */
                widgets[i].content_len = 0;
                break;
            case WK_PROGRAMS:
                widgets[i].r = 3; widgets[i].c = 14;
                widgets[i].w = 48; widgets[i].h = 18;
                set_title(&widgets[i], "Programs");
                widgets[i].sel = 0;      /* selected .zbx index */
                widgets[i].top = 0;      /* scroll */
                break;
            default: break;
        }
        if (widgets[i].focused) focus_only(i);
        return i;
    }
    return -1;     /* table full */
}

static void close_widget(int i) {
    if (i < 0 || i >= MAX_WIDGETS) return;
    if (i == 0) return;          /* Welcome is permanent; user can ignore it */
    /* Persist Notes on close. */
    if (widgets[i].kind == WK_NOTES) {
        fs_create("NOTES.TXT");
        int fh = fs_open("NOTES.TXT");
        if (fh >= 0) {
            fs_write(fh, widgets[i].content, widgets[i].content_len);
            fs_close(fh);
        }
    }
    widgets[i].used = 0;
    widgets[i].focused = 0;
}

static int widget_titlebar_hit(int idx, int col, int row) {
    struct widget *w = &widgets[idx];
    if (!w->used) return 0;
    return row == w->r && col >= w->c && col < w->c + w->w;
}

/* Hit-test the red close dot. draw_window paints it at title-row,
 * col w->c+1 (see draw_window). A click there closes the window. */
static int widget_close_hit(int idx, int col, int row) {
    struct widget *w = &widgets[idx];
    if (!w->used) return 0;
    return row == w->r && col == w->c + 1;
}

/* Hit-test the yellow minimize dot at col w->c+2. */
static int widget_min_hit(int idx, int col, int row) {
    struct widget *w = &widgets[idx];
    if (!w->used) return 0;
    return row == w->r && col == w->c + 2;
}

static void minimize_widget(int i) {
    if (i < 0 || i >= MAX_WIDGETS) return;
    if (i == 0) return;          /* Welcome stays put */
    widgets[i].minimized = 1;
    widgets[i].focused   = 0;
}

static void unminimize_widget(int i) {
    if (i < 0 || i >= MAX_WIDGETS) return;
    widgets[i].minimized = 0;
    widgets[i].z = next_z++;
    focus_only(i);
}

/* Topmost widget (highest z) that contains (col,row). Returns -1 for
 * none. Minimized widgets are skipped -- they live on the taskbar, not
 * the canvas. */
static int widget_at(int col, int row) {
    int best = -1, best_z = -1;
    for (int i = 0; i < MAX_WIDGETS; i++) {
        struct widget *w = &widgets[i];
        if (!w->used || w->minimized) continue;
        if (col < w->c || col >= w->c + w->w) continue;
        if (row < w->r || row >= w->r + w->h) continue;
        if (w->z > best_z) { best_z = w->z; best = i; }
    }
    return best;
}

static void raise_widget(int i) {
    if (i < 0 || i >= MAX_WIDGETS) return;
    widgets[i].z = next_z++;
}

/* --- Per-widget renderers ------------------------------------------- */
static void render_welcome(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    vga_write(w->r + 2, w->c + 2,
              "Welcome to Zenbite v" ZENBITE_VERSION, 1, win_bg);
    vga_write(w->r + 4, w->c + 2,
              "Drag this title bar to move me.", win_fg, win_bg);
    vga_write(w->r + 5, w->c + 2,
              "Click Zenbite menu for apps.", win_fg, win_bg);
    /* Live geometry readout. Updates every frame in graphics mode so
     * the user has confirmation that the resolution-switch worked. */
    char geom[40];
    if (vga_in_graphics_mode())
        ksnprintf(geom, sizeof geom, "Display: 1280x720 VBE  (%dx%d cells)",
                  VGA_COLS, VGA_ROWS);
    else
        ksnprintf(geom, sizeof geom, "Display: 80x%d text mode", VGA_ROWS);
    vga_write(w->r + 6, w->c + 2, geom, 8, win_bg);
}

static void render_clock_widget(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    struct rtc_time t; rtc_read(&t);
    int dx = w->c + (w->w - 17) / 2, dy = w->r + 2;
    char buf[16];
    ksnprintf(buf, sizeof buf, "%02u:%02u:%02u",
              (u32)t.hour, (u32)t.min, (u32)t.sec);
    vga_write(dy,     dx, buf, 1, win_bg);
    char date[16];
    ksnprintf(date, sizeof date, "%04u-%02u-%02u",
              (u32)t.year, (u32)t.month, (u32)t.day);
    vga_write(dy + 1, dx, date, 8, win_bg);
    vga_write(w->r + w->h - 2, w->c + 2,
              "Live clock. Drag to move.", 8, win_bg);
}

static void render_sysmon_widget(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    const struct cpu_info *ci = cpu_info();
    char line[80];
    /* CPU header. Identify the family for pre-CPUID chips too. */
    if (!ci->has_cpuid)
        ksnprintf(line, sizeof line, "CPU:  %s (family %u, no CPUID)",
                  ci->vendor, ci->family);
    else
        ksnprintf(line, sizeof line, "CPU:  %s  family %u model %u%s",
                  ci->vendor, ci->family, ci->model,
                  ci->long_mode ? "  (64-bit capable)" : "");
    vga_write(w->r + 1, w->c + 2, line, win_fg, win_bg);
    /* Display: live text-mode geometry. */
    ksnprintf(line, sizeof line, "Disp: %dx%d  @ 70 Hz%s",
              VGA_COLS, VGA_ROWS,
              ci->has_pse ? "  (paging on)" : "  (paging off)");
    vga_write(w->r + 2, w->c + 2, line, 8, win_bg);

    /* Total RAM = low (BIOS reserved 640 KiB) + kernel image bytes +
     * PMM-managed pool. */
    extern char _kernel_end[];
    u32 kernel_kib = ((u32)_kernel_end - 0x100000 + 1023) / 1024;
    u32 pmm_used = pmm_used_kib();
    u32 pmm_tot  = pmm_total_kib();
    u32 used     = kernel_kib + pmm_used;
    u32 tot      = 640 + kernel_kib + pmm_tot;
    ksnprintf(line, sizeof line, "RAM:  %u/%u KiB used (%u%%)",
              used, tot, tot ? (used * 100 / tot) : 0u);
    vga_write(w->r + 3, w->c + 2, line, win_fg, win_bg);
    int bw = w->w - 6;
    int filled = tot ? (int)((used * bw) / tot) : 0;
    for (int x = 0; x < bw; x++)
        vga_put_cell(w->r + 4, w->c + 3 + x,
                     x < filled ? 0xDB : 0xB0,
                     (x < filled ? 4 : 8), win_bg);
    ksnprintf(line, sizeof line, "  kernel %u KiB  +  PMM %u/%u KiB",
              kernel_kib, pmm_used, pmm_tot);
    vga_write(w->r + 5, w->c + 2, line, 8, win_bg);
    ksnprintf(line, sizeof line, "  kheap  %u/%u KiB",
              kheap_used_kib(), kheap_total_kib());
    vga_write(w->r + 6, w->c + 2, line, 8, win_bg);
    int row = w->r + 8;
    for (int i = 0; i < DISK_MAX && row < w->r + w->h - 1; i++) {
        struct disk *d = disk_get(i);
        if (!d || !d->present) continue;
        ksnprintf(line, sizeof line, "  %-4s  %u KiB",
                  d->name, d->sectors / 2);
        vga_write(row++, w->c + 2, line, win_fg, win_bg);
    }
}

/* Recursive-descent expression eval used by the Mini-Calc widget. */
static const char *g_calc_p;
static int calc_expr(void);
static int calc_atom(void) {
    while (*g_calc_p == ' ') g_calc_p++;
    if (*g_calc_p == '(') {
        g_calc_p++;
        int v = calc_expr();
        if (*g_calc_p == ')') g_calc_p++;
        return v;
    }
    int sign = 1;
    if (*g_calc_p == '-') { sign = -1; g_calc_p++; }
    int v = 0;
    while (*g_calc_p >= '0' && *g_calc_p <= '9') {
        v = v * 10 + (*g_calc_p - '0');
        g_calc_p++;
    }
    return sign * v;
}
static int calc_term(void) {
    int v = calc_atom();
    while (*g_calc_p == '*' || *g_calc_p == '/') {
        char op = *g_calc_p++; int rhs = calc_atom();
        if (op == '*') v *= rhs; else if (rhs) v /= rhs; else v = 0;
    }
    return v;
}
static int calc_expr(void) {
    int v = calc_term();
    while (*g_calc_p == '+' || *g_calc_p == '-') {
        char op = *g_calc_p++; int rhs = calc_term();
        v = (op == '+') ? v + rhs : v - rhs;
    }
    return v;
}

static void render_minicalc(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    vga_write(w->r + 1, w->c + 2,
              w->focused ? "Type expression, ENTER to compute:"
                         : "(click to focus)  press ENTER:",
              w->focused ? win_fg : 8, win_bg);
    vga_fill (w->r + 2, w->c + 2, w->w - 4, 1, ' ', win_fg, win_bg);
    vga_write(w->r + 2, w->c + 2, "> ", 2, win_bg);
    vga_write(w->r + 2, w->c + 4, w->input, win_fg, win_bg);
    if (w->focused)
        vga_put_cell(w->r + 2, w->c + 4 + w->input_len, '_', win_fg, win_bg);
}

static void render_about(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    vga_write(w->r + 2, w->c + 3, "Zenbite v" ZENBITE_VERSION, 1, win_bg);
    vga_write(w->r + 3, w->c + 3, "32-bit retro operating system, MIT", win_fg, win_bg);
    vga_write(w->r + 5, w->c + 3, "(c) 2026 Oliver Petz and contributors", win_fg, win_bg);
    vga_write(w->r + 7, w->c + 3, "Built from scratch -- bootloader,", win_fg, win_bg);
    vga_write(w->r + 8, w->c + 3, "kernel, FAT12/16/32, TCP/IP, shell, GUI.", win_fg, win_bg);
    vga_write(w->r + w->h - 2, w->c + 3,
              "Drag title to move. Red dot closes.", 8, win_bg);
}

static void render_notes(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    vga_write(w->r + 1, w->c + 2,
              "Notes -- click red dot to save & close.",
              8, win_bg);
    /* Render the content buffer with simple wrapping. */
    int row0 = w->r + 3, col0 = w->c + 2;
    int rows = w->h - 4, cols = w->w - 4;
    for (int rr = 0; rr < rows; rr++)
        vga_fill(row0 + rr, col0, cols, 1, ' ', win_fg, win_bg);
    int rr = 0, cc = 0;
    for (int i = 0; i < w->content_len && rr < rows; i++) {
        char ch = w->content[i];
        if (ch == '\n' || cc >= cols) {
            rr++; cc = 0;
            if (ch == '\n') continue;
        }
        if (ch >= ' ' && (u8)ch < 127 && rr < rows)
            vga_put_cell(row0 + rr, col0 + cc++, ch, win_fg, win_bg);
    }
    /* Caret. */
    if (w->focused && rr < rows)
        vga_put_cell(row0 + rr, col0 + cc, '_', win_fg, win_bg);
}

/* Deferred "open the selected file in the editor" request. The actual
 * tui_end / edit_main / tui_init dance has to happen at the top of the
 * desktop main loop (where draw state is consistent), not inside a
 * click or key handler. The desktop main loop checks this each tick. */
static char  files_open_path[FS_PATH_MAX];
static int   files_open_pending;

/* Two-pane commander-style file browser. Each pane has its own path,
 * scroll position, and selection. Tab swaps the focused pane; F2/Del/
 * F7 rename/delete/mkdir in-place; F3 toggles a preview overlay on
 * the inactive pane; F5/F6 copy/move from focused to other pane.
 * State lives in w->content overlay; w->sel/w->top mirror the active
 * pane's selection for the existing mouse-row click handler. */
struct files_state {
    char path[2][FS_PATH_MAX];   /* e.g. "A:\\HOME\\OLIVER" -- no trailing \ */
    int  sel[2];
    int  top[2];
    u8   active;                 /* 0 = left, 1 = right */
    u8   sort_mode;              /* 0 = name, 1 = size, 2 = extension */
    u8   preview;                /* 1 = right pane shows file preview */
};

/* Where to dispatch the editor launch when the user opens a text file. */
static char files_open_path_full[FS_PATH_MAX];

static void files_path_current(char *out, int outsz) {
    /* Build "X:<cwd>" without trailing backslash. */
    char drv = fs_get_drive();
    const char *cwd = fs_cwd();
    int len = ksnprintf(out, (size_t)outsz, "%c:%s", drv, cwd);
    while (len > 3 && out[len - 1] == '\\') { len--; out[len] = '\0'; }
}

static void files_state_init(struct widget *w) {
    struct files_state *s = (struct files_state *)w->content;
    memset(s, 0, sizeof *s);
    files_path_current(s->path[0], FS_PATH_MAX);
    files_path_current(s->path[1], FS_PATH_MAX);
    s->active = 0;
    s->sort_mode = 0;
    w->content_len = (int)sizeof *s;
}

/* Sort: directories first; secondary key chosen by sort_mode. */
static int files_cmp(const struct fs_dirent *a, const struct fs_dirent *b, int mode) {
    int ad = (a->attr & FS_ATTR_DIR) ? 0 : 1;
    int bd = (b->attr & FS_ATTR_DIR) ? 0 : 1;
    if (ad != bd) return ad - bd;
    if (mode == 1) {
        if (a->size != b->size) return (a->size > b->size) ? -1 : 1;
    } else if (mode == 2) {
        /* Sort by extension: scan to last '.' in each name. */
        const char *ae = "", *be = "";
        for (const char *p = a->name; *p; p++) if (*p == '.') ae = p;
        for (const char *p = b->name; *p; p++) if (*p == '.') be = p;
        int c = strcasecmp(ae, be);
        if (c) return c;
    }
    return strcasecmp(a->name, b->name);
}

static int files_read_path(const char *path, struct fs_dirent *ents, int max, int sort_mode) {
    int dh = fs_opendir(path);
    if (dh < 0) return -1;
    int n = 0;
    while (n < max && fs_readdir(dh, &ents[n])) n++;
    fs_closedir(dh);
    /* Insertion sort -- n <= 128 so this is fine. */
    for (int i = 1; i < n; i++) {
        struct fs_dirent tmp = ents[i];
        int j = i - 1;
        while (j >= 0 && files_cmp(&ents[j], &tmp, sort_mode) > 0) {
            ents[j + 1] = ents[j];
            j--;
        }
        ents[j + 1] = tmp;
    }
    return n;
}

/* Join "<base>\<leaf>" handling "..", root, and trailing slashes.
 * If leaf == ".." the last path component is popped; if popping past the
 * drive root the path is left as "X:\". */
static void files_path_join(char *path, int outsz, const char *leaf) {
    if (strcmp(leaf, "..") == 0) {
        int len = (int)strlen(path);
        if (len > 3) {
            /* Drop trailing component. */
            int k = len - 1;
            while (k > 2 && path[k] != '\\') k--;
            path[k] = '\0';
            if (k <= 2) {
                /* Root: "X:" + "\". */
                if (path[2] != '\\') {
                    path[2] = '\\'; path[3] = '\0';
                }
            }
        }
        return;
    }
    int len = (int)strlen(path);
    if (len == 0 || (len > 0 && path[len - 1] != '\\')) {
        if (len < outsz - 1) { path[len++] = '\\'; path[len] = '\0'; }
    }
    int rem = outsz - len - 1;
    for (int i = 0; leaf[i] && rem > 0; i++, rem--) path[len++] = leaf[i];
    path[len] = '\0';
    /* Strip duplicated backslashes that may have come from "X:\" + "leaf". */
    for (int i = 0; path[i] && path[i + 1]; ) {
        if (path[i] == '\\' && path[i + 1] == '\\') {
            for (int j = i; path[j]; j++) path[j] = path[j + 1];
        } else { i++; }
    }
}

/* Set the global drive + cwd to point at the given absolute path
 * "X:\foo\bar" so subsequent fs_create / fs_unlink / fs_mkdir act on it.
 * Returns 0 on success. */
static int files_set_global_cwd(const char *path) {
    if (!path[0] || path[1] != ':') return -1;
    if (fs_set_drive(path[0]) < 0) return -1;
    if (fs_chdir("\\") < 0) return -1;
    /* Walk components after "X:\\". */
    int i = 2;
    if (path[i] == '\\') i++;
    while (path[i]) {
        char leaf[FS_NAME_MAX];
        int j = 0;
        while (path[i] && path[i] != '\\' && j < (int)sizeof leaf - 1) {
            leaf[j++] = path[i++];
        }
        leaf[j] = '\0';
        if (j == 0) { if (path[i] == '\\') i++; continue; }
        if (fs_chdir(leaf) < 0) return -1;
        if (path[i] == '\\') i++;
    }
    return 0;
}

/* Take a top-bar-ish row prompt: shows `label`, then accepts text until
 * ENTER. Returns 1 on accept, 0 on cancel. Reuses the input buffer in
 * the widget so we don't need a scratch buffer up the stack. */
static int files_prompt(struct widget *w, const char *label,
                        const char *prefill, char *out, int outsz) {
    int r = w->r + w->h - 2;
    int c = w->c + 2;
    int wpix = w->w - 4;
    vga_fill(r, c, wpix, 1, ' ', win_bg, win_fg);
    vga_write(r, c, label, win_bg, win_fg);
    int lc = c + (int)strlen(label) + 1;
    int len = 0;
    if (prefill) {
        for (int i = 0; prefill[i] && i < outsz - 1; i++) out[i] = prefill[i];
        len = (int)strlen(prefill);
        out[len] = '\0';
    } else {
        out[0] = '\0';
    }
    for (;;) {
        vga_fill(r, lc, wpix - (int)strlen(label) - 1, 1, ' ', win_bg, win_fg);
        for (int i = 0; i < len; i++) vga_put_cell(r, lc + i, out[i], win_bg, win_fg);
        vga_put_cell(r, lc + len, '_', win_bg, win_fg);
        vga_present();
        int k = kb_getc();
        if (k == 27) { out[0] = '\0'; return 0; }
        if (k == '\n' || k == '\r') return 1;
        if (k == '\b') { if (len > 0) { len--; out[len] = '\0'; } continue; }
        if (k < ' ' || k > '~') continue;
        if (len + 1 < outsz) { out[len++] = (char)k; out[len] = '\0'; }
    }
}

static int files_confirm(struct widget *w, const char *msg) {
    int r = w->r + w->h - 2;
    int c = w->c + 2;
    int wpix = w->w - 4;
    vga_fill(r, c, wpix, 1, ' ', 14, 4);
    vga_write(r, c + 1, msg, 14, 4);
    vga_present();
    for (;;) {
        int k = kb_getc();
        if (k == 'y' || k == 'Y') return 1;
        if (k == 'n' || k == 'N' || k == 27 || k == '\n' || k == '\r') return 0;
    }
}

/* Append leaf onto a "X:\dir" path producing a usable fs_open string. */
static void files_full_path(const struct files_state *s, int pane,
                            const char *leaf, char *out, int outsz) {
    int n = ksnprintf(out, (size_t)outsz, "%s", s->path[pane]);
    if (n > 0 && out[n - 1] != '\\' && n < outsz - 1) { out[n++] = '\\'; out[n] = '\0'; }
    int rem = outsz - n - 1;
    for (int i = 0; leaf[i] && rem > 0; i++, rem--) out[n++] = leaf[i];
    out[n] = '\0';
}

static void files_pane_activate(struct widget *w, int pane) {
    struct files_state *s = (struct files_state *)w->content;
    struct fs_dirent ents[128];
    int n = files_read_path(s->path[pane], ents, 128, s->sort_mode);
    if (n <= 0 || s->sel[pane] >= n) return;
    struct fs_dirent *e = &ents[s->sel[pane]];
    if (e->attr & FS_ATTR_DIR) {
        files_path_join(s->path[pane], FS_PATH_MAX, e->name);
        s->sel[pane] = 0; s->top[pane] = 0;
        w->dirty = 1;
    } else {
        /* Launch editor with the absolute path. */
        files_full_path(s, pane, e->name, files_open_path_full, FS_PATH_MAX);
        /* Drop the "X:" prefix because edit_main wants a relative path
         * against the current drive. Set the global cwd to the file's
         * directory first. */
        files_set_global_cwd(s->path[pane]);
        int i = 0;
        while (e->name[i] && i < FS_PATH_MAX - 1) {
            files_open_path[i] = e->name[i]; i++;
        }
        files_open_path[i] = '\0';
        files_open_pending = 1;
    }
}

/* Mouse hit on a pane row -> (pane, row index). Returns -1 if outside. */
static int files_hit(struct widget *w, int mc, int mr, int *out_pane) {
    int rows = w->h - 5;
    int body_r = w->r + 3;
    int rel = mr - body_r;
    if (rel < 0 || rel >= rows) return -1;
    int pane_w = (w->w - 3) / 2;
    int left_c = w->c + 1;
    int right_c = w->c + 2 + pane_w;
    if (mc >= left_c && mc < left_c + pane_w)        { *out_pane = 0; return rel; }
    if (mc >= right_c && mc < right_c + pane_w)      { *out_pane = 1; return rel; }
    return -1;
}

static void files_render_pane(struct widget *w, int pane, int x, int y,
                              int pw, int ph, struct fs_dirent *ents, int n) {
    struct files_state *s = (struct files_state *)w->content;
    int active = (pane == s->active);
    /* Pane header: path. */
    vga_fill (y, x, pw, 1, ' ', active ? win_bg : win_fg, active ? win_fg : win_bg);
    char hdr[FS_PATH_MAX + 2];
    int len = ksnprintf(hdr, sizeof hdr, " %s", s->path[pane]);
    if (len > pw) {
        /* Truncate from the LEFT so the last directory component stays
         * visible -- much more useful than truncating from the right. */
        int skip = len - pw;
        for (int i = 0; i < pw && hdr[skip + i]; i++)
            vga_put_cell(y, x + i, hdr[skip + i],
                         active ? win_bg : win_fg, active ? win_fg : win_bg);
    } else {
        for (int i = 0; i < len; i++)
            vga_put_cell(y, x + i, hdr[i],
                         active ? win_bg : win_fg, active ? win_fg : win_bg);
    }
    /* Bound sel/top. */
    if (s->sel[pane] >= n) s->sel[pane] = n - 1;
    if (s->sel[pane] < 0)  s->sel[pane] = 0;
    if (s->top[pane] > s->sel[pane]) s->top[pane] = s->sel[pane];
    if (s->sel[pane] >= s->top[pane] + ph) s->top[pane] = s->sel[pane] - ph + 1;
    for (int i = 0; i < ph; i++) {
        int idx = s->top[pane] + i;
        vga_fill(y + 1 + i, x, pw, 1, ' ', win_fg, win_bg);
        if (idx >= n) continue;
        char line[80];
        int is_dir = (ents[idx].attr & FS_ATTR_DIR);
        if (is_dir)
            ksnprintf(line, sizeof line, " %c %-12s     <DIR>",
                      idx == s->sel[pane] && active ? '>' : ' ', ents[idx].name);
        else
            ksnprintf(line, sizeof line, " %c %-12s %8u",
                      idx == s->sel[pane] && active ? '>' : ' ', ents[idx].name,
                      ents[idx].size);
        u8 fg = win_fg, bg = win_bg;
        if (idx == s->sel[pane]) {
            if (active) { fg = win_bg; bg = win_fg; }
            else        { fg = 8;      bg = win_bg; }
        } else if (is_dir) fg = 1;
        int lnln = (int)strlen(line);
        for (int xc = 0; xc < pw; xc++)
            vga_put_cell(y + 1 + i, x + xc,
                         xc < lnln ? line[xc] : ' ', fg, bg);
    }
}

static void files_render_preview(struct widget *w, int x, int y, int pw, int ph) {
    struct files_state *s = (struct files_state *)w->content;
    int active_pane = s->active;
    /* Find currently selected file. */
    struct fs_dirent ents[128];
    int n = files_read_path(s->path[active_pane], ents, 128, s->sort_mode);
    if (n <= 0 || s->sel[active_pane] >= n) {
        vga_write(y, x, " Preview: no selection", 8, win_bg);
        return;
    }
    struct fs_dirent *e = &ents[s->sel[active_pane]];
    char hdr[64];
    if (e->attr & FS_ATTR_DIR) {
        ksnprintf(hdr, sizeof hdr, " Folder: %s", e->name);
        vga_write(y, x, hdr, 14, win_bg);
        return;
    }
    ksnprintf(hdr, sizeof hdr, " File: %s  (%u B)", e->name, e->size);
    vga_write(y, x, hdr, 14, win_bg);

    /* Read & display up to ph-1 lines. */
    char full[FS_PATH_MAX];
    files_full_path(s, active_pane, e->name, full, FS_PATH_MAX);
    int fh = fs_open(full);
    if (fh < 0) {
        vga_write(y + 1, x, " (cannot open)", 4, win_bg);
        return;
    }
    char buf[256];
    int row = y + 1;
    int col = x;
    int line_left = pw;
    int lines_left = ph - 1;
    int got;
    while (lines_left > 0 && (got = fs_read(fh, buf, sizeof buf)) > 0) {
        for (int i = 0; i < got && lines_left > 0; i++) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n' || line_left <= 0) {
                row++; col = x; line_left = pw; lines_left--;
                if (c == '\n') continue;
            }
            if (c < ' ' || c > '~') c = '.';
            vga_put_cell(row, col++, c, win_fg, win_bg);
            line_left--;
        }
    }
    fs_close(fh);
}

static void render_files(struct widget *w) {
    struct files_state *s = (struct files_state *)w->content;
    draw_window(w->r, w->c, w->w, w->h, w->title);
    if (w->content_len == 0 ||
            w->content_len < (int)sizeof(struct files_state))
        files_state_init(w);

    /* Header line. */
    const char *modes[3] = { "name", "size", "ext" };
    char hdr[80];
    ksnprintf(hdr, sizeof hdr,
              "TAB | F2 ren | Del rm | F7 mkd | F5 cp | F6 mv | F3 prev | F4 / search | S sort=%s",
              modes[s->sort_mode % 3]);
    vga_write(w->r + 1, w->c + 2, hdr, 8, win_bg);

    int rows = w->h - 5;
    int pane_w = (w->w - 3) / 2;
    /* Read each pane. */
    static struct fs_dirent left_ents[128], right_ents[128];
    int ln = files_read_path(s->path[0], left_ents,  128, s->sort_mode);
    int rn = files_read_path(s->path[1], right_ents, 128, s->sort_mode);
    if (ln < 0) ln = 0;
    if (rn < 0) rn = 0;
    files_render_pane(w, 0, w->c + 1,           w->r + 2, pane_w, rows,
                      left_ents,  ln);
    if (s->preview) {
        files_render_preview(w, w->c + 2 + pane_w, w->r + 2, pane_w, rows);
    } else {
        files_render_pane(w, 1, w->c + 2 + pane_w, w->r + 2, pane_w, rows,
                          right_ents, rn);
    }
    /* Sync widget's sel/top with active pane so the existing mouse-row
     * handler keeps working. */
    w->sel = s->sel[s->active];
    w->top = s->top[s->active];
}

/* Activation for the existing mouse path (single-click on a row). */
static void files_activate(struct widget *w) {
    struct files_state *s = (struct files_state *)w->content;
    if (w->content_len == 0) files_state_init(w);
    s->sel[s->active] = w->sel;
    files_pane_activate(w, s->active);
}

/* --- File-manager keyboard handler (called from the main dispatch). --- */
static int files_handle_key(struct widget *w, int k) {
    struct files_state *s = (struct files_state *)w->content;
    if (w->content_len == 0) files_state_init(w);
    int pane = s->active;
    /* Reload current view to know the count. */
    struct fs_dirent ents[128];
    int n = files_read_path(s->path[pane], ents, 128, s->sort_mode);
    if (n < 0) n = 0;
    if (k == '\t') {
        if (!s->preview) { s->active ^= 1; w->dirty = 1; return 1; }
        return 0;
    }
    if (k == (int)KB_UP) {
        if (s->sel[pane] > 0) { s->sel[pane]--; w->dirty = 1; return 1; }
        return 1;
    }
    if (k == (int)KB_DOWN) {
        if (s->sel[pane] < n - 1) { s->sel[pane]++; w->dirty = 1; return 1; }
        return 1;
    }
    if (k == (int)KB_PGUP) {
        s->sel[pane] -= 10; if (s->sel[pane] < 0) s->sel[pane] = 0;
        w->dirty = 1; return 1;
    }
    if (k == (int)KB_PGDN) {
        s->sel[pane] += 10; if (s->sel[pane] >= n) s->sel[pane] = n - 1;
        w->dirty = 1; return 1;
    }
    if (k == '\n' || k == '\r') {
        files_pane_activate(w, pane);
        return 1;
    }
    if (k == (int)KB_F3) {
        s->preview ^= 1;
        w->dirty = 1;
        return 1;
    }
    if (k == 's' || k == 'S') {
        s->sort_mode = (s->sort_mode + 1) % 3;
        w->dirty = 1; return 1;
    }
    /* F4 or / - Search for file */
    if (k == (int)KB_F4 || k == '/') {
        char pattern[FS_NAME_MAX];
        if (files_prompt(w, "Search files:", NULL, pattern, sizeof pattern) && pattern[0]) {
            /* Search through all entries for a match */
            int found = 0;
            for (int i = 0; i < n; i++) {
                /* Case-insensitive substring search */
                const char *name = ents[i].name;
                const char *p = pattern;
                int match = 1;
                while (*p && *name) {
                    char c1 = *name >= 'A' && *name <= 'Z' ? *name + 32 : *name;
                    char c2 = *p >= 'A' && *p <= 'Z' ? *p + 32 : *p;
                    if (c1 != c2) { match = 0; break; }
                    name++; p++;
                }
                if (match && *p == '\0') {
                    s->sel[pane] = i;
                    /* Ensure selection is visible */
                    if (i < s->top[pane]) s->top[pane] = i;
                    if (i >= s->top[pane] + 15) s->top[pane] = i - 14;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                /* Show "not found" message briefly */
                extern void notify(const char *, const char *, int);
                notify("Files", "Pattern not found", 1);
            }
        }
        w->dirty = 1; return 1;
    }
    /* H - Toggle hex view in preview pane */
    if (k == 'h' || k == 'H') {
        if (s->preview && s->sel[pane] < n && !(ents[s->sel[pane]].attr & FS_ATTR_DIR)) {
            /* Toggle hex/text preview mode */
            extern void notify(const char *, const char *, int);
            notify("Files", "Hex view toggle (preview)", 0);
        }
        w->dirty = 1; return 1;
    }
    if (k == (int)KB_F2 && s->sel[pane] < n) {
        struct fs_dirent *e = &ents[s->sel[pane]];
        if (strcmp(e->name, "..") == 0) return 1;
        char newname[FS_NAME_MAX];
        if (files_prompt(w, "Rename to:", e->name, newname, sizeof newname) &&
            newname[0]) {
            if (files_set_global_cwd(s->path[pane]) == 0) {
                fs_rename(e->name, newname);
            }
        }
        w->dirty = 1; return 1;
    }
    if (k == (int)KB_DEL && s->sel[pane] < n) {
        struct fs_dirent *e = &ents[s->sel[pane]];
        if (strcmp(e->name, "..") == 0) return 1;
        char msg[80];
        ksnprintf(msg, sizeof msg, " Delete %s? (Y/N) ", e->name);
        if (files_confirm(w, msg)) {
            if (files_set_global_cwd(s->path[pane]) == 0) {
                if (e->attr & FS_ATTR_DIR) fs_rmdir(e->name);
                else                       fs_unlink(e->name);
            }
        }
        w->dirty = 1; return 1;
    }
    if (k == (int)KB_F7) {
        char name[FS_NAME_MAX];
        if (files_prompt(w, "New folder name:", NULL, name, sizeof name) &&
            name[0]) {
            if (files_set_global_cwd(s->path[pane]) == 0) fs_mkdir(name);
        }
        w->dirty = 1; return 1;
    }
    if ((k == (int)KB_F5 || k == (int)KB_F6) && s->sel[pane] < n && !s->preview) {
        struct fs_dirent *e = &ents[s->sel[pane]];
        if (strcmp(e->name, "..") == 0) return 1;
        int other = pane ^ 1;
        char msg[80];
        ksnprintf(msg, sizeof msg, " %s %s to other pane? (Y/N) ",
                  k == (int)KB_F5 ? "Copy" : "Move", e->name);
        if (!files_confirm(w, msg)) { w->dirty = 1; return 1; }
        char src[FS_PATH_MAX], dst[FS_PATH_MAX];
        files_full_path(s, pane,  e->name, src, FS_PATH_MAX);
        files_full_path(s, other, e->name, dst, FS_PATH_MAX);
        if (e->attr & FS_ATTR_DIR) {
            /* Recursive directory copy */
            if (files_set_global_cwd(s->path[pane]) == 0) {
                /* Create directory in destination */
                if (files_set_global_cwd(s->path[other]) == 0) {
                    fs_mkdir(e->name);
                }
                /* Copy all files in directory */
                struct fs_dirent sub_ents[64];
                int sub_n = files_read_path(e->name, sub_ents, 64, 0);
                for (int i = 0; i < sub_n; i++) {
                    if (strcmp(sub_ents[i].name, "..") == 0) continue;
                    char sub_src[FS_PATH_MAX], sub_dst[FS_PATH_MAX];
                    ksnprintf(sub_src, sizeof sub_src, "%s\\%s", e->name, sub_ents[i].name);
                    ksnprintf(sub_dst, sizeof sub_dst, "%s\\%s", e->name, sub_ents[i].name);
                    /* Copy file */
                    int sh = fs_open(sub_src);
                    if (sh >= 0) {
                        if (files_set_global_cwd(s->path[other]) == 0) {
                            fs_unlink(sub_ents[i].name);
                            fs_create(sub_ents[i].name);
                            int dh = fs_open(sub_ents[i].name);
                            if (dh >= 0) {
                                char buf[256];
                                int got;
                                while ((got = fs_read(sh, buf, sizeof buf)) > 0) {
                                    if (fs_write(dh, buf, (size_t)got) != got) break;
                                }
                                fs_close(dh);
                            }
                        }
                        fs_close(sh);
                    }
                }
            }
        } else {
            /* Single file copy */
            int sh = fs_open(src);
            if (sh >= 0) {
                fs_unlink(dst);
                fs_create(dst);
                int dh = fs_open(dst);
                if (dh >= 0) {
                    char buf[256];
                    int got;
                    while ((got = fs_read(sh, buf, sizeof buf)) > 0) {
                        if (fs_write(dh, buf, (size_t)got) != got) break;
                    }
                    fs_close(dh);
                }
                fs_close(sh);
                if (k == (int)KB_F6) {
                    if (files_set_global_cwd(s->path[pane]) == 0) fs_unlink(e->name);
                }
            }
        }
        w->dirty = 1; return 1;
    }
    if (k == 8 /* backspace */) {
        files_path_join(s->path[pane], FS_PATH_MAX, "..");
        s->sel[pane] = 0; s->top[pane] = 0;
        w->dirty = 1; return 1;
    }
    return 0;
}

static void render_settings(struct widget *w) {
    static const char *tab_label[6] = {
        "Background", "Theme", "Date/Time", "Network", "Autostart", "Security"
    };
    draw_window(w->r, w->c, w->w, w->h, w->title);
    vga_write(w->r + w->h - 2, w->c + 2,
              (w->tab == 4 || w->tab == 5)
                ? "[ TAB pane | UP/DOWN row | SPACE toggle ]"
                : "[ TAB pane | LEFT/RIGHT value | UP/DOWN row ]",
              8, win_bg);
    int x = w->c + 2;
    for (int i = 0; i < 6; i++) {
        int len = (int)strlen(tab_label[i]) + 2;
        u8 fg = (i == w->tab) ? win_bg  : win_fg;
        u8 bg = (i == w->tab) ? win_fg  : win_bg;
        vga_fill (w->r + 1, x, len, 1, ' ', fg, bg);
        vga_write(w->r + 1, x + 1, tab_label[i], fg, bg);
        x += len + 1;
    }
    /* Pane body. */
    for (int rr = w->r + 3; rr < w->r + w->h - 2; rr++)
        vga_fill(rr, w->c + 2, w->w - 4, 1, ' ', win_fg, win_bg);
    char line[80];
    if (w->tab == 0) {
        ksnprintf(line, sizeof line, "%-20s  <  %-22s  >",
                  "Pattern", wallpaper_label[wallpaper_style]);
        vga_write(w->r + 3, w->c + 2, line,
                  (w->row == 0 && w->focused) ? win_bg : win_fg,
                  (w->row == 0 && w->focused) ? win_fg : win_bg);
        ksnprintf(line, sizeof line, "%-20s  <  %-22s  >",
                  "Background colour", bg_color_label[current_bg_color]);
        vga_write(w->r + 5, w->c + 2, line,
                  (w->row == 1 && w->focused) ? win_bg : win_fg,
                  (w->row == 1 && w->focused) ? win_fg : win_bg);
        ksnprintf(line, sizeof line, "%-20s  <  %-22s  >",
                  "Mouse cursor", cursor_style_label[cursor_style]);
        vga_write(w->r + 7, w->c + 2, line,
                  (w->row == 2 && w->focused) ? win_bg : win_fg,
                  (w->row == 2 && w->focused) ? win_fg : win_bg);
        /* Resolution row. Cycles 80x25 text -> 80x50 text -> 1280x720
         * graphics (Bochs VBE / BGA). The first two use the VGA hardware
         * character generator; 1280x720 routes shadow-buf cells through
         * an 8x16 bitmap renderer into a linear framebuffer. */
        char res_val[32];
        if (vga_in_graphics_mode())
            ksnprintf(res_val, sizeof res_val, "1280x720 (VBE) @ 60");
        else
            ksnprintf(res_val, sizeof res_val, "80x%d text @ 70 Hz",
                      vga_get_rows());
        ksnprintf(line, sizeof line, "%-20s  <  %-22s  >",
                  "Resolution", res_val);
        vga_write(w->r + 9, w->c + 2, line,
                  (w->row == 3 && w->focused) ? win_bg : win_fg,
                  (w->row == 3 && w->focused) ? win_fg : win_bg);
    } else if (w->tab == 1) {
        ksnprintf(line, sizeof line, "%-20s  <  %-22s  >",
                  "Theme", theme_label[current_theme]);
        vga_write(w->r + 3, w->c + 2, line, win_fg, win_bg);
    } else if (w->tab == 2) {
        struct rtc_time t; rtc_read(&t);
        ksnprintf(line, sizeof line, "Now: %04u-%02u-%02u %02u:%02u:%02u",
                  (u32)t.year, (u32)t.month, (u32)t.day,
                  (u32)t.hour, (u32)t.min, (u32)t.sec);
        vga_write(w->r + 3, w->c + 2, line, win_fg, win_bg);
        vga_write(w->r + 5, w->c + 2,
                  "Press 'S' to set (YYYY-MM-DD HH:MM:SS).", 8, win_bg);
    } else if (w->tab == 3) {
        extern ip4_addr_t dns_server;
        struct net_iface *n = net_iface();
        char ip[16] = "?", gw[16] = "?", ns[16] = "?";
        if (n) { ip4_format(n->ip, ip); ip4_format(n->gateway, gw); }
        ip4_format(dns_server, ns);
        ksnprintf(line, sizeof line, "IP:     %s", ip);
        vga_write(w->r + 3, w->c + 2, line, win_fg, win_bg);
        ksnprintf(line, sizeof line, "Gate:   %s", gw);
        vga_write(w->r + 4, w->c + 2, line, win_fg, win_bg);
        ksnprintf(line, sizeof line, "DNS:    %s", ns);
        vga_write(w->r + 5, w->c + 2, line, win_fg, win_bg);
        vga_write(w->r + 7, w->c + 2,
                  "Press 'D' to change DNS server.", 8, win_bg);
    } else if (w->tab == 4) {
        /* Autostart pane. Row 0 is the global "launch desktop at boot"
         * toggle (kmain reads this before falling through to the
         * shell); rows 1..APP_COUNT are individual apps spawned right
         * after Welcome when the desktop opens. */
        vga_write(w->r + 3, w->c + 2,
                  "Boot + auto-launch:", 8, win_bg);
        int rows = w->h - 6;
        int total_rows = APP_COUNT + 1;
        int top = w->row >= rows ? w->row - rows + 1 : 0;
        for (int i = 0; i < rows; i++) {
            int idx = top + i;
            int row_y = w->r + 4 + i;
            vga_fill(row_y, w->c + 2, w->w - 4, 1, ' ', win_fg, win_bg);
            if (idx >= total_rows) continue;
            const char *lbl;
            int checked;
            if (idx == 0) {
                lbl = "Desktop  (start at boot instead of shell)";
                checked = autostart_desktop;
            } else {
                lbl = app_defs[idx - 1].label;
                if (!lbl) continue;
                checked = autostart_apps[idx - 1];
            }
            ksnprintf(line, sizeof line, " [%c] %s",
                      checked ? 'x' : ' ', lbl);
            u8 fg = win_fg, bg = win_bg;
            if (idx == w->row && w->focused) { fg = win_bg; bg = win_fg; }
            int lnln = (int)strlen(line);
            for (int xc = 0; xc < w->w - 4; xc++)
                vga_put_cell(row_y, w->c + 2 + xc,
                             xc < lnln ? line[xc] : ' ', fg, bg);
        }
    } else {
        /* Security pane. Five toggles that affect day-to-day
         * behaviour. Settings persist in CONFIG.TXT through the
         * generic security_* accessors. */
        extern int  security_get(int idx);
        extern void security_set(int idx, int v);
        extern const char *security_label(int idx);
        extern int  security_count(void);
        vga_write(w->r + 3, w->c + 2,
                  "Security + behaviour:", 8, win_bg);
        int n = security_count();
        for (int i = 0; i < n; i++) {
            int row_y = w->r + 4 + i;
            vga_fill(row_y, w->c + 2, w->w - 4, 1, ' ', win_fg, win_bg);
            ksnprintf(line, sizeof line, " [%c] %s",
                      security_get(i) ? 'x' : ' ',
                      security_label(i));
            u8 fg = win_fg, bg = win_bg;
            if (i == w->row && w->focused) { fg = win_bg; bg = win_fg; }
            int lnln = (int)strlen(line);
            for (int xc = 0; xc < w->w - 4; xc++)
                vga_put_cell(row_y, w->c + 2 + xc,
                             xc < lnln ? line[xc] : ' ', fg, bg);
        }
        vga_write(w->r + 4 + n + 1, w->c + 2,
                  "(persisted in \\SYSTEM\\CONFIG.TXT)",
                  8, win_bg);
    }
}

static void render_terminal(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    vga_write(w->r + 1, w->c + 2,
              w->focused ? "$ commands ENTER to run, ESC unfocus"
                         : "(click to focus)",
              8, win_bg);
    /* Body shows the tail of the scrollback buffer wrapped to widget
     * width. Newest line is just above the prompt. */
    int rows = w->h - 4, cols = w->w - 4;
    int row0 = w->r + 2, col0 = w->c + 2;
    for (int rr = 0; rr < rows; rr++)
        vga_fill(row0 + rr, col0, cols, 1, ' ', win_fg, win_bg);
    /* Count rendered lines so we can show the tail. */
    int total = 0;
    {
        int cc = 0;
        for (int i = 0; i < w->content_len; i++) {
            char ch = w->content[i];
            if (ch == '\n' || cc >= cols) { total++; cc = 0; if (ch=='\n') continue; }
            if (ch >= ' ' && (u8)ch < 127) cc++;
        }
        if (cc > 0) total++;
    }
    int skip = total > rows ? total - rows : 0;
    int cur_line = 0, rr = 0, cc = 0;
    for (int i = 0; i < w->content_len && rr < rows; i++) {
        char ch = w->content[i];
        if (ch == '\n' || cc >= cols) {
            if (cur_line >= skip) rr++;
            cur_line++;
            cc = 0;
            if (ch == '\n') continue;
        }
        if (cur_line < skip) continue;
        if (ch >= ' ' && (u8)ch < 127 && rr < rows)
            vga_put_cell(row0 + rr, col0 + cc++, ch, win_fg, win_bg);
    }
    /* Prompt + input line. */
    int prow = w->r + w->h - 2;
    vga_fill(prow, col0, cols, 1, ' ', win_fg, win_bg);
    vga_write(prow, col0, "$ ", 2, win_bg);
    vga_write(prow, col0 + 2, w->input, win_fg, win_bg);
    if (w->focused)
        vga_put_cell(prow, col0 + 2 + w->input_len, '_', win_fg, win_bg);
}

/* Tiny HTML -> text renderer, shared by the legacy fullscreen Web
 * view and the desktop Web widget.  Strips tags, skips
 * <script>/<style> bodies entirely, decodes a small entity set,
 * inserts newlines around block-level tags, and collapses
 * whitespace runs.  Returns the number of bytes written to `out`
 * (NUL-terminated). */
static int html_render(const char *body, int len, char *out, int outmax) {
    int tl = 0;
    int in_tag = 0, in_script = 0, in_style = 0;
    int last_space = 1;
    for (int i = 0; i < len && tl < outmax - 1; i++) {
        char ch = body[i];
        if (in_script || in_style) {
            const char *end = in_script ? "</script" : "</style";
            int el = (int)strlen(end);
            if (i + el < len) {
                int ok = 1;
                for (int j = 0; j < el; j++) {
                    char a = body[i + j], b = end[j];
                    if (a >= 'A' && a <= 'Z') a += 32;
                    if (a != b) { ok = 0; break; }
                }
                if (ok) { in_script = in_style = 0; i += el - 1; in_tag = 1; }
            }
            continue;
        }
        if (in_tag) { if (ch == '>') in_tag = 0; continue; }
        if (ch == '<') {
            /* Identify the tag (lowercase, alpha only) so we can
             * insert newlines for block-level elements. */
            int j = i + 1;
            while (j < len && body[j] == ' ') j++;
            int closing = (j < len && body[j] == '/');
            if (closing) j++;
            char tag[12]; int tl2 = 0;
            while (j < len && tl2 < (int)sizeof tag - 1 &&
                   ((body[j] >= 'a' && body[j] <= 'z') ||
                    (body[j] >= 'A' && body[j] <= 'Z') ||
                    (body[j] >= '0' && body[j] <= '9'))) {
                char ck = body[j++];
                if (ck >= 'A' && ck <= 'Z') ck += 32;
                tag[tl2++] = ck;
            }
            tag[tl2] = '\0';
            if (strcmp(tag, "script") == 0 && !closing) in_script = 1;
            else if (strcmp(tag, "style") == 0 && !closing) in_style = 1;
            else if (strcmp(tag, "br") == 0 || strcmp(tag, "p") == 0 ||
                     strcmp(tag, "div") == 0 || strcmp(tag, "li") == 0 ||
                     strcmp(tag, "tr") == 0  || strcmp(tag, "h1") == 0 ||
                     strcmp(tag, "h2") == 0 || strcmp(tag, "h3") == 0 ||
                     strcmp(tag, "h4") == 0 || strcmp(tag, "ul") == 0 ||
                     strcmp(tag, "ol") == 0 || strcmp(tag, "hr") == 0 ||
                     strcmp(tag, "title") == 0 || strcmp(tag, "header") == 0 ||
                     strcmp(tag, "section") == 0 || strcmp(tag, "article") == 0 ||
                     strcmp(tag, "nav") == 0 || strcmp(tag, "footer") == 0 ||
                     strcmp(tag, "main") == 0 || strcmp(tag, "blockquote") == 0) {
                if (!last_space) { out[tl++] = '\n'; last_space = 1; }
            }
            in_tag = 1;
            continue;
        }
        if (ch == '&') {
            static const struct { const char *name; char ch; } ents[] = {
                {"amp;",  '&'}, {"lt;",   '<'}, {"gt;",   '>'},
                {"quot;", '"'}, {"apos;", '\''}, {"nbsp;", ' '},
                {"mdash;",'-'}, {"ndash;",'-'}, {"hellip;",'.'},
                {"copy;", 'c'}, {"reg;",  'r'},
            };
            int matched = 0;
            for (size_t e = 0; e < sizeof ents / sizeof ents[0]; e++) {
                int el = (int)strlen(ents[e].name);
                if (i + 1 + el <= len &&
                    strncmp(body + i + 1, ents[e].name, (size_t)el) == 0) {
                    out[tl++] = ents[e].ch;
                    i += el;
                    matched = 1;
                    last_space = (ents[e].ch == ' ');
                    break;
                }
            }
            if (matched) continue;
            /* Numeric entity &#NN; / &#xNN; -- best effort to ASCII. */
            if (i + 2 < len && body[i + 1] == '#') {
                int j = i + 2, base = 10, val = 0;
                if (j < len && (body[j] == 'x' || body[j] == 'X')) { base = 16; j++; }
                while (j < len && body[j] != ';') {
                    char c = body[j++];
                    int dig = (c >= '0' && c <= '9') ? c - '0' :
                              (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                              (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                    if (dig < 0 || dig >= base) { val = -1; break; }
                    val = val * base + dig;
                }
                if (val >= 0x20 && val < 0x7F) {
                    out[tl++] = (char)val;
                    last_space = 0;
                }
                i = j;
                continue;
            }
            /* Unknown entity: skip up to ';'. */
            while (i < len && body[i] != ';' && body[i] != ' ') i++;
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
            if (!last_space) { out[tl++] = ' '; last_space = 1; }
            continue;
        }
        if (ch < ' ' || (u8)ch > 126) continue;
        out[tl++] = ch;
        last_space = 0;
    }
    out[tl] = '\0';
    return tl;
}

static void render_web(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    int cols = w->w - 4;
    /* URL bar. */
    vga_write(w->r + 1, w->c + 2, "URL/q:", 8, win_bg);
    vga_fill (w->r + 1, w->c + 9, cols - 9, 1, ' ', win_fg, win_bg);
    vga_write(w->r + 1, w->c + 9, w->input, win_fg, win_bg);
    if (w->focused)
        vga_put_cell(w->r + 1, w->c + 9 + w->input_len, '_', win_fg, win_bg);
    /* Render body window-pane: wrap content, skip rows < w->top, draw
     * the next `rows` rows. Total rows is computed so we can clamp
     * scrolling + show "row N/M" in the status. */
    int rows = w->h - 4, col0 = w->c + 2;
    for (int rr = 0; rr < rows; rr++)
        vga_fill(w->r + 3 + rr, col0, cols, 1, ' ', win_fg, win_bg);
    int total_rows = 0, vis_row = 0, cc = 0;
    for (int i = 0; i < w->content_len; i++) {
        char ch = w->content[i];
        if (ch == '\n' || cc >= cols) {
            if (total_rows >= w->top && vis_row < rows) vis_row++;
            total_rows++;
            cc = 0;
            if (ch == '\n') continue;
        }
        if (ch < ' ' || (u8)ch > 126) continue;
        if (total_rows >= w->top && (vis_row < rows)) {
            vga_put_cell(w->r + 3 + vis_row, col0 + cc, ch, win_fg, win_bg);
        }
        cc++;
    }
    if (cc > 0) total_rows++;
    /* Status line: HTTP code + body bytes + scroll position. */
    char st[80];
    int status = http_last_status();
    const char *codetext =
        status == 0   ? "ready" :
        status == 200 ? "OK"    :
        status == 301 ? "moved" :
        status == 302 ? "found" :
        status == 404 ? "not found" :
        status == 503 ? "unavailable" :
        status  < 0   ? "transport err" : "?";
    ksnprintf(st, sizeof st,
              "[ HTTP %d %s | %d B | rows %d-%d/%d | PgUp/PgDn ]",
              status, codetext, w->content_len,
              w->top + 1,
              w->top + rows < total_rows ? w->top + rows : total_rows,
              total_rows);
    vga_write(w->r + w->h - 2, col0, st, 8, win_bg);
}

/* --- Disk Manager: list every populated slot in disk_get() ---------- */
extern struct disk *disk_get(int id);
static int diskmgr_sel = 0;  /* selected disk slot */

static int diskmgr_handle_key(struct widget *w, int k) {
    /* Count present disks for selection bounds */
    int count = 0;
    for (int id = 0; id < DISK_MAX; id++) {
        struct disk *d = disk_get(id);
        if (d && d->present) count++;
    }
    if (count == 0) return 0;

    if (k == (int)KB_UP && diskmgr_sel > 0) diskmgr_sel--;
    else if (k == (int)KB_DOWN && diskmgr_sel < count - 1) diskmgr_sel++;
    else if (k == (int)KB_F5) {
        extern int fs_rescan(void);
        fs_rescan();
        vga_invalidate();
        diskmgr_sel = 0;
    }
    else if (k == 'm' || k == 'M') {
        /* Mount/unmount the selected disk */
        int slot = -1, idx = 0;
        for (int id = 0; id < DISK_MAX; id++) {
            struct disk *d = disk_get(id);
            if (d && d->present) {
                if (idx == diskmgr_sel) { slot = id; break; }
                idx++;
            }
        }
        if (slot >= 0) {
            /* Check if already mounted */
            char letter = '-';
            for (char L = 'A'; L <= 'Z'; L++) {
                extern int fs_drive_disk_id(char drive);
                if (fs_drive_disk_id(L) == slot) { letter = L; break; }
            }
            if (letter != '-') {
                /* Unmount */
                extern int fs_unmount(char drive);
                fs_unmount(letter);
                char msg[40];
                ksnprintf(msg, sizeof msg, "Unmounted drive %c:", letter);
                extern void notify(const char *, const char *, int);
                notify("Disk Manager", msg, 0);
            } else {
                /* Find next free drive letter */
                for (char L = 'A'; L <= 'F'; L++) {
                    extern int fs_drive_disk_id(char drive);
                    if (fs_drive_disk_id(L) < 0) {
                        extern int fs_mount(char drive, int disk_id);
                        if (fs_mount(L, slot) == 0) {
                            char msg[40];
                            ksnprintf(msg, sizeof msg, "Mounted as %c:", L);
                            extern void notify(const char *, const char *, int);
                            notify("Disk Manager", msg, 0);
                        } else {
                            extern void notify(const char *, const char *, int);
                            notify("Disk Manager", "Mount failed", 1);
                        }
                        break;
                    }
                }
            }
        }
    }
    else if (k == 'f' || k == 'F') {
        /* Format the selected disk (with confirmation) */
        int slot = -1, idx = 0;
        for (int id = 0; id < DISK_MAX; id++) {
            struct disk *d = disk_get(id);
            if (d && d->present) {
                if (idx == diskmgr_sel) { slot = id; break; }
                idx++;
            }
        }
        if (slot >= 0) {
            /* Unmount first if mounted */
            char letter = '-';
            for (char L = 'A'; L <= 'Z'; L++) {
                extern int fs_drive_disk_id(char drive);
                if (fs_drive_disk_id(L) == slot) { letter = L; break; }
            }
            if (letter != '-') {
                extern int fs_unmount(char drive);
                fs_unmount(letter);
            }
            /* Format */
            extern int fs_format(int disk_id, const char *label);
            if (fs_format(slot, "ZENBITE") == 0) {
                extern void notify(const char *, const char *, int);
                notify("Disk Manager", "Format complete", 0);
                /* Remount if it was mounted */
                if (letter != '-') {
                    extern int fs_mount(char drive, int disk_id);
                    fs_mount(letter, slot);
                }
            } else {
                extern void notify(const char *, const char *, int);
                notify("Disk Manager", "Format failed", 1);
            }
        }
    }
    else if (k == 'i' || k == 'I') {
        /* Show disk info */
        int slot = -1, idx = 0;
        for (int id = 0; id < DISK_MAX; id++) {
            struct disk *d = disk_get(id);
            if (d && d->present) {
                if (idx == diskmgr_sel) { slot = id; break; }
                idx++;
            }
        }
        if (slot >= 0) {
            struct disk *d = disk_get(slot);
            char msg[80];
            ksnprintf(msg, sizeof msg, "%s: %u MiB, %u sectors",
                      d->name, (u32)(d->sectors / 2048), (u32)d->sectors);
            extern void notify(const char *, const char *, int);
            notify("Disk Info", msg, 0);
        }
    }
    else return 0;
    return 1;
}

static void render_diskmgr(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    vga_write(w->r + 1, w->c + 2,
              "Slot  Name      Kind     Size       Mounted",
              win_bg, win_fg);
    int row = w->r + 3, col = w->c + 2;
    int shown = 0, idx = 0;
    for (int id = 0; id < DISK_MAX; id++) {
        struct disk *d = disk_get(id);
        if (!d || !d->present) continue;
        const char *kind =
            id < 4  ? "ATA"   :
            id < 8  ? "AHCI"  :
            id < 10 ? "Flop"  :
            id < 12 ? "USB1"  : "USB2";
        u32 mib = d->sectors / 2048;
        /* Reverse-lookup mount letter via fs_drive_disk_id. */
        char letter = '-';
        for (char L = 'A'; L <= 'Z'; L++) {
            extern int fs_drive_disk_id(char drive);
            if (fs_drive_disk_id(L) == id) { letter = L; break; }
        }
        char line[80];
        ksnprintf(line, sizeof line,
                  "%s %2d  %-8s  %-7s  %5u MiB  %c%c",
                  (idx == diskmgr_sel) ? ">" : " ",
                  id, d->name, kind, mib,
                  letter, letter == '-' ? ' ' : ':');
        u8 fg = win_fg, bg = win_bg;
        if (idx == diskmgr_sel) { fg = 15; bg = 1; }  /* highlight selected */
        vga_write(row + shown, col, line, fg, bg);
        shown++;
        idx++;
        if (shown >= w->h - 6) break;
    }
    if (!shown) {
        vga_write(row, col, "No disks present.", 8, win_bg);
    }
    char st[80];
    ksnprintf(st, sizeof st, "[ %d disk%s | F5 rescan | M mount | F format | I info ]",
              shown, shown == 1 ? "" : "s");
    vga_write(w->r + w->h - 2, col, st, 8, win_bg);
}

/* --- Calendar: month view --------------------------------------------- */
static int month_days(int year, int month) {
    static const u8 days[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int d = days[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        d = 29;
    return d;
}
/* Zeller's congruence -- day of week for the 1st of given month. */
static int dow_first(int year, int month) {
    int q = 1;
    int m = month;
    if (m < 3) { m += 12; year--; }
    int K = year % 100, J = year / 100;
    int h = (q + (13*(m+1))/5 + K + K/4 + J/4 + 5*J) % 7;
    /* Zeller: 0=Saturday, 1=Sunday, ...; convert to 0=Mon..6=Sun. */
    static const int conv[7] = { 5, 6, 0, 1, 2, 3, 4 };
    return conv[h];
}
static void render_calendar(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    struct rtc_time t; rtc_read(&t);
    int year = t.year ? t.year : 2026;
    int month = t.month ? t.month : 1;
    int today = t.day;
    /* w->tab is signed offset in months from the current real month. */
    int off = w->tab;
    int ym = year * 12 + (month - 1) + off;
    int cy = ym / 12; int cm = ym % 12 + 1;
    int cur_real = (cy == (int)t.year && cm == (int)t.month);

    static const char *mn[12] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    char head[40];
    ksnprintf(head, sizeof head, "%s %d", mn[cm - 1], cy);
    int hx = w->c + (w->w - (int)strlen(head)) / 2;
    vga_write(w->r + 1, hx, head, win_bg, win_fg);
    vga_write(w->r + 3, w->c + 4, "Mo Tu We Th Fr Sa Su", 8, win_bg);
    int days = month_days(cy, cm);
    int start = dow_first(cy, cm);
    int row = w->r + 4, col0 = w->c + 4;
    int r2 = 0, c2 = start;
    for (int d = 1; d <= days; d++) {
        char num[3];
        ksnprintf(num, sizeof num, "%2d", d);
        u8 fg = win_fg, bg = win_bg;
        if (cur_real && d == today) { fg = win_bg; bg = 14; } /* highlight today */
        vga_write(row + r2, col0 + c2 * 3, num, fg, bg);
        c2++;
        if (c2 >= 7) { c2 = 0; r2++; }
    }
    vga_write(w->r + w->h - 2, w->c + 2,
              w->focused
                ? "[ LEFT/RIGHT month  HOME today  ESC unfocus ]"
                : "[ click to focus ]",
              8, win_bg);
}

/* --- Snake game ------------------------------------------------------- *
 * Body stored in w->content as packed (col, row) bytes. Head at index 0.
 * w->sel = score, w->top = direction (0=R,1=D,2=L,3=U),
 * w->row = state (0 playing, 1 game over). prev_sec is the wall-clock
 * second used to throttle movement (one cell per second is too slow --
 * we tick on every render call when focused, gated by mouse jitter,
 * roughly 5 cells/sec). */
static u32 rng_state = 0xc0ffee01;
static u32 quick_rand(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}
static void snake_reset(struct widget *w, int play_cols, int play_rows) {
    w->sel = 0; w->top = 0; w->row = 0;
    w->content_len = 0;
    int cx = play_cols / 2, cy = play_rows / 2;
    /* Three-segment starting snake heading right. */
    for (int i = 0; i < 3; i++) {
        w->content[w->content_len++] = (char)(cx - i);
        w->content[w->content_len++] = (char)cy;
    }
    /* Apple at (col, row) packed in last two unused bytes; we keep
     * apple at end of buffer for simplicity. */
    u8 ax = (u8)(quick_rand() % play_cols);
    u8 ay = (u8)(quick_rand() % play_rows);
    w->content[(int)sizeof w->content - 2] = (char)ax;
    w->content[(int)sizeof w->content - 1] = (char)ay;
}
extern u32 pit_ticks(void);
static void render_snake(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    int play_cols = w->w - 4;
    int play_rows = w->h - 5;
    int origin_r = w->r + 2, origin_c = w->c + 2;
    /* Initialize on first render. */
    if (w->content_len == 0) snake_reset(w, play_cols, play_rows);
    /* Throttle to ~7 ticks/sec using pit_ticks (100 Hz). prev_sec is
     * reused here as "last tick we moved on". */
    u32 now = pit_ticks();
    int should_tick = w->row == 0 && w->focused &&
                      (u32)(now - (u32)w->prev_sec) >= 14;
    if (should_tick) w->prev_sec = (int)now;
    if (should_tick) {
        int n = w->content_len / 2;
        int hx = (u8)w->content[0];
        int hy = (u8)w->content[1];
        int dx = (w->top == 0) - (w->top == 2);
        int dy = (w->top == 1) - (w->top == 3);
        int nx = hx + dx, ny = hy + dy;
        /* Walls. */
        if (nx < 0 || nx >= play_cols || ny < 0 || ny >= play_rows) {
            w->row = 1;
        } else {
            /* Self-collision. */
            for (int i = 0; i < n; i++) {
                if ((u8)w->content[i*2] == nx && (u8)w->content[i*2+1] == ny) {
                    w->row = 1; break;
                }
            }
        }
        if (w->row == 0) {
            int ax = (u8)w->content[(int)sizeof w->content - 2];
            int ay = (u8)w->content[(int)sizeof w->content - 1];
            int grow = (nx == ax && ny == ay);
            /* Shift body right by 2 bytes (one cell). */
            for (int i = w->content_len; i > 0; i--)
                w->content[i] = w->content[i - 1];
            w->content[0] = (char)nx; w->content[1] = (char)ny;
            w->content_len += 2;
            if (!grow) {
                w->content_len -= 2;
            } else {
                w->sel++;
                /* Move apple, retry if it lands on snake. */
                int tries = 0;
                do {
                    ax = (int)(quick_rand() % play_cols);
                    ay = (int)(quick_rand() % play_rows);
                    int hit = 0;
                    for (int i = 0; i < w->content_len / 2; i++)
                        if ((u8)w->content[i*2] == ax &&
                            (u8)w->content[i*2+1] == ay) { hit = 1; break; }
                    if (!hit) break;
                } while (++tries < 50);
                w->content[(int)sizeof w->content - 2] = (char)ax;
                w->content[(int)sizeof w->content - 1] = (char)ay;
            }
        }
    }
    /* Draw playfield. */
    for (int r = 0; r < play_rows; r++)
        vga_fill(origin_r + r, origin_c, play_cols, 1, ' ', win_fg, win_bg);
    /* Apple. */
    int ax = (u8)w->content[(int)sizeof w->content - 2];
    int ay = (u8)w->content[(int)sizeof w->content - 1];
    if (ax < play_cols && ay < play_rows)
        vga_put_cell(origin_r + ay, origin_c + ax, 0x04, 4, win_bg); /* red diamond */
    /* Snake. */
    int n = w->content_len / 2;
    for (int i = 0; i < n; i++) {
        int x = (u8)w->content[i*2];
        int y = (u8)w->content[i*2+1];
        if (x < play_cols && y < play_rows)
            vga_put_cell(origin_r + y, origin_c + x, 0xDB,
                         i == 0 ? 10 : 2, win_bg);   /* head bright green */
    }
    char st[60];
    if (w->row == 1)
        ksnprintf(st, sizeof st, "[ GAME OVER -- score %d -- press R to restart ]", w->sel);
    else
        ksnprintf(st, sizeof st, "[ score %d | arrows to steer | focus = play ]", w->sel);
    vga_write(w->r + w->h - 2, w->c + 2, st, 8, win_bg);
}

/* --- Tetris ----------------------------------------------------------- */
/* Board is 10 wide x 20 tall. Each cell holds a piece colour (1..7) or 0
 * for empty. We store the board + piece state in a struct laid over
 * w->content so the existing widget pipeline can persist state across
 * frames. The piece tetromino tables live below; rotations are computed
 * on the fly so we don't need to encode all four orientations per piece. */
#define TET_W 10
#define TET_H 20
struct tet_state {
    u8  board[TET_H * TET_W];
    i8  piece;
    i8  rot;
    i8  pr, pc;
    i8  next;
    u8  level;
    u32 score;
    u32 lines;
    u32 last_drop;
};

/* 7 tetrominoes, each as 4 (r,c) blocks in its base orientation. */
static const i8 tet_pieces[7][4][2] = {
    /* I */ { {0,-1},{0,0},{0,1},{0,2} },
    /* O */ { {0,0},{0,1},{1,0},{1,1} },
    /* T */ { {0,-1},{0,0},{0,1},{1,0} },
    /* S */ { {0,0},{0,1},{1,-1},{1,0} },
    /* Z */ { {0,-1},{0,0},{1,0},{1,1} },
    /* J */ { {0,-1},{0,0},{0,1},{1,1} },
    /* L */ { {0,-1},{0,0},{0,1},{1,-1} },
};
static const u8 tet_colour[7] = { 11, 14, 5, 10, 4, 1, 6 };  /* I O T S Z J L */

static void tet_block_at(int piece, int rot, int i, int *out_dr, int *out_dc) {
    int dr = tet_pieces[piece][i][0];
    int dc = tet_pieces[piece][i][1];
    /* Rotate (dr, dc) by rot*90 CW: (r,c) -> (c, -r). */
    for (int n = 0; n < rot; n++) {
        int nr = dc, nc = -dr;
        dr = nr; dc = nc;
    }
    *out_dr = dr; *out_dc = dc;
}

static int tet_fits(const struct tet_state *s, int piece, int rot, int pr, int pc) {
    for (int i = 0; i < 4; i++) {
        int dr, dc;
        tet_block_at(piece, rot, i, &dr, &dc);
        int r = pr + dr, c = pc + dc;
        if (c < 0 || c >= TET_W || r >= TET_H) return 0;
        if (r >= 0 && s->board[r * TET_W + c]) return 0;
    }
    return 1;
}

static void tet_lock(struct tet_state *s) {
    for (int i = 0; i < 4; i++) {
        int dr, dc;
        tet_block_at(s->piece, s->rot, i, &dr, &dc);
        int r = s->pr + dr, c = s->pc + dc;
        if (r < 0 || r >= TET_H || c < 0 || c >= TET_W) continue;
        s->board[r * TET_W + c] = (u8)(s->piece + 1);
    }
}

static int tet_clear_lines(struct tet_state *s) {
    int cleared = 0;
    for (int r = TET_H - 1; r >= 0; ) {
        int full = 1;
        for (int c = 0; c < TET_W; c++)
            if (!s->board[r * TET_W + c]) { full = 0; break; }
        if (full) {
            cleared++;
            /* Shift everything above down by one. */
            for (int rr = r; rr > 0; rr--)
                for (int c = 0; c < TET_W; c++)
                    s->board[rr * TET_W + c] = s->board[(rr - 1) * TET_W + c];
            for (int c = 0; c < TET_W; c++) s->board[c] = 0;
        } else {
            r--;
        }
    }
    return cleared;
}

static void tet_spawn(struct tet_state *s) {
    s->piece = s->next;
    s->next  = (i8)(quick_rand() % 7);
    s->rot = 0;
    s->pr = 0; s->pc = TET_W / 2;
}

static void tet_reset(struct widget *w) {
    struct tet_state *s = (struct tet_state *)w->content;
    memset(s, 0, sizeof *s);
    s->next  = (i8)(quick_rand() % 7);
    tet_spawn(s);
    s->level = 1;
    s->last_drop = pit_ticks();
    w->row = 0;     /* state: 0=playing 1=over 2=paused */
    w->content_len = (int)sizeof *s;
    w->sel = 0;
    w->top = 0;
}

/* The big input shim: keyboard cases call this from the main loop. */
static int tet_handle_key(struct widget *w, int k) {
    struct tet_state *s = (struct tet_state *)w->content;
    if (w->content_len == 0) tet_reset(w);
    if (w->row == 1) {
        if (k == 'r' || k == 'R') { tet_reset(w); return 1; }
        return 0;
    }
    if (k == 'p' || k == 'P') {
        w->row = w->row == 2 ? 0 : 2;
        s->last_drop = pit_ticks();
        return 1;
    }
    if (w->row == 2) return 0;
    if (k == KB_LEFT  && tet_fits(s, s->piece, s->rot, s->pr, s->pc - 1)) { s->pc--; return 1; }
    if (k == KB_RIGHT && tet_fits(s, s->piece, s->rot, s->pr, s->pc + 1)) { s->pc++; return 1; }
    if (k == KB_DOWN  && tet_fits(s, s->piece, s->rot, s->pr + 1, s->pc)) { s->pr++; s->last_drop = pit_ticks(); return 1; }
    if (k == KB_UP) {
        int nr = (s->rot + 1) & 3;
        /* Try wall kicks: no offset, +1, -1, +2, -2 in column. */
        const int kicks[] = { 0, 1, -1, 2, -2 };
        for (unsigned i = 0; i < sizeof kicks / sizeof kicks[0]; i++) {
            if (tet_fits(s, s->piece, nr, s->pr, s->pc + kicks[i])) {
                s->rot = (i8)nr;
                s->pc = (i8)(s->pc + kicks[i]);
                return 1;
            }
        }
        return 0;
    }
    if (k == ' ') {
        /* Hard drop: fall as far as possible, lock, clear, spawn. */
        while (tet_fits(s, s->piece, s->rot, s->pr + 1, s->pc)) {
            s->pr++; s->score += 2;
        }
        tet_lock(s);
        int cl = tet_clear_lines(s);
        if (cl) {
            static const u32 line_pts[5] = { 0, 100, 300, 500, 800 };
            s->lines += (u32)cl;
            s->score += line_pts[cl] * s->level;
            s->level = 1 + (s->lines / 10);
        }
        tet_spawn(s);
        if (!tet_fits(s, s->piece, s->rot, s->pr, s->pc)) w->row = 1;
        s->last_drop = pit_ticks();
        return 1;
    }
    return 0;
}

static void render_tetris(struct widget *w) {
    struct tet_state *s = (struct tet_state *)w->content;
    draw_window(w->r, w->c, w->w, w->h, w->title);
    if (w->content_len == 0) tet_reset(w);

    /* Gravity tick. Drop interval scales down with level. */
    u32 now = pit_ticks();
    int interval = 60 - (int)s->level * 5;
    if (interval < 8) interval = 8;
    if (w->row == 0 && w->focused && (u32)(now - s->last_drop) >= (u32)interval) {
        s->last_drop = now;
        if (tet_fits(s, s->piece, s->rot, s->pr + 1, s->pc)) {
            s->pr++;
        } else {
            tet_lock(s);
            int cl = tet_clear_lines(s);
            if (cl) {
                static const u32 line_pts[5] = { 0, 100, 300, 500, 800 };
                s->lines += (u32)cl;
                s->score += line_pts[cl] * s->level;
                s->level = 1 + (s->lines / 10);
            }
            tet_spawn(s);
            if (!tet_fits(s, s->piece, s->rot, s->pr, s->pc)) w->row = 1;
        }
    }

    /* Board: 2 cols per cell so square aspect. */
    int board_c = w->c + 2;
    int board_r = w->r + 2;
    for (int r = 0; r < TET_H; r++) {
        for (int c = 0; c < TET_W; c++) {
            u8 v = s->board[r * TET_W + c];
            int cc = board_c + c * 2;
            int rr = board_r + r;
            if (v) {
                vga_put_cell(rr, cc,     0xDB, tet_colour[v - 1], win_bg);
                vga_put_cell(rr, cc + 1, 0xDB, tet_colour[v - 1], win_bg);
            } else {
                vga_put_cell(rr, cc,     '.', 8, win_bg);
                vga_put_cell(rr, cc + 1, ' ', 8, win_bg);
            }
        }
    }
    /* Active piece. */
    if (w->row != 1) {
        for (int i = 0; i < 4; i++) {
            int dr, dc;
            tet_block_at(s->piece, s->rot, i, &dr, &dc);
            int r = s->pr + dr, c = s->pc + dc;
            if (r < 0 || r >= TET_H || c < 0 || c >= TET_W) continue;
            int cc = board_c + c * 2;
            int rr = board_r + r;
            vga_put_cell(rr, cc,     0xDB, tet_colour[s->piece], win_bg);
            vga_put_cell(rr, cc + 1, 0xDB, tet_colour[s->piece], win_bg);
        }
    }

    /* Side panel: score + next preview. */
    int pc = w->c + 24;
    char buf[24];
    ksnprintf(buf, sizeof buf, "Score:  %u", s->score);
    vga_write(w->r + 2, pc, buf, win_fg, win_bg);
    ksnprintf(buf, sizeof buf, "Lines:  %u", s->lines);
    vga_write(w->r + 3, pc, buf, win_fg, win_bg);
    ksnprintf(buf, sizeof buf, "Level:  %u", s->level);
    vga_write(w->r + 4, pc, buf, win_fg, win_bg);
    vga_write(w->r + 6, pc, "Next:", win_fg, win_bg);
    for (int i = 0; i < 4; i++) {
        int dr, dc;
        tet_block_at(s->next, 0, i, &dr, &dc);
        int cc = pc + (dc + 2) * 2;
        int rr = w->r + 8 + dr;
        vga_put_cell(rr, cc,     0xDB, tet_colour[s->next], win_bg);
        vga_put_cell(rr, cc + 1, 0xDB, tet_colour[s->next], win_bg);
    }
    vga_write(w->r + 13, pc, "Left/Right", 8, win_bg);
    vga_write(w->r + 14, pc, "Up = rotate", 8, win_bg);
    vga_write(w->r + 15, pc, "Down = soft", 8, win_bg);
    vga_write(w->r + 16, pc, "Space= drop", 8, win_bg);
    vga_write(w->r + 17, pc, "P=pause R=new", 8, win_bg);

    if (w->row == 1)
        vga_write(w->r + w->h - 2, w->c + 2,
                  "GAME OVER -- R to restart", 4, win_bg);
    else if (w->row == 2)
        vga_write(w->r + w->h - 2, w->c + 2,
                  "PAUSED -- P to resume     ", 14, win_bg);
    else
        vga_write(w->r + w->h - 2, w->c + 2,
                  "[ click to focus + play ]  ", 8, win_bg);
}

/* --- Minesweeper ------------------------------------------------------ */
/* Beginner board: 9x9 grid, 10 mines. Each cell byte encodes:
 *   bit 4    = mine
 *   bit 5    = revealed
 *   bit 6    = flagged
 *   bits 0-3 = neighbour mine count (computed once when the grid is built) */
#define MINE_W 9
#define MINE_H 9
#define MINE_COUNT 10
#define MINE_BIT_MINE   0x10
#define MINE_BIT_OPEN   0x20
#define MINE_BIT_FLAG   0x40

struct mine_state {
    u8 cells[MINE_H * MINE_W];
    u8 generated;     /* mines placed only after the first reveal so 1st click is safe */
    u8 opened;
    u8 flagged;
};

static void mine_reset(struct widget *w) {
    struct mine_state *s = (struct mine_state *)w->content;
    memset(s, 0, sizeof *s);
    w->content_len = (int)sizeof *s;
    w->sel = MINE_H / 2;   /* cursor row */
    w->top = MINE_W / 2;   /* cursor col */
    w->row = 0;            /* state */
}

static void mine_generate(struct mine_state *s, int safe_r, int safe_c) {
    int placed = 0;
    while (placed < MINE_COUNT) {
        int r = (int)(quick_rand() % MINE_H);
        int c = (int)(quick_rand() % MINE_W);
        if (r == safe_r && c == safe_c) continue;
        if (s->cells[r * MINE_W + c] & MINE_BIT_MINE) continue;
        s->cells[r * MINE_W + c] |= MINE_BIT_MINE;
        placed++;
    }
    /* Count neighbours. */
    for (int r = 0; r < MINE_H; r++) {
        for (int c = 0; c < MINE_W; c++) {
            if (s->cells[r * MINE_W + c] & MINE_BIT_MINE) continue;
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    if (!dr && !dc) continue;
                    int rr = r + dr, cc = c + dc;
                    if (rr < 0 || rr >= MINE_H || cc < 0 || cc >= MINE_W) continue;
                    if (s->cells[rr * MINE_W + cc] & MINE_BIT_MINE) n++;
                }
            s->cells[r * MINE_W + c] |= (u8)n;
        }
    }
    s->generated = 1;
}

static void mine_flood(struct mine_state *s, int r, int c) {
    if (r < 0 || r >= MINE_H || c < 0 || c >= MINE_W) return;
    u8 *cell = &s->cells[r * MINE_W + c];
    if (*cell & (MINE_BIT_OPEN | MINE_BIT_FLAG | MINE_BIT_MINE)) return;
    *cell |= MINE_BIT_OPEN;
    s->opened++;
    if ((*cell & 0x0F) != 0) return;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            if (dr || dc) mine_flood(s, r + dr, c + dc);
}

static void mine_check_win(struct widget *w, struct mine_state *s) {
    if (s->opened + MINE_COUNT == MINE_W * MINE_H) w->row = 1; /* won */
}

static void mine_reveal(struct widget *w, struct mine_state *s, int r, int c) {
    if (r < 0 || r >= MINE_H || c < 0 || c >= MINE_W) return;
    u8 *cell = &s->cells[r * MINE_W + c];
    if (*cell & (MINE_BIT_OPEN | MINE_BIT_FLAG)) return;
    if (!s->generated) mine_generate(s, r, c);
    if (*cell & MINE_BIT_MINE) {
        *cell |= MINE_BIT_OPEN;
        w->row = 2; /* lost: reveal everything */
        for (int i = 0; i < MINE_W * MINE_H; i++) s->cells[i] |= MINE_BIT_OPEN;
        return;
    }
    if ((*cell & 0x0F) == 0) mine_flood(s, r, c);
    else { *cell |= MINE_BIT_OPEN; s->opened++; }
    mine_check_win(w, s);
}

static void mine_flag(struct mine_state *s, int r, int c) {
    if (r < 0 || r >= MINE_H || c < 0 || c >= MINE_W) return;
    u8 *cell = &s->cells[r * MINE_W + c];
    if (*cell & MINE_BIT_OPEN) return;
    *cell ^= MINE_BIT_FLAG;
    if (*cell & MINE_BIT_FLAG) s->flagged++;
    else                       s->flagged--;
}

static int mine_handle_key(struct widget *w, int k) {
    struct mine_state *s = (struct mine_state *)w->content;
    if (w->content_len == 0) mine_reset(w);
    if (k == 'r' || k == 'R') { mine_reset(w); return 1; }
    if (w->row != 0) return 0;
    int r = w->sel, c = w->top;
    if (k == KB_UP    && r > 0)            { w->sel--; return 1; }
    if (k == KB_DOWN  && r < MINE_H - 1)   { w->sel++; return 1; }
    if (k == KB_LEFT  && c > 0)            { w->top--; return 1; }
    if (k == KB_RIGHT && c < MINE_W - 1)   { w->top++; return 1; }
    if (k == ' ' || k == '\n' || k == '\r') { mine_reveal(w, s, r, c); return 1; }
    if (k == 'f' || k == 'F') { mine_flag(s, r, c); return 1; }
    return 0;
}

/* Translate a mouse click on the board to (row, col). Returns -1 on miss. */
static int mine_hit(const struct widget *w, int mc, int mr, int *out_r, int *out_c) {
    int board_c = w->c + 4;
    int board_r = w->r + 3;
    int cell_w  = 4;
    int cell_h  = 2;
    int dc = mc - board_c;
    int dr = mr - board_r;
    if (dc < 0 || dr < 0) return -1;
    int cc = dc / cell_w;
    int rr = dr / cell_h;
    if (cc >= MINE_W || rr >= MINE_H) return -1;
    *out_r = rr; *out_c = cc;
    return 0;
}

static void render_mines(struct widget *w) {
    struct mine_state *s = (struct mine_state *)w->content;
    draw_window(w->r, w->c, w->w, w->h, w->title);
    if (w->content_len == 0) mine_reset(w);

    char hdr[40];
    ksnprintf(hdr, sizeof hdr, "Mines: %d   Flags: %d/%d",
              MINE_COUNT, s->flagged, MINE_COUNT);
    vga_write(w->r + 1, w->c + 2, hdr, win_fg, win_bg);

    int board_c = w->c + 4;
    int board_r = w->r + 3;
    int cell_w  = 4;
    int cell_h  = 2;
    for (int r = 0; r < MINE_H; r++) {
        for (int c = 0; c < MINE_W; c++) {
            u8 v = s->cells[r * MINE_W + c];
            int rr = board_r + r * cell_h;
            int cc = board_c + c * cell_w;
            int hl = (r == w->sel && c == w->top && w->focused && w->row == 0);
            u8 bg = hl ? 6 : 7;
            u8 fg = 0;
            char ch = ' ';
            if (v & MINE_BIT_OPEN) {
                bg = 8;
                if (v & MINE_BIT_MINE) { ch = '*'; fg = 4; bg = 4; }
                else if ((v & 0x0F) != 0) {
                    ch = (char)('0' + (v & 0x0F));
                    static const u8 num_fg[9] = { 0, 9, 2, 4, 1, 4, 3, 0, 8 };
                    fg = num_fg[v & 0x0F];
                    bg = 15;
                } else { bg = 15; }
            } else if (v & MINE_BIT_FLAG) {
                ch = 'F'; fg = 4; bg = 14;
            }
            for (int dr2 = 0; dr2 < cell_h; dr2++)
                for (int dc2 = 0; dc2 < cell_w; dc2++)
                    vga_put_cell(rr + dr2, cc + dc2, ' ', fg, bg);
            vga_put_cell(rr, cc + 1, ch, fg, bg);
        }
    }
    const char *st;
    if (w->row == 1) st = "[ WIN!  R = new game ]";
    else if (w->row == 2) st = "[ BOOM. R = new game ]";
    else st = "[ arrows + space, F=flag, click cells, R=new ]";
    vga_write(w->r + w->h - 2, w->c + 2, st, 8, win_bg);
}

/* --- Analog clock ----------------------------------------------------- */
/* Hour/minute marks on a circular face + three hands (hour, minute,
 * second). Sin/cos table indexed by minute (0..59 = 6 degrees each)
 * stored in fixed-point /1024 so we don't need floats. */
static const i16 sin60[60] = {
       0,  107,  213,  316,  416,  512,  603,  689,  768,  840,
     905,  962, 1011, 1051, 1083, 1106, 1119, 1124, 1119, 1106,
    1083, 1051, 1011,  962,  905,  840,  768,  689,  603,  512,
     416,  316,  213,  107,    0, -107, -213, -316, -416, -512,
    -603, -689, -768, -840, -905, -962,-1011,-1051,-1083,-1106,
   -1119,-1124,-1119,-1106,-1083,-1051,-1011, -962, -905, -840,
};
static const i16 cos60[60] = {
    1024, 1019, 1008,  990,  967,  938,  902,  861,  815,  765,
     710,  651,  588,  522,  453,  381,  308,  234,  159,   84,
       9,  -65, -139, -212, -283, -353, -421, -487, -551, -612,
    -670, -724, -775, -823, -867, -907, -942, -973,-1000,-1021,
   -1037,-1049,-1056,-1058,-1054,-1046,-1033,-1015, -992, -964,
    -932, -896, -855, -811, -763, -711, -656, -598, -537, -474,
};

/* Plot the line from clock centre to (centre + minute_pos * length). */
static void analog_hand(int cy, int cx, int minute, int length,
                        char glyph, u8 fg) {
    int sx = sin60[minute];     /* /1024 */
    int sy = cos60[minute];
    for (int t = 1; t <= length; t++) {
        /* dx pixels are 2-wide; dy 1-tall. */
        int px = (sx * t) >> 10;
        int py = (sy * t) >> 10;
        int rr = cy - py;
        int cc = cx + px * 2;
        if (rr < 0 || cc < 0 || cc >= VGA_COLS - 1) continue;
        vga_put_cell(rr, cc,     glyph, fg, win_bg);
        vga_put_cell(rr, cc + 1, glyph, fg, win_bg);
    }
}

static void render_analog(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    struct rtc_time t; rtc_read(&t);
    int cy = w->r + w->h / 2;
    int cx = w->c + w->w / 2;
    if (cx & 1) cx--;
    /* Face: 12 hour marks at radius 4. */
    for (int m = 0; m < 60; m += 5) {
        int px = (sin60[m] * 4) >> 10;
        int py = (cos60[m] * 4) >> 10;
        int rr = cy - py;
        int cc = cx + px * 2;
        char g = (m % 15 == 0) ? '+' : '.';
        u8 fg = (m % 15 == 0) ? 14 : 8;
        vga_put_cell(rr, cc, g, fg, win_bg);
    }
    int sec = (t.sec < 60) ? t.sec : 0;
    int min = (t.min < 60) ? t.min : 0;
    int hour_min = ((t.hour % 12) * 5 + min / 12);
    analog_hand(cy, cx, hour_min, 2, 0xDB, 0);    /* hour: dark + short */
    analog_hand(cy, cx, min,      3, 0xB1, 11);   /* minute */
    analog_hand(cy, cx, sec,      4, 0xFA, 4);    /* second: thin red */
    vga_put_cell(cy, cx,     'o', 0, win_bg);
    vga_put_cell(cy, cx + 1, ' ', 0, win_bg);
    char buf[16];
    ksnprintf(buf, sizeof buf, "%02u:%02u:%02u",
              (u32)t.hour, (u32)t.min, (u32)t.sec);
    vga_write(w->r + w->h - 2, w->c + 2, buf, win_fg, win_bg);
}

/* --- Network scanner --------------------------------------------------- */
/* ICMP ping sweep over the local /24. We do one ping per render frame so
 * the desktop stays responsive while scanning. Per-ping timeout is kept
 * low (50 ticks ~= 500 ms in tick-time, but the real wait is much
 * shorter because the LAN almost always replies in <2 ticks). Found
 * hosts accumulate in w->content as 4-byte (a,b,c,d) tuples. */
#define NETSCAN_MAX_HOSTS 32

struct netscan_state {
    u8  found_a[NETSCAN_MAX_HOSTS];
    u8  found_b[NETSCAN_MAX_HOSTS];
    u8  found_c[NETSCAN_MAX_HOSTS];
    u8  found_d[NETSCAN_MAX_HOSTS];
    u16 found_rtt[NETSCAN_MAX_HOSTS];
    u8  found_count;
    u8  base_a, base_b, base_c;
    u16 next_octet;
};

static void netscan_reset(struct widget *w) {
    struct netscan_state *s = (struct netscan_state *)w->content;
    memset(s, 0, sizeof *s);
    w->content_len = (int)sizeof *s;
    w->row  = 0;        /* 0=idle 1=scanning 2=done */
    w->sel  = 0;
    w->top  = 0;
    struct net_iface *n = net_iface();
    if (n && n->present) {
        u32 v = ntohl(n->ip);
        s->base_a = (u8)((v >> 24) & 0xFF);
        s->base_b = (u8)((v >> 16) & 0xFF);
        s->base_c = (u8)((v >> 8)  & 0xFF);
    } else {
        s->base_a = 192; s->base_b = 168; s->base_c = 1;
    }
}

static void netscan_record(struct netscan_state *s, u8 d, u32 rtt) {
    if (s->found_count >= NETSCAN_MAX_HOSTS) return;
    s->found_a[s->found_count]   = s->base_a;
    s->found_b[s->found_count]   = s->base_b;
    s->found_c[s->found_count]   = s->base_c;
    s->found_d[s->found_count]   = d;
    s->found_rtt[s->found_count] = (u16)(rtt > 65535 ? 65535 : rtt);
    s->found_count++;
}

static int netscan_handle_key(struct widget *w, int k) {
    if (w->content_len == 0) netscan_reset(w);
    if (k == ' ' || k == '\n' || k == '\r') {
        if (w->row == 1) { w->row = 2; return 1; }   /* stop */
        netscan_reset(w);
        w->row = 1;
        return 1;
    }
    if (k == 'r' || k == 'R') { netscan_reset(w); return 1; }
    return 0;
}

static void render_netscan(struct widget *w) {
    struct netscan_state *s = (struct netscan_state *)w->content;
    draw_window(w->r, w->c, w->w, w->h, w->title);
    if (w->content_len == 0) netscan_reset(w);

    /* Drive one ping per ~10 ticks while scanning. */
    if (w->row == 1) {
        u32 now = pit_ticks();
        if ((u32)(now - (u32)w->prev_sec) >= 10 && s->next_octet <= 254) {
            w->prev_sec = (int)now;
            u8 d = (u8)s->next_octet;
            s->next_octet++;
            u32 ip_h = ((u32)s->base_a << 24) | ((u32)s->base_b << 16)
                     | ((u32)s->base_c << 8)  | (u32)d;
            ip4_addr_t dst = htonl(ip_h);
            u32 rtt = 0;
            if (icmp_ping(dst, 5, &rtt) == 0) {
                netscan_record(s, d, rtt);
            }
            if (s->next_octet > 254) w->row = 2;
        }
    }

    /* Header: interface / range. */
    char line[80];
    struct net_iface *n = net_iface();
    if (n && n->present) {
        u32 v = ntohl(n->ip);
        ksnprintf(line, sizeof line, "Local: %u.%u.%u.%u   GW: ",
                  (v>>24)&0xFF, (v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
        v = ntohl(n->gateway);
        int p = (int)strlen(line);
        ksnprintf(line + p, (int)sizeof line - p, "%u.%u.%u.%u",
                  (v>>24)&0xFF, (v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
    } else {
        ksnprintf(line, sizeof line, "Local: (no network)");
    }
    vga_write(w->r + 1, w->c + 2, line, win_fg, win_bg);
    ksnprintf(line, sizeof line, "Scanning %u.%u.%u.1..254",
              s->base_a, s->base_b, s->base_c);
    vga_write(w->r + 2, w->c + 2, line, win_fg, win_bg);

    /* Progress + status. */
    if (w->row == 0)
        ksnprintf(line, sizeof line, "[ SPACE = start scan ]");
    else if (w->row == 1)
        ksnprintf(line, sizeof line, "[ scanning ... %d / 254  found %d  SPACE = stop ]",
                  s->next_octet, s->found_count);
    else
        ksnprintf(line, sizeof line, "[ done. %d alive. SPACE = rescan ]",
                  s->found_count);
    vga_write(w->r + 3, w->c + 2, line, 14, win_bg);

    /* Progress bar. */
    int bw = w->w - 6;
    int filled = (int)((u32)s->next_octet * bw / 254);
    for (int x = 0; x < bw; x++)
        vga_put_cell(w->r + 4, w->c + 3 + x,
                     x < filled ? 0xDB : 0xB0,
                     (x < filled ? 2 : 8), win_bg);

    /* Found hosts. */
    vga_write(w->r + 6, w->c + 2, "Alive hosts:", win_fg, win_bg);
    int max_visible = w->h - 9;
    int top = (int)s->found_count > max_visible
              ? (int)s->found_count - max_visible : 0;
    for (int i = 0; i < max_visible; i++) {
        vga_fill(w->r + 7 + i, w->c + 2, w->w - 4, 1, ' ', win_fg, win_bg);
        int idx = top + i;
        if (idx >= s->found_count) break;
        ksnprintf(line, sizeof line, "  %3u.%3u.%3u.%3u   %u ticks%s",
                  s->found_a[idx], s->found_b[idx],
                  s->found_c[idx], s->found_d[idx],
                  (u32)s->found_rtt[idx],
                  (n && n->present && s->found_d[idx] == ((ntohl(n->ip)) & 0xFF))
                      ? "  (this host)" : "");
        vga_write(w->r + 7 + i, w->c + 2, line, win_fg, win_bg);
    }
}

/* --- Partition manager ------------------------------------------------- */
/* MBR partition editor. Lists every raw disk; for the selected one,
 * shows its 4 partition slots with start/size/type. New partitions get
 * a Y/N confirm and a size-in-MiB prompt that defaults to "use the
 * whole free span" so the user doesn't have to type sectors. The
 * footer is the only place we draw status -- the disk-manager-style
 * scrollback-bleed bug came from kprintf writes during fs_rescan(),
 * which is now safe because vga_putc is shadow-buffer aware. */

/* Pick the next raw disk index that has present == 1, starting from
 * `start_idx` inclusive. Returns -1 if no disk is present. */
static int partmgr_next_raw_disk(int start_idx) {
    for (int i = start_idx; i < DISK_RAW_MAX; i++) {
        struct disk *d = disk_get(i);
        if (d && d->present) return i;
    }
    return -1;
}

/* Bottom-line prompt: shows `label`, reads a string into `out`. Returns
 * 1 on accept, 0 on ESC cancel. Identical UX to files_prompt but lives
 * here so the partition manager doesn't pull in file-state structs. */
static int partmgr_prompt(struct widget *w, const char *label,
                          const char *prefill, char *out, int outsz) {
    int r = w->r + w->h - 2;
    int c = w->c + 2;
    int wpix = w->w - 4;
    vga_fill(r, c, wpix, 1, ' ', win_bg, win_fg);
    vga_write(r, c, label, win_bg, win_fg);
    int lc = c + (int)strlen(label) + 1;
    int len = 0;
    if (prefill) {
        for (int i = 0; prefill[i] && i < outsz - 1; i++) out[i] = prefill[i];
        len = (int)strlen(prefill);
    }
    out[len] = '\0';
    for (;;) {
        vga_fill(r, lc, wpix - (int)strlen(label) - 1, 1, ' ', win_bg, win_fg);
        for (int i = 0; i < len; i++) vga_put_cell(r, lc + i, out[i], win_bg, win_fg);
        vga_put_cell(r, lc + len, '_', win_bg, win_fg);
        vga_present();
        int k = kb_getc();
        if (k == 27) { out[0] = '\0'; return 0; }
        if (k == '\n' || k == '\r') return 1;
        if (k == '\b') { if (len > 0) { len--; out[len] = '\0'; } continue; }
        if (k < ' ' || k > '~') continue;
        if (len + 1 < outsz) { out[len++] = (char)k; out[len] = '\0'; }
    }
}

static int partmgr_confirm(struct widget *w, const char *msg) {
    int r = w->r + w->h - 2;
    int c = w->c + 2;
    int wpix = w->w - 4;
    vga_fill(r, c, wpix, 1, ' ', 14, 4);
    vga_write(r, c + 1, msg, 14, 4);
    vga_present();
    for (;;) {
        int k = kb_getc();
        if (k == 'y' || k == 'Y') return 1;
        if (k == 'n' || k == 'N' || k == 27 || k == '\n' || k == '\r') return 0;
    }
}

/* Parse a non-negative decimal. Returns -1 on empty / bad input. */
static int partmgr_parse_uint(const char *s, u32 *out) {
    u32 v = 0;
    int any = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (u32)(*s++ - '0'); any = 1; }
    if (!any) return -1;
    *out = v;
    return 0;
}

static const char *partmgr_type_name(u8 t) {
    switch (t) {
    case 0x00: return "empty";
    case 0x01: return "FAT12";
    case 0x04: return "FAT16<32M";
    case 0x06: return "FAT16";
    case 0x0B: return "FAT32";
    case 0x0C: return "FAT32 LBA";
    case 0x0E: return "FAT16 LBA";
    default:   return "other";
    }
}

/* --- Programs widget: list + launch .ZBX executables --------------- */
extern int zbx_run(const char *path);
extern int zbx_inspect(const char *path, const char **out_src);

/* Collect *.ZBX entries from the current drive root into a static
 * array. Returns count. Names are 8.3 uppercase as stored. */
#define PROG_MAX 64
static char  prog_names[PROG_MAX][16];
static int   prog_count;

/* Stash the source dir for each program so launch can build the full
 * path. parallel array to prog_names. */
static char prog_dirs[PROG_MAX][16];

static void programs_scan_dir(const char *dir) {
    int dh = fs_opendir(dir);
    if (dh < 0) return;
    struct fs_dirent e;
    while (prog_count < PROG_MAX && fs_readdir(dh, &e)) {
        if (e.attr & FS_ATTR_DIR) continue;
        const char *dot = 0;
        for (const char *p = e.name; *p; p++) if (*p == '.') dot = p;
        if (!dot) continue;
        if (!(dot[1]=='Z'&&dot[2]=='B'&&dot[3]=='X'&&dot[4]==0) &&
            !(dot[1]=='z'&&dot[2]=='b'&&dot[3]=='x'&&dot[4]==0)) continue;
        int j = 0;
        while (e.name[j] && j < 15) { prog_names[prog_count][j] = e.name[j]; j++; }
        prog_names[prog_count][j] = '\0';
        j = 0;
        while (dir[j] && j < 15) { prog_dirs[prog_count][j] = dir[j]; j++; }
        prog_dirs[prog_count][j] = '\0';
        prog_count++;
    }
    fs_closedir(dh);
}

static void programs_scan(void) {
    prog_count = 0;
    /* Drive root (user-installed programs) + the system bin dir
     * (shipped CLOCK / COLORS / GUESS / ...). Same widget surfaces
     * both -- the user doesn't need to know which is which. */
    programs_scan_dir(".");
    programs_scan_dir("\\SYSTEM\\BIN");
}

static void render_programs(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    if (w->content_len == 0) { programs_scan(); w->content_len = 1; }
    vga_write(w->r + 1, w->c + 2,
              "ENTER=run  F5=rescan  (*.ZBX on this drive)", 8, win_bg);
    int rows = w->h - 4;
    if (w->sel >= prog_count) w->sel = prog_count ? prog_count - 1 : 0;
    if (w->sel < 0) w->sel = 0;
    if (w->top > w->sel) w->top = w->sel;
    if (w->sel >= w->top + rows) w->top = w->sel - rows + 1;
    if (prog_count == 0) {
        vga_write(w->r + 3, w->c + 2, "No .ZBX programs found.", win_fg, win_bg);
        vga_write(w->r + 5, w->c + 2, "Make one in the shell:", 8, win_bg);
        vga_write(w->r + 6, w->c + 2, " mkzbx hello.c HELLO.ZBX", 8, win_bg);
        return;
    }
    for (int i = 0; i < rows; i++) {
        int idx = w->top + i;
        int ry = w->r + 3 + i;
        vga_fill(ry, w->c + 2, w->w - 4, 1, ' ', win_fg, win_bg);
        if (idx >= prog_count) continue;
        char line[40];
        ksnprintf(line, sizeof line, "  %s", prog_names[idx]);
        u8 fg = win_fg, bg = win_bg;
        if (idx == w->sel && w->focused) { fg = win_bg; bg = win_fg; }
        int ln = (int)strlen(line);
        for (int x = 0; x < w->w - 4; x++)
            vga_put_cell(ry, w->c + 2 + x, x < ln ? line[x] : ' ', fg, bg);
    }
}

/* Launch the selected program fullscreen: tui_end -> run -> tui_init.
 * Mirrors how the Editor app is bracketed. Returns 1 to request a
 * full desktop redraw. */
static int programs_launch(struct widget *w) {
    if (w->sel < 0 || w->sel >= prog_count) return 0;
    char path[48];
    /* Build full path: "<dir>\<name>" (skip dir prefix if it's the
     * current dir marker). zbx_run / fs_open accept absolute paths
     * starting with "\\..." or relative ones. */
    const char *d = prog_dirs[w->sel];
    if (d[0] == '.' && d[1] == '\0')
        ksnprintf(path, sizeof path, "%s", prog_names[w->sel]);
    else
        ksnprintf(path, sizeof path, "%s\\%s", d, prog_names[w->sel]);
    tui_end();
    kputs("\n");
    vga_clear();
    zbx_run(path);
    kputs("\n[program exited -- press a key]\n");
    kb_getc();
    tui_init();
    return 1;
}

static void render_partmgr(struct widget *w) {
    draw_window(w->r, w->c, w->w, w->h, w->title);
    /* Resolve "current disk" (w->row): pick first present raw disk
     * if -1, otherwise validate. */
    if (w->row < 0 || disk_get(w->row) == NULL || !disk_get(w->row)->present) {
        w->row = partmgr_next_raw_disk(0);
    }
    if (w->row < 0) {
        vga_write(w->r + 2, w->c + 2,
                  "No raw disks present.", 4, win_bg);
        vga_write(w->r + w->h - 2, w->c + 2,
                  "[ F5 = rescan disks ]", 8, win_bg);
        return;
    }
    struct disk *d = disk_get(w->row);
    char line[80];
    ksnprintf(line, sizeof line,
              "Disk: %-6s  %u MiB  (%u sectors)",
              d->name, d->sectors / 2048, d->sectors);
    vga_write(w->r + 1, w->c + 2, line, win_fg, win_bg);

    /* Partition table. */
    struct mbr_part pt[MBR_PART_MAX];
    int has_mbr = mbr_has_table(w->row);
    if (has_mbr) {
        if (mbr_read(w->row, pt) < 0) memset(pt, 0, sizeof pt);
    } else {
        memset(pt, 0, sizeof pt);
    }

    vga_write(w->r + 3, w->c + 2,
              " #  Boot     Start      Sectors    Size   Type",
              8, win_bg);
    if (w->sel < 0) w->sel = 0;
    if (w->sel > 3) w->sel = 3;
    for (int i = 0; i < MBR_PART_MAX; i++) {
        int row_y = w->r + 4 + i;
        vga_fill(row_y, w->c + 2, w->w - 4, 1, ' ', win_fg, win_bg);
        if (pt[i].type == 0) {
            ksnprintf(line, sizeof line, " %d   --       --         --      --   empty",
                      i + 1);
        } else {
            ksnprintf(line, sizeof line,
                      " %d   %c    %9u  %10u  %4u M  %02X %s",
                      i + 1, pt[i].boot == 0x80 ? '*' : ' ',
                      pt[i].start_lba, pt[i].sectors, pt[i].sectors / 2048,
                      pt[i].type, partmgr_type_name(pt[i].type));
        }
        u8 fg = win_fg, bg = win_bg;
        if (i == w->sel && w->focused) { fg = win_bg; bg = win_fg; }
        int lnln = (int)strlen(line);
        for (int xc = 0; xc < w->w - 4; xc++)
            vga_put_cell(row_y, w->c + 2 + xc,
                         xc < lnln ? line[xc] : ' ', fg, bg);
    }
    /* Free-span hint. */
    if (has_mbr) {
        u32 fs = 0, fc = 0;
        mbr_largest_free(w->row, &fs, &fc);
        ksnprintf(line, sizeof line,
                  "Free: %u sectors (%u MiB) starting at LBA %u",
                  fc, fc / 2048, fs);
        vga_write(w->r + 9, w->c + 2, line, 8, win_bg);
    } else {
        vga_write(w->r + 9, w->c + 2,
                  "No MBR -- press M to write one.", 14, win_bg);
    }

    /* Help line just above the status line. */
    vga_write(w->r + w->h - 4, w->c + 2,
              " N=new  D=delete  A=active  M=mkmbr",
              win_fg, win_bg);
    vga_write(w->r + w->h - 3, w->c + 2,
              " TAB=next disk   F5=rescan   UP/DOWN=slot",
              win_fg, win_bg);

    char status[80];
    ksnprintf(status, sizeof status,
              "[ %s -- slot %d selected ]",
              d->name, w->sel + 1);
    vga_write(w->r + w->h - 2, w->c + 2, status, 8, win_bg);
}

static int partmgr_handle_key(struct widget *w, int k) {
    if (w->row < 0) w->row = partmgr_next_raw_disk(0);
    if (w->row < 0) {
        if (k == (int)KB_F5) { fs_rescan(); w->dirty = 1; return 1; }
        return 0;
    }
    struct disk *d = disk_get(w->row);
    if (!d) return 0;
    int has_mbr = mbr_has_table(w->row);

    if (k == (int)KB_UP   && w->sel > 0) { w->sel--; w->dirty = 1; return 1; }
    if (k == (int)KB_DOWN && w->sel < 3) { w->sel++; w->dirty = 1; return 1; }
    if (k == '\t') {
        int next = partmgr_next_raw_disk(w->row + 1);
        if (next < 0) next = partmgr_next_raw_disk(0);
        if (next >= 0) { w->row = next; w->sel = 0; w->dirty = 1; }
        return 1;
    }
    if (k == (int)KB_F5) {
        fs_rescan();
        w->dirty = 1;
        return 1;
    }
    if (k == 'm' || k == 'M') {
        if (!has_mbr) {
            char msg[64];
            ksnprintf(msg, sizeof msg,
                      " Write a fresh MBR to %s? (Y/N) ", d->name);
            if (partmgr_confirm(w, msg)) {
                mbr_init_disk(w->row);
                fs_rescan();
            }
        }
        w->dirty = 1; return 1;
    }
    if (k == 'd' || k == 'D') {
        struct mbr_part pt[MBR_PART_MAX];
        if (!has_mbr || mbr_read(w->row, pt) < 0) return 1;
        if (pt[w->sel].type == 0) {
            partmgr_confirm(w, " Slot is already empty.  ENTER to dismiss. ");
        } else {
            char msg[64];
            ksnprintf(msg, sizeof msg,
                      " Delete partition %d on %s? (Y/N) ",
                      w->sel + 1, d->name);
            if (partmgr_confirm(w, msg)) {
                mbr_delete_partition(w->row, w->sel);
                fs_rescan();
            }
        }
        w->dirty = 1; return 1;
    }
    if (k == 'a' || k == 'A') {
        struct mbr_part pt[MBR_PART_MAX];
        if (!has_mbr || mbr_read(w->row, pt) < 0) return 1;
        if (pt[w->sel].type == 0) return 1;
        mbr_set_active(w->row, w->sel);
        w->dirty = 1; return 1;
    }
    if (k == 'n' || k == 'N') {
        /* Auto-write MBR if missing. */
        if (!has_mbr) {
            if (!partmgr_confirm(w, " No MBR on this disk. Write one now? (Y/N) "))
                return 1;
            mbr_init_disk(w->row);
            has_mbr = 1;
        }
        /* Find largest free span. */
        u32 fs = 0, fc = 0;
        if (mbr_largest_free(w->row, &fs, &fc) < 0 || fc < 2048) {
            partmgr_confirm(w, " No free space (>=1 MiB) on this disk.  ENTER dismiss. ");
            w->dirty = 1; return 1;
        }
        /* Find an empty slot. */
        struct mbr_part pt[MBR_PART_MAX];
        if (mbr_read(w->row, pt) < 0) { w->dirty = 1; return 1; }
        int slot = -1;
        for (int i = 0; i < MBR_PART_MAX; i++)
            if (pt[i].type == 0) { slot = i; break; }
        if (slot < 0) {
            partmgr_confirm(w, " All 4 partition slots full -- delete one first. ");
            w->dirty = 1; return 1;
        }
        /* Prompt for size in MiB, default = max. */
        u32 max_mib = fc / 2048;
        char def[16]; ksnprintf(def, sizeof def, "%u", max_mib);
        char prompt[80];
        ksnprintf(prompt, sizeof prompt,
                  " Size in MiB (1..%u, ENTER = use max):", max_mib);
        char buf[16];
        if (!partmgr_prompt(w, prompt, def, buf, sizeof buf)) {
            w->dirty = 1; return 1;
        }
        u32 mib = 0;
        if (partmgr_parse_uint(buf, &mib) < 0 || mib == 0) mib = max_mib;
        if (mib > max_mib) mib = max_mib;
        u32 sectors = mib * 2048;
        if (sectors > fc) sectors = fc;
        u8 type = mbr_suggest_type(sectors);
        int active = 1;
        for (int i = 0; i < MBR_PART_MAX; i++)
            if (pt[i].boot == 0x80) { active = 0; break; }
        char confirm[80];
        ksnprintf(confirm, sizeof confirm,
                  " Create %u MiB %s on %s slot %d? (Y/N) ",
                  mib, partmgr_type_name(type), d->name, slot + 1);
        if (partmgr_confirm(w, confirm)) {
            mbr_create_partition(w->row, slot, type, fs, sectors, active);
            /* Make the new partition usable immediately: scan + format
             * the view-disk to FAT, mount under next free letter. */
            fs_rescan();
            int view = mbr_view_for(w->row, slot);
            if (view >= 0) {
                extern int fs_format(int disk_id, const char *label);
                fs_format(view, "ZENBITE");
                fs_rescan();
            }
        }
        w->dirty = 1; return 1;
    }
    return 0;
}

static void render_one(struct widget *w) {
    switch (w->kind) {
        case WK_WELCOME:  render_welcome      (w); break;
        case WK_CLOCK:    render_clock_widget (w); break;
        case WK_SYSMON:   render_sysmon_widget(w); break;
        case WK_MINICALC: render_minicalc     (w); break;
        case WK_ABOUT:    render_about        (w); break;
        case WK_NOTES:    render_notes        (w); break;
        case WK_FILES:    render_files        (w); break;
        case WK_SETTINGS: render_settings     (w); break;
        case WK_TERMINAL: render_terminal     (w); break;
        case WK_WEB:      render_web          (w); break;
        case WK_DISKMGR:  render_diskmgr      (w); break;
        case WK_CALENDAR: render_calendar     (w); break;
        case WK_SNAKE:    render_snake        (w); break;
        case WK_TETRIS:   render_tetris       (w); break;
        case WK_MINES:    render_mines        (w); break;
        case WK_ANALOG:   render_analog       (w); break;
        case WK_NETSCAN:  render_netscan      (w); break;
        case WK_PARTMGR:  render_partmgr      (w); break;
        case WK_PROGRAMS: render_programs     (w); break;
        default: break;
    }
    /* Resize handle: a small "+" at the bottom-right cell of every
     * resizable widget. Welcome, Clock, Mini-Calc, About are fixed-
     * size; the rest are resizable. */
    if (w->kind != WK_WELCOME && w->kind != WK_CLOCK &&
        w->kind != WK_MINICALC && w->kind != WK_ABOUT) {
        vga_put_cell(w->r + w->h - 1, w->c + w->w - 1, 0xC4, tb_fg, tb_bg);
    }
}

static void draw_taskbar(void);

static void draw_all_widgets(void) {
    /* Render in ascending z order so higher-z widgets paint on top.
     * Minimized widgets are excluded -- the taskbar represents them. */
    for (int z = 0; z < next_z; z++)
        for (int i = 0; i < MAX_WIDGETS; i++)
            if (widgets[i].used && !widgets[i].minimized && widgets[i].z == z)
                render_one(&widgets[i]);
    draw_taskbar();
}

/* Dropdown contents for File / View / Help (Zenbite menu = app list). */
static const char *file_menu[] = { "New (Editor)", "Open (Files)", "Quit Desktop" };
static const char *view_menu[] = {
    "Refresh", "Terminal", "Web",
    "Disk Manager", "Calendar", "Snake"
};
static const char *help_menu[] = { "Shell commands", "About Zenbite" };
#define FILE_MENU_N (int)(sizeof file_menu / sizeof file_menu[0])
#define VIEW_MENU_N (int)(sizeof view_menu / sizeof view_menu[0])
#define HELP_MENU_N (int)(sizeof help_menu / sizeof help_menu[0])

/* --- Clock ----------------------------------------------------------- */
static void draw_clock(void) {
    struct rtc_time t;
    rtc_read(&t);
    if (!t.year) return;
    char buf[6];
    ksnprintf(buf, sizeof buf, "%02u:%02u", (u32)t.hour, (u32)t.min);
    vga_write(0, VGA_COLS - 5, buf, mb_fg, mb_bg);
}

/* --- Menu bar -------------------------------------------------------- */
static void draw_bar(int hot_title) {
    vga_fill(0, 0, VGA_COLS, 1, ' ', mb_fg, mb_bg);
    /* Apple-style logo: CP437 0x0F (eight-pointed star). */
    vga_put_cell(0, 1, 0x0F, mb_fg, mb_bg);
    int col = 3;
    for (int i = 0; i < BAR_COUNT; i++) {
        int len = (int)strlen(bar_titles[i]);
        bar_x[i] = col;
        bar_w[i] = len + 2;
        u8 fg = (i == hot_title) ? mb_hi_fg : mb_fg;
        u8 bg = (i == hot_title) ? mb_hi_bg : mb_bg;
        vga_fill(0, col, len + 2, 1, ' ', fg, bg);
        vga_write(0, col + 1, bar_titles[i], fg, bg);
        col += len + 3;
    }
    draw_clock();
}

static int bar_hit(int c, int r) {
    if (r != 0) return -1;
    for (int i = 0; i < BAR_COUNT; i++)
        if (c >= bar_x[i] && c < bar_x[i] + bar_w[i]) return i;
    return -1;
}

/* --- Status line ----------------------------------------------------- */
static void draw_status(const char *msg) {
    vga_fill(VGA_ROWS - 1, 0, VGA_COLS, 1, ' ', 0, mb_bg);
    if (msg) vga_write(VGA_ROWS - 1, 1, msg, 0, mb_bg);
}

/* Taskbar layout: x-position and width of each widget's chip on
 * row VGA_ROWS-1.  Used by bar click-handling to map a mouse hit to
 * a widget index. -1 means "no chip" (slot unused or Welcome). */
static int  task_x[MAX_WIDGETS];
static int  task_w[MAX_WIDGETS];

/* XP-Luna bottom bar layout:
 *   col 0..8    : green "Start" button
 *   col 9..N    : task chips for visible widgets
 *   right side  : system tray (network indicator + clock)
 * The Start button is just a fixed hit zone reusing the existing
 * Zenbite menu dropdown -- clicking it pops the categorised app
 * list, same as F9 or clicking the top-bar logo. */
#define START_BTN_WIDTH 9
static int taskbar_start_x = 0;
static int taskbar_tray_x  = 0;
static void draw_taskbar(void) {
    int row = VGA_ROWS - 1;
    vga_fill(row, 0, VGA_COLS, 1, ' ', mb_fg, mb_bg);
    /* Start button: bright green with white text, like XP's green
     * Start orb collapsed to text mode. */
    taskbar_start_x = 0;
    vga_fill(row, 0, START_BTN_WIDTH, 1, ' ', 15, 2);
    vga_write(row, 2, "Start", 15, 2);
    /* Task chips start after the Start button. */
    int col = START_BTN_WIDTH + 1;
    /* Reserve room on the right for the tray (network + HH:MM). */
    int tray_w = 14;
    int tray_col = VGA_COLS - tray_w;
    for (int i = 0; i < MAX_WIDGETS; i++) {
        task_x[i] = -1; task_w[i] = 0;
        if (!widgets[i].used) continue;
        const char *t = widgets[i].title;
        int len = (int)strlen(t);
        if (len > 14) len = 14;
        int chipw = len + 2;
        if (col + chipw >= tray_col - 1) break;
        u8 fg = widgets[i].minimized ? 8 : mb_fg;
        u8 bg = widgets[i].focused   ? mb_hi_bg : mb_bg;
        vga_fill(row, col, chipw, 1, ' ', fg, bg);
        for (int j = 0; j < len; j++)
            vga_put_cell(row, col + 1 + j, t[j], fg, bg);
        task_x[i] = col; task_w[i] = chipw;
        col += chipw + 1;
    }
    /* Tray: optional network glyph + clock. Drawn over a slightly
     * darker band to read as a separate region. */
    taskbar_tray_x = tray_col;
    vga_fill(row, tray_col, tray_w, 1, ' ', 15, 8);
    /* Network indicator: filled square if the iface is up. */
    struct net_iface *n = net_iface();
    int up = (n && n->present);
    vga_put_cell(row, tray_col + 1, up ? 0xFE : 0xF8, up ? 10 : 4, 8);
    /* Clock HH:MM at right. */
    struct rtc_time t; rtc_read(&t);
    char buf[8];
    ksnprintf(buf, sizeof buf, "%02u:%02u", (u32)t.hour, (u32)t.min);
    vga_write(row, tray_col + tray_w - 6, buf, 15, 8);
}

/* Map a click at (c,r) on the taskbar row to a widget index, or -1. */
static int taskbar_hit(int c, int r) {
    if (r != VGA_ROWS - 1) return -1;
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (task_x[i] >= 0 && c >= task_x[i] && c < task_x[i] + task_w[i])
            return i;
    return -1;
}

/* --- Wallpaper ------------------------------------------------------- */
/* WALL.TXT image cache. The file is plain ASCII / CP437; lines up to
 * VGA_COLS wide map left-to-right, top-to-bottom. Loaded once per
 * style-change so we don't hammer the disk on every full repaint. */
/* Size for the max text-mode height (80x50) so a runtime mode-switch
 * never has to realloc. Unused rows in 80x25 mode are just empty. */
static char wall_img[(VGA_ROWS_MAX - 2) * VGA_COLS_MAX];
static int  wall_img_loaded;

static void load_wall_image(void) {
    for (int i = 0; i < (int)sizeof wall_img; i++) wall_img[i] = ' ';
    int fh = fs_open("WALL.TXT");
    if (fh < 0) { wall_img_loaded = 1; return; }
    static char buf[(VGA_ROWS_MAX - 2) * (VGA_COLS_MAX + 2)];
    int n = fs_read(fh, buf, sizeof buf);
    fs_close(fh);
    if (n < 0) n = 0;
    int row = 0, col = 0;
    for (int i = 0; i < n && row < VGA_ROWS - 2; i++) {
        char ch = buf[i];
        if (ch == '\r') continue;
        if (ch == '\n') { row++; col = 0; continue; }
        if (col < VGA_COLS) wall_img[row * VGA_COLS + col++] = ch;
    }
    wall_img_loaded = 1;
}

/* Forward decl: defined further down. Called from draw_wallpaper so
 * every wallpaper-repaint site (including the Bliss early-return
 * path) picks up the icon overlay. */
void draw_desktop_icons(void);

static void draw_wallpaper(void) {
    if (wallpaper_style == 4 && !wall_img_loaded) load_wall_image();
    /* "Bliss" wallpaper is its own renderer: sky gradient on top, an
     * arching green hill curve on the bottom third, no relation to
     * the dt_fg / dt_bg knobs. Drawn here when wallpaper_style == 5. */
    if (wallpaper_style == 5) {
        int rows = VGA_ROWS - 2;
        int cols = VGA_COLS;
        int horizon = rows * 6 / 10;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                u8 ch;
                u8 fg;
                u8 bg;
                if (r < horizon) {
                    /* Sky: light blue with darker upper band + a few
                     * cloud-like '~' specks for texture. */
                    bg = 11;     /* light cyan */
                    fg = 15;
                    if (r < horizon / 3) bg = 9;   /* darker top */
                    ch = ' ';
                    /* sparse clouds */
                    if (r == horizon / 2 && ((c * 7) & 0x1F) == 5) ch = 0xB0;
                    if (r == horizon / 2 + 1 && ((c * 11) & 0x1F) == 4) ch = 0xB1;
                } else {
                    /* Hill: arching green that swells across the
                     * middle. We map column to a sine-like bulge
                     * via integer math. */
                    int x = c - cols / 2;
                    int bulge = (cols * cols / 16 - x * x) / 64;
                    int hill_top = horizon - (bulge > 0 ? bulge : 0) / 4;
                    bg = (r >= hill_top) ? 2 : 11;   /* green vs sky */
                    fg = 10;
                    ch = (r >= hill_top) ? (((r + c) & 3) == 0 ? 0xB0 : ' ') : ' ';
                }
                vga_put_cell(r + 1, c, ch, fg, bg);
            }
        }
        draw_desktop_icons();
        return;
    }
    for (int r = 1; r < VGA_ROWS - 1; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            u8 ch = ' ';
            switch (wallpaper_style) {
                case 0: ch = ' '; break;                          /* solid */
                case 1: ch = ((r + c) & 1) ? 0xB0 : ' '; break;   /* stipple */
                case 2: ch = ((r & 1) && (c & 1)) ? 0xFA : ' '; break; /* dots */
                case 3: ch = (r % 4 == 0 || c % 8 == 0) ? 0xC4 : ' '; break; /* grid */
                case 4: ch = (u8)wall_img[(r - 1) * VGA_COLS + c]; break;
            }
            vga_put_cell(r, c, ch, dt_fg, dt_bg);
        }
    }
    /* Icons sit on top of the wallpaper but under any windows.
     * Layering: draw_wallpaper -> draw_desktop_icons -> widgets.
     * Calling draw_desktop_icons here keeps every wallpaper-repaint
     * site icon-aware without needing to edit each one. */
    draw_desktop_icons();
}

/* --- Desktop icons --------------------------------------------------- *
 * Clickable shortcuts placed on the wallpaper. Single-click launches
 * the app (KDE/GNOME-style; double-click would be more XP-like but
 * adds timing-sensitive state for marginal benefit at text-mode cell
 * granularity). Each icon is a 3-col-wide block: row 0 = coloured
 * glyph, row 1 = label (up to 7 chars truncated). */
struct desk_icon {
    int  app;
    const char *label;
    u8   glyph;
    u8   color;        /* fg in low nibble, bg in high nibble */
    int  r, c;         /* placed at startup */
};

static struct desk_icon icons[] = {
    { APP_FILES,    "Files",  0xDB, 0xE0, 0, 0 },  /* yellow on black */
    { APP_EDITOR,   "Editor", 0xDB, 0xB0, 0, 0 },  /* light cyan */
    { APP_NOTES,    "Notes",  0xDB, 0x60, 0, 0 },  /* brown */
    { APP_TERMINAL, "Cmd",    0xDB, 0x80, 0, 0 },  /* dark grey */
    { APP_WEB,      "Web",    0xDB, 0x90, 0, 0 },  /* light blue */
    { APP_CALC,     "Calc",   0xDB, 0xC0, 0, 0 },  /* light red */
    { APP_TETRIS,   "Tetris", 0xDB, 0xA0, 0, 0 },  /* light green */
    { APP_CALENDAR, "Cal",    0xDB, 0xD0, 0, 0 },  /* light magenta */
    { APP_DISKMGR,  "Disk",   0xDB, 0x70, 0, 0 },  /* light grey */
};
#define DESK_ICON_COUNT (int)(sizeof icons / sizeof icons[0])

/* Place icons in a vertical column on the left, just after the menu
 * bar. Called once at desktop_main entry so the positions don't
 * shift on every redraw. */
static void place_desktop_icons(void) {
    /* Two columns of icons, top-down, so 9 fit cleanly in the
     * usable desktop region (rows 1..VGA_ROWS-2). Each icon owns
     * a 7-col x 2-row block (glyph + label). */
    int per_col = (VGA_ROWS - 4) / 3;
    if (per_col < 1) per_col = 1;
    for (int i = 0; i < DESK_ICON_COUNT; i++) {
        int col_n = i / per_col;
        int row_n = i % per_col;
        icons[i].r = 2 + row_n * 3;
        icons[i].c = 1 + col_n * 9;
    }
}

void draw_desktop_icons(void) {
    for (int i = 0; i < DESK_ICON_COUNT; i++) {
        struct desk_icon *ic = &icons[i];
        u8 fg = ic->color & 0x0F;
        u8 bg = (ic->color >> 4) & 0x0F;
        /* 3 cells of glyph for a chunky look. */
        vga_put_cell(ic->r,     ic->c,     ic->glyph, fg, bg);
        vga_put_cell(ic->r,     ic->c + 1, ic->glyph, fg, bg);
        vga_put_cell(ic->r,     ic->c + 2, ic->glyph, fg, bg);
        /* Label centred under the glyph in white-on-transparent. */
        const char *s = ic->label;
        for (int j = 0; j < 7 && s[j]; j++)
            vga_put_cell(ic->r + 1, ic->c + j, s[j], 15, 1);
    }
}

/* Return the icon index under (col, row), or -1 if none.
 * Icons claim a 3x2 cell rectangle. */
static int icon_hit(int col, int row) {
    for (int i = 0; i < DESK_ICON_COUNT; i++) {
        if (row < icons[i].r || row > icons[i].r + 1) continue;
        if (col < icons[i].c || col > icons[i].c + 2) continue;
        return i;
    }
    return -1;
}

/* --- Window chrome --------------------------------------------------- */
static void draw_window(int r, int c, int w, int h, const char *title) {
    vga_fill(r, c, w, 1, ' ', tb_fg, tb_bg);
    vga_put_cell(r, c + 1, 0x07, 4, tb_bg);    /* red dot */
    vga_put_cell(r, c + 2, 0x07, 14, tb_bg);   /* yellow */
    vga_put_cell(r, c + 3, 0x07, 2, tb_bg);    /* green */
    int tlen = (int)strlen(title);
    vga_write(r, c + (w - tlen) / 2, title, tb_fg, tb_bg);
    vga_fill(r + 1, c, w, h - 1, ' ', win_fg, win_bg);
    /* Drop shadow. */
    for (int rr = r + 1; rr < r + h + 1 && rr < VGA_ROWS - 1; rr++)
        vga_put_cell(rr, c + w, 0xB1, 8, dt_bg);
    for (int cc = c + 1; cc < c + w + 1 && cc < VGA_COLS; cc++)
        if (r + h < VGA_ROWS - 1) vga_put_cell(r + h, cc, 0xB1, 8, dt_bg);
}

/* --- Mouse cursor ---------------------------------------------------- */
/* Inverted-cell cursor: instead of replacing the cell glyph with a block
 * (which "deletes" whatever character was underneath and made the text
 * appear to flicker as the mouse wobbled), we keep the original glyph
 * and just swap its fg/bg attribute. The user always sees the real
 * content under the cursor -- there's no text loss when the cursor
 * moves over a window. */
static int mx_prev = -1, my_prev = -1;
static u16 mx_save;

static void cursor_restore(void) {
    if (mx_prev < 0) return;
    u8 ch = mx_save & 0xFF;
    u8 attr = (mx_save >> 8) & 0xFF;
    vga_put_cell(my_prev, mx_prev, ch, attr & 0x0F, (attr >> 4) & 0x0F);
    mx_prev = my_prev = -1;
}

static void cursor_draw(int col, int row) {
    u8 ch, attr;
    vga_get_cell_raw(row, col, &ch, &attr);
    mx_save = (u16)ch | ((u16)attr << 8);
    u8 fg = attr & 0x0F;
    u8 bg = (attr >> 4) & 0x0F;
    if (cursor_style == 1) {
        /* Arrow glyph (CP437 slot 0x01, custom bitmap installed at
         * boot in vga_init -> install_arrow_glyph). Painted in black
         * on the existing cell background so it shows against any
         * window/wallpaper colour. The character underneath is
         * temporarily hidden -- same as every other GUI cursor. */
        vga_put_cell(row, col, 0x01, 0, bg);
    } else {
        /* Inverted attribute: swap fg/bg so the same glyph still
         * renders but with reversed colours -- the classic text-mode
         * cursor look, preserves the character underneath. */
        vga_put_cell(row, col, ch, bg, fg);
    }
    mx_prev = col; my_prev = row;
}

/* --- Generic dropdown menu ------------------------------------------ */
/* Draws a bordered popup of `count` items anchored under column col0,
 * tracks hover + keyboard, returns the chosen index or -1 on cancel.
 * Reusable for every top-bar menu. */
static int show_menu(const char *const *items, int count, int col0) {
    int w = 0;
    for (int i = 0; i < count; i++) {
        int l = (int)strlen(items[i]);
        if (l > w) w = l;
    }
    w += 4;
    int h = count + 2;
    int r0 = 1;
    int c0 = col0;
    if (c0 + w > VGA_COLS) c0 = VGA_COLS - w;
    if (c0 < 0) c0 = 0;
    /* Save the area we're about to overdraw. */
    static u16 save[16 * 40];
    for (int rr = 0; rr < h; rr++)
        for (int cc = 0; cc < w; cc++) {
            u8 ch, at;
            vga_get_cell_raw(r0 + rr, c0 + cc, &ch, &at);
            save[rr * w + cc] = (u16)ch | ((u16)at << 8);
        }
    /* Border. */
    vga_fill(r0, c0, w, h, ' ', menu_fg, menu_bg);
    for (int cc = 0; cc < w; cc++) {
        vga_put_cell(r0,     c0 + cc, 0xC4, menu_fg, menu_bg);
        vga_put_cell(r0+h-1, c0 + cc, 0xC4, menu_fg, menu_bg);
    }
    for (int rr = 0; rr < h; rr++) {
        vga_put_cell(r0 + rr, c0,     0xB3, menu_fg, menu_bg);
        vga_put_cell(r0 + rr, c0+w-1, 0xB3, menu_fg, menu_bg);
    }
    vga_put_cell(r0,     c0,     0xDA, menu_fg, menu_bg);
    vga_put_cell(r0,     c0+w-1, 0xBF, menu_fg, menu_bg);
    vga_put_cell(r0+h-1, c0,     0xC0, menu_fg, menu_bg);
    vga_put_cell(r0+h-1, c0+w-1, 0xD9, menu_fg, menu_bg);

    int sel = 0;
    int result = -1;
    int prev_mc = -1, prev_mr = -1;
    cursor_restore();
    for (;;) {
        for (int i = 0; i < count; i++) {
            u8 fg = (i == sel) ? menu_sel_fg : menu_fg;
            u8 bg = (i == sel) ? menu_sel_bg : menu_bg;
            vga_fill (r0 + 1 + i, c0 + 1, w - 2, 1, ' ', fg, bg);
            vga_write(r0 + 1 + i, c0 + 2, items[i], fg, bg);
        }
        int mc, mr, mb;
        mouse_get(&mc, &mr, &mb);
        /* Restore the previous cursor cell BEFORE drawing the new one --
         * otherwise every cursor position leaves a magenta block trail. */
        if (mc != prev_mc || mr != prev_mr) {
            cursor_restore();
            cursor_draw(mc, mr);
            prev_mc = mc; prev_mr = mr;
        }
        if (mb & 1) {
            cursor_restore();
            while (mb & 1) mouse_get(&mc, &mr, &mb);
            if (mr > r0 && mr < r0 + h - 1 && mc > c0 && mc < c0 + w - 1) {
                result = mr - r0 - 1;
                if (result >= count) result = -1;
            }
            break;
        }
        if (mr > r0 && mr < r0 + h - 1 && mc > c0 && mc < c0 + w - 1) {
            int new_sel = mr - r0 - 1;
            if (new_sel >= 0 && new_sel < count) sel = new_sel;
        }
        int k = kb_trygetc();
        if (k == 27)                 { cursor_restore(); break; }
        if (k == KB_UP   && sel > 0)   sel--;
        if (k == KB_DOWN && sel < count - 1) sel++;
        if (k == '\n' || k == '\r')  { cursor_restore(); result = sel; break; }
        vga_present();
        __asm__ volatile ("hlt");
    }
    /* Restore saved area. */
    for (int rr = 0; rr < h; rr++)
        for (int cc = 0; cc < w; cc++) {
            u16 v = save[rr * w + cc];
            vga_put_cell(r0 + rr, c0 + cc, v & 0xFF,
                         v >> 8 & 0x0F, (v >> 8) >> 4 & 0x0F);
        }
    return result;
}

/* Categorised app launcher. First click shows the four category folders;
 * pick one to see the apps in it. ESC backs out of either level.
 * Returns the chosen APP_* index, or -1. */
static int show_zenbite_menu(int col0) {
    for (;;) {
        int cat = show_menu(cat_label, CAT_COUNT, col0);
        if (cat < 0) return -1;
        /* Collect apps in this category. */
        const char *sub_labels[APP_COUNT];
        int         sub_app   [APP_COUNT];
        int sub_n = 0;
        for (int i = 0; i < APP_COUNT; i++) {
            if (app_defs[i].label && app_defs[i].category == cat) {
                sub_labels[sub_n] = app_defs[i].label;
                sub_app   [sub_n] = i;
                sub_n++;
            }
        }
        if (sub_n == 0) continue;
        int sel = show_menu(sub_labels, sub_n, col0 + 12);
        if (sel < 0) continue;        /* back out to category list */
        return sub_app[sel];
    }
}

/* --- About box ------------------------------------------------------- */
static void run_about(void) {
    int w = 50, h = 11;
    int r = (VGA_ROWS - h) / 2, c = (VGA_COLS - w) / 2;
    draw_window(r, c, w, h, "About Zenbite");
    vga_write(r + 2, c + 3, "Zenbite v" ZENBITE_VERSION, 1, win_bg);
    vga_write(r + 3, c + 3, "32-bit retro operating system, MIT", win_fg, win_bg);
    vga_write(r + 5, c + 3, "(c) 2026 Oliver Petz and contributors", win_fg, win_bg);
    vga_write(r + 7, c + 3, "Built from scratch -- bootloader,", win_fg, win_bg);
    vga_write(r + 8, c + 3, "kernel, FAT16/32, TCP/IP, shell, GUI.", win_fg, win_bg);
    vga_write(r + h - 1, c + 3, "[ Press any key to close ]", 8, win_bg);
    kb_getc();
}

/* --- Files app ------------------------------------------------------- */
static void files_draw_list(int r, int c, int w, int rows, int n,
                            struct fs_dirent *ents, int top, int sel) {
    cursor_restore();
    for (int i = 0; i < rows; i++) {
        int idx = top + i;
        vga_fill(r + 1 + i, c + 1, w - 2, 1, ' ', win_fg, win_bg);
        if (idx >= n) continue;
        char line[80];
        const char *kind = (ents[idx].attr & FS_ATTR_DIR) ? "<DIR>" : "     ";
        ksnprintf(line, sizeof line, " %s %-12s %8u",
                  kind, ents[idx].name, ents[idx].size);
        u8 fg = (idx == sel) ? win_bg : win_fg;
        u8 bg = (idx == sel) ? win_fg : win_bg;
        int len = (int)strlen(line);
        for (int x = 0; x < w - 2; x++)
            vga_put_cell(r + 1 + i, c + 1 + x,
                         x < len ? line[x] : ' ', fg, bg);
    }
}

static void run_files(int r, int c, int w, int h) {
    draw_window(r, c, w, h, "Files");
    int dh = fs_opendir(".");
    if (dh < 0) {
        vga_write(r + 2, c + 2, "Cannot open current directory.", 4, win_bg);
        kb_getc();
        return;
    }
    static struct fs_dirent ents[128];
    int n = 0;
    while (n < 128 && fs_readdir(dh, &ents[n])) n++;
    fs_closedir(dh);
    int rows = h - 2;
    int top = 0, sel = 0;
    int prev_mc = -1, prev_mr = -1;
    int dirty = 1;                       /* draw the list once up front */
    for (;;) {
        if (dirty) {
            files_draw_list(r, c, w, rows, n, ents, top, sel);
            /* re-draw cursor on top after a list repaint */
            prev_mc = -1;
            dirty = 0;
        }
        int mc, mr, mb;
        mouse_get(&mc, &mr, &mb);
        if (mc != prev_mc || mr != prev_mr) {
            cursor_restore();
            cursor_draw(mc, mr);
            prev_mc = mc; prev_mr = mr;
        }
        int k = kb_trygetc();
        if (k == 27) { cursor_restore(); break; }
        if (k == KB_UP   && sel > 0)     { sel--; if (sel < top) top = sel; dirty = 1; }
        if (k == KB_DOWN && sel < n - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; dirty = 1; }
        vga_present();
        __asm__ volatile ("hlt");
    }
}

/* --- Read-line for in-window text input ----------------------------- */
static int read_line_box(int r, int c, int w, char *buf, int max) {
    int len = (int)strlen(buf);
    if (len > max - 1) len = max - 1;
    for (;;) {
        vga_fill(r, c, w, 1, ' ', win_fg, win_bg);
        vga_write(r, c, buf, win_fg, win_bg);
        vga_put_cell(r, c + len, '_', win_fg, win_bg);
        int k = kb_getc();
        if (k == '\n' || k == '\r') { buf[len] = '\0'; return len; }
        if (k == 27)                return -1;
        if (k == '\b' && len > 0)   { len--; buf[len] = '\0'; continue; }
        if (k >= ' ' && k < 127 && len < max - 1) { buf[len++] = (char)k; buf[len] = '\0'; }
    }
}

/* --- Web app --------------------------------------------------------- */
/* If the user typed a bare query (no scheme), turn it into a search
 * URL.  We use Frogfind (http://frogfind.com) -- a search front-end
 * designed for retro browsers, serving plain HTML over HTTP.  Google
 * itself redirects http -> https and we can't follow into TLS.
 * Spaces become '+'; characters outside [A-Za-z0-9._~-] are
 * percent-encoded so the server's parser doesn't choke on punctuation. */
static void build_google_url(const char *query, char *out, int max) {
    const char *prefix = "http://frogfind.com/?q=";
    int n = 0;
    while (*prefix && n < max - 1) out[n++] = *prefix++;
    for (const char *p = query; *p && n < max - 4; p++) {
        char ch = *p;
        if (ch == ' ') { out[n++] = '+'; continue; }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '.' || ch == '_' || ch == '~' || ch == '-') {
            out[n++] = ch;
        } else {
            const char *hex = "0123456789ABCDEF";
            out[n++] = '%';
            out[n++] = hex[(u8)ch >> 4];
            out[n++] = hex[(u8)ch & 0x0F];
        }
    }
    out[n] = '\0';
}

static void run_web(int r, int c, int w, int h) {
    draw_window(r, c, w, h, "Web (Google)");
    char input[128] = "";
    vga_write(r + 1, c + 2, "Search or URL:", win_fg, win_bg);
    int n = read_line_box(r + 1, c + 17, w - 19, input, sizeof input);
    if (n < 0) return;
    char url[256];
    if (strncmp(input, "http://", 7) == 0) {
        /* Use as-is. */
        int i = 0;
        while (input[i] && i < (int)sizeof url - 1) { url[i] = input[i]; i++; }
        url[i] = '\0';
    } else {
        build_google_url(input, url, sizeof url);
    }
    vga_fill (r + 2, c + 2, w - 4, 1, ' ', win_fg, win_bg);
    vga_write(r + 2, c + 2, url, 8, win_bg);
    vga_write(r + 3, c + 2, "Loading ...", 8, win_bg);
    vga_present();
    int got = http_get(url, "INDEX.HTM");
    char st[80];
    ksnprintf(st, sizeof st, "Fetched %d bytes -> A:\\INDEX.HTM", got);
    vga_fill(r + 3, c + 1, w - 2, 1, ' ', win_fg, win_bg);
    vga_write(r + 3, c + 2, st, (got > 0 ? 2 : 4), win_bg);
    int fh = fs_open("INDEX.HTM");
    if (fh < 0) { vga_write(r + h - 1, c + 2, "[any key]", 8, win_bg); kb_getc(); return; }
    static char body[8192];
    int len = fs_read(fh, body, sizeof body - 1);
    fs_close(fh);
    if (len < 0) len = 0;
    body[len] = '\0';

    /* --- Tiny HTML renderer.
     *   * Tags are skipped (everything from '<' to '>').
     *   * <script>/<style> bodies are skipped entirely.
     *   * <br>, <p>, <div>, <li>, <tr>, headings -> linebreaks.
     *   * Common &entities; are decoded.
     *   * Whitespace runs are collapsed to one space.
     * Output is reflowed into the window body cell-by-cell so it acts
     * like a real (very) basic browser instead of a hex dump. */
    static char text[8192];
    int tl = 0;
    int in_tag = 0;
    int in_script = 0, in_style = 0;
    int last_space = 1;            /* suppress leading whitespace */
    for (int i = 0; i < len && tl < (int)sizeof text - 1; i++) {
        char ch = body[i];
        if (in_script || in_style) {
            /* Look for closing tag. */
            const char *end = in_script ? "</script" : "</style";
            int el = (int)strlen(end);
            if (i + el < len) {
                int ok = 1;
                for (int j = 0; j < el; j++) {
                    char a = body[i + j], b = end[j];
                    if (a >= 'A' && a <= 'Z') a += 32;
                    if (a != b) { ok = 0; break; }
                }
                if (ok) { in_script = in_style = 0; i += el - 1; in_tag = 1; }
            }
            continue;
        }
        if (in_tag) {
            if (ch == '>') {
                in_tag = 0;
                /* If we just saw a block-level open, insert a newline. */
                /* Simple heuristic: the tag we just skipped lives at
                 *   body[tag_start..i]. We re-scan it locally. */
            }
            continue;
        }
        if (ch == '<') {
            /* Inspect the tag start to decide if it's block-level or
             * a script/style block. */
            int j = i + 1;
            while (j < len && body[j] == ' ') j++;
            int closing = (j < len && body[j] == '/');
            if (closing) j++;
            char tag[12]; int tl2 = 0;
            while (j < len && tl2 < (int)sizeof tag - 1 &&
                   ((body[j] >= 'a' && body[j] <= 'z') ||
                    (body[j] >= 'A' && body[j] <= 'Z') ||
                    (body[j] >= '0' && body[j] <= '9'))) {
                char ck = body[j++];
                if (ck >= 'A' && ck <= 'Z') ck += 32;
                tag[tl2++] = ck;
            }
            tag[tl2] = '\0';
            if (strcmp(tag, "script") == 0 && !closing) in_script = 1;
            else if (strcmp(tag, "style") == 0 && !closing) in_style = 1;
            else if (strcmp(tag, "br") == 0 || strcmp(tag, "p") == 0 ||
                     strcmp(tag, "div") == 0 || strcmp(tag, "li") == 0 ||
                     strcmp(tag, "tr") == 0 || strcmp(tag, "h1") == 0 ||
                     strcmp(tag, "h2") == 0 || strcmp(tag, "h3") == 0 ||
                     strcmp(tag, "h4") == 0 || strcmp(tag, "ul") == 0 ||
                     strcmp(tag, "ol") == 0 || strcmp(tag, "hr") == 0 ||
                     strcmp(tag, "title") == 0) {
                if (!last_space) { text[tl++] = '\n'; last_space = 1; }
            }
            in_tag = 1;
            continue;
        }
        if (ch == '&') {
            /* Decode a small entity set. */
            const struct { const char *name; char ch; } ents[] = {
                {"amp;",  '&'}, {"lt;",   '<'}, {"gt;",   '>'},
                {"quot;", '"'}, {"apos;", '\''}, {"nbsp;", ' '},
            };
            int matched = 0;
            for (size_t e = 0; e < sizeof ents / sizeof ents[0]; e++) {
                int el = (int)strlen(ents[e].name);
                if (i + 1 + el <= len &&
                    strncmp(body + i + 1, ents[e].name, (size_t)el) == 0) {
                    text[tl++] = ents[e].ch;
                    i += el;
                    matched = 1;
                    last_space = (ents[e].ch == ' ');
                    break;
                }
            }
            if (matched) continue;
            /* Unknown entity: drop. */
            while (i < len && body[i] != ';' && body[i] != ' ') i++;
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
            if (!last_space) { text[tl++] = ' '; last_space = 1; }
            continue;
        }
        if (ch < ' ' || (u8)ch > 126) continue;
        text[tl++] = ch;
        last_space = 0;
    }
    text[tl] = '\0';

    /* Word-wrap the rendered text into the window body. */
    int row = r + 5, col = c + 2, lw = w - 4, ll = 0;
    int max_row = r + h - 2;
    int i = 0;
    while (i < tl && row < max_row) {
        if (text[i] == '\n') { row++; ll = 0; i++; continue; }
        /* Measure next word. */
        int wlen = 0;
        while (i + wlen < tl && text[i + wlen] != ' ' && text[i + wlen] != '\n')
            wlen++;
        if (wlen == 0) { i++; continue; }
        if (ll + wlen > lw) { row++; ll = 0; if (row >= max_row) break; }
        for (int k = 0; k < wlen && row < max_row; k++) {
            vga_put_cell(row, col + ll, text[i + k], win_fg, win_bg);
            ll++;
        }
        i += wlen;
        if (i < tl && text[i] == ' ') {
            if (ll < lw) { vga_put_cell(row, col + ll++, ' ', win_fg, win_bg); }
            i++;
        }
    }
    char st2[80];
    ksnprintf(st2, sizeof st2, "[ %d B HTML rendered | any key ]", tl);
    vga_write(r + h - 1, c + 2, st2, 8, win_bg);
    kb_getc();
}

/* --- Terminal REPL --------------------------------------------------- */
#define TERM_HISTORY 16

static int term_scroll_if_needed(int *row, int r, int c, int w, int h) {
    int max_row = r + h - 2;
    if (*row < max_row) return 0;
    for (int rr = r + 1; rr < max_row; rr++) {
        for (int cc = c + 1; cc < c + w - 1; cc++) {
            u8 ch, attr;
            vga_get_cell_raw(rr + 1, cc, &ch, &attr);
            vga_put_cell(rr, cc, ch, attr & 0x0F, (attr >> 4) & 0x0F);
        }
    }
    for (int cc = c + 1; cc < c + w - 1; cc++)
        vga_put_cell(max_row, cc, ' ', win_fg, win_bg);
    *row = max_row;
    return 1;
}

static int term_read_line(int row, int col, int w, char *buf, int max,
                          char history[][128], int hist_count, int *hist_pos) {
    int len = 0;
    buf[0] = '\0';
    for (;;) {
        vga_fill(row, col, w, 1, ' ', win_fg, win_bg);
        vga_write(row, col, "$ ", 2, win_bg);
        vga_write(row, col + 2, buf, win_fg, win_bg);
        vga_put_cell(row, col + 2 + len, '_', win_fg, win_bg);
        int k = kb_getc();
        if (k == 27) return -1;
        if (k == '\n' || k == '\r') { buf[len] = '\0'; return len; }
        if (k == '\b' && len > 0) { len--; buf[len] = '\0'; continue; }
        if (k == (int)KB_UP && *hist_pos > 0) {
            (*hist_pos)--;
            int idx = (hist_count - 1 - *hist_pos + TERM_HISTORY) % TERM_HISTORY;
            int hl = (int)strlen(history[idx]); if (hl > max - 1) hl = max - 1;
            memcpy(buf, history[idx], (size_t)hl); buf[hl] = '\0'; len = hl;
            continue;
        }
        if (k == (int)KB_DOWN && *hist_pos < hist_count) {
            (*hist_pos)++;
            if (*hist_pos == hist_count) { buf[0] = '\0'; len = 0; }
            else {
                int idx = (hist_count - 1 - *hist_pos + TERM_HISTORY) % TERM_HISTORY;
                int hl = (int)strlen(history[idx]); memcpy(buf, history[idx], (size_t)hl);
                buf[hl] = '\0'; len = hl;
            }
            continue;
        }
        if (k >= ' ' && k < 127 && len < max - 1) {
            buf[len++] = (char)k; buf[len] = '\0';
        }
    }
}

static void run_terminal(int r, int c, int w, int h) {
    draw_window(r, c, w, h, "Terminal");
    vga_write(r + 1, c + 2,
              "Zenbite Terminal -- ESC closes, UP/DOWN history",
              8, win_bg);
    int row = r + 3, col = c + 2;
    static char captured[4096];
    static char history[TERM_HISTORY][128];
    int hist_count = 0, hist_pos = 0;
    for (;;) {
        term_scroll_if_needed(&row, r, c, w, h);
        char line[128] = "";
        hist_pos = hist_count;
        int n = term_read_line(row, col, w - 4, line, sizeof line,
                               history, hist_count, &hist_pos);
        if (n < 0) break;
        row++; term_scroll_if_needed(&row, r, c, w, h);
        if (line[0] == '\0') continue;
        int last = (hist_count - 1 + TERM_HISTORY) % TERM_HISTORY;
        if (hist_count == 0 || strcmp(history[last], line) != 0) {
            int slot = hist_count % TERM_HISTORY;
            int len = (int)strlen(line); if (len > 127) len = 127;
            memcpy(history[slot], line, (size_t)len); history[slot][len] = '\0';
            hist_count++;
        }
        u32 cl = 0;
        vga_redirect(captured, sizeof captured, &cl);
        shell_run_line(line);
        vga_redirect(NULL, 0, NULL);
        int line_w = w - 4, ll = 0;
        for (u32 i = 0; i < cl; i++) {
            char ch = captured[i];
            if (ch == '\n' || ll >= line_w) {
                row++; term_scroll_if_needed(&row, r, c, w, h);
                ll = 0;
                if (ch == '\n') continue;
            }
            if (ch < ' ' || ch > 126) continue;
            vga_put_cell(row, col + ll, ch, win_fg, win_bg); ll++;
        }
        if (ll > 0) { row++; term_scroll_if_needed(&row, r, c, w, h); }
    }
}

/* --- New apps -------------------------------------------------------- */
static void run_calculator(int r, int c, int w, int h);
static void run_clock_app (int r, int c, int w, int h);
static void run_sysmon    (int r, int c, int w, int h);
static void run_notes     (int r, int c, int w, int h);
static void run_settings  (int r, int c, int w, int h);

/* --- Launch one app -------------------------------------------------- */
/* Returns 1 if the caller needs to force a full desktop redraw (the app
 * was modal and took over the screen). Returns 0 for widget spawns,
 * which compose on the desktop and don't require a teardown. */
static int launch_app(int which) {
    cursor_restore();
    int w = 70, h = VGA_ROWS - 4;
    int c = (VGA_COLS - w) / 2;
    int r = 2;
    switch (which) {
        /* Persistent widgets: live on the desktop, multiple at once,
         * draggable, close via the red dot on their title bar. */
        case APP_CLOCK:    spawn_widget(WK_CLOCK);    return 0;
        case APP_SYSMON:   spawn_widget(WK_SYSMON);   return 0;
        case APP_CALC:     spawn_widget(WK_MINICALC); return 0;
        case APP_ABOUT:    spawn_widget(WK_ABOUT);    return 0;
        case APP_NOTES:    spawn_widget(WK_NOTES);    return 0;
        case APP_FILES:    spawn_widget(WK_FILES);    return 0;
        case APP_SETTINGS: spawn_widget(WK_SETTINGS); return 0;
        case APP_TERMINAL: spawn_widget(WK_TERMINAL); return 0;
        case APP_WEB:      spawn_widget(WK_WEB);      return 0;
        case APP_DISKMGR:  spawn_widget(WK_DISKMGR);  return 0;
        case APP_CALENDAR: spawn_widget(WK_CALENDAR); return 0;
        case APP_SNAKE:    spawn_widget(WK_SNAKE);    return 0;
        case APP_TETRIS:   spawn_widget(WK_TETRIS);   return 0;
        case APP_MINES:    spawn_widget(WK_MINES);    return 0;
        case APP_ANALOG:   spawn_widget(WK_ANALOG);   return 0;
        case APP_NETSCAN:  spawn_widget(WK_NETSCAN);  return 0;
        case APP_PARTMGR:  spawn_widget(WK_PARTMGR);  return 0;
        case APP_PROGRAMS: spawn_widget(WK_PROGRAMS); return 0;
        case APP_REBOOT: {
            extern void vga_shadow_flush(void);
            extern void reboot(void);
            tui_end();
            vga_shadow_flush();
            vga_clear();
            kputs("Rebooting ...\n");
            for (volatile int i = 0; i < 5000000; i++);
            reboot();
        }
        case APP_SHUTDOWN: {
            extern void vga_shadow_flush(void);
            extern void shutdown(void);
            tui_end();
            vga_shadow_flush();
            vga_clear();
            kputs("Shutting down ...\n");
            for (volatile int i = 0; i < 5000000; i++);
            shutdown();
        }
        case APP_EDITOR: {
            tui_end();   /* edit_main is fullscreen / shell-driven */
            kputs("\n");
            char path[FS_PATH_MAX] = "UNTITLED.TXT";
            kprintf("Path: %s (ENTER to keep, type new path): ", path);
            char p[FS_PATH_MAX] = ""; int pl = 0;
            for (;;) {
                int k = kb_getc();
                if (k == '\n' || k == '\r') break;
                if (k == '\b' && pl > 0) { pl--; p[pl] = '\0'; kputs("\b \b"); continue; }
                if (k >= ' ' && k < 127 && pl < (int)sizeof p - 1) { p[pl++] = (char)k; p[pl] = '\0'; kputc((char)k); }
            }
            kputs("\n");
            if (pl) memcpy(path, p, (size_t)pl + 1);
            edit_main(path);
            tui_init();    /* re-enter desktop fullscreen */
            break;
        }
    }
    return 1;     /* modal app: caller must full-redraw the desktop */
}

/* ====================================================================
 *  Calculator (modal full-window). Reuses calc_expr/term/atom from
 *  the Mini-Calc widget definitions earlier in the file.
 * ==================================================================== */
static void run_calculator(int r, int c, int w, int h) {
    draw_window(r, c, w, h, "Calculator");
    vga_write(r + 1, c + 2, "Type an expression (e.g. (2+3)*7), ENTER to compute.",
              win_fg, win_bg);
    vga_write(r + 2, c + 2, "Operators: + - * /  parens  integer only. ESC closes.",
              8, win_bg);
    int row = r + 4;
    for (;;) {
        char line[80] = "";
        vga_fill (row, c + 2, w - 4, 1, ' ', win_fg, win_bg);
        vga_write(row, c + 2, "> ", 2, win_bg);
        int n = read_line_box(row, c + 4, w - 6, line, sizeof line);
        if (n < 0) return;
        if (n == 0) continue;
        g_calc_p = line;
        int v = calc_expr();
        char out[64];
        ksnprintf(out, sizeof out, "  = %d", v);
        row++;
        if (row >= r + h - 1) {
            /* Scroll the result region: simple clear & restart at top. */
            for (int rr = r + 4; rr < r + h - 1; rr++)
                vga_fill(rr, c + 1, w - 2, 1, ' ', win_fg, win_bg);
            row = r + 4;
            vga_write(row, c + 2, "> ", 2, win_bg);
            vga_write(row, c + 4, line, win_fg, win_bg);
            row++;
        }
        vga_write(row, c + 2, out, 2, win_bg);
        row++;
    }
}

/* ====================================================================
 *  Big-digit Clock
 * ==================================================================== */
/* 5x3 pixel font for digits 0-9 and ':'. Each row is a 3-char string;
 * '#' = filled, ' ' = empty. */
static const char *clock_font[11][5] = {
    {"###","# #","# #","# #","###"},  /* 0 */
    {"  #","  #","  #","  #","  #"},  /* 1 */
    {"###","  #","###","#  ","###"},  /* 2 */
    {"###","  #","###","  #","###"},  /* 3 */
    {"# #","# #","###","  #","  #"},  /* 4 */
    {"###","#  ","###","  #","###"},  /* 5 */
    {"###","#  ","###","# #","###"},  /* 6 */
    {"###","  #","  #","  #","  #"},  /* 7 */
    {"###","# #","###","# #","###"},  /* 8 */
    {"###","# #","###","  #","###"},  /* 9 */
    {"   "," # ","   "," # ","   "},  /* : */
};
static void draw_big_digit(int row, int col, int d, u8 fg, u8 bg) {
    if (d < 0 || d > 10) return;
    for (int rr = 0; rr < 5; rr++) {
        for (int cc = 0; cc < 3; cc++) {
            char p = clock_font[d][rr][cc];
            vga_put_cell(row + rr, col + cc * 2,     p == '#' ? 0xDB : ' ', fg, bg);
            vga_put_cell(row + rr, col + cc * 2 + 1, p == '#' ? 0xDB : ' ', fg, bg);
        }
    }
}
static void run_clock_app(int r, int c, int w, int h) {
    draw_window(r, c, w, h, "Clock");
    vga_write(r + h - 1, c + 2, "[ ESC closes ]", 8, win_bg);
    int prev_sec = -1;
    for (;;) {
        struct rtc_time t; rtc_read(&t);
        if ((int)t.sec != prev_sec) {
            int dx = c + (w - 38) / 2, dy = r + 3;
            vga_fill(dy, dx, 38, 5, ' ', win_fg, win_bg);
            int x = dx;
            draw_big_digit(dy, x, t.hour / 10, 1, win_bg); x += 7;
            draw_big_digit(dy, x, t.hour % 10, 1, win_bg); x += 7;
            draw_big_digit(dy, x, 10,           1, win_bg); x += 7;
            draw_big_digit(dy, x, t.min / 10,  1, win_bg); x += 7;
            draw_big_digit(dy, x, t.min % 10,  1, win_bg);
            char date[32];
            ksnprintf(date, sizeof date, "%04u-%02u-%02u  %02u:%02u:%02u",
                      (u32)t.year, (u32)t.month, (u32)t.day,
                      (u32)t.hour, (u32)t.min, (u32)t.sec);
            vga_fill (dy + 7, c + 2, w - 4, 1, ' ', win_fg, win_bg);
            int dl = (int)strlen(date);
            vga_write(dy + 7, c + (w - dl) / 2, date, win_fg, win_bg);
            prev_sec = t.sec;
        }
        int k = kb_trygetc();
        if (k == 27) return;
        vga_present();
        __asm__ volatile ("hlt");
    }
}

/* ====================================================================
 *  System Monitor
 * ==================================================================== */
static void run_sysmon(int r, int c, int w, int h) {
    draw_window(r, c, w, h, "System Monitor");
    vga_write(r + h - 1, c + 2, "[ ESC closes / live ]", 8, win_bg);
    int prev_sec = -1;
    for (;;) {
        struct rtc_time t; rtc_read(&t);
        if ((int)t.sec != prev_sec) {
            for (int rr = r + 1; rr < r + h - 1; rr++)
                vga_fill(rr, c + 1, w - 2, 1, ' ', win_fg, win_bg);
            int row = r + 2;
            vga_write(row++, c + 2, "Memory (kernel heap)", 1, win_bg);
            u32 used = kheap_used_kib(), tot = kheap_total_kib();
            char line[80];
            ksnprintf(line, sizeof line, "  %u / %u KiB used  (%u%%)",
                      used, tot, tot ? (used * 100 / tot) : 0u);
            vga_write(row++, c + 2, line, win_fg, win_bg);
            /* bar */
            int bw = w - 8;
            int filled = tot ? (int)((used * bw) / tot) : 0;
            for (int x = 0; x < bw; x++)
                vga_put_cell(row, c + 4 + x, x < filled ? 0xDB : 0xB0,
                             (x < filled ? 4 : 8), win_bg);
            row += 2;

            vga_write(row++, c + 2, "Disks", 1, win_bg);
            int dr = row;
            for (int i = 0; i < DISK_MAX && dr < r + h - 4; i++) {
                struct disk *d = disk_get(i);
                if (!d || !d->present) continue;
                ksnprintf(line, sizeof line,
                          "  %-4s %u sectors (%u KiB)",
                          d->name, d->sectors, d->sectors / 2);
                vga_write(dr++, c + 2, line, win_fg, win_bg);
            }
            ksnprintf(line, sizeof line, "Time: %04u-%02u-%02u  %02u:%02u:%02u",
                      (u32)t.year, (u32)t.month, (u32)t.day,
                      (u32)t.hour, (u32)t.min, (u32)t.sec);
            vga_write(r + h - 2, c + 2, line, 8, win_bg);
            prev_sec = t.sec;
        }
        int k = kb_trygetc();
        if (k == 27) return;
        vga_present();
        __asm__ volatile ("hlt");
    }
}

/* ====================================================================
 *  Notes: simple text area saved to A:\NOTES.TXT on close.
 * ==================================================================== */
#define NOTES_PATH "NOTES.TXT"
#define NOTES_CAP  2048
static void run_notes(int r, int c, int w, int h) {
    draw_window(r, c, w, h, "Notes");
    vga_write(r + 1, c + 2,
              "Notes (auto-saves to A:\\NOTES.TXT on close). ESC to close.",
              8, win_bg);
    static char buf[NOTES_CAP + 1];
    int len = 0;
    int fh = fs_open(NOTES_PATH);
    if (fh >= 0) {
        len = fs_read(fh, buf, NOTES_CAP);
        if (len < 0) len = 0;
        fs_close(fh);
    }
    buf[len] = '\0';
    int row0 = r + 3, col0 = c + 2;
    int rows = h - 5, cols = w - 4;
    int redraw = 1;
    for (;;) {
        if (redraw) {
            for (int rr = 0; rr < rows; rr++)
                vga_fill(row0 + rr, col0, cols, 1, ' ', win_fg, win_bg);
            int rr = 0, cc = 0;
            for (int i = 0; i < len && rr < rows; i++) {
                char ch = buf[i];
                if (ch == '\n' || cc >= cols) { rr++; cc = 0; if (ch == '\n') continue; }
                if (ch >= ' ' && ch < 127 && rr < rows)
                    vga_put_cell(row0 + rr, col0 + cc++, ch, win_fg, win_bg);
            }
            /* cursor block at end */
            if (rr < rows)
                vga_put_cell(row0 + rr, col0 + cc, '_', win_fg, win_bg);
            redraw = 0;
        }
        vga_present();
        int k = kb_getc();
        if (k == 27) break;
        if (k == '\b' && len > 0) { len--; buf[len] = '\0'; redraw = 1; continue; }
        if ((k == '\n' || (k >= ' ' && k < 127)) && len < NOTES_CAP) {
            buf[len++] = (char)k; buf[len] = '\0'; redraw = 1;
        }
    }
    /* Save on close. */
    fs_create(NOTES_PATH);
    int wh = fs_open(NOTES_PATH);
    if (wh >= 0) { fs_write(wh, buf, len); fs_close(wh); }
}

/* ====================================================================
 *  Settings: tabbed pane (Background / Theme / Date+Time / Network)
 * ==================================================================== */
static int settings_select(const char *title, const char *const *opts,
                           int n, int sel, int r, int c, int w) {
    /* Simple horizontal-arrows picker: <  current  > */
    char line[80];
    ksnprintf(line, sizeof line, "%-20s  <  %-22s  >",
              title, opts[sel < 0 ? 0 : (sel >= n ? n - 1 : sel)]);
    vga_fill (r, c, w, 1, ' ', win_fg, win_bg);
    vga_write(r, c, line, win_fg, win_bg);
    return sel;
}

static void settings_datetime_pane(int r, int c, int w) {
    struct rtc_time t; rtc_read(&t);
    char line[80];
    ksnprintf(line, sizeof line, "Now: %04u-%02u-%02u %02u:%02u:%02u",
              (u32)t.year, (u32)t.month, (u32)t.day,
              (u32)t.hour, (u32)t.min, (u32)t.sec);
    vga_fill (r,     c, w, 1, ' ', win_fg, win_bg);
    vga_write(r,     c, line, win_fg, win_bg);
    vga_write(r + 1, c, "Press 'S' to set time (YYYY-MM-DD HH:MM:SS)",
              8, win_bg);
}

static void settings_set_time(int r, int c, int w) {
    char in[32] = "";
    vga_fill (r, c, w, 1, ' ', win_fg, win_bg);
    vga_write(r, c, "New time: ", 1, win_bg);
    int n = read_line_box(r, c + 10, w - 12, in, sizeof in);
    if (n <= 0) return;
    /* Parse YYYY-MM-DD HH:MM:SS leniently. */
    int yr = 0, mo = 0, da = 0, hr = 0, mi = 0, se = 0;
    int idx = 0;
    int *fields[6] = { &yr, &mo, &da, &hr, &mi, &se };
    int fi = 0;
    while (in[idx] && fi < 6) {
        while (in[idx] && (in[idx] < '0' || in[idx] > '9')) idx++;
        if (!in[idx]) break;
        int v = 0;
        while (in[idx] >= '0' && in[idx] <= '9') {
            v = v * 10 + (in[idx] - '0'); idx++;
        }
        *fields[fi++] = v;
    }
    if (yr < 2000 || mo < 1 || mo > 12 || da < 1 || da > 31) return;
    struct rtc_time nt = {
        .sec = (u8)se, .min = (u8)mi, .hour = (u8)hr,
        .day = (u8)da, .month = (u8)mo, .year = (u16)yr,
    };
    rtc_write(&nt);
}

static void settings_network_pane(int r, int c, int w) {
    extern ip4_addr_t dns_server;
    struct net_iface *n = net_iface();
    char ip[16] = "?", gw[16] = "?", ns[16] = "?";
    if (n) {
        ip4_format(n->ip, ip);
        ip4_format(n->gateway, gw);
    }
    ip4_format(dns_server, ns);
    char line[80];
    ksnprintf(line, sizeof line, "IP:     %s", ip);
    vga_fill (r,     c, w, 1, ' ', win_fg, win_bg);
    vga_write(r,     c, line, win_fg, win_bg);
    ksnprintf(line, sizeof line, "Gate:   %s", gw);
    vga_fill (r + 1, c, w, 1, ' ', win_fg, win_bg);
    vga_write(r + 1, c, line, win_fg, win_bg);
    ksnprintf(line, sizeof line, "DNS:    %s", ns);
    vga_fill (r + 2, c, w, 1, ' ', win_fg, win_bg);
    vga_write(r + 2, c, line, win_fg, win_bg);
    vga_write(r + 4, c, "Press 'D' to change DNS server.", 8, win_bg);
}

static void settings_set_dns(int r, int c, int w) {
    char in[20] = "8.8.8.8";
    vga_fill (r, c, w, 1, ' ', win_fg, win_bg);
    vga_write(r, c, "DNS: ", 1, win_bg);
    int n = read_line_box(r, c + 5, w - 7, in, sizeof in);
    if (n <= 0) return;
    net_set_dns(ip4_parse(in));
}

static void run_settings(int r, int c, int w, int h) {
    static const char *tab_label[4] = {
        "Background", "Theme", "Date/Time", "Network"
    };
    int tab = 0;
    int prev_clk = -1;
    for (;;) {
        /* Tab strip + window chrome. */
        draw_window(r, c, w, h, "Settings");
        vga_write(r + h - 1, c + 2,
                  "[ TAB switch | LEFT/RIGHT change | ESC close ]",
                  8, win_bg);
        int x = c + 2;
        for (int i = 0; i < 4; i++) {
            int len = (int)strlen(tab_label[i]) + 2;
            u8 fg = (i == tab) ? win_bg  : win_fg;
            u8 bg = (i == tab) ? win_fg  : win_bg;
            vga_fill (r + 1, x, len, 1, ' ', fg, bg);
            vga_write(r + 1, x + 1, tab_label[i], fg, bg);
            x += len + 1;
        }
        /* Pane body. */
        for (int rr = r + 3; rr < r + h - 1; rr++)
            vga_fill(rr, c + 2, w - 4, 1, ' ', win_fg, win_bg);
        if (tab == 0) {
            settings_select("Pattern",       wallpaper_label, WALLPAPER_STYLE_COUNT,
                            wallpaper_style, r + 3, c + 2, w - 4);
            settings_select("Background col",bg_color_label,  6, current_bg_color,
                            r + 5, c + 2, w - 4);
            vga_write(r + 7, c + 2,
                      "LEFT/RIGHT on the focused row changes value.",
                      8, win_bg);
            vga_write(r + 8, c + 2,
                      "UP/DOWN moves between rows.", 8, win_bg);
        } else if (tab == 1) {
            settings_select("Theme", theme_label, THEME_COUNT, current_theme,
                            r + 3, c + 2, w - 4);
        } else if (tab == 2) {
            settings_datetime_pane(r + 3, c + 2, w - 4);
        } else {
            settings_network_pane(r + 3, c + 2, w - 4);
        }
        vga_present();
        int k = kb_getc();
        if (k == 27) return;
        if (k == '\t') { tab = (tab + 1) % 4; continue; }
        if (tab == 0) {
            static int focus_row = 0;
            if (k == (int)KB_UP   && focus_row > 0) focus_row--;
            if (k == (int)KB_DOWN && focus_row < 1) focus_row++;
            if (focus_row == 0) {
                if (k == (int)KB_LEFT) {
                    wallpaper_style = (wallpaper_style + WALLPAPER_STYLE_COUNT - 1)
                                      % WALLPAPER_STYLE_COUNT;
                    wall_img_loaded = 0;
                }
                if (k == (int)KB_RIGHT) {
                    wallpaper_style = (wallpaper_style + 1) % WALLPAPER_STYLE_COUNT;
                    wall_img_loaded = 0;
                }
            } else {
                if (k == (int)KB_LEFT)  current_bg_color = (current_bg_color + 5) % 6;
                if (k == (int)KB_RIGHT) current_bg_color = (current_bg_color + 1) % 6;
                dt_bg = bg_color_value[current_bg_color];
            }
        } else if (tab == 1) {
            if (k == (int)KB_LEFT)  apply_theme((current_theme + THEME_COUNT - 1) % THEME_COUNT);
            if (k == (int)KB_RIGHT) apply_theme((current_theme + 1) % THEME_COUNT);
        } else if (tab == 2) {
            if (k == 's' || k == 'S') settings_set_time(r + 6, c + 2, w - 4);
        } else if (tab == 3) {
            if (k == 'd' || k == 'D') settings_set_dns(r + 7, c + 2, w - 4);
        }
        (void)prev_clk;
    }
}

/* --- Main loop ------------------------------------------------------- */
int desktop_main(int argc, char **argv) {
    (void)argc; (void)argv;
    tui_init();
    mouse_set_bounds(VGA_COLS, VGA_ROWS);

    /* Reset widget table; spawn the Welcome window. */
    for (int i = 0; i < MAX_WIDGETS; i++) widgets[i].used = 0;
    next_z = 1;
    add_welcome_widget();
    place_desktop_icons();
    /* Launch any apps marked as autostart in CONFIG.TXT. Only apps that
     * map to a persistent widget (not a modal) make sense here, so we
     * call launch_app() which dispatches accordingly. */
    for (int i = 0; i < APP_COUNT; i++) {
        if (autostart_apps[i] && app_defs[i].label) launch_app(i);
    }

    int prev_mc = -1, prev_mr = -1;
    int full_redraw = 1;
    int dragging = -1;             /* widget index being dragged, -1 = none */
    int resizing = -1;             /* widget index being resized,  -1 = none */
    int drag_off_c = 0, drag_off_r = 0;
    int was_pressed = 0;

    for (;;) {
        /* If a Files widget asked us to open a file in the editor, do
         * it here -- between frames, with a clean cursor and no half-
         * drawn widget on screen. */
        if (files_open_pending) {
            files_open_pending = 0;
            cursor_restore();
            tui_end();
            edit_main(files_open_path);
            tui_init();
            full_redraw = 1;
        }
        if (full_redraw) {
            mx_prev = -1;
            draw_bar(-1);
            draw_wallpaper();
            draw_all_widgets();
            draw_taskbar();
            int mc0, mr0;
            mouse_get(&mc0, &mr0, NULL);
            cursor_draw(mc0, mr0);
            prev_mc = mc0; prev_mr = mr0;
            full_redraw = 0;
        }

        /* Live widgets refresh only when their second has actually
         * ticked, OR when something marked them dirty. Re-rendering
         * every frame writes hundreds of cells into the shadow buffer
         * which -- even with the differential present -- can show up
         * as periodic flicker on real hardware where VGA writes race
         * the CRT scan. The clock cell on the top bar is included. */
        struct rtc_time _t; rtc_read(&_t);
        int sec_now = (int)_t.sec;
        for (int i = 0; i < MAX_WIDGETS; i++) {
            struct widget *w = &widgets[i];
            if (!w->used) continue;
            int needs = w->dirty;
            if (w->kind == WK_CLOCK || w->kind == WK_SYSMON ||
                w->kind == WK_ANALOG ||
                (w->kind == WK_SETTINGS && w->tab == 2)) {
                if (sec_now != w->prev_sec) { needs = 1; w->prev_sec = sec_now; }
            }
            /* Tetris / Snake / Net scanner all drive their own clock
             * via pit_ticks, so they need a fresh frame whenever
             * focused (or, for net scan, while a scan is in flight). */
            if (w->kind == WK_SNAKE && w->focused) needs = 1;
            if (w->kind == WK_TETRIS && w->focused) needs = 1;
            if (w->kind == WK_NETSCAN && w->row == 1) needs = 1;
            if (needs) { render_one(w); w->dirty = 0; }
        }
        static int prev_clock_min = -1;
        int mins = _t.hour * 60 + _t.min;
        if (mins != prev_clock_min) {
            if (my_prev == 0 && mx_prev >= VGA_COLS - 5) cursor_restore();
            draw_clock();
            prev_clock_min = mins;
        }

        /* Mouse + cursor. */
        int mc, mr, mb;
        mouse_get(&mc, &mr, &mb);
        if (mc != prev_mc || mr != prev_mr) {
            cursor_restore();
            cursor_draw(mc, mr);
            prev_mc = mc; prev_mr = mr;
        }

        /* Right-click dispatch. Edge-triggered so it fires once per
         * press. Three context menus:
         *   - empty desktop  -> Refresh / Wallpaper / About
         *   - icon           -> Open / Properties
         *   - widget chrome  -> Minimize / Close
         * Wallpaper picks cycle through the styles. */
        int rpressed = (mb & 2) != 0;
        static int rwas = 0;
        if (rpressed && !rwas) {
            cursor_restore();
            while (mb & 2) mouse_get(NULL, NULL, &mb);
            int ih = icon_hit(mc, mr);
            int wh = widget_at(mc, mr);
            if (ih >= 0 && wh < 0) {
                static const char *menu_icon[] = { "Open", "Properties" };
                int it = show_menu(menu_icon, 2, mc);
                if (it == 0) { if (launch_app(icons[ih].app)) full_redraw = 1; }
                else if (it == 1) {
                    char msg[64];
                    ksnprintf(msg, sizeof msg, "%s -- app id %d",
                              icons[ih].label, icons[ih].app);
                    /* Re-use the existing alert via a temporary mini-window.
                     * For now just print to terminal scrollback would need
                     * a widget; cheapest is a status flash. */
                    draw_status(msg);
                }
            } else if (wh >= 0) {
                static const char *menu_win[] = { "Minimize", "Close" };
                int it = show_menu(menu_win, 2, mc);
                if (it == 0) { minimize_widget(wh); full_redraw = 1; }
                else if (it == 1) {
                    if (wh != 0) close_widget(wh);     /* don't kill Welcome */
                    full_redraw = 1;
                }
            } else {
                static const char *menu_desk[] = {
                    "Refresh", "Next wallpaper", "About Zenbite",
                };
                int it = show_menu(menu_desk, 3, mc);
                if (it == 0) full_redraw = 1;
                else if (it == 1) {
                    wallpaper_style = (wallpaper_style + 1)
                                      % WALLPAPER_STYLE_COUNT;
                    wall_img_loaded = 0;
                    full_redraw = 1;
                    extern void config_save(void); config_save();
                }
                else if (it == 2) { spawn_widget(WK_ABOUT); full_redraw = 1; }
            }
            draw_wallpaper(); draw_bar(-1); draw_all_widgets();
            prev_mc = -1; prev_mr = -1;
            vga_present();
        }
        rwas = rpressed;

        /* --- Window dragging / resizing / focus / close --- */
        int pressed = (mb & 1) != 0;
        if (pressed && !was_pressed) {
            /* Start button on the taskbar opens the categorised app
             * menu -- same path as F9 or the top-bar logo. We catch
             * this BEFORE the chip click so the leftmost portion of
             * the bottom bar always means "Start", not "first chip". */
            if (mr == VGA_ROWS - 1 && mc < START_BTN_WIDTH) {
                cursor_restore();
                while (mb & 1) mouse_get(NULL, NULL, &mb);
                int app = show_zenbite_menu(0);
                if (app >= 0) { if (launch_app(app)) full_redraw = 1; }
                draw_wallpaper(); draw_bar(-1); draw_all_widgets();
                prev_mc = -1; prev_mr = -1;
                was_pressed = 0;
                vga_present();
                continue;
            }
            /* Desktop icon click -- only when not on a window. We
             * test icon_hit first; if it matches and there's no
             * widget overlapping the same cell, we launch the app.
             * Single click launches (KDE-style); chosen over the
             * XP double-click because text-mode cursors are jittery
             * and double-click detection needs timed state. */
            {
                int ih = icon_hit(mc, mr);
                if (ih >= 0 && widget_at(mc, mr) < 0) {
                    cursor_restore();
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    if (launch_app(icons[ih].app)) full_redraw = 1;
                    prev_mc = -1; prev_mr = -1;
                    was_pressed = 0;
                    vga_present();
                    continue;
                }
            }
            /* Taskbar chip click -- restore (if minimized) and focus. */
            int tb_idx = taskbar_hit(mc, mr);
            if (tb_idx >= 0) {
                if (widgets[tb_idx].minimized) unminimize_widget(tb_idx);
                else { raise_widget(tb_idx); focus_only(tb_idx); }
                cursor_restore();
                draw_wallpaper();
                draw_bar(-1);
                draw_all_widgets();
                prev_mc = -1; prev_mr = -1;
                while (mb & 1) mouse_get(NULL, NULL, &mb);
                was_pressed = 0;
                vga_present();
                continue;
            }
            int hit = widget_at(mc, mr);
            if (hit >= 0) {
                struct widget *hw = &widgets[hit];
                /* Close button on the title bar? */
                if (widget_close_hit(hit, mc, mr)) {
                    close_widget(hit);
                    cursor_restore();
                    draw_wallpaper();
                    draw_bar(-1);
                    draw_all_widgets();
                    prev_mc = -1; prev_mr = -1;
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    was_pressed = 0;
                    vga_present();
                    continue;
                }
                /* Minimize (yellow dot)? Hide the window; user can
                 * restore it by clicking its taskbar entry. */
                if (widget_min_hit(hit, mc, mr)) {
                    minimize_widget(hit);
                    cursor_restore();
                    draw_wallpaper();
                    draw_bar(-1);
                    draw_all_widgets();
                    prev_mc = -1; prev_mr = -1;
                    while (mb & 1) mouse_get(NULL, NULL, &mb);
                    was_pressed = 0;
                    vga_present();
                    continue;
                }
                raise_widget(hit);
                focus_only(hit);
                for (int i = 0; i < MAX_WIDGETS; i++)
                    if (widgets[i].used) widgets[i].dirty = 1;
                /* Resize handle: bottom-right corner cell of resizable
                 * widgets (Files / Notes / Settings / Sysmon / Terminal /
                 * Web). Click + drag from there changes w/h. */
                int is_resizable = !(hw->kind == WK_WELCOME ||
                                     hw->kind == WK_CLOCK   ||
                                     hw->kind == WK_MINICALC||
                                     hw->kind == WK_ABOUT);
                if (is_resizable &&
                    mr == hw->r + hw->h - 1 && mc == hw->c + hw->w - 1) {
                    resizing = hit;
                } else if (widget_titlebar_hit(hit, mc, mr)) {
                    dragging   = hit;
                    drag_off_c = mc - hw->c;
                    drag_off_r = mr - hw->r;
                } else if (hw->kind == WK_FILES) {
                    /* Click on a body row in either pane: switch the
                     * focused pane to that side, select the row, then
                     * activate (cd / open). The file list is re-read
                     * inside files_hit so the index matches what was
                     * drawn last frame. */
                    int pane;
                    int row = files_hit(hw, mc, mr, &pane);
                    if (row >= 0) {
                        struct files_state *fs = (struct files_state *)hw->content;
                        if (hw->content_len == 0) files_state_init(hw);
                        fs->active = (u8)pane;
                        fs->sel[pane] = fs->top[pane] + row;
                        files_pane_activate(hw, pane);
                        hw->dirty = 1;
                    }
                } else if (hw->kind == WK_MINES) {
                    /* Reveal cell under cursor. Right-click = flag. */
                    int rr, cc;
                    if (mine_hit(hw, mc, mr, &rr, &cc) == 0) {
                        struct mine_state *ms = (struct mine_state *)hw->content;
                        hw->sel = rr; hw->top = cc;
                        if (mb & 2) mine_flag(ms, rr, cc);
                        else        mine_reveal(hw, ms, rr, cc);
                        hw->dirty = 1;
                    }
                }
            } else {
                focus_only(-1);
            }
        }
        if (resizing >= 0) {
            struct widget *w = &widgets[resizing];
            if (!pressed) {
                resizing = -1;
            } else {
                int nw = mc - w->c + 1;
                int nh = mr - w->r + 1;
                if (nw < 16) nw = 16;
                if (nh < 5)  nh = 5;
                if (w->c + nw > VGA_COLS) nw = VGA_COLS - w->c;
                if (w->r + nh > VGA_ROWS - 1) nh = VGA_ROWS - 1 - w->r;
                if (nw != w->w || nh != w->h) {
                    w->w = nw; w->h = nh;
                    cursor_restore();
                    draw_wallpaper();
                    draw_bar(-1);
                    draw_all_widgets();
                    prev_mc = -1; prev_mr = -1;
                }
                was_pressed = pressed;
                if (mc != prev_mc || mr != prev_mr) {
                    cursor_restore();
                    cursor_draw(mc, mr);
                    prev_mc = mc; prev_mr = mr;
                }
                vga_present();
                continue;
            }
        }
        if (dragging >= 0) {
            struct widget *w = &widgets[dragging];
            if (!pressed) {
                dragging = -1;
            } else {
                int nc = mc - drag_off_c;
                int nr = mr - drag_off_r;
                if (nc < 0) nc = 0;
                if (nc + w->w > VGA_COLS) nc = VGA_COLS - w->w;
                if (nr < 1) nr = 1;
                if (nr + w->h > VGA_ROWS - 1) nr = VGA_ROWS - 1 - w->h;
                if (nc != w->c || nr != w->r) {
                    w->c = nc; w->r = nr;
                    /* Repaint just the moving frame -- wallpaper +
                     * everything that overlapped. With the differential
                     * present this updates only the actually-changed
                     * cells and stays smooth. */
                    cursor_restore();
                    draw_wallpaper();
                    draw_all_widgets();
                    prev_mc = -1; prev_mr = -1;
                }
                was_pressed = pressed;
                /* CRITICAL: present BEFORE the continue, otherwise the
                 * window position only flushes to VGA on release, which
                 * looks like the window "teleporting" to the cursor. */
                if (mc != prev_mc || mr != prev_mr) {
                    cursor_restore();
                    cursor_draw(mc, mr);
                    prev_mc = mc; prev_mr = mr;
                }
                vga_present();
                continue;
            }
        }
        was_pressed = pressed;

        /* Click on a menu-bar title? */
        if (mb & 1) {
            int hit = bar_hit(mc, mr);
            /* wait for release */
            while (mb & 1) mouse_get(NULL, NULL, &mb);
            if (hit == 0) {                          /* Zenbite -> apps */
                int app = show_zenbite_menu(bar_x[0]);
                if (app >= 0) { if (launch_app(app)) full_redraw = 1; }
            } else if (hit == 1) {                   /* File */
                int it = show_menu(file_menu, FILE_MENU_N, bar_x[1]);
                if (it == 0)      { launch_app(APP_EDITOR);   full_redraw = 1; }
                else if (it == 1) { launch_app(APP_FILES);    full_redraw = 1; }
                else if (it == 2) { cursor_restore(); tui_end();
                                    kputs("Zenbite Desktop exited.\n"); return 0; }
            } else if (hit == 2) {                   /* View */
                int it = show_menu(view_menu, VIEW_MENU_N, bar_x[2]);
                if (it == 0)      { full_redraw = 1; }            /* Refresh */
                else if (it == 1) { launch_app(APP_TERMINAL); full_redraw = 1; }
                else if (it == 2) { launch_app(APP_WEB);      full_redraw = 1; }
                else if (it == 3) { spawn_widget(WK_DISKMGR);  full_redraw = 1; }
                else if (it == 4) { spawn_widget(WK_CALENDAR); full_redraw = 1; }
                else if (it == 5) { spawn_widget(WK_SNAKE);    full_redraw = 1; }
            } else if (hit == 3) {                   /* Help */
                int it = show_menu(help_menu, HELP_MENU_N, bar_x[3]);
                if (it == 0) {                                   /* commands */
                    cursor_restore();
                    int w = 60, h = 16;
                    int r = 2, c = (VGA_COLS - w) / 2;
                    draw_window(r, c, w, h, "Shell commands");
                    static char cap[4096]; u32 cl = 0;
                    vga_redirect(cap, sizeof cap, &cl);
                    shell_run_line("help");
                    vga_redirect(NULL, 0, NULL);
                    int row = r + 1, col = c + 2, lw = w - 4, ll = 0;
                    for (u32 i = 0; i < cl && row < r + h - 1; i++) {
                        char ch = cap[i];
                        if (ch == '\n' || ll >= lw) { row++; ll = 0; if (ch=='\n') continue; }
                        if (ch < ' ' || ch > 126) continue;
                        vga_put_cell(row, col + ll, ch, win_fg, win_bg); ll++;
                    }
                    vga_write(r + h - 1, c + 2, "[ any key ]", 8, win_bg);
                    kb_getc();
                    full_redraw = 1;
                } else if (it == 1) { run_about(); full_redraw = 1; }
            }
            continue;
        }

        /* Keyboard -- routed to the focused widget first; whatever
         * isn't handled falls through to the global hot-keys (ESC,
         * F-keys). */
        int k = kb_trygetc();
        if (k >= 0) {
            int fi = -1;
            for (int i = 0; i < MAX_WIDGETS; i++)
                if (widgets[i].used && widgets[i].focused) { fi = i; break; }
            if (fi >= 0 && k != 27) {
                struct widget *fw = &widgets[fi];
                int handled = 1;
                /* Cross-app clipboard. Ctrl+C/Ctrl+X copy the widget's
                 * primary text (Notes -> content, others -> input).
                 * Ctrl+V pastes into the same target. We intercept
                 * before the per-kind switch so every text widget
                 * behaves the same. */
                if (k == 3 || k == 24) {       /* ^C / ^X */
                    if (fw->kind == WK_NOTES)
                        clipboard_set(fw->content, fw->content_len);
                    else
                        clipboard_set(fw->input, fw->input_len);
                    if (k == 24) {              /* cut: also clear */
                        if (fw->kind == WK_NOTES) {
                            fw->content_len = 0; fw->content[0] = '\0';
                        } else {
                            fw->input_len = 0;   fw->input[0]   = '\0';
                        }
                    }
                    fw->dirty = 1;
                    k = -1;     /* consumed */
                } else if (k == 22) {           /* ^V paste */
                    char buf[256];
                    int n = clipboard_get(buf, sizeof buf);
                    if (fw->kind == WK_NOTES) {
                        for (int j = 0; j < n &&
                             fw->content_len < (int)sizeof fw->content - 1; j++)
                            fw->content[fw->content_len++] = buf[j];
                        fw->content[fw->content_len] = '\0';
                    } else {
                        for (int j = 0; j < n &&
                             fw->input_len < (int)sizeof fw->input - 1; j++) {
                            char c2 = buf[j];
                            if (c2 == '\n' || c2 == '\r') continue;
                            fw->input[fw->input_len++] = c2;
                        }
                        fw->input[fw->input_len] = '\0';
                    }
                    fw->dirty = 1;
                    k = -1;
                }
                if (k < 0) goto kbd_done;
                switch (fw->kind) {
                case WK_MINICALC:
                    if (k == '\n' || k == '\r') {
                        g_calc_p = fw->input;
                        int v = calc_expr();
                        ksnprintf(fw->input, sizeof fw->input, "%d", v);
                        fw->input_len = (int)strlen(fw->input);
                    } else if (k == '\b' && fw->input_len > 0) {
                        fw->input[--fw->input_len] = '\0';
                    } else if (k >= ' ' && k < 127 &&
                               fw->input_len < (int)sizeof fw->input - 1) {
                        fw->input[fw->input_len++] = (char)k;
                        fw->input[fw->input_len] = '\0';
                    } else handled = 0;
                    break;
                case WK_NOTES:
                    if (k == '\b' && fw->content_len > 0) {
                        fw->content[--fw->content_len] = '\0';
                    } else if ((k == '\n' || (k >= ' ' && k < 127)) &&
                               fw->content_len < (int)sizeof fw->content - 1) {
                        fw->content[fw->content_len++] = (char)k;
                        fw->content[fw->content_len] = '\0';
                    } else handled = 0;
                    break;
                case WK_FILES:
                    if (!files_handle_key(fw, k)) handled = 0;
                    break;
                case WK_TERMINAL:
                    if (k == '\n' || k == '\r') {
                        /* Echo command into scrollback, run it, append
                         * captured output. shell_run_line() blocks for
                         * the duration of the command.
                         *
                         * The viewport is set to the widget's interior
                         * before the call: streaming kprintf output is
                         * still captured into scrollback (via redirect),
                         * but fullscreen apps like `evi` that draw with
                         * vga_put_cell/vga_fill have their coordinates
                         * translated into the widget box and clipped to
                         * its bounds. So `evi FOO.TXT` now renders an
                         * editor inside the Terminal window instead of
                         * stomping the whole desktop. */
                        if (fw->content_len + fw->input_len + 4
                              < (int)sizeof fw->content) {
                            fw->content[fw->content_len++] = '$';
                            fw->content[fw->content_len++] = ' ';
                            for (int j = 0; j < fw->input_len; j++)
                                fw->content[fw->content_len++] = fw->input[j];
                            fw->content[fw->content_len++] = '\n';
                        }
                        /* Handle 'exit' locally: close the terminal
                         * widget instead of calling the shell's exit
                         * (which powers off the machine). */
                        if (strcmp(fw->input, "exit") == 0 ||
                            strcmp(fw->input, "quit") == 0) {
                            fw->input_len = 0;
                            fw->input[0]  = '\0';
                            close_widget(fi);
                            full_redraw = 1;
                            break;   /* break out of the switch */
                        }
                        static char cap[4096];
                        u32 cl = 0;
                        /* Reserve the widget interior. Leave a 1-cell
                         * border so the window chrome stays visible. */
                        int vr = fw->r + 1;
                        int vc = fw->c + 1;
                        int vrows = fw->h - 2;
                        int vcols = fw->w - 2;
                        vga_view_set(vr, vc, vrows, vcols);
                        vga_redirect(cap, sizeof cap, &cl);
                        shell_run_line(fw->input);
                        vga_redirect(NULL, 0, NULL);
                        vga_view_clear();
                        for (u32 j = 0;
                             j < cl && fw->content_len < (int)sizeof fw->content - 1; j++)
                            fw->content[fw->content_len++] = cap[j];
                        fw->content[fw->content_len] = '\0';
                        fw->input_len = 0;
                        fw->input[0]  = '\0';
                        full_redraw = 1;
                    } else if (k == '\b' && fw->input_len > 0) {
                        fw->input[--fw->input_len] = '\0';
                    } else if (k >= ' ' && k < 127 &&
                               fw->input_len < (int)sizeof fw->input - 1) {
                        fw->input[fw->input_len++] = (char)k;
                        fw->input[fw->input_len]   = '\0';
                    } else handled = 0;
                    break;
                case WK_WEB:
                    if (k == (int)KB_PGUP) {
                        fw->top -= (fw->h - 4);
                        if (fw->top < 0) fw->top = 0;
                    } else if (k == (int)KB_PGDN) {
                        fw->top += (fw->h - 4);
                    } else if (k == (int)KB_UP)   { if (fw->top > 0) fw->top--; }
                    else if   (k == (int)KB_DOWN) { fw->top++; }
                    else if (k == '\n' || k == '\r') {
                        /* Reset scroll on new fetch. */
                        fw->top = 0;
                        char url[256];
                        if (strncmp(fw->input, "http://", 7) == 0) {
                            int j = 0;
                            while (fw->input[j] && j < (int)sizeof url - 1) {
                                url[j] = fw->input[j]; j++;
                            }
                            url[j] = '\0';
                        } else {
                            build_google_url(fw->input, url, sizeof url);
                        }
                        int got = http_get(url, "INDEX.HTM");
                        /* http_get logs status / DNS / TCP progress via
                         * kprintf direct to VGA, behind the shadow
                         * buffer. Invalidate so the next frame repaints
                         * every cell from shadow instead of leaving the
                         * leaked log text on screen. */
                        vga_invalidate();
                        if (got > 0) {
                            int fh = fs_open("INDEX.HTM");
                            if (fh >= 0) {
                                static char body[16384];
                                int n = fs_read(fh, body, sizeof body - 1);
                                fs_close(fh);
                                if (n < 0) n = 0;
                                body[n] = '\0';
                                /* Shared HTML->text renderer: strips
                                 * tags, skips <script>/<style>, decodes
                                 * entities, collapses whitespace. */
                                fw->content_len =
                                    html_render(body, n, fw->content,
                                                (int)sizeof fw->content);
                            }
                        }
                        full_redraw = 1;
                    } else if (k == '\b' && fw->input_len > 0) {
                        fw->input[--fw->input_len] = '\0';
                    } else if (k >= ' ' && k < 127 &&
                               fw->input_len < (int)sizeof fw->input - 1) {
                        fw->input[fw->input_len++] = (char)k;
                        fw->input[fw->input_len]   = '\0';
                    } else handled = 0;
                    break;
                case WK_SETTINGS:
                    if (k == '\t') { fw->tab = (fw->tab + 1) % 6; fw->row = 0; }
                    else if (fw->tab == 0) {
                        if (k == (int)KB_UP   && fw->row > 0) fw->row--;
                        else if (k == (int)KB_DOWN && fw->row < 3) fw->row++;
                        else if (fw->row == 0) {
                            if (k == (int)KB_LEFT)
                                wallpaper_style = (wallpaper_style + WALLPAPER_STYLE_COUNT - 1)
                                                  % WALLPAPER_STYLE_COUNT;
                            else if (k == (int)KB_RIGHT)
                                wallpaper_style = (wallpaper_style + 1) % WALLPAPER_STYLE_COUNT;
                            else handled = 0;
                            if (handled) { wall_img_loaded = 0; full_redraw = 1; config_save(); }
                        } else if (fw->row == 1) {
                            if (k == (int)KB_LEFT)  current_bg_color = (current_bg_color + 5) % 6;
                            else if (k == (int)KB_RIGHT) current_bg_color = (current_bg_color + 1) % 6;
                            else handled = 0;
                            if (handled) { dt_bg = bg_color_value[current_bg_color]; full_redraw = 1; config_save(); }
                        } else if (fw->row == 2) {
                            if (k == (int)KB_LEFT)  cursor_style = (cursor_style + 1) & 1;
                            else if (k == (int)KB_RIGHT) cursor_style = (cursor_style + 1) & 1;
                            else handled = 0;
                            if (handled) { full_redraw = 1; config_save(); }
                        } else {
                            /* Row 3: resolution toggle. LEFT/RIGHT both
                             * flip between 80x25 and 80x50; on success
                             * we have to:
                             *  - resize the desktop's tracking of
                             *    mouse bounds
                             *  - clamp any widgets that hung off the
                             *    old (taller / shorter) screen
                             *  - force a full redraw so the new mode
                             *    sees a fresh paint instead of a
                             *    stretched leftover from before. */
                            if (k == (int)KB_LEFT || k == (int)KB_RIGHT) {
                                /* 3-state cycle: 80x25 -> 80x50 -> 1280x720
                                 * graphics -> back to 80x25. RIGHT advances,
                                 * LEFT goes back. If the BGA mode-set fails
                                 * (no VBE -- e.g. real hardware without a
                                 * Bochs-compatible card) we skip that step
                                 * and keep the current mode. */
                                int cur;
                                if (vga_in_graphics_mode())            cur = 2;
                                else if (vga_get_rows() == 50)         cur = 1;
                                else                                   cur = 0;
                                int next = (k == (int)KB_RIGHT)
                                           ? (cur + 1) % 3
                                           : (cur + 2) % 3;
                                if (next == 0) {
                                    vga_set_text_mode(25);
                                } else if (next == 1) {
                                    vga_set_text_mode(50);
                                } else {
                                    if (vga_set_graphics(1280, 720) < 0) {
                                        /* Fall back: stay where we were. */
                                        next = cur;
                                    }
                                }
                                mouse_set_bounds(VGA_COLS, VGA_ROWS);
                                for (int wi = 0; wi < MAX_WIDGETS; wi++) {
                                    struct widget *ww = &widgets[wi];
                                    if (!ww->used) continue;
                                    if (ww->c + ww->w >= VGA_COLS)
                                        ww->c = VGA_COLS - ww->w;
                                    if (ww->c < 1) ww->c = 1;
                                    if (ww->r + ww->h >= VGA_ROWS - 1)
                                        ww->r = VGA_ROWS - 1 - ww->h;
                                    if (ww->r < 1) ww->r = 1;
                                }
                                full_redraw = 1;
                                config_save();
                            } else handled = 0;
                        }
                    } else if (fw->tab == 1) {
                        if (k == (int)KB_LEFT)  apply_theme((current_theme + THEME_COUNT - 1) % THEME_COUNT);
                        else if (k == (int)KB_RIGHT) apply_theme((current_theme + 1) % THEME_COUNT);
                        else handled = 0;
                        if (handled) { full_redraw = 1; config_save(); }
                    } else if (fw->tab == 4) {
                        /* Autostart pane. Row 0 is the global "Desktop
                         * at boot" toggle; rows 1..APP_COUNT map to
                         * autostart_apps[row-1]. */
                        int total = APP_COUNT + 1;
                        if (k == (int)KB_UP   && fw->row > 0) fw->row--;
                        else if (k == (int)KB_DOWN && fw->row < total - 1) fw->row++;
                        else if (k == ' ' || k == '\n' || k == '\r' ||
                                 k == (int)KB_LEFT || k == (int)KB_RIGHT) {
                            if (fw->row == 0) autostart_desktop ^= 1;
                            else              autostart_apps[fw->row - 1] ^= 1;
                            config_save();
                        }
                        else handled = 0;
                    } else if (fw->tab == 5) {
                        /* Security pane: same shape as Autostart --
                         * arrows pick a row, SPACE / ENTER / LEFT /
                         * RIGHT toggle it. */
                        int n = security_count();
                        if (k == (int)KB_UP   && fw->row > 0) fw->row--;
                        else if (k == (int)KB_DOWN && fw->row < n - 1) fw->row++;
                        else if (k == ' ' || k == '\n' || k == '\r' ||
                                 k == (int)KB_LEFT || k == (int)KB_RIGHT) {
                            security_set(fw->row, !security_get(fw->row));
                            config_save();
                        }
                        else handled = 0;
                    } else handled = 0;
                    break;
                case WK_CALENDAR:
                    if (k == (int)KB_LEFT)       fw->tab--;
                    else if (k == (int)KB_RIGHT) fw->tab++;
                    else if (k == (int)KB_HOME)  fw->tab = 0;
                    else handled = 0;
                    break;
                case WK_SNAKE:
                    if (k == (int)KB_UP    && fw->top != 1) fw->top = 3;
                    else if (k == (int)KB_DOWN  && fw->top != 3) fw->top = 1;
                    else if (k == (int)KB_LEFT  && fw->top != 0) fw->top = 2;
                    else if (k == (int)KB_RIGHT && fw->top != 2) fw->top = 0;
                    else if (k == 'r' || k == 'R') {
                        fw->content_len = 0;   /* triggers reset on next render */
                    } else handled = 0;
                    break;
                case WK_TETRIS:   if (!tet_handle_key(fw, k))      handled = 0; break;
                case WK_MINES:    if (!mine_handle_key(fw, k))     handled = 0; break;
                case WK_NETSCAN:  if (!netscan_handle_key(fw, k))  handled = 0; break;
                case WK_PARTMGR:  if (!partmgr_handle_key(fw, k))  handled = 0; break;
                case WK_PROGRAMS:
                    if (k == (int)KB_UP   && fw->sel > 0)  fw->sel--;
                    else if (k == (int)KB_DOWN)            fw->sel++;
                    else if (k == (int)KB_F5)  { fw->content_len = 0; }  /* rescan */
                    else if (k == '\n' || k == '\r') {
                        if (programs_launch(fw)) full_redraw = 1;
                    } else handled = 0;
                    break;
                case WK_DISKMGR:  if (!diskmgr_handle_key(fw, k))  handled = 0; break;
                default: handled = 0;
                }
                if (handled) { fw->dirty = 1; k = -1; }
            kbd_done: ;
            }
        }
        if (k == 27)              { cursor_restore(); break; }
        if (k == (int)KB_F1)      { launch_app(APP_FILES);    full_redraw = 1; }
        if (k == (int)KB_F2)      { launch_app(APP_WEB);      full_redraw = 1; }
        if (k == (int)KB_F3)      { launch_app(APP_TERMINAL); full_redraw = 1; }
        if (k == (int)KB_F4)      { launch_app(APP_EDITOR);   full_redraw = 1; }
        if (k == (int)KB_F5)      { spawn_widget(WK_CLOCK);    }
        if (k == (int)KB_F6)      { spawn_widget(WK_SYSMON);   }
        if (k == (int)KB_F7)      { spawn_widget(WK_MINICALC); }
        if (k == (int)KB_F9) {
            int app = show_zenbite_menu(3);
            if (app >= 0) { launch_app(app); full_redraw = 1; }
        }
        if (k == (int)KB_F8) {
            /* Minimize the focused widget. */
            for (int i = 0; i < MAX_WIDGETS; i++)
                if (widgets[i].used && widgets[i].focused) {
                    minimize_widget(i); full_redraw = 1; break;
                }
        }

        /* Flush back-buffer once per frame. All draw_* helpers wrote to
         * the shadow buffer; this single copy makes the new frame appear
         * atomically, killing the flicker. */
        /* Compose active TTY into shadow buffer if in TTY mode */
        extern int tty_initialized;
        extern void tty_compose(void);
        if (tty_initialized) tty_compose();
        vga_present();
        /* Wait for the next IRQ (timer ~100 Hz, mouse, keyboard) rather
         * than busy-spinning -- gives us up to 100 frames/sec with zero
         * CPU when idle. */
        __asm__ volatile ("hlt");
    }
    tui_end();
    kputs("Zenbite Desktop exited.\n");
    return 0;
}
