local root = path.join(os.scriptdir(), "..")

set_allowedmodes("debug", "dev", "release")
includes(root)

target("helloworld", function()
    set_kind("binary")
    add_rules("nugget")
    add_files("main/**.c")
end)

target("helloworld.ps-exe", function()
    add_deps("helloworld")
    add_rules("ps-exe")
end)
