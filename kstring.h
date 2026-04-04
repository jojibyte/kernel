#ifndef _KSTRING_H
#define _KSTRING_H

#include "types.h"

size_t kstrlen(const char *s);
size_t kstrnlen(const char *s, size_t maxlen);

char *kstrcpy(char *dest, const char *src);
char *kstrncpy(char *dest, const char *src, size_t n);

char *kstrcat(char *dest, const char *src);
char *kstrncat(char *dest, const char *src, size_t n);

int kstrcmp(const char *s1, const char *s2);
int kstrncmp(const char *s1, const char *s2, size_t n);

char *kstrchr(const char *s, int c);
char *kstrrchr(const char *s, int c);
char *kstrstr(const char *haystack, const char *needle);

void *kmemcpy(void *dest, const void *src, size_t n);
void *kmemmove(void *dest, const void *src, size_t n);
void *kmemset(void *s, int c, size_t n);
int kmemcmp(const void *s1, const void *s2, size_t n);
void *kmemchr(const void *s, int c, size_t n);

int katoi(const char *s);
long katol(const char *s);

#endif
