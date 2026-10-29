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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "common/psxlibc/circularbuffer.h"
#include "common/psxlibc/device.h"
#include "common/psxlibc/handlers.h"

struct JmpBuf;

#ifndef PS1_PC_PORT
// Each BIOS call goes through a two-instruction thunk, `j <table>` with t1 set
// in its delay slot, emitted in every file that includes this header as a weak
// symbol in its own COMDAT section, so the linker keeps one copy of the ones
// that are used. The thunks are plain functions to the compiler: t1 cannot be
// reused before the jump, the BIOS returns straight to the caller, and a call
// in tail position stays a jump.
// Code spread over more than one 256 MB region (OpenBIOS runs from ROM and
// RAM) defines NUGGET_BIOS_LONG_CALLS, so the thunks are reached through a
// register rather than a jal. It has to be set for every object in a link or
// for none: both forms of a thunk share one name and one COMDAT group, so the
// linker keeps whichever copy it sees first without a diagnostic.
// Every included thunk is emitted in every object, so a link needs
// --gc-sections (common.mk passes it) to drop the ones nothing calls.
#ifdef NUGGET_BIOS_LONG_CALLS
#define NUGGET_BIOS_CALL_ATTR __attribute__((long_call))
#else
#define NUGGET_BIOS_CALL_ATTR
#endif
// A j reaches 0xa0 only from code in the same 256 MB region, which ROM code
// is not, so long-call code jumps through t2 (the BIOS handlers clobber it).
#ifdef NUGGET_BIOS_LONG_CALLS
#define NUGGET_BIOS_JUMP(table) "    li $t2, " #table "\n    jr $t2\n"
#else
#define NUGGET_BIOS_JUMP(table) "    j " #table "\n"
#endif
#ifdef __cplusplus
#define NUGGET_BIOS_EXTERN extern "C" NUGGET_BIOS_CALL_ATTR
#else
#define NUGGET_BIOS_EXTERN extern NUGGET_BIOS_CALL_ATTR
#endif
#define NUGGET_BIOS_THUNK(name, table, number)                                                              \
    __asm__(".ifndef __nugget_bios_" #name "\n"                                                             \
            ".pushsection .text.__nugget_bios_" #name ",\"axG\",@progbits,__nugget_bios_" #name ",comdat\n" \
            ".weak __nugget_bios_" #name "\n"                                                               \
            ".type __nugget_bios_" #name ", @function\n"                                                    \
            ".set push\n.set noreorder\n"                                                                   \
            "__nugget_bios_" #name ":\n"                                                                    \
            NUGGET_BIOS_JUMP(table)                                                                         \
            "    li $t1, " #number "\n"                                                                     \
            ".set pop\n"                                                                                     \
            ".size __nugget_bios_" #name ", . - __nugget_bios_" #name "\n"                                  \
            ".popsection\n"                                                                                \
            ".endif\n");
#endif

#ifndef PS1_PC_PORT
static __attribute__((always_inline)) int enterCriticalSection() {
    register int n asm("a0") = 1;
    register int r asm("v0");
    __asm__ volatile("syscall\n" : "=r"(r) : "r"(n) : "memory");
    return r;
}

static __attribute__((always_inline)) void leaveCriticalSection() {
    register int n asm("a0") = 2;
    __asm__ volatile("syscall\n" : : "r"(n) : "memory");
}

static __attribute__((always_inline)) int changeThreadSubFunction(uint32_t address) {
    register int n asm("a0") = 3;
    register int tcb asm("a1") = address;
    register int r asm("v0");
    __asm__ volatile("syscall\n" : "=r"(r) : "r"(n), "r"(tcb) : "memory");
    return r;
}

/* A0 table */
NUGGET_BIOS_THUNK(syscall_write, 0xa0, 0x03)
NUGGET_BIOS_EXTERN size_t __nugget_bios_syscall_write(int fd, const void *buf, size_t size);
static __attribute__((always_inline)) size_t syscall_write(int fd, const void *buf, size_t size) {
    return __nugget_bios_syscall_write(fd, buf, size);
}

NUGGET_BIOS_THUNK(syscall_setjmp, 0xa0, 0x13)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_setjmp(struct JmpBuf *buf) __attribute__((returns_twice));
// A macro: GCC never inlines a function that calls a returns_twice function.
#define syscall_setjmp(buf) __nugget_bios_syscall_setjmp(buf)

NUGGET_BIOS_THUNK(syscall_longjmp, 0xa0, 0x14)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_longjmp(struct JmpBuf *buf, int ret) __attribute__((noreturn));
static __attribute__((always_inline, noreturn)) void syscall_longjmp(struct JmpBuf *buf, int ret) {
    __nugget_bios_syscall_longjmp(buf, ret);
    __builtin_unreachable();
}

NUGGET_BIOS_THUNK(syscall_strcat, 0xa0, 0x15)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_strcat(char *dst, const char *src);
static __attribute__((always_inline)) char * syscall_strcat(char *dst, const char *src) {
    return __nugget_bios_syscall_strcat(dst, src);
}

NUGGET_BIOS_THUNK(syscall_strncat, 0xa0, 0x16)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_strncat(char *dst, const char *src, size_t size);
static __attribute__((always_inline)) char * syscall_strncat(char *dst, const char *src, size_t size) {
    return __nugget_bios_syscall_strncat(dst, src, size);
}

NUGGET_BIOS_THUNK(syscall_strcmp, 0xa0, 0x17)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_strcmp(const char *s1, const char *s2);
static __attribute__((always_inline)) int syscall_strcmp(const char *s1, const char *s2) {
    return __nugget_bios_syscall_strcmp(s1, s2);
}

NUGGET_BIOS_THUNK(syscall_strncmp, 0xa0, 0x18)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_strncmp(const char *s1, const char *s2, size_t size);
static __attribute__((always_inline)) int syscall_strncmp(const char *s1, const char *s2, size_t size) {
    return __nugget_bios_syscall_strncmp(s1, s2, size);
}

NUGGET_BIOS_THUNK(syscall_strcpy, 0xa0, 0x19)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_strcpy(char *dst, const char *src);
static __attribute__((always_inline)) char * syscall_strcpy(char *dst, const char *src) {
    return __nugget_bios_syscall_strcpy(dst, src);
}

NUGGET_BIOS_THUNK(syscall_strncpy, 0xa0, 0x1a)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_strncpy(char *dst, const char *src, size_t size);
static __attribute__((always_inline)) char * syscall_strncpy(char *dst, const char *src, size_t size) {
    return __nugget_bios_syscall_strncpy(dst, src, size);
}

NUGGET_BIOS_THUNK(syscall_strlen, 0xa0, 0x1b)
NUGGET_BIOS_EXTERN size_t __nugget_bios_syscall_strlen(const char *s);
static __attribute__((always_inline)) size_t syscall_strlen(const char *s) {
    return __nugget_bios_syscall_strlen(s);
}

NUGGET_BIOS_THUNK(syscall_index, 0xa0, 0x1c)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_index(const char *s, int c);
static __attribute__((always_inline)) char * syscall_index(const char *s, int c) {
    return __nugget_bios_syscall_index(s, c);
}

NUGGET_BIOS_THUNK(syscall_rindex, 0xa0, 0x1d)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_rindex(const char *s, int c);
static __attribute__((always_inline)) char * syscall_rindex(const char *s, int c) {
    return __nugget_bios_syscall_rindex(s, c);
}

NUGGET_BIOS_THUNK(syscall_strchr, 0xa0, 0x1e)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_strchr(const char *s, int c);
static __attribute__((always_inline)) char * syscall_strchr(const char *s, int c) {
    return __nugget_bios_syscall_strchr(s, c);
}

NUGGET_BIOS_THUNK(syscall_strrchr, 0xa0, 0x1f)
NUGGET_BIOS_EXTERN char * __nugget_bios_syscall_strrchr(const char *s, int c);
static __attribute__((always_inline)) char * syscall_strrchr(const char *s, int c) {
    return __nugget_bios_syscall_strrchr(s, c);
}

NUGGET_BIOS_THUNK(syscall_memcpy, 0xa0, 0x2a)
NUGGET_BIOS_EXTERN void * __nugget_bios_syscall_memcpy(void *dst, const void *src, size_t count);
static __attribute__((always_inline)) void * syscall_memcpy(void *dst, const void *src, size_t count) {
    return __nugget_bios_syscall_memcpy(dst, src, count);
}

NUGGET_BIOS_THUNK(syscall_memset, 0xa0, 0x2b)
NUGGET_BIOS_EXTERN void * __nugget_bios_syscall_memset(void *dst, int c, size_t count);
static __attribute__((always_inline)) void * syscall_memset(void *dst, int c, size_t count) {
    return __nugget_bios_syscall_memset(dst, c, count);
}

NUGGET_BIOS_THUNK(syscall_qsort, 0xa0, 0x31)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_qsort(void *base, size_t nel, size_t width,
                                                    int (*compar)(const void *, const void *));
static __attribute__((always_inline)) void syscall_qsort(void *base, size_t nel, size_t width,
                                                         int (*compar)(const void *, const void *)) {
    __nugget_bios_syscall_qsort(base, nel, width, compar);
}

NUGGET_BIOS_THUNK(syscall_userMalloc, 0xa0, 0x33)
NUGGET_BIOS_EXTERN void * __nugget_bios_syscall_userMalloc(size_t size);
static __attribute__((always_inline)) void * syscall_userMalloc(size_t size) {
    return __nugget_bios_syscall_userMalloc(size);
}

NUGGET_BIOS_THUNK(syscall_userFree, 0xa0, 0x34)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_userFree(void *ptr);
static __attribute__((always_inline)) void syscall_userFree(void *ptr) {
    __nugget_bios_syscall_userFree(ptr);
}

NUGGET_BIOS_THUNK(syscall_userInitheap, 0xa0, 0x39)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_userInitheap(void *ptr, size_t size);
static __attribute__((always_inline)) void syscall_userInitheap(void *ptr, size_t size) {
    __nugget_bios_syscall_userInitheap(ptr, size);
}

NUGGET_BIOS_THUNK(syscall__exit, 0xa0, 0x3a)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall__exit(int code);
static __attribute__((always_inline)) void syscall__exit(int code) {
    __nugget_bios_syscall__exit(code);
}

NUGGET_BIOS_THUNK(syscall_puts, 0xa0, 0x3e)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_puts(const char *msg);
static __attribute__((always_inline)) void syscall_puts(const char *msg) {
    __nugget_bios_syscall_puts(msg);
}

// doing this one in raw inline assembly would prove tricky,
// and there's already enough voodoo in this file.
// this is syscall a0:3f
#ifdef __cplusplus
extern "C" {
#endif
int romsyscall_printf(const char *fmt, ...);
int ramsyscall_printf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif

NUGGET_BIOS_THUNK(syscall_unresolvedException, 0xa0, 0x40)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_unresolvedException(void);
static __attribute__((always_inline)) int syscall_unresolvedException() {
    return __nugget_bios_syscall_unresolvedException();
}

NUGGET_BIOS_THUNK(syscall_flushCache, 0xa0, 0x44)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_flushCache(void);
static __attribute__((always_inline)) void syscall_flushCache() {
    __nugget_bios_syscall_flushCache();
}

NUGGET_BIOS_THUNK(syscall_cdromSeekL, 0xa0, 0x78)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_cdromSeekL(uint8_t *msf);
static __attribute__((always_inline)) int syscall_cdromSeekL(uint8_t *msf) {
    return __nugget_bios_syscall_cdromSeekL(msf);
}

NUGGET_BIOS_THUNK(syscall_cdromGetStatus, 0xa0, 0x7c)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_cdromGetStatus(uint8_t *ptr);
static __attribute__((always_inline)) int syscall_cdromGetStatus(uint8_t *ptr) {
    return __nugget_bios_syscall_cdromGetStatus(ptr);
}

NUGGET_BIOS_THUNK(syscall_cdromRead, 0xa0, 0x7e)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_cdromRead(int count, void *buffer, uint32_t mode);
static __attribute__((always_inline)) int syscall_cdromRead(int count, void *buffer, uint32_t mode) {
    return __nugget_bios_syscall_cdromRead(count, buffer, mode);
}

NUGGET_BIOS_THUNK(syscall_cdromInnerInit, 0xa0, 0x95)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_cdromInnerInit(void);
static __attribute__((always_inline)) int syscall_cdromInnerInit() {
    return __nugget_bios_syscall_cdromInnerInit();
}

NUGGET_BIOS_THUNK(syscall_addCDRomDevice, 0xa0, 0x96)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_addCDRomDevice(void);
static __attribute__((always_inline)) int syscall_addCDRomDevice() {
    return __nugget_bios_syscall_addCDRomDevice();
}

NUGGET_BIOS_THUNK(syscall_addMemoryCardDevice, 0xa0, 0x97)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_addMemoryCardDevice(void);
static __attribute__((always_inline)) int syscall_addMemoryCardDevice() {
    return __nugget_bios_syscall_addMemoryCardDevice();
}

NUGGET_BIOS_THUNK(syscall_addConsoleDevice, 0xa0, 0x98)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_addConsoleDevice(void);
static __attribute__((always_inline)) int syscall_addConsoleDevice() {
    return __nugget_bios_syscall_addConsoleDevice();
}

NUGGET_BIOS_THUNK(syscall_addDummyConsoleDevice, 0xa0, 0x99)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_addDummyConsoleDevice(void);
static __attribute__((always_inline)) int syscall_addDummyConsoleDevice() {
    return __nugget_bios_syscall_addDummyConsoleDevice();
}

NUGGET_BIOS_THUNK(syscall_exception, 0xa0, 0xa1)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_exception(int code1, int code2);
static __attribute__((always_inline)) void syscall_exception(int code1, int code2) {
    __nugget_bios_syscall_exception(code1, code2);
}

NUGGET_BIOS_THUNK(syscall_enqueueCDRomHandlers, 0xa0, 0xa2)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_enqueueCDRomHandlers(void);
static __attribute__((always_inline)) void syscall_enqueueCDRomHandlers() {
    __nugget_bios_syscall_enqueueCDRomHandlers();
}

NUGGET_BIOS_THUNK(syscall_dequeueCDRomHandlers, 0xa0, 0xa3)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_dequeueCDRomHandlers(void);
static __attribute__((always_inline)) void syscall_dequeueCDRomHandlers() {
    __nugget_bios_syscall_dequeueCDRomHandlers();
}

NUGGET_BIOS_THUNK(syscall_buLowLevelOpCompleted, 0xa0, 0xa7)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_buLowLevelOpCompleted(void);
static __attribute__((always_inline)) void syscall_buLowLevelOpCompleted() {
    __nugget_bios_syscall_buLowLevelOpCompleted();
}

NUGGET_BIOS_THUNK(syscall_buLowLevelOpError1, 0xa0, 0xa8)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_buLowLevelOpError1(void);
static __attribute__((always_inline)) void syscall_buLowLevelOpError1() {
    __nugget_bios_syscall_buLowLevelOpError1();
}

NUGGET_BIOS_THUNK(syscall_buLowLevelOpError2, 0xa0, 0xa9)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_buLowLevelOpError2(void);
static __attribute__((always_inline)) void syscall_buLowLevelOpError2() {
    __nugget_bios_syscall_buLowLevelOpError2();
}

NUGGET_BIOS_THUNK(syscall_buLowLevelOpError3, 0xa0, 0xaa)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_buLowLevelOpError3(void);
static __attribute__((always_inline)) void syscall_buLowLevelOpError3() {
    __nugget_bios_syscall_buLowLevelOpError3();
}

NUGGET_BIOS_THUNK(syscall_buLowLevelOpError4, 0xa0, 0xae)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_buLowLevelOpError4(void);
static __attribute__((always_inline)) void syscall_buLowLevelOpError4() {
    __nugget_bios_syscall_buLowLevelOpError4();
}

NUGGET_BIOS_THUNK(syscall_ioabortraw, 0xa0, 0xb2)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_ioabortraw(int code);
static __attribute__((always_inline)) int syscall_ioabortraw(int code) {
    return __nugget_bios_syscall_ioabortraw(code);
}

/* B0 table */
NUGGET_BIOS_THUNK(syscall_kmalloc, 0xb0, 0x00)
NUGGET_BIOS_EXTERN void * __nugget_bios_syscall_kmalloc(unsigned size);
static __attribute__((always_inline)) void * syscall_kmalloc(unsigned size) {
    return __nugget_bios_syscall_kmalloc(size);
}

NUGGET_BIOS_THUNK(syscall_kfree, 0xb0, 0x01)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_kfree(void *ptr);
static __attribute__((always_inline)) void syscall_kfree(void *ptr) {
    __nugget_bios_syscall_kfree(ptr);
}

NUGGET_BIOS_THUNK(syscall_initTimer, 0xb0, 0x02)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_initTimer(uint32_t timer, uint16_t target, uint16_t flags);
static __attribute__((always_inline)) int syscall_initTimer(uint32_t timer, uint16_t target, uint16_t flags) {
    return __nugget_bios_syscall_initTimer(timer, target, flags);
}

NUGGET_BIOS_THUNK(syscall_getTimer, 0xb0, 0x03)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_getTimer(uint32_t timer);
static __attribute__((always_inline)) int syscall_getTimer(uint32_t timer) {
    return __nugget_bios_syscall_getTimer(timer);
}

NUGGET_BIOS_THUNK(syscall_enableTimerIRQ, 0xb0, 0x04)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_enableTimerIRQ(uint32_t timer);
static __attribute__((always_inline)) int syscall_enableTimerIRQ(uint32_t timer) {
    return __nugget_bios_syscall_enableTimerIRQ(timer);
}

NUGGET_BIOS_THUNK(syscall_disableTimerIRQ, 0xb0, 0x05)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_disableTimerIRQ(uint32_t timer);
static __attribute__((always_inline)) int syscall_disableTimerIRQ(uint32_t timer) {
    return __nugget_bios_syscall_disableTimerIRQ(timer);
}

NUGGET_BIOS_THUNK(syscall_restartTimer, 0xb0, 0x06)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_restartTimer(uint32_t timer);
static __attribute__((always_inline)) int syscall_restartTimer(uint32_t timer) {
    return __nugget_bios_syscall_restartTimer(timer);
}

NUGGET_BIOS_THUNK(syscall_deliverEvent, 0xb0, 0x07)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_deliverEvent(uint32_t classId, uint32_t spec);
static __attribute__((always_inline)) void syscall_deliverEvent(uint32_t classId, uint32_t spec) {
    __nugget_bios_syscall_deliverEvent(classId, spec);
}

NUGGET_BIOS_THUNK(syscall_openEvent, 0xb0, 0x08)
NUGGET_BIOS_EXTERN uint32_t __nugget_bios_syscall_openEvent(uint32_t classId, uint32_t spec, uint32_t mode,
                                                            void (*handler)());
static __attribute__((always_inline)) uint32_t syscall_openEvent(uint32_t classId, uint32_t spec, uint32_t mode,
                                                                 void (*handler)()) {
    return __nugget_bios_syscall_openEvent(classId, spec, mode, handler);
}

NUGGET_BIOS_THUNK(syscall_closeEvent, 0xb0, 0x09)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_closeEvent(uint32_t event);
static __attribute__((always_inline)) int syscall_closeEvent(uint32_t event) {
    return __nugget_bios_syscall_closeEvent(event);
}

NUGGET_BIOS_THUNK(syscall_testEvent, 0xb0, 0x0b)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_testEvent(uint32_t event);
static __attribute__((always_inline)) int syscall_testEvent(uint32_t event) {
    return __nugget_bios_syscall_testEvent(event);
}

NUGGET_BIOS_THUNK(syscall_enableEvent, 0xb0, 0x0c)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_enableEvent(uint32_t event);
static __attribute__((always_inline)) int syscall_enableEvent(uint32_t event) {
    return __nugget_bios_syscall_enableEvent(event);
}

NUGGET_BIOS_THUNK(syscall_initPad, 0xb0, 0x12)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_initPad(void *buffer1, size_t size1, void *buffer2, size_t size2);
static __attribute__((always_inline)) void syscall_initPad(void *buffer1, size_t size1, void *buffer2, size_t size2) {
    __nugget_bios_syscall_initPad(buffer1, size1, buffer2, size2);
}

NUGGET_BIOS_THUNK(syscall_startPad, 0xb0, 0x13)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_startPad(void);
static __attribute__((always_inline)) void syscall_startPad() {
    __nugget_bios_syscall_startPad();
}

NUGGET_BIOS_THUNK(syscall_stopPad, 0xb0, 0x14)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_stopPad(void);
static __attribute__((always_inline)) void syscall_stopPad() {
    __nugget_bios_syscall_stopPad();
}

NUGGET_BIOS_THUNK(syscall_returnFromException, 0xb0, 0x17)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_returnFromException(void) __attribute__((noreturn));
static __attribute__((always_inline, noreturn)) void syscall_returnFromException() {
    __nugget_bios_syscall_returnFromException();
    __builtin_unreachable();
}

NUGGET_BIOS_THUNK(syscall_setDefaultExceptionJmpBuf, 0xb0, 0x18)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_setDefaultExceptionJmpBuf(void);
static __attribute__((always_inline)) void syscall_setDefaultExceptionJmpBuf() {
    __nugget_bios_syscall_setDefaultExceptionJmpBuf();
}

NUGGET_BIOS_THUNK(syscall_undeliverEvent, 0xb0, 0x20)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_undeliverEvent(uint32_t classId, uint32_t mode);
static __attribute__((always_inline)) void syscall_undeliverEvent(uint32_t classId, uint32_t mode) {
    __nugget_bios_syscall_undeliverEvent(classId, mode);
}

NUGGET_BIOS_THUNK(syscall_open, 0xb0, 0x32)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_open(const char *filename, int mode);
static __attribute__((always_inline)) int syscall_open(const char *filename, int mode) {
    return __nugget_bios_syscall_open(filename, mode);
}

NUGGET_BIOS_THUNK(syscall_read, 0xb0, 0x34)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_read(int fd, void *buffer, int size);
static __attribute__((always_inline)) int syscall_read(int fd, void *buffer, int size) {
    return __nugget_bios_syscall_read(fd, buffer, size);
}

NUGGET_BIOS_THUNK(syscall_close, 0xb0, 0x36)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_close(int fd);
static __attribute__((always_inline)) int syscall_close(int fd) {
    return __nugget_bios_syscall_close(fd);
}

NUGGET_BIOS_THUNK(syscall_putchar, 0xb0, 0x3d)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_putchar(int c);
static __attribute__((always_inline)) void syscall_putchar(int c) {
    __nugget_bios_syscall_putchar(c);
}

NUGGET_BIOS_THUNK(syscall_addDevice, 0xb0, 0x47)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_addDevice(const struct Device *device);
static __attribute__((always_inline)) int syscall_addDevice(const struct Device *device) {
    return __nugget_bios_syscall_addDevice(device);
}

NUGGET_BIOS_THUNK(syscall_cardInfoInternal, 0xb0, 0x4d)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_cardInfoInternal(int deviceID);
static __attribute__((always_inline)) int syscall_cardInfoInternal(int deviceID) {
    return __nugget_bios_syscall_cardInfoInternal(deviceID);
}

NUGGET_BIOS_THUNK(syscall_mcWriteSector, 0xb0, 0x4e)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_mcWriteSector(int deviceID, int sector, const uint8_t *buffer);
static __attribute__((always_inline)) int syscall_mcWriteSector(int deviceID, int sector, const uint8_t *buffer) {
    return __nugget_bios_syscall_mcWriteSector(deviceID, sector, buffer);
}

NUGGET_BIOS_THUNK(syscall_mcReadSector, 0xb0, 0x4f)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_mcReadSector(int deviceID, int sector, uint8_t *buffer);
static __attribute__((always_inline)) int syscall_mcReadSector(int deviceID, int sector, uint8_t *buffer) {
    return __nugget_bios_syscall_mcReadSector(deviceID, sector, buffer);
}

NUGGET_BIOS_THUNK(syscall_mcAllowNewCard, 0xb0, 0x50)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_mcAllowNewCard(void);
static __attribute__((always_inline)) void syscall_mcAllowNewCard() {
    __nugget_bios_syscall_mcAllowNewCard();
}

NUGGET_BIOS_THUNK(syscall_Krom2RawAdd, 0xb0, 0x51)
NUGGET_BIOS_EXTERN const uint8_t * __nugget_bios_syscall_Krom2RawAdd(uint32_t c);
static __attribute__((always_inline)) const uint8_t * syscall_Krom2RawAdd(uint32_t c) {
    return __nugget_bios_syscall_Krom2RawAdd(c);
}

NUGGET_BIOS_THUNK(syscall_mcGetLastDevice, 0xb0, 0x58)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_mcGetLastDevice(void);
static __attribute__((always_inline)) int syscall_mcGetLastDevice() {
    return __nugget_bios_syscall_mcGetLastDevice();
}

/* C0 table */
NUGGET_BIOS_THUNK(syscall_enqueueRCntIrqs, 0xc0, 0x00)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_enqueueRCntIrqs(int priority);
static __attribute__((always_inline)) int syscall_enqueueRCntIrqs(int priority) {
    return __nugget_bios_syscall_enqueueRCntIrqs(priority);
}

NUGGET_BIOS_THUNK(syscall_enqueueSyscallHandler, 0xc0, 0x01)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_enqueueSyscallHandler(int priority);
static __attribute__((always_inline)) int syscall_enqueueSyscallHandler(int priority) {
    return __nugget_bios_syscall_enqueueSyscallHandler(priority);
}

NUGGET_BIOS_THUNK(syscall_sysEnqIntRP, 0xc0, 0x02)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_sysEnqIntRP(int priority, struct HandlerInfo *info);
static __attribute__((always_inline)) int syscall_sysEnqIntRP(int priority, struct HandlerInfo *info) {
    return __nugget_bios_syscall_sysEnqIntRP(priority, info);
}

NUGGET_BIOS_THUNK(syscall_sysDeqIntRP, 0xc0, 0x03)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_sysDeqIntRP(int priority, struct HandlerInfo *info);
static __attribute__((always_inline)) int syscall_sysDeqIntRP(int priority, struct HandlerInfo *info) {
    return __nugget_bios_syscall_sysDeqIntRP(priority, info);
}

NUGGET_BIOS_THUNK(syscall_installExceptionHandler, 0xc0, 0x07)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_installExceptionHandler(void);
static __attribute__((always_inline)) void syscall_installExceptionHandler() {
    __nugget_bios_syscall_installExceptionHandler();
}

NUGGET_BIOS_THUNK(syscall_kernInitheap, 0xc0, 0x08)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_kernInitheap(void *base, size_t size);
static __attribute__((always_inline)) void syscall_kernInitheap(void *base, size_t size) {
    __nugget_bios_syscall_kernInitheap(base, size);
}

NUGGET_BIOS_THUNK(syscall_setTimerAutoAck, 0xc0, 0x0a)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_setTimerAutoAck(uint32_t timer, int value);
static __attribute__((always_inline)) int syscall_setTimerAutoAck(uint32_t timer, int value) {
    return __nugget_bios_syscall_setTimerAutoAck(timer, value);
}

NUGGET_BIOS_THUNK(syscall_enqueueIrqHandler, 0xc0, 0x0c)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_enqueueIrqHandler(int priority);
static __attribute__((always_inline)) int syscall_enqueueIrqHandler(int priority) {
    return __nugget_bios_syscall_enqueueIrqHandler(priority);
}

// This syscall is broken beyond repair, as the kernel code contains a race
// condition that can cause the kernel to lose IRQs. Please don't use it.
NUGGET_BIOS_THUNK(syscall_setIrqAutoAck, 0xc0, 0x0d)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_setIrqAutoAck(uint32_t irq, int value);
static __attribute__((always_inline)) void syscall_setIrqAutoAck(uint32_t irq, int value) {
    __nugget_bios_syscall_setIrqAutoAck(irq, value);
}

NUGGET_BIOS_THUNK(syscall_setupFileIO, 0xc0, 0x12)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_setupFileIO(int installTTY);
static __attribute__((always_inline)) void syscall_setupFileIO(int installTTY) {
    __nugget_bios_syscall_setupFileIO(installTTY);
}

NUGGET_BIOS_THUNK(syscall_cdevinput, 0xc0, 0x15)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_cdevinput(struct CircularBuffer *circ, int c);
static __attribute__((always_inline)) void syscall_cdevinput(struct CircularBuffer *circ, int c) {
    __nugget_bios_syscall_cdevinput(circ, c);
}

NUGGET_BIOS_THUNK(syscall_cdevscan, 0xc0, 0x16)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_cdevscan(void);
static __attribute__((always_inline)) void syscall_cdevscan() {
    __nugget_bios_syscall_cdevscan();
}

NUGGET_BIOS_THUNK(syscall_circgetc, 0xc0, 0x17)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_circgetc(struct CircularBuffer *circ);
static __attribute__((always_inline)) int syscall_circgetc(struct CircularBuffer *circ) {
    return __nugget_bios_syscall_circgetc(circ);
}

NUGGET_BIOS_THUNK(syscall_ioabort, 0xc0, 0x19)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_ioabort(const char *msg);
static __attribute__((always_inline)) int syscall_ioabort(const char *msg) {
    return __nugget_bios_syscall_ioabort(msg);
}

NUGGET_BIOS_THUNK(syscall_setDeviceStatus, 0xc0, 0x1a)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_setDeviceStatus(int status);
static __attribute__((always_inline)) void syscall_setDeviceStatus(int status) {
    __nugget_bios_syscall_setDeviceStatus(status);
}

NUGGET_BIOS_THUNK(syscall_patchA0table, 0xc0, 0x1c)
NUGGET_BIOS_EXTERN void __nugget_bios_syscall_patchA0table(void);
static __attribute__((always_inline)) void syscall_patchA0table() {
    __nugget_bios_syscall_patchA0table();
}

NUGGET_BIOS_THUNK(syscall_getDeviceStatus, 0xc0, 0x1d)
NUGGET_BIOS_EXTERN int __nugget_bios_syscall_getDeviceStatus(void);
static __attribute__((always_inline)) int syscall_getDeviceStatus() {
    return __nugget_bios_syscall_getDeviceStatus();
}
#endif // #ifdef PS1_PC_PORT
