local psyqo = path.join(os.scriptdir(), "..", "..")

includes(psyqo)

target("hello", function()
    add_rules("psyqo.app")
    add_files("*.cpp")
end)

target("hello.ps-exe", function()
    add_deps("hello")
    add_rules("ps-exe")
end)
