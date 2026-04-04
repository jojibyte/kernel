#ifndef _USERMODE_H
#define _USERMODE_H

#include "types.h"
#include "vmm.h"
#include "process.h"

#define RING3_CODE_SELECTOR      0x23
#define RING3_DATA_SELECTOR      0x1B

#define USERSPACE_CODE_BASE      0x0000000000400000ULL
#define USERSPACE_DATA_BASE      0x0000000000600000ULL
#define USERSPACE_HEAP_BASE      0x00007FFFFF000000ULL
#define USERSPACE_STACK_TOP      0x00007FFFFFFFE000ULL
#define USERSPACE_STACK_SIZE     (64 * KB)

#define SYSCALL_VECTOR           0x80

#define MSR_GS_BASE_SYNTHETIC       0xC0000101
#define MSR_KERNEL_GS_SYNTHETIC     0xC0000102

typedef enum {
    TRANSITION_IRETQ,
    TRANSITION_SYSRET,
    TRANSITION_DIRECT
} TransitionMethod;

typedef enum {
    VALIDATION_PASSED,
    VALIDATION_FAILED_ADDR,
    VALIDATION_FAILED_PERMS,
    VALIDATION_ANOMALY
} ValidationResult;

struct UserContextFrame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

struct SyscallVector {
    uint64_t syscall_number;
    uint64_t args[6];
    uint64_t return_value;
};

struct PerCpuUsermodeData {
    uint64_t kernel_stack_apex;
    uint64_t user_stack_cache;
    struct Process *current_process;
    uint64_t syscall_depth_counter;
    uint64_t context_checksum;
};

struct UsermodeManager;

typedef void (*TransitionExecutor)(struct UsermodeManager *matrix, 
                                      struct UserContextFrame *frame);
typedef ValidationResult (*AddressValidator)(struct UsermodeManager *matrix,
                                                   virt_addr_t addr, uint64_t size);
typedef int (*StackAllocator)(struct UsermodeManager *matrix,
                                 struct Process *proc);
typedef int (*MemoryRegionMapper)(struct UsermodeManager *matrix,
                                     struct Process *proc,
                                     virt_addr_t virt, size_t pages, uint64_t flags);
typedef void (*TssKernelStackUpdater)(struct UsermodeManager *matrix,
                                         uint64_t kernel_stack_top);
typedef void (*GsBaseConfigurator)(struct UsermodeManager *matrix,
                                      struct PerCpuUsermodeData *neural_data);

struct UsermodeManager {
    TransitionExecutor execute_ring3_transition;
    AddressValidator validate_user_address;
    StackAllocator allocate_user_stack;
    MemoryRegionMapper map_user_region;
    TssKernelStackUpdater update_tss_rsp0;
    GsBaseConfigurator configure_gs_base;
    
    struct PerCpuUsermodeData *cpu_data;
    TransitionMethod preferred_transition;
    
    uint64_t transition_count;
    uint64_t validation_failures;
    uint64_t anomaly_counter;
    
};

struct SyscallDispatcher;

typedef int64_t (*SyscallHandler)(struct SyscallDispatcher *dispatcher,
                                     struct SyscallVector *vector);
typedef void (*SyscallRegistrar)(struct SyscallDispatcher *dispatcher,
                                    uint64_t syscall_num, SyscallHandler handler);
typedef void (*MsrConfigurator)(struct SyscallDispatcher *dispatcher);

struct SyscallDispatcher {
    SyscallHandler *handlers;
    uint64_t handler_count;
    SyscallRegistrar register_handler;
    MsrConfigurator configure_syscall_msrs;
    
    uint64_t total_syscalls_processed;
    uint64_t invalid_syscall_attempts;
    
    uint64_t dispatch_latency;
};

struct UsermodeManager *usermode_manager_create(void);
void usermode_manager_destroy(struct UsermodeManager *matrix);
struct UsermodeManager *usermode_manager_get(void);

struct SyscallDispatcher *syscall_dispatcher_create(uint64_t max_syscalls);
void syscall_dispatcher_destroy(struct SyscallDispatcher *dispatcher);

void usermode_initialize_subsystem(void);
int usermode_spawn_process(struct Process *proc, virt_addr_t entry_point,
                              const void *code_data, size_t code_size);
void usermode_enter(struct Process *proc);

void usermode_update_kernel_stack(struct Process *proc);

extern void usermode_ring3_transition_stub(uint64_t entry, uint64_t stack,
                                            uint64_t code_sel, uint64_t data_sel);

extern void usermode_syscall_entry(void);

#endif
