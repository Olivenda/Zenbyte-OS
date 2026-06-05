#ifndef ZENBITE_PROC_H
#define ZENBITE_PROC_H

#include "types.h"

/* Simple cooperative multitasking.
 * No full process scheduler -- just yield() to let other code run.
 * The timer IRQ preempts every ~10 ms for responsive mouse/keyboard. */

void proc_init(void);
void proc_yield(void);      /* give other code a chance to run */
void proc_sleep(u32 ms);    /* sleep for N milliseconds */
int  proc_running(void);    /* is scheduler active? */

#endif
