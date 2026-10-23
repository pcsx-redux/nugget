-- Writes the host input fields of this process's shared display block (needs -shmdisplay) for the
-- multitap test, and leaves the arm in the scratchpad for the guest: 'MTAP', then 0 for nohost, 1 for
-- host, 2 for req1. Word offsets are the byte offsets from the static_asserts in pcsx-redux's
-- src/core/shmdisplay.h, divided by 4. Called by the setup-*.lua scripts beside this file.
local ffi = require("ffi")
ffi.cdef [[
int getpid(void);
int shm_open(const char* name, int oflag, int mode);
void* mmap(void* addr, size_t length, int prot, int flags, int fd, long offset);
]]

return function(arm)
    local host = arm ~= 0
    local fd = ffi.C.shm_open("pcsx-redux-display-" .. ffi.C.getpid(), 2, 0) -- O_RDWR
    assert(fd >= 0, "no shared display block, is -shmdisplay missing?")
    local u32 = ffi.cast("volatile uint32_t*", ffi.C.mmap(nil, 4096, 3, 1, fd, 0)) -- RW, MAP_SHARED
    local i32 = ffi.cast("volatile int32_t*", u32)
    assert(u32[0] == 0x44535850 and u32[1] >= 2, "need a version 2 shared display block")

    local HOSTINPUT, HOSTPADS, MULTITAP, SLOTPADS, DEVICE, DX, DY, BUTTONS = 12, 13, 15, 17, 25, 27, 29, 31
    -- Port 1: a tap, Cross on slot A, Triangle on B, C empty, Square on D.
    u32[HOSTPADS + 0] = 0xbfff
    u32[MULTITAP + 0] = 1
    u32[SLOTPADS + 1] = 0xefff
    u32[SLOTPADS + 2] = 0xffffffff
    u32[SLOTPADS + 3] = 0x7fff
    if arm == 2 then
        -- req1: the setup the real tap was measured with, a neGcon in slot A and B to D empty.
        u32[DEVICE + 0] = 1 + 3
        u32[SLOTPADS + 1] = 0xffffffff
        u32[SLOTPADS + 3] = 0xffffffff
    end
    -- Port 2: a Mouse, 300 right and 5 up pending, left button down.
    u32[HOSTPADS + 1] = 0xffff
    u32[DEVICE + 1] = 1 + 2
    i32[DX + 1] = 300
    i32[DY + 1] = -5
    u32[BUTTONS + 1] = 1
    u32[HOSTINPUT] = host and 1 or 0

    -- Pad 2 stays disconnected in the host arms: a device the host picks plugs it in.
    PCSX.settings.pads[2].Connected = not host

    local scratch = ffi.cast("uint32_t*", PCSX.getScratchPtr())
    scratch[0] = 0x5041544d
    scratch[1] = arm
end
