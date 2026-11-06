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

namespace psyqo {

/**
 * @brief Shadow call stack, filled in by `BUILD=FastDebug`.
 *
 * @details MIPS GCC does not keep a walkable chain of frame pointers: even
 * with `-fno-omit-frame-pointer`, where a function saves its caller's frame
 * pointer depends on the size of its frame. So the FastDebug build type
 * compiles everything with `-finstrument-functions`, and psyqo provides the
 * `__cyg_profile_func_enter` and `__cyg_profile_func_exit` hooks, which push
 * and pop the call site of every instrumented function onto a small array.
 * Reading that array from anywhere gives the chain of return addresses that
 * led there, which `mipsel-none-elf-addr2line -f -e program.elf` turns back
 * into functions and lines. Each entry is a return address, 8 bytes past the
 * `jal`, so subtract 8 to get the line of the call itself. The hooks cost two calls per function call.
 *
 * FastDebug also defines `PSYQO_SHADOW_STACK`. Without it, the hooks are not
 * compiled at all, `enabled` is false, and the accessors below return zero.
 *
 * Entry 0 is the outermost call. The stack holds `c_capacity` entries;
 * deeper calls are counted in `depth()` but their call sites are not
 * stored, so the pops stay balanced and the innermost frames are the ones
 * that go missing.
 *
 * Rules for code running under FastDebug:
 * - Interrupt handlers may be instrumented. A push reserves its slot before
 *   writing it, and an interrupt pops everything it pushed before it
 *   returns, so an interrupt landing in the middle of a hook leaves the
 *   interrupted stack intact. Anything sampling the stack from an interrupt
 *   sees the interrupt's own frames on top of the interrupted ones.
 * - A function that leaves through anything but a return, such as a
 *   callback that ends in `syscall_returnFromException`, leaks its entry,
 *   and has to be marked `__attribute__((no_instrument_function))`.
 * - Code that switches stacks has to switch shadow stacks with it, using
 *   `switchTo`. The stackful coroutines do this already.
 * - Hot helpers that are called often enough to drown everything else can
 *   opt out with `__attribute__((no_instrument_function))`.
 */
class ShadowStack {
  public:
    static constexpr unsigned c_capacity = 64;

    /**
     * @brief One shadow stack, for one machine stack.
     */
    struct Context {
        uint32_t depth = 0;
        uintptr_t entries[c_capacity] = {};
    };

#ifdef PSYQO_SHADOW_STACK
    static constexpr bool enabled = true;

    /**
     * @brief The current call depth, which may exceed `c_capacity`.
     */
    [[gnu::no_instrument_function]] static unsigned depth();

    /**
     * @brief The call site at `index`, 0 being the outermost call.
     * @return 0 if `index` is past the depth or the capacity.
     */
    [[gnu::no_instrument_function]] static uintptr_t entry(unsigned index);

    /**
     * @brief Copy the call sites out, innermost first, like a backtrace.
     * @return The number of entries written, at most `max`.
     */
    [[gnu::no_instrument_function]] static unsigned copy(uintptr_t* out, unsigned max);

    /**
     * @brief Make `context` the current shadow stack.
     * @return The previous one.
     */
    [[gnu::no_instrument_function]] static Context* switchTo(Context* context);
#else
    static constexpr bool enabled = false;
    static constexpr unsigned depth() { return 0; }
    static constexpr uintptr_t entry(unsigned) { return 0; }
    static constexpr unsigned copy(uintptr_t*, unsigned) { return 0; }
    static constexpr Context* switchTo(Context*) { return nullptr; }
#endif
};

}  // namespace psyqo
