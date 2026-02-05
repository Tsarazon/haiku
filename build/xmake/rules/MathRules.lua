--[[
    MathRules.lua - Basic arithmetic operations for Haiku build system

    xmake equivalent of build/jam/MathRules (1:1 migration)

    Jam lacks native arithmetic, so it uses digit lookup tables for addition.
    Lua has native numbers, making these operations trivial.

    Functions exported:
    - AddNumAbs(a, b)  - Returns a + b (absolute/unsigned addition)

    Usage from other rules:
        import("rules.MathRules")
        local result = MathRules.AddNumAbs(5, 3)  -- returns 8
]]

-- AddNumAbs: add two non-negative integers
-- In Jam this was digit-by-digit with carry via lookup tables.
-- In Lua we use native arithmetic.
function AddNumAbs(a, b)
    return (tonumber(a) or 0) + (tonumber(b) or 0)
end
