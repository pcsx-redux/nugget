# Visual Studio Code extension
The easiest to get started with PSYQo, and with development on the PlayStation 1 in general, is through a [Visual Studio Code](https://code.visualstudio.com/) extension. You can [find it in the marketplace](https://marketplace.visualstudio.com/items?itemName=grumpycoders.psx-dev), and [watch a video](https://youtu.be/KbAv-Ao7lzU) to see how to use it.

# The toolchain
In order to develop for the PlayStation1 using the PSYQo library, a modern C++ toolchain is required. There are several different toolchains available for the PS1, but the PSYQo library is designed to work with at least gcc 10 targeting mips I. Here are some installation methods available:

## Linux
There's no working distribution package for the toolchain, so build it from source with [this script](https://github.com/pcsx-redux/pcsx-redux/blob/main/tools/linux-mips/spawn-compiler.sh). Install the build dependencies first.

Debian derivatives:

```bash
sudo apt-get install -y make wget bzip2 xz-utils bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev
```

Arch derivatives:

```bash
sudo pacman -S --needed base-devel wget texinfo gmp mpfr libmpc
```

Then build and install it into `/usr/local`, or set `PREFIX` to install it elsewhere as a regular user:

```bash
wget https://raw.githubusercontent.com/pcsx-redux/pcsx-redux/main/tools/linux-mips/spawn-compiler.sh
sudo bash spawn-compiler.sh
```

The script works the same on other Unix flavors, given equivalent dependencies.

## MacOS

Using [Homebrew](https://brew.sh/), you can install the mips toolchain from the [pcsx-redux/mips tap](https://github.com/pcsx-redux/homebrew-mips):

```bash
brew install pcsx-redux/mips/mipsel-none-elf-gcc
```

## Windows

It is possible to install a mips toolchain on Windows by first running the following command:

```cmd
powershell -c "& { iwr -UseBasicParsing https://raw.githubusercontent.com/grumpycoders/pcsx-redux/main/mips.ps1 | iex }"
```

The computer might need to be rebooted after the installation of this script.

Next, run the following command to install the proper toolchain and its dependencies:

```cmd
mips install 16.2.0
```

## Docker
For users who don't want to modify their environments, it is possible to use [Docker](https://www.docker.com/) to run the mips toolchain. There are two scripts provided at the root of this repository, called `dockermake.sh` for Unix users and `dockermake.bat` for Windows users.

These scripts will behave like the `make` command, running in the same directory where the command was run. It'll be in a Linux environment with an adequate mips toolchain to use.

# Creating a project.

The canonical method to use `PSYQo` is to use the [nugget](https://github.com/pcsx-redux/nugget) repository as a submodule for your own project, say in the `third_party` directory. You can create a `hello.cpp` file at the root of your directory that's a copy of [the example hello world](examples/hello/hello.cpp). Doing a simple code drop of the nugget repository is also acceptable.

Here's how to get all this going. The commands are aimed at Unix environments, but the spirit of what's going on should be very simple to understand for Windows too.

First, let's create a folder for your project:

```bash
mkdir hello
cd hello
```

Then, initialize the git repository:
```bash
git init
```

And then, let's add nugget as a submodule:
```bash
git submodule add https://github.com/pcsx-redux/nugget.git third_party/nugget
```

Now, download [the example hello world](examples/hello/hello.cpp) and drop it in your project folder.

Finally, create a `Makefile` at the root of your project, which follows the following structure:

```makefile
# The name of the binary to be created.
TARGET = hello
# The type of the binary to be created - ps-exe is most common.
TYPE = ps-exe

# The list of sources files to compile within the binary.
SRCS = \
hello.cpp \

# Setting the minimum version of the C++. C++-20 is the minimum required version by PSYQo.
CXXFLAGS = -std=c++20

# This will activate the PSYQo library and the rest of the toolchain.
include third_party/nugget/psyqo/psyqo.mk
```

Once done, you can simply run `make` (or use the `dockermake` script) in your project root to create the application. This should create the binary called `hello.ps-exe`, which you can then run in the emulator of your choice, or on the real hardware using for example [unirom](https://github.com/JonathanDotCel/unirom8_bootdisc_and_firmware_for_ps1) and [nops](https://github.com/JonathanDotCel/NOTPSXSerial).

See the [examples](examples) directory for more complex project structures you can use. Since the examples are subfolders of the psyqo library, their `Makefile`s will have a different method to include the file `psyqo.mk`, but that's otherwise the only difference.

If a more complex build system is required, compiling PSYQo itself is very simple, and should be straightforward to integrate. Refer to the [Makefile](Makefile) for what to do exactly.

# IDE integration
The recommended IDE is [Visual Studio Code](https://code.visualstudio.com/). The [clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) extension is configured within PSYQo, and will provide valuable IDE features such as completion or code navigation. It is also possible to debug PSYQo binaries, using the [Native Debug](https://marketplace.visualstudio.com/items?itemName=webfreak.debug) extension. See the [PCSX-Redux's launch.json](https://github.com/grumpycoders/pcsx-redux/blob/main/.vscode/launch.json) for some examples.

# Build options
When using the `psyqo.mk` file, you can specify build options for the PSYQo library and your binary. It is possible to specify the following options:

- `make BUILD=Release`  
This is the default build, and will enable a release build of the library and your binary. It will be fast and small, but debugging will be hard.
- `make BUILD=Debug`  
This will enable a full debug build of the library and your binary. It will be slow and bloated, but debugging will be very easy.
- `make BUILD=SmallDebug`  
This will enable a debug build of the library and your binary with still some optimizations. Debugging is possible, but will be hindered at times. It is the best compromise if debugging is needed while retaining some speed and small code footprint.
- `make BUILD=FastDebug`  
This will enable an optimized build where every function call records its call site on a shadow call stack, which `psyqo::ShadowStack` reads back on the console, to get callstacks for profiling or crash reports. See [shadow-stack.hh](shadow-stack.hh) for the rules, and the [shadow-stack](examples/shadow-stack) example. The bookkeeping costs two calls per function call.
- `make BUILD=LTO`  
This will enable a release build of the library and your binary with [LTO](https://gcc.gnu.org/wiki/LinkTimeOptimization) enabled. It will be really fast and really small, but debugging will be impossible.

# Profiling
[profiler.hh](profiler.hh) times scopes into a ring buffer: install a `psyqo::Profiler::Buffer` once, then put `PSYQO_PROFILE_SCOPE("name")` at the top of any block to time. PCSX-Redux reads the buffer live and draws it as a timeline; on a console, `psyqo::Profiler::dumpPCDRV` writes it to a host file. See the [profiler](examples/profiler) example.

# Further reading
Moving on to the [Concepts](CONCEPTS.md) page is the next step in understanding how the library functions. The library itself is also thoroughly documented, and the [nugget website](https://pcsx-redux.github.io/nugget/) has a render of the doxygen documentation.

To discuss PlayStation1's development, hacking, and reverse engineering in general, please join the PSX.Dev Discord server: [![Discord](https://img.shields.io/discord/642647820683444236)](https://discord.gg/QByKPpH)
