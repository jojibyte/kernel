#include <stdio.h>
#include <string.h>
#include <assert.h>

/*
 * We want to test the implementation of strncmp from kstring.c.
 * To avoid conflict with the standard library and types,
 * we will redefine the necessary types and include the implementation.
 */

typedef unsigned long long k_size_t;
typedef unsigned char k_uint8_t;

#define size_t k_size_t
#define uint8_t k_uint8_t

// Use a macro to rename the function in kstring.c
#define strlen k_strlen
#define strnlen k_strnlen
#define strcpy k_strcpy
#define strncpy k_strncpy
#define strcat k_strcat
#define strncat k_strncat
#define strcmp k_strcmp
#define strncmp k_strncmp
#define strchr k_strchr
#define strrchr k_strrchr
#define strstr k_strstr
#define memcpy k_memcpy
#define memmove k_memmove
#define memset k_memset
#define memcmp k_memcmp
#define memchr k_memchr
#define atoi k_atoi
#define atol k_atol

// We don't include kstring.h because it might have conflicting definitions
// We only need the implementation from kstring.c

// Mock types.h if needed, or just define what's missing
#define _KSTRING_H
#define _KERNEL_TYPES_H

/* Inline the parts of kstring.c we need or just include it and hope for the best
   after the defines above. We need to be careful about headers included in kstring.c */
// kstring.c includes kstring.h which we've guarded.

#include "../kstring.c"

#undef size_t
#undef uint8_t

void test_strncmp() {
    // Exactly n characters match
    assert(k_strncmp("abc", "abc", 3) == 0);

    // Different at the end within n
    assert(k_strncmp("abc", "abd", 3) < 0);
    assert(k_strncmp("abd", "abc", 3) > 0);

    // Different before n
    assert(k_strncmp("abc", "axc", 3) < 0);

    // Identical up to n, but different after
    assert(k_strncmp("abcde", "abcfg", 3) == 0);

    // One string shorter than n
    assert(k_strncmp("abc", "abcd", 4) < 0);
    assert(k_strncmp("abcd", "abc", 4) > 0);

    // Both strings shorter than n
    assert(k_strncmp("abc", "abc", 5) == 0);

    // n is 0
    assert(k_strncmp("abc", "def", 0) == 0);

    // Empty strings
    assert(k_strncmp("", "", 1) == 0);
    assert(k_strncmp("", "a", 1) < 0);
    assert(k_strncmp("a", "", 1) > 0);

    // Null characters within n
    assert(k_strncmp("a\0b", "a\0c", 3) == 0);

    // Unsigned comparison test
    // \xFF is 255 if unsigned, -1 if signed char.
    // The standard requires unsigned comparison.
    assert(k_strncmp("\xFF", "\x01", 1) > 0);

    printf("strncmp tests passed!\n");
}

int main() {
    test_strncmp();
    return 0;
}
