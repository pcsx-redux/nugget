local root = path.join(os.scriptdir(), "..")
local mips = path.join(os.scriptdir(), "..")

set_allowedmodes("debug", "dev", "release")
includes(mips)

target("nugget.xmplayer", function()
    set_kind("static")
    add_rules("nugget")
    add_files("xmplayer.c")
end)

target("xmplayer.song", function ()
    set_kind("static")
    add_rules("nugget", "nugget.bin2c")
    add_values("nugget.bin2c.files", "song.xmp", "song.smp")
end)

target("xmplayer.demo", function()
    set_kind("binary")
    add_rules("nugget")
    add_deps("nugget.xmplayer", "xmplayer.song")
    add_files("demo.c")
end)

target("xmplayer.demo.ps-exe", function()
    add_deps("xmplayer.demo")
    add_rules("ps-exe")
end)
