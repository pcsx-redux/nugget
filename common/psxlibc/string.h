/*

MIT License

Copyright (c) 2020 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *s1, const void *s2, size_t n);
void *memmove(void *s1, const void *s2, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memset(void *s, int c, size_t n);

#ifdef __cplusplus
}
#endif

static __inline__ const void *memchr(const void *s_, int c, size_t n) {
    const uint8_t *s = (const uint8_t *)s_;
    for (size_t i = 0; i < n; i++, s++) {
        if (*s == c) return s;
    }
    return NULL;
}

static __inline__ char *strcat(char *s1, const char *s2) {
    char *r = s1;
    char c;
    while (*s1) s1++;
    while ((c = *s2++)) *s1++ = c;
    *s1 = 0;
    return r;
}

static __inline__ char *strcpy(char *s1, const char *s2) {
    char *r = s1;
    while ((*s1++ = *s2++));
    return r;
}

static __inline__ char *strncpy(char *s1, const char *s2, size_t n) {
    char *r = s1;
    char c;
    int done = 0;
    for (size_t i = 0; i < n; i++) {
        if (!done && (c = *s2++)) {
            *s1++ = c;
        } else {
            *s1++ = 0;
            done = 1;
        }
    }
    return r;
}

static __inline__ char *strchr(const char *s, int c) {
    char b;
    while ((b = *s)) {
        if (b == c) return (char *)s;
        s++;
    }
    return NULL;
}

static __inline__ char *strrchr(const char *s, int c) {
    const char *r = NULL;
    char b;
    while ((b = *s)) {
        if (b == c) r = s;
        s++;
    }
    return (char *)r;
}

static __inline__ size_t strlen(const char *s) {
    size_t r = 0;
    while (*s++) r++;
    return r;
}

static __inline__ char *strncat(char *s1, const char *s2, size_t n) {
    char *r = s1;
    char c;
    while (*s1) s1++;
    while (n-- && ((c = *s2++))) *s1++ = c;
    *s1 = 0;
    return r;
}

static __inline__ int strcmp(const char *s1, const char *s2) {
    int c1, c2;
    while (1) {
        if (((c1 = *s1++) != (c2 = *s2++)) || !c1 || !c2) break;
    }
    return c1 - c2;
}

static __inline__ int strncmp(const char *s1, const char *s2, size_t n) {
    int c1, c2;
    while (n--) {
        if (((c1 = *s1++) != (c2 = *s2++)) || !c1 || !c2) return c1 - c2;
    }
    return 0;
}

static __inline__ char *strstr(const char *s1, const char *s2) {
    size_t l = strlen(s2);
    while (*s1) {
        if (!strncmp(s1, s2, l)) return (char *)s1;
        s1++;
    }
    return NULL;
}

static __attribute__((always_inline)) void* safeMemZero(void* ptr_, int size) {
    uint8_t* ptr = (uint8_t*)ptr_;
    if (!ptr || size <= 0) return NULL;
    uint8_t* orig = ptr;
    for (; size > 0; ptr++) {
        size--;
        *ptr = 0;
    }
    return orig;
}
