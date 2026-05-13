/*
 * KosmCG FreeType backend — internal header (C++20)
 *
 * Implementation-private types and helpers shared between rasterizer
 * translation units. Not part of the public API; do not include from
 * client code.
 */

#pragma once

#include "kosmcg-ft.hpp"

#include <cstdint>
#include <cstddef>

namespace kcg::ft::detail {

// -- pixel configuration --

inline constexpr int     PIXEL_BITS = 8;
inline constexpr int64_t ONE_PIXEL  = 1L << PIXEL_BITS;

// -- internal coordinate types --

using TCoord = int32_t;   // integer pixel coordinate
using TPos   = int64_t;   // sub-pixel coordinate
using TArea  = int64_t;   // cell areas, coordinate products

// -- error codes --

inline constexpr int ErrRaster_Invalid_Mode     = -2;
inline constexpr int ErrRaster_Invalid_Outline  = -1;
inline constexpr int ErrRaster_Invalid_Argument = -3;
inline constexpr int ErrRaster_Memory_Overflow  = -4;
inline constexpr int ErrRaster_OutOfMemory      = -6;

inline constexpr int kMinPoolSize  = 8192;
inline constexpr int kMaxGraySpans = 256;

// -- pixel-coordinate helpers --

constexpr TCoord trunc_pixel(TPos x) { return static_cast<TCoord>(x >> PIXEL_BITS); }
constexpr TCoord fract_pixel(TPos x) { return static_cast<TCoord>(x & (ONE_PIXEL - 1)); }

constexpr TPos upscale(TPos x)
{
    if constexpr (PIXEL_BITS >= 6) return x * (ONE_PIXEL >> 6);
    else                            return x >> (6 - PIXEL_BITS);
}

constexpr TPos downscale(TPos x)
{
    if constexpr (PIXEL_BITS >= 6) return x >> (PIXEL_BITS - 6);
    else                            return x * (64 >> PIXEL_BITS);
}

// Division with corrected positive remainder. Dividend is 64-bit to avoid
// truncation on large coordinates.
inline void ft_div_mod(int64_t dividend, int64_t divisor,
                       int32_t& quotient, int32_t& remainder)
{
    int64_t q = dividend / divisor;
    int64_t r = dividend % divisor;
    if (r < 0) {
        q--;
        r += divisor;
    }
    quotient  = static_cast<int32_t>(q);
    remainder = static_cast<int32_t>(r);
}

// -- cell --

struct TCell {
    int    x;
    int    cover;
    TArea  area;
    TCell* next;
};

using PCell = TCell*;

// -- raster memory overflow exception (replaces setjmp/longjmp) --

struct RasterMemoryOverflow {};

// -- gamma correction table --

class GammaTable {
public:
    // Build for given gamma value (16.16 fixed-point).
    // gamma == 0 leaves the table in identity mode (no correction).
    void build(Fixed gamma_16_16);

    bool is_identity() const { return identity_; }

    uint16_t decode(uint8_t v) const { return decode_[v]; }

    uint8_t encode_linear(uint32_t linear) const {
        if (linear >= 65536u) linear = 65535u;
        return encode_[linear >> 6];   // index by top 10 bits
    }

private:
    bool     identity_ = true;
    uint16_t decode_[256];
    uint8_t  encode_[1024];
};

// -- LCD scan-line buffer --
//
// Holds per-scanline subpixel coverage during LCD rasterization. The
// rasterizer writes coverage into subpixel_coverage as it sweeps, then
// at end-of-scanline the LCD filter pass reads it, applies gamma
// decode + 5-tap FIR + gamma encode, and writes packed RGB triplets
// into pixel_rgb.

struct LcdScanlineBuffer {
    uint8_t* subpixel_coverage = nullptr;  // size = subpixel_width bytes
    uint8_t* pixel_rgb         = nullptr;  // size = pixel_width * 3 bytes
    int      subpixel_width    = 0;
    int      pixel_width       = 0;
    int      current_y         = -1;
    bool     dirty             = false;
};

} // namespace kcg::ft::detail
