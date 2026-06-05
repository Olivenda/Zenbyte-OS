/* desktop.c -- Shared desktop state and config accessors.
 *
 * The text-mode desktop has been removed. All UI is in desktop_g.c.
 * This file only provides the persistence helpers that config.c,
 * builtins.c, kmain.c, and desktop_g.c call into.
 */

#include "kernel.h"
#include "kio.h"
#include "string.h"
#include "vga.h"

/* ── Theme ──────────────────────────────────────────────────────────── */
static int current_theme;

/* apply_theme() refreshes any live palette/window colours after a
 * theme switch.  The graphical desktop (desktop_g.c) repaints on
 * its own; this is a no-op stub for config.c linkage. */
void apply_theme(int t) { (void)t; }

int  desktop_get_theme(void)  { return current_theme; }
void desktop_set_theme(int t) { current_theme = t; apply_theme(t); }

/* ── Wallpaper style ────────────────────────────────────────────────── */
static int wallpaper_style = 1;   /* 0=solid 1=gradient 2=diagonal */
#define WALLPAPER_STYLE_COUNT 3
int  desktop_get_wallpaper_style(void) { return wallpaper_style; }
void desktop_set_wallpaper_style(int v) {
    if (v >= 0 && v < WALLPAPER_STYLE_COUNT) wallpaper_style = v;
}

/* ── Keyboard layout passthrough ────────────────────────────────────── */
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

/* ── Autostart flags ────────────────────────────────────────────────── */
static u8 autostart_desktop;
int  desktop_get_start_desktop(void) { return autostart_desktop ? 1 : 0; }
void desktop_set_start_desktop(int v){ autostart_desktop = v ? 1 : 0; }

static u8 autostart_gdesk = 1;
int  desktop_get_start_gdesk(void) { return autostart_gdesk ? 1 : 0; }
void desktop_set_start_gdesk(int v){ autostart_gdesk = v ? 1 : 0; }

/* ── Lock-screen password ───────────────────────────────────────────── */
static char lock_password[24] = "zenbite";
int  desktop_get_lock_password(char *out, int outsz) {
    int i = 0;
    while (i < outsz - 1 && lock_password[i]) { out[i] = lock_password[i]; i++; }
    out[i] = '\0';
    return i;
}
void desktop_set_lock_password(const char *s) {
    int i = 0;
    while (s[i] && i < (int)sizeof lock_password - 1) { lock_password[i] = s[i]; i++; }
    lock_password[i] = '\0';
}

/* ── Desktop main (forward to graphical desktop) ────────────────────── */
extern int g_desktop_main(int argc, char **argv);
int desktop_main(int argc, char **argv) {
    return g_desktop_main(argc, argv);
}

/* ── Text-mode notify stub (for any legacy callers) ─────────────────── */
void notify(const char *title, const char *body, int kind) {
    (void)kind;
    kprintf("[%s] %s\n", title, body);
}

/* ── Wallpaper ──────────────────────────────────────────────────────── */
static int wallpaper_id = 1;
int  desktop_get_wallpaper(void) { return wallpaper_id; }
void desktop_set_wallpaper(int v){ wallpaper_id = v; }

/* ── Background colour (text-mode attribute index) ──────────────────── */
static int bg_color = 1;
int  desktop_get_bgcolor(void) { return bg_color; }
void desktop_set_bgcolor(int v){ bg_color = v; }

/* ── Cursor style ───────────────────────────────────────────────────── */
static int cursor_style = 1;
int  desktop_get_cursor(void) { return cursor_style; }
void desktop_set_cursor(int v){ cursor_style = v; }

/* ── Row count (text-mode lines) ────────────────────────────────────── */
static int row_count = 25;
int  desktop_get_rows(void) { return row_count; }
void desktop_set_rows(int v){ if (v >= 12 && v <= 50) row_count = v; }

/* ── Security flags ─────────────────────────────────────────────────── */
#define SECURITY_FLAG_COUNT 8
static u8 security_flags[SECURITY_FLAG_COUNT];
int  security_count(void) { return SECURITY_FLAG_COUNT; }
int  security_get(int i)  { return (i >= 0 && i < SECURITY_FLAG_COUNT) ? security_flags[i] : 0; }
void security_set(int i, int v){ if (i >= 0 && i < SECURITY_FLAG_COUNT) security_flags[i] = v ? 1 : 0; }

/* ── Autostart per-app flags ────────────────────────────────────────── */
#define APP_MAX 16
static u8 autostart_flags[APP_MAX];
int  desktop_get_autostart(int idx){ return (idx >= 0 && idx < APP_MAX) ? autostart_flags[idx] : 0; }
void desktop_set_autostart(int idx, int v){ if (idx >= 0 && idx < APP_MAX) autostart_flags[idx] = v ? 1 : 0; }

/* ── App table (name + description + action) ────────────────────────── */
struct app_entry {
    const char *name;
    const char *desc;
    void (*action)(void);
};
static struct app_entry apps[] = {
    { "Terminal",    "Command-line shell",           0 },
    { "Editor",      "Text file editor",             0 },
    { "File Manager","Browse files on disk",         0 },
    { "Web Browser", "Simple HTTP browser",          0 },
    { "Settings",    "System preferences",           0 },
    { "System Info", "Hardware and OS details",      0 },
    { "Tetris",      "Falling blocks game",          0 },
    { "About",       "About Zenbite",                0 },
    { "Activity",    "Process monitor",              0 },
};
#define APP_COUNT ((int)(sizeof apps / sizeof apps[0]))
int  desktop_app_count_pub(void) { return APP_COUNT; }
int  desktop_app_name(int idx, char *out, int outsz) {
    if (idx < 0 || idx >= APP_COUNT) return -1;
    int i = 0;
    const char *s = apps[idx].name;
    while (s[i] && i < outsz - 1) { out[i] = s[i]; i++; }
    out[i] = '\0';
    return i;
}
int  desktop_app_lookup(const char *name) {
    for (int i = 0; i < APP_COUNT; i++)
        if (strcmp(apps[i].name, name) == 0) return i;
    return -1;
}
