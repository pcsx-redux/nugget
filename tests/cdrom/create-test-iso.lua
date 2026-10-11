--   Copyright (C) 2022 PCSX-Redux authors
--
--   This program is free software; you can redistribute it and/or modify
--   it under the terms of the GNU General Public License as published by
--   the Free Software Foundation; either version 2 of the License, or
--   (at your option) any later version.
--
--   This program is distributed in the hope that it will be useful,
--   but WITHOUT ANY WARRANTY; without even the implied warranty of
--   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--   GNU General Public License for more details.
--
--   You should have received a copy of the GNU General Public License
--   along with this program; if not, write to the
--   Free Software Foundation, Inc.,
--   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

-- This script creates a test ISO image for the CDROM unit tests.
--
-- The data track boots the retail monitor (monitor/hosts/retail), so a
-- console that boots burned discs can load the tests over SIO1. BOOT_EXE
-- overrides the boot executable. If an iso is mounted, its license sectors
-- are copied; otherwise the disc carries none.

local ffi = require 'ffi'
local bit = require 'bit'
local scriptDir = debug.getinfo(1, 'S').source:match '^@(.*[/\\])' or './'
local bootPath = os.getenv 'BOOT_EXE' or (scriptDir .. '../../monitor/hosts/retail/monitor-retail.ps-exe')
local bootFile = Support.File.open(bootPath)
if bootFile:failed() then error('cannot open boot executable ' .. bootPath) end
local licenseFile
local disc = PCSX.getCurrentIso()
if disc and not disc:failed() then licenseFile = disc:open(0, 2352 * 16, 'RAW') end
local iso = PCSX.isoBuilder(Support.File.open('test.bin', 'TRUNCATE'))
iso:writeLicense(licenseFile)

local b = Support.NewLuaBuffer(2352)
ffi.fill(b.data, 2352)
b:resize(2048)

local pvd = Support.File.buffer()
pvd:writeAt(b, 0)
pvd:writeU8At(1, 0)
pvd:writeAt('CD001', 1)
pvd:writeU8At(1, 6)
pvd:writeU32At(1, 132)
pvd:writeU32At(17, 140)
pvd:writeU32At(18, 158)

local pt = Support.File.buffer()
pt:writeAt(b, 0)
pt:writeU8At(1, 0)
pt:writeU8At(18, 2)
pt:writeU8At(1, 6)

local root = Support.File.buffer()
root:writeAt(b, 0)
root:writeU8At(42, 0)
root:writeU32At(19, 2)
root:writeU32At(bootFile:size(), 10)
root:writeU8At(9, 32)
root:writeAt('PSX.EXE;1', 33)

pvd:read(b)
iso:writeSector(b:cast 'uint8_t *', 2048)
pt:read(b)
iso:writeSector(b:cast 'uint8_t *', 2048)
root:read(b)
iso:writeSector(b:cast 'uint8_t *', 2048)

local count = 19
while not bootFile:eof() do
    ffi.fill(b.data, 2048)
    bootFile:read(b)
    iso:writeSector(b:cast 'uint8_t *', 2048)
    count = count + 1
end

ffi.fill(b.data, 2048)
for i = count, 30 * 60 * 75 - 1 do
    b[0] = bit.band(i, 0xff)
    b[1] = bit.band(bit.rshift(i, 8), 0xff)
    b[2] = bit.band(bit.rshift(i, 16), 0xff)
    iso:writeSector(b:cast 'uint8_t *', 2048)
end

local function generateToneSample(frequency, sampleRate, t)
    return math.sin(2 * math.pi * frequency * t / sampleRate)
end

-- XA region, LBA 135000 to 179999. Strides are the ones a game would use at
-- double speed. Every sector not listed below is a Form 1 data sector
-- carrying its LBA in its first 3 bytes, like the rest of the data track.
-- ADPCM sectors carry their LBA and 'X' in the first 4 bytes of the unused
-- 0x14-byte tail, so a raw read can tell which one it got. The last sector of
-- every stream has EOR and EOF set, unless the stream says otherwise.
--
--   LBA     sectors  stride  file ch  coding  tone Hz      what
--   135000  3200     16      1   0-7  0x00    300+100*ch   8 channels, slots 0-7 of each 16
--   138216  3200     16      1   0    0x00    1000         two files on the same channel,
--                    16      2   0    0x00    1500           file 2 in slot 8
--   141432  1600     8       1   8    0x01    440 / 660    stereo 37.8 kHz 4-bit
--   143048  3200     32      1   9    0x04    500          mono 18.9 kHz 4-bit
--   146264  1600     16      1   10   0x05    440 / 660    stereo 18.9 kHz 4-bit
--   147880  1600     8       1   11   0x10    700          mono 37.8 kHz 8-bit
--   149496  800      4       1   12   0x11    440 / 660    stereo 37.8 kHz 8-bit
--   150312  1600     16      1   13   0x14    800          mono 18.9 kHz 8-bit
--   151928  800      8       1   14   0x15    440 / 660    stereo 18.9 kHz 8-bit
--   152744  3200     16      1   0    0x00    1200         slot 0: audio, submode 0x64
--                    16      1   1    0x00    1300         slot 8: audio without RT, 0x24
--                    16      1   0    -       -            slot 4: Form 2 data, 0x28
--                    16      1   ff   0x00    1400         slot 12: audio on channel 0xff
--   155960  800      16      1   3    0x00    900          EOF alone on audio sector 24,
--                                                          EOR alone on 34, both on 49
-- Stereo streams put the first frequency on the left and the second on the right.

local xaStart = 30 * 60 * 75
local xaEnd = 40 * 60 * 75
local plan = {}

local function newStream(file, channel, coding, freqL, freqR)
    local stream = {
        file = file,
        channel = channel,
        coding = coding,
        stereo = bit.band(coding, 0x01) ~= 0,
        rate = bit.band(coding, 0x04) ~= 0 and 18900 or 37800,
        mode = bit.band(coding, 0x10) ~= 0 and 'XAEightBits' or 'XAFourBits',
        freqL = freqL,
        freqR = freqR or freqL,
        t = 0,
        encoder = PCSX.Adpcm.NewEncoder(),
    }
    stream.encoder:reset 'XA'
    return stream
end

local function place(lba, entry)
    if lba < xaStart or lba >= xaEnd then error('XA plan outside its region: ' .. lba) end
    if plan[lba] then error('XA plan collision at ' .. lba) end
    plan[lba] = entry
end

-- Places `count` audio sectors of `stream` from `lba`, `stride` apart.
-- Returns the LBA just past the last group.
local function placeStream(stream, lba, stride, count, submode, overrides)
    for i = 0, count - 1 do
        local sm = submode or 0x64
        if overrides and overrides[i] then
            sm = bit.bor(sm, overrides[i])
        elseif not overrides and i == count - 1 then
            sm = bit.bor(sm, 0x81)
        end
        place(lba + i * stride, { stream = stream, submode = sm })
    end
    return lba + count * stride
end

local lba = xaStart
local gap = 16
local groups = 200

for ch = 0, 7 do
    placeStream(newStream(1, ch, 0x00, 300 + 100 * ch), lba + ch, 16, groups)
end
lba = lba + 16 * groups + gap

placeStream(newStream(1, 0, 0x00, 1000), lba, 16, groups)
placeStream(newStream(2, 0, 0x00, 1500), lba + 8, 16, groups)
lba = lba + 16 * groups + gap

local formats = {
    { ch = 8, coding = 0x01, stride = 8, count = 200, f = { 440, 660 } },
    { ch = 9, coding = 0x04, stride = 32, count = 100, f = { 500 } },
    { ch = 10, coding = 0x05, stride = 16, count = 100, f = { 440, 660 } },
    { ch = 11, coding = 0x10, stride = 8, count = 200, f = { 700 } },
    { ch = 12, coding = 0x11, stride = 4, count = 200, f = { 440, 660 } },
    { ch = 13, coding = 0x14, stride = 16, count = 100, f = { 800 } },
    { ch = 14, coding = 0x15, stride = 8, count = 100, f = { 440, 660 } },
}
for _, f in ipairs(formats) do
    lba = placeStream(newStream(1, f.ch, f.coding, f.f[1], f.f[2]), lba, f.stride, f.count) + gap
end

placeStream(newStream(1, 0, 0x00, 1200), lba, 16, groups)
placeStream(newStream(1, 1, 0x00, 1300), lba + 8, 16, groups, 0x24)
placeStream(newStream(1, 0xff, 0x00, 1400), lba + 12, 16, groups)
for i = 0, groups - 1 do
    place(lba + 4 + i * 16, { form2data = true })
end
lba = lba + 16 * groups + gap

placeStream(newStream(1, 3, 0x00, 900), lba, 16, 50, nil, { [24] = 0x80, [34] = 0x01, [49] = 0x81 })

local xa = Support.NewLuaBuffer(2336)
local samples = ffi.new('int16_t[?]', 224 * 18)

local function fillSubheader(file, channel, submode, coding)
    xa[0], xa[1], xa[2], xa[3] = file, channel, submode, coding
    xa[4], xa[5], xa[6], xa[7] = file, channel, submode, coding
end

local function stampLBA(offset, i, marker)
    xa[offset + 0] = bit.band(i, 0xff)
    xa[offset + 1] = bit.band(bit.rshift(i, 8), 0xff)
    xa[offset + 2] = bit.band(bit.rshift(i, 16), 0xff)
    xa[offset + 3] = string.byte(marker)
end

local function writeAudioSector(i, entry)
    local s = entry.stream
    ffi.fill(xa.data, 2336)
    fillSubheader(s.file, s.channel, entry.submode, s.coding)
    -- One XA block holds 224 16-bit values for 4-bit, 112 for 8-bit;
    -- stereo interleaves left and right within those.
    local perBlock = s.mode == 'XAFourBits' and 224 or 112
    local frames = s.stereo and perBlock / 2 or perBlock
    for n = 0, 18 * frames - 1 do
        if s.stereo then
            samples[n * 2 + 0] = 25000 * generateToneSample(s.freqL, s.rate, s.t)
            samples[n * 2 + 1] = 25000 * generateToneSample(s.freqR, s.rate, s.t)
        else
            samples[n] = 25000 * generateToneSample(s.freqL, s.rate, s.t)
        end
        s.t = s.t + 1
    end
    for blk = 0, 17 do
        s.encoder:processXABlock(samples + perBlock * blk, xa:cast 'uint8_t *' + 8 + 128 * blk, s.mode,
                                 s.stereo and 2 or 1)
    end
    stampLBA(8 + 0x900, i, 'X')
    iso:writeSector(xa:cast 'uint8_t *', 2336, 'M2_RAW')
end

local function writeForm2DataSector(i)
    ffi.fill(xa.data, 2336)
    fillSubheader(1, 0, 0x28, 0)
    stampLBA(8, i, 'D')
    iso:writeSector(xa:cast 'uint8_t *', 2336, 'M2_RAW')
end

for i = xaStart, xaEnd - 1 do
    local entry = plan[i]
    if entry and entry.stream then
        writeAudioSector(i, entry)
    elseif entry and entry.form2data then
        writeForm2DataSector(i)
    else
        b[0] = bit.band(i, 0xff)
        b[1] = bit.band(bit.rshift(i, 8), 0xff)
        b[2] = bit.band(bit.rshift(i, 16), 0xff)
        iso:writeSector(b:cast 'uint8_t *', 2048)
    end
end

for i = 40 * 60 * 75, 70 * 60 * 75 - 1 do
    b[0] = bit.band(i, 0xff)
    b[1] = bit.band(bit.rshift(i, 8), 0xff)
    b[2] = bit.band(bit.rshift(i, 16), 0xff)
    iso:writeSector(b:cast 'uint8_t *', 2048)
end

b:resize(2352)
for i = 0, 588 - 1 do
    local s = 25000 * generateToneSample(450, 44100, i)
    b[i * 4 + 0] = bit.band(s, 0xff)
    b[i * 4 + 1] = bit.band(bit.rshift(s, 8), 0xff)
    b[i * 4 + 2] = b[i * 4 + 0]
    b[i * 4 + 3] = b[i * 4 + 1]
end
local audioTrack
audioTrack = Support.File.open('test-t2.bin', 'TRUNCATE')
for i = 1, 75 * 5 do
    audioTrack:write(b)
end
audioTrack = Support.File.open('test-t3.bin', 'TRUNCATE')
for i = 1, 75 * 5 + 15 do
    audioTrack:write(b)
end
audioTrack = Support.File.open('test-t4.bin', 'TRUNCATE')
for i = 1, 75 * 5 + 15 do
    audioTrack:write(b)
end
audioTrack = Support.File.open('test-t5.bin', 'TRUNCATE')
for i = 1, 75 * 5 + 15 do
    audioTrack:write(b)
end
audioTrack = Support.File.open('test-t6.bin', 'TRUNCATE')
for i = 1, 75 * 5 + 15 do
    audioTrack:write(b)
end

local cue = Support.File.open('test.cue', 'TRUNCATE')
cue:write [[
FILE "test.bin" BINARY
  TRACK 01 MODE2/2352
    INDEX 01 00:00:00
FILE "test-t2.bin" BINARY
  TRACK 02 AUDIO
    INDEX 01 00:00:00
FILE "test-t3.bin" BINARY
  TRACK 03 AUDIO
    INDEX 01 00:00:00
FILE "test-t4.bin" BINARY
  TRACK 04 AUDIO
    INDEX 00 00:00:00
    INDEX 01 00:02:00
FILE "test-t5.bin" BINARY
  TRACK 05 AUDIO
    INDEX 00 00:00:00
    INDEX 01 00:02:45
FILE "test-t6.bin" BINARY
  TRACK 06 AUDIO
    INDEX 00 00:00:00
    INDEX 01 00:02:00
]]

for i = 7, 25 do
    audioTrack = Support.File.open(string.format('test-t%d.bin', i), 'TRUNCATE')
    for j = 1, 75 * 5 + 15 do
        audioTrack:write(b)
    end
    cue:write(string.format('FILE "test-t%d.bin" BINARY\n', i))
    cue:write(string.format('  TRACK %02d AUDIO\n', i))
    cue:write '    INDEX 01 00:02:00\n'
end

PCSX.quit()
