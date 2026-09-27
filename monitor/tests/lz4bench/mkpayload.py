#!/usr/bin/env python3
# Compress files into linked 8 KiB LZ4 blocks (each block may reference the
# previous 64 KiB of output), the shape a compressed WRITE_MEM would use.
# Output: payload.bin = per stream [u32 rawlen][u32 sum][u32 nblocks]
# then [u16 len][block] per block, padded to 4 bytes per stream.
import struct, sys
import lz4.block

CHUNK = 8192
out = bytearray(struct.pack('<I', len(sys.argv) - 2))
for path in sys.argv[2:]:
    raw = open(path, 'rb').read()
    blocks = []
    for i in range(0, len(raw), CHUNK):
        kw = dict(mode='high_compression', compression=12, store_size=False)
        dic = raw[max(0, i - 65536):i]
        blocks.append(lz4.block.compress(raw[i:i + CHUNK], dict=dic, **kw) if dic
                      else lz4.block.compress(raw[i:i + CHUNK], **kw))
    out += struct.pack('<III', len(raw), sum(raw) & 0xffffffff, len(blocks))
    for b in blocks:
        out += struct.pack('<H', len(b)) + b
    while len(out) % 4:
        out.append(0)
open(sys.argv[1], 'wb').write(out)
