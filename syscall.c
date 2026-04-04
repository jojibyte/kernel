#include "types.h"
#include "x86_64.h"
#include "process.h"
#include "vfs.h"
#include "console.h"
#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "syscall.h"
#include "net.h"
#include "uaccess.h"

static void strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static int64_t sys_write(int fd, const void *buf, size_t count) {
    if (!access_ok(buf, count)) return -EFAULT;

    if (fd == 1 || fd == 2) {
        console_write(buf, count);
        return count;
    }
    
    return -EBADF;
}

static int64_t sys_read(int fd, void *buf, size_t count) {
    if (!access_ok(buf, count)) return -EFAULT;

    if (fd == 0) {
        char *p = buf;
        for (size_t i = 0; i < count; i++) {
            if (!keyboard_has_input()) break;
            p[i] = keyboard_getchar();
        }
        return count;
    }
    
    return -EBADF;
}

static int64_t sys_open(const char *path, int flags, mode_t mode) {
    (void)path;
    (void)flags;
    (void)mode;
    return -ENOSYS;
}

static int64_t sys_close(int fd) {
    (void)fd;
    return 0;
}

static int64_t sys_getpid(void) {
    struct Process *proc = process_current();
    return proc ? proc->pid : 0;
}

static int64_t sys_getppid(void) {
    struct Process *proc = process_current();
    return proc ? proc->ppid : 0;
}

static int64_t sys_getuid(void) {
    struct Process *proc = process_current();
    return proc ? proc->uid : 0;
}

static int64_t sys_getgid(void) {
    struct Process *proc = process_current();
    return proc ? proc->gid : 0;
}

static int64_t sys_geteuid(void) {
    struct Process *proc = process_current();
    return proc ? proc->euid : 0;
}

static int64_t sys_getegid(void) {
    struct Process *proc = process_current();
    return proc ? proc->egid : 0;
}

static int64_t sys_exit(int code) {
    process_exit(code);
    return 0;
}

static int64_t sys_fork(void) {
    return process_fork();
}

static int64_t sys_execve(const char *path, char *const argv[], char *const envp[]) {
    if (!access_ok(path, 1)) return -EFAULT;
    return process_execve(path, argv, envp);
}

#define CLONE_VM        0x00000100
#define CLONE_FS        0x00000200
#define CLONE_FILES     0x00000400
#define CLONE_SIGHAND   0x00000800
#define CLONE_THREAD    0x00010000

static int64_t sys_clone(uint64_t flags, void *child_stack,
                         void *ptid, void *ctid, uint64_t tls) {
    (void)ptid;
    (void)ctid;
    (void)tls;

    if (flags & CLONE_THREAD) {
        return -ENOSYS;
    }

    if (child_stack && !access_ok(child_stack, 8))
        return -EFAULT;

    pid_t child = process_fork();
    if (child < 0)
        return child;

    if (child == 0 && child_stack) {
        struct Process *proc = process_current();
        if (proc)
            proc->user_stack = (uint64_t)child_stack;
    }

    return child;
}

static int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage) {
    (void)options;
    (void)rusage;
    return process_wait(pid, status);
}

static int64_t sys_kill(pid_t pid, int sig) {
    return process_kill(pid, sig);
}

static int64_t sys_rt_sigaction(int sig, const struct Sigaction *act,
                                struct Sigaction *oldact, size_t sigsetsize) {
    (void)sigsetsize;
    return process_sigaction(sig, act, oldact);
}

static int64_t sys_rt_sigprocmask(int how, const uint64_t *set, uint64_t *oldset, size_t sigsetsize) {
    (void)sigsetsize;
    struct Process *proc = process_current();
    if (!proc) return -ESRCH;
    
    if (oldset) {
        *oldset = proc->signal_frame.signal_blocked;
    }
    
    if (set) {
        switch (how) {
        case 0:
            proc->signal_frame.signal_blocked |= *set;
            break;
        case 1:
            proc->signal_frame.signal_blocked &= ~(*set);
            break;
        case 2:
            proc->signal_frame.signal_blocked = *set;
            break;
        default:
            return -EINVAL;
        }
        
        proc->signal_frame.signal_blocked &= ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
    }
    
    return 0;
}

static int64_t sys_brk(void *addr) {
    struct Process *proc = process_current();
    if (!proc) return -ESRCH;
    
    if (addr == NULL) {
        return proc->heap_end;
    }
    
    virt_addr_t new_brk = (virt_addr_t)addr;
    
    if (new_brk < proc->heap_start) {
        return proc->heap_end;
    }
    
    if (new_brk > proc->heap_end) {
        virt_addr_t old_page = ALIGN_UP(proc->heap_end, PAGE_SIZE);
        virt_addr_t new_page = ALIGN_UP(new_brk, PAGE_SIZE);
        
        for (virt_addr_t p = old_page; p < new_page; p += PAGE_SIZE) {
            phys_addr_t phys = pmm_alloc_page();
            if (!phys) {
                return proc->heap_end;
            }
            vmm_map_page(p, phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        }
    }
    
    proc->heap_end = new_brk;
    return new_brk;
}

struct Utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

static int64_t sys_uname(struct Utsname *buf) {
    if (!buf) return -EFAULT;
    
    strncpy(buf->sysname, "kernel", 65);
    strncpy(buf->nodename, "localhost", 65);
    strncpy(buf->release, "0.1.0", 65);
    strncpy(buf->version, "x86-64 Kernel by jojibyte", 65);
    strncpy(buf->machine, "x86_64", 65);
    
    return 0;
}

struct Timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static int64_t sys_nanosleep(const struct Timespec *req, struct Timespec *rem) {
    if (!req) return -EFAULT;
    
    uint64_t ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    process_sleep(ms);
    
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    
    return 0;
}

static int64_t sys_mkdir(const char *path, mode_t mode) {
    return vfs_mkdir(path, mode);
}

static int64_t sys_chdir(const char *path) {
    (void)path;
    return -ENOSYS;
}

int64_t syscall_handler(uint64_t syscall_num, struct SyscallArgs *args) {
    switch (syscall_num) {
    case SYS_READ:
        return sys_read(args->arg1, (void *)args->arg2, args->arg3);
    case SYS_WRITE:
        return sys_write(args->arg1, (const void *)args->arg2, args->arg3);
    case SYS_OPEN:
        return sys_open((const char *)args->arg1, args->arg2, args->arg3);
    case SYS_CLOSE:
        return sys_close(args->arg1);
    case SYS_BRK:
        return sys_brk((void *)args->arg1);
    case SYS_RT_SIGACTION:
        return sys_rt_sigaction(args->arg1, 
                                (const struct Sigaction *)args->arg2,
                                (struct Sigaction *)args->arg3,
                                args->arg4);
    case SYS_RT_SIGPROCMASK:
        return sys_rt_sigprocmask(args->arg1,
                                  (const uint64_t *)args->arg2,
                                  (uint64_t *)args->arg3,
                                  args->arg4);
    case SYS_GETPID:
        return sys_getpid();
    case SYS_GETPPID:
        return sys_getppid();
    case SYS_GETUID:
        return sys_getuid();
    case SYS_GETGID:
        return sys_getgid();
    case SYS_GETEUID:
        return sys_geteuid();
    case SYS_GETEGID:
        return sys_getegid();
    case SYS_EXIT:
        return sys_exit(args->arg1);
    case SYS_CLONE:
        return sys_clone(args->arg1, (void *)args->arg2,
                         (void *)args->arg3, (void *)args->arg4, args->arg5);
    case SYS_FORK:
        return sys_fork();
    case SYS_EXECVE:
        return sys_execve((const char *)args->arg1, 
                          (char *const *)args->arg2,
                          (char *const *)args->arg3);
    case SYS_WAIT4:
        return sys_wait4(args->arg1, (int *)args->arg2, args->arg3, (void *)args->arg4);
    case SYS_KILL:
        return sys_kill(args->arg1, args->arg2);
    case SYS_UNAME:
        return sys_uname((struct Utsname *)args->arg1);
    case SYS_NANOSLEEP:
        return sys_nanosleep((const struct Timespec *)args->arg1,
                             (struct Timespec *)args->arg2);
    case SYS_MKDIR:
        return sys_mkdir((const char *)args->arg1, args->arg2);
    case SYS_CHDIR:
        return sys_chdir((const char *)args->arg1);
    case SYS_SOCKET:
        return sys_socket(args->arg1, args->arg2, args->arg3);
    case SYS_BIND:
        return sys_bind(args->arg1, (const struct SockaddrIn *)args->arg2);
    case SYS_SENDTO:
        return sys_sendto(args->arg1, (const void *)args->arg2, args->arg3,
                          args->arg4, (const struct SockaddrIn *)args->arg5);
    case SYS_RECVFROM:
        return sys_recvfrom(args->arg1, (void *)args->arg2, args->arg3,
                            args->arg4, (struct SockaddrIn *)args->arg5);
    default:
        kprintf("[SYSCALL] Unknown syscall: %llu\n", (unsigned long long)syscall_num);
        return -ENOSYS;
    }
}
