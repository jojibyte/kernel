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

#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGSTKFLT   16
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22
#define SIGURG      23
#define SIGXCPU     24
#define SIGXFSZ     25
#define SIGVTALRM   26
#define SIGPROF     27
#define SIGWINCH    28
#define SIGIO       29
#define SIGPWR      30
#define SIGSYS      31
#define NSIG        32

#define EPERM       1
#define ENOENT      2
#define ESRCH       3
#define EINTR       4
#define EIO         5
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define ENODEV      19
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define EMFILE      24
#define ENOSPC      28
#define EROFS       30
#define ENOEXEC     8
#define ECHILD      10
#define EAGAIN      11

#define SIG_DFL     ((void (*)(int))0)
#define SIG_IGN     ((void (*)(int))1)
#define SIG_ERR     ((void (*)(int))-1)

#define SA_NOCLDSTOP    0x00000001
#define SA_NOCLDWAIT    0x00000002
#define SA_SIGINFO      0x00000004
#define SA_RESTORER     0x04000000
#define SA_ONSTACK      0x08000000
#define SA_RESTART      0x10000000
#define SA_NODEFER      0x40000000
#define SA_RESETHAND    0x80000000

struct Sigaction {
    void (*sa_handler)(int);
    uint64_t sa_flags;
    void (*sa_restorer)(void);
    uint64_t sa_mask;
};

struct SignalFrame {
    uint64_t signal_pending;
    uint64_t signal_blocked;
    struct Sigaction handlers[NSIG];
};

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

struct UserContext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
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
    struct UserContext user_context;
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
    
    struct SignalFrame signal_frame;
    bool in_syscall;
    int64_t syscall_return;
    
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
int process_execve(const char *path, char *const argv[], char *const envp[]);

int process_kill(pid_t pid, int sig);
int process_signal(struct Process *proc, int sig);
int process_sigaction(int sig, const struct Sigaction *act,
                      struct Sigaction *oldact);
void process_check_signals(struct Process *proc);

struct Process *process_current(void);
void process_set_current(struct Process *proc);
struct Process *process_get(pid_t pid);
void process_list(void);

extern void context_switch(struct CpuContext *old, struct CpuContext *next_ctx);

#endif
