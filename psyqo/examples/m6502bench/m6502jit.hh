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

#include <stdint.h>

#include "m6502.hh"

// A 6502 -> MIPS I translator. Code is interpreted until a block entry point has
// been reached THRESHOLD times, then translated. Blocks are superblocks: a
// conditional branch is a side exit and the not-taken path continues inline.
// Every block carries its static cycle total and checks it against the budget on
// entry; side exits refund what they skipped. Stores check a one-bit-per-byte map
// of translated code, so self-modifying code exits right after the store and the
// dispatcher invalidates the blocks that cover the written byte.

namespace m6502jit {

struct Stats {
    uint32_t compiled;
    uint32_t compileFailed;
    uint32_t invalidations;
    uint32_t blocksKilled;
    uint32_t flushes;
    uint32_t exitsChain;
    uint32_t exitsBudget;
    uint32_t exitsDecimal;
    uint32_t exitsSmc;
    uint32_t interpBlocks;
    uint32_t codeWords;
    uint32_t interpCycles;
    uint32_t exitsSlow;
    uint32_t exitsIrq;
    uint32_t singleSteps;
    uint32_t rangeFlushes;
};

void init(m6502::State& st);
m6502::Stop run(m6502::State& st, uint32_t budget);
const Stats& stats();
// Drop translations of [lo, hi), for a machine whose memory map just changed.
void invalidateRange(uint32_t lo, uint32_t hi);

}  // namespace m6502jit
