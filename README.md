# kernel

minimal x86-64 kernel

## wsl setup

```bash
sudo apt update
sudo apt install -y build-essential nasm qemu-system-x86 grub-pc-bin xorriso mtools
```

## build

```bash
make
make run
make debug
make clean
```

## architecture

```
+-------------------+
|    syscall api    |
+-------------------+
| process | memory  |
+---------+---------+
|   vfs   | network |
+---------+---------+
|  device drivers   |
+-------------------+
```

## shell

```
kernel$ help
kernel$ mem
kernel$ ps
kernel$ reboot
```
