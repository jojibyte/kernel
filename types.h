#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

typedef signed char             int8_t;
typedef unsigned char           uint8_t;
typedef signed short            int16_t;
typedef unsigned short          uint16_t;
typedef signed int              int32_t;
typedef unsigned int            uint32_t;
typedef signed long long        int64_t;
typedef unsigned long long      uint64_t;

typedef uint64_t                size_t;
typedef int64_t                 ssize_t;
typedef int64_t                 ptrdiff_t;

typedef uint64_t                uintptr_t;
typedef int64_t                 intptr_t;
typedef uint64_t                phys_addr_t;
typedef uint64_t                virt_addr_t;

typedef int32_t                 pid_t;
typedef uint32_t                uid_t;
typedef uint32_t                gid_t;
typedef uint32_t                mode_t;
typedef int64_t                 off_t;
typedef int64_t                 time_t;

typedef uint32_t                dev_t;
typedef uint64_t                ino_t;
typedef uint64_t                blkcnt_t;
typedef uint32_t                blksize_t;

#include <stdbool.h>

#ifndef NULL
#define NULL                    ((void *)0)
#endif

#define ARRAY_SIZE(arr)         (sizeof(arr) / sizeof((arr)[0]))
#define ALIGN_UP(x, align)      (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align)    ((x) & ~((align) - 1))
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))

#define BIT(n)                  (1ULL << (n))
#define SET_BIT(x, n)           ((x) |= BIT(n))
#define CLEAR_BIT(x, n)         ((x) &= ~BIT(n))
#define TEST_BIT(x, n)          (((x) & BIT(n)) != 0)

#define KB                      (1024ULL)
#define MB                      (1024ULL * KB)
#define GB                      (1024ULL * MB)

#define PAGE_SIZE               4096ULL
#define PAGE_SHIFT              12
#define PAGE_MASK               (~(PAGE_SIZE - 1))

#define __packed                __attribute__((packed))
#define __aligned(x)            __attribute__((aligned(x)))
#define __section(x)            __attribute__((section(x)))
#define __unused                __attribute__((unused))
#define __noreturn              __attribute__((noreturn))
#define __always_inline         inline __attribute__((always_inline))
#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

#define barrier()               __asm__ __volatile__("" ::: "memory")
#define mb()                    __asm__ __volatile__("mfence" ::: "memory")

#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EIO             5
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EBUSY           16
#define EEXIST          17
#define ENODEV          19
#define ENOTDIR         20
#define EINVAL          22
#define EBADF           9
#define ENOSYS          38

#define container_of(ptr, type, member) ({                      \
    const typeof(((type *)0)->member) *__mptr = (ptr);          \
    (type *)((char *)__mptr - offsetof(type, member)); })

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
