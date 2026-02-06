#ifndef PLUTOVG_FT_RASTER_HPP
#define PLUTOVG_FT_RASTER_HPP

#include "plutovg-ft-types.hpp"

#include <functional>

namespace plutovg::ft {

struct BBox {
    Pos x_min = 0;
    Pos y_min = 0;
    Pos x_max = 0;
    Pos y_max = 0;

    constexpr BBox() = default;
    constexpr BBox(Pos x_min, Pos y_min, Pos x_max, Pos y_max)
        : x_min(x_min), y_min(y_min), x_max(x_max), y_max(y_max) {}
};

struct Outline {
    int n_contours = 0;
    int n_points = 0;
    Vector* points = nullptr;
    char* tags = nullptr;
    int* contours = nullptr;
    char* contours_flag = nullptr;
    int flags = 0;
};

enum class OutlineFlags : int {
    None          = 0x0,
    Owner         = 0x1,
    EvenOddFill   = 0x2,
    ReverseFill   = 0x4
};

enum class CurveTag : int {
    On    = 1,
    Conic = 0,
    Cubic = 2
};

inline CurveTag curve_tag(int flag) {
    return static_cast<CurveTag>(flag & 3);
}

Error outline_check(Outline& outline);
void outline_get_cbox(const Outline& outline, BBox& cbox);

struct Span {
    int x = 0;
    int len = 0;
    int y = 0;
    uint8_t coverage = 0;

    constexpr Span() = default;
    constexpr Span(int x, int len, int y, uint8_t coverage)
        : x(x), len(len), y(y), coverage(coverage) {}
};

using SpanFunc = void(*)(int count, const Span* spans, void* user);

enum class RasterFlag : int {
    Default = 0x0,
    AA      = 0x1,
    Direct  = 0x2,
    Clip    = 0x4
};

struct RasterParams {
    const void* source = nullptr;
    int flags = 0;
    SpanFunc gray_spans = nullptr;
    void* user = nullptr;
    BBox clip_box;
};

void raster_render(const RasterParams& params);

} // namespace plutovg::ft

#endif // PLUTOVG_FT_RASTER_HPP
