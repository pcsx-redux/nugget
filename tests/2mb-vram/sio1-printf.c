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

// Replacement printf for the 573 VRAM test suite that streams output over
// SIO1 instead of BIOS A0:0x3F. Lazy-inits SIO1 on first call - test code
// does not need to know about this layer; existing ramsyscall_printf
// references just work and bypass any BIOS TTY redirection (or its
// absence) entirely.
//
// Configuration: 115200 baud, 8N2, RTS asserted. The System 573 loops RTS
// back to CTS internally and SIO1 has always-on hardware flow control, so
// asserting RTS is mandatory or the transmitter will stall waiting for CTS.

#include <stdarg.h>
#include <stdint.h>

#include "common/hardware/hwregs.h"

// SIO1 register map (psx-spx). kseg1 / uncached bypass addresses.
#define SIO1_DATA8   (*(volatile uint8_t  *)0xbf801050)
#define SIO1_STAT    (*(volatile uint32_t *)0xbf801054)
#define SIO1_MODE    (*(volatile uint16_t *)0xbf801058)
#define SIO1_CTRL    (*(volatile uint16_t *)0xbf80105a)
#define SIO1_BAUD    (*(volatile uint16_t *)0xbf80105e)

// SIO_STAT bits used here.
#define SIO_STAT_TX_RDY1   (1u << 0)   // ready to take new TX byte
#define SIO_STAT_TX_RDY2   (1u << 2)   // TX fully complete

// SIO_CTRL bits.
#define SIO_CTRL_TX_EN     (1u << 0)
#define SIO_CTRL_DTR       (1u << 1)
#define SIO_CTRL_RX_EN     (1u << 2)
#define SIO_CTRL_RTS       (1u << 5)
#define SIO_CTRL_RESET     (1u << 6)

static int g_sio1_initialized = 0;

static void sio1Init(void) {
    // On a retail PS1 under Unirom, the
    // resident TTY stub may have an active SIO1 IRQ hooked. If it fires
    // while we're configuring SIO1, the handler races with our register
    // writes and corrupts the state. Disable interrupts globally before
    // touching the registers; tests do not use IRQs, so we never
    // re-enable.

    // Mask all hardware interrupts in the IRQ controller and ack any
    // pending. This prevents the SIO1 IRQ line from ever reaching the
    // CPU.
    IMASK = 0;
    IREG = 0;

    // Clear COP0 SR bit 0 (IEc) so even a previously-pending interrupt
    // already in flight on the CPU side will not be serviced.
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr &= ~1u;
    __asm__ volatile("mtc0 %0, $12\n nop" : : "r"(sr));

    // Wait for any in-flight TX from Unirom / BIOS to drain before we
    // disturb the registers - a partial byte transmitted across our
    // reconfigure produces a garbled prefix and confuses the receiver's
    // framing. Bit 2 of STAT (TX Ready 2) goes high once the shift
    // register has fully clocked out the last byte.
    while ((SIO1_STAT & SIO_STAT_TX_RDY2) == 0) {
    }

    // Stop the transmitter so subsequent BAUD/MODE writes don't fight
    // an active byte. Then set the parameters explicitly. We deliberately
    // do NOT pulse SIO_CTRL_RESET here - on retail under Unirom that
    // wipes some state Unirom or our own boot path needs intact, and
    // Unirom's existing SIO1 config is already 115200 8N2 (matches what
    // we want).
    SIO1_CTRL = 0;

    // baud = sysclock / (factor * BAUD_reload). With MUL64 there is no
    // integer reload near 115200 (4.59), so use MUL1:
    //   33868800 / 115200 = 294 -> 115200 exact.
    SIO1_BAUD = 294;

    // MODE: factor=MUL1 (bits 0-1=01), char_len=8 (bits 2-3=11),
    //       parity_off (bit 4=0), stop_bits=2 (bits 6-7=11).
    //       Unirom uses two stop bits; matching it avoids framing-error
    //       garbage on retail.
    // = 0xc0 | 0x0c | 0x01 = 0xcd.
    SIO1_MODE = 0x00cd;

    // CTRL: TX | DTR | RX | RTS, all interrupts disabled.
    SIO1_CTRL = SIO_CTRL_TX_EN | SIO_CTRL_DTR | SIO_CTRL_RX_EN | SIO_CTRL_RTS;

    g_sio1_initialized = 1;
}

static void sio1Putc(char c) {
    if (!g_sio1_initialized) {
        sio1Init();
    }
    while ((SIO1_STAT & SIO_STAT_TX_RDY1) == 0) {
    }
    SIO1_DATA8 = (uint8_t)c;
}

static void sio1Putstr(const char *s) {
    while (*s) sio1Putc(*s++);
}

// Print an unsigned value in the requested base, padded to the requested
// minimum width with the requested pad char.
static void putUnsigned(unsigned int v, unsigned int base, int width, char pad) {
    char buf[16];
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    } else {
        while (v > 0) {
            unsigned int d = v % base;
            buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            v /= base;
        }
    }
    while (n < width) {
        sio1Putc(pad);
        width--;
    }
    while (n > 0) {
        sio1Putc(buf[--n]);
    }
}

// Replacement for ramsyscall_printf. Supports the format specifiers used
// by the test suite: %s, %c, %d, %u, %x, %X, plus optional zero-pad and
// width modifiers (e.g. %02d, %04x, %08x). %% emits a literal percent.
// Anything else gets a '?' so missing-feature bugs are visible in output.
int ramsyscall_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            sio1Putc(*fmt++);
            continue;
        }
        fmt++;

        char pad = ' ';
        int width = 0;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
            case 'd':
            case 'i': {
                int v = va_arg(ap, int);
                if (v < 0) {
                    sio1Putc('-');
                    if (width > 0) width--;
                    putUnsigned((unsigned)(-v), 10, width, pad);
                } else {
                    putUnsigned((unsigned)v, 10, width, pad);
                }
                break;
            }
            case 'u':
                putUnsigned(va_arg(ap, unsigned int), 10, width, pad);
                break;
            case 'x':
            case 'X':
                putUnsigned(va_arg(ap, unsigned int), 16, width, pad);
                break;
            case 'c':
                sio1Putc((char)va_arg(ap, int));
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (s == 0) s = "(null)";
                while (*s) sio1Putc(*s++);
                break;
            }
            case '%':
                sio1Putc('%');
                break;
            default:
                sio1Putc('?');
                break;
        }
        if (*fmt) fmt++;
    }
    va_end(ap);
    return 0;
}

// Drain the TX FIFO before any caller gives up. Useful right before an
// infinite-loop idle so the last few bytes have time to actually clock
// out of the UART.
void sio1Flush(void) {
    if (!g_sio1_initialized) return;
    while ((SIO1_STAT & SIO_STAT_TX_RDY2) == 0) {
    }
}
