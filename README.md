# Nugget

Nugget is a collection of bare-metal libraries, runtime pieces, and example projects for
the original PlayStation. It is the home of [PSYQo](psyqo/README.md), an object-oriented
C++ SDK for the console, and of [OpenBIOS](openbios/README.md), a from-scratch
reimplementation of Sony's BIOS.

It is built to be consumed as a submodule, so a project can pull in only the parts it
needs without dragging along an emulator or a toolchain.
[PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) uses it that way, and its
`src/mips` tree is this repository.

Read the [doc](doc/README.md) folder for what lives here, and
[PSYQo's Getting Started](psyqo/GETTING_STARTED.md) for toolchain setup. Generated API
documentation is at <https://pcsx-redux.github.io/nugget/>.

Issues and pull requests are welcome here. To talk about PlayStation development,
hacking and reverse engineering, join the PSX.Dev Discord server:
[![Discord](https://img.shields.io/discord/642647820683444236)](https://discord.gg/QByKPpH)

The code in this repository is MIT-licensed; see [LICENSE](LICENSE). The contents of
`third_party` carry their own licenses.
