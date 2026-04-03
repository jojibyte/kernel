# Kernel Roadmap

## Phase 1: Core [DONE]
- [x] Multiboot2 boot
- [x] Long mode transition
- [x] GDT/IDT/TSS
- [x] PIC initialization
- [x] Physical memory manager
- [x] Virtual memory manager
- [x] Kernel heap

## Phase 2: Process Management [DONE]
- [x] Process control block
- [x] Context switching
- [x] Round-robin scheduler
- [x] Priority levels
- [x] Process sleep/wake

## Phase 3: Drivers [DONE]
- [x] VGA console
- [x] PS/2 keyboard
- [x] PIT timer
- [x] Serial debug output

## Phase 4: Filesystem [DONE]
- [x] VFS layer
- [x] RAM filesystem
- [ ] Ext2 read support
- [ ] FAT32 support

## Phase 5: Networking [PARTIAL]
- [x] Ethernet frames
- [x] ARP protocol
- [x] IPv4
- [x] ICMP (ping)
- [ ] UDP
- [ ] TCP
- [ ] DHCP client

## Phase 6: Userspace [TODO]
- [ ] ELF loader
- [ ] User mode processes
- [ ] Dynamic linking
- [ ] POSIX libc

## Phase 7: Advanced [TODO]
- [ ] SMP support
- [ ] APIC/IOAPIC
- [ ] AHCI driver
- [ ] USB support
- [ ] Graphics mode

## Phase 8: Security [TODO]
- [ ] ASLR
- [ ] Stack canaries
- [ ] Seccomp
- [ ] Capabilities
