#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "types.h"

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
#define SYS_RT_SIGACTION 13
#define SYS_RT_SIGPROCMASK 14
#define SYS_IOCTL       16
#define SYS_ACCESS      21
#define SYS_PIPE        22
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_NANOSLEEP   35
#define SYS_GETPID      39
#define SYS_CLONE       56
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61
#define SYS_KILL        62
#define SYS_UNAME       63
#define SYS_FCNTL       72
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_MKDIR       83
#define SYS_GETUID      102
#define SYS_GETGID      104
#define SYS_GETEUID     107
#define SYS_GETEGID     108
#define SYS_GETPPID     110
#define SYS_CLOCK_GETTIME 228

struct SyscallArgs {
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
};

int64_t syscall_handler(uint64_t syscall_num, struct SyscallArgs *args);

void syscall_init(void);

#endif
