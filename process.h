#ifndef _PROCESS_H
#define _PROCESS_H

#include "types.h"
#include "vmm.h"
#include "x86_64.h"

typedef enum {
    PROC_STATE_CREATED,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_ZOMBIE,
    PROC_STATE_DEAD
} ProcState;

#define PRIORITY_IDLE       0
#define PRIORITY_LOW        1
#define PRIORITY_NORMAL     2
#define PRIORITY_HIGH       3
#define PRIORITY_REALTIME   4
#define PRIORITY_COUNT      5

#define MAX_PROCESSES       256
#define MAX_FDS             64
#define KERNEL_STACK_SIZE   (16 * KB)

struct CpuContext {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint64_t rip;
};

struct FileDescriptor {
    int type;
    void *data;
    off_t offset;
    uint32_t flags;
};

struct Process {
    pid_t pid;
    pid_t ppid;
    char name[32];
    
    ProcState state;
    int exit_code;
    uint32_t priority;
    
    struct CpuContext context;
    uint64_t kernel_stack;
    uint64_t user_stack;
    
    struct AddressSpace *address_space;
    virt_addr_t heap_start;
    virt_addr_t heap_end;
    virt_addr_t stack_bottom;
    
    struct FileDescriptor fds[MAX_FDS];
    
    uid_t uid;
    gid_t gid;
    uid_t euid;
    gid_t egid;
    
    uint64_t start_time;
    uint64_t cpu_time;
    uint64_t sleep_until;
    
    struct Process *next;
    struct Process *wait_queue;
    
    struct Process *parent;
    struct Process *children;
    struct Process *sibling;
};

void process_init(void);
struct Process *process_create(const char *name, virt_addr_t entry, bool user);
struct Process *process_create_kernel_thread(const char *name, void (*entry)(void *), void *arg);
void process_destroy(struct Process *proc);
void process_exit(int code);
void process_yield(void);
void process_sleep(uint64_t ms);
int process_wait(pid_t pid, int *status);
pid_t process_fork(void);

struct Process *process_current(void);
void process_set_current(struct Process *proc);
struct Process *process_get(pid_t pid);
void process_list(void);

extern void context_switch(struct CpuContext *old, struct CpuContext *new);

#endif
