#pragma once

// C0(1Bh), installStdIo. In a header of its own because cester includes the
// test file again inside functions, where a top-level asm cannot go.
#include "common/syscalls/syscalls.h"

NUGGET_BIOS_THUNK(installStdIo, 0xc0, 0x1b)
NUGGET_BIOS_EXTERN void __nugget_bios_installStdIo(int mode);
