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

#pragma once

static __inline__ int isascii(int c) { return (c & 0x80) == 0; }
static __inline__ int isblank(int c) { return c == ' ' || c == '\t'; }
static __inline__ int isdigit(int c) { return c >= '0' && c <= '9'; }
static __inline__ int iscntrl(int c) { return c < 32; }
static __inline__ int islower(int c) { return c >= 'a' && c <= 'z'; }
static __inline__ int isspace(int c) {
    return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}
static __inline__ int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static __inline__ int isxdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

static __inline__ int isalpha(int c) { return isupper(c) || islower(c); }
static __inline__ int isalnum(int c) { return isalpha(c) || isdigit(c); }
static __inline__ int isgraph(int c) { return !iscntrl(c) && !isspace(c); }
static __inline__ int isprint(int c) { return !iscntrl(c); }
static __inline__ int ispunct(int c) { return !iscntrl(c) && !isspace(c) && !isalnum(c); }

static __inline__ int toupper(int c) { return islower(c) ? c & ~0x20 : c; }
static __inline__ int tolower(int c) { return isupper(c) ? c | 0x20 : c; }
