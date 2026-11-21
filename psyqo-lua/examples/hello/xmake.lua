includes(path.join(os.scriptdir(), "..", ".."))

target("hello", function()
    add_rules("psyqo-lua.app")
    add_files("*.cpp")
end)

target("hello.ps-exe", function()
    add_deps("hello")
    add_rules("ps-exe")
end)
