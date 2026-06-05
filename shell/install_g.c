/* Fully graphical installer -- all screens are pixel-rendered on the
 * BGA framebuffer. No text-mode fallback, no mode-switch. Steps:
 *   1. Welcome screen (Continue / Quit)
 *   2. Disk picker (scrollable list of present disks)
 *   3. Format + copy progress (step-by-step log + bar)
 *   4. Done screen (press key to reboot)
 *
 * Backend helpers (copy_file, install_bootloader, etc.) are provided
 * by install.c with non-static linkage.
 */
#include "kernel.h"
#include "kio.h"
#include "vga.h"
#include "string.h"
#include "fs.h"
#include "disk.h"
#include "mbr.h"

/* ── BGA / framebuffer externs ───────────────────────────────────── */
extern int  bga_present_active(void);
extern int  fb_w(void);
extern int  fb_h(void);
extern void fb_fill_rect(int x, int y, int w, int h, u32 color);
extern void fb_present(void);
extern void fb_hline(int x, int y, int w, u32 color);
extern void fb_vline(int x, int y, int h, u32 color);
extern void fb_draw_text(int x, int y, const char *s, u32 fg, u32 bg);
extern void fb_bevel_raised(int x, int y, int w, int h, u32 lt, u32 dk);
extern void fb_bevel_sunken(int x, int y, int w, int h, u32 lt, u32 dk);
extern void fb_hgradient(int x, int y, int w, int h, u32 l, u32 r);
extern void fb_blit_glyph(int x, int y, u8 glyph, u32 fg, u32 bg);
extern int  vga_set_graphics(int w, int h);
extern int  bga_best_mode(int *out_w, int *out_h);
extern void vga_set_text_mode(int rows);
extern int  kb_getc(void);
extern u32  pit_ticks(void);

/* ── Backend from install.c ───────────────────────────────────────── */
extern int  file_exists(char drive, const char *path);
extern int  copy_file(const char *src, const char *dst);
extern int  copy_tree(char src_drive, const char *src_dir,
                      char dst_drive, const char *dst_dir,
                      int (*progress)(const char *fname, int bytes));
extern int  install_bootloader(int target_disk, char target_letter);
extern void create_user_home(char target_letter, const char *username);
extern char find_source_dir(const char *dir, char exclude);
extern int  count_dir_files(char src_drive, const char *src_dir);
extern int  zb_write_file(const char *path, const void *buf, int n);
extern u8   stage1_blob[];
extern u32  stage1_blob_size;
extern u8   stage2_blob[];
extern u32  stage2_blob_size;
extern u8   stage1_hdd_blob[];
extern u32  stage1_hdd_blob_size;
extern u8   stage2_hdd_blob[];
extern u32  stage2_hdd_blob_size;
extern char _bss_start[];
extern int  fs_rescan(void);

#define FB_T 0xFF000000u   /* transparent */

/* ── Z mark (12x12) ──────────────────────────────────────────────── */
static const u8 z_mark[144] = {
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
static void draw_z(int x, int y, int s, u32 fg) {
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12; c++)
            if (z_mark[r*12+c])
                fb_fill_rect(x+c*s, y+r*s, s, s, fg);
}

/* ── Shared drawing helpers ───────────────────────────────────────── */
static void centered(const char *s, int cx, int ry, u32 fg) {
    int n = 0; while (s[n]) n++;
    fb_draw_text(cx - n*4, ry, s, fg, FB_T);
}
static void button(int x, int y, int w, int h, const char *label, int sel) {
    fb_fill_rect(x, y, w, h, 0xC0C8D0);
    sel ? fb_bevel_sunken(x,y,w,h,0xFFFFFF,0x404850)
        : fb_bevel_raised(x,y,w,h,0xFFFFFF,0x404850);
    int n = 0; while (label[n]) n++;
    fb_draw_text(x+(w-n*8)/2+(sel?1:0), y+(h-16)/2+(sel?1:0),
                 label, 0x000000, FB_T);
}
static void filled_bar(int x, int y, int w, int h, int pct, const char *txt) {
    fb_fill_rect(x, y, w, h, 0x12182C);
    fb_bevel_sunken(x, y, w, h, 0x6477A0, 0x0A0C18);
    int fill = (pct * (w-4)) / 100;
    if (fill > w-4) fill = w-4;
    if (fill > 0) fb_fill_rect(x+2, y+2, fill, h-4, 0xFFA831);
    if (txt) {
        int n = 0; while (txt[n]) n++;
        fb_draw_text(x+(w-n*8)/2, y+(h-16)/2, txt, 0xFFFFFF, FB_T);
    }
}
/* Card geometry helper: fills common layout vars. */
static void card_dims(int *cx, int *cy, int *cw, int *ch) {
    int W = fb_w(), H = fb_h();
    int hdr = (H < 500) ? 56 : 80;
    *cw = W - 40; if (*cw > 720) *cw = 720;
    *cx = (W - *cw) / 2;
    *cy = hdr + 16;
    *ch = H - *cy - 40; if (*ch > 360) *ch = 360;
}
static void hdr_band(const char *title, const char *sub, u32 bg1, u32 bg2) {
    int W = fb_w(), H = fb_h();
    int hdr = (H < 500) ? 56 : 80;
    fb_fill_rect(0, 0, W, H, 0x101830);
    fb_hgradient(0, 0, W, hdr, bg1, bg2);
    int zs = (H < 500) ? 3 : 5;
    draw_z(16, (hdr-12*zs)/2, zs, 0xFFA831);
    int tx = 16 + 12*zs + 8;
    fb_draw_text(tx, hdr/2-14, title, 0xFFC447, FB_T);
    if (sub) fb_draw_text(tx, hdr/2+2, sub, 0xC8D4E0, FB_T);
}
static void footer(const char *left) {
    int W = fb_w(), H = fb_h();
    fb_draw_text(16, H-20, left, 0xC8D4E0, FB_T);
}

/* ── Log area for progress screens ────────────────────────────────── */
static char g_log[2048];
static int  g_log_len;
static void log_clear(void) { g_log_len = 0; g_log[0] = 0; }
static void log_add(const char *s) {
    while (*s && g_log_len < (int)sizeof g_log - 2)
        g_log[g_log_len++] = *s++;
    g_log[g_log_len] = 0;
}
static void log_printf(const char *fmt, ...) {
    char buf[120];
    va_list ap; va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_add(buf);
}

/* ── Step 1: Welcome ──────────────────────────────────────────────── */
static int step_welcome(void) {
    int sel = 0;
    for (;;) {
        hdr_band("Zenbite Setup", "graphical installer", 0x2C2152, 0x6447B0);
        int cx, cy, cw, ch;
        card_dims(&cx, &cy, &cw, &ch);
        fb_fill_rect(cx, cy, cw, ch, 0xE8ECF1);
        fb_bevel_raised(cx, cy, cw, ch, 0xFFFFFF, 0x404850);
        int lx = cx + 24;
        fb_draw_text(lx, cy+20, "Welcome to Zenbite " ZENBITE_VERSION ".", 0x1E2A38, FB_T);
        if (ch >= 240) {
            fb_draw_text(lx,      cy+46,  "This wizard will:", 0x1E2A38, FB_T);
            fb_draw_text(lx+16, cy+66,  "Pick a target disk", 0x1E2A38, FB_T);
            fb_draw_text(lx+16, cy+82,  "Format it as FAT16", 0x1E2A38, FB_T);
            fb_draw_text(lx+16, cy+98,  "Copy the kernel + apps", 0x1E2A38, FB_T);
            fb_draw_text(lx+16, cy+114, "Install the bootloader", 0x1E2A38, FB_T);
        }
        int wy = cy + (ch >= 240 ? 146 : 46);
        fb_draw_text(lx, wy, "Everything on the target disk will be erased.", 0xC0392B, FB_T);
        int by = cy + ch - 52;
        button(lx, by, 140, 32, "Continue", sel == 0);
        button(cx+cw-16-140, by, 140, 32, "Quit", sel == 1);
        footer("Zenbite Setup");
        fb_present();
        int k = kb_getc();
        if (k == 27 || (k == '\t' && sel == 1)) return -1;
        if (k == '\n' || k == '\r') return sel == 0 ? 0 : -1;
        if (k == KB_LEFT || k == KB_RIGHT || k == ' ') sel = 1 - sel;
    }
}

/* ── Step 2: Disk picker ──────────────────────────────────────────── */
struct disk_entry { int id; char desc[64]; };
static int scan_disks(struct disk_entry *out, int max) {
    int n = 0;
    /* Partition views first, then raw disks without MBR. */
    for (int id = 0; id < DISK_MAX && n < max; id++) {
        struct disk *d = disk_get(id);
        if (!d || !d->present) continue;
        if (id >= PART_SLOT_BASE) {
            int raw, part; u32 start; u8 type, boot;
            mbr_view_info(id, &raw, &part, &start, NULL, &type, &boot);
            struct disk *parent = disk_get(raw);
            /* Check if mounted */
            char mnt = '-';
            for (char L = 'A'; L < 'A'+FS_DRIVE_MAX; L++)
                if (fs_drive_disk_id(L) == id) mnt = L;
            const char *what = "FAT";
            if (file_exists(mnt, "INSTALL.TAG")) what = "Install media";
            ksnprintf(out[n].desc, sizeof out[n].desc,
                      "%-8s %3u MiB  %c  %s", d->name,
                      d->sectors/2048, boot==0x80?'*':' ', what);
            out[n].id = id; n++;
        }
    }
    for (int id = 0; id < DISK_RAW_MAX && n < max; id++) {
        struct disk *d = disk_get(id);
        if (!d || !d->present) continue;
        if (mbr_has_table(id)) continue;  /* skip raw -- partitions shown */
        ksnprintf(out[n].desc, sizeof out[n].desc,
                  "%-8s %4u MiB  (superfloppy)", d->name, d->sectors/2048);
        out[n].id = id; n++;
    }
    return n;
}

static int step_pick_disk(int *out_target) {
    struct disk_entry disks[32];
    int nd = scan_disks(disks, 32);
    if (nd == 0) {
        /* No disks -- tell user to attach and rescan. */
        for (;;) {
            hdr_band("Zenbite Setup", "disk selection", 0x2C2152, 0x6447B0);
            int cx, cy, cw, ch; card_dims(&cx,&cy,&cw,&ch);
            fb_fill_rect(cx, cy, cw, ch, 0xE8ECF1);
            fb_bevel_raised(cx, cy, cw, ch, 0xFFFFFF, 0x404850);
            int lx = cx+24;
            fb_draw_text(lx, cy+20, "No disks found.", 0xC0392B, FB_T);
            fb_draw_text(lx, cy+44, "Attach a disk and press F5 to rescan,", 0x1E2A38, FB_T);
            fb_draw_text(lx, cy+60, "or ESC to cancel.", 0x1E2A38, FB_T);
            footer("F5=Rescan  ESC=Cancel");
            fb_present();
            int k = kb_getc();
            if (k == 27) return -1;
            if (k == KB_F5) { fs_rescan(); nd = scan_disks(disks, 32); if (nd > 0) break; }
        }
    }
    int sel = 0;
    for (;;) {
        hdr_band("Zenbite Setup", "select target disk", 0x2C2152, 0x6447B0);
        int cx, cy, cw, ch; card_dims(&cx,&cy,&cw,&ch);
        fb_fill_rect(cx, cy, cw, ch, 0xE8ECF1);
        fb_bevel_raised(cx, cy, cw, ch, 0xFFFFFF, 0x404850);
        int lx = cx+24;
        fb_draw_text(lx, cy+16, "Select target disk:", 0x1E2A38, FB_T);
        int row_y = cy+40;
        int vis = (ch - 80) / 20;
        if (vis > nd) vis = nd;
        int top = 0;
        if (nd > vis) { top = sel - vis/2; if (top<0) top=0; if (top>nd-vis) top=nd-vis; }
        for (int i = 0; i < vis; i++) {
            int idx = top + i;
            int ry = row_y + i*20;
            int is_sel = (idx == sel);
            fb_fill_rect(lx, ry, cw-48, 18, is_sel ? 0x6447B0 : 0xE8ECF1);
            fb_draw_text(lx+4, ry+1, disks[idx].desc,
                         is_sel ? 0xFFFFFF : 0x1E2A38, FB_T);
        }
        fb_draw_text(lx, cy+ch-28, "ENTER=select  F5=rescan  ESC=cancel",
                     0x70808A, FB_T);
        footer("Zenbite Setup");
        fb_present();
        int k = kb_getc();
        if (k == 27) return -1;
        if (k == KB_F5) { fs_rescan(); nd = scan_disks(disks, 32); sel = 0; continue; }
        if (k == KB_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == KB_DOWN || k == 'j') { if (sel < nd-1) sel++; }
        else if (k == '\n' || k == '\r') { *out_target = disks[sel].id; return 0; }
    }
}

/* ── Step 3: Format + copy progress ───────────────────────────────── */
static int g_pct;
static int g_step_total, g_step_done;
static const char *g_step_label;

static void draw_progress(const char *title) {
    hdr_band("Zenbite Setup", title, 0x2C2152, 0x6447B0);
    int cx, cy, cw, ch; card_dims(&cx,&cy,&cw,&ch);
    fb_fill_rect(cx, cy, cw, ch, 0xE8ECF1);
    fb_bevel_raised(cx, cy, cw, ch, 0xFFFFFF, 0x404850);
    /* Progress bar */
    char pct_txt[16];
    ksnprintf(pct_txt, sizeof pct_txt, "%d%%", g_pct);
    filled_bar(cx+16, cy+ch-56, cw-32, 24, g_pct, pct_txt);
    /* Log area */
    int log_x = cx+16, log_y = cy+40;
    int log_w = cw-32, log_h = ch-104;
    fb_fill_rect(log_x, log_y, log_w, log_h, 0x0A0C18);
    fb_bevel_sunken(log_x, log_y, log_w, log_h, 0x33495E, 0x000000);
    /* Render last N lines that fit */
    int rows = log_h / 14;
    int lines = 0;
    for (int i = 0; i < g_log_len; i++) if (g_log[i] == '\n') lines++;
    int skip = lines - rows + 1;
    if (skip < 0) skip = 0;
    int y = log_y + 4, col = 0, skipped = 0;
    for (int i = 0; i < g_log_len && y < log_y + log_h - 12; i++) {
        char c = g_log[i];
        if (c == '\n') {
            if (skipped < skip) { skipped++; }
            else { y += 14; }
            col = 0; continue;
        }
        if (skipped >= skip) {
            if (col < log_w - 8)
                fb_blit_glyph(log_x+4+col*8, y, (u8)c, 0xAAAAAA, FB_T);
            col++;
        }
    }
    footer("Zenbite Setup -- installing");
    fb_present();
}

static int copy_progress_cb(const char *fname, int bytes) {
    g_step_done++;
    if (bytes >= 0)
        log_printf("  %s  %d B  OK\n", fname, bytes);
    else
        log_printf("  %s  ERROR\n", fname);
    g_pct = (g_step_done * 100) / g_step_total;
    draw_progress("Installing ...");
    return 0;
}

static int step_install(int target) {
    struct disk *td = disk_get(target);
    if (!td) return -1;
    /* Always mount the target as A: -- unmount anything currently on A: first.
     * This ensures the installed system is on the primary drive letter. */
    char letter = 'A';
    if (fs_drive_present('A') && fs_drive_disk_id('A') != target)
        fs_unmount('A');
    if (fs_drive_disk_id('A') == target) {
        letter = 'A';
    } else {
        /* Unmount whatever is on A: and use it for the target. */
        fs_unmount('A');
        letter = 'A';
    }
    log_clear();
    g_pct = 0; g_step_done = 0; g_step_total = 1;
    draw_progress("Installing ...");

    /* Format */
    log_printf("Formatting %s as FAT16 ...\n", td->name);
    draw_progress("Formatting ...");
    fs_unmount(letter);
    if (fs_format(target, "ZENBITE") < 0) {
        log_printf("  FORMAT FAILED\n");
        draw_progress("Format failed!");
        return -1;
    }
    if (fs_mount(letter, target) < 0) {
        log_printf("  MOUNT FAILED\n");
        draw_progress("Mount failed!");
        return -1;
    }
    fs_set_drive(letter);
    log_printf("  Format OK, mounted on %c:\n", letter);

    /* User home */
    log_printf("Creating home directory ...\n");
    create_user_home(letter, "ZENBITE");
    log_printf("  \\HOME\\ZENBITE\\ created\n");

    /* Find sources */
    char sys_src  = find_source_dir("SYSTEM",  letter);
    char boot_src = find_source_dir("BOOT",    letter);
    char smpl_src = find_source_dir("SAMPLES", letter);

    g_step_done = 0;
    g_step_total = (sys_src  != '?' ? count_dir_files(sys_src,  "SYSTEM")  : 0)
                 + (boot_src != '?' ? count_dir_files(boot_src, "BOOT")    : 0)
                 + (smpl_src != '?' ? count_dir_files(smpl_src, "SAMPLES") : 0)
                 + 1;  /* +1 for bootloader */
    g_pct = 0;

    if (sys_src != '?') {
        log_printf("Copying \\SYSTEM\\ ...\n");
        draw_progress("Copying SYSTEM ...");
        copy_tree(sys_src, "SYSTEM", letter, "SYSTEM", copy_progress_cb);
    }
    if (boot_src != '?') {
        log_printf("Copying \\BOOT\\ ...\n");
        draw_progress("Copying BOOT ...");
        copy_tree(boot_src, "BOOT", letter, "BOOT", copy_progress_cb);
    }
    if (smpl_src != '?') {
        log_printf("Copying \\SAMPLES\\ ...\n");
        draw_progress("Copying SAMPLES ...");
        copy_tree(smpl_src, "SAMPLES", letter, "SAMPLES", copy_progress_cb);
    }

    /* Ensure SYSTEM\BIN exists with the .ZBX apps.
     * If the source had BIN, it was copied above. If not, create the
     * directory so the desktop's Programs widget has somewhere to look. */
    {
        /* Set current drive so fs_mkdir resolves relative paths correctly. */
        fs_set_drive(letter);
        fs_mkdir("SYSTEM");
        fs_mkdir("SYSTEM\\BIN");

        /* Also copy the kernel + loader into SYSTEM\ for boot. */
        char path[FS_PATH_MAX];
        extern u8  stage2_blob[];
        extern u32 stage2_blob_size;
        ksnprintf(path, sizeof path, "%c:\\SYSTEM\\STAGE2.BIN", letter);
        zb_write_file(path, stage2_blob, (int)stage2_blob_size);
        ksnprintf(path, sizeof path, "%c:\\SYSTEM\\KERNEL.BIN", letter);
        zb_write_file(path, (void *)0x100000,
                      (int)((char *)_bss_start - (char *)0x100000));

        /* Copy .ZBX apps from SYSTEM\BIN on source if available */
        if (sys_src != '?') {
            char src_bin[16], dst_bin[16];
            ksnprintf(src_bin, sizeof src_bin, "%c:\\SYSTEM\\BIN", sys_src);
            ksnprintf(dst_bin, sizeof dst_bin, "%c:\\SYSTEM\\BIN", letter);
            /* Re-copy BIN specifically to ensure .ZBX files are there */
            copy_tree(sys_src, "SYSTEM\\BIN", letter, "SYSTEM\\BIN", copy_progress_cb);
        }
        log_printf("  SYSTEM\\BIN\\ ready\n");
    }

    /* Bootloader */
    log_printf("Installing bootloader ...\n");
    draw_progress("Installing bootloader ...");
    int rc = install_bootloader(target, letter);
    g_step_done++; g_pct = 100;
    if (rc < 0) {
        log_printf("  BOOTLOADER FAILED\n");
        draw_progress("Bootloader failed!");
        return -1;
    }
    log_printf("  Bootloader OK\n");

    /* Verify */
    log_printf("Verifying ...\n");
    int ok = 1;
    if (!file_exists(letter, "STAGE2.BIN"))     { log_printf("  MISSING STAGE2.BIN\n"); ok = 0; }
    if (!file_exists(letter, "KERNEL.BIN"))     { log_printf("  MISSING KERNEL.BIN\n"); ok = 0; }
    if (!file_exists(letter, "SYSTEM\\ZENBITE.SYS")) { log_printf("  MISSING ZENBITE.SYS\n"); ok = 0; }
    /* Check SYSTEM\BIN exists as a directory */
    {
        char bindir[32];
        char *cwd = fs_cwd();
        ksnprintf(bindir, sizeof bindir, "%c:\\SYSTEM\\BIN", letter);
        int ddh = fs_opendir(bindir);
        if (ddh < 0) { log_printf("  MISSING SYSTEM\\BIN\n"); ok = 0; }
        else fs_closedir(ddh);
    }
    if (ok) log_printf("  All files verified OK\n");

    draw_progress(ok ? "Installation complete!" : "Installation completed with errors");
    kb_getc();
    return ok ? 0 : -1;
}

/* ── Step 4: Done ─────────────────────────────────────────────────── */
static void step_done(void) {
    hdr_band("Installation complete", "Zenbite " ZENBITE_VERSION, 0x1F6B3B, 0x4FB37A);
    int cx, cy, cw, ch; card_dims(&cx,&cy,&cw,&ch);
    fb_fill_rect(cx, cy, cw, ch, 0xE8ECF1);
    fb_bevel_raised(cx, cy, cw, ch, 0xFFFFFF, 0x404850);
    int lx = cx+24;
    fb_draw_text(lx, cy+20, "Zenbite is now installed.", 0x1E2A38, FB_T);
    fb_draw_text(lx, cy+44, "Remove install media, then reboot.", 0x1E2A38, FB_T);
    if (ch >= 200) {
        fb_draw_text(lx, cy+76,  "The graphical desktop (gdesk) launches", 0x1E2A38, FB_T);
        fb_draw_text(lx, cy+92,  "automatically on next boot.", 0x1E2A38, FB_T);
        fb_draw_text(lx, cy+120, "Lock password is set in Settings.", 0x6447B0, FB_T);
    }
    fb_draw_text(lx, cy+ch-28, "Press any key to reboot ...", 0x70808A, FB_T);
    footer("Zenbite Setup -- complete");
    fb_present();
    kb_getc();
}

/* ── Entry point ──────────────────────────────────────────────────── */
int install_g_main(int argc, char **argv) {
    (void)argc; (void)argv;
    /* Ensure we have a pixel framebuffer. */
    if (!bga_present_active()) {
        int gw, gh;
        if (bga_best_mode(&gw, &gh) < 0 || fb_w() <= 0 || fb_h() <= 0) {
            extern int install_main(int, char **);
            return install_main(argc, argv);
        }
    }
    for (;;) {
        /* Step 1: Welcome */
        if (step_welcome() < 0) { vga_set_text_mode(25); return -1; }
        /* Step 2: Pick disk */
        int target;
        if (step_pick_disk(&target) < 0) { vga_set_text_mode(25); return -1; }
        /* Confirm */
        int confirmed = 0;
        {
            struct disk *td = disk_get(target);
            hdr_band("Confirm", "are you sure?", 0x2C2152, 0x6447B0);
            int cx,cy,cw,ch; card_dims(&cx,&cy,&cw,&ch);
            fb_fill_rect(cx,cy,cw,ch, 0xE8ECF1);
            fb_bevel_raised(cx,cy,cw,ch, 0xFFFFFF, 0x404850);
            int lx = cx+24;
            char msg[128];
            if (target >= PART_SLOT_BASE) {
                int raw,part; u32 sec;
                mbr_view_info(target, &raw, &part, NULL, &sec, NULL, NULL);
                struct disk *p = disk_get(raw);
                ksnprintf(msg, sizeof msg, "Partition %s%d (%u MiB) will be erased.",
                          p?p->name:"?", part+1, sec/2048);
            } else {
                ksnprintf(msg, sizeof msg, "Disk %s (%u MiB) will be completely erased.",
                          td?td->name:"?", td?td->sectors/2048:0);
            }
            fb_draw_text(lx, cy+20, msg, 0xC0392B, FB_T);
            fb_draw_text(lx, cy+44, "This cannot be undone.", 0xC0392B, FB_T);
            int sel = 0;
            for (;;) {
                int by = cy+ch-52;
                button(lx, by, 140, 32, "Install", sel==0);
                button(cx+cw-16-140, by, 140, 32, "Back", sel==1);
                fb_present();
                int k = kb_getc();
                if (k == 27 || (k=='\t' && sel==1) || k==KB_LEFT) { sel=1-sel; continue; }
                if (k == KB_RIGHT) { sel=1-sel; continue; }
                if (k == '\n' || k == '\r') { if (sel==1) break; confirmed=1; break; }
                if (k == ' ') sel = 1-sel;
            }
        }
        if (!confirmed) continue;  /* Back -> disk picker */
        /* Step 3: Install */
        if (step_install(target) < 0) { vga_set_text_mode(25); return -1; }
        /* Step 4: Done */
        step_done();
        extern void reboot(void);
        reboot();
    }
}
