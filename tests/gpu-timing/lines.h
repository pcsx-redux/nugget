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

// Line classes for the gpu-timing sweep. The harness owns timing, the
// baseline, the GPU state (E1h: blend mode, dither) and the drawn check; the
// builder below only turns a class and a size into GP0 words.
#define LINE_GOURAUD 1u  // GP0(50h) family instead of GP0(40h)
#define LINE_POLY 2u     // polyline (48h/58h) instead of a single line
#define LINE_SEMI 4u     // semi-transparent (bit 25 of the command word)
#define LINE_SLANTED 8u  // slanted segments instead of horizontal ones

#define LINE_MAX_SEGS 16u
#define LINE_MAX_WORDS (2u * (LINE_MAX_SEGS + 1u) + 1u)

// Writes one line command of class |kind| into |out| and returns the number
// of words written, polyline terminator included. Returns 0 for a kind it
// does not build.
//
// |len| is the length of each segment in pixels along its major axis,
// 1..255. |segs| is the segment count: 1 for a single line, 2..LINE_MAX_SEGS
// for a polyline.
//
// Geometry. A vertex word is (y << 16) | x.
//  - Without LINE_SLANTED every segment is horizontal: a single line runs from
//    (0, 0) to (len, 0), and a polyline bounces back and forth between (0, 0)
//    and (len, 0).
//  - With LINE_SLANTED every segment is x-major with a 2:1 slope: a single
//    line runs from (0, 0) to (len, len / 2), and a polyline bounces back and
//    forth between (0, 0) and (len, len / 2).
//
// Contract the harness relies on:
//  - the first vertex is (0, 0), so pixel (0, 0) is drawn;
//  - every vertex lies inside 0..255 on both axes;
//  - a polyline ends with the word 55555555h;
//  - the colour words are non-black, so a drawn pixel differs from the
//    cleared background;
//  - the return value counts the polyline terminator.
unsigned buildLineCommand(uint32_t *out, unsigned kind, unsigned len, unsigned segs);
