#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "types.h"
#include "process.h"

void scheduler_init(void);
void __noreturn scheduler_start(void);
void scheduler_add(struct Process *proc);
void scheduler_remove(struct Process *proc);
void scheduler_yield(void);
void scheduler_tick(void);

void timer_init(uint32_t frequency);
uint64_t timer_get_ticks(void);

#endif
