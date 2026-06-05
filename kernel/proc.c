/* proc.c -- Process table and cooperative context-switch scheduler.
 *
 * Each process gets a 4 KiB stack and runs until it calls proc_yield().
 * The scheduler picks the next READY process by priority (highest first),
 * then round-robin within the same priority level.
 *
 * Process 0 is the desktop -- it is never created via proc_create();
 * it is always READY and uses the kernel's boot stack.
 */
#include "kernel.h"
#include "proc.h"
#include "string.h"

struct proc procs[PROC_MAX];
int current_pid = 0;
static int scheduler_active = 0;
static u32 next_pid = 1;       /* PID 0 is reserved for the desktop */

/* ── Scheduler internals ────────────────────────────────────────────── */

static inline u32 prio_score(u32 p) { return p; }

/* Find the next process to run. Returns -1 if nothing is ready. */
static int find_next(void) {
    int best = -1;
    u32 best_prio = 0;
    /* Round-robin: start searching after current_pid so we don't
     * just starve lower-PID processes. */
    for (int i = 1; i < PROC_MAX; i++) {
        int idx = (current_pid + i) % PROC_MAX;
        if (procs[idx].state == PROC_READY) {
            if (best < 0 || prio_score(procs[idx].priority) > best_prio) {
                best = idx;
                best_prio = prio_score(procs[idx].priority);
            }
        }
    }
    return best;
}

/* ── Init ───────────────────────────────────────────────────────────── */

void proc_init(void) {
    memset(procs, 0, sizeof procs);
    /* PID 0 = desktop, always ready, uses the boot stack. */
    procs[0].pid   = 0;
    procs[0].state  = PROC_READY;
    procs[0].esp    = 0;            /* not used -- desktop never switches out */
    procs[0].priority = PRIO_NORMAL;
    strncpy(procs[0].name, "desktop", PROC_NAME_MAX - 1);
    current_pid = 0;
    next_pid = 1;
    scheduler_active = 1;
}

int proc_running(void) { return scheduler_active; }

/* ── Context switch ─────────────────────────────────────────────────── */

void proc_yield(void) {
    if (!scheduler_active) return;
    int cur = current_pid;
    int nxt = find_next();
    if (nxt < 0 || nxt == cur) return;
    procs[cur].state = PROC_READY;
    procs[nxt].state = PROC_RUNNING;
    current_pid = nxt;
    ctx_switch(&procs[cur].esp, procs[nxt].esp);
    /* We return here when someone switches back to us. */
}

void proc_sleep(u32 ms) {
    if (!scheduler_active) {
        u32 target = pit_ticks() + ms;
        while (pit_ticks() < target) __asm__ volatile("sti; hlt; cli");
        return;
    }
    int cur = current_pid;
    procs[cur].state = PROC_SLEEPING;
    u32 wake = pit_ticks() + ms;
    /* Yield in 10 ms slices so we don't sleep forever if tick wraps. */
    while (pit_ticks() < wake) {
        int nxt = find_next();
        if (nxt < 0) {
            __asm__ volatile("sti; hlt; cli");
            continue;
        }
        procs[cur].state = PROC_SLEEPING;
        procs[nxt].state = PROC_RUNNING;
        int prev = current_pid;
        current_pid = nxt;
        ctx_switch(&procs[cur].esp, procs[nxt].esp);
    }
    procs[cur].state = PROC_READY;
}

/* ── Process creation ───────────────────────────────────────────────── */

/* Entry trampoline: wraps a user entry function and marks the process
 * as ZOMBIE when it returns, then yields so the parent notices.
 * The entry pointer is read from proc_entry_fn_table[current_pid]. */
typedef void (*proc_entry_fn)(void);
static proc_entry_fn proc_entry_fn_table[PROC_MAX];

static void proc_trampoline(void) {
    int my = current_pid;
    proc_entry_fn fn = proc_entry_fn_table[my];
    fn();
    /* Command finished. Mark zombie and yield. */
    procs[my].state = PROC_ZOMBIE;
    proc_yield();   /* never returns */
    for (;;) __asm__ volatile("cli; hlt");
}

int proc_create(const char *name, void (*entry)(void), u32 priority) {
    /* Find a free slot. */
    int slot = -1;
    for (int i = 1; i < PROC_MAX; i++) {
        if (procs[i].state == PROC_FREE || procs[i].state == PROC_ZOMBIE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    memset(&procs[slot], 0, sizeof(struct proc));
    procs[slot].pid      = next_pid++;
    procs[slot].state    = PROC_READY;
    procs[slot].priority = priority;
    procs[slot].parent_pid = current_pid;
    procs[slot].start_tick = pit_ticks();
    procs[slot].exit_code  = -1;
    strncpy(procs[slot].name, name, PROC_NAME_MAX - 1);
    procs[slot].name[PROC_NAME_MAX - 1] = '\0';

    /* Save entry pointer for the trampoline. */
    proc_entry_fn_table[slot] = (proc_entry_fn)entry;

    /* Build a fake context-switch frame on the process stack.
     *
     * ctx_switch does: popa (8 regs) -> popfd -> ret.
     * popa pops EAX ECX EDX EBX _(skip)_ EBP ESI EDI in ascending
     * address order.  So ESP must point to the EAX slot and the
     * registers must be laid out at consecutive ascending addresses.
     *
     * Frame layout starting at procs[slot].esp:
     *   [esp +  0] = EAX              (stack[slot*1024 + 0])
     *   [esp +  4] = ECX              (stack[slot*1024 + 1])
     *   [esp +  8] = EDX              (stack[slot*1024 + 2])
     *   [esp + 12] = EBX              (stack[slot*1024 + 3])
     *   [esp + 16] = ESP (discarded)  (stack[slot*1024 + 4])
     *   [esp + 20] = EBP              (stack[slot*1024 + 5])
     *   [esp + 24] = ESI              (stack[slot*1024 + 6])
     *   [esp + 28] = EDI              (stack[slot*1024 + 7])
     *   [esp + 32] = EFLAGS           (stack[slot*1024 + 8])
     *   [esp + 36] = return address   (stack[slot*1024 + 9]) -> proc_trampoline
     */
    procs[slot].stack[0]  = 0;                    /* EAX */
    procs[slot].stack[1]  = 0;                    /* ECX */
    procs[slot].stack[2]  = 0;                    /* EDX */
    procs[slot].stack[3]  = 0;                    /* EBX */
    procs[slot].stack[4]  = 0;                    /* ESP (discarded by popa) */
    procs[slot].stack[5]  = 0;                    /* EBP */
    procs[slot].stack[6]  = 0;                    /* ESI */
    procs[slot].stack[7]  = 0;                    /* EDI */
    procs[slot].stack[8]  = 0x200;                /* EFLAGS (IF=1) */
    procs[slot].stack[9]  = (u32)proc_trampoline; /* return address for ret */
    procs[slot].esp = (u32)&procs[slot].stack[0];

    return procs[slot].pid;
}

/* ── Kill / priority ────────────────────────────────────────────────── */

void proc_kill(u32 pid) {
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].pid == pid && procs[i].state != PROC_FREE) {
            procs[i].state = PROC_KILLED;
            procs[i].exit_code = -1;
            return;
        }
    }
}

void proc_set_priority(u32 pid, u32 priority) {
    if (priority > PRIO_REALTIME) priority = PRIO_REALTIME;
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].pid == pid && procs[i].state != PROC_FREE) {
            procs[i].priority = priority;
            return;
        }
    }
}

int proc_wait(void) {
    /* Yield until the current process is killed or no child is running.
     * Simplified: just yield once. */
    proc_yield();
    return 0;
}

/* ── Enumeration (for Activity Monitor) ─────────────────────────────── */

int proc_enumerate(struct proc_info *out, int max) {
    int count = 0;
    for (int i = 0; i < PROC_MAX && count < max; i++) {
        if (procs[i].state == PROC_FREE) continue;
        out[count].pid        = procs[i].pid;
        out[count].state      = procs[i].state;
        out[count].priority   = procs[i].priority;
        out[count].cpu_ticks  = procs[i].cpu_ticks;
        out[count].start_tick = procs[i].start_tick;
        out[count].exit_code  = procs[i].exit_code;
        strncpy(out[count].name, procs[i].name, PROC_NAME_MAX - 1);
        out[count].name[PROC_NAME_MAX - 1] = '\0';
        count++;
    }
    return count;
}

/* ── Tick accounting (called from PIT IRQ handler path) ─────────────── */
void proc_tick_account(void) {
    if (current_pid >= 0 && current_pid < PROC_MAX)
        procs[current_pid].cpu_ticks++;
}
