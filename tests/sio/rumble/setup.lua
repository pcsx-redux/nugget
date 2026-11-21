-- The rumble mapping commands only answer on an analog pad, and safe mode
-- starts port 1 as a digital one. The type is cached at map() time.
PCSX.settings.pads[1].DeviceType = 'Analog'
PCSX.settings.pads[1].Connected = true
PCSX.SIO0.slots[1].pads[1].map()
