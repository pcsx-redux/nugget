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

#include <stdint.h>

#include "psyqo/bump-allocator.hh"
#include "psyqo/ordering-table.hh"
#include "psyqo/primitives/triangles.hh"
#include "snitch_all.hpp"

using namespace psyqo;

static bool aligned4(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 3) == 0; }

struct ThreeBytes {
    uint8_t b[3];
};

TEST_CASE("BumpAllocator keeps fragments aligned after an odd-sized allocation") {
    static BumpAllocator<256> balloc;
    balloc.reset();
    balloc.allocate<uint8_t>(uint8_t(1));
    auto& f1 = balloc.allocateFragment<Prim::Triangle>();
    REQUIRE(aligned4(&f1));
    balloc.allocate<ThreeBytes>();
    auto& f2 = balloc.allocateFragment<Prim::Triangle>();
    REQUIRE(aligned4(&f2));
    REQUIRE(balloc.used() % 4 == 0);
}

TEST_CASE("DynamicBumpAllocator keeps fragments aligned after an odd-sized allocation") {
    static uint32_t storage[64];
    DynamicBumpAllocator<> balloc(storage, sizeof(storage));
    balloc.allocate<uint8_t>(uint8_t(1));
    auto& f1 = balloc.allocateFragment<Prim::Triangle>();
    REQUIRE(aligned4(&f1));
    balloc.allocate<ThreeBytes>();
    auto& f2 = balloc.allocateFragment<Prim::Triangle>();
    REQUIRE(aligned4(&f2));
    REQUIRE(balloc.used() % 4 == 0);
    REQUIRE(balloc.remaining() == sizeof(storage) - balloc.used());
}

TEST_CASE("DynamicOrderingTable chains an inserted fragment into its bucket") {
    static Fragments::ChainEntry storage[9];
    DynamicOrderingTable<> ot(storage, 8);
    REQUIRE(ot.size() == 8);
    static uint32_t fragStorage[16];
    DynamicBumpAllocator<> balloc(fragStorage, sizeof(fragStorage));
    auto& frag = balloc.allocateFragment<Prim::Triangle>();
    ot.insert(frag, 3);
    REQUIRE((storage[4].head & 0xffffff) == (reinterpret_cast<uint32_t>(&frag) & 0xffffff));
}
