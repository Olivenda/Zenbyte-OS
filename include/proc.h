#ifndef ZENBITE_PROC_H
#define ZENBITE_PROC_H

#include "types.h"

/* Process states. */
#define PROC_FREE       0
#define PROC_READY      1
#define PROC_RUNNING    2
#define PROC_SLEEPING   3
#define PROC_ZOMBIE     4
#define PROC_KILLED     5

/* Priority levels. */
#define PRIO_LOW        0
#define PRIO_NORMAL     1
#define PRIO_HIGH       2
#define PRIO_REALTIME   3

#define PROC_MAX        8       /* max concurrent processes */
#define PROC_STACK_SIZE 4096    /* 4 KiB per process stack */
#define PROC_NAME_MAX   32

struct proc {
    u32  pid;
    u32  state;
    u32  esp;               /* saved stack pointer (points into stack[]) */
    u32  stack[PROC_STACK_SIZE / 4];
    char name[PROC_NAME_MAX];
    u32  priority;          /* PRIO_LOW .. PRIO_REALTIME */
    u32  cpu_ticks;         /* cumulative PIT ticks consumed */
    u32  start_tick;        /* PIT tick when spawned */
    int  exit_code;         /* valid when state == ZOMBIE */
    u32  parent_pid;        /* PID of parent (-1 = none) */
};

/* Scheduler API. */
void proc_init(void);
void proc_yield(void);
void proc_sleep(u32 ms);
int  proc_running(void);

/* Process management. */
int  proc_create(const char *name, void (*entry)(void), u32 priority);
void proc_kill(u32 pid);
void proc_set_priority(u32 pid, u32 priority);
int  proc_wait(void);              /* yield until current process exits */

/* Process table queries. */
struct proc_info {
    u32  pid;
    u32  state;
    char name[PROC_NAME_MAX];
    u32  priority;
    u32  cpu_ticks;
    u32  start_tick;
    int  exit_code;
};
int  proc_enumerate(struct proc_info *out, int max);

/* Tick accounting (call from PIT handler). */
void proc_tick_account(void);

/* Context switch (ctxsw.asm). */
extern void ctx_switch(u32 *save_sp, u32 load_sp);

#endif
