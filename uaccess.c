#include "types.h"
#include "vmm.h"
#include "process.h"
#include "kstring.h"

#define USER_SPACE_START    0x1000ULL
#define USER_SPACE_END      0x00007FFFFFFFFFFFULL

bool access_ok(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;

    if (end < start)
        return false;

    if (start < USER_SPACE_START)
        return false;

    if (end > USER_SPACE_END)
        return false;

    return true;
}

bool access_ok_write(const void *addr, size_t size) {
    if (!access_ok(addr, size))
        return false;

    uintptr_t page_start = (uintptr_t)addr & ~(PAGE_SIZE - 1);
    uintptr_t page_end = ((uintptr_t)addr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t p = page_start; p < page_end; p += PAGE_SIZE) {
        if (!vmm_is_mapped(p))
            return false;
    }

    return true;
}

int copy_from_user(void *kernel_dst, const void *user_src, size_t size) {
    if (!size)
        return 0;

    if (!access_ok(user_src, size))
        return -EFAULT;

    uintptr_t page_start = (uintptr_t)user_src & ~(PAGE_SIZE - 1);
    uintptr_t page_end = ((uintptr_t)user_src + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t p = page_start; p < page_end; p += PAGE_SIZE) {
        if (!vmm_is_mapped(p))
            return -EFAULT;
    }

    kmemcpy(kernel_dst, user_src, size);
    return 0;
}

int copy_to_user(void *user_dst, const void *kernel_src, size_t size) {
    if (!size)
        return 0;

    if (!access_ok(user_dst, size))
        return -EFAULT;

    uintptr_t page_start = (uintptr_t)user_dst & ~(PAGE_SIZE - 1);
    uintptr_t page_end = ((uintptr_t)user_dst + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t p = page_start; p < page_end; p += PAGE_SIZE) {
        if (!vmm_is_mapped(p))
            return -EFAULT;
    }

    kmemcpy(user_dst, kernel_src, size);
    return 0;
}

int strncpy_from_user(char *kernel_dst, const char *user_src, size_t max_len) {
    if (!access_ok(user_src, 1))
        return -EFAULT;

    for (size_t i = 0; i < max_len; i++) {
        uintptr_t addr = (uintptr_t)(user_src + i);

        if ((addr & (PAGE_SIZE - 1)) == 0) {
            if (!vmm_is_mapped(addr))
                return -EFAULT;
        }

        kernel_dst[i] = user_src[i];

        if (user_src[i] == '\0')
            return (int)i;
    }

    kernel_dst[max_len - 1] = '\0';
    return (int)(max_len - 1);
}

size_t strnlen_user(const char *user_str, size_t max_len) {
    if (!access_ok(user_str, 1))
        return 0;

    for (size_t i = 0; i < max_len; i++) {
        uintptr_t addr = (uintptr_t)(user_str + i);

        if ((addr & (PAGE_SIZE - 1)) == 0) {
            if (!vmm_is_mapped(addr))
                return 0;
        }

        if (user_str[i] == '\0')
            return i + 1;
    }

    return max_len + 1;
}

int clear_user(void *user_dst, size_t size) {
    if (!access_ok(user_dst, size))
        return -EFAULT;

    uintptr_t page_start = (uintptr_t)user_dst & ~(PAGE_SIZE - 1);
    uintptr_t page_end = ((uintptr_t)user_dst + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t p = page_start; p < page_end; p += PAGE_SIZE) {
        if (!vmm_is_mapped(p))
            return -EFAULT;
    }

    kmemset(user_dst, 0, size);
    return 0;
}
