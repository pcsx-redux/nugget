local root = path.join(os.scriptdir(), "..")
local psyqolua = os.scriptdir()
local lua = path.join(root, "third_party", "psxlua", "src")

includes(path.join(root, "psyqo"))

local lua_core = {
    "lapi", "lctype", "ldebug", "ldo", "lfunc", "lgc", "lmem", "lobject", "lopcodes",
    "lstate", "lstring", "ltable", "ltm", "lundump", "lvm", "lzio", "llibc",
    "lauxlib", "lbaselib", "lbitlib", "lcorolib", "ldblib", "lstrlib", "ltablib", "linit",
}

local function psxlua(name, parser)
    target(name, function()
        set_kind("static")
        add_rules("nugget")
        add_deps("psyqo")
        set_optimize("smallest")
        for _, f in ipairs(lua_core) do
            add_files(path.join(lua, f .. ".c"))
        end
        for _, f in ipairs(parser) do
            add_files(path.join(lua, f .. ".c"))
        end
        add_defines("LUA_COMPAT_ALL")
        add_defines("LUA_TARGET_PSX", {public = true})
        add_includedirs(lua, {public = true})
        add_cflags("-fno-builtin", "-Wno-attributes")
    end)
end

psxlua("psxlua", {"lcode", "ldump", "llex", "lparser"})
psxlua("psxlua.noparser", {"lnoparser"})

target("psyqo-lua", function()
    set_kind("static")
    add_rules("nugget")
    add_values("c++", true)
    add_deps("psyqo")
    add_files(path.join(psyqolua, "src", "*.cpp"))
    add_defines("LUA_TARGET_PSX", {public = true})
    add_includedirs(lua, {public = true})
    add_ldflags(
        "-Wl,--defsym,luaI_sprintf=sprintf_for_Lua",
        "-Wl,--defsym,luaI_realloc=libc_realloc",
        "-Wl,--defsym,luaI_free=libc_free",
        {public = true, force = true})
end)

-- A PSYQo application with Lua. Set the "psyqo-lua.noparser" value to link
-- the Lua runtime without its parser, for scripts precompiled to bytecode.
rule("psyqo-lua.app", function()
    add_deps("psyqo.app")
    on_load(function(target)
        target:add("deps", "psyqo-lua")
        if target:values("psyqo-lua.noparser") then
            target:add("deps", "psxlua.noparser")
        else
            target:add("deps", "psxlua")
        end
    end)
end)
