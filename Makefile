CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy

ifeq ($(shell which $(CC) 2>/dev/null),)
    CC = gcc
    LD = ld
    OBJCOPY = objcopy
endif

BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/isodir

CFLAGS = -ffreestanding \
         -fno-stack-protector \
         -fno-pic \
         -mno-red-zone \
         -mno-mmx \
         -mno-sse \
         -mno-sse2 \
         -mcmodel=kernel \
         -Wall \
         -Wextra \
         -Werror \
         -O2 \
         -g \
         -I.

ASFLAGS = -f elf64 -g -F dwarf

LDFLAGS = -nostdlib \
          -T linker.ld \
          -z max-page-size=0x1000

C_SOURCES = kernel.c \
            console.c \
            cpu.c \
            pmm.c \
            vmm.c \
            heap.c \
            process.c \
            scheduler.c \
            vfs.c \
            net.c \
            syscall.c \
            usermode.c \
            elf.c

ASM_SOURCES = boot.asm

C_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
ISO_FILE = $(BUILD_DIR)/kernel.iso

all: $(KERNEL_ELF)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL_ELF): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

iso: $(KERNEL_ELF)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	echo 'menuentry "kernel" {' > $(ISO_DIR)/boot/grub/grub.cfg
	echo '    set gfxpayload=text' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.elf' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '}' >> $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_FILE) $(ISO_DIR)

run: iso
	qemu-system-x86_64 \
		-cdrom $(ISO_FILE) \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

debug: $(KERNEL_ELF)
	qemu-system-x86_64 \
		-kernel $(KERNEL_ELF) \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-s -S &
	gdb -ex "target remote :1234" \
	    -ex "symbol-file $(KERNEL_ELF)" \
	    -ex "break kernel_main"

run-iso: iso
	qemu-system-x86_64 \
		-cdrom $(ISO_FILE) \
		-m 256M \
		-serial stdio \
		-no-reboot

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "kernel x86-64 Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build kernel ELF (default)"
	@echo "  iso      - Create bootable ISO image"
	@echo "  run      - Run kernel in QEMU"
	@echo "  run-iso  - Run ISO in QEMU"
	@echo "  debug    - Run with GDB debugging"
	@echo "  clean    - Remove build files"

.PHONY: all iso run run-iso debug clean help
