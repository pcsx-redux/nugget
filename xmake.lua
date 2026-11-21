local root = path.join(os.scriptdir())
local mips = path.join(os.scriptdir())

includes(path.join(root, "tools", "xmake", "assets.lua"))

local xmakePsx = path.join(root, "third_party", "xmake-psx")
if not os.isfile(path.join(xmakePsx, "xmake.lua")) then
    print("nugget: %s is empty, so the psx rule cannot load. Fetch the submodules: git submodule sync --recursive && git submodule update --init --recursive", xmakePsx)
end
includes(xmakePsx)

-- Turns the one binary target this depends on into a PS-X EXE, the same
-- layout the Makefile build produces through ps-exe.ld: the file holds the
-- loadable segments' file contents only, and crt0 clears BSS.
rule("ps-exe", function()
    on_load(function(target)
        target:set("kind", "binary")
        target:set("plat", "psx")
        target:set("arch", "mipsel")
        if not target:get("extension") then
            target:set("extension", ".ps-exe")
        end
    end)
    before_config(function(target)
        local binary
        for _, dep in ipairs(target:orderdeps()) do
            if dep:kind() == "binary" then
                if binary then
                    raise("nugget: ps-exe target %s depends on more than one binary", target:name())
                end
                binary = dep
            end
        end
        if not binary then
            raise("nugget: ps-exe target %s needs a binary dependency", target:name())
        end
        target:data_set("nugget.ps-exe.binary", binary)
        target:set("basename", binary:basename())
    end)
    on_build(function(target)
        import("core.project.depend")
        local binary = target:data("nugget.ps-exe.binary")
        local input = binary:targetfile()
        local output = target:targetfile()
        local sp = target:values("ps-exe.sp") or 0x801fff00
        depend.on_changed(function()
            local elf = io.readfile(input, {encoding = "binary"})
            local function u16(offset) return string.unpack("<I2", elf, offset + 1) end
            local function u32(offset) return string.unpack("<I4", elf, offset + 1) end
            if elf:sub(1, 4) ~= "\x7fELF" or elf:byte(5) ~= 1 or elf:byte(6) ~= 1 then
                raise("nugget: %s is not a little-endian ELF32 file", input)
            end
            local entry = u32(0x18)
            local phoff, phentsize, phnum = u32(0x1c), u16(0x2a), u16(0x2c)
            local segments = {}
            local low, high
            for i = 0, phnum - 1 do
                local ph = phoff + i * phentsize
                local vaddr, filesz = u32(ph + 8), u32(ph + 16)
                -- PT_LOAD, skipping the scratchpad, which the loader cannot fill.
                if u32(ph) == 1 and filesz > 0 and vaddr ~= 0x1f800000 then
                    local offset = u32(ph + 4)
                    table.insert(segments, {vaddr = vaddr, data = elf:sub(offset + 1, offset + filesz)})
                    low = low and math.min(low, vaddr) or vaddr
                    high = high and math.max(high, vaddr + filesz) or vaddr + filesz
                end
            end
            if not low then
                raise("nugget: %s has nothing to load", input)
            end
            local size = (high - low + 0x7ff) & ~0x7ff
            local image = {}
            local cursor = low
            table.sort(segments, function(a, b) return a.vaddr < b.vaddr end)
            for _, segment in ipairs(segments) do
                table.insert(image, string.rep("\0", segment.vaddr - cursor))
                table.insert(image, segment.data)
                cursor = segment.vaddr + #segment.data
            end
            table.insert(image, string.rep("\0", low + size - cursor))
            local header = "PS-X EXE" .. string.rep("\0", 8)
                .. string.pack("<I4I4I4I4", entry, 0, low, size)
                .. string.rep("\0", 16)
                .. string.pack("<I4I4", sp, 0)
            header = header .. string.rep("\0", 0x800 - #header)
            os.mkdir(path.directory(output))
            io.writefile(output, header .. table.concat(image), {encoding = "binary"})
            print("ps-exe %s", output)
        end, {files = input, values = {tostring(sp)}})
    end)
end)

target("nugget.crt", function()
    set_kind("static")
    add_rules("psx")
    add_files(path.join(mips, "common", "crt0", "memory-c.c"))
    add_files(path.join(mips, "common", "crt0", "memory-s.s"))
    add_files(path.join(mips, "common", "syscalls", "printf.s"))
    add_values("psx.no_gpopt", true)
    add_values("psx.abi", "o32")
    add_asflags("-mabi=32")
    add_cxflags("-mabi=32", "-fno-reg-struct-return", "-G0")
    add_cxflags("-fno-fuse-ops-with-volatile-access")
end)

rule("nugget", function()
    add_deps("psx")
    on_load(function(target)
        target:add("includedirs", mips)
        target:add("values", "psx.no_crt", true)
        target:add("values", "psx.no_ldscript", true)
        target:add("values", "psx.no_gpopt", true)
        -- o32, like the BIOS and the Makefile build. xmake-psx's default eabi
        -- also passes -fcall-saved-k0/k1, which -mabi=32 below does not undo,
        -- and the exception handler clobbers k0 and k1.
        target:set("values", "psx.abi", "o32")
        target:add("asflags", "-mabi=32")
        target:add("cxflags", "-mabi=32", "-fno-reg-struct-return", "-G0", "-fno-strict-aliasing")
        -- gcc 16 can narrow a 32-bit volatile hardware register load; xmake
        -- checks these and drops them on compilers that lack the option.
        target:add("cxflags", "-fno-fuse-ops-with-volatile-access")
        target:add("ldflags", "-fno-fuse-ops-with-volatile-access")
    end)
    before_config(function(target)
        if target:get("kind") == "binary" then
            target:add("deps", "nugget.crt")
            if target:values("c++") then
                target:add("files", path.join(mips, "common", "crt0", "crt0cxx.s"))
                target:add("files", path.join(mips, "common", "crt0", "cxxglue.c"))
            else
                target:add("files", path.join(mips, "common", "crt0", "crt0.s"))
            end
            target:add("files", path.join(mips, "xmake.ld"))
            target:add("files", path.join(mips, "nooverlay.ld"))
        end
    end)
end)
