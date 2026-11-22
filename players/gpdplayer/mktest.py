#!/usr/bin/env python3

# MIT License
#
# Copyright (c) 2026 PCSX-Redux authors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Generates test.gpd, a small synthetic GPUDUMP stream for the demo.
#
#   python3 mktest.py [output]
#
# The stream sets up a 320x240 NTSC 15bpp display, uploads a 64x64
# pattern off-screen, then plays 60 animated frames: a background fill,
# a flat quad sliding right, a gouraud triangle, a polyline, and a
# VRAM-to-VRAM copy of the pattern bouncing around. A VRAM readback is
# thrown in on two frames to exercise the 0x03 and 0x04 packets.

import math
import struct
import sys

FRAMES = 60

PKT_GP0 = 0x00
PKT_GP1 = 0x01
PKT_VSYNC = 0x02
PKT_THROW_AWAY = 0x03
PKT_READBACK = 0x04
PKT_TRACE_BEGIN = 0x05
PKT_GPU_VERSION = 0x06
PKT_GAME_ID = 0x10
PKT_VIDEO_FORMAT = 0x11
PKT_COMMENT = 0x12

out = bytearray(b"PSXGPUDUMPv1r1\0\0")


def packet(type, words):
    out.extend(struct.pack("<I", (type << 24) | len(words)))
    for w in words:
        out.extend(struct.pack("<I", w & 0xFFFFFFFF))


def string(type, s):
    b = s.encode("ascii") + b"\0"
    b += b"\0" * (-len(b) & 3)
    packet(type, list(struct.unpack("<%dI" % (len(b) // 4), b)))


def rgb(r, g, b):
    return r | (g << 8) | (b << 16)


def xy(x, y):
    return (x & 0xFFFF) | ((y & 0xFFFF) << 16)


def rgb15(r, g, b):
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


# Metadata.
string(PKT_COMMENT, "mktest.py")
string(PKT_VIDEO_FORMAT, "NTSC")
packet(PKT_GPU_VERSION, [2])

# GP1 state restore: reset, 320x240 NTSC 15bpp, standard ranges.
packet(PKT_GP1, [
    0x00000000,  # reset
    0x03000001,  # display off
    0x08000001,  # 320x240, NTSC, 15bpp, no interlace
    0x05000000,  # display area 0,0
    0x06000000 | 0x260 | ((0x260 + 320 * 8) << 12),
    0x07000000 | 16 | ((16 + 240) << 10),
])

# Synthetic VRAM upload: a 64x64 pattern at (512, 0). The data is split
# across two GP0 packets, to exercise the player's streaming state.
pixels = []
for y in range(64):
    for x in range(64):
        if ((x >> 3) ^ (y >> 3)) & 1:
            pixels.append(rgb15(255, 255, 255))
        else:
            pixels.append(rgb15(x * 4, y * 4, 128))
data = [pixels[i] | (pixels[i + 1] << 16) for i in range(0, len(pixels), 2)]
packet(PKT_GP0, [0xA0000000, xy(512, 0), xy(64, 64)] + data[:777])
packet(PKT_GP0, data[777:])

# Drawing environment.
packet(PKT_GP0, [
    0xE1000600,  # draw to display area allowed, dither
    0xE2000000,
    0xE3000000,  # drawing area top-left 0,0
    0xE4000000 | 319 | (239 << 10),
    0xE5000000,  # offset 0,0
    0xE6000000,
])

packet(PKT_TRACE_BEGIN, [])
packet(PKT_GP1, [0x03000000])  # display on

cycles = 0
for f in range(FRAMES):
    t = f / FRAMES
    words = []
    # Background fill, slowly shifting hue.
    words += [0x02000000 | rgb(16, 16 + f, 64 - f // 2), xy(0, 0), xy(320, 240)]
    # Flat red quad sliding left to right.
    qx = 10 + f * 4
    words += [0x28000000 | rgb(220, 40, 40),
              xy(qx, 20), xy(qx + 40, 20), xy(qx, 60), xy(qx + 40, 60)]
    # Gouraud triangle spinning around the center.
    cx, cy, r = 160, 140, 70
    tri = []
    for i, col in enumerate([rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255)]):
        a = 2 * math.pi * (t + i / 3)
        vx = int(cx + r * math.cos(a))
        vy = int(cy + r * math.sin(a))
        tri += [col, xy(vx, vy)]
    tri[0] |= 0x30000000
    words += tri
    # Flat polyline: a zigzag whose amplitude breathes.
    amp = int(20 * math.sin(2 * math.pi * t)) + 1
    words.append(0x48000000 | rgb(255, 255, 0))
    for i in range(12):
        words.append(xy(10 + i * 27, 215 + (amp if i & 1 else -amp)))
    words.append(0x55555555)
    # Copy the uploaded pattern to a position bouncing along a diagonal.
    px = 20 + int(200 * abs(math.sin(math.pi * t)))
    py = 70 + f
    words += [0x80000000, xy(512, 0), xy(px, py), xy(64, 64)]
    packet(PKT_GP0, words)

    if f == 10 or f == 20:
        # Read back an 8x2 area: 16 pixels, 8 words.
        packet(PKT_GP0, [0x01000000, 0xC0000000, xy(512, 0), xy(8, 2)])
        packet(PKT_THROW_AWAY if f == 10 else PKT_READBACK, [8])

    cycles += 564480
    packet(PKT_VSYNC, [cycles])

with open(sys.argv[1] if len(sys.argv) > 1 else "test.gpd", "wb") as f:
    f.write(out)
