/*

MIT License

Copyright (c) 2024 PCSX-Redux authors

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

#include "psyqo/hardware/sio.hh"

psyqo::Hardware::Register<0x0040, uint8_t, psyqo::Hardware::WriteQueue::Bypass> psyqo::Hardware::SIO::Data;
psyqo::Hardware::Register<0x0044, uint32_t, psyqo::Hardware::WriteQueue::Bypass> psyqo::Hardware::SIO::Stat;
psyqo::Hardware::Register<0x0048, uint16_t, psyqo::Hardware::WriteQueue::Bypass> psyqo::Hardware::SIO::Mode;
psyqo::Hardware::Register<0x004a, uint16_t, psyqo::Hardware::WriteQueue::Bypass> psyqo::Hardware::SIO::Ctrl;
psyqo::Hardware::Register<0x004e, uint16_t, psyqo::Hardware::WriteQueue::Bypass> psyqo::Hardware::SIO::Baud;