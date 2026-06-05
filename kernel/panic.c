#include <stdarg.h>
#include "kernel.h"
#include "kio.h"
#include "io.h"
#include "vga.h"

__attribute__((noreturn))
void panic(const char *fmt, ...) {
    cli();
    vga_set_colour(VGA_WHITE, VGA_RED);
    kputs("\n*** ZENBITE PANIC: ");

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    kputs(buf);
    kputs(" ***\n");

    for (;;) hlt();
}

__attribute__((noreturn))
void reboot(void) {
    cli();
    /* Try every well-known reset path in order of cleanliness.
     * Each step waits briefly so the hardware has a chance to act. */
    /* System control port A: bit 0 = INIT (fast reset). Pre-PCI but
     * still wired on every modern chipset. */
    outb(0x92, inb(0x92) | 0x01);
    for (volatile int t = 0; t < 200000; t++) io_wait();
    /* 8042 keyboard controller pulse-reset. */
    for (int i = 0; i < 8; i++) {
        for (int t = 0; t < 1000; t++) {
            if (!(inb(0x64) & 0x02)) break;
            io_wait();
        }
        outb(0x64, 0xFE);
        for (volatile int t = 0; t < 200000; t++) io_wait();
    }
    /* PCI reset control register (ICH / PIIX et al). Two-phase: arm
     * the reset, then trigger it. Some chipsets need both bits, some
     * just bit 2 (RST_CPU). */
    outb(0xCF9, 0x02);
    for (volatile int t = 0; t < 50000; t++) io_wait();
    outb(0xCF9, 0x06);
    for (volatile int t = 0; t < 200000; t++) io_wait();
    outb(0xCF9, 0x0E);
    for (volatile int t = 0; t < 200000; t++) io_wait();
    /* Triple-fault: load a null IDT, then trigger an interrupt. */
    struct { u16 limit; u32 base; } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile("lidt (%0); int $0x03" :: "r"(&null_idt));
    for (;;) hlt();
}

__attribute__((noreturn))
void shutdown(void) {
    cli();
    /* Try every well-known VM power-off port. ACPI shutdown is the
     * proper answer on real hardware but requires reading and parsing
     * AML, which is well beyond v0.3. On a VM these ports work; on
     * real iron we fall through to halt_forever. */
    outw(0x604,  0x2000);        /* QEMU q35 / piix4 */
    outw(0xB004, 0x2000);        /* QEMU older */
    outw(0x4004, 0x3400);        /* VirtualBox */
    outw(0x4321, 0x3400);        /* VMware */
    /* Bochs / older QEMU sometimes wired to the legacy APM port. */
    outb(0x8900, 'S');
    outb(0x8900, 'h');
    outb(0x8900, 'u');
    outb(0x8900, 't');
    outb(0x8900, 'd');
    outb(0x8900, 'o');
    outb(0x8900, 'w');
    outb(0x8900, 'n');
    for (;;) hlt();
}

__attribute__((noreturn))
void halt_forever(void) {
    cli();
    for (;;) hlt();
}
