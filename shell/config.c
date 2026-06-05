/* Persistent settings: keymap, theme, wallpaper, background colour,
 * cursor style, and the autostart-app list. Stored as plain text at
 * <system drive>\SYSTEM\CONFIG.TXT in key=value lines. The desktop
 * setters call config_save() after every change; kmain calls
 * config_load() once after fs_init so the loaded values are in effect
 * before the shell prints its banner.
 *
 * Keeping the file path under SYSTEM/ means installs that copy the
 * SYSTEM tree to a fresh disk also carry the user's preferences. */
#include "kernel.h"
#include "kio.h"
#include "vga.h"
#include "string.h"
#include "fs.h"

#define CONFIG_PATH "\\SYSTEM\\CONFIG.TXT"

/* Desktop-owned setters / getters -- declared static in desktop.c and
 * exposed here through these thin wrappers so config.c doesn't need
 * a header file for every theme variable. */
extern int  desktop_get_keymap   (char *out, int outsz);
extern void desktop_set_keymap   (const char *s);
extern int  desktop_get_theme    (void);
extern void desktop_set_theme    (int t);
extern int  desktop_get_wallpaper(void);
extern void desktop_set_wallpaper(int v);
extern int  desktop_get_bgcolor  (void);
extern void desktop_set_bgcolor  (int v);
extern int  desktop_get_cursor   (void);
extern void desktop_set_cursor   (int v);
extern int  desktop_get_rows     (void);
extern void desktop_set_rows     (int v);
extern int  desktop_get_start_desktop(void);
extern void desktop_set_start_desktop(int v);
extern int  desktop_get_start_gdesk(void);
extern void desktop_set_start_gdesk(int v);
extern int  desktop_get_lock_password(char *out, int outsz);
extern void desktop_set_lock_password(const char *s);
extern int  mouse_get_speed(void);
extern void mouse_set_speed(int s);
extern int  gdesk_get_res_w(void);
extern int  gdesk_get_res_h(void);
extern void gdesk_set_res(int w, int h);
extern int  kb_get_repeat_delay(void);
extern int  kb_get_repeat_rate(void);
extern void kb_set_repeat(int delay_ms, int rate_cps);
extern int  g_sound_enabled;
extern int  g_sound_vol;
extern int  gdesk_get_brightness(void);
extern void gdesk_set_brightness(int v);
extern int  gdesk_get_wallpaper_style(void);
extern void gdesk_set_wallpaper_style(int v);
extern int  gdesk_get_last_layout(char *out, int sz);
extern void gdesk_set_last_layout(const char *s);
extern int  security_count(void);
extern int  security_get(int i);
extern void security_set(int i, int v);
extern int  desktop_get_autostart(int idx);   /* idx 0..APP_COUNT_PUB-1 */
extern void desktop_set_autostart(int idx, int on);
extern int  desktop_app_name     (int idx, char *out, int outsz);
extern int  desktop_app_lookup   (const char *name);    /* -1 if unknown */
extern int  desktop_app_count_pub(void);

/* --- Parsing helpers --------------------------------------------------- */
static int eq_to_int(const char *s, int def) {
    int v = 0, sign = 1, any = 0;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; }
    return any ? sign * v : def;
}

static int read_full(const char *path, char *buf, int cap) {
    int h = fs_open(path);
    if (h < 0) return -1;
    int total = 0, n;
    while (total < cap - 1 &&
           (n = fs_read(h, buf + total, cap - 1 - total)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    fs_close(h);
    return total;
}

void config_load(void) {
    /* Walk drive letters A..D and pick the first one that has the
     * config file. This means a user who boots from a Setup disk
     * temporarily but has an installed drive still gets their
     * persisted settings. */
    char drv_saved = fs_get_drive();
    int loaded = 0;
    char buf[2048];
    for (char L = 'A'; L < 'A' + FS_DRIVE_MAX && !loaded; L++) {
        if (!fs_drive_present(L)) continue;
        fs_set_drive(L);
        int n = read_full(CONFIG_PATH, buf, (int)sizeof buf);
        if (n <= 0) continue;
        loaded = 1;
        /* Parse line by line. */
        char *p = buf;
        while (*p) {
            char *line = p;
            while (*p && *p != '\n' && *p != '\r') p++;
            char saved = *p;
            *p = '\0';
            if (*line && *line != '#') {
                char *eq = strchr(line, '=');
                if (eq) {
                    *eq = '\0';
                    const char *key = line;
                    const char *val = eq + 1;
                    if      (strcmp(key, "keymap")    == 0) desktop_set_keymap(val);
                    else if (strcmp(key, "theme")     == 0) desktop_set_theme    (eq_to_int(val, 0));
                    else if (strcmp(key, "wallpaper") == 0) desktop_set_wallpaper(eq_to_int(val, 1));
                    else if (strcmp(key, "bgcolor")   == 0) desktop_set_bgcolor  (eq_to_int(val, 0));
                    else if (strcmp(key, "cursor")    == 0) desktop_set_cursor   (eq_to_int(val, 1));
                    else if (strcmp(key, "rows")      == 0) desktop_set_rows     (eq_to_int(val, 25));
                    else if (strcmp(key, "start_desktop") == 0) desktop_set_start_desktop(eq_to_int(val, 0));
                    else if (strcmp(key, "start_gdesk") == 0)   desktop_set_start_gdesk  (eq_to_int(val, 1));
                    else if (strcmp(key, "lock_password") == 0) desktop_set_lock_password(val);
                    else if (strcmp(key, "mouse_speed") == 0)  mouse_set_speed(eq_to_int(val, 8));
                    else if (strcmp(key, "res_w") == 0) {
                        int w = eq_to_int(val, 800);
                        gdesk_set_res(w, gdesk_get_res_h());
                    } else if (strcmp(key, "res_h") == 0) {
                        int h = eq_to_int(val, 600);
                        gdesk_set_res(gdesk_get_res_w(), h);
                    } else if (strcmp(key, "sound") == 0) {
                        g_sound_enabled = eq_to_int(val, 1);
                    } else if (strcmp(key, "sound_vol") == 0) {
                        g_sound_vol = eq_to_int(val, 8);
                    } else if (strcmp(key, "kbd_repeat_delay") == 0) {
                        int d = eq_to_int(val, 500);
                        kb_set_repeat(d, kb_get_repeat_rate());
                    } else if (strcmp(key, "kbd_repeat_rate") == 0) {
                        int r = eq_to_int(val, 10);
                        kb_set_repeat(kb_get_repeat_delay(), r);
                    } else if (strcmp(key, "brightness") == 0) {
                        gdesk_set_brightness(eq_to_int(val, 100));
                    } else if (strcmp(key, "wallpaper_style") == 0) {
                        gdesk_set_wallpaper_style(eq_to_int(val, 1));
                    } else if (strcmp(key, "last_layout") == 0) {
                        gdesk_set_last_layout(val);
                    }
                    /* security_flags=01100 -- one digit per flag,
                     * left-to-right matches security_label order. */
                    else if (strcmp(key, "security_flags") == 0) {
                        int n = security_count();
                        for (int i = 0; i < n && val[i]; i++)
                            security_set(i, val[i] != '0');
                    }
                    else if (strcmp(key, "autostart") == 0) {
                        /* Clear all then turn on names from a comma list. */
                        int total = desktop_app_count_pub();
                        for (int i = 0; i < total; i++) desktop_set_autostart(i, 0);
                        const char *q = val;
                        while (*q) {
                            char name[24]; int nl = 0;
                            while (*q == ' ' || *q == ',') q++;
                            while (*q && *q != ',' && nl < (int)sizeof name - 1)
                                name[nl++] = *q++;
                            name[nl] = '\0';
                            if (nl > 0) {
                                int idx = desktop_app_lookup(name);
                                if (idx >= 0) desktop_set_autostart(idx, 1);
                            }
                        }
                    }
                }
            }
            *p = saved;
            while (*p == '\n' || *p == '\r') p++;
        }
    }
    if (drv_saved != '?') fs_set_drive(drv_saved);
}

static int write_str(int h, const char *s) {
    int n = (int)strlen(s);
    return fs_write(h, s, (size_t)n);
}

void config_save(void) {
    /* Write to whichever drive the user is currently on. Setup disks
     * are read-only in practice but we don't know that here; if the
     * write fails we silently give up -- saving prefs is best-effort. */
    char drv_saved = fs_get_drive();
    if (drv_saved == '?') return;
    fs_mkdir("SYSTEM");
    fs_unlink(CONFIG_PATH);
    if (fs_create(CONFIG_PATH) < 0) return;
    int h = fs_open(CONFIG_PATH);
    if (h < 0) return;
    char km[16]; desktop_get_keymap(km, sizeof km);
    char line[120];
    write_str(h, "# Zenbite settings -- written by Settings widget\n");
    ksnprintf(line, sizeof line, "keymap=%s\n", km);         write_str(h, line);
    ksnprintf(line, sizeof line, "theme=%d\n",     desktop_get_theme());     write_str(h, line);
    ksnprintf(line, sizeof line, "wallpaper=%d\n", desktop_get_wallpaper()); write_str(h, line);
    ksnprintf(line, sizeof line, "bgcolor=%d\n",   desktop_get_bgcolor());   write_str(h, line);
    ksnprintf(line, sizeof line, "cursor=%d\n",    desktop_get_cursor());    write_str(h, line);
    ksnprintf(line, sizeof line, "rows=%d\n",      desktop_get_rows());      write_str(h, line);
    ksnprintf(line, sizeof line, "start_desktop=%d\n", desktop_get_start_desktop()); write_str(h, line);
    ksnprintf(line, sizeof line, "start_gdesk=%d\n",   desktop_get_start_gdesk());   write_str(h, line);
    ksnprintf(line, sizeof line, "mouse_speed=%d\n",   mouse_get_speed());           write_str(h, line);
    ksnprintf(line, sizeof line, "res_w=%d\n",         gdesk_get_res_w());           write_str(h, line);
    ksnprintf(line, sizeof line, "res_h=%d\n",         gdesk_get_res_h());           write_str(h, line);
    ksnprintf(line, sizeof line, "sound=%d\n",         g_sound_enabled);             write_str(h, line);
    ksnprintf(line, sizeof line, "sound_vol=%d\n",     g_sound_vol);                 write_str(h, line);
    ksnprintf(line, sizeof line, "kbd_repeat_delay=%d\n", kb_get_repeat_delay());    write_str(h, line);
    ksnprintf(line, sizeof line, "kbd_repeat_rate=%d\n",  kb_get_repeat_rate());     write_str(h, line);
    ksnprintf(line, sizeof line, "brightness=%d\n",    gdesk_get_brightness());      write_str(h, line);
    ksnprintf(line, sizeof line, "wallpaper_style=%d\n", gdesk_get_wallpaper_style()); write_str(h, line);
    {
        char layout[384];
        gdesk_get_last_layout(layout, sizeof layout);
        if (layout[0]) {
            char lline[420];
            ksnprintf(lline, sizeof lline, "last_layout=%s\n", layout);
            write_str(h, lline);
        }
    }
    {
        char pw[24];
        desktop_get_lock_password(pw, sizeof pw);
        /* Skip writing if empty -- lets the user clear the password by
         * leaving the field blank in Settings without it persisting as
         * "lock_password=" which is awkward to round-trip. */
        if (pw[0]) {
            ksnprintf(line, sizeof line, "lock_password=%s\n", pw);
            write_str(h, line);
        }
    }
    /* Pack the security flags into a digit string. */
    {
        char sec[16] = {0};
        int n = security_count();
        for (int i = 0; i < n && i < 15; i++)
            sec[i] = security_get(i) ? '1' : '0';
        ksnprintf(line, sizeof line, "security_flags=%s\n", sec);
        write_str(h, line);
    }
    /* Build autostart=name1,name2,... */
    char as_line[120];
    int pos = ksnprintf(as_line, sizeof as_line, "autostart=");
    int total = desktop_app_count_pub();
    int first = 1;
    for (int i = 0; i < total; i++) {
        if (!desktop_get_autostart(i)) continue;
        char name[24];
        if (desktop_app_name(i, name, sizeof name) < 0) continue;
        if (!first && pos < (int)sizeof as_line - 1) as_line[pos++] = ',';
        for (int j = 0; name[j] && pos < (int)sizeof as_line - 1; j++)
            as_line[pos++] = name[j];
        first = 0;
    }
    if (pos < (int)sizeof as_line - 1) as_line[pos++] = '\n';
    as_line[pos] = '\0';
    write_str(h, as_line);
    fs_close(h);
}
