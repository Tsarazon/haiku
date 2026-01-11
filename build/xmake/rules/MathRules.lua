--[[
    MathRules.lua - Basic arithmetic operations

    xmake equivalent of build/jam/MathRules

    In Jam, there's no native arithmetic, so MathRules implements
    addition using digit lookup tables. In Lua/xmake, we have native
    arithmetic, so these functions are simple wrappers.

    Rules defined:
    - AddNumAbs - Add two absolute numbers

    Note: The original Jam implementation uses reversed digit lists
    (least significant digit first) for arbitrary precision arithmetic.
    We provide both the compatible API and simpler direct functions.
]]

-- ============================================================================
-- Native Arithmetic Functions (preferred in xmake)
-- ============================================================================

--[[
    Add(a, b)

    Add two numbers.

    Parameters:
        a - First number
        b - Second number

    Returns:
        Sum of a and b
]]
function Add(a, b)
    return (tonumber(a) or 0) + (tonumber(b) or 0)
end

--[[
    Sub(a, b)

    Subtract b from a.

    Parameters:
        a - First number
        b - Second number

    Returns:
        Difference (a - b)
]]
function Sub(a, b)
    return (tonumber(a) or 0) - (tonumber(b) or 0)
end

--[[
    Mul(a, b)

    Multiply two numbers.

    Parameters:
        a - First number
        b - Second number

    Returns:
        Product of a and b
]]
function Mul(a, b)
    return (tonumber(a) or 0) * (tonumber(b) or 0)
end

--[[
    Div(a, b)

    Divide a by b (integer division).

    Parameters:
        a - Dividend
        b - Divisor

    Returns:
        Integer quotient (a / b)
]]
function Div(a, b)
    local divisor = tonumber(b) or 1
    if divisor == 0 then
        error("Division by zero")
    end
    return math.floor((tonumber(a) or 0) / divisor)
end

--[[
    Mod(a, b)

    Modulo operation.

    Parameters:
        a - Dividend
        b - Divisor

    Returns:
        Remainder (a % b)
]]
function Mod(a, b)
    local divisor = tonumber(b) or 1
    if divisor == 0 then
        error("Division by zero")
    end
    return (tonumber(a) or 0) % divisor
end

-- ============================================================================
-- Jam-Compatible Functions (digit list format)
-- ============================================================================

--[[
    NumberToDigits(n)

    Convert a number to a list of digits (least significant first).
    This is the format used by the original Jam MathRules.

    Parameters:
        n - Number to convert

    Returns:
        Table of digits (reversed order)

    Example:
        NumberToDigits(123) -> {3, 2, 1}
]]
function NumberToDigits(n)
    n = math.abs(tonumber(n) or 0)
    if n == 0 then
        return {0}
    end

    local digits = {}
    while n > 0 do
        table.insert(digits, n % 10)
        n = math.floor(n / 10)
    end
    return digits
end

--[[
    DigitsToNumber(digits)

    Convert a list of digits (least significant first) to a number.

    Parameters:
        digits - Table of digits (reversed order)

    Returns:
        Number

    Example:
        DigitsToNumber({3, 2, 1}) -> 123
]]
function DigitsToNumber(digits)
    if type(digits) ~= "table" then
        return tonumber(digits) or 0
    end

    local result = 0
    local multiplier = 1
    for _, digit in ipairs(digits) do
        result = result + (tonumber(digit) or 0) * multiplier
        multiplier = multiplier * 10
    end
    return result
end

--[[
    AddNumAbs(a, b)

    Add two absolute numbers represented as digit lists.

    This is the Jam-compatible API. In Jam, numbers are represented
    as lists of digits with the least significant digit first.

    Equivalent to Jam:
        rule AddNumAbs { }

    Parameters:
        a - First number (digit list or number)
        b - Second number (digit list or number)

    Returns:
        Sum as digit list (for Jam compatibility) or number

    Examples:
        AddNumAbs({3, 2, 1}, {7, 5}) -> {0, 8, 1}  (123 + 57 = 180)
        AddNumAbs(123, 57) -> 180
]]
function AddNumAbs(a, b)
    -- If inputs are numbers, just add them
    if type(a) == "number" and type(b) == "number" then
        return a + b
    end

    -- Convert to numbers if digit lists
    local num_a = type(a) == "table" and DigitsToNumber(a) or (tonumber(a) or 0)
    local num_b = type(b) == "table" and DigitsToNumber(b) or (tonumber(b) or 0)

    local result = num_a + num_b

    -- Return in same format as input
    if type(a) == "table" or type(b) == "table" then
        return NumberToDigits(result)
    end

    return result
end

-- ============================================================================
-- Comparison Functions
-- ============================================================================

--[[
    IsGreater(a, b)

    Check if a > b.

    Parameters:
        a - First number
        b - Second number

    Returns:
        true if a > b, false otherwise
]]
function IsGreater(a, b)
    return (tonumber(a) or 0) > (tonumber(b) or 0)
end

--[[
    IsLess(a, b)

    Check if a < b.

    Parameters:
        a - First number
        b - Second number

    Returns:
        true if a < b, false otherwise
]]
function IsLess(a, b)
    return (tonumber(a) or 0) < (tonumber(b) or 0)
end

--[[
    IsEqual(a, b)

    Check if a == b.

    Parameters:
        a - First number
        b - Second number

    Returns:
        true if a == b, false otherwise
]]
function IsEqual(a, b)
    return (tonumber(a) or 0) == (tonumber(b) or 0)
end

--[[
    Min(a, b)

    Return minimum of two numbers.

    Parameters:
        a - First number
        b - Second number

    Returns:
        Minimum value
]]
function Min(a, b)
    return math.min(tonumber(a) or 0, tonumber(b) or 0)
end

--[[
    Max(a, b)

    Return maximum of two numbers.

    Parameters:
        a - First number
        b - Second number

    Returns:
        Maximum value
]]
function Max(a, b)
    return math.max(tonumber(a) or 0, tonumber(b) or 0)
end

-- ============================================================================
-- Utility Functions
-- ============================================================================

--[[
    Clamp(value, min_val, max_val)

    Clamp a value to a range.

    Parameters:
        value - Value to clamp
        min_val - Minimum allowed value
        max_val - Maximum allowed value

    Returns:
        Clamped value
]]
function Clamp(value, min_val, max_val)
    value = tonumber(value) or 0
    min_val = tonumber(min_val) or 0
    max_val = tonumber(max_val) or 0
    return math.max(min_val, math.min(max_val, value))
end

--[[
    Abs(n)

    Return absolute value.

    Parameters:
        n - Number

    Returns:
        Absolute value
]]
function Abs(n)
    return math.abs(tonumber(n) or 0)
end

--[[
    Sign(n)

    Return sign of number.

    Parameters:
        n - Number

    Returns:
        -1, 0, or 1
]]
function Sign(n)
    n = tonumber(n) or 0
    if n > 0 then return 1
    elseif n < 0 then return -1
    else return 0
    end
end

--[[
    Round(n)

    Round to nearest integer.

    Parameters:
        n - Number

    Returns:
        Rounded value
]]
function Round(n)
    n = tonumber(n) or 0
    return math.floor(n + 0.5)
end

--[[
    Pow(base, exp)

    Raise base to power exp.

    Parameters:
        base - Base number
        exp - Exponent

    Returns:
        base^exp
]]
function Pow(base, exp)
    return math.pow(tonumber(base) or 0, tonumber(exp) or 0)
end