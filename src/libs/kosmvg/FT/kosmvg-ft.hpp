/*
 * KosmVG FreeType backend (C++20)
 *
 * Fixed-point math, CORDIC trigonometry, outline rasterizer, path stroker.
 *
 * Based on FreeType sources:
 *   fttypes.h, fttrigon.h, ftimage.h, ftgrays.c, ftstroke.h
 * Copyright 1996-2014
 * David Turner, Robert Wilhelm, Werner Lemberg.
 * FreeType project license, FTL.TXT.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace kvg::ft {

// -- Scalar types --

using Fixed  = int32_t;   // 16.16 fixed-point
using Pos    = int32_t;   // vectorial coordinate (integer, 16.16, or 26.6)
using Angle  = Fixed;     // 16.16 fixed-point degrees

using Int    = int;
using UInt   = unsigned int;
using Long   = int32_t;
using ULong  = uint32_t;
using Short  = int16_t;
using Byte   = uint8_t;
using Bool   = bool;
using Error  = int;

using Int64  = int64_t;
using UInt64 = uint64_t;
using Int32  = int32_t;
using UInt32 = uint32_t;

struct Vector {
    Pos x;
    Pos y;
};

// -- Utility functions --

template<typename T>
constexpr T ft_min(T a, T b) { return std::min(a, b); }

template<typename T>
constexpr T ft_max(T a, T b) { return std::max(a, b); }

template<typename T>
constexpr T ft_abs(T a) { return a < 0 ? -a : a; }

// Approximate sqrt(x*x+y*y) using alpha max + beta min.
// alpha = 1, beta = 3/8, largest error < 7%.
inline Pos ft_hypot(Pos x, Pos y)
{
    x = ft_abs(x);
    y = ft_abs(y);
    return x > y ? x + (3 * y >> 3)
                 : y + (3 * x >> 3);
}

// -- Fixed-point arithmetic --

Long mul_fix(Long a, Long b);          // (a*b) / 0x10000
Long mul_div(Long a, Long b, Long c);  // (a*b) / c
Long div_fix(Long a, Long b);          // (a*0x10000) / b

// -- Angle constants (16.16 fixed-point degrees) --

inline constexpr Angle angle_pi  = 180L << 16;
inline constexpr Angle angle_2pi = angle_pi * 2;
inline constexpr Angle angle_pi2 = angle_pi / 2;
inline constexpr Angle angle_pi4 = angle_pi / 4;

// -- Trigonometric functions --

Fixed ft_sin(Angle angle);
Fixed ft_cos(Angle angle);
Fixed ft_tan(Angle angle);
Angle ft_atan2(Fixed x, Fixed y);
Angle angle_diff(Angle angle1, Angle angle2);

// -- Vector operations --

void  vector_unit(Vector* vec, Angle angle);
void  vector_rotate(Vector* vec, Angle angle);
Fixed vector_length(Vector* vec);
void  vector_polarize(Vector* vec, Fixed* length, Angle* angle);
void  vector_from_polar(Vector* vec, Fixed length, Angle angle);

// -- Bounding box --

struct BBox {
    Pos xMin, yMin;
    Pos xMax, yMax;
};

// -- Curve tags --

enum class CurveTag : char {
    Conic = 0,
    On    = 1,
    Cubic = 2,
};

inline CurveTag curve_tag(int flag) { return static_cast<CurveTag>(flag & 3); }

// -- Outline flags --

enum class OutlineFlags : int {
    None        = 0x0,
    Owner       = 0x1,
    EvenOddFill = 0x2,
    ReverseFill = 0x4,
};

// -- Outline (RAII) --

struct Outline {
    int      n_contours = 0;
    int      n_points   = 0;

    Vector*  points        = nullptr;
    char*    tags          = nullptr;
    int*     contours      = nullptr;
    char*    contours_flag = nullptr;
    int      flags         = 0;

    struct Storage {
        void* data;
        void operator()(void* /*unused*/) const { std::free(data); }
    };

    std::unique_ptr<void, Storage> storage_;

    Outline() = default;
    Outline(Outline&&) = default;
    Outline& operator=(Outline&&) = default;

    Outline(const Outline&) = delete;
    Outline& operator=(const Outline&) = delete;

    static Outline create(int num_points, int num_contours)
    {
        auto align = [](size_t sz) -> size_t { return (sz + 7u) & ~7u; };

        auto total_pts  = static_cast<size_t>(num_points + num_contours);
        auto n_contours = static_cast<size_t>(num_contours);
        size_t points_sz        = align(total_pts * sizeof(Vector));
        size_t tags_sz          = align(total_pts * sizeof(char));
        size_t contours_sz      = align(n_contours * sizeof(int));
        size_t contours_flag_sz = align(n_contours * sizeof(char));
        size_t total = points_sz + tags_sz + contours_sz + contours_flag_sz;

        void* mem = std::malloc(total);
        auto* data = static_cast<Byte*>(mem);

        Outline o;
        o.storage_ = std::unique_ptr<void, Storage>(mem, Storage{mem});
        o.points        = reinterpret_cast<Vector*>(data);
        o.tags          = reinterpret_cast<char*>(data + points_sz);
        o.contours      = reinterpret_cast<int*>(data + points_sz + tags_sz);
        o.contours_flag = reinterpret_cast<char*>(data + points_sz + tags_sz + contours_sz);
        o.n_points   = 0;
        o.n_contours = 0;
        o.flags      = 0;
        return o;
    }

    explicit operator bool() const { return storage_ != nullptr; }
};

// -- Outline validation --

Error outline_check(Outline* outline);
void  outline_get_cbox(const Outline* outline, BBox* acbox);

// -- Span --

struct Span {
    int           x;
    int           len;
    int           y;
    unsigned char coverage;
};

using SpanFunc = void(*)(int count, const Span* spans, void* user);

// -- Raster flags --

enum class RasterFlags : int {
    Default = 0x0,
    AA      = 0x1,
    Direct  = 0x2,
    Clip    = 0x4,
};

constexpr RasterFlags operator|(RasterFlags a, RasterFlags b) {
    return static_cast<RasterFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr bool operator&(RasterFlags a, RasterFlags b) {
    return (static_cast<int>(a) & static_cast<int>(b)) != 0;
}

// -- Raster parameters --

struct RasterParams {
    const Outline* source    = nullptr;
    RasterFlags    flags     = RasterFlags::Default;
    SpanFunc       gray_spans = nullptr;
    void*          user      = nullptr;
    BBox           clip_box  = {};
};

// -- Raster entry point --

void raster_render(const RasterParams* params);

// -- Monochrome bitmap --

struct MonoBitmap {
    int    width  = 0;
    int    rows   = 0;
    int    pitch  = 0;    // bytes per row (may be negative for top-down)
    Byte*  buffer = nullptr;
};

// -- Monochrome raster parameters --

struct MonoRasterParams {
    const Outline*  source      = nullptr;
    MonoBitmap*     target      = nullptr;   // if non-null, render to 1bpp bitmap
    SpanFunc        mono_spans  = nullptr;   // if non-null, emit spans (coverage 0 or 255)
    void*           user        = nullptr;
    bool            high_precision = false;   // sub-pixel precision (for small ppem)
    BBox            clip_box    = {};         // clip rectangle (span callback mode)
    bool            clip        = false;      // enable clip_box
};

// -- Monochrome raster entry point --

Error raster_render_mono(const MonoRasterParams* params);

// -- Line join style --

enum class LineJoin {
    Round         = 0,
    Bevel         = 1,
    MiterVariable = 2,
    Miter         = MiterVariable,
    MiterFixed    = 3,
};

// -- Line cap style --

enum class LineCap {
    Butt   = 0,
    Round  = 1,
    Square = 2,
};

// -- Stroke border selection --

enum class StrokerBorder {
    Left  = 0,
    Right = 1,
};

// -- Stroker (RAII) --

struct StrokerRec;

class Stroker {
public:
    Stroker();
    ~Stroker();

    Stroker(Stroker&& other) noexcept;
    Stroker& operator=(Stroker&& other) noexcept;

    Stroker(const Stroker&) = delete;
    Stroker& operator=(const Stroker&) = delete;

    void set(Fixed radius, LineCap cap, LineJoin join, Fixed miter_limit);

    Error parse_outline(const Outline* outline);
    Error get_counts(UInt* num_points, UInt* num_contours);
    void  export_outline(Outline* outline);

    explicit operator bool() const { return impl_ != nullptr; }

private:
    std::unique_ptr<StrokerRec> impl_;
};

} // namespace kvg::ft
