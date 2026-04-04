#include "process.h"
#include "scheduler.h"
#include "heap.h"
#include "pmm.h"
#include "console.h"
#include "elf.h"
#include "usermode.h"
#include "vfs.h"
#include "kstring.h"

static struct Process *process_table[MAX_PROCESSES];
static pid_t next_pid = 1;

static struct Process *current_process = NULL;

static struct Process idle_process;

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
    kmemset(process_table, 0, sizeof(process_table));
    
    kmemset(&idle_process, 0, sizeof(idle_process));
    idle_process.pid = 0;
    idle_process.state = PROC_STATE_READY;
    idle_process.priority = PRIORITY_IDLE;
    kstrncpy(idle_process.name, "idle", sizeof(idle_process.name));
    idle_process.address_space = vmm_get_kernel_address_space();
    
    for (int i = 0; i < NSIG; i++) {
        idle_process.signal_frame.handlers[i].sa_handler = SIG_DFL;
    }
    
    process_table[0] = &idle_process;
    current_process = &idle_process;
}

__attribute__((naked)) static void kernel_thread_entry(void) {
    __asm__ __volatile__(
        "pop %%rax\n"
        "pop %%rdi\n"
        "call *%%rax\n"
        "mov $0, %%edi\n"
        "call process_exit\n"
        : : : "memory"
    );
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
    kstrncpy(proc->name, name, sizeof(proc->name));
    proc->address_space = vmm_get_kernel_address_space();
    proc->uid = 0;
    proc->gid = 0;
    proc->start_time = timer_get_ticks();
    
    for (int i = 0; i < NSIG; i++) {
        proc->signal_frame.handlers[i].sa_handler = SIG_DFL;
    }
    
    uint64_t *stack = (uint64_t *)(proc->kernel_stack + KERNEL_STACK_SIZE);
    
    *--stack = (uint64_t)arg;
    *--stack = (uint64_t)entry;
    
    proc->context.rsp = (uint64_t)stack;
    proc->context.rip = (uint64_t)kernel_thread_entry;
    proc->context.rbp = 0;
    
    if (current_process) {
        proc->parent = current_process;
        proc->sibling = current_process->children;
        current_process->children = proc;
    }
    
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
    kstrncpy(proc->name, name, sizeof(proc->name));
    proc->uid = user ? 1000 : 0;
    proc->gid = user ? 1000 : 0;
    proc->euid = proc->uid;
    proc->egid = proc->gid;
    proc->start_time = timer_get_ticks();
    
    for (int i = 0; i < NSIG; i++) {
        proc->signal_frame.handlers[i].sa_handler = SIG_DFL;
    }
    
    proc->context.rsp = proc->kernel_stack + KERNEL_STACK_SIZE;
    proc->context.rip = entry;
    
    if (current_process) {
        proc->parent = current_process;
        proc->sibling = current_process->children;
        current_process->children = proc;
    }
    
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
    
    struct Process *child = proc->children;
    while (child) {
        child->parent = process_get(1);
        child->ppid = 1;
        child = child->sibling;
    }
    
    if (proc->parent) {
        process_signal(proc->parent, SIGCHLD);
        
        if (proc->parent->state == PROC_STATE_BLOCKED) {
            proc->parent->state = PROC_STATE_READY;
            scheduler_add(proc->parent);
        }
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
    struct Process *child = NULL;
    
    if (pid > 0) {
        child = process_get(pid);
        if (!child) return -ESRCH;
        if (child->ppid != current_process->pid) return -ECHILD;
    } else if (pid == -1) {
        child = current_process->children;
        if (!child) return -ECHILD;
    } else {
        return -EINVAL;
    }
    
    while (1) {
        struct Process *c = (pid > 0) ? child : current_process->children;
        while (c) {
            if (c->state == PROC_STATE_ZOMBIE) {
                pid_t ret_pid = c->pid;
                if (status) {
                    *status = (c->exit_code & 0xFF) << 8;
                }
                
                if (c->parent) {
                    struct Process **pp = &c->parent->children;
                    while (*pp && *pp != c) pp = &(*pp)->sibling;
                    if (*pp) *pp = c->sibling;
                }
                
                process_destroy(c);
                return ret_pid;
            }
            c = (pid > 0) ? NULL : c->sibling;
        }
        
        current_process->state = PROC_STATE_BLOCKED;
        scheduler_yield();
    }
}

pid_t process_fork(void) {
    struct Process *parent = current_process;
    if (!parent) return -ESRCH;
    
    struct Process *child = kzalloc(sizeof(struct Process));
    if (!child) return -ENOMEM;
    
    pid_t child_pid = alloc_pid();
    if (child_pid < 0) {
        kfree(child);
        return -EAGAIN;
    }
    
    child->kernel_stack = (uint64_t)kzalloc(KERNEL_STACK_SIZE);
    if (!child->kernel_stack) {
        kfree(child);
        return -ENOMEM;
    }
    
    child->address_space = vmm_clone_address_space(parent->address_space);
    if (!child->address_space) {
        kfree((void *)child->kernel_stack);
        kfree(child);
        return -ENOMEM;
    }
    
    child->pid = child_pid;
    child->ppid = parent->pid;
    kstrncpy(child->name, parent->name, sizeof(child->name));
    
    child->state = PROC_STATE_READY;
    child->priority = parent->priority;
    child->exit_code = 0;
    
    kmemcpy(&child->context, &parent->context, sizeof(struct CpuContext));
    kmemcpy(&child->user_context, &parent->user_context, sizeof(struct UserContext));
    
    child->user_stack = parent->user_stack;
    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;
    child->stack_bottom = parent->stack_bottom;
    
    for (int i = 0; i < MAX_FDS; i++) {
        child->fds[i] = parent->fds[i];
    }
    
    child->uid = parent->uid;
    child->gid = parent->gid;
    child->euid = parent->euid;
    child->egid = parent->egid;
    
    child->start_time = timer_get_ticks();
    child->cpu_time = 0;
    
    kmemcpy(&child->signal_frame, &parent->signal_frame, sizeof(struct SignalFrame));
    child->signal_frame.signal_pending = 0;
    
    
    child->parent = parent;
    child->sibling = parent->children;
    parent->children = child;
    child->children = NULL;
    
    process_table[child_pid] = child;
    
    child->syscall_return = 0;
    
    scheduler_add(child);
    
    kprintf("[FORK] Created child PID %d from parent PID %d\n", child_pid, parent->pid);
    
    return child_pid;
}

int process_execve(const char *path, char *const argv[], char *const envp[]) {
    struct Process *proc = current_process;
    if (!proc) return -ESRCH;
    
    struct VfsNode *node = vfs_resolve_path(path);
    if (!node) return -ENOENT;
    
    size_t file_size = node->size;
    void *elf_data = kmalloc(file_size);
    if (!elf_data) return -ENOMEM;
    
    ssize_t bytes_read = vfs_read(node, elf_data, file_size, 0);
    if (bytes_read < 0 || (size_t)bytes_read != file_size) {
        kfree(elf_data);
        return -EIO;
    }
    
    ElfValidationResult valid = elf_validate(elf_data, file_size);
    if (valid != ELF_VALIDATION_SUCCESS) {
        kfree(elf_data);
        return -ENOEXEC;
    }
    
    struct AddressSpace *old_as = proc->address_space;
    proc->address_space = vmm_create_address_space();
    if (!proc->address_space) {
        proc->address_space = old_as;
        kfree(elf_data);
        return -ENOMEM;
    }
    
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }
    
    struct UsermodeManager *matrix = usermode_manager_get();
    int stack_result = matrix->allocate_user_stack(matrix, proc);
    if (stack_result < 0) {
        vmm_destroy_address_space(proc->address_space);
        proc->address_space = old_as;
        kfree(elf_data);
        return stack_result;
    }
    
    struct ElfLoadInfo load_info;
    ElfLoadResult load_result = elf_load_executable(proc, elf_data, file_size, &load_info);
    if (load_result != ELF_LOAD_SUCCESS) {
        vmm_destroy_address_space(proc->address_space);
        proc->address_space = old_as;
        kfree(elf_data);
        return -ENOEXEC;
    }
    
    int setup_result = elf_setup_stack(proc, argc, (char **)argv, (char **)envp, &load_info);
    if (setup_result < 0) {
        vmm_destroy_address_space(proc->address_space);
        proc->address_space = old_as;
        kfree(elf_data);
        return setup_result;
    }
    
    if (old_as != vmm_get_kernel_address_space()) {
        vmm_destroy_address_space(old_as);
    }
    
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') name = p + 1;
        p++;
    }
    kstrncpy(proc->name, name, sizeof(proc->name));
    
    for (int i = 0; i < NSIG; i++) {
        if (proc->signal_frame.handlers[i].sa_handler != SIG_IGN) {
            proc->signal_frame.handlers[i].sa_handler = SIG_DFL;
        }
    }
    proc->signal_frame.signal_pending = 0;
    
    proc->context.rip = load_info.entry_point;
    proc->context.rsp = proc->user_stack;
    
    kfree(elf_data);
    
    kprintf("[EXECVE] Process %d now running '%s' at 0x%llx\n",
            proc->pid, proc->name, (unsigned long long)load_info.entry_point);
    
    return 0;
}

int process_kill(pid_t pid, int sig) {
    if (sig < 0 || sig >= NSIG) return -EINVAL;
    
    if (pid > 0) {
        struct Process *proc = process_get(pid);
        if (!proc) return -ESRCH;
        return process_signal(proc, sig);
    } else if (pid == 0) {
        return -ESRCH;
    } else if (pid == -1) {
        int count = 0;
        for (int i = 1; i < MAX_PROCESSES; i++) {
            if (process_table[i] && process_table[i] != current_process) {
                process_signal(process_table[i], sig);
                count++;
            }
        }
        return count > 0 ? 0 : -ESRCH;
    } else {
        return -ESRCH;
    }
}

int process_signal(struct Process *proc, int sig) {
    if (!proc || sig < 0 || sig >= NSIG) return -EINVAL;
    
    if (sig == 0) return 0;
    
    if (sig == SIGKILL || sig == SIGSTOP) {
        proc->signal_frame.signal_pending |= (1ULL << sig);
        
        if (sig == SIGKILL) {
            if (proc->state == PROC_STATE_BLOCKED) {
                proc->state = PROC_STATE_READY;
                scheduler_add(proc);
            }
        }
        return 0;
    }
    
    void (*handler)(int) = proc->signal_frame.handlers[sig].sa_handler;
    
    if (handler == SIG_IGN) {
        return 0;
    }
    
    proc->signal_frame.signal_pending |= (1ULL << sig);
    
    if (proc->state == PROC_STATE_BLOCKED) {
        proc->state = PROC_STATE_READY;
        scheduler_add(proc);
    }
    
    return 0;
}

int process_sigaction(int sig, const struct Sigaction *act,
                      struct Sigaction *oldact) {
    struct Process *proc = current_process;
    if (!proc) return -ESRCH;
    
    if (sig < 1 || sig >= NSIG) return -EINVAL;
    if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;
    
    if (oldact) {
        kmemcpy(oldact, &proc->signal_frame.handlers[sig], sizeof(struct Sigaction));
    }
    
    if (act) {
        kmemcpy(&proc->signal_frame.handlers[sig], act, sizeof(struct Sigaction));
    }
    
    return 0;
}

void process_check_signals(struct Process *proc) {
    if (!proc || !proc->signal_frame.signal_pending) return;
    
    for (int sig = 1; sig < NSIG; sig++) {
        if (!(proc->signal_frame.signal_pending & (1ULL << sig))) continue;
        if (proc->signal_frame.signal_blocked & (1ULL << sig)) continue;
        
        proc->signal_frame.signal_pending &= ~(1ULL << sig);
        
        void (*handler)(int) = proc->signal_frame.handlers[sig].sa_handler;
        
        if (handler == SIG_IGN) {
            continue;
        }
        
        if (handler == SIG_DFL) {
            switch (sig) {
            case SIGCHLD:
            case SIGCONT:
            case SIGURG:
            case SIGWINCH:
                continue;
                
            case SIGSTOP:
            case SIGTSTP:
            case SIGTTIN:
            case SIGTTOU:
                proc->state = PROC_STATE_BLOCKED;
                return;
                
            case SIGKILL:
            case SIGTERM:
            case SIGINT:
            case SIGQUIT:
            case SIGABRT:
            case SIGSEGV:
            case SIGBUS:
            case SIGFPE:
            case SIGILL:
            default:
                kprintf("[SIGNAL] Process %d killed by signal %d\n", proc->pid, sig);
                proc->exit_code = sig;
                proc->state = PROC_STATE_ZOMBIE;
                if (proc->parent) {
                    process_signal(proc->parent, SIGCHLD);
                }
                return;
            }
        }
        
        kprintf("[SIGNAL] Process %d: would call handler for signal %d\n", proc->pid, sig);
    }
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
