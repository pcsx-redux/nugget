/* RAM-loaded transport bring-up probe.
 *
 * Emits the monitor's HELLO frame in a loop over the ATCONS word channel,
 * reusing the monitor's real transport.c verbatim. A green HELLO on the host's
 * monitor-runner validates the ACTUAL word path the deployed monitor runs, not
 * a reimplementation. The emit loop self-throttles: wordPut() spins on the
 * PS1-side TX-ready STAT bit, so the host drains frames at the channel's rate.
 *
 * HELLO payload (5 words), mirroring monitor.c:emitHello():
 *   [proto_ver:u16][sram_base:u32][ram_size:u32]
 */
#include <stdint.h>

#include "openbios/monitor/transport.h"

#define MON_HELLO      0x80
#define MON_PROTO_VER  0x0001
#define MON_SRAM_BASE  0x1fa00000u
#define PROBE_RAM_SIZE 0x00800000u /* 8 MiB devkit; placeholder, host just decodes it */

static void sendWord32(uint32_t v) {
    transportSendWord((uint16_t)(v & 0xffff));
    transportSendWord((uint16_t)(v >> 16));
}

void probeMain(void) {
    transportInit();
    for (;;) {
        transportSendBegin(MON_HELLO, 5);
        transportSendWord(MON_PROTO_VER);
        sendWord32(MON_SRAM_BASE);
        sendWord32(PROBE_RAM_SIZE);
        transportSendEnd();
    }
}
