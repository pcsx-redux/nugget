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

#include "gpdplayer/gpdplayer.h"

#include <stddef.h>
#include <stdint.h>

#include "common/hardware/hwregs.h"
#include "common/kernel/pcdrv.h"
#include "common/syscalls/syscalls.h"

// GPUDUMP magic: "PSXGPUDUMPv1r1\0\0"
static const uint8_t s_magic[16] = {
    'P', 'S', 'X', 'G', 'P', 'U', 'D', 'U',
    'M', 'P', 'v', '1', 'r', '1', '\0', '\0',
};

// Packet types
#define PKT_GP0          0x00
#define PKT_GP1          0x01
#define PKT_VSYNC        0x02
#define PKT_THROW_AWAY   0x03
#define PKT_READBACK     0x04
#define PKT_TRACE_BEGIN  0x05
#define PKT_GPU_VERSION  0x06
#define PKT_GAME_ID      0x10
#define PKT_VIDEO_FORMAT 0x11
#define PKT_COMMENT      0x12

// GPU status bits
#define GPUSTAT_FIFO_NOT_FULL 0x02000000  // bit 25, when GP1(04h) = 1
#define GPUSTAT_CMD_READY     0x04000000  // bit 26
#define GPUSTAT_READ_READY    0x08000000  // bit 27
#define GPUSTAT_FIFO_EMPTY    0x10000000  // bit 28
#define GPUSTAT_DMA_DIR       0x60000000  // bits 29-30

// GP0 parser states
enum GP0State {
    GP0_COMMAND,   // next word is a command word
    GP0_PARAMS,    // fixed-size parameters of the current command
    GP0_UPLOAD,    // pixel data of a CPU to VRAM transfer
    GP0_POLYLINE,  // vertices of a polyline, until the terminator
};

#define BUFFER_WORDS 256

// Public state
uint32_t GPD_GPUVersion = 0;
uint32_t GPD_Frame = 0;

// Private state
static GPD_ReadFn s_readFn = NULL;
static void* s_readCtx = NULL;
static void (*s_closeFn)() = NULL;
static uint32_t s_buffer[BUFFER_WORDS];
static unsigned s_bufferPos = 0;
static unsigned s_bufferSize = 0;

static enum GP0State s_gp0State = GP0_COMMAND;
static uint32_t s_gp0Command = 0;
static uint32_t s_gp0Remaining = 0;
static uint32_t s_polylineWords = 0;
static uint32_t s_polylineDMADir = 0;
static uint32_t s_pendingReads = 0;

static int readWord(uint32_t* word) {
    if (s_bufferPos >= s_bufferSize) {
        uint8_t* dst = (uint8_t*)s_buffer;
        int total = 0;
        while (total < (int)sizeof(s_buffer)) {
            int r = s_readFn(s_readCtx, dst + total, sizeof(s_buffer) - total);
            if (r <= 0) break;
            total += r;
        }
        s_bufferPos = 0;
        s_bufferSize = total >> 2;
        if (s_bufferSize == 0) return 0;
    }
    *word = s_buffer[s_bufferPos++];
    return 1;
}

static void waitStatus(uint32_t mask) {
    while ((GPU_STATUS & mask) != mask)
        ;
}

// Number of parameter words following a GP0 command word, for
// all commands but polylines and the CPU to VRAM pixel data.
static uint32_t gp0ParamCount(uint32_t command) {
    uint32_t op = command >> 24;
    switch (op >> 5) {
        case 0:  // misc
            return op == 0x02 ? 2 : 0;
        case 1: {  // polygons
            uint32_t vertices = (command & 0x08000000) ? 4 : 3;
            uint32_t perVertex = (command & 0x04000000) ? 2 : 1;
            uint32_t colors = (command & 0x10000000) ? vertices - 1 : 0;
            return vertices * perVertex + colors;
        }
        case 2:  // lines, non-poly only
            return (command & 0x10000000) ? 3 : 2;
        case 3: {  // rectangles
            uint32_t count = 1;
            if (command & 0x04000000) count++;
            if ((command & 0x18000000) == 0) count++;
            return count;
        }
        case 4:  // VRAM to VRAM
            return 3;
        case 5:  // CPU to VRAM, pixel data not included
        case 6:  // VRAM to CPU
            return 2;
        default:  // environment
            return 0;
    }
}

// Words of data for a VRAM transfer, given its size parameter.
static uint32_t transferWords(uint32_t size) {
    uint32_t w = (((size & 0xffff) - 1) & 0x3ff) + 1;
    uint32_t h = (((size >> 16) - 1) & 0x1ff) + 1;
    return (w * h + 1) >> 1;
}

static void endPolyline() {
    GPU_STATUS = 0x04000000 | s_polylineDMADir;
    s_gp0State = GP0_COMMAND;
}

// Pushes one GP0 word, tracking command boundaries to know which
// status bit to wait on. Command words wait until the GPU is idle,
// so the fixed-size parameters, at most 11 words, always fit in the
// 16 words FIFO. The pixel data of a CPU to VRAM transfer waits for
// the FIFO to be empty before each word. Polylines have no size
// limit, and the FIFO could overflow during one; they are sent in
// the FIFO state DMA mode, waiting for the FIFO to not be full,
// and the previous DMA mode is restored at the end.
static void pushGP0(uint32_t word) {
    switch (s_gp0State) {
        case GP0_COMMAND: {
            waitStatus(GPUSTAT_CMD_READY | GPUSTAT_FIFO_EMPTY);
            GPU_DATA = word;
            s_gp0Command = word;
            if ((word & 0xe8000000) == 0x48000000) {
                s_polylineDMADir = (GPU_STATUS & GPUSTAT_DMA_DIR) >> 29;
                GPU_STATUS = 0x04000001;
                s_polylineWords = 0;
                s_gp0State = GP0_POLYLINE;
                break;
            }
            s_gp0Remaining = gp0ParamCount(word);
            if (s_gp0Remaining != 0) s_gp0State = GP0_PARAMS;
            break;
        }
        case GP0_PARAMS:
            GPU_DATA = word;
            if (--s_gp0Remaining != 0) break;
            s_gp0State = GP0_COMMAND;
            switch (s_gp0Command >> 29) {
                case 5:
                    s_gp0Remaining = transferWords(word);
                    s_gp0State = GP0_UPLOAD;
                    break;
                case 6:
                    s_pendingReads = transferWords(word);
                    break;
            }
            break;
        case GP0_UPLOAD:
            waitStatus(GPUSTAT_FIFO_EMPTY);
            GPU_DATA = word;
            if (--s_gp0Remaining == 0) s_gp0State = GP0_COMMAND;
            break;
        case GP0_POLYLINE: {
            // The terminator can only appear after the first two
            // vertices, and only as the first word of a vertex, which
            // is the color word for gouraud polylines. Word 0 is the
            // first vertex, as the command word holds the first color.
            uint32_t index = s_polylineWords++;
            int canTerminate;
            if (s_gp0Command & 0x10000000) {
                canTerminate = (index >= 3) && (index & 1);
            } else {
                canTerminate = index >= 2;
            }
            int isTerminator = canTerminate && ((word & 0xf000f000) == 0x50005000);
            waitStatus(GPUSTAT_FIFO_NOT_FULL);
            GPU_DATA = word;
            if (isTerminator) endPolyline();
            break;
        }
    }
}

static void pushGP1(uint32_t word) {
    GPU_STATUS = word;
    uint32_t op = (word >> 24) & 0x3f;
    if (op <= 0x01) {
        // Reset and command buffer reset both abort any command in progress.
        s_gp0State = GP0_COMMAND;
        s_gp0Remaining = 0;
        s_pendingReads = 0;
    } else if ((op == 0x04) && (s_gp0State == GP0_POLYLINE)) {
        s_polylineDMADir = word & 3;
        GPU_STATUS = 0x04000001;
    }
}

// Reads and discards GPUREAD words. When they belong to a VRAM to
// CPU transfer, waits for the data to be ready before each word.
static void discardGPURead(uint32_t count) {
    while (count--) {
        if (s_pendingReads != 0) {
            waitStatus(GPUSTAT_READ_READY);
            s_pendingReads--;
        }
        (void)GPU_DATA;
    }
}

static int skipWords(uint32_t count) {
    uint32_t word;
    while (count--) {
        if (!readWord(&word)) return 0;
    }
    return 1;
}

int GPD_Check(const void* header16) {
    const uint8_t* header = (const uint8_t*)header16;
    for (unsigned i = 0; i < sizeof(s_magic); i++) {
        if (header[i] != s_magic[i]) return 0;
    }
    return 1;
}

int GPD_Init(GPD_ReadFn fn, void* ctx) {
    s_readFn = fn;
    s_readCtx = ctx;
    s_bufferPos = 0;
    s_bufferSize = 0;
    s_gp0State = GP0_COMMAND;
    s_gp0Remaining = 0;
    s_pendingReads = 0;
    GPD_GPUVersion = 0;
    GPD_Frame = 0;

    uint32_t header[4];
    for (unsigned i = 0; i < 4; i++) {
        if (!readWord(&header[i])) return 0;
    }
    return GPD_Check(header);
}

int GPD_PlayFrame() {
    uint32_t header;
    if (s_readFn == NULL) return 0;
    while (readWord(&header)) {
        uint32_t type = header >> 24;
        uint32_t length = header & 0xffffff;
        uint32_t word;
        switch (type) {
            case PKT_GP0:
                while (length--) {
                    if (!readWord(&word)) return 0;
                    pushGP0(word);
                }
                break;
            case PKT_GP1:
                while (length--) {
                    if (!readWord(&word)) return 0;
                    pushGP1(word);
                }
                break;
            case PKT_VSYNC:
                if (!skipWords(length)) return 0;
                GPD_Frame++;
                return 1;
            case PKT_THROW_AWAY:
            case PKT_READBACK:
                if (length == 0) break;
                if (!readWord(&word)) return 0;
                if (!skipWords(length - 1)) return 0;
                discardGPURead(word);
                break;
            case PKT_GPU_VERSION:
                if (length == 0) break;
                if (!readWord(&GPD_GPUVersion)) return 0;
                if (!skipWords(length - 1)) return 0;
                break;
            default:
                // Trace begin, strings, and unknown packets.
                if (!skipWords(length)) return 0;
                break;
        }
    }
    return 0;
}

// Memory source

struct MemoryReader {
    const uint8_t* data;
    uint32_t remaining;
};

static struct MemoryReader s_memoryReader;

static int memoryRead(void* ctx_, void* dst_, int bytes) {
    struct MemoryReader* ctx = (struct MemoryReader*)ctx_;
    uint8_t* dst = (uint8_t*)dst_;
    if ((uint32_t)bytes > ctx->remaining) bytes = ctx->remaining;
    __builtin_memcpy(dst, ctx->data, bytes);
    ctx->data += bytes;
    ctx->remaining -= bytes;
    return bytes;
}

int GPD_InitMemory(const void* data, uint32_t size) {
    GPD_Close();
    s_memoryReader.data = (const uint8_t*)data;
    s_memoryReader.remaining = size;
    return GPD_Init(memoryRead, &s_memoryReader);
}

// Shared file descriptor for the pcdrv and CD-ROM sources.
static int s_fd = -1;

// pcdrv source

static int pcdrvRead(void* ctx, void* dst, int bytes) { return PCread(s_fd, dst, bytes); }

static void pcdrvClose() { PCclose(s_fd); }

int GPD_InitPCdrv(const char* hostPath) {
    GPD_Close();
    if (PCinit() != 0) return 0;
    s_fd = PCopen(hostPath, 0, 0);
    if (s_fd < 0) return 0;
    s_closeFn = pcdrvClose;
    if (GPD_Init(pcdrvRead, NULL)) return 1;
    GPD_Close();
    return 0;
}

// CD-ROM source. The BIOS can only read whole sectors, so reads go
// through a sector buffer. Reading past the end of the file is fine
// for the BIOS, which clamps the returned size, but may fail if the
// file sits at the very end of the disc, so a failed multi-sectors
// read is retried one sector at a time.

#define CD_SECTOR_SIZE 2048
#define CD_BUFFER_SECTORS 8

static uint8_t s_cdBuffer[CD_SECTOR_SIZE * CD_BUFFER_SECTORS];
static int s_cdBufferPos = 0;
static int s_cdBufferSize = 0;
static int s_cdSectors = CD_BUFFER_SECTORS;
static int s_cdEOF = 0;

static int cdromRead(void* ctx, void* dst_, int bytes) {
    uint8_t* dst = (uint8_t*)dst_;
    if (s_cdBufferPos >= s_cdBufferSize) {
        if (s_cdEOF) return 0;
        int r = syscall_read(s_fd, s_cdBuffer, s_cdSectors * CD_SECTOR_SIZE);
        if ((r <= 0) && (s_cdSectors != 1)) {
            s_cdSectors = 1;
            r = syscall_read(s_fd, s_cdBuffer, CD_SECTOR_SIZE);
        }
        if (r <= 0) {
            s_cdEOF = 1;
            return 0;
        }
        if (r < s_cdSectors * CD_SECTOR_SIZE) s_cdEOF = 1;
        s_cdBufferPos = 0;
        s_cdBufferSize = r;
    }
    int available = s_cdBufferSize - s_cdBufferPos;
    if (bytes > available) bytes = available;
    __builtin_memcpy(dst, s_cdBuffer + s_cdBufferPos, bytes);
    s_cdBufferPos += bytes;
    return bytes;
}

static void cdromClose() { syscall_close(s_fd); }

int GPD_InitCDRom(const char* path) {
    GPD_Close();
    s_fd = syscall_open(path, 1);
    if (s_fd < 0) return 0;
    s_closeFn = cdromClose;
    s_cdBufferPos = 0;
    s_cdBufferSize = 0;
    s_cdSectors = CD_BUFFER_SECTORS;
    s_cdEOF = 0;
    if (GPD_Init(cdromRead, NULL)) return 1;
    GPD_Close();
    return 0;
}

void GPD_Close() {
    if (s_closeFn) s_closeFn();
    s_closeFn = NULL;
    s_fd = -1;
    s_readFn = NULL;
}
