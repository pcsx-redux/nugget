-- Rules that turn asset files into C sources a target can link: nugget.bin2c
-- embeds any file as a byte array, nugget.psxm converts a Wavefront OBJ into
-- the PXM2 mesh format described in common/util/psxm.hh. The root xmake.lua
-- includes this file, so both rules are available to anything that includes
-- the root.

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

-- Mesh conversion. Turns a Wavefront OBJ into the compact indexed form the
-- PS1 wants: int16 vertices in GTE units, deduplicated 4.12 normals, and
-- triangles carrying indices plus UVs. Textures are not this rule's business;
-- they travel as TIMs and go through nugget.bin2c like any other blob.

local function _psxm_split(line)
    local parts = {}
    for word in line:gmatch("%S+") do
        parts[#parts + 1] = word
    end
    return parts
end

local function _psxm_parse_obj(raise, io, pathname)
    local file = io.open(pathname, "r")
    if not file then
        raise("nugget.psxm: cannot open '%s'", pathname)
    end

    local positions, texcoords, normals, faces = {}, {}, {}, {}
    for line in file:lines() do
        local parts = _psxm_split(line)
        local op = parts[1]
        if op == "v" then
            positions[#positions + 1] = {
                tonumber(parts[2]),
                tonumber(parts[3]),
                tonumber(parts[4]),
            }
        elseif op == "vt" then
            texcoords[#texcoords + 1] = { tonumber(parts[2]), tonumber(parts[3]) }
        elseif op == "vn" then
            normals[#normals + 1] = {
                tonumber(parts[2]),
                tonumber(parts[3]),
                tonumber(parts[4]),
            }
        elseif op == "f" then
            if #parts ~= 4 then
                raise("nugget.psxm: %s: only triangles are supported, got a %d-gon", pathname,
                    #parts - 1)
            end
            local corners = {}
            for i = 2, 4 do
                local v, t, n = parts[i]:match("^(-?%d*)/(-?%d*)/(-?%d*)$")
                if not v then
                    v, t = parts[i]:match("^(-?%d*)/(-?%d*)$")
                end
                if not v then
                    v = parts[i]:match("^(-?%d*)$")
                end
                if not v or v == "" then
                    raise("nugget.psxm: %s: cannot parse face corner '%s'", pathname, parts[i])
                end
                -- OBJ indices count from 1, and a negative one counts back from
                -- the most recent element declared so far.
                local function resolve(text, list, what)
                    if text == nil or text == "" then return nil end
                    local index = tonumber(text)
                    if index < 0 then index = #list + 1 + index end
                    if index < 1 or index > #list then
                        raise("nugget.psxm: %s: face corner '%s' has %s index %s, out of range",
                            pathname, parts[i], what, text)
                    end
                    return index
                end
                corners[#corners + 1] = {
                    v = resolve(v, positions, "vertex"),
                    t = resolve(t, texcoords, "texture"),
                    n = resolve(n, normals, "normal"),
                }
            end
            faces[#faces + 1] = corners
        end
    end
    file:close()
    return positions, texcoords, normals, faces
end

-- Blender hands every panel of a curved surface its own flat normal, so a
-- directional light gives each one a different brightness and the joins show up
-- as hard seams. Averaging the normals of faces that meet at a shallow angle
-- blends across them, while a real edge stays sharp because its faces are too
-- far apart in angle to be averaged together. Nothing about the geometry
-- changes: same vertices, same triangles, no subdivision.
local function _psxm_smooth_normals(positions, faces, angle)
    local faceNormals = {}
    for index, corners in ipairs(faces) do
        local a = positions[corners[1].v]
        local b = positions[corners[2].v]
        local c = positions[corners[3].v]
        local e1 = { b[1] - a[1], b[2] - a[2], b[3] - a[3] }
        local e2 = { c[1] - a[1], c[2] - a[2], c[3] - a[3] }
        local n = {
            e1[2] * e2[3] - e1[3] * e2[2],
            e1[3] * e2[1] - e1[1] * e2[3],
            e1[1] * e2[2] - e1[2] * e2[1],
        }
        local length = math.sqrt(n[1] * n[1] + n[2] * n[2] + n[3] * n[3])
        if length == 0 then length = 1 end
        faceNormals[index] = { n[1] / length, n[2] / length, n[3] / length }
    end

    local touching = {}
    for index, corners in ipairs(faces) do
        for _, corner in ipairs(corners) do
            local list = touching[corner.v]
            if not list then
                list = {}
                touching[corner.v] = list
            end
            list[#list + 1] = index
        end
    end

    local limit = math.cos(angle * math.pi / 180.0)
    local smoothed = {}
    for index, corners in ipairs(faces) do
        local own = faceNormals[index]
        for _, corner in ipairs(corners) do
            local sum = { 0.0, 0.0, 0.0 }
            for _, other in ipairs(touching[corner.v]) do
                local n = faceNormals[other]
                if own[1] * n[1] + own[2] * n[2] + own[3] * n[3] >= limit then
                    sum[1] = sum[1] + n[1]
                    sum[2] = sum[2] + n[2]
                    sum[3] = sum[3] + n[3]
                end
            end
            local length = math.sqrt(sum[1] * sum[1] + sum[2] * sum[2] + sum[3] * sum[3])
            if length == 0 then length = 1 end
            smoothed[index .. ":" .. corner.v] = {
                sum[1] / length,
                sum[2] / length,
                sum[3] / length,
            }
        end
    end
    return smoothed, faceNormals
end

local function _psxm_clamp16(value)
    value = math.floor(value + 0.5)
    if value < -32768 then return -32768 end
    if value > 32767 then return 32767 end
    return value
end

local function _psxm_u16(value)
    return string.char(value % 256, math.floor(value / 256) % 256)
end

local function _psxm_u32(value)
    return string.char(value % 256, math.floor(value / 256) % 256,
        math.floor(value / 65536) % 256, math.floor(value / 16777216) % 256)
end

local function _psxm_s16(value)
    if value < 0 then value = value + 65536 end
    return _psxm_u16(value)
end

local function _psxm_align4(value)
    return value + ((4 - value % 4) % 4)
end

-- The blob layout described in common/util/psxm.hh. Every section carries its
-- own offset so that adding one later cannot silently shift the ones behind it.
local function _psxm_convert(raise, io, input, scale, smoothAngle, textureSize)
    local positions, texcoords, normals, faces = _psxm_parse_obj(raise, io, input)
    if #faces == 0 then
        raise("nugget.psxm: '%s' contains no triangles", input)
    end

    -- Object space to GTE space. The PS1 screen has Y growing downwards, so the
    -- sign flips here rather than in the renderer. Corner order is deliberately
    -- left alone: negating Y flips the model's handedness, which would reverse
    -- the winding, but the GPU already measures winding in a Y-down frame and
    -- the two cancel. Rewinding here on handedness grounds reads as obviously
    -- correct and inverts every single face in practice.
    local vertices = {}
    local clamped = 0
    for index, p in ipairs(positions) do
        local x = _psxm_clamp16(p[1] * scale)
        local y = _psxm_clamp16(-p[2] * scale)
        local z = _psxm_clamp16(p[3] * scale)
        for _, v in ipairs({ p[1] * scale, -p[2] * scale, p[3] * scale }) do
            if v < -32768 or v > 32767 then clamped = clamped + 1 end
        end
        vertices[index] = { x, y, z }
    end
    if clamped > 0 then
        raise("nugget.psxm: '%s': %d coordinates overflow int16 at scale %g; lower the scale",
            input, clamped, scale)
    end

    local smoothed
    if smoothAngle > 0 then
        smoothed = _psxm_smooth_normals(positions, faces, smoothAngle)
    end

    -- Normals are deduplicated because the light is evaluated once per unique
    -- normal into a table rather than once per vertex, so the per-frame cost
    -- tracks this count and not the triangle count.
    -- Deduplicate on the integers that actually get written, not on the
    -- floats. Two normals that round to the same 4.12 triple are the same
    -- normal in the output, and any finer distinction is one the format cannot
    -- represent. Keying on formatted floats instead looks equivalent and is
    -- not: "%.4f" prints both true negative zero and -1e-17 as "-0.0000", so
    -- it merges by accident, and normalising the sign splits them back apart.
    local normalTable, normalOrder = {}, {}
    local function normalIndex(n)
        local x = _psxm_clamp16(n[1] * 4096)
        local y = _psxm_clamp16(n[2] * 4096)
        local z = _psxm_clamp16(n[3] * 4096)
        local key = x .. "," .. y .. "," .. z
        local existing = normalTable[key]
        if existing then return existing end
        normalOrder[#normalOrder + 1] = { x, y, z }
        normalTable[key] = #normalOrder - 1
        return #normalOrder - 1
    end

    local last = textureSize - 1
    local triangles = {}
    local repaired = 0
    for index, corners in ipairs(faces) do
        local order = { 1, 2, 3 }

        -- Self-consistency only, checked in object space where no handedness
        -- question exists yet. On a sane export this fires on nothing.
        local authored = corners[1].n and normals[corners[1].n]
        if authored then
            local a = positions[corners[1].v]
            local b = positions[corners[2].v]
            local c = positions[corners[3].v]
            local e1 = { b[1] - a[1], b[2] - a[2], b[3] - a[3] }
            local e2 = { c[1] - a[1], c[2] - a[2], c[3] - a[3] }
            local g = {
                e1[2] * e2[3] - e1[3] * e2[2],
                e1[3] * e2[1] - e1[1] * e2[3],
                e1[1] * e2[2] - e1[2] * e2[1],
            }
            if g[1] * authored[1] + g[2] * authored[2] + g[3] * authored[3] < 0 then
                order = { 1, 3, 2 }
                repaired = repaired + 1
            end
        end

        local record = { v = {}, n = {}, uv = {} }
        for slot, which in ipairs(order) do
            local corner = corners[which]
            record.v[slot] = corner.v - 1

            local n
            if smoothed then
                n = smoothed[index .. ":" .. corner.v]
            elseif corner.n then
                n = normals[corner.n]
            else
                n = { 0, 0, 0 }
            end
            -- Normals take the same Y flip as the positions, stored as 4.12 so
            -- the runtime can dot them against a light direction directly.
            record.n[slot] = normalIndex({ n[1], -n[2], n[3] })

            local uv = corner.t and texcoords[corner.t] or { 0, 0 }
            -- The last addressable texel is size - 1, and V is flipped because
            -- OBJ texture space grows upwards while VRAM grows downwards.
            local u = math.floor(uv[1] * last + 0.5)
            local v = math.floor((1.0 - uv[2]) * last + 0.5)
            record.uv[slot] = {
                math.max(0, math.min(255, u)),
                math.max(0, math.min(255, v)),
            }
        end
        triangles[#triangles + 1] = record
    end

    local headerSize = 24
    local vertexOffset = headerSize
    local normalOffset = _psxm_align4(vertexOffset + #vertices * 6)
    local triangleOffset = _psxm_align4(normalOffset + #normalOrder * 6)

    local out = { "PXM2" }
    out[#out + 1] = _psxm_u16(#vertices)
    out[#out + 1] = _psxm_u16(#normalOrder)
    out[#out + 1] = _psxm_u16(#triangles)
    out[#out + 1] = _psxm_u16(0)
    out[#out + 1] = _psxm_u32(vertexOffset)
    out[#out + 1] = _psxm_u32(normalOffset)
    out[#out + 1] = _psxm_u32(triangleOffset)

    local body = table.concat(out)
    for _, v in ipairs(vertices) do
        body = body .. _psxm_s16(v[1]) .. _psxm_s16(v[2]) .. _psxm_s16(v[3])
    end
    body = body .. string.rep("\0", normalOffset - #body)
    for _, n in ipairs(normalOrder) do
        body = body .. _psxm_s16(n[1]) .. _psxm_s16(n[2]) .. _psxm_s16(n[3])
    end
    body = body .. string.rep("\0", triangleOffset - #body)
    for _, t in ipairs(triangles) do
        body = body .. _psxm_u16(t.v[1]) .. _psxm_u16(t.v[2]) .. _psxm_u16(t.v[3])
            .. _psxm_u16(t.n[1]) .. _psxm_u16(t.n[2]) .. _psxm_u16(t.n[3])
            .. string.char(t.uv[1][1], t.uv[1][2], t.uv[2][1], t.uv[2][2],
                t.uv[3][1], t.uv[3][2])
            .. _psxm_u16(0)
    end

    return body, #vertices, #normalOrder, #triangles, repaired
end

local function _psxm_collect_inputs(raise, target)
    local entries = target:values("nugget.psxm.files") or {}
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
            raise("nugget.psxm: input '%s' did not match any file", entry)
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

rule("nugget.psxm", function()
    -- Generating in before_config rather than on_build is deliberate, and is
    -- the same reason bin2c does it: a rule that only writes its source at
    -- build time leaves a clean tree with nothing to compile.
    before_config(function(target)
        local inputs = _psxm_collect_inputs(raise, target)
        if #inputs == 0 then
            raise(
            "nugget.psxm: no input files found; add values via nugget.psxm.files (for example: add_values(\"nugget.psxm.files\", \"*.obj\"))")
        end

        local prefix = target:values("nugget.psxm.prefix") or "psxm"
        local scale = tonumber(target:values("nugget.psxm.scale")) or 256
        local smooth = tonumber(target:values("nugget.psxm.smooth")) or 50
        local textureSize = tonumber(target:values("nugget.psxm.texturesize")) or 256

        local output_dir = path.join(target:autogendir(), "rules", "nugget", "psxm")
        os.mkdir(output_dir)

        local cfiles = {}
        for _, input in ipairs(inputs) do
            local blob = _psxm_convert(raise, io, input, scale, smooth, textureSize)

            -- Round-trip through a file so the C emission stays in one place
            -- rather than being reimplemented here.
            local blobfile = path.join(output_dir, path.basename(input) .. ".psxm")
            _bin2c_write(raise, io, os, blobfile, blob)

            -- Name the symbol after the OBJ rather than the intermediate blob.
            -- The blob lives under the build directory, so naming it after that
            -- would bake the build layout into a symbol the source has to
            -- spell out.
            local symbol = _bin2c_sanitize_symbol(prefix, input)
            local output_h = path.join(output_dir, symbol .. ".h")
            local output_c = path.join(output_dir, symbol .. ".c")
            _bin2c_generate(raise, io, os, blobfile, output_c, output_h, symbol,
                _bin2c_sanitize_basename(input))
            cfiles[#cfiles + 1] = output_c
        end

        target:add("includedirs", output_dir)
        target:add("files", cfiles, { always_added = true })
    end)
end)
