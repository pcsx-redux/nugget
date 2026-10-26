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

#include "lines.h"
#include "prim.h"

static uint32_t colors[4] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff};

unsigned buildLineCommand(uint32_t *out, unsigned kind, unsigned len,
                          unsigned segs) {
  if (((kind & LINE_POLY) == 0) && (segs > 1)) {
    // Non-polylines cannot have more than one segment.
    return 0;
  }
  if (len > 255) {
    // Length cannot exceed the maximum allowed for a line.
    return 0;
  }

  int x = 0, y = 0;
  const uint32_t *start = out;

  uint32_t first = 2 << 29;
  if (kind & LINE_GOURAUD) {
    first |= 1 << 28; // Set the Gouraud shading bit
  }
  if (kind & LINE_POLY) {
    first |= 1 << 27; // Set the polyline bit
  }
  if (kind & LINE_SEMI) {
    first |= 1 << 25; // Set the semi-transparent bit
  }

  *out++ = first | colors[0];

  for (unsigned i = 0; i <= segs; i++) {
    if ((kind & LINE_GOURAUD) && i != 0) {
      *out++ = colors[i % 4];
    }
    *out++ = vtx(x, y);
    switch (i % 2) {
    case 0: {
      x += len;
      if (kind & LINE_SLANTED) {
        y += len / 2;
      }
    } break;
    case 1: {
      x -= len;
      if (kind & LINE_SLANTED) {
        y -= len / 2;
      }
    } break;
    }
  }

  if (kind & LINE_POLY) {
    *out++ = 0x55555555;
  }

  return out - start;
}
