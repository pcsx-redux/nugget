local psyqo = path.join(os.scriptdir(), "..", "..")

includes(psyqo)

target("sprite", function()
    add_rules("psyqo.app")
    add_files("*.cpp")
end)

target("sprite.ps-exe", function()
    add_deps("sprite")
    add_rules("ps-exe")
end)
