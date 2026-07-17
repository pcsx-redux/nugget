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

#include "openbios/monitor/monitor.h"

#include "openbios/monitor/transport.h"

/* Keystone command buffer. Sized only for the small keystone commands (PING);
   Stage 2 grows this to cover READ_MEM/WRITE_MEM/LOAD payloads. */
static uint16_t s_rxPayload[64];

static void emitHello(void) {
    uint16_t hello[5];
    hello[0] = MON_PROTO_VER;
    hello[1] = (uint16_t)(MON_SRAM_BASE & 0xffff); /* sram_base low word */
    hello[2] = (uint16_t)(MON_SRAM_BASE >> 16);    /* sram_base high word */
    hello[3] = (uint16_t)(MON_RAM_SIZE & 0xffff);  /* ram_size low word */
    hello[4] = (uint16_t)(MON_RAM_SIZE >> 16);     /* ram_size high word */
    transportSendFrame(MON_HELLO, hello, 5);
}

void monitorMain(void) {
    transportInit();
    emitHello();

    /* Keystone loop: HALTED state, PING -> PONG only. Stage 2 replaces this
       with the full command dispatch (READ/WRITE_MEM, GET/SET_REG, breakpoints,
       LOAD/RUN/CONT, PCDRV) and the exception-driven event path. */
    for (;;) {
        uint16_t type;
        uint16_t len;
        int rc = transportRecvFrame(&type, s_rxPayload, 64, &len);
        if (rc != TRANSPORT_OK) {
            if (rc == TRANSPORT_ECKSUM) {
                uint16_t err[1] = {MON_ECKSUM};
                transportSendFrame(MON_ERROR, err, 1);
            }
            continue;
        }

        switch (type) {
            case MON_PING: {
                uint16_t pong[1] = {MON_PROTO_VER};
                transportSendFrame(MON_PONG, pong, 1);
                break;
            }
            default: {
                uint16_t err[1] = {MON_EBADCMD};
                transportSendFrame(MON_ERROR, err, 1);
                break;
            }
        }
    }
}
