#ifndef _HEAP_H
#define _HEAP_H

#include "types.h"

#define KERNEL_HEAP_START  0xFFFFFFFFC0000000
#define KERNEL_HEAP_SIZE   0x10000000  // 256 MB

void heap_init(void);

void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

void *kmalloc_aligned(size_t size, size_t align);

size_t heap_get_free(void);
size_t heap_get_used(void);

#endif
