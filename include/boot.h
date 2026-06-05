#ifndef ZENBITE_BOOT_H
#define ZENBITE_BOOT_H

#include "types.h"

#define BOOT_INFO_MAGIC 0x5A454E42u  /* 'ZENB' */

struct e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 acpi;
} __attribute__((packed));

struct boot_info {
    u32 magic;
    u32 mem_lower_kib;     /* < 1 MiB */
    u32 mem_upper_kib;     /* between 1 MiB and 16 MiB (from E801) */
    u32 e820_count;
    u32 e820_addr;
    u8  boot_drive;
    u8  _pad[3];
    /* VESA / VBE linear framebuffer. fb_addr == 0 means stage2 ran on
     * a BIOS without VBE support (or VBE mode-set failed) and the
     * kernel should stay in VGA text mode. When non-zero, the kernel
     * renders the shadow text-cell buffer as bitmap glyphs into this
     * framebuffer instead of touching 0xB8000. */
    u32 fb_addr;
    u32 fb_pitch;          /* bytes per scan-line */
    u16 fb_width;
    u16 fb_height;
    u8  fb_bpp;
    u8  _pad2[3];
} __attribute__((packed));

#endif
