#include "types.h"
#include "x86_64.h"
#include "process.h"
#include "vfs.h"
#include "console.h"
#include "heap.h"
#include "pmm.h"
#include "vmm.h"

#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSEEK       8
#define SYS_MMAP        9
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_IOCTL       16
#define SYS_GETPID      39
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61
#define SYS_KILL        62
#define SYS_UNAME       63
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_MKDIR       83
#define SYS_RMDIR       84
#define SYS_GETUID      102
#define SYS_GETGID      104
#define SYS_SETUID      105
#define SYS_SETGID      106
#define SYS_GETEUID     107
#define SYS_GETEGID     108
#define SYS_GETPPID     110
#define SYS_NANOSLEEP   35
#define SYS_CLOCK_GETTIME 228

struct SyscallArgs {
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
};

static void strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static int64_t sys_write(int fd, const void *buf, size_t count) {
    if (fd == 1 || fd == 2) {
        console_write(buf, count);
        return count;
    }
    
    return -EBADF;
}

static int64_t sys_read(int fd, void *buf, size_t count) {
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
    return -ENOSYS;
}

static int64_t sys_execve(const char *path, char *const argv[], char *const envp[]) {
    (void)path;
    (void)argv;
    (void)envp;
    return -ENOSYS;
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
    case SYS_FORK:
        return sys_fork();
    case SYS_EXECVE:
        return sys_execve((const char *)args->arg1, 
                          (char *const *)args->arg2,
                          (char *const *)args->arg3);
    case SYS_UNAME:
        return sys_uname((struct Utsname *)args->arg1);
    case SYS_NANOSLEEP:
        return sys_nanosleep((const struct Timespec *)args->arg1,
                             (struct Timespec *)args->arg2);
    case SYS_MKDIR:
        return sys_mkdir((const char *)args->arg1, args->arg2);
    case SYS_CHDIR:
        return sys_chdir((const char *)args->arg1);
    default:
        kprintf("[SYSCALL] Unknown syscall: %llu\n", (unsigned long long)syscall_num);
        return -ENOSYS;
    }
}
