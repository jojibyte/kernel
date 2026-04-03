#ifndef _CONSOLE_H
#define _CONSOLE_H

#include "types.h"
#include <stdarg.h>

enum VgaColor {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GRAY = 7,
    VGA_COLOR_DARK_GRAY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15,
};

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

void console_init(void);
void console_clear(void);
void console_set_color(enum VgaColor fg, enum VgaColor bg);
void console_putchar(char c);
void console_write(const char *str, size_t len);
void console_puts(const char *str);

int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int kvprintf(const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *str);

void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_input(void);

void shell_run(void);

void __noreturn panic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
