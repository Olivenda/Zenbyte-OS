/* Bochs / QEMU VBE (BGA) graphics driver.
 *
 * BGA is the simple framebuffer device exposed by QEMU's std-vga
 * card (also Bochs). It's controlled via two I/O ports:
 *   0x1CE -- index
 *   0x1CF -- data
 * Indices we care about:
 *   0 ID        -- read returns 0xB0C0..0xB0CF if BGA is present
 *   1 XRES      -- write width  in pixels
 *   2 YRES      -- write height in pixels
 *   3 BPP       -- write 32 for x8r8g8b8
 *   4 ENABLE    -- write 0x41 (ENABLE | LFB) after setting the above
 * The linear framebuffer lives at the device's PCI BAR0. QEMU pins it
 * at 0xFD000000 for the default std-vga and at 0xE0000000 for the
 * legacy vbe-display device; we probe a small list of addresses by
 * writing a tag word and reading it back. */
#include "kernel.h"
#include "kio.h"
#include "io.h"
#include "vga.h"
#include "string.h"

extern const u8 *vga_get_font(void);

#define BGA_INDEX   0x01CE
#define BGA_DATA    0x01CF
#define BGA_ID      0
#define BGA_XRES    1
#define BGA_YRES    2
#define BGA_BPP     3
#define BGA_ENABLE  4
#define BGA_ENABLED 0x01
#define BGA_LFB     0x40

static u32     *fb_ptr;        /* linear framebuffer (MMIO), 0 = not initialized */
static int      fb_width;
static int      fb_height;
static int      fb_cols;       /* text cells (fb_width / 8) */
static int      fb_rows;       /* text cells (fb_height / 16) */

/* === Double buffer ====================================================
 * back_buf holds the current "scene" -- every fb_* function writes
 * pixels here, not directly to MMIO. fb_present() compares back_buf
 * against prev_buf and pushes only the changed pixels out to the
 * framebuffer.
 *
 * Why bother: BGA MMIO writes are slow under QEMU (every store traps
 * to the hypervisor). Doing a full-window fill_rect on every keystroke
 * hammered ~400k MMIO writes per keypress and produced visible flicker.
 * With the diff present, typing a single character writes ~128 changed
 * pixels per frame -- a 3000x reduction -- and the visible artefact
 * disappears entirely.
 *
 * Memory: back_buf + prev_buf = 2 * W * H * 4 bytes. At 1024x768 that
 * is ~6.3 MiB, well within the 14 MiB free in pmm. */
static u32     *back_buf;
static u32     *prev_buf;
static int      bb_pixels;
static int      bb_initialised;

/* The "draw target" is wherever fb_* writes land. Normally back_buf.
 * Set to fb_ptr (direct MMIO) when the back buffer isn't allocated
 * yet, so early-boot draws still work. */
static u32 *draw_target(void) {
    return back_buf ? back_buf : fb_ptr;
}

/* Walk back_buf vs prev_buf in 64-pixel runs and emit only the changed
 * runs to MMIO. This batches MMIO writes inside contiguous changed
 * regions and avoids touching the framebuffer at all when nothing
 * changed in a span. */
void fb_present(void) {
    if (!fb_ptr || !back_buf || !prev_buf) return;
    int total = bb_pixels;
    int i = 0;
    while (i < total) {
        /* Skip a run of unchanged pixels. */
        while (i < total && back_buf[i] == prev_buf[i]) i++;
        if (i >= total) break;
        /* Find the end of this changed run. */
        int start = i;
        while (i < total && back_buf[i] != prev_buf[i]) {
            prev_buf[i] = back_buf[i];
            i++;
        }
        /* Push the changed run to MMIO in one tight memcpy. GCC turns
         * the loop into rep movsd which the hypervisor batches better
         * than scattered stores. */
        u32 *src = &back_buf[start];
        u32 *dst = &fb_ptr[start];
        int n = i - start;
        for (int k = 0; k < n; k++) dst[k] = src[k];
    }
}

/* Allocate the back / prev buffers from raw pmm frames so we don't
 * blow the 64 KiB kheap. Frames are contiguous because pmm scans
 * low-to-high and nothing else allocates from it. Idempotent -- only
 * runs on first bga_init call. */
extern void *pmm_alloc_frame(void);
static int alloc_back_buffers(int total_pixels) {
    if (bb_initialised) return 0;
    int total_bytes = total_pixels * 4 * 2;        /* back + prev */
    int frames = (total_bytes + 4095) / 4096;
    u32 base = 0;
    for (int i = 0; i < frames; i++) {
        u32 f = (u32)pmm_alloc_frame();
        if (!f) return -1;
        if (i == 0) base = f;
        else if (f != base + (u32)i * 4096) {
            /* Non-contiguous -- pmm gave us a fragmented run.
             * Bail and stay on direct-MMIO. The earlier frames
             * are now leaked but pmm doesn't free anyway. */
            return -1;
        }
    }
    back_buf = (u32 *)base;
    prev_buf = (u32 *)(base + (u32)total_pixels * 4);
    /* Clear both halves. prev_buf must start as the inverse of back_buf
     * so the first present writes every pixel. We just init prev to a
     * sentinel that back_buf won't match. */
    for (int i = 0; i < total_pixels; i++) {
        back_buf[i] = 0x000000;
        prev_buf[i] = 0xDEADBEEF;
    }
    bb_pixels = total_pixels;
    bb_initialised = 1;
    return 0;
}

/* Palette: VGA 16-colour table -> 32-bit RGB. Matches the colour
 * indices in the existing text-mode attribute bytes (low nibble = fg,
 * high nibble = bg) so all the existing vga_put_cell calls just work. */
static const u32 vga_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static u16 bga_read(u16 index) {
    outw(BGA_INDEX, index);
    return inw(BGA_DATA);
}
static void bga_write(u16 index, u16 v) {
    outw(BGA_INDEX, index);
    outw(BGA_DATA,  v);
}

/* PCI config space read -- same pattern as e1000/ehci drivers. */
static u32 pci_r32(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11)
             | ((u32)fn << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

/* Scan PCI bus for a BGA-compatible display device and return its
 * BAR0 address.  QEMU std-vga = 0x1234:0x1111, bochs-display = 0x1234:0x1112.
 * Also scans for real VGA-class devices (class 0x03) such as Intel
 * integrated graphics, AMD, and Cirrus -- these expose a linear
 * framebuffer at BAR0 that works identically to BGA for our purposes.
 * Returns 0 if not found or BAR is I/O-mapped. */
static u32 pci_find_bga_bar0(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int d = 0; d < 32; d++) {
            u32 id = pci_r32((u8)bus, (u8)d, 0, 0);
            if (id == 0xFFFFFFFF) continue;
            u16 vendor = (u16)(id & 0xFFFF);
            u16 device = (u16)(id >> 16);

            /* BGA-specific devices (QEMU/Bochs) */
            if (vendor == 0x1234 &&
                (device == 0x1111 || device == 0x1112)) {
                u32 bar0 = pci_r32((u8)bus, (u8)d, 0, 0x10);
                if (bar0 & 1) continue;   /* I/O BAR */
                return bar0 & ~0xFu;
            }

            /* Generic VGA class (0x03): check class code at offset 8 */
            u32 cc = pci_r32((u8)bus, (u8)d, 0, 8);
            u8 class_code = (cc >> 24) & 0xFF;
            if (class_code == 0x03) {
                /* VGA-compatible controller: BAR0 is the framebuffer.
                 * Try BAR0 first; some devices put it at BAR2. */
                for (int bar = 0; bar < 6; bar++) {
                    u32 bar_val = pci_r32((u8)bus, (u8)d, 0, 0x10 + bar * 4);
                    if (bar_val == 0 || bar_val == 0xFFFFFFFF) continue;
                    if (bar_val & 1) continue;  /* I/O BAR */
                    u32 addr = bar_val & ~0xFu;
                    /* Sanity: framebuffer should be in high memory (>1MiB) */
                    if (addr >= 0x100000 && addr < 0xF0000000u)
                        return addr;
                }
            }
        }
    }
    return 0;
}

/* Write-back test at a physical address. */
static int addr_writable(u32 phys) {
    volatile u32 *p = (volatile u32 *)phys;
    u32 saved = p[0];
    p[0] = 0xDEADBEEF;
    int ok = (p[0] == 0xDEADBEEF);
    p[0] = saved;
    return ok;
}

/* Find the linear framebuffer.  PCI scan gives the authoritative BAR0;
 * fall back to a list of well-known addresses when PCI finds nothing. */
static u32 *probe_fb(void) {
    /* Authoritative: read BAR0 from PCI config space. */
    u32 bar = pci_find_bga_bar0();
    if (bar && addr_writable(bar))
        return (u32 *)bar;

    /* Fallback list covers the common QEMU / VirtualBox / UTM /
     * real-hardware SVGA layouts. */
    static const u32 candidates[] = {
        0xFD000000,   /* QEMU std-vga (most common) */
        0xE0000000,   /* Bochs legacy / VirtualBox */
        0xF0000000,   /* some virtio-vga configs */
        0xE8000000,   /* alternate VirtualBox range */
        0xC0000000,   /* real-hardware SVGA cards */
        0xD8000000,   /* Intel i810/i815 IGP */
        0xA0000000,   /* Intel HD/UHD Graphics */
        0xB0000000,   /* some AMD GPUs */
        0xFC000000,   /* nVidia legacy */
        0xFE000000,   /* nVidia modern */
    };
    for (unsigned i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        if (addr_writable(candidates[i]))
            return (u32 *)candidates[i];
    }
    return 0;
}

int bga_init(int want_w, int want_h) {
    /* Probe for the BGA device via its magic ID register. */
    u16 id = bga_read(BGA_ID);
    if ((id & 0xFFF0) != 0xB0C0) return -1;

    /* Disable, set mode, re-enable. */
    bga_write(BGA_ENABLE, 0);
    bga_write(BGA_XRES,   (u16)want_w);
    bga_write(BGA_YRES,   (u16)want_h);
    bga_write(BGA_BPP,    32);
    bga_write(BGA_ENABLE, BGA_ENABLED | BGA_LFB);

    /* Read back actual dimensions -- some VMs round or cap the resolution.
     * Accept whatever the hardware settled on, not necessarily want_w/h. */
    int got_w = (int)bga_read(BGA_XRES);
    int got_h = (int)bga_read(BGA_YRES);
    if (got_w < 320 || got_h < 240) {
        bga_write(BGA_ENABLE, 0);
        return -1;
    }

    fb_ptr = probe_fb();
    if (!fb_ptr) { bga_write(BGA_ENABLE, 0); return -1; }
    fb_width  = got_w;
    fb_height = got_h;
    fb_cols   = got_w / 8;
    fb_rows   = got_h / 16;
    int total = fb_width * fb_height;
    /* Try to allocate the diff back buffer. If pmm can't give us
     * contiguous frames we fall back to direct MMIO (works, just
     * flickers under load). */
    if (alloc_back_buffers(total) == 0) {
        /* Wipe both buffers + the screen so first present pushes
         * a known-good frame out. */
        for (int i = 0; i < total; i++) {
            back_buf[i] = 0x000000;
            prev_buf[i] = 0x000000;
            fb_ptr[i]   = 0x000000;
        }
    } else {
        /* No back buffer -- clear MMIO directly. */
        for (int i = 0; i < total; i++) fb_ptr[i] = 0x000000;
    }
    return 0;
}

void bga_disable(void) {
    if (!fb_ptr) return;
    bga_write(BGA_ENABLE, 0);
    fb_ptr = 0;
}

int  bga_present_active(void) { return fb_ptr != 0; }
int  bga_cols(void)           { return fb_cols; }
int  bga_rows(void)           { return fb_rows; }

/* Try a prioritised list of common resolutions and activate the first
 * one the hardware accepts.  On return *out_w / *out_h hold the actual
 * dimensions (which may differ from the request if the hardware rounded).
 * Returns 0 on success, -1 if no mode could be set. */
int bga_best_mode(int *out_w, int *out_h) {
    /* Prioritised list: common VGA/SVGA first, then HDMI widescreen,
     * then large desktop modes. Real HDMI displays typically support
     * at least one of these. */
    static const int modes[][2] = {
        { 800,  600},   /* Win95-era baseline -- most reliable */
        {1024,  768},   /* XGA -- common QEMU default */
        { 640,  480},   /* minimum fallback */
        {1280,  720},   /* 720p widescreen (HDMI common) */
        {1280, 1024},   /* SXGA */
        {1024,  600},   /* netbook / small QEMU window */
        {1920, 1080},   /* 1080p Full HD (HDMI) */
        {1680, 1050},   /* WSXGA+ */
        {1600,  900},   /* HD+ */
        {1440,  900},   /* WXGA+ */
        {1366,  768},   /* HD common laptop/TV */
        {1280,  800},   /* WXGA */
    };
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        if (bga_init(modes[i][0], modes[i][1]) == 0) {
            if (out_w) *out_w = fb_width;
            if (out_h) *out_h = fb_height;
            return 0;
        }
    }
    return -1;
}
int  bga_pixel_w(void)        { return fb_width;  }
int  bga_pixel_h(void)        { return fb_height; }

/* Render one cell at (cell_row, cell_col) to the framebuffer. The
 * cell is (glyph, attr) with attr=(bg<<4)|fg. 8x16 pixel glyphs. */
void bga_draw_cell(int cell_row, int cell_col, u8 glyph, u8 attr) {
    u32 *t = draw_target();
    if (!t) return;
    if (cell_col < 0 || cell_col >= fb_cols) return;
    if (cell_row < 0 || cell_row >= fb_rows) return;
    u32 fg = vga_rgb[attr & 0x0F];
    u32 bg = vga_rgb[(attr >> 4) & 0x0F];
    const u8 *font = vga_get_font();
    const u8 *g = font + (u32)glyph * 16;
    int x = cell_col * 8;
    int y = cell_row * 16;
    for (int r = 0; r < 16; r++) {
        u8 bits = g[r];
        u32 *row = t + (y + r) * fb_width + x;
        row[0] = (bits & 0x80) ? fg : bg;
        row[1] = (bits & 0x40) ? fg : bg;
        row[2] = (bits & 0x20) ? fg : bg;
        row[3] = (bits & 0x10) ? fg : bg;
        row[4] = (bits & 0x08) ? fg : bg;
        row[5] = (bits & 0x04) ? fg : bg;
        row[6] = (bits & 0x02) ? fg : bg;
        row[7] = (bits & 0x01) ? fg : bg;
    }
}

/* === Pixel API ============================================================
 * Direct framebuffer primitives in 32-bit RGB. The Win95-style
 * graphical desktop is built on these; widget chrome (3D bevels,
 * gradient title bars, taskbar, mouse cursor) is drawn pixel-accurate
 * instead of cell-aligned.
 *
 * Coordinates are pixels, origin top-left. No clipping outside the
 * framebuffer extents -- callers are expected to stay in range; we
 * just bound-check the framebuffer pointer once. */

int  fb_w(void) { return fb_width;  }
int  fb_h(void) { return fb_height; }
/* fb_addr returns the draw target so the cursor save/restore in
 * desktop_g.c reads and writes the SAME buffer that everything else
 * draws to. With the back buffer that's back_buf; without, it's the
 * MMIO framebuffer. */
u32 *fb_addr(void) { return draw_target(); }

void fb_pixel(int x, int y, u32 color) {
    u32 *t = draw_target();
    if (!t) return;
    if ((unsigned)x >= (unsigned)fb_width || (unsigned)y >= (unsigned)fb_height) return;
    t[y * fb_width + x] = color;
}

void fb_fill_rect(int x, int y, int w, int h, u32 color) {
    u32 *t = draw_target();
    if (!t) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_width)  w = fb_width  - x;
    if (y + h > fb_height) h = fb_height - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++) {
        u32 *row = t + (y + r) * fb_width + x;
        for (int c = 0; c < w; c++) row[c] = color;
    }
}

void fb_hline(int x, int y, int w, u32 color) { fb_fill_rect(x, y, w, 1, color); }
void fb_vline(int x, int y, int h, u32 color) { fb_fill_rect(x, y, 1, h, color); }

/* Render the BIOS 8x16 glyph at arbitrary pixel coords. fg/bg are
 * 32-bit RGB. Setting bg to a sentinel like 0xFF000000 (caller
 * convention) means "transparent background" -- we just skip the
 * non-set bits so the underlying pixels show through. */
#define FB_TRANSPARENT 0xFF000000u
void fb_blit_glyph(int x, int y, u8 glyph, u32 fg, u32 bg) {
    u32 *t = draw_target();
    if (!t) return;
    extern const u8 *vga_get_font(void);
    const u8 *g = vga_get_font() + (u32)glyph * 16;
    for (int r = 0; r < 16; r++) {
        int py = y + r;
        if ((unsigned)py >= (unsigned)fb_height) continue;
        u8 bits = g[r];
        u32 *row = t + py * fb_width;
        for (int c = 0; c < 8; c++) {
            int px = x + c;
            if ((unsigned)px >= (unsigned)fb_width) continue;
            if (bits & (0x80u >> c)) row[px] = fg;
            else if (bg != FB_TRANSPARENT) row[px] = bg;
        }
    }
}

void fb_draw_text(int x, int y, const char *s, u32 fg, u32 bg) {
    int px = x;
    while (*s) {
        fb_blit_glyph(px, y, (u8)*s, fg, bg);
        px += 8;
        s++;
    }
}

/* Sprite blit -- one byte per pixel, 0 = transparent, otherwise an
 * index into a passed-in palette. Used for the mouse cursor. */
void fb_blit_sprite(int x, int y, int w, int h,
                    const u8 *data, const u32 *palette) {
    u32 *t = draw_target();
    if (!t) return;
    for (int r = 0; r < h; r++) {
        int py = y + r;
        if ((unsigned)py >= (unsigned)fb_height) continue;
        for (int c = 0; c < w; c++) {
            int px = x + c;
            if ((unsigned)px >= (unsigned)fb_width) continue;
            u8 idx = data[r * w + c];
            if (idx == 0) continue;
            t[py * fb_width + px] = palette[idx];
        }
    }
}

/* === Win95-style chrome helpers ==========================================
 * 3D bevel: a single rectangle drawn with two passes -- the top/left
 * edges in the light colour, the bottom/right in the dark one. The
 * combo reads as raised (button) or sunken (input field) depending
 * on which colour goes where.
 */
void fb_bevel_raised(int x, int y, int w, int h,
                     u32 light, u32 dark) {
    /* top + left = light, bottom + right = dark */
    fb_hline(x,             y,             w, light);
    fb_vline(x,             y,             h, light);
    fb_hline(x,             y + h - 1,     w, dark);
    fb_vline(x + w - 1,     y,             h, dark);
}

void fb_bevel_sunken(int x, int y, int w, int h,
                     u32 light, u32 dark) {
    fb_hline(x,             y,             w, dark);
    fb_vline(x,             y,             h, dark);
    fb_hline(x,             y + h - 1,     w, light);
    fb_vline(x + w - 1,     y,             h, light);
}

/* Horizontal gradient between two colours over a band. Used for the
 * title bar (XP-style was Luna gradient; for 95 we'll do a darker
 * left -> lighter right blue, with the bottom 1 pixel a sharp edge.) */
static u32 interp(u32 a, u32 b, int num, int den) {
    int ra = (a >> 16) & 0xFF, ga = (a >> 8) & 0xFF, ba = a & 0xFF;
    int rb = (b >> 16) & 0xFF, gb = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ra + (rb - ra) * num / den;
    int g = ga + (gb - ga) * num / den;
    int bb2 = ba + (bb - ba) * num / den;
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bb2;
}

void fb_hgradient(int x, int y, int w, int h, u32 left, u32 right) {
    if (!draw_target()) return;
    if (w <= 0) return;
    for (int c = 0; c < w; c++) {
        u32 col = interp(left, right, c, w - 1 > 0 ? w - 1 : 1);
        fb_fill_rect(x + c, y, 1, h, col);
    }
}
