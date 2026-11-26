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

// The emulator fills a guest buffer with host randomness, or reports why it
// did not. Every refusal must leave memory exactly as it was.

#include <stdint.h>

#include "common/hardware/pcsxcmd.h"
#include "common/hardware/pcsxhw.h"
#include "common/syscalls/syscalls.h"

static int s_failures = 0;

static void fail(const char* what, uint32_t got, uint32_t expected) {
    ramsyscall_printf("get-random: FAIL %s: got %u, expected %u\n", what, got, expected);
    s_failures++;
}

static void expectStatus(const char* what, uint32_t got, uint32_t expected) {
    if (got != expected) fail(what, got, expected);
}

static uint32_t fill(volatile uint8_t* p, uint32_t length, uint8_t value) {
    for (uint32_t i = 0; i < length; i++) p[i] = value;
    return length;
}

static int untouched(volatile const uint8_t* p, uint32_t length, uint8_t value) {
    for (uint32_t i = 0; i < length; i++) {
        if (p[i] != value) return 0;
    }
    return 1;
}

static uint32_t sumBytes(volatile const uint8_t* p, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += p[i];
    return sum;
}

// 64 bytes at an odd offset inside a guarded buffer, so the fill has to be
// unaligned and exactly as long as asked.
static uint8_t s_first[72];
static uint8_t s_second[72];

int main() {
    if (!pcsx_commandSupported(PCSX_CMD_GET_RANDOM)) {
        ramsyscall_printf("get-random: FAIL operation not supported\n");
        return 1;
    }

    fill(s_first, sizeof(s_first), 0xa5);
    fill(s_second, sizeof(s_second), 0xa5);
    expectStatus("first fill", pcsx_getRandom(s_first + 3, 64), PCSX_CMDSTATUS_OK);
    expectStatus("second fill", pcsx_getRandom(s_second + 3, 64), PCSX_CMDSTATUS_OK);
    if (!untouched(s_first, 3, 0xa5) || !untouched(s_first + 67, 5, 0xa5)) fail("first fill guards", 0, 1);
    if (!untouched(s_second, 3, 0xa5) || !untouched(s_second + 67, 5, 0xa5)) fail("second fill guards", 0, 1);
    if (untouched(s_first + 3, 64, 0xa5)) fail("first fill wrote nothing", 0, 1);
    if (untouched(s_second + 3, 64, 0xa5)) fail("second fill wrote nothing", 0, 1);
    int same = 1;
    for (unsigned i = 3; i < 67; i++) {
        if (s_first[i] != s_second[i]) same = 0;
    }
    if (same) fail("two fills are identical", 1, 0);

    // A length of zero succeeds and writes nothing, wherever it points.
    uint8_t canary[4];
    fill(canary, 4, 0x5a);
    expectStatus("zero length", pcsx_getRandom(canary, 0), PCSX_CMDSTATUS_OK);
    if (!untouched(canary, 4, 0x5a)) fail("zero length wrote", 0, 1);
    expectStatus("zero length, unmapped buffer", pcsx_getRandom((void*)0xfffffff0, 0), PCSX_CMDSTATUS_OK);

    // The BIOS ROM can be read but not written.
    volatile uint8_t* rom = (volatile uint8_t*)0xbfc00000;
    uint32_t romSum = sumBytes(rom, 16);
    expectStatus("rom", pcsx_getRandom((void*)rom, 16), PCSX_CMDSTATUS_UNMAPPED);
    if (sumBytes(rom, 16) != romSum) fail("rom changed", 0, 1);

    // Half in the dev board SRAM, half in the ROM above it: the whole range is
    // refused, so the SRAM half keeps its canary.
    volatile uint8_t* sram = (volatile uint8_t*)0xbfbffff8;
    fill(sram, 8, 0x3c);
    expectStatus("straddle", pcsx_getRandom((void*)sram, 16), PCSX_CMDSTATUS_UNMAPPED);
    if (!untouched(sram, 8, 0x3c)) fail("straddle wrote its mapped half", 0, 1);

    // A range running off the top of the address space.
    expectStatus("wrap", pcsx_getRandom((void*)0xfffffff8, 16), PCSX_CMDSTATUS_UNMAPPED);

    // The scratchpad is writable, up to its last byte and not past it.
    volatile uint8_t* scratch = (volatile uint8_t*)0x1f800100;
    fill(scratch, 72, 0xa5);
    expectStatus("scratchpad", pcsx_getRandom((void*)(scratch + 3), 64), PCSX_CMDSTATUS_OK);
    if (!untouched(scratch, 3, 0xa5) || !untouched(scratch + 67, 5, 0xa5)) fail("scratchpad guards", 0, 1);
    if (untouched(scratch + 3, 64, 0xa5)) fail("scratchpad fill wrote nothing", 0, 1);
    volatile uint8_t* scratchTop = (volatile uint8_t*)0x1f8003f8;
    fill(scratchTop, 8, 0x3c);
    expectStatus("scratchpad end", pcsx_getRandom((void*)scratchTop, 8), PCSX_CMDSTATUS_OK);
    fill(scratchTop, 8, 0x3c);
    expectStatus("past the scratchpad", pcsx_getRandom((void*)scratchTop, 9), PCSX_CMDSTATUS_UNMAPPED);
    if (!untouched(scratchTop, 8, 0x3c)) fail("past the scratchpad wrote", 0, 1);

    // Filled msan memory reads as initialized; the run quits on the read below
    // if it does not. A range reaching past the block is refused.
    pcsx_initMsan();
    volatile uint8_t* block = (volatile uint8_t*)pcsx_msanAlloc(32);
    expectStatus("msan block", pcsx_getRandom((void*)block, 32), PCSX_CMDSTATUS_OK);
    ramsyscall_printf("get-random: msan block sum %u\n", sumBytes(block, 32));
    expectStatus("past the msan block", pcsx_getRandom((void*)(block + 24), 16), PCSX_CMDSTATUS_UNMAPPED);
    pcsx_msanFree((void*)block);

    // A struct declaring itself without the length field.
    struct pcsx_GetRandom cmd;
    pcsx_commandInit(&cmd.header, PCSX_CMD_GET_RANDOM, sizeof(cmd) - 4);
    cmd.buffer = (uint32_t)(uintptr_t)canary;
    cmd.length = 4;
    pcsx_command(&cmd);
    expectStatus("short struct", cmd.header.results.status, PCSX_CMDSTATUS_SHORT_STRUCT);
    if (!untouched(canary, 4, 0x5a)) fail("short struct wrote", 0, 1);

    if (s_failures) {
        ramsyscall_printf("get-random: %d failures\n", s_failures);
        return 1;
    }
    ramsyscall_printf("get-random: PASS\n");
    return 0;
}
