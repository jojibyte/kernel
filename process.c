#include "process.h"
#include "scheduler.h"
#include "heap.h"
#include "pmm.h"
#include "console.h"

static struct Process *process_table[MAX_PROCESSES];
static pid_t next_pid = 1;

static struct Process *current_process = NULL;

static struct Process idle_process;

static void strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = c;
    return s;
}

static pid_t alloc_pid(void) {
    for (pid_t i = 0; i < MAX_PROCESSES; i++) {
        pid_t pid = (next_pid + i) % MAX_PROCESSES;
        if (pid == 0) pid = 1;
        if (process_table[pid] == NULL) {
            next_pid = pid + 1;
            return pid;
        }
    }
    return -1;
}

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    
    memset(&idle_process, 0, sizeof(idle_process));
    idle_process.pid = 0;
    idle_process.state = PROC_STATE_READY;
    idle_process.priority = PRIORITY_IDLE;
    strncpy(idle_process.name, "idle", sizeof(idle_process.name));
    idle_process.address_space = vmm_get_kernel_address_space();
    
    process_table[0] = &idle_process;
    current_process = &idle_process;
}

static void kernel_thread_entry(void) {
    void (*entry)(void *);
    void *arg;
    
    __asm__ __volatile__(
        "pop %0\n"
        "pop %1\n"
        : "=r"(entry), "=r"(arg)
    );
    
    entry(arg);
    process_exit(0);
}

struct Process *process_create_kernel_thread(const char *name,
                                              void (*entry)(void *), void *arg) {
    struct Process *proc = kzalloc(sizeof(struct Process));
    if (!proc) return NULL;
    
    pid_t pid = alloc_pid();
    if (pid < 0) {
        kfree(proc);
        return NULL;
    }
    
    proc->kernel_stack = (uint64_t)kzalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        kfree(proc);
        return NULL;
    }
    
    proc->pid = pid;
    proc->ppid = current_process ? current_process->pid : 0;
    proc->state = PROC_STATE_CREATED;
    proc->priority = PRIORITY_NORMAL;
    strncpy(proc->name, name, sizeof(proc->name));
    proc->address_space = vmm_get_kernel_address_space();
    proc->uid = 0;
    proc->gid = 0;
    proc->start_time = timer_get_ticks();
    
    uint64_t *stack = (uint64_t *)(proc->kernel_stack + KERNEL_STACK_SIZE);
    
    *--stack = (uint64_t)arg;
    *--stack = (uint64_t)entry;
    
    proc->context.rsp = (uint64_t)stack;
    proc->context.rip = (uint64_t)kernel_thread_entry;
    proc->context.rbp = 0;
    
    process_table[pid] = proc;
    
    scheduler_add(proc);
    
    return proc;
}

struct Process *process_create(const char *name, virt_addr_t entry, bool user) {
    struct Process *proc = kzalloc(sizeof(struct Process));
    if (!proc) return NULL;
    
    pid_t pid = alloc_pid();
    if (pid < 0) {
        kfree(proc);
        return NULL;
    }
    
    proc->kernel_stack = (uint64_t)kzalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        kfree(proc);
        return NULL;
    }
    
    if (user) {
        proc->address_space = vmm_create_address_space();
        if (!proc->address_space) {
            kfree((void *)proc->kernel_stack);
            kfree(proc);
            return NULL;
        }
        
        proc->stack_bottom = 0x7FFFFFFFE000ULL;
    } else {
        proc->address_space = vmm_get_kernel_address_space();
    }
    
    proc->pid = pid;
    proc->ppid = current_process ? current_process->pid : 0;
    proc->state = PROC_STATE_CREATED;
    proc->priority = PRIORITY_NORMAL;
    strncpy(proc->name, name, sizeof(proc->name));
    proc->uid = user ? 1000 : 0;
    proc->gid = user ? 1000 : 0;
    proc->start_time = timer_get_ticks();
    
    proc->context.rsp = proc->kernel_stack + KERNEL_STACK_SIZE;
    proc->context.rip = entry;
    
    process_table[pid] = proc;
    
    scheduler_add(proc);
    
    return proc;
}

void process_destroy(struct Process *proc) {
    if (!proc || proc->pid == 0) return;
    
    process_table[proc->pid] = NULL;
    
    if (proc->address_space != vmm_get_kernel_address_space()) {
        vmm_destroy_address_space(proc->address_space);
    }
    
    kfree((void *)proc->kernel_stack);
    
    kfree(proc);
}

void process_exit(int code) {
    struct Process *proc = current_process;
    if (!proc || proc->pid == 0) {
        return;
    }
    
    proc->exit_code = code;
    proc->state = PROC_STATE_ZOMBIE;
    
    if (proc->parent && proc->parent->state == PROC_STATE_BLOCKED) {
        proc->parent->state = PROC_STATE_READY;
        scheduler_add(proc->parent);
    }
    
    scheduler_yield();
}

void process_yield(void) {
    scheduler_yield();
}

void process_sleep(uint64_t ms) {
    if (!current_process) return;
    
    current_process->sleep_until = timer_get_ticks() + ms;
    current_process->state = PROC_STATE_BLOCKED;
    
    scheduler_yield();
}

int process_wait(pid_t pid, int *status) {
    struct Process *child = process_get(pid);
    if (!child) return -ESRCH;
    
    if (child->ppid != current_process->pid) {
        return -ECHILD;
    }
    
    while (child->state != PROC_STATE_ZOMBIE) {
        current_process->state = PROC_STATE_BLOCKED;
        scheduler_yield();
    }
    
    if (status) {
        *status = child->exit_code;
    }
    
    process_destroy(child);
    return pid;
}

struct Process *process_current(void) {
    return current_process;
}

void process_set_current(struct Process *proc) {
    current_process = proc;
}

struct Process *process_get(pid_t pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return NULL;
    return process_table[pid];
}

void process_list(void) {
    static const char *state_names[] = {
        "CREATED", "READY", "RUNNING", "BLOCKED", "ZOMBIE", "DEAD"
    };
    
    kprintf("PID  PPID  STATE    PRI  NAME\n");
    kprintf("---  ----  -------  ---  ----\n");
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct Process *proc = process_table[i];
        if (proc) {
            kprintf("%3d  %4d  %-7s  %3d  %s\n",
                    proc->pid, proc->ppid,
                    state_names[proc->state],
                    proc->priority,
                    proc->name);
        }
    }
}
