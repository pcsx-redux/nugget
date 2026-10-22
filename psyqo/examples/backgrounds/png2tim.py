#!/usr/bin/env python3

# Converts a PNG into a PlayStation 1 TIM file, ready to be DMA'd straight into
# VRAM. This is an offline, one-shot asset conversion: run it by hand when the
# art changes, check the .tim in, and never think about it again.
#
# The 8bpp path is deliberately lossless. If the PNG is already palettised its
# palette and its indices are copied through byte for byte, because requantising
# art someone already tuned by hand is a silent downgrade. Truecolour input has
# to opt into quantisation explicitly.
#
# Usage:
#   png2tim.py input.png output.tim [--bpp 8] [--vram-x 0] [--vram-y 0]
#                                   [--clut-x 0] [--clut-y 0]
#                                   [--quantise pngquant]

import argparse
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image

TIM_ID = 0x10
TIM_VERSION = 0x00

# Flags bit0-2, the pixel storage mode.
TYPE_4BPP = 0
TYPE_8BPP = 1
TYPE_16BPP = 2
TYPE_24BPP = 3

FLAG_HAS_CLUT = 1 << 3

# Both the CLUT block and the pixel block carry the same 12-byte preamble.
SECTION_HEADER_SIZE = 12

CLUT_ENTRIES = 256

# The GPU addresses texels with 8-bit U and V registers, so anything past 256
# in either direction is simply unreachable by a textured primitive.
MAX_DIMENSION = 256


def to_rgb555(r, g, b):
    # 8:8:8 down to 5:5:5. A value of zero means "fully transparent" on the
    # PS1, so opaque black has to be nudged up to the darkest non-zero color.
    v = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)
    if v == 0:
        v = 0x0421
    return v


def pack_section(data, vram_x, vram_y, halfwords, rows):
    # The size field counts itself, and both the width field and the VRAM X
    # coordinate are measured in 16-bit halfwords rather than in texels. That
    # distinction is the single easiest thing to get wrong in this format: a
    # 256-texel-wide 8bpp image is 128 halfwords across.
    size = halfwords * 2 * rows + SECTION_HEADER_SIZE
    if size != len(data) + SECTION_HEADER_SIZE:
        raise SystemExit(
            f"internal error: section is {len(data)} bytes but the header claims "
            f"{size - SECTION_HEADER_SIZE}"
        )
    header = struct.pack(
        "<III",
        size,
        (vram_y << 16) | vram_x,
        (rows << 16) | halfwords,
    )
    return header + data, size


def run_pngquant(path):
    handle, tmp = tempfile.mkstemp(suffix=".png")
    os.close(handle)
    command = [
        "pngquant",
        "--quality",
        "0-100",
        "--speed",
        "1",
        "--nofs",
        "256",
        "--force",
        "--output",
        tmp,
        path,
    ]
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError:
        os.unlink(tmp)
        raise SystemExit("pngquant is not installed or not on PATH")
    except subprocess.CalledProcessError as e:
        os.unlink(tmp)
        raise SystemExit(f"pngquant failed with exit code {e.returncode}")
    image = Image.open(tmp)
    image.load()
    os.unlink(tmp)
    return image


def load_palettised(path, quantise):
    image = Image.open(path)
    if image.mode == "P":
        return image
    if quantise != "pngquant":
        raise SystemExit(
            f"{path} is {image.mode}, not palettised, and 8bpp needs a 256-entry "
            "palette. Reducing a truecolour image to 256 colours is lossy and "
            "throws away detail the artist put there, so it is never done "
            "silently: pass --quantise pngquant to opt in, or palettise the "
            "source yourself and re-run."
        )
    print(f"  note: {image.mode} source, quantising with pngquant")
    return run_pngquant(path)


def transparent_indices(image):
    # A tRNS chunk marks palette entries as see-through. Only fully transparent
    # entries (alpha 0) are allowed to stay 0x0000; every other entry, partial
    # alpha included, is opaque and gets nudged off zero even if the art really
    # is pure black.
    info = image.info.get("transparency")
    if info is None:
        return set()
    if isinstance(info, int):
        return {info}
    return {i for i, alpha in enumerate(info) if alpha == 0}


def build_8bpp(path, quantise):
    image = load_palettised(path, quantise)
    width, height = image.size
    if width % 2:
        raise SystemExit(f"{path}: 8bpp needs an even width, got {width}")

    palette = image.getpalette() or []
    entries = min(CLUT_ENTRIES, len(palette) // 3)
    seethrough = transparent_indices(image)

    clut = [0] * CLUT_ENTRIES
    for i in range(entries):
        if i in seethrough:
            continue
        clut[i] = to_rgb555(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2])

    # Two 8-bit indices per halfword, low byte first, which for a little-endian
    # write is just the index bytes in source order.
    texels = image.tobytes()
    return texels, clut, entries, width, height


def build_16bpp(path):
    image = Image.open(path).convert("RGBA")
    width, height = image.size
    pixels = image.tobytes()
    out = bytearray()
    for i in range(0, len(pixels), 4):
        r, g, b, a = pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]
        out += struct.pack("<H", 0 if a == 0 else to_rgb555(r, g, b))
    return bytes(out), width, height


def convert(args):
    print(f"{os.path.basename(args.input)}:")

    probe = Image.open(args.input)
    if probe.width > MAX_DIMENSION or probe.height > MAX_DIMENSION:
        raise SystemExit(
            f"{args.input}: {probe.width}x{probe.height} exceeds {MAX_DIMENSION} in at "
            f"least one direction, and the GPU's 8-bit UV registers cannot address "
            f"past {MAX_DIMENSION}"
        )

    blocks = []
    if args.bpp == 8:
        texels, clut, entries, width, height = build_8bpp(args.input, args.quantise)
        flags = TYPE_8BPP | FLAG_HAS_CLUT
        clut_block, clut_size = pack_section(
            struct.pack(f"<{CLUT_ENTRIES}H", *clut),
            args.clut_x,
            args.clut_y,
            CLUT_ENTRIES,
            1,
        )
        blocks.append(clut_block)
        halfwords = width // 2
    else:
        texels, width, height = build_16bpp(args.input)
        flags = TYPE_16BPP
        clut = None
        clut_size = 0
        entries = 0
        halfwords = width

    pixel_block, pixel_size = pack_section(
        texels, args.vram_x, args.vram_y, halfwords, height
    )
    blocks.append(pixel_block)

    blob = struct.pack("<BBHI", TIM_ID, TIM_VERSION, 0, flags) + b"".join(blocks)

    with open(args.output, "wb") as f:
        f.write(blob)

    print(f"  {width}x{height}, {args.bpp}bpp")
    if clut is not None:
        used = sum(1 for v in clut if v != 0)
        print(
            f"  CLUT: {entries} source entries padded to {CLUT_ENTRIES} "
            f"({used} non-zero), section {clut_size} bytes at VRAM "
            f"({args.clut_x},{args.clut_y})"
        )
    print(
        f"  pixels: {halfwords} halfwords x {height} rows, section {pixel_size} bytes "
        f"at VRAM ({args.vram_x},{args.vram_y})"
    )
    print(f"  wrote {args.output}: {len(blob)} bytes")


def main():
    parser = argparse.ArgumentParser(description="Convert a PNG into a PS1 TIM file")
    parser.add_argument("input", help="input PNG file")
    parser.add_argument("output", help="output TIM file")
    parser.add_argument(
        "--bpp",
        type=int,
        choices=(8, 16),
        default=8,
        help="pixel storage mode: 8 for palettised, 16 for direct RGB555 (default 8)",
    )
    parser.add_argument(
        "--vram-x", type=int, default=0, help="destination VRAM X, in halfwords"
    )
    parser.add_argument("--vram-y", type=int, default=0, help="destination VRAM Y")
    parser.add_argument("--clut-x", type=int, default=0, help="CLUT VRAM X, in halfwords")
    parser.add_argument("--clut-y", type=int, default=0, help="CLUT VRAM Y")
    parser.add_argument(
        "--quantise",
        choices=("pngquant",),
        help="reduce a truecolour source to 256 colours; lossy, so it must be asked for",
    )
    args = parser.parse_args()

    if args.bpp == 16 and args.quantise:
        print("note: --quantise is meaningless at 16bpp, ignoring", file=sys.stderr)

    convert(args)


if __name__ == "__main__":
    main()
