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

#include "psyqo/shadow-stack.hh"

#ifdef PSYQO_SHADOW_STACK

namespace {

constinit psyqo::ShadowStack::Context s_mainContext;

}  // namespace

// Constant-initialized, so it is valid before any constructor runs, and
// constructors are instrumented too.
constinit static psyqo::ShadowStack::Context* s_current = &s_mainContext;

extern "C" {

[[gnu::no_instrument_function]] void __cyg_profile_func_enter(void*, void* callSite) {
    psyqo::ShadowStack::Context* context = s_current;
    uint32_t depth = context->depth;
    // Reserve the slot before writing it: an interrupt landing in between
    // pushes and pops above it.
    context->depth = depth + 1;
    __asm__ volatile("" ::: "memory");
    if (depth < psyqo::ShadowStack::c_capacity) context->entries[depth] = reinterpret_cast<uintptr_t>(callSite);
}

[[gnu::no_instrument_function]] void __cyg_profile_func_exit(void*, void*) {
    psyqo::ShadowStack::Context* context = s_current;
    uint32_t depth = context->depth;
    if (depth) context->depth = depth - 1;
}
}

unsigned psyqo::ShadowStack::depth() { return s_current->depth; }

uintptr_t psyqo::ShadowStack::entry(unsigned index) {
    Context* context = s_current;
    if ((index >= context->depth) || (index >= c_capacity)) return 0;
    return context->entries[index];
}

unsigned psyqo::ShadowStack::copy(uintptr_t* out, unsigned max) {
    Context* context = s_current;
    unsigned depth = context->depth;
    if (depth > c_capacity) depth = c_capacity;
    if (max > depth) max = depth;
    for (unsigned i = 0; i < max; i++) out[i] = context->entries[depth - 1 - i];
    return max;
}

psyqo::ShadowStack::Context* psyqo::ShadowStack::switchTo(Context* context) {
    Context* previous = s_current;
    s_current = context;
    return previous;
}

#endif
