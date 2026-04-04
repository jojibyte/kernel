#ifndef _UACCESS_H
#define _UACCESS_H

#include "types.h"

bool access_ok(const void *addr, size_t size);
bool access_ok_write(const void *addr, size_t size);

int copy_from_user(void *kernel_dst, const void *user_src, size_t size);
int copy_to_user(void *user_dst, const void *kernel_src, size_t size);

int strncpy_from_user(char *kernel_dst, const char *user_src, size_t max_len);
size_t strnlen_user(const char *user_str, size_t max_len);
int clear_user(void *user_dst, size_t size);

#endif
