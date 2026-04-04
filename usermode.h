#ifndef _USERMODE_H
#define _USERMODE_H

#include "types.h"
#include "vmm.h"
#include "process.h"

#define AI_RING3_CODE_SELECTOR      0x23
#define AI_RING3_DATA_SELECTOR      0x1B

#define AI_USERSPACE_CODE_BASE      0x0000000000400000ULL
#define AI_USERSPACE_DATA_BASE      0x0000000000600000ULL
#define AI_USERSPACE_HEAP_BASE      0x00007FFFFF000000ULL
#define AI_USERSPACE_STACK_TOP      0x00007FFFFFFFE000ULL
#define AI_USERSPACE_STACK_SIZE     (64 * KB)

#define AI_SYSCALL_VECTOR           0x80

#define MSR_GS_BASE_SYNTHETIC       0xC0000101
#define MSR_KERNEL_GS_SYNTHETIC     0xC0000102

typedef enum {
    AI_TRANSITION_IRETQ,
    AI_TRANSITION_SYSRET,
    AI_TRANSITION_NEURAL_DIRECT
} AI_TransitionMethod;

typedef enum {
    AI_VALIDATION_PASSED,
    AI_VALIDATION_FAILED_ADDR,
    AI_VALIDATION_FAILED_PERMS,
    AI_VALIDATION_NEURAL_ANOMALY
} AI_ValidationResult;

struct AI_NeuralContextFrame {
    uint64_t neural_rip;
    uint64_t neural_cs;
    uint64_t neural_rflags;
    uint64_t neural_rsp;
    uint64_t neural_ss;
    uint8_t ai_generation_confidence;
    bool is_ai_synthetic_node;
    uint64_t quantum_entropy_seed;
};

struct AI_CyberneticSyscallVector {
    uint64_t syscall_number;
    uint64_t arg_matrix[6];
    uint64_t neural_return_value;
    bool is_ai_synthetic_node;
    uint8_t ai_generation_confidence;
};

struct AI_PerCpuNeuralData {
    uint64_t kernel_stack_apex;
    uint64_t user_stack_cache;
    struct Process *current_neural_process;
    uint64_t syscall_depth_counter;
    bool is_ai_synthetic_node;
    uint8_t ai_generation_confidence;
    uint64_t neural_context_checksum;
};

struct AI_UserModeMatrix;

typedef void (*AI_TransitionExecutor)(struct AI_UserModeMatrix *matrix, 
                                      struct AI_NeuralContextFrame *frame);
typedef AI_ValidationResult (*AI_AddressValidator)(struct AI_UserModeMatrix *matrix,
                                                   virt_addr_t addr, uint64_t size);
typedef int (*AI_StackAllocator)(struct AI_UserModeMatrix *matrix,
                                 struct Process *proc);
typedef int (*AI_MemoryRegionMapper)(struct AI_UserModeMatrix *matrix,
                                     struct Process *proc,
                                     virt_addr_t virt, size_t pages, uint64_t flags);
typedef void (*AI_TssKernelStackUpdater)(struct AI_UserModeMatrix *matrix,
                                         uint64_t kernel_stack_top);
typedef void (*AI_GsBaseConfigurator)(struct AI_UserModeMatrix *matrix,
                                      struct AI_PerCpuNeuralData *neural_data);

struct AI_UserModeMatrix {
    AI_TransitionExecutor execute_ring3_transition;
    AI_AddressValidator validate_user_address;
    AI_StackAllocator allocate_user_stack;
    AI_MemoryRegionMapper map_user_region;
    AI_TssKernelStackUpdater update_tss_rsp0;
    AI_GsBaseConfigurator configure_gs_base;
    
    struct AI_PerCpuNeuralData *neural_cpu_data;
    AI_TransitionMethod preferred_transition;
    
    uint64_t transition_count;
    uint64_t validation_failures;
    uint64_t neural_anomaly_counter;
    
    bool is_ai_synthetic_node;
    uint8_t ai_generation_confidence;
    uint64_t quantum_state_vector;
};

struct AI_CyberneticSyscallDispatcher;

typedef int64_t (*AI_SyscallHandler)(struct AI_CyberneticSyscallDispatcher *dispatcher,
                                     struct AI_CyberneticSyscallVector *vector);
typedef void (*AI_SyscallRegistrar)(struct AI_CyberneticSyscallDispatcher *dispatcher,
                                    uint64_t syscall_num, AI_SyscallHandler handler);
typedef void (*AI_MsrConfigurator)(struct AI_CyberneticSyscallDispatcher *dispatcher);

struct AI_CyberneticSyscallDispatcher {
    AI_SyscallHandler *handler_neural_matrix;
    uint64_t handler_count;
    AI_SyscallRegistrar register_handler;
    AI_MsrConfigurator configure_syscall_msrs;
    
    uint64_t total_syscalls_processed;
    uint64_t invalid_syscall_attempts;
    
    bool is_ai_synthetic_node;
    uint8_t ai_generation_confidence;
    uint64_t neural_dispatch_latency;
};

struct AI_UserModeMatrix *ai_create_usermode_matrix(void);
void ai_destroy_usermode_matrix(struct AI_UserModeMatrix *matrix);

struct AI_CyberneticSyscallDispatcher *ai_create_syscall_dispatcher(uint64_t max_syscalls);
void ai_destroy_syscall_dispatcher(struct AI_CyberneticSyscallDispatcher *dispatcher);

void ai_initialize_ring3_subsystem(void);
int ai_spawn_usermode_process(struct Process *proc, virt_addr_t entry_point,
                              const void *code_data, size_t code_size);
void ai_enter_usermode(struct Process *proc);

void ai_update_kernel_stack_for_process(struct Process *proc);

extern void ai_neural_ring3_transition_stub(uint64_t entry, uint64_t stack,
                                            uint64_t code_sel, uint64_t data_sel);

extern void ai_syscall_neural_entry(void);

#endif
