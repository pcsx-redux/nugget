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
 * @brief Ownership arbiter for the SIO0 serial bus.
 *
 * @details The controller (pad) and the memory card share a single SIO0 bus
 * and cannot drive it at the same time: two owners clocking the line at once
 * corrupt each other's transfers. A memory card operation can span many
 * sector transfers over many frames, so it takes this lock for its whole
 * duration, and `AdvancedPad` (and `SimplePad`) consult it each frame.
 *
 * The lock is re-entrant: a filesystem operation holds it across all of its
 * sector transfers even though each blocking sector transfer also takes it.
 *
 * Holding the lock does not mean the bus is busy. Between two sector
 * transfers the card is deselected and the bus is idle for about a frame. A
 * holder that knows this can open a poll window with
 * `allowPollsBetweenTransfers`; the asynchronous filesystem does. With the
 * window open, a pad poll runs whenever no transfer is in flight, and a poll
 * that arrives during a transfer is left pending and serviced by the card
 * driver before it issues the next one. Without the window, the pad stands
 * down for as long as the lock is held.
 *
 * There is a single bus, so the state is static. Ownership and the poll window
 * are only mutated from the main thread. The in-flight flag is set from the
 * main thread when a transfer starts and cleared by the transfer engine's
 * interrupt when it ends; since transfers only start on the main thread, a
 * main-thread reader that sees it clear cannot have a transfer start under it.
 */
class SIO0Bus {
  public:
    /**
     * @brief Takes ownership of the bus for the calling transaction.
     */
    static void acquire() { s_depth = s_depth + 1; }

    /**
     * @brief Releases one level of ownership taken by `acquire`.
     */
    static void release() {
        if (s_depth) s_depth = s_depth - 1;
    }

    /**
     * @brief Returns whether the bus is currently owned by a card transaction.
     */
    static bool owned() { return s_depth != 0; }

    /**
     * @brief RAII helper that owns the bus for the lifetime of the object.
     *
     * @details Used at the entry of a memory card operation; the bus stays
     * owned until the operation's stack frame (or coroutine frame) unwinds.
     */
    struct Lock {
        Lock() { acquire(); }
        ~Lock() { release(); }
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;
    };

    /**
     * @brief Opens or closes the poll window of the current owner.
     *
     * @details While open, the pad may poll during the owner's transaction,
     * provided no transfer is in flight. Closed by default.
     */
    static void allowPollsBetweenTransfers(bool allow) { s_pollWindow = allow; }

    /**
     * @brief Marks the start and end of one transfer on the bus.
     *
     * @details `endTransfer` may be called from interrupt context.
     */
    static void beginTransfer() { s_transferring = true; }
    static void endTransfer() { s_transferring = false; }

    /**
     * @brief Returns whether a pad poll may drive the bus right now.
     */
    static bool canPoll() { return !s_transferring && (s_depth == 0 || s_pollWindow); }

    /**
     * @brief Registers the function that performs a pad poll.
     *
     * @details Used to service a pending poll. A single pad driver registers
     * itself here.
     */
    static void setPollService(void (*service)(void *), void *context) {
        s_pollService = service;
        s_pollContext = context;
    }

    /**
     * @brief Records that a pad poll was refused and should run when possible.
     */
    static void requestPoll() { s_pollPending = true; }

    /**
     * @brief Clears a pending poll request, when the poll ran anyway.
     */
    static void clearPendingPoll() { s_pollPending = false; }

    /**
     * @brief Runs a pending pad poll if the bus allows one now.
     *
     * @details Called from the main thread by the card driver at the point
     * where the bus is idle and the next transfer is about to be issued.
     */
    static void servicePendingPoll() {
        if (!s_pollPending || !s_pollService || !canPoll()) return;
        s_pollPending = false;
        s_pollService(s_pollContext);
    }

  private:
    static inline volatile uint32_t s_depth = 0;
    static inline volatile bool s_transferring = false;
    static inline bool s_pollWindow = false;
    static inline bool s_pollPending = false;
    static inline void (*s_pollService)(void *) = nullptr;
    static inline void *s_pollContext = nullptr;
};

}  // namespace psyqo
