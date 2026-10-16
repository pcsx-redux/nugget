local psyqo = path.join(os.scriptdir(), "..", "..")
local mips = path.join(psyqo, "..")

includes(psyqo)
includes(mips)

target("backgrounds", function()
    add_rules("psyqo.app", "nugget.psxm", "nugget.bin2c")
    add_files("*.cpp")
    -- Meshes are built from the OBJs at build time. Textures travel as TIMs,
    -- which are already in the layout the GPU wants, so they need no conversion
    -- at all and ride along as plain blobs.
    add_values("nugget.psxm.files", "*.obj")
    add_values("nugget.bin2c.files", "*.tim")
end)

target("backgrounds.ps-exe", function()
    add_deps("backgrounds")
    add_rules("ps-exe")
end)
