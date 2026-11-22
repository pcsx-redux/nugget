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

// Player for GPUDUMP streams, as described in
// https://github.com/ps1dev/standards/blob/main/GPUDUMP.md
// The stream is consumed through a reader
// callback, so it can come from memory, from the host through
// pcdrv, or from the CD-ROM through the BIOS file API, and is
// never required to fit in memory: packets are streamed through
// a small internal buffer, whatever their size.

// Reads up to `bytes` bytes into `dst`. Returns the number of bytes
// read, which may be less than requested, or 0 or a negative
// value at the end of the stream or on error.
typedef int (*GPD_ReadFn)(void* ctx, void* dst, int bytes);

// GPU version from the stream's 0x06 packet, or 0 if none was seen.
// 1 = GPU v1 with 1MB of VRAM, 2 = GPU v2 with 1MB, 3 = GPU v2 with 2MB.
extern uint32_t GPD_GPUVersion;

// Number of vsync packets consumed so far.
extern uint32_t GPD_Frame;

// Validates the 16 bytes magic header of a GPUDUMP stream.
// Returns 1 if valid, 0 if not.
int GPD_Check(const void* header16);

// Starts playing a stream from an arbitrary reader. Reads the magic
// header and validates it. Returns 1 on success, 0 if the stream is
// invalid. The ctx pointer is passed as-is to the reader.
int GPD_Init(GPD_ReadFn fn, void* ctx);

// Starts playing a stream stored in memory. The data needs to stay
// valid for the duration of the playback.
int GPD_InitMemory(const void* data, uint32_t size);

// Starts playing a stream from a file on the host, through pcdrv.
// Returns 0 if pcdrv is unavailable, the file can't be opened, or
// the stream is invalid.
int GPD_InitPCdrv(const char* hostPath);

// Starts playing a stream from a file on the CD-ROM, using the
// BIOS file API. The path is a BIOS path, for instance
// "cdrom:\\FILE.GPD;1". Returns 0 on error. The BIOS CD-ROM code
// needs interrupts, so this call and the subsequent GPD_PlayFrame
// calls must be done outside of a critical section.
int GPD_InitCDRom(const char* path);

// Closes the current stream, if it was opened by GPD_InitPCdrv
// or GPD_InitCDRom. Safe to call at any time.
void GPD_Close();

// Pushes packets to the GPU until the next vsync packet, or the end
// of the stream. The first call goes through the state restoration
// part of the stream, up to and past the trace begin packet. The
// caller is expected to wait for a vblank between calls.
// Returns 1 if a vsync packet was reached, 0 at the end of the
// stream, or if the stream is truncated.
int GPD_PlayFrame();
