local root = path.join(os.scriptdir())
local mips = path.join(os.scriptdir())

local function _bin2c_sanitize_symbol(prefix, input)
    local rel = path.relative(input, os.projectdir())
    local symbol = rel:gsub("[^%w]", "_")
    if symbol:match("^[0-9]") then
        symbol = "_" .. symbol
    end
    return prefix .. "_" .. symbol
end

local function _bin2c_sanitize_basename(input)
    local rel = path.relative(input, os.projectdir())
    local name = rel:gsub("[^%w]", "_")
    if name:match("^[0-9]") then
        name = "_" .. name
    end
    return name
end

local function _bin2c_read(raise, io, pathname)
    local f = io.open(pathname, "rb")
    if not f then
        raise("bin2c: unable to read input file '%s'", pathname)
    end
    local data = f:read("*all")
    f:close()
    return data or ""
end

local function _bin2c_write(raise, io, os, pathname, data)
    if os.isfile(pathname) then
        local existing = io.open(pathname, "rb")
        local previous = existing:read("*all")
        existing:close()
        if previous == data then
            return
        end
    end
    local f = io.open(pathname, "wb")
    if not f then
        raise("bin2c: unable to write output file '%s'", pathname)
    end
    f:write(data)
    f:close()
end

local function _bin2c_generate(raise, io, os, input, output_c, output_h, symbol, compat_name)
    local blob = _bin2c_read(raise, io, input)
    local size = #blob

    local bytes = {}
    for i = 1, size do
        bytes[#bytes + 1] = string.format("0x%02x", blob:byte(i))
    end

    local lines = {}
    if size > 0 then
        for i = 1, #bytes, 12 do
            local chunk = {}
            for j = i, math.min(i + 11, #bytes) do
                chunk[#chunk + 1] = bytes[j]
            end
            lines[#lines + 1] = "    " .. table.concat(chunk, ", ")
        end
    else
        lines[#lines + 1] = "    0x00"
    end

    local header_name = path.filename(output_h)
    local cfile = table.concat({
        string.format("#include \"%s\"", header_name),
        "",
        string.format("const unsigned char %s[] = {", symbol),
        table.concat(lines, ",\n"),
        "};",
        string.format("const size_t %s_len = %du;", symbol, size),
        string.format("__asm__(\".globl _binary_%s_start\\n_binary_%s_start = %s\");", compat_name, compat_name, symbol),
        string.format("__asm__(\".globl _binary_%s_end\\n_binary_%s_end = %s + %d\");", compat_name, compat_name, symbol, size),
        string.format("const unsigned int _binary_%s_size = %du;", compat_name, size),
        "",
    }, "\n")

    local hfile = table.concat({
        "#pragma once",
        "#include <stddef.h>",
        "",
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
        "",
        string.format("extern const unsigned char %s[];", symbol),
        string.format("extern const size_t %s_len;", symbol),
        string.format("extern const unsigned char _binary_%s_start[];", compat_name),
        string.format("extern const unsigned char _binary_%s_end[];", compat_name),
        string.format("extern const unsigned int _binary_%s_size;", compat_name),
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
    }, "\n")

    _bin2c_write(raise, io, os, output_h, hfile)
    _bin2c_write(raise, io, os, output_c, cfile)
end

local function _bin2c_collect_inputs(raise, target)
    local entries = target:values("nugget.bin2c.files") or {}
    if type(entries) == "string" then
        entries = { entries }
    end

    local files = {}
    local seen = {}
    for _, entry in ipairs(entries) do
        local pattern = entry
        if not path.is_absolute(pattern) then
            pattern = path.join(target:scriptdir(), pattern)
        end

        local matches = os.files(pattern)
        if #matches == 0 and os.isfile(pattern) then
            matches = { pattern }
        end

        if #matches == 0 then
            raise("nugget.bin2c: input '%s' did not match any file", entry)
        end

        for _, file in ipairs(matches) do
            local abs = path.absolute(file)
            if not seen[abs] then
                seen[abs] = true
                files[#files + 1] = abs
            end
        end
    end
    return files
end

rule("nugget.bin2c", function()
    before_config(function(target)
        local inputs = _bin2c_collect_inputs(raise, target)
        if #inputs == 0 then
            raise(
            "nugget.bin2c: no input files found; add values via nugget.bin2c.files (for example: add_values(\"nugget.bin2c.files\", \"assets/*.bin\"))")
        end

        local prefix = target:values("nugget.bin2c.prefix") or "bin2c"
        local output_dir = target:values("nugget.bin2c.outputdir")
        if output_dir then
            if not path.is_absolute(output_dir) then
                output_dir = path.join(target:scriptdir(), output_dir)
            end
        else
            output_dir = path.join(target:autogendir(), "rules", "nugget", "bin2c")
        end

        local generated = {}
        local cfiles = {}
        for _, input in ipairs(inputs) do
            local symbol = _bin2c_sanitize_symbol(prefix, input)
            local output_h = path.join(output_dir, symbol .. ".h")
            local output_c = path.join(output_dir, symbol .. ".c")
            generated[#generated + 1] = {
                input = input,
                output_h = output_h,
                output_c = output_c,
                symbol = symbol,
                compat_name = _bin2c_sanitize_basename(input),
            }
            cfiles[#cfiles + 1] = output_c
        end

        for _, entry in ipairs(generated) do
            os.mkdir(path.directory(entry.output_h))
            _bin2c_generate(raise, io, os, entry.input, entry.output_c, entry.output_h, entry.symbol, entry.compat_name)
        end

        target:data_set("nugget.bin2c.generated", generated)
        target:add("includedirs", output_dir)
        target:add("files", cfiles, { always_added = true })
    end)
end)

includes(path.join(root, "third_party", "xmake-psx"))

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
            local sp = target:values("ps-exe.sp") or 0x801fff00
            local header = "PS-X EXE" .. string.rep("\0", 8)
                .. string.pack("<I4I4I4I4", entry, 0, low, size)
                .. string.rep("\0", 16)
                .. string.pack("<I4I4", sp, 0)
            header = header .. string.rep("\0", 0x800 - #header)
            os.mkdir(path.directory(output))
            io.writefile(output, header .. table.concat(image), {encoding = "binary"})
            print("ps-exe %s", output)
        end, {files = input})
    end)
end)

target("nugget.crt", function()
    set_kind("static")
    add_rules("psx")
    add_files(path.join(mips, "common", "crt0", "memory-c.c"))
    add_files(path.join(mips, "common", "crt0", "memory-s.s"))
    add_files(path.join(mips, "common", "syscalls", "printf.s"))
    add_values("psx.no_gpopt", true)
    add_asflags("-mabi=32")
    add_cxflags("-mabi=32", "-fno-reg-struct-return", "-G0")
end)

rule("nugget", function()
    add_deps("psx")
    on_load(function(target)
        target:add("includedirs", mips)
        target:add("values", "psx.no_crt", true)
        target:add("values", "psx.no_ldscript", true)
        target:add("values", "psx.no_gpopt", true)
        target:add("asflags", "-mabi=32")
        target:add("cxflags", "-mabi=32", "-fno-reg-struct-return", "-G0", "-fno-strict-aliasing")
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
