#ifndef PLUTOVG_FT_TYPES_HPP
#define PLUTOVG_FT_TYPES_HPP

#include <cstdint>

namespace plutovg::ft {

using Fixed  = int32_t;    // 16.16 fixed-point
using Int    = int32_t;
using UInt   = uint32_t;
using Long   = int64_t;
using ULong  = uint64_t;
using Short  = int16_t;
using Byte   = uint8_t;
using Bool   = uint8_t;
using Error  = int32_t;
using Pos    = int32_t;     // 26.6 or 16.16 fixed-point coordinates

using Int32  = int32_t;
using UInt32 = uint32_t;
using Int64  = int64_t;
using UInt64 = uint64_t;

struct Vector {
    Pos x = 0;
    Pos y = 0;

    constexpr Vector() = default;
    constexpr Vector(Pos x, Pos y) : x(x), y(y) {}
};

inline constexpr Bool to_bool(auto x) { return static_cast<Bool>(x); }

} // namespace plutovg::ft

#endif // PLUTOVG_FT_TYPES_HPP
