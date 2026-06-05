/* Paging is intentionally LEFT DISABLED.
 *
 * Zenbite runs in flat 32-bit protected mode: every segment has base 0
 * and a 4 GiB limit (see gdt.c), so a linear address already equals the
 * physical address. The kernel uses no virtual-memory features -- no
 * demand paging, no per-process address spaces, no copy-on-write -- so
 * turning paging on buys nothing and costs portability:
 *
 *   - 4 MiB pages (PSE) are Pentium+. Enabling CR4.PSE on a 386/486
 *     either faults (no CR4) or is silently ignored, leaving a broken
 *     half-mapped state.
 *   - Even on CPUs that report PSE, a subtly-wrong page directory maps
 *     code to the wrong physical frame and the next instruction fetch
 *     decodes garbage -> #UD / #GP, which is exactly the "Invalid
 *     opcode" panic that showed up under qemu-system-i386 -cpu 486 and
 *     -cpu pentium.
 *
 * Running flat with paging off makes the 386, 486, Pentium and modern
 * CPUs behave identically and keeps all 4 GiB of physical space
 * (including MMIO BARs for AHCI / e1000 / EHCI) directly reachable.
 *
 * The function is kept as a no-op so the kmain call site and any future
 * "real paging when userland lands" work have a home. */
#include "kernel.h"

void paging_init(void) {
    /* Deliberately empty -- see file comment. */
}
