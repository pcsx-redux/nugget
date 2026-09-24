/*

MIT License

Copyright (c) 2022 PCSX-Redux authors

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

#include <stdarg.h>
#include <stddef.h>

#include "common/libc/xprintf.h"
#include "common/syscalls/syscalls.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int xprintf(void (*func)(const char *, int, void *), void *arg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vxprintf(func, arg, fmt, ap);
    va_end(ap);
    return ret;
}

static inline int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

static inline int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return ret;
}

static inline int asprintf(char **out, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vasprintf(out, fmt, ap);
    va_end(ap);
    return ret;
}

static inline void writeToTTYCallback(const char *str, int len, void *opaque) { syscall_write(1, str, len); }
static inline int vprintf(const char *fmt, va_list ap) { return vxprintf(writeToTTYCallback, NULL, fmt, ap); }
static inline int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

#ifdef __cplusplus
}

#include <EASTL/fixed_string.h>
#include <EASTL/string.h>

/**
 * @brief Prints a formatted string to a C++ eastl::fixed_string.
 *
 * @details This function is a helper around `vxprintf`. The
 * `sprintf` variant is available.
 *
 * @param str The string to print to.
 * @param fmt The format string.
 * @param ap The vararg list of arguments.
 */
template <int nodeCount, bool bEnableOverflow = true>
static inline void vfsprintf(eastl::fixed_string<char, nodeCount, bEnableOverflow> &str, const char *fmt, va_list ap) {
    str.clear();
    vxprintf(
        [](const char *str, int len, void *opaque) {
            eastl::fixed_string<char, nodeCount, bEnableOverflow> *out =
                (eastl::fixed_string<char, nodeCount, bEnableOverflow> *)opaque;
            out->append(str, len);
        },
        &str, fmt, ap);
}

template <int nodeCount, bool bEnableOverflow = true>
static inline void fsprintf(eastl::fixed_string<char, nodeCount, bEnableOverflow> &str, const char *fmt, ...) {
    str.clear();
    va_list ap;
    va_start(ap, fmt);
    vfsprintf<nodeCount, bEnableOverflow>(str, fmt, ap);
    va_end(ap);
}

/**
 * @brief Prints a formatted string to a C++ eastl::string.
 *
 * @details This function is a helper around `vxprintf`. The
 * `sprintf` variant is available.
 *
 * @param fmt The format string.
 * @param ap The vararg list of arguments.
 * @return eastl::string The formatted string.
 */
static inline eastl::string vsprintf(const char *fmt, va_list ap) {
    eastl::string ret;
    vxprintf(
        [](const char *str, int len, void *opaque) {
            eastl::string *ret = (eastl::string *)opaque;
            ret->append(str, len);
        },
        &ret, fmt, ap);
    return ret;
}

static inline eastl::string sprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    eastl::string ret = vsprintf(fmt, ap);
    va_end(ap);
    return ret;
}

#endif
