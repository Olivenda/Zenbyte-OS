/* CPU identification with graceful degradation for pre-Pentium hardware.
 *
 * The 386 and very early 486s lack the CPUID instruction; executing
 * it would raise #UD. We probe for CPUID by trying to toggle the ID
 * bit (bit 21) in EFLAGS -- if the bit can't be flipped, CPUID is
 * absent and we treat the chip as a 386/486 with no extended info.
 *
 * Family/model detection follows the same two-tier path: rely on
 * CPUID where available, otherwise distinguish 386 from 486 using the
 * AC (alignment-check) bit (bit 18) in EFLAGS, which is reserved on
 * the 386 but writable on the 486+. */
#include "kernel.h"
#include "string.h"

static struct cpu_info info;

static inline int cpu_has_cpuid_eflags(void) {
    u32 a, b;
    __asm__ volatile(
        "pushfl                 \n"
        "pushfl                 \n"
        "xorl $0x00200000, (%%esp)\n"
        "popfl                  \n"
        "pushfl                 \n"
        "popl %0                \n"
        "popl %1                \n"
        : "=r"(a), "=r"(b)
        :
        : "cc");
    return ((a ^ b) & 0x00200000) != 0;
}

/* True on 486+ (AC bit writable). False on 386 (AC reserved). */
static inline int cpu_has_ac_eflags(void) {
    u32 a, b;
    __asm__ volatile(
        "pushfl                 \n"
        "pushfl                 \n"
        "xorl $0x00040000, (%%esp)\n"
        "popfl                  \n"
        "pushfl                 \n"
        "popl %0                \n"
        "popl %1                \n"
        : "=r"(a), "=r"(b)
        :
        : "cc");
    return ((a ^ b) & 0x00040000) != 0;
}

static inline void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0));
}

void cpu_detect(struct cpu_info *out) {
    /* Defaults for the no-CPUID path. */
    info.has_cpuid = 0;
    info.has_pse   = 0;
    info.long_mode = 0;
    info.sse       = 0;
    info.apic      = 0;
    info.brand[0]  = '\0';

    if (!cpu_has_cpuid_eflags()) {
        /* Pre-CPUID: distinguish 386 from 486 via the AC EFLAGS bit. */
        if (cpu_has_ac_eflags()) {
            info.family = 4;
            strncpy(info.vendor, "Intel486    ", sizeof info.vendor);
            strncpy(info.brand,  "Intel 80486 (no CPUID)",
                     sizeof info.brand);
        } else {
            info.family = 3;
            strncpy(info.vendor, "Intel386    ", sizeof info.vendor);
            strncpy(info.brand,  "Intel 80386 (no CPUID, no PSE)",
                     sizeof info.brand);
        }
        info.model = 0;
        info.vendor[12] = '\0';
        if (out) *out = info;
        return;
    }

    info.has_cpuid = 1;
    u32 a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    u32 max_basic = a;
    *(u32 *)&info.vendor[0] = b;
    *(u32 *)&info.vendor[4] = d;
    *(u32 *)&info.vendor[8] = c;
    info.vendor[12] = '\0';

    if (max_basic >= 1) {
        cpuid(1, &a, &b, &c, &d);
        info.family = (a >> 8) & 0xF;
        info.model  = (a >> 4) & 0xF;
        if (info.family == 0xF) info.family += (a >> 20) & 0xFF;
        if (info.family == 0xF || info.family == 0x6)
            info.model |= ((a >> 16) & 0xF) << 4;
        info.sse     = (d >> 25) & 1;
        info.apic    = (d >>  9) & 1;
        info.has_pse = (d >>  3) & 1;
    }

    /* Extended feature flags (long mode, brand string). */
    u32 max_ext;
    cpuid(0x80000000, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000001) {
        cpuid(0x80000001, &a, &b, &c, &d);
        info.long_mode = (d >> 29) & 1;
    }

    if (max_ext >= 0x80000004) {
        u32 *brand = (u32 *)info.brand;
        cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        info.brand[48] = '\0';
    } else {
        info.brand[0] = '\0';
    }

    if (out) *out = info;
}

const struct cpu_info *cpu_info(void) { return &info; }
