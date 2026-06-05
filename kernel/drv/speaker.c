/* PC speaker driver: PIT channel 2 + port 0x61.
 * speaker_beep(freq, ms) plays a square-wave tone then silences.
 * g_sound_enabled = 0 suppresses all beeps (settable from Settings).
 * g_sound_vol is stored but has no physical effect on the PC speaker
 * (it's 1-bit hardware); it exists so Settings can display/persist it
 * and future sound hardware can honour it. */

#include "io.h"
#include "kernel.h"

int g_sound_enabled = 1;
int g_sound_vol     = 8;    /* 1..16, full-volume default */

void speaker_off(void) {
    outb(0x61, inb(0x61) & ~3);
}

void speaker_beep(u32 freq, u32 ms) {
    if (!g_sound_enabled || freq < 20 || freq > 20000) return;
    /* Program PIT channel 2 for a square wave at the requested freq. */
    u32 divisor = 1193180 / freq;
    outb(0x43, 0xB6);                       /* ch2, mode 3, binary */
    outb(0x42, (u8)(divisor & 0xFF));
    outb(0x42, (u8)((divisor >> 8) & 0xFF));
    /* Gate: bit 0 of port 0x61 = PIT ch2 gate; bit 1 = speaker output. */
    outb(0x61, inb(0x61) | 3);
    /* Wait for ms milliseconds via PIT ticks (1000 Hz = 1 ms / tick). */
    u32 ticks = ms;
    u32 t0 = pit_ticks();
    while ((u32)(pit_ticks() - t0) < ticks)
        __asm__ volatile("hlt");
    speaker_off();
}
