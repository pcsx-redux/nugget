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

// The BIOS memory card calls, against the card in MEMCARD_PORT (0 = slot 1,
// 1 = slot 2): the low level sector calls _card_read / _card_write and the
// "bu" device behind open / read / write / firstfile / erase / format. In the
// emulator that is OpenBIOS; on a console it is the retail kernel, so the two
// are held to the same expectations. THIS TEST ERASES THE CARD, after the same
// check psyqo's memcard test makes that it holds nothing but this suite's own
// files. Nothing here addresses the other port: _bu_init, which touches both,
// is never called.
//
// Grading: every check prints PASS or FAIL, and the run ends on
// "memcard-bios port N: all K checks passed" only when every check passed.

#include <stdint.h>

#include "common/psxlibc/direntry.h"
#include "common/psxlibc/stdio.h"
#include "common/syscalls/syscalls.h"

#ifndef MEMCARD_PORT
#define MEMCARD_PORT 0
#endif

#define DEVICE (MEMCARD_PORT << 4)
#define BU_PATH(name) (MEMCARD_PORT == 0 ? "bu00:" name : "bu10:" name)

NUGGET_BIOS_THUNK(initCard, 0xb0, 0x4a)
NUGGET_BIOS_THUNK(startCard, 0xb0, 0x4b)
NUGGET_BIOS_THUNK(buFormat, 0xb0, 0x41)
NUGGET_BIOS_THUNK(firstFile, 0xb0, 0x42)
NUGGET_BIOS_THUNK(nextFile, 0xb0, 0x43)
NUGGET_BIOS_THUNK(erase, 0xb0, 0x45)
int __nugget_bios_initCard(int padEnable);
int __nugget_bios_startCard(void);
int __nugget_bios_buFormat(const char *dev);
struct DirEntry *__nugget_bios_firstFile(const char *pattern, struct DirEntry *entry);
struct DirEntry *__nugget_bios_nextFile(struct DirEntry *entry);
int __nugget_bios_erase(const char *name);
#define initCard __nugget_bios_initCard
#define startCard __nugget_bios_startCard
#define buFormat __nugget_bios_buFormat
#define firstFile __nugget_bios_firstFile
#define nextFile __nugget_bios_nextFile
#define erase __nugget_bios_erase

// Low level completion is signalled on the hardware card event class.
#define EVENT_HWCARD 0xf0000011
enum { DONE, ERROR, TIMEOUT, NEWCARD, EVENTS };
static const uint32_t s_specs[EVENTS] = {0x0004, 0x8000, 0x0100, 0x2000};
static const char *const s_names[EVENTS] = {"done", "error", "timeout", "new card"};
static uint32_t s_events[EVENTS];

static unsigned s_checks = 0;
static unsigned s_failures = 0;

static int check(int pass, const char *name) {
    s_checks++;
    if (!pass) s_failures++;
    ramsyscall_printf("%s %s\n", pass ? "PASS" : "FAIL", name);
    return pass;
}

static int same(const uint8_t *a, const uint8_t *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int streq(const char *a, const char *b) {
    for (uint32_t i = 0;; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 1;
    }
}

static void drainEvents() {
    for (int i = 0; i < EVENTS; i++) syscall_testEvent(s_events[i]);
}

// Which event ended the last low level operation, or -1 if none came.
static int waitCard() {
    for (uint32_t spin = 0; spin < 0x200000; spin++) {
        for (int i = 0; i < EVENTS; i++) {
            if (syscall_testEvent(s_events[i])) return i;
        }
    }
    return -1;
}

static const char *eventName(int e) { return e < 0 ? "no event" : s_names[e]; }

// A card reports itself new on every access until something writes to it,
// and the kernel turns that into a "new card" event. _new_card tells the
// kernel to let the next access through; the access is then retried once.
static int cardRead(int sector, uint8_t *buffer) {
    for (int attempt = 0;; attempt++) {
        drainEvents();
        if (!syscall_mcReadSector(DEVICE, sector, buffer)) return -2;
        int e = waitCard();
        if (e != NEWCARD || attempt) return e;
        syscall_mcAllowNewCard();
    }
}

static int cardWrite(int sector, const uint8_t *buffer) {
    for (int attempt = 0;; attempt++) {
        drainEvents();
        if (!syscall_mcWriteSector(DEVICE, sector, buffer)) return -2;
        int e = waitCard();
        if (e != NEWCARD || attempt) return e;
        syscall_mcAllowNewCard();
    }
}

static uint8_t s_frame[128];
static uint8_t s_frame2[128];
static uint8_t s_data[2048];
static uint8_t s_readback[2048];

static void fill(uint8_t *buf, uint32_t len, uint32_t seed) {
    uint32_t x = seed * 2654435761u + 7;
    for (uint32_t i = 0; i < len; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (uint8_t)(x >> 16);
    }
}

static int startsWith(const uint8_t *s, const char *prefix) {
    for (uint32_t i = 0; prefix[i]; i++) {
        if (s[i] != (uint8_t)prefix[i]) return 0;
    }
    return 1;
}

// The same rule as tests/memcard: blank, this suite's own title frame over
// frame 0, or a Sony directory naming nothing but BIMEMTEST files.
static int cardIsOursToErase() {
    static const uint8_t titleSjis[] = {0x82, 0x6c, 0x82, 0x73};
    int e = cardRead(0, s_frame);
    if (e != DONE) {
        ramsyscall_printf("REFUSE: frame 0 read ended on %s\n", e == -2 ? "a refused call" : eventName(e));
        return 0;
    }
    int zeros = 1, ones = 1;
    for (int i = 0; i < 128; i++) {
        zeros = zeros && s_frame[i] == 0x00;
        ones = ones && s_frame[i] == 0xff;
    }
    if (zeros || ones) {
        ramsyscall_printf("card: blank (frame 0 all %s)\n", zeros ? "00" : "ff");
        return 1;
    }
    if (s_frame[0] == 'S' && s_frame[1] == 'C' && same(s_frame + 4, titleSjis, sizeof(titleSjis))) {
        ramsyscall_printf("card: frame 0 holds the memcard test's title frame, from an earlier run\n");
        return 1;
    }
    if (s_frame[0] != 'M' || s_frame[1] != 'C') {
        ramsyscall_printf("REFUSE: frame 0 is neither blank nor a Sony header\n");
        return 0;
    }
    for (int s = 1; s <= 15; s++) {
        e = cardRead(s, s_frame);
        if (e != DONE) {
            ramsyscall_printf("REFUSE: directory frame %d read ended on %s\n", s, eventName(e));
            return 0;
        }
        if (s_frame[0] == 0x51 && !startsWith(s_frame + 0x0a, "BIMEMTEST")) {
            s_frame[0x0a + 20] = 0;
            ramsyscall_printf("REFUSE: block %d holds a save this test did not write: %s\n", s,
                              (const char *)s_frame + 0x0a);
            return 0;
        }
    }
    ramsyscall_printf("card: Sony filesystem, no save but this suite's\n");
    return 1;
}

static void testSectors() {
    int ok = 1;
    for (int s = 64; s < 67; s++) {
        fill(s_frame, 128, s);
        int e = cardWrite(s, s_frame);
        if (e != DONE) ramsyscall_printf("_card_write %d ended on %s\n", s, eventName(e)), ok = 0;
    }
    for (int s = 64; s < 67; s++) {
        fill(s_frame, 128, s);
        int e = cardRead(s, s_frame2);
        if (e != DONE || !same(s_frame, s_frame2, 128)) {
            ramsyscall_printf("_card_read %d ended on %s%s\n", s, eventName(e), e == DONE ? ", data differs" : "");
            ok = 0;
        }
    }
    check(ok, "_card_write then _card_read, 3 sectors");

    // The last sector is 1023. The retail kernel's range check lets 1024
    // through (measured on an SCPH-5501, and OpenBIOS matches it), and the
    // card is what refuses it, with an error event.
    drainEvents();
    int accepted = syscall_mcReadSector(DEVICE, 1024, s_frame2);
    int e = accepted ? waitCard() : -2;
    ramsyscall_printf("_card_read 1024: %s, ended on %s\n", accepted ? "accepted" : "refused", eventName(e));
    check(accepted, "_card_read passes sector 1024 to the card");
#ifndef PCSX_TESTS
    // PCSX-Redux's card answers sector 1024 with 128 FF bytes and a good end
    // byte, so the kernel reports it done.
    check(e == ERROR, "the card refuses sector 1024");
#endif
    drainEvents();
    accepted = syscall_mcReadSector(DEVICE, 1023, s_frame2);
    e = accepted ? waitCard() : -2;
    check(accepted && e == DONE, "_card_read takes sector 1023");
}

static void testDevice() {
    struct DirEntry entry;
    check(buFormat(BU_PATH("")) == 1, "format");
    check(firstFile(BU_PATH("*"), &entry) == NULL, "firstfile on a formatted card finds nothing");

    int fd = syscall_open(BU_PATH("BIMEMTEST-BIOS"), PSXF_CREAT | (1 << 16));
    check(fd >= 0, "open O_CREAT, 1 block");
    if (fd >= 0) syscall_close(fd);

    fill(s_data, 2048, 1);
    fd = syscall_open(BU_PATH("BIMEMTEST-BIOS"), PSXF_WRITE);
    int n = fd >= 0 ? (int)syscall_write(fd, s_data, 1024) : -1;
    if (fd >= 0) syscall_close(fd);
    check(n == 1024, "write 1024 bytes");

    fd = syscall_open(BU_PATH("BIMEMTEST-BIOS2"), PSXF_CREAT | (2 << 16));
    check(fd >= 0, "open O_CREAT, 2 blocks");
    if (fd >= 0) syscall_close(fd);
    fill(s_data, 2048, 2);
    fd = syscall_open(BU_PATH("BIMEMTEST-BIOS2"), PSXF_WRITE);
    n = fd >= 0 ? (int)syscall_write(fd, s_data, 2048) : -1;
    if (fd >= 0) syscall_close(fd);
    check(n == 2048, "write 2048 bytes");

    int found = 0, sizes = 1;
    for (struct DirEntry *p = firstFile(BU_PATH("*"), &entry); p; p = nextFile(&entry)) {
        found++;
        ramsyscall_printf("firstfile/nextfile: %s, %d bytes\n", entry.name, (int)entry.size);
        if (streq(entry.name, "BIMEMTEST-BIOS"))
            sizes = sizes && entry.size == 8192;
        else if (streq(entry.name, "BIMEMTEST-BIOS2"))
            sizes = sizes && entry.size == 16384;
        else
            sizes = 0;
        if (found > 15) break;
    }
    check(found == 2 && sizes, "firstfile/nextfile list both files and their sizes");

    fill(s_data, 2048, 1);
    __builtin_memset(s_readback, 0, 1024);
    fd = syscall_open(BU_PATH("BIMEMTEST-BIOS"), PSXF_READ);
    n = fd >= 0 ? syscall_read(fd, s_readback, 1024) : -1;
    if (fd >= 0) syscall_close(fd);
    check(n == 1024 && same(s_data, s_readback, 1024), "read back the first file");
    fill(s_data, 2048, 2);
    __builtin_memset(s_readback, 0, 2048);
    fd = syscall_open(BU_PATH("BIMEMTEST-BIOS2"), PSXF_READ);
    n = fd >= 0 ? syscall_read(fd, s_readback, 2048) : -1;
    if (fd >= 0) syscall_close(fd);
    check(n == 2048 && same(s_data, s_readback, 2048), "read back the second file");

    check(erase(BU_PATH("BIMEMTEST-BIOS")) == 1, "erase the first file");
    struct DirEntry *p = firstFile(BU_PATH("*"), &entry);
    check(p && streq(entry.name, "BIMEMTEST-BIOS2") && nextFile(&entry) == NULL, "only the second file is left");
    check(syscall_open(BU_PATH("BIMEMTEST-BIOS"), PSXF_READ) < 0, "open an erased file fails");

    // What the kernel left in the directory, read raw: block 1 deleted, blocks
    // 2-3 the second file's chain, in the Sony layout psyqo's filesystem reads.
    int e1 = cardRead(1, s_frame);
    int dir1 = e1 == DONE ? s_frame[0] : -1;
    int e2 = cardRead(2, s_frame);
    int dir2 = e2 == DONE ? s_frame[0] : -1;
    int named = e2 == DONE && startsWith(s_frame + 0x0a, "BIMEMTEST-BIOS2");
    int e3 = cardRead(3, s_frame);
    int dir3 = e3 == DONE ? s_frame[0] : -1;
    ramsyscall_printf("directory states: block 1 %02x, block 2 %02x, block 3 %02x\n", dir1, dir2, dir3);
    check(dir1 == 0xa1 && dir2 == 0x51 && named && dir3 == 0x53, "directory: a1 / 51 named / 53");

    check(erase(BU_PATH("BIMEMTEST-BIOS2")) == 1, "erase the second file");
    check(firstFile(BU_PATH("*"), &entry) == NULL, "card is empty");
    check(buFormat(BU_PATH("")) == 1, "cleanup: format");
}

int main() {
    ramsyscall_printf("BIOS memory card calls, port %d (slot %d)\n", MEMCARD_PORT, MEMCARD_PORT + 1);
    initCard(0);
    startCard();
    for (int i = 0; i < EVENTS; i++) {
        s_events[i] = syscall_openEvent(EVENT_HWCARD, s_specs[i], 0x2000, NULL);
        syscall_enableEvent(s_events[i]);
    }

    if (cardIsOursToErase()) {
        testSectors();
        testDevice();
    } else {
        s_failures++;
    }

    if (s_failures == 0) {
        ramsyscall_printf("memcard-bios port %d: all %d checks passed\n", MEMCARD_PORT, s_checks);
    } else {
        ramsyscall_printf("memcard-bios port %d: %d of %d checks FAILED\n", MEMCARD_PORT, s_failures, s_checks);
    }
    return s_failures == 0 ? 0 : 1;
}
