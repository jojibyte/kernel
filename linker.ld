OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_PHYS = 0x100000;
KERNEL_VIRT = 0xFFFFFFFF80000000;

SECTIONS
{
    . = KERNEL_PHYS;

    __kernel_start = .;

    .multiboot_header : {
        KEEP(*(.multiboot_header))
    }

    .boot : {
        *(.boot)
        *(.boot.*)
    }

    . = ALIGN(4096);

    .text : {
        *(.text)
        *(.text.*)
    }

    . = ALIGN(4096);

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(4096);

    .data : {
        *(.data)
        *(.data.*)
    }

    . = ALIGN(4096);

    .page_tables (NOLOAD) : {
        *(.page_tables)
    }

    . = ALIGN(4096);

    .early_stack (NOLOAD) : {
        *(.early_stack)
    }

    . = ALIGN(4096);

    __bss_start = .;
    .bss (NOLOAD) : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    __bss_end = .;

    . = ALIGN(4096);

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
