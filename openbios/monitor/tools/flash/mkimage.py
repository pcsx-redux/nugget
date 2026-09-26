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

# Builds the 512 KB H2700 flash image the flasher programs: the stock H2700
# flash with the monitor dropped into the 128 KB code cave at 0xbfc40000, and
# the stock entry jump at 0xbfc00414 redirected to the monitor's hook.
#
# usage: mkimage.py STOCK OPENBIOS_ELF OUT [OBJCOPY]
# OPENBIOS_ELF is openbios/openbios.elf from `make BOOT=cart MONITOR=1`.

import re
import struct
import subprocess
import sys
import tempfile

FLASH_BASE = 0xBFC00000
CAVE_BASE = 0xBFC40000
CAVE_SIZE = 0x20000
ENTRY_JUMP = 0xBFC00414
STOCK_JUMP = 0x0BF01C60  # j 0xbfc07180

stock_path, elf_path, out_path = sys.argv[1:4]
objcopy = sys.argv[4] if len(sys.argv) > 4 else "mipsel-none-elf-objcopy"

stock = open(stock_path, "rb").read()
if len(stock) != 0x80000:
    sys.exit(f"{stock_path}: expected a 512 KB flash image, got {len(stock)} bytes")
off = ENTRY_JUMP - FLASH_BASE
if struct.unpack_from("<I", stock, off)[0] != STOCK_JUMP:
    sys.exit(f"{stock_path}: no stock entry jump at {ENTRY_JUMP:#x}, not an H2700 flash image")

with tempfile.NamedTemporaryFile() as tmp:
    subprocess.run([objcopy, "-O", "binary", elf_path, tmp.name], check=True)
    cave = open(tmp.name, "rb").read()
if len(cave) > CAVE_SIZE:
    sys.exit(f"monitor is {len(cave)} bytes, the cave holds {CAVE_SIZE}")

readelf = objcopy.rsplit("objcopy", 1)[0] + "readelf"
header = subprocess.run([readelf, "-hlW", elf_path], capture_output=True, text=True, check=True).stdout
entry = int(re.search(r"Entry point address:\s+0x([0-9a-f]+)", header).group(1), 16)

# objcopy -O binary starts at the lowest load address with contents; that has
# to be the cave, and the segments have to account for every byte it wrote.
loads = [(int(p, 16), int(s, 16)) for p, s in
         re.findall(r"^\s*LOAD\s+\S+\s+\S+\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", header, re.M)]
loads = [(p, s) for p, s in loads if s]
lo = min(p for p, s in loads)
hi = max(p + s for p, s in loads)
if lo != CAVE_BASE or hi - lo != len(cave):
    sys.exit(f"monitor loads at {lo:#x}..{hi:#x}, expected {CAVE_BASE:#x} and {len(cave)} bytes")
if not CAVE_BASE <= entry < CAVE_BASE + len(cave):
    sys.exit(f"entry {entry:#x} is outside the cave; was this built with BOOT=cart MONITOR=1?")

img = bytearray(stock)
img[CAVE_BASE - FLASH_BASE:CAVE_BASE - FLASH_BASE + len(cave)] = cave
struct.pack_into("<I", img, off, (2 << 26) | ((entry >> 2) & 0x3FFFFFF))
open(out_path, "wb").write(img)
print(f"{out_path}: monitor {len(cave)} bytes, entry {entry:#x}")
