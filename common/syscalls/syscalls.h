/***************************************************************************
 *   Copyright (C) 2020 PCSX-Redux authors                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

#pragma once

#include <stdarg.h>

static __inline__ void enterCriticalSection() {
    register volatile int n asm("a0") = 1;
    __asm__ volatile("syscall\n" : "=r"(n) : "r"(n));
}

static __inline__ void leaveCriticalSection() {
    register volatile int n asm("a0") = 2;
    __asm__ volatile("syscall\n" : "=r"(n) : "r"(n));
}

/* A0 table */
static __inline__ int syscall_strcmp(const char * s1, const char * s2) {
    register volatile int n asm("t1") = 0x17;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(const char * s1, const char * s2))0xa0)(s1, s2);
}

static __inline__ void syscall__exit() {
    register volatile int n asm("t1") = 0x3a;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)())0xa0)();
}

// doing this one in raw inline assembly would prove tricky,
// and there's already enough voodoo in this file.
// this is syscall a0:3f
int syscall_printf(const char * fmt, ...);

static __inline__ int syscall_cdromSeekL(uint8_t * msf) {
    register volatile int n asm("t1") = 0x78;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(uint8_t *))0xa0)(msf);
}

static __inline__ int syscall_cdromRead(int count, char * buffer, uint32_t mode) {
    register volatile int n asm("t1") = 0x7e;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(int, char *, uint32_t))0xa0)(count, buffer, mode);
}

static __inline__ int syscall_cdromInnerInit() {
    register volatile int n asm("t1") = 0x95;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)())0xa0)();
}

static __inline__ int syscall_addConsoleDevice() {
    register volatile int n asm("t1") = 0x98;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)())0xa0)();
}

static __inline__ int syscall_addDummyConsoleDevice() {
    register volatile int n asm("t1") = 0x99;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)())0xa0)();
}

static __inline__ void syscall_cdromException(int code1, int code2) {
    register volatile int n asm("t1") = 0xa1;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)(int, int))0xa0)(code1, code2);
}

static __inline__ void syscall_enqueueCDRomHandlers() {
    register volatile int n asm("t1") = 0xa2;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)())0xa0)();
}

static __inline__ void syscall_ioabort(int code) {
    register volatile int n asm("t1") = 0xb2;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)(int))0xa0)(code);
}

/* B0 table */
static __inline__ uint32_t syscall_openEvent(uint32_t class, uint32_t spec, uint32_t mode, void * handler) {
    register volatile int n asm("t1") = 0x08;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((uint32_t(*)(uint32_t, uint32_t, uint32_t, void *))0xb0)(class, spec, mode, handler);
}

static __inline__ int syscall_testEvent(uint32_t event) {
    register volatile int n asm("t1") = 0x0b;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(uint32_t))0xb0)(event);
}

static __inline__ int syscall_enableEvent(uint32_t event) {
    register volatile int n asm("t1") = 0x0c;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(uint32_t))0xb0)(event);
}

static __inline__ void syscall_setDefaultExceptionJmpBuf() {
    register volatile int n asm("t1") = 0x18;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)())0xb0)();
}

static __inline__ int syscall_open(const char * filename, int mode) {
    register volatile int n asm("t1") = 0x32;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(const char *, int))0xb0)(filename, mode);
}

static __inline__ int syscall_read(int fd, void * buffer, int size) {
    register volatile int n asm("t1") = 0x34;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(int, void *, int))0xb0)(fd, buffer, size);
}

static __inline__ int syscall_close(int fd) {
    register volatile int n asm("t1") = 0x36;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    return ((int(*)(int))0xb0)(fd);
}

static __inline__ void syscall_putchar(int c) {
    register volatile int n asm("t1") = 0x3d;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)(int))0xb0)(c);
}

/* C0 table */
static __inline__ void syscall_installExceptionHandler() {
    register volatile int n asm("t1") = 0x07;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)())0xc0)();
}

static __inline__ void syscall_setupFileIO(int installTTY) {
    register volatile int n asm("t1") = 0x12;
    __asm__ volatile("" : "=r"(n) : "r"(n));
    ((void(*)(int))0xc0)(installTTY);
}