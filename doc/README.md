# Nugget

## Where

This repository is the upstream for all of the code in it. It is designed to be used as a
submodule, so that a project can pull in only the parts it wants without dragging along an
emulator or a toolchain. [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) consumes
it that way, at `src/mips`.

## What

This is a collection of several projects that are intended for running on the PlayStation 1,
either through the official hardware or emulators.

This can serve as a base for other projects, or as a reference for how to write code for the
PlayStation 1.

### Libraries

 - [common](../common) - Common code for all projects. Highly recommended to be used by other projects.
 - [psyqo](../psyqo) The PSYQo project. This is a new SDK for the PlayStation 1, written from scratch in C++, using modern paradigms. This is probably where the most complete documentation exists for this project, and should be used as a reference for how to write code for the PlayStation 1.
 - [psyqo-lua](../psyqo-lua) Lua scripting for PSYQo applications.
 - [psyqo-paths](../psyqo-paths) Convenience routines that sit on top of PSYQo, such as loading a file off the CD-Rom, which are too generic to belong in PSYQo itself.
 - [psyq](../psyq) Some additional code for the converted psyq libraries.
 - [lz4](../lz4) A single-function LZ4 block decompressor.
 - [crc32](../crc32) A CRC32 implementation that is optimized for the PlayStation 1, using the scratchpad as a speedup.

### Audio

 - [modplayer](../modplayer) A MOD player from the reverse engineering of HITMEN's modplayer.
 - [psmplayer](../psmplayer) Real-time MIDI playback, driving a VAB instrument bank from a PSM event stream. The [PSM format](https://github.com/ps1dev/standards/blob/main/PSM.md) is documented in the ps1dev standards repository.
 - [spdplayer](../spdplayer) Playback of [SPU dumps](https://github.com/ps1dev/standards/blob/main/SPUDUMP.md), with sound effect voices available above the music voices.

### Systems and examples

 - [openbios](../openbios) A fully functional BIOS implementation for the PlayStation 1, based on the reverse engineering of Sony's BIOS.
 - [shell](../shell) The tiny shell project. This is currently the shell software that OpenBIOS uses to have a boot logo and chime.
 - [helloworld](../helloworld) A very simple hello world.
 - [cxxhello](../cxxhello) A very quick hello world using C++.
 - [cube](../cube) A small demo that's demonstrating the use of the converted psyq libraries.
 - [ucl-demo](../ucl-demo) Decompressing a UCL/NRV2E stream, with a from-scratch decompressor.
 - [scratchstack](../scratchstack) Running a hot function on a separate stack in the scratchpad, to keep the compiler from spilling registers into slow main RAM.

### Tests

 - [tests](../tests) A collection of tests to verify emulator and hardware behavior.

## How

A toolchain will need to be installed before any of the projects can be built. The [PSYQo's Getting Started](../psyqo/GETTING_STARTED.md) documentation has instructions on how to install the toolchain.

Use the `Makefile` in each project to build that project. `cube` is the exception: it links against the converted Sony libraries, which have to be produced locally first. See [psyq](../psyq/README.md).

One trap worth knowing about: `openbios` and `tests` compile the same uC-sdk sources into the shared source directory under incompatible flags. Whichever one builds last wins. The objects it leaves behind are newer than their sources, so nothing that keys on timestamps notices. If a build fails with an undefined reference somewhere in the libc, run `find third_party/uC-sdk -name '*.o' -delete` and build again.

## Who

The PCSX-Redux project's authors are also the main authors and maintainers of this code. To discuss PlayStation 1 development, hacking, and reverse engineering in general, please join the PSX.Dev Discord server: [![Discord](https://img.shields.io/discord/642647820683444236)](https://discord.gg/QByKPpH)
