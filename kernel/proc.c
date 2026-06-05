/* Simple cooperative multitasking.
 *
 * No process table, no context switching -- just yield() to let
 * the desktop event loop run, and sleep() for timed delays.
 * The timer IRQ drives preemption: every ~10 ms it fires and
 * can interrupt any long-running loop.
 */
#include "kernel.h"
#include "proc.h"

static int scheduler_active = 0;

void proc_init(void) {
    scheduler_active = 1;
}

int proc_running(void) {
    return scheduler_active;
}

/* Yield: let the desktop + other code run for one tick.
 * The timer IRQ will fire within ~10 ms and can preempt us. */
void proc_yield(void) {
    if (!scheduler_active) return;
    __asm__ volatile("sti; hlt; cli");
}

/* Sleep for approximately N milliseconds.
 * Uses PIT ticks (1000 Hz) for timing. */
void proc_sleep(u32 ms) {
    u32 target = pit_ticks() + ms;
    while (pit_ticks() < target) {
        __asm__ volatile("sti; hlt; cli");
    }
}
