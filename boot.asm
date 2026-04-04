section .multiboot_header
align 8
header_start:
    dd 0xE85250D6
    dd 0
    dd header_end - header_start
    dd -(0xE85250D6 + 0 + (header_end - header_start))

    align 8
    dw 5
    dw 0
    dd 20
    dd 1024
    dd 768
    dd 32

    align 8
    dw 0
    dw 0
    dd 8
header_end:

section .page_tables
align 4096
pml4_table:     resb 4096
pdpt_table:     resb 4096
pd_table:       resb 4096
pdpt_table_hh:  resb 4096

section .early_stack
align 16
early_stack_bottom: resb 4096
stack_top_low:

section .bss
align 16
stack_bottom:   resb 65536
stack_top:

section .data
align 16
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)
.user_data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41) | (3<<45)
.user_code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) | (3<<45)
.tss: equ $ - gdt64
    dq 0
    dq 0
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
bits 32

global _start
extern kernel_main

_start:
    mov ebp, ebx
    mov esp, stack_top_low
    push eax

    cli

    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    jz .no_cpuid

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    mov edi, pml4_table
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd

    mov edi, pml4_table
    mov eax, pdpt_table
    or eax, 0x03
    mov [edi], eax

    mov eax, pdpt_table_hh
    or eax, 0x03
    mov [edi + 511*8], eax

    mov edi, pdpt_table
    mov eax, pd_table
    or eax, 0x03
    mov [edi], eax

    mov edi, pdpt_table_hh
    mov eax, pd_table
    or eax, 0x03
    mov [edi + 510*8], eax

    mov edi, pd_table
    mov eax, 0x83
    mov ecx, 512
.set_pd_entry:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .set_pd_entry

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64.pointer]

    jmp gdt64.code:long_mode_start

.no_cpuid:
    mov al, 'C'
    jmp .error

.no_long_mode:
    mov al, 'L'
    jmp .error

.error:
    mov dword [0xB8000], 0x4F524F45
    mov dword [0xB8004], 0x4F3A4F52
    mov byte  [0xB8008], al
    mov byte  [0xB8009], 0x4F
    hlt

bits 64
long_mode_start:
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top

    mov r12d, ebp
    mov r13d, dword [stack_top_low - 4]

    cld

    extern __bss_start
    extern __bss_end
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    shr rcx, 3
    xor rax, rax
    rep stosq

    mov rax, cr0
    and ax, 0xFFFB
    or ax, 0x2
    mov cr0, rax
    mov rax, cr4
    or ax, 3 << 9
    mov cr4, rax

    mov rdi, r12
    mov rsi, r13
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

section .text
global isr_stub_table
extern interrupt_handler

%macro isr_no_err 1
isr_stub_%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro isr_err 1
isr_stub_%1:
    push %1
    jmp isr_common
%endmacro

isr_no_err 0
isr_no_err 1
isr_no_err 2
isr_no_err 3
isr_no_err 4
isr_no_err 5
isr_no_err 6
isr_no_err 7
isr_err    8
isr_no_err 9
isr_err    10
isr_err    11
isr_err    12
isr_err    13
isr_err    14
isr_no_err 15
isr_no_err 16
isr_err    17
isr_no_err 18
isr_no_err 19
isr_no_err 20
isr_err    21

%assign i 22
%rep 10
isr_no_err i
%assign i i+1
%endrep

%assign i 32
%rep 16
isr_no_err i
%assign i i+1
%endrep

%assign i 48
%rep 208
isr_no_err i
%assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp

    call interrupt_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16

    iretq

section .data
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
%assign i i+1
%endrep

section .text
global context_switch

context_switch:
    mov [rdi + 0], rbx
    mov [rdi + 8], rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    mov [rdi + 48], rsp
    
    mov rax, [rsp]
    mov [rdi + 56], rax

    mov rbx, [rsi + 0]
    mov rbp, [rsi + 8]
    mov r12, [rsi + 16]
    mov r13, [rsi + 24]
    mov r14, [rsi + 32]
    mov r15, [rsi + 40]
    mov rsp, [rsi + 48]

    mov rax, [rsi + 56]
    push rax
    ret

global syscall_entry
extern syscall_handler
syscall_entry:
    swapgs
    mov [gs:8], rsp
    mov rsp, [gs:0]

    push rcx
    push r11
    push rax
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    mov rdi, rax
    lea rsi, [rsp]
    call syscall_handler

    add rsp, 56
    pop r11
    pop rcx

    mov rsp, [gs:8]
    swapgs

    sysretq

global ai_syscall_neural_entry
extern ai_cybernetic_syscall_dispatch
ai_syscall_neural_entry:
    swapgs
    mov [gs:8], rsp
    mov rsp, [gs:0]

    push rcx
    push r11

    sub rsp, 8

    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi

    mov rdi, rax
    mov rsi, [rsp + 0]
    mov rdx, [rsp + 8]
    mov rcx, [rsp + 16]
    mov r8,  [rsp + 24]
    mov r9,  [rsp + 32]
    push qword [rsp + 40]

    call ai_cybernetic_syscall_dispatch

    add rsp, 8

    add rsp, 48
    add rsp, 8

    pop r11
    pop rcx

    mov rsp, [gs:8]
    swapgs

    sysretq

global ai_neural_ring3_transition_stub
ai_neural_ring3_transition_stub:
    cli

    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push rdx
    push rsi
    push qword 0x202
    push rcx
    push rdi

    iretq

global ai_neural_return_from_usermode
ai_neural_return_from_usermode:
    mov rax, rdi
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
