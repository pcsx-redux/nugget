#!/usr/bin/env python3
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

header = subprocess.run([objcopy.rsplit("objcopy", 1)[0] + "readelf", "-h", elf_path],
                        capture_output=True, text=True, check=True).stdout
entry = int(re.search(r"Entry point address:\s+0x([0-9a-f]+)", header).group(1), 16)
if not CAVE_BASE <= entry < CAVE_BASE + len(cave):
    sys.exit(f"entry {entry:#x} is outside the cave; was this built with BOOT=cart MONITOR=1?")

img = bytearray(stock)
img[CAVE_BASE - FLASH_BASE:CAVE_BASE - FLASH_BASE + len(cave)] = cave
struct.pack_into("<I", img, off, (2 << 26) | ((entry >> 2) & 0x3FFFFFF))
open(out_path, "wb").write(img)
print(f"{out_path}: monitor {len(cave)} bytes, entry {entry:#x}")
