/*

MIT License

Copyright (c) 2026 PCSX-Redux authors

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

#include <stddef.h>

#include "openbios/kernel/libcmisc.h"

void qsort(void *base, size_t num, size_t size, int (*cmp)(const void *, const void *)) {
    size_t gap, i, j, k;
    if ((num > 1) && (size > 0)) {
        gap = 0;
        while (gap < (num - 1) / 3) gap = 3 * gap + 1;
        gap *= size;
        num *= size;
        while (gap != 0) {
            i = gap;
            while (i < num) {
                j = i;
                while (j >= gap) {
                    char *a, *b;
                    j -= gap;
                    a = j + ((char *)base);
                    b = a + gap;
                    if ((*cmp)(a, b) <= 0) break;
                    k = size;
                    while (k--) {
                        char tmp = *a;
                        *a++ = *b;
                        *b++ = tmp;
                    }
                }
                i += size;
            }
            gap = (gap - size) / 3;
        }
    }
}
