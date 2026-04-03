#include "console.h"
#include "x86_64.h"

#define VGA_BUFFER 0xB8000
#define VGA_CTRL_PORT 0x3D4
#define VGA_DATA_PORT 0x3D5

static struct {
    uint16_t *buffer;
    uint8_t color;
    size_t row;
    size_t col;
} console;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline uint8_t vga_color(enum VgaColor fg, enum VgaColor bg) {
    return fg | (bg << 4);
}

static void update_cursor(void) {
    uint16_t pos = console.row * VGA_WIDTH + console.col;
    outb(VGA_CTRL_PORT, 14);
    outb(VGA_DATA_PORT, (pos >> 8) & 0xFF);
    outb(VGA_CTRL_PORT, 15);
    outb(VGA_DATA_PORT, pos & 0xFF);
}

static void scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            console.buffer[y * VGA_WIDTH + x] = 
                console.buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        console.buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = 
            vga_entry(' ', console.color);
    }
    
    console.row = VGA_HEIGHT - 1;
}

void console_init(void) {
    console.buffer = (uint16_t *)VGA_BUFFER;
    console.color = vga_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
    console.row = 0;
    console.col = 0;
    
    outb(VGA_CTRL_PORT, 0x0A);
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xC0) | 14);
    outb(VGA_CTRL_PORT, 0x0B);
    outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xE0) | 15);
}

void console_clear(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        console.buffer[i] = vga_entry(' ', console.color);
    }
    console.row = 0;
    console.col = 0;
    update_cursor();
}

void console_set_color(enum VgaColor fg, enum VgaColor bg) {
    console.color = vga_color(fg, bg);
}

void console_putchar(char c) {
    if (c == '\n') {
        console.col = 0;
        console.row++;
    } else if (c == '\r') {
        console.col = 0;
    } else if (c == '\t') {
        console.col = (console.col + 8) & ~7;
    } else if (c == '\b') {
        if (console.col > 0) {
            console.col--;
            console.buffer[console.row * VGA_WIDTH + console.col] = 
                vga_entry(' ', console.color);
        }
    } else {
        console.buffer[console.row * VGA_WIDTH + console.col] = 
            vga_entry(c, console.color);
        console.col++;
    }
    
    if (console.col >= VGA_WIDTH) {
        console.col = 0;
        console.row++;
    }
    
    if (console.row >= VGA_HEIGHT) {
        scroll();
    }
    
    update_cursor();
}

void console_write(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        console_putchar(str[i]);
    }
}

void console_puts(const char *str) {
    while (*str) {
        console_putchar(*str++);
    }
}

static void print_num(char **buf, size_t *rem, uint64_t num, int base, 
                      int width, char pad, bool is_signed, bool uppercase) {
    char tmp[24];
    char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    bool negative = false;
    
    if (is_signed && (int64_t)num < 0) {
        negative = true;
        num = -(int64_t)num;
    }
    
    if (num == 0) {
        tmp[i++] = '0';
    } else {
        while (num > 0) {
            tmp[i++] = digits[num % base];
            num /= base;
        }
    }
    
    if (negative) {
        tmp[i++] = '-';
    }
    
    while (i < width) {
        tmp[i++] = pad;
    }
    
    while (i > 0 && *rem > 0) {
        **buf = tmp[--i];
        (*buf)++;
        (*rem)--;
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    char *start = buf;
    size_t rem = size > 0 ? size - 1 : 0;
    
    while (*fmt && rem > 0) {
        if (*fmt != '%') {
            *buf++ = *fmt++;
            rem--;
            continue;
        }
        
        fmt++;
        
        char pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        bool is_long = false;
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') fmt++;
        }
        
        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t num = is_long ? va_arg(args, int64_t) : va_arg(args, int);
            print_num(&buf, &rem, num, 10, width, pad, true, false);
            break;
        }
        case 'u': {
            uint64_t num = is_long ? va_arg(args, uint64_t) : va_arg(args, unsigned);
            print_num(&buf, &rem, num, 10, width, pad, false, false);
            break;
        }
        case 'x': {
            uint64_t num = is_long ? va_arg(args, uint64_t) : va_arg(args, unsigned);
            print_num(&buf, &rem, num, 16, width, pad, false, false);
            break;
        }
        case 'X': {
            uint64_t num = is_long ? va_arg(args, uint64_t) : va_arg(args, unsigned);
            print_num(&buf, &rem, num, 16, width, pad, false, true);
            break;
        }
        case 'p': {
            uint64_t num = (uint64_t)va_arg(args, void *);
            if (rem >= 2) {
                *buf++ = '0';
                *buf++ = 'x';
                rem -= 2;
            }
            print_num(&buf, &rem, num, 16, 16, '0', false, false);
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            while (*s && rem > 0) {
                *buf++ = *s++;
                rem--;
            }
            break;
        }
        case 'c':
            *buf++ = (char)va_arg(args, int);
            rem--;
            break;
        case '%':
            *buf++ = '%';
            rem--;
            break;
        default:
            if (rem > 0) {
                *buf++ = '%';
                rem--;
            }
            if (rem > 0 && *fmt) {
                *buf++ = *fmt;
                rem--;
            }
        }
        
        if (*fmt) fmt++;
    }
    
    if (size > 0) {
        *buf = '\0';
    }
    
    return buf - start;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int kvprintf(const char *fmt, va_list args) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    console_write(buf, len);
    serial_puts(buf);
    return len;
}

int kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = kvprintf(fmt, args);
    va_end(args);
    return ret;
}

#define COM1_PORT 0x3F8

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

void serial_putchar(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0);
    outb(COM1_PORT, c);
}

void serial_puts(const char *str) {
    while (*str) {
        if (*str == '\n')
            serial_putchar('\r');
        serial_putchar(*str++);
    }
}

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

static const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0
};

static const char scancode_to_ascii_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0
};

static volatile char kbd_buffer[256];
static volatile uint8_t kbd_read_ptr = 0;
static volatile uint8_t kbd_write_ptr = 0;
static volatile bool shift_pressed = false;
static volatile bool ctrl_pressed = false;

void keyboard_init(void) {
    while (inb(KBD_STATUS_PORT) & 0x02);
    outb(KBD_STATUS_PORT, 0xAE);
    while (inb(KBD_STATUS_PORT) & 0x01) {
        inb(KBD_DATA_PORT);
    }
}

void keyboard_irq_handler(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);
    
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = true;
        return;
    }
    if (scancode == 0x9D) {
        ctrl_pressed = false;
        return;
    }
    
    if (scancode & 0x80)
        return;
    
    char c;
    if (shift_pressed) {
        c = scancode_to_ascii_shift[scancode];
    } else {
        c = scancode_to_ascii[scancode];
    }
    
    if (c == 0)
        return;
    
    if (ctrl_pressed && (c == 'c' || c == 'C')) {
        c = 3;
    }
    
    kbd_buffer[kbd_write_ptr++] = c;
}

bool keyboard_has_input(void) {
    return kbd_read_ptr != kbd_write_ptr;
}

char keyboard_getchar(void) {
    while (!keyboard_has_input()) {
        hlt();
    }
    return kbd_buffer[kbd_read_ptr++];
}

static char line_buffer[256];

static void shell_readline(char *buf, size_t size) {
    size_t pos = 0;
    
    while (pos < size - 1) {
        char c = keyboard_getchar();
        
        if (c == '\n' || c == '\r') {
            console_putchar('\n');
            break;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                console_putchar('\b');
                console_putchar(' ');
                console_putchar('\b');
            }
        } else if (c == 3) {
            kprintf("^C\n");
            pos = 0;
            break;
        } else if (c >= 32 && c < 127) {
            buf[pos++] = c;
            console_putchar(c);
        }
    }
    
    buf[pos] = '\0';
}

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static void cmd_help(void) {
    kprintf("Available commands:\n");
    kprintf("  help      - Show this help\n");
    kprintf("  clear     - Clear screen\n");
    kprintf("  mem       - Show memory info\n");
    kprintf("  ps        - List processes\n");
    kprintf("  ls        - List files\n");
    kprintf("  cat FILE  - Display file contents\n");
    kprintf("  uptime    - Show system uptime\n");
    kprintf("  reboot    - Reboot system\n");
}

static void cmd_clear(void) {
    console_clear();
}

extern uint64_t pmm_get_free_memory(void);
extern uint64_t pmm_get_total_memory(void);

static void cmd_mem(void) {
    uint64_t free = pmm_get_free_memory();
    uint64_t total = pmm_get_total_memory();
    kprintf("Memory: %u MB free / %u MB total\n",
            (uint32_t)(free / MB), (uint32_t)(total / MB));
}

extern void process_list(void);

static void cmd_ps(void) {
    process_list();
}

extern uint64_t timer_get_ticks(void);

static void cmd_uptime(void) {
    uint64_t ticks = timer_get_ticks();
    uint64_t secs = ticks / 1000;
    kprintf("Uptime: %llu seconds\n", (unsigned long long)secs);
}

static void cmd_reboot(void) {
    kprintf("Rebooting...\n");
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    hlt();
}

void shell_run(void) {
    for (;;) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        kprintf("kernel");
        console_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
        kprintf("$ ");
        
        shell_readline(line_buffer, sizeof(line_buffer));
        
        if (line_buffer[0] == '\0')
            continue;
        
        if (strcmp(line_buffer, "help") == 0) {
            cmd_help();
        } else if (strcmp(line_buffer, "clear") == 0) {
            cmd_clear();
        } else if (strcmp(line_buffer, "mem") == 0) {
            cmd_mem();
        } else if (strcmp(line_buffer, "ps") == 0) {
            cmd_ps();
        } else if (strcmp(line_buffer, "uptime") == 0) {
            cmd_uptime();
        } else if (strcmp(line_buffer, "reboot") == 0) {
            cmd_reboot();
        } else if (strncmp(line_buffer, "ls", 2) == 0) {
            kprintf("(filesystem not fully implemented)\n");
        } else if (strncmp(line_buffer, "cat ", 4) == 0) {
            kprintf("(filesystem not fully implemented)\n");
        } else {
            kprintf("Unknown command: %s\n", line_buffer);
            kprintf("Type 'help' for available commands.\n");
        }
    }
}
