#include "scheduler.h"
#include "process.h"
#include "x86_64.h"
#include "console.h"

static struct {
    struct Process *head;
    struct Process *tail;
} run_queues[PRIORITY_COUNT];

#define TIME_SLICE 10

static uint32_t current_slice = 0;

static volatile bool sched_lock = false;

static void lock_scheduler(void) {
    while (__sync_lock_test_and_set(&sched_lock, true)) {
        cpu_pause();
    }
}

static void unlock_scheduler(void) {
    __sync_lock_release(&sched_lock);
}

void scheduler_init(void) {
    for (int i = 0; i < PRIORITY_COUNT; i++) {
        run_queues[i].head = NULL;
        run_queues[i].tail = NULL;
    }
    
    process_init();
}

void scheduler_add(struct Process *proc) {
    if (!proc || proc->state == PROC_STATE_DEAD) return;
    
    lock_scheduler();
    
    proc->state = PROC_STATE_READY;
    proc->next = NULL;
    
    uint32_t pri = proc->priority;
    if (pri >= PRIORITY_COUNT) pri = PRIORITY_NORMAL;
    
    if (run_queues[pri].tail) {
        run_queues[pri].tail->next = proc;
        run_queues[pri].tail = proc;
    } else {
        run_queues[pri].head = proc;
        run_queues[pri].tail = proc;
    }
    
    unlock_scheduler();
}

void scheduler_remove(struct Process *proc) {
    if (!proc) return;
    
    lock_scheduler();
    
    for (int i = 0; i < PRIORITY_COUNT; i++) {
        struct Process *prev = NULL;
        struct Process *curr = run_queues[i].head;
        
        while (curr) {
            if (curr == proc) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    run_queues[i].head = curr->next;
                }
                
                if (curr == run_queues[i].tail) {
                    run_queues[i].tail = prev;
                }
                
                proc->next = NULL;
                unlock_scheduler();
                return;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    
    unlock_scheduler();
}

static struct Process *get_next_process(void) {
    for (int i = PRIORITY_COUNT - 1; i >= 0; i--) {
        if (run_queues[i].head) {
            struct Process *proc = run_queues[i].head;
            run_queues[i].head = proc->next;
            
            if (!run_queues[i].head) {
                run_queues[i].tail = NULL;
            }
            
            proc->next = NULL;
            return proc;
        }
    }
    
    return process_get(0);
}

static void schedule(void) {
    struct Process *current = process_current();
    struct Process *next;
    
    lock_scheduler();
    
    if (current && current->state == PROC_STATE_RUNNING) {
        current->state = PROC_STATE_READY;
        scheduler_add(current);
    }
    
    next = get_next_process();
    
    unlock_scheduler();
    
    if (next == current) {
        if (current) current->state = PROC_STATE_RUNNING;
        return;
    }
    
    next->state = PROC_STATE_RUNNING;
    process_set_current(next);
    
    current_slice = TIME_SLICE;
    
    if (next->address_space != vmm_get_kernel_address_space()) {
        vmm_switch_address_space(next->address_space);
    }
    
    if (current) {
        context_switch(&current->context, &next->context);
    } else {
        context_switch(&(struct CpuContext){0}, &next->context);
    }
}

void scheduler_yield(void) {
    uint64_t flags = save_irq();
    schedule();
    restore_irq(flags);
}

void scheduler_tick(void) {
    struct Process *current = process_current();
    
    if (current) {
        current->cpu_time++;
    }
    
    uint64_t now = timer_get_ticks();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct Process *proc = process_get(i);
        if (proc && proc->state == PROC_STATE_BLOCKED && 
            proc->sleep_until && proc->sleep_until <= now) {
            proc->sleep_until = 0;
            proc->state = PROC_STATE_READY;
            scheduler_add(proc);
        }
    }
    
    if (current_slice > 0) {
        current_slice--;
    }
    
    if (current_slice == 0 && current && current->pid != 0) {
        schedule();
    }
}

void __noreturn scheduler_start(void) {
    current_slice = TIME_SLICE;
    
    sti();
    schedule();
    
    for (;;) {
        hlt();
    }
}

#define PIT_FREQ 1193182
#define PIT_CH0  0x40
#define PIT_CMD  0x43

static volatile uint64_t timer_ticks = 0;
static uint32_t timer_freq = 0;

void timer_init(uint32_t frequency) {
    timer_freq = frequency;
    uint32_t divisor = PIT_FREQ / frequency;
    
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}

void timer_handler(void) {
    timer_ticks++;
    scheduler_tick();
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}
