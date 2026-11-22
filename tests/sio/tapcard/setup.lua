-- Arms for tapcard.c, left in the scratchpad: 'TAPC', then 1 for a tap on port 1 with cards in slots B
-- and D and none in C, or 0 for no tap with the slot B and D cards still inserted. Called by the setup-*.lua beside this.
return function(tap)
    PCSX.settings.pads[1].Multitap = tap
    PCSX.settings.emulator.Mcd1BInserted = true
    PCSX.settings.emulator.Mcd1CInserted = false
    PCSX.settings.emulator.Mcd1DInserted = true
    local scratch = require("ffi").cast("uint32_t*", PCSX.getScratchPtr())
    scratch[0] = 0x43504154
    scratch[1] = tap and 1 or 0
end
