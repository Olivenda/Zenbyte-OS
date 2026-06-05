#include "kernel.h"
#include "boot.h"
#include "kio.h"
#include "vga.h"
#include "string.h"
#include "fs.h"
#include "disk.h"
#include "io.h"
#include "env.h"
#include "usb.h"
#include "net.h"
#include "proc.h"

extern int fs_init(void);

extern void shell_main(void);
extern int  install_main  (int argc, char **argv);
extern int  install_g_main(int argc, char **argv);
extern int  install_system_installed(void);

/* Text-mode boot splash. Drawn after vga_init() before the kmain
 * progress trace starts. Stays for ~1 second so the user sees the
 * version cleanly, then the trace overwrites it.
 *
 * The logo is composed from a tiny 5x5 font: each letter is a
 * 5-row x 5-col stencil of '#' / ' '. We render '#' as 0xDB (full
 * block) and ' ' as a real space, with one column of spacing
 * between letters. Centred horizontally on an 80-col screen. */
static void boot_splash(void) {
    vga_set_colour(VGA_WHITE, VGA_BLUE);
    vga_clear();

    /* 5x5 stencils for Z E N B I T (E reused). */
    static const char *LZ[5] = {"#####","   # ","  #  "," #   ","#####"};
    static const char *LE[5] = {"#####","#    ","#### ","#    ","#####"};
    static const char *LN[5] = {"#   #","##  #","# # #","#  ##","#   #"};
    static const char *LB[5] = {"#### ","#   #","#### ","#   #","#### "};
    static const char *LI[5] = {"#####","  #  ","  #  ","  #  ","#####"};
    static const char *LT[5] = {"#####","  #  ","  #  ","  #  ","  #  "};
    static const char **logo[7] = { LZ, LE, LN, LB, LI, LT, LE };

    int letter_w = 5;
    int gap = 1;
    int total_w = 7 * letter_w + 6 * gap;       /* 41 cells */
    int x0 = (80 - total_w) / 2;
    int y0 = 5;
    for (int row = 0; row < 5; row++) {
        int x = x0;
        for (int l = 0; l < 7; l++) {
            const char *line = logo[l][row];
            for (int c = 0; c < letter_w; c++) {
                u8 ch = (line[c] == '#') ? 0xDB : ' ';
                vga_put_cell(y0 + row, x + c, (char)ch, VGA_WHITE, VGA_BLUE);
            }
            x += letter_w + gap;
        }
    }

    /* Subtitle + version. */
    const char *sub = "32-bit retro operating system";
    int sub_len = 0; while (sub[sub_len]) sub_len++;
    vga_write(y0 + 7, (80 - sub_len) / 2, sub, VGA_WHITE, VGA_BLUE);
    char vbuf[40];
    int n = ksnprintf(vbuf, sizeof vbuf,
                      "version %s  -  %s", ZENBITE_VERSION, ZENBITE_NAME);
    vga_write(y0 + 8, (80 - n) / 2, vbuf, VGA_YELLOW, VGA_BLUE);

    const char *msg = "booting, please wait ...";
    int mlen = 0; while (msg[mlen]) mlen++;
    vga_write(y0 + 11, (80 - mlen) / 2, msg, VGA_LIGHT_CYAN, VGA_BLUE);

    /* Progress-bar frame centred on row 22. */
    int bar_w = 30;
    int bar_x = (80 - bar_w) / 2;
    vga_put_cell(22, bar_x,             '[', VGA_WHITE, VGA_BLUE);
    vga_put_cell(22, bar_x + bar_w - 1, ']', VGA_WHITE, VGA_BLUE);
    for (int i = 1; i < bar_w - 1; i++)
        vga_put_cell(22, bar_x + i, 0xB0, VGA_LIGHT_CYAN, VGA_BLUE);
    /* Short splash pause. Previously 100M-iteration spin -- on slow
     * CPUs / single-thread VMs that costs multiple seconds of boot
     * time. 1M is ~50 ms on real iron, ~100 ms in QEMU. */
    for (volatile int i = 0; i < 1000000; i++) ;
    /* Reset colour FIRST so the subsequent vga_clear paints the
     * screen in normal grey-on-black, not white-on-blue. */
    vga_set_colour(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

static void banner(void) {
    vga_set_colour(VGA_LIGHT_CYAN, VGA_BLACK);
    kputs("\n");
    kputs("====================================================\n");
    kprintf("        " ZENBITE_NAME " v%s  --  32-bit retro OS\n", ZENBITE_VERSION);
    kputs("        MIT License (c) 2026 Zenbite contributors\n");
    kputs("====================================================\n");
    vga_set_colour(VGA_LIGHT_GREY, VGA_BLACK);
}

static void show_cpu(void) {
    const struct cpu_info *c = cpu_info();
    kprintf("CPU   : %s", c->vendor);
    if (!c->has_cpuid)
        kprintf("  family %u (pre-CPUID, paging disabled)\n", c->family);
    else if (c->long_mode)
        kputs("  64-bit capable -- running in 32-bit compatibility mode\n");
    else
        kputs("  32-bit only\n");
    if (c->brand[0]) kprintf("        %s\n", c->brand);
}

extern char _kernel_end[];
static void show_mem(void) {
    u32 low_kib    = 640;                                /* low-mem hole */
    u32 kernel_kib = ((u32)_kernel_end - 0x100000 + 1023) / 1024;
    u32 total_kib  = low_kib + kernel_kib + pmm_total_kib();
    u32 used_kib   = kernel_kib;
    kprintf("Memory: %u KiB total, %u KiB used (kernel), %u KiB free\n",
            total_kib, used_kib, pmm_free_kib());
}

void kmain(struct boot_info *bi) {
    serial_init();
    vga_init();
    boot_splash();
    kputs("kmain: serial+vga up\n");

    /* Each kmain stage prints what it actually probed. On a hang the
     * last line tells the user exactly which step wedged, instead of
     * just "stuck after kmain: gdt+idt+pic up" with no clue what's
     * next on the wire. */
    u32 t0 = pit_ticks();
    gdt_init();           kputs("  .. gdt\n");
    idt_init();           kputs("  .. idt\n");
    pic_init();           kputs("  .. pic (8259A remap)\n");
    kputs("kmain: gdt+idt+pic up\n");

    sti();
    kputs("  .. interrupts enabled\n");

    pit_init(1000);       kputs("  .. pit @ 1000 Hz\n");
    keyboard_init();      kputs("  .. ps/2 keyboard\n");
    mouse_init();         kputs("  .. ps/2 mouse\n");
    kputs("kmain: timer+kbd+mouse up\n");

    /* USB controllers. Detection prints which class shows up; useful
     * when running under a hypervisor (UTM / QEMU q35) where the
     * default is xhci -- in that case neither uhci nor ehci match
     * and HID keyboard/mouse won't enumerate (use `-device piix3-
     * usb-uhci` or UTM's "USB 2.0" toggle to expose EHCI/UHCI). */
    u32 t_usb = pit_ticks();
    kputs("  .. probing USB controllers ...\n");
    usb_init();           /* UHCI: keyboard / mouse / mass-storage HID */
    ehci_init();          /* EHCI: USB 2.0 mass storage + HID kbd/mouse */
    /* Summarise input device state so the user knows what's available. */
    {
        int ps2_mouse = mouse_ps2_present();
        int usb_k = usb_kbd_present();
        int usb_m = usb_mouse_present();
        kprintf("  Input: PS/2 kbd=yes PS/2 mouse=%s USB kbd=%s USB mouse=%s\n",
                ps2_mouse ? "yes" : "no",
                usb_k ? "yes" : "no",
                usb_m ? "yes" : "no");
    }
    kprintf("kmain: usb done (%u ms)\n", pit_ticks() - t_usb);

    cpu_detect(NULL);     kputs("  .. cpu detected\n");
    u32 t_mm = pit_ticks();
    pmm_init(bi);         kputs("  .. pmm\n");
    paging_init();        kputs("  .. paging\n");
    kheap_init();         kputs("  .. kheap\n");
    env_init();           kputs("  .. env\n");
    proc_init();          kputs("  .. scheduler\n");
    kprintf("kmain: mm+env up (%u ms)\n", pit_ticks() - t_mm);

    u32 t_fs = pit_ticks();
    if (fs_init() != 0) {
        kputs("warning: FAT12 init failed; DIR/TYPE will not work\n");
    }
    kprintf("kmain: fs done (%u ms)\n", pit_ticks() - t_fs);

    /* Load persistent desktop settings (keymap, theme, autostart, ...). */
    extern void config_load(void);
    config_load();
    kputs("  .. config loaded\n");

    /* Network: probe both the legacy NE2000 and the modern Intel e1000.
     * e1000_init runs after ne2000_init so it overrides the iface when
     * both are present (e1000 wins on hardware that has it). */
    ne2000_init();
    e1000_init();
    net_init();
    kputs("kmain: net done\n");

    banner();
    show_cpu();
    show_mem();
    kprintf("Boot time: %u ms total\n", pit_ticks() - t0);

    /* First-boot detection: if no mounted drive has \SYSTEM\ZENBITE.SYS
     * we drop the user into the setup wizard rather than the shell. */
    if (!install_system_installed()) {
        kputs("\nNo installed Zenbite system found on any mounted disk.\n");
        kputs("Launching setup wizard ...\n");
        /* Use the graphical installer when BGA is available. It keeps
         * graphics mode active throughout (no mode-switch), so the
         * green/black-stripe bug is gone. Falls back to text on machines
         * without a VBE-capable display. */
        install_g_main(1, NULL);
    }

    /* CONFIG.TXT may ask us to start a desktop instead of the shell.
     * `start_gdesk` wins over `start_desktop` if both are set --
     * gdesk falls back to the cell desktop transparently when the
     * BGA framebuffer isn't available, so the user always gets a
     * usable UI. When the chosen desktop exits we fall through to
     * the shell so the command line is reachable. */
    extern int  desktop_get_start_desktop(void);
    extern int  desktop_get_start_gdesk(void);
    extern int  desktop_main  (int argc, char **argv);
    extern int  g_desktop_main(int argc, char **argv);
    /* Safety: if ESC is held when we hit the autostart check, skip
     * the desktop entirely. Lets the user recover from a black-
     * screen gdesk on a machine without a usable VBE mode -- type
     * `desktop` for the text WM or just stay in the shell. */
    int boot_esc = 0;
    for (int i = 0; i < 4; i++) {
        int k = kb_trygetc();
        if (k == 27) { boot_esc = 1; break; }
    }
    if (!boot_esc) {
        if (desktop_get_start_gdesk())        g_desktop_main(0, NULL);
        else if (desktop_get_start_desktop()) desktop_main  (0, NULL);
    } else {
        kputs("(ESC at boot -- skipping desktop autostart)\n");
    }

    shell_main();

    panic("shell returned");
}
