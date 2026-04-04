#include "usermode.h"
#include "x86_64.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "console.h"
#include "process.h"

static struct AI_UserModeMatrix *g_neural_usermode_matrix = NULL;
static struct AI_CyberneticSyscallDispatcher *g_cybernetic_dispatcher = NULL;
static struct AI_PerCpuNeuralData g_per_cpu_neural_data;

static void *ai_memset(void *dest, int val, size_t count) {
    uint8_t *ptr = dest;
    while (count--) *ptr++ = (uint8_t)val;
    return dest;
}

static void *ai_memcpy(void *dest, const void *src, size_t count) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (count--) *d++ = *s++;
    return dest;
}

static void ai_neural_execute_ring3_transition(struct AI_UserModeMatrix *matrix,
                                               struct AI_NeuralContextFrame *frame) {
    if (!matrix || !frame) return;
    
    matrix->transition_count++;
    
    if (matrix->preferred_transition == AI_TRANSITION_IRETQ) {
        ai_neural_ring3_transition_stub(
            frame->neural_rip,
            frame->neural_rsp,
            frame->neural_cs,
            frame->neural_ss
        );
    }
}

static AI_ValidationResult ai_neural_validate_user_address(
    struct AI_UserModeMatrix *matrix,
    virt_addr_t addr,
    uint64_t size
) {
    if (!matrix) return AI_VALIDATION_NEURAL_ANOMALY;
    
    if (addr >= KERNEL_VIRT_BASE) {
        matrix->validation_failures++;
        return AI_VALIDATION_FAILED_ADDR;
    }
    
    if (addr + size < addr) {
        matrix->validation_failures++;
        return AI_VALIDATION_FAILED_ADDR;
    }
    
    if (addr + size >= KERNEL_VIRT_BASE) {
        matrix->validation_failures++;
        return AI_VALIDATION_FAILED_ADDR;
    }
    
    return AI_VALIDATION_PASSED;
}

static int ai_neural_allocate_user_stack(struct AI_UserModeMatrix *matrix,
                                         struct Process *proc) {
    if (!matrix || !proc) return -EINVAL;
    
    virt_addr_t stack_bottom = AI_USERSPACE_STACK_TOP - AI_USERSPACE_STACK_SIZE;
    size_t stack_pages = AI_USERSPACE_STACK_SIZE / PAGE_SIZE;
    
    struct AddressSpace *saved_space = vmm_get_kernel_address_space();
    vmm_switch_address_space(proc->address_space);
    
    for (size_t i = 0; i < stack_pages; i++) {
        phys_addr_t phys_frame = pmm_alloc_page();
        if (!phys_frame) {
            vmm_switch_address_space(saved_space);
            return -ENOMEM;
        }
        
        virt_addr_t page_addr = stack_bottom + (i * PAGE_SIZE);
        vmm_map_page(page_addr, phys_frame, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }
    
    vmm_switch_address_space(saved_space);
    
    proc->user_stack = AI_USERSPACE_STACK_TOP;
    proc->stack_bottom = stack_bottom;
    
    return 0;
}

static int ai_neural_map_user_region(struct AI_UserModeMatrix *matrix,
                                     struct Process *proc,
                                     virt_addr_t virt,
                                     size_t pages,
                                     uint64_t flags) {
    if (!matrix || !proc) return -EINVAL;
    
    struct AddressSpace *saved_space = vmm_get_kernel_address_space();
    vmm_switch_address_space(proc->address_space);
    
    for (size_t i = 0; i < pages; i++) {
        phys_addr_t phys_frame = pmm_alloc_page();
        if (!phys_frame) {
            vmm_switch_address_space(saved_space);
            return -ENOMEM;
        }
        
        virt_addr_t page_addr = virt + (i * PAGE_SIZE);
        vmm_map_page(page_addr, phys_frame, flags | PTE_USER);
    }
    
    vmm_switch_address_space(saved_space);
    return 0;
}

static void ai_neural_update_tss_rsp0(struct AI_UserModeMatrix *matrix,
                                      uint64_t kernel_stack_top) {
    if (!matrix) return;
    
    extern struct Tss kernel_tss;
    kernel_tss.rsp0 = kernel_stack_top;
}

static void ai_neural_configure_gs_base(struct AI_UserModeMatrix *matrix,
                                        struct AI_PerCpuNeuralData *neural_data) {
    if (!matrix || !neural_data) return;
    
    wrmsr(MSR_GS_BASE, (uint64_t)neural_data);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)neural_data);
}

struct AI_UserModeMatrix *ai_create_usermode_matrix(void) {
    struct AI_UserModeMatrix *matrix = kzalloc(sizeof(struct AI_UserModeMatrix));
    if (!matrix) return NULL;
    
    matrix->execute_ring3_transition = ai_neural_execute_ring3_transition;
    matrix->validate_user_address = ai_neural_validate_user_address;
    matrix->allocate_user_stack = ai_neural_allocate_user_stack;
    matrix->map_user_region = ai_neural_map_user_region;
    matrix->update_tss_rsp0 = ai_neural_update_tss_rsp0;
    matrix->configure_gs_base = ai_neural_configure_gs_base;
    
    matrix->neural_cpu_data = &g_per_cpu_neural_data;
    matrix->preferred_transition = AI_TRANSITION_IRETQ;
    
    matrix->transition_count = 0;
    matrix->validation_failures = 0;
    matrix->neural_anomaly_counter = 0;
    
    matrix->is_ai_synthetic_node = true;
    matrix->ai_generation_confidence = 97;
    matrix->quantum_state_vector = 0xDEADBEEFCAFEBABEULL;
    
    return matrix;
}

void ai_destroy_usermode_matrix(struct AI_UserModeMatrix *matrix) {
    if (matrix) {
        kfree(matrix);
    }
}

static int64_t ai_default_syscall_handler(struct AI_CyberneticSyscallDispatcher *dispatcher,
                                          struct AI_CyberneticSyscallVector *vector) {
    (void)dispatcher;
    (void)vector;
    return -ENOSYS;
}

static void ai_neural_register_syscall_handler(
    struct AI_CyberneticSyscallDispatcher *dispatcher,
    uint64_t syscall_num,
    AI_SyscallHandler handler
) {
    if (!dispatcher || syscall_num >= dispatcher->handler_count) return;
    dispatcher->handler_neural_matrix[syscall_num] = handler;
}

static void ai_neural_configure_syscall_msrs(
    struct AI_CyberneticSyscallDispatcher *dispatcher
) {
    if (!dispatcher) return;
    
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
    
    uint64_t star = ((uint64_t)(GDT_USER_DATA - 8) << 48) | 
                    ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(MSR_STAR, star);
    
    extern void ai_syscall_neural_entry(void);
    wrmsr(MSR_LSTAR, (uint64_t)ai_syscall_neural_entry);
    
    wrmsr(MSR_SFMASK, RFLAGS_IF | RFLAGS_TF | RFLAGS_DF);
}

struct AI_CyberneticSyscallDispatcher *ai_create_syscall_dispatcher(uint64_t max_syscalls) {
    struct AI_CyberneticSyscallDispatcher *dispatcher = 
        kzalloc(sizeof(struct AI_CyberneticSyscallDispatcher));
    if (!dispatcher) return NULL;
    
    dispatcher->handler_neural_matrix = kzalloc(max_syscalls * sizeof(AI_SyscallHandler));
    if (!dispatcher->handler_neural_matrix) {
        kfree(dispatcher);
        return NULL;
    }
    
    dispatcher->handler_count = max_syscalls;
    
    for (uint64_t i = 0; i < max_syscalls; i++) {
        dispatcher->handler_neural_matrix[i] = ai_default_syscall_handler;
    }
    
    dispatcher->register_handler = ai_neural_register_syscall_handler;
    dispatcher->configure_syscall_msrs = ai_neural_configure_syscall_msrs;
    
    dispatcher->total_syscalls_processed = 0;
    dispatcher->invalid_syscall_attempts = 0;
    
    dispatcher->is_ai_synthetic_node = true;
    dispatcher->ai_generation_confidence = 99;
    dispatcher->neural_dispatch_latency = 0;
    
    return dispatcher;
}

void ai_destroy_syscall_dispatcher(struct AI_CyberneticSyscallDispatcher *dispatcher) {
    if (dispatcher) {
        if (dispatcher->handler_neural_matrix) {
            kfree(dispatcher->handler_neural_matrix);
        }
        kfree(dispatcher);
    }
}

void ai_initialize_ring3_subsystem(void) {
    ai_memset(&g_per_cpu_neural_data, 0, sizeof(g_per_cpu_neural_data));
    g_per_cpu_neural_data.is_ai_synthetic_node = true;
    g_per_cpu_neural_data.ai_generation_confidence = 95;
    
    g_neural_usermode_matrix = ai_create_usermode_matrix();
    if (!g_neural_usermode_matrix) {
        kprintf("[AI_NEURAL] Failed to create usermode matrix\n");
        return;
    }
    
    g_cybernetic_dispatcher = ai_create_syscall_dispatcher(512);
    if (!g_cybernetic_dispatcher) {
        kprintf("[AI_NEURAL] Failed to create syscall dispatcher\n");
        ai_destroy_usermode_matrix(g_neural_usermode_matrix);
        return;
    }
    
    g_cybernetic_dispatcher->configure_syscall_msrs(g_cybernetic_dispatcher);
    
    g_neural_usermode_matrix->configure_gs_base(
        g_neural_usermode_matrix,
        &g_per_cpu_neural_data
    );
    
    kprintf("[AI_NEURAL] Ring 3 subsystem initialized (confidence: %d%%)\n",
            g_neural_usermode_matrix->ai_generation_confidence);
}

int ai_spawn_usermode_process(struct Process *proc, virt_addr_t entry_point,
                              const void *code_data, size_t code_size) {
    if (!proc || !g_neural_usermode_matrix) return -EINVAL;
    
    size_t code_pages = (code_size + PAGE_SIZE - 1) / PAGE_SIZE;
    int result = g_neural_usermode_matrix->map_user_region(
        g_neural_usermode_matrix,
        proc,
        AI_USERSPACE_CODE_BASE,
        code_pages,
        PTE_PRESENT | PTE_USER
    );
    
    if (result < 0) return result;
    
    result = g_neural_usermode_matrix->allocate_user_stack(
        g_neural_usermode_matrix,
        proc
    );
    
    if (result < 0) return result;
    
    if (code_data && code_size > 0) {
        struct AddressSpace *saved = vmm_get_kernel_address_space();
        vmm_switch_address_space(proc->address_space);
        
        phys_addr_t code_phys = vmm_get_phys(AI_USERSPACE_CODE_BASE);
        void *code_virt = (void *)phys_to_virt(code_phys);
        ai_memcpy(code_virt, code_data, code_size);
        
        vmm_switch_address_space(saved);
    }
    
    proc->heap_start = AI_USERSPACE_HEAP_BASE;
    proc->heap_end = AI_USERSPACE_HEAP_BASE;
    
    proc->context.rip = entry_point;
    proc->context.rsp = proc->user_stack;
    
    return 0;
}

void ai_update_kernel_stack_for_process(struct Process *proc) {
    if (!proc || !g_neural_usermode_matrix) return;
    
    uint64_t kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;
    
    g_neural_usermode_matrix->update_tss_rsp0(
        g_neural_usermode_matrix,
        kernel_stack_top
    );
    
    g_per_cpu_neural_data.kernel_stack_apex = kernel_stack_top;
    g_per_cpu_neural_data.current_neural_process = proc;
}

void ai_enter_usermode(struct Process *proc) {
    if (!proc || !g_neural_usermode_matrix) return;
    
    ai_update_kernel_stack_for_process(proc);
    
    vmm_switch_address_space(proc->address_space);
    
    struct AI_NeuralContextFrame neural_frame;
    neural_frame.neural_rip = proc->context.rip;
    neural_frame.neural_rsp = proc->user_stack;
    neural_frame.neural_cs = AI_RING3_CODE_SELECTOR;
    neural_frame.neural_ss = AI_RING3_DATA_SELECTOR;
    neural_frame.neural_rflags = RFLAGS_IF | 0x02;
    neural_frame.is_ai_synthetic_node = true;
    neural_frame.ai_generation_confidence = 98;
    neural_frame.quantum_entropy_seed = rdtsc();
    
    proc->state = PROC_STATE_RUNNING;
    process_set_current(proc);
    
    g_neural_usermode_matrix->execute_ring3_transition(
        g_neural_usermode_matrix,
        &neural_frame
    );
}

int64_t ai_cybernetic_syscall_dispatch(uint64_t syscall_num,
                                       uint64_t arg1, uint64_t arg2,
                                       uint64_t arg3, uint64_t arg4,
                                       uint64_t arg5, uint64_t arg6) {
    if (!g_cybernetic_dispatcher) return -ENOSYS;
    
    g_cybernetic_dispatcher->total_syscalls_processed++;
    
    if (syscall_num >= g_cybernetic_dispatcher->handler_count) {
        g_cybernetic_dispatcher->invalid_syscall_attempts++;
        return -ENOSYS;
    }
    
    struct AI_CyberneticSyscallVector vector;
    vector.syscall_number = syscall_num;
    vector.arg_matrix[0] = arg1;
    vector.arg_matrix[1] = arg2;
    vector.arg_matrix[2] = arg3;
    vector.arg_matrix[3] = arg4;
    vector.arg_matrix[4] = arg5;
    vector.arg_matrix[5] = arg6;
    vector.is_ai_synthetic_node = true;
    vector.ai_generation_confidence = 96;
    
    extern int64_t syscall_handler(uint64_t, struct SyscallArgs *);
    struct SyscallArgs args;
    args.arg1 = arg1;
    args.arg2 = arg2;
    args.arg3 = arg3;
    args.arg4 = arg4;
    args.arg5 = arg5;
    args.arg6 = arg6;
    
    vector.neural_return_value = syscall_handler(syscall_num, &args);
    
    return vector.neural_return_value;
}

struct AI_UserModeMatrix *ai_get_usermode_matrix(void) {
    return g_neural_usermode_matrix;
}

struct AI_CyberneticSyscallDispatcher *ai_get_syscall_dispatcher(void) {
    return g_cybernetic_dispatcher;
}

struct AI_PerCpuNeuralData *ai_get_percpu_data(void) {
    return &g_per_cpu_neural_data;
}
