#ifndef ZENBITE_KERNEL_H
#define ZENBITE_KERNEL_H

#include "types.h"
#include "boot.h"

#define ZENBITE_VERSION "3.1"
#define ZENBITE_NAME    "Zenbite"

void gdt_init(void);
void idt_init(void);
void pic_init(void);
void pit_init(u32 hz);
u32  pit_ticks(void);
void keyboard_init(void);

void irq_install_handler(u8 irq, void (*fn)(void));
void irq_uninstall_handler(u8 irq);

/* CPU detection. */
struct cpu_info {
    char vendor[13];
    char brand[49];
    u32  family;
    u32  model;
    int  long_mode;        /* 1 if 64-bit capable */
    int  sse;
    int  apic;
    /* Set when the EFLAGS-flip test fails -- on a 386 or very early
     * 486 the CPUID instruction is missing and `vendor`/`brand`/
     * `family`/`model` are best-effort guesses. */
    int  has_cpuid;
    /* CR4 + PSE (4 MiB pages) -- the paging setup uses 4 MiB pages,
     * which is Pentium+. On a 386/486 we skip paging entirely. */
    int  has_pse;
};

void cpu_detect(struct cpu_info *out);
const struct cpu_info *cpu_info(void);

/* Memory. */
void pmm_init(const struct boot_info *bi);
void *pmm_alloc_frame(void);
void pmm_free_frame(void *frame);
u32  pmm_total_kib(void);
u32  pmm_used_kib(void);
u32  pmm_free_kib(void);

void paging_init(void);
void kheap_init(void);
void *kmalloc(size_t n);
u32  kheap_used_kib(void);
u32  kheap_total_kib(void);
void  kfree(void *p);

/* Keyboard. */
int  kb_getc(void);            /* blocking */
int  kb_trygetc(void);         /* -1 if none */
int  kb_intr_pending(void);    /* Ctrl+C pressed since last consume? */
void kb_intr_consume(void);    /* clear the Ctrl+C latch */
void        kb_set_layout(const char *name);    /* "us" / "de" */
const char *kb_get_layout(void);
void kb_set_repeat(int delay_ms, int rate_cps); /* PS/2 typematic rate */
int  kb_get_repeat_delay(void);
int  kb_get_repeat_rate(void);
int  kb_alt_held(void);
int  kb_ctrl_held(void);
int  kb_shift_held(void);

/* PC speaker. */
void speaker_beep(u32 freq, u32 ms);
void speaker_off(void);
extern int g_sound_enabled;
extern int g_sound_vol;

/* Mouse. */
void mouse_init(void);
void mouse_get (int *col, int *row, int *buttons);  /* cell coords */
void mouse_set_bounds(int cols, int rows);
int  mouse_ps2_present(void);  /* 1 if PS/2 mouse detected */

/* Special key codes returned by kb_getc() / kb_trygetc() */
#define KB_UP    0x80
#define KB_DOWN  0x81
#define KB_LEFT  0x82
#define KB_RIGHT 0x83
#define KB_DEL   0x84
#define KB_HOME  0x85
#define KB_END   0x86
#define KB_PGUP  0x87
#define KB_PGDN  0x88
#define KB_F1    0x90
#define KB_F2    0x91
#define KB_F3    0x92
#define KB_F4    0x93
#define KB_F5    0x94
#define KB_F6    0x95
#define KB_F7    0x96
#define KB_F8    0x97
#define KB_F9    0x98
#define KB_F10   0x99
#define KB_F11   0x9A
#define KB_F12   0x9B
/* Windows / Super / GUI key. Either Win key sends the same code; the
 * Menu / app-key (right-click equivalent) sends KB_MENU. */
#define KB_WIN   0x8A
#define KB_MENU  0x8B

/* Clipboard (cross-app shared text buffer). */
void clipboard_set(const char *buf, int n);
int  clipboard_get(char *out, int max);
int  clipboard_len(void);

/* CMOS RTC. */
struct rtc_time {
    u8  sec, min, hour;
    u8  day, month;
    u16 year;
};
void rtc_read(struct rtc_time *t);
void rtc_write(const struct rtc_time *t);

/* Panic. */
__attribute__((noreturn)) void panic(const char *fmt, ...);

/* Reboot via triple-fault (good enough for v0.1). */
__attribute__((noreturn)) void reboot(void);
__attribute__((noreturn)) void shutdown(void);
__attribute__((noreturn)) void halt_forever(void);

#endif
