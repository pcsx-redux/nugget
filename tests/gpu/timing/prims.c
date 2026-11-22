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

#include "prims.h"

#include <stdint.h>

#include "prim.h"

unsigned buildPoly(uint32_t* o, unsigned quad, unsigned g, unsigned tex, unsigned semi, unsigned x0, unsigned w,
                   unsigned h) {
    unsigned op = 0x20 | (g ? 0x10 : 0) | (quad ? 0x08 : 0) | (tex ? 0x04 : 0) | (semi ? 0x02 : 0);
    unsigned nv = quad ? 4 : 3;
    unsigned vx[4] = {x0, x0 + w, x0, x0 + w};
    unsigned vy[4] = {0, 0, h, h};
    unsigned n = 0;
    uint32_t c0 = (tex && !g) ? 0x808080 : s_colors[0];
    uint32_t tpage = tex ? (tpageX(tex) | (abr(semi) << 5) | (tdepth(tex) << 7)) : 0;
    o[n++] = (op << 24) | c0;
    for (unsigned k = 0; k < nv; k++) {
        if (k && g) o[n++] = s_colors[k];
        o[n++] = vtx(vx[k], vy[k]);
        if (tex) {
            uint32_t hi = k == 0 ? clutWord(tex) : k == 1 ? tpage : 0;
            o[n++] = uvw(vx[k] - x0, vy[k]) | (hi << 16);
        }
    }
    return n;
}

unsigned buildRect(uint32_t* o, unsigned size, unsigned tex, unsigned semi, unsigned raw, unsigned w, unsigned h) {
    unsigned code = size == 0 ? 0 : size == 1 ? 1 : size == 8 ? 2 : 3;
    unsigned op = 0x60 | (code << 3) | (tex ? 0x04 : 0) | (semi ? 0x02 : 0) | (raw ? 0x01 : 0);
    unsigned n = 0;
    o[n++] = (op << 24) | (tex ? 0x808080 : 0x50a020);
    o[n++] = vtx(0, 0);
    if (tex) o[n++] = clutWord(tex) << 16;
    if (!size) o[n++] = (h << 16) | w;
    return n;
}

unsigned buildLineCommand(uint32_t* out, unsigned kind, unsigned len, unsigned segs) {
    if (((kind & LINE_POLY) == 0) && (segs > 1)) {
        // Non-polylines cannot have more than one segment.
        return 0;
    }
    if (len > 255) {
        // Length cannot exceed the maximum allowed for a line.
        return 0;
    }

    int x = 0, y = 0;
    int major = len, minor = (kind & LINE_SLANTED) ? len / 2 : 0;
    int dx = (kind & LINE_VERTICAL) ? minor : major;
    int dy = (kind & LINE_VERTICAL) ? major : minor;
    const uint32_t* start = out;

    uint32_t first = 2 << 29;
    if (kind & LINE_GOURAUD) {
        first |= 1 << 28;  // Set the Gouraud shading bit
    }
    if (kind & LINE_POLY) {
        first |= 1 << 27;  // Set the polyline bit
    }
    if (kind & LINE_SEMI) {
        first |= 1 << 25;  // Set the semi-transparent bit
    }

    *out++ = first | s_colors[0];

    for (unsigned i = 0; i <= segs; i++) {
        if ((kind & LINE_GOURAUD) && i != 0) {
            *out++ = s_colors[i % 4];
        }
        *out++ = vtx(x, y);
        if (i % 2 == 0) {
            x += dx;
            y += dy;
        } else {
            x -= dx;
            y -= dy;
        }
    }

    if (kind & LINE_POLY) {
        *out++ = 0x55555555;
    }

    return out - start;
}
