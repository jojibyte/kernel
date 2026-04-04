#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "console.h"

struct HeapBlock {
    size_t size;
    bool free;
    struct HeapBlock *next;
    struct HeapBlock *prev;
};

#define HEAP_BLOCK_SIZE ALIGN_UP(sizeof(struct HeapBlock), 16)
#define MIN_BLOCK_SIZE (HEAP_BLOCK_SIZE + 16)

static struct HeapBlock *heap_start;
static struct HeapBlock *heap_end;
static size_t heap_size;
static size_t heap_used;

#define INITIAL_HEAP_SIZE (1 * MB)

static void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = c;
    return s;
}

void heap_init(void) {
    size_t pages = INITIAL_HEAP_SIZE / PAGE_SIZE;
    virt_addr_t heap_virt = vmm_alloc_kernel_pages(pages);
    
    if (!heap_virt) {
        panic("Failed to allocate kernel heap");
    }
    
    heap_start = (struct HeapBlock *)heap_virt;
    heap_size = INITIAL_HEAP_SIZE;
    heap_used = HEAP_BLOCK_SIZE;
    
    heap_start->size = heap_size;
    heap_start->free = true;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    heap_end = heap_start;
}

static void split_block(struct HeapBlock *block, size_t size) {
    size_t remaining = block->size - size;
    
    if (remaining >= MIN_BLOCK_SIZE) {
        struct HeapBlock *new_block = 
            (struct HeapBlock *)((uint8_t *)block + size);
        
        new_block->size = remaining;
        new_block->free = true;
        new_block->next = block->next;
        new_block->prev = block;
        
        if (block->next) {
            block->next->prev = new_block;
        }
        
        block->size = size;
        block->next = new_block;
        
        if (block == heap_end) {
            heap_end = new_block;
        }
    }
}

static void coalesce(struct HeapBlock *block) {
    while (block->next && block->next->free) {
        struct HeapBlock *next = block->next;
        block->size += next->size;
        block->next = next->next;
        if (next->next) {
            next->next->prev = block;
        }
        if (next == heap_end) {
            heap_end = block;
        }
    }
    
    while (block->prev && block->prev->free) {
        struct HeapBlock *prev = block->prev;
        prev->size += block->size;
        prev->next = block->next;
        if (block->next) {
            block->next->prev = prev;
        }
        if (block == heap_end) {
            heap_end = prev;
        }
        block = prev;
    }
}

static bool expand_heap(size_t min_size) {
    size_t pages = ALIGN_UP(min_size, PAGE_SIZE) / PAGE_SIZE;
    if (pages < 16) pages = 16;
    
    virt_addr_t new_pages = vmm_alloc_kernel_pages(pages);
    if (!new_pages) {
        return false;
    }
    
    struct HeapBlock *new_block = (struct HeapBlock *)new_pages;
    new_block->size = pages * PAGE_SIZE;
    new_block->free = true;
    new_block->next = NULL;
    new_block->prev = heap_end;
    
    heap_end->next = new_block;
    heap_end = new_block;
    heap_size += pages * PAGE_SIZE;
    
    coalesce(new_block);
    
    return true;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    size = ALIGN_UP(size + HEAP_BLOCK_SIZE, 16);
    if (size < MIN_BLOCK_SIZE) {
        size = MIN_BLOCK_SIZE;
    }
    
    struct HeapBlock *block = heap_start;
    
    while (block) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = false;
            heap_used += block->size;
            return (void *)((uint8_t *)block + HEAP_BLOCK_SIZE);
        }
        block = block->next;
    }
    
    if (!expand_heap(size)) {
        return NULL;
    }
    
    return kmalloc(size - HEAP_BLOCK_SIZE);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    
    struct HeapBlock *block = 
        (struct HeapBlock *)((uint8_t *)ptr - HEAP_BLOCK_SIZE);
    
    if (block->free) {
        return;
    }
    
    heap_used -= block->size;
    block->free = true;
    
    coalesce(block);
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    struct HeapBlock *block = 
        (struct HeapBlock *)((uint8_t *)ptr - HEAP_BLOCK_SIZE);
    
    size_t old_size = block->size - HEAP_BLOCK_SIZE;
    
    if (size <= old_size) {
        return ptr;
    }
    
    void *new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    
    uint8_t *src = ptr;
    uint8_t *dst = new_ptr;
    for (size_t i = 0; i < old_size; i++) {
        dst[i] = src[i];
    }
    
    kfree(ptr);
    return new_ptr;
}

void *kmalloc_aligned(size_t size, size_t align) {
    void *ptr = kmalloc(size + align);
    if (!ptr) return NULL;
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = ALIGN_UP(addr, align);
    
    return (void *)aligned;
}

size_t heap_get_free(void) {
    return heap_size - heap_used;
}

size_t heap_get_used(void) {
    return heap_used;
}
