#!/usr/bin/env python3
"""make-demo-song.py OUT.xm: write the source of the demo song (song.xmp and song.smp are this, converted
with xmconv). Everything in it, samples included, is generated
here, so the song has no third-party content. Five channels, six instruments, linear table."""
import math, struct, sys

def note(name):  # "A-3" -> XM note number, C-0 = 1
    names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    return names.index(name[:2]) + 12 * int(name[2]) + 1

def lcg(seed):
    while True:
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        yield (seed >> 16) / 32768.0 - 1.0

def samples():
    saw = [int(100 * (2 * i / 32 - 1)) for i in range(32)]                          # C-4 = 261 Hz
    square = [80 if i < 16 else -80 for i in range(32)]
    ramp = [int(-110 + 220 * i / 32) for i in range(33)]                             # ping-pong -> triangle, 64
    kick = [int(120 * math.exp(-i / 900) * math.sin(2 * math.pi * (55 * i / 8363 + 140 * (1 - math.exp(-i / 300)) * 300 / 8363)))
            for i in range(3500)]
    n = lcg(7); snare = [int(110 * math.exp(-i / 500) * (0.7 * next(n) + 0.3 * math.sin(2 * math.pi * 190 * i / 8363))) for i in range(2600)]
    n = lcg(11); hat = [int(70 * math.exp(-i / 90) * next(n)) for i in range(700)]
    return saw, square, ramp, kick, snare, hat

def instrument(name, pcm, loop=0, loopstart=0, relnote=0, vol=64, volenv=None, sustain=None, panenv=None, fade=0,
               vib=(0, 0, 0, 0)):
    pts = lambda e: b"".join(struct.pack("<HH", *p) for p in (e or [])) + bytes(4 * (12 - len(e or [])))
    vtype = (1 if volenv else 0) | (2 if sustain is not None else 0); ptype = 1 if panenv else 0
    h = struct.pack("<I", 263) + name.encode().ljust(22, b"\0") + b"\0" + struct.pack("<HI", 1, 40) + bytes(96)
    h += pts(volenv) + pts(panenv)
    h += bytes([len(volenv or []), len(panenv or []), sustain or 0, 0, 0, 0, 0, 0, vtype, ptype, *vib])
    h += struct.pack("<H", fade) + bytes(22)
    assert len(h) == 263
    looplen = len(pcm) - loopstart if loop else 0
    h += struct.pack("<IIIBbBBbB", len(pcm), loopstart if loop else 0, looplen, vol, 0, loop, 128, relnote, 0)
    h += name.encode().ljust(22, b"\0")
    prev = 0; d = bytearray()
    for v in pcm: d.append((v - prev) & 255); prev = v
    return h + bytes(d)

def pattern(rows, channels, cells):
    # SOLO=<channel> keeps only that channel's cells, to measure one part of the song at a time
    import os
    if os.environ.get("SOLO"): cells = {k: v for k, v in cells.items() if k[1] == int(os.environ["SOLO"])}
    out = bytearray()
    for r in range(rows):
        for c in range(channels):
            cell = cells.get((r, c))
            if not cell: out.append(0x80); continue
            n, i, v, f, p = cell
            mask = (1 if n else 0) | (2 if i else 0) | (4 if v else 0) | (8 if f else 0) | (16 if p else 0)
            out.append(0x80 | mask); out.extend(x for x in (n, i, v, f, p) if x)
    return struct.pack("<IBHH", 9, 0, rows, len(out)) + bytes(out)

KICK, SNARE, HAT, BASS, PAD, LEAD = 0, 1, 1, 2, 3, 4
def beat(cells, variant):
    for r in range(0, 32, 8): cells[(r, KICK)] = (note("C-4"), 4, 0, 0, 0)
    if variant: cells[(14, KICK)] = (note("C-4"), 4, 0x30, 0, 0); cells[(30, KICK)] = (note("C-4"), 4, 0x30, 0, 0)
    for r in range(0, 32, 2):
        if r % 8 == 4: cells[(r, SNARE)] = (note("C-4"), 5, 0, 0, 0)
        else: cells[(r, HAT)] = (note("C-5"), 6, 0x10 + (0x28 if r % 4 == 0 else 0x18), 0, 0)

def chord_bar(cells, row, root, arp, melody):
    cells[(row, BASS)] = (note(root[:2] + "2"), 1, 0, 0, 0)
    cells[(row + 6, BASS)] = (note(root[:2] + "3"), 1, 0x28, 0, 0)
    cells[(row + 8, BASS)] = (note(root[:2] + "2"), 1, 0, 0, 0)
    cells[(row + 12, BASS)] = (97, 0, 0, 0, 0)
    cells[(row, PAD)] = (note(root[:2] + "4"), 2, 0x30, 0, arp)
    for r in range(row + 1, row + 16): cells[(r, PAD)] = (0, 0, 0, 0, arp)
    for k, m in enumerate(melody):
        if m: cells[(row + 4 * k, LEAD)] = (97, 0, 0, 0, 0) if m == "off" else (note(m), 3, 0, 0, 0)

def main(out):
    saw, square, ramp, kick, snare, hat = samples()
    ins = [instrument("bass", saw, loop=1, relnote=0, volenv=[(0, 64), (6, 44), (30, 36)], sustain=2, fade=1500),
           instrument("pad", ramp, loop=2, relnote=12, vol=40, volenv=[(0, 0), (12, 64), (40, 52)], sustain=2,
                      panenv=[(0, 20), (64, 44), (128, 20)], fade=600),
           instrument("lead", square, loop=1, vol=44, volenv=[(0, 64), (8, 52)], sustain=1, fade=900, vib=(0, 24, 3, 8)),
           instrument("kick", kick), instrument("snare", snare, vol=48), instrument("hat", hat, vol=40)]
    pats = []
    for variant, bars in ((0, [("A-", 0x37, ["A-5", "C-6", "E-6", "C-6"]), ("F-", 0x47, ["A-5", "off", "F-5", "A-5"])]),
                          (0, [("C-", 0x47, ["G-5", "E-5", "G-5", "C-6"]), ("G-", 0x47, ["B-5", "D-6", "B-5", "G-5"])]),
                          (1, [("A-", 0x37, ["E-6", "D-6", "C-6", "B-5"]), ("F-", 0x47, ["C-6", "A-5", "F-5", "off"])]),
                          (1, [("C-", 0x47, ["E-5", "G-5", "C-6", "E-6"]), ("G-", 0x47, ["D-6", "B-5", "G-5", "off"])])):
        cells = {}; beat(cells, variant)
        for k, (root, arp, mel) in enumerate(bars): chord_bar(cells, 16 * k, root, arp, mel)
        pats.append(pattern(32, 5, cells))
    orders = [0, 1, 2, 3, 0, 1, 2, 3]
    h = b"Extended Module: " + b"xmplayer demo".ljust(20, b"\0") + b"\x1a" + b"mkdemo".ljust(20, b"\0")
    h += struct.pack("<HIHHHHHHHH", 0x0104, 276, len(orders), 0, 5, len(pats), len(ins), 1, 6, 125)
    h += bytes(orders).ljust(256, b"\0")
    open(out, "wb").write(h + b"".join(pats) + b"".join(ins))

if __name__ == "__main__":
    main(sys.argv[1])
