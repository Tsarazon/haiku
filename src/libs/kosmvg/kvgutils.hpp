// kvgutils.hpp — Internal parsing and pixel utilities
// C++20

#ifndef KVGUTILS_HPP
#define KVGUTILS_HPP

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace kvg {

// -- Character classification --

inline constexpr bool is_num(char c) { return c >= '0' && c <= '9'; }
inline constexpr bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline constexpr bool is_alnum(char c) { return is_alpha(c) || is_num(c); }
inline constexpr bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// -- Pixel component access (premultiplied ARGB) --

inline constexpr uint8_t pixel_alpha(uint32_t c) { return static_cast<uint8_t>((c >> 24) & 0xff); }
inline constexpr uint8_t red(uint32_t c)   { return static_cast<uint8_t>((c >> 16) & 0xff); }
inline constexpr uint8_t green(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xff); }
inline constexpr uint8_t blue(uint32_t c)  { return static_cast<uint8_t>(c & 0xff); }

inline constexpr uint32_t pack_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

inline uint32_t premultiply_argb(uint32_t color) {
    uint32_t a = pixel_alpha(color);
    if (a == 255) return color;
    if (a == 0)   return 0;
    uint32_t r = (red(color)   * a + 127) / 255;
    uint32_t g = (green(color) * a + 127) / 255;
    uint32_t b = (blue(color)  * a + 127) / 255;
    return pack_argb(static_cast<uint8_t>(a), static_cast<uint8_t>(r),
                     static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

/// Un-premultiplied RGB from a premultiplied ARGB pixel.
struct UnpremulRGB { uint8_t r, g, b; };

inline UnpremulRGB unpremultiply(uint32_t px) {
    uint8_t a = pixel_alpha(px);
    if (a == 0)   return {0, 0, 0};
    if (a == 255) return {red(px), green(px), blue(px)};
    return {
        static_cast<uint8_t>(std::min(255u, (uint32_t(red(px))   * 255) / a)),
        static_cast<uint8_t>(std::min(255u, (uint32_t(green(px)) * 255) / a)),
        static_cast<uint8_t>(std::min(255u, (uint32_t(blue(px))  * 255) / a))
    };
}

/// Un-premultiplied RGBA in [0..1] float range.
struct UnpremulRGBA_f { float r, g, b, a; };

inline UnpremulRGBA_f unpremultiply_f(uint32_t px) {
    float a = pixel_alpha(px) / 255.0f;
    if (a <= 0.0f) return {0, 0, 0, 0};
    float inv_a = 1.0f / a;
    return {
        std::min(1.0f, (red(px)   / 255.0f) * inv_a),
        std::min(1.0f, (green(px) / 255.0f) * inv_a),
        std::min(1.0f, (blue(px)  / 255.0f) * inv_a),
        a
    };
}

/// BT.709 luminance from straight (non-premultiplied) RGB [0..255].
inline constexpr uint8_t luminance_from_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>((54u * r + 183u * g + 19u * b) >> 8);
}

// -- Blend mode pixel operations --

/// Multiply a premultiplied ARGB pixel by a [0..255] factor.
inline uint32_t byte_mul(uint32_t x, uint32_t a) {
    uint32_t t = (x & 0xff00ff) * a;
    t = (t + ((t >> 8) & 0xff00ff) + 0x800080) >> 8;
    t &= 0xff00ff;

    x = ((x >> 8) & 0xff00ff) * a;
    x = (x + ((x >> 8) & 0xff00ff) + 0x800080);
    x &= 0xff00ff00;
    x |= t;
    return x;
}

namespace blend_ops {

inline constexpr uint8_t multiply(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>((uint32_t(a) * b) / 255);
}

inline constexpr uint8_t screen(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(a + b - (uint32_t(a) * b) / 255);
}

inline constexpr uint8_t overlay(uint8_t a, uint8_t b) {
    return (a <= 127)
        ? static_cast<uint8_t>((2u * a * b) / 255)
        : static_cast<uint8_t>(255 - (2u * (255 - a) * (255 - b)) / 255);
}

inline constexpr uint8_t darken(uint8_t a, uint8_t b) { return std::min(a, b); }
inline constexpr uint8_t lighten(uint8_t a, uint8_t b) { return std::max(a, b); }

inline constexpr uint8_t color_dodge(uint8_t a, uint8_t b) {
    if (b == 255) return 255;
    return static_cast<uint8_t>(std::min(255u, (uint32_t(a) * 255) / (255 - b)));
}

inline constexpr uint8_t color_burn(uint8_t a, uint8_t b) {
    if (b == 0) return 0;
    return static_cast<uint8_t>(255 - std::min(255u, ((255u - a) * 255) / b));
}

inline constexpr uint8_t hard_light(uint8_t a, uint8_t b) { return overlay(b, a); }

/// W3C Compositing spec §13.11.8: D(Cb) helper for soft-light.
inline float soft_light_d(float cb) {
    return (cb <= 0.25f)
        ? ((16.0f * cb - 12.0f) * cb + 4.0f) * cb
        : std::sqrt(cb);
}

inline uint8_t soft_light(uint8_t a, uint8_t b) {
    float cb = a / 255.0f;  // backdrop
    float cs = b / 255.0f;  // source
    float result = (cs <= 0.5f)
        ? cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb)
        : cb + (2.0f * cs - 1.0f) * (soft_light_d(cb) - cb);
    return static_cast<uint8_t>(std::clamp(result * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline constexpr uint8_t difference(uint8_t a, uint8_t b) {
    return (a > b) ? (a - b) : (b - a);
}

inline constexpr uint8_t exclusion(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(a + b - (2u * a * b) / 255);
}

} // namespace blend_ops

// -- Float [0,1] blend mode operations --
// Used by the linear-space compositing pipeline to avoid quantisation
// round-trips through uint8. All inputs and outputs are in [0,1].

namespace blend_ops_f {

inline constexpr float multiply(float a, float b) { return a * b; }

inline constexpr float screen(float a, float b) { return a + b - a * b; }

inline constexpr float overlay(float a, float b) {
    return (a <= 0.5f)
        ? 2.0f * a * b
        : 1.0f - 2.0f * (1.0f - a) * (1.0f - b);
}

inline constexpr float darken(float a, float b) { return std::min(a, b); }
inline constexpr float lighten(float a, float b) { return std::max(a, b); }

inline constexpr float color_dodge(float a, float b) {
    if (a <= 0.0f) return 0.0f;
    if (b >= 1.0f) return 1.0f;
    return std::min(1.0f, a / (1.0f - b));
}

inline constexpr float color_burn(float a, float b) {
    if (b <= 0.0f) return 0.0f;
    if (a >= 1.0f) return 1.0f;
    return 1.0f - std::min(1.0f, (1.0f - a) / b);
}

inline constexpr float hard_light(float a, float b) { return overlay(b, a); }

inline float soft_light(float cb, float cs) {
    if (cs <= 0.5f)
        return cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb);
    float d = (cb <= 0.25f)
        ? ((16.0f * cb - 12.0f) * cb + 4.0f) * cb
        : std::sqrt(cb);
    return cb + (2.0f * cs - 1.0f) * (d - cb);
}

inline constexpr float difference(float a, float b) { return std::abs(a - b); }

inline constexpr float exclusion(float a, float b) { return a + b - 2.0f * a * b; }

inline constexpr float add(float a, float b) { return std::min(1.0f, a + b); }

} // namespace blend_ops_f

// -- HSL blend operations --

namespace hsl_blend_ops {

struct HSL { float h, s, l; };

inline HSL rgb_to_hsl(uint8_t r8, uint8_t g8, uint8_t b8) {
    float r = r8 / 255.0f, g = g8 / 255.0f, b = b8 / 255.0f;
    float max_c = std::max({r, g, b});
    float min_c = std::min({r, g, b});
    float l = (max_c + min_c) * 0.5f;
    if (max_c == min_c)
        return {0.0f, 0.0f, l};
    float d = max_c - min_c;
    float s = (l > 0.5f) ? d / (2.0f - max_c - min_c) : d / (max_c + min_c);
    float h = 0.0f;
    if (max_c == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (max_c == g) h = (b - r) / d + 2.0f;
    else                 h = (r - g) / d + 4.0f;
    return {h / 6.0f, s, l};
}

inline float hue_to_rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f)         return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

inline void hsl_to_rgb(HSL hsl, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (hsl.s == 0.0f) {
        r = g = b = static_cast<uint8_t>(std::clamp(hsl.l * 255.0f + 0.5f, 0.0f, 255.0f));
        return;
    }
    float q = (hsl.l < 0.5f) ? hsl.l * (1.0f + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
    float p = 2.0f * hsl.l - q;
    r = static_cast<uint8_t>(std::clamp(hue_to_rgb(p, q, hsl.h + 1.0f / 3.0f) * 255.0f + 0.5f, 0.0f, 255.0f));
    g = static_cast<uint8_t>(std::clamp(hue_to_rgb(p, q, hsl.h)               * 255.0f + 0.5f, 0.0f, 255.0f));
    b = static_cast<uint8_t>(std::clamp(hue_to_rgb(p, q, hsl.h - 1.0f / 3.0f) * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline void hue(uint8_t sr, uint8_t sg, uint8_t sb,
                uint8_t dr, uint8_t dg, uint8_t db,
                uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({s.h, d.s, d.l}, or_, og, ob);
}

inline void saturation(uint8_t sr, uint8_t sg, uint8_t sb,
                       uint8_t dr, uint8_t dg, uint8_t db,
                       uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({d.h, s.s, d.l}, or_, og, ob);
}

inline void color(uint8_t sr, uint8_t sg, uint8_t sb,
                  uint8_t dr, uint8_t dg, uint8_t db,
                  uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({s.h, s.s, d.l}, or_, og, ob);
}

inline void luminosity(uint8_t sr, uint8_t sg, uint8_t sb,
                       uint8_t dr, uint8_t dg, uint8_t db,
                       uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({d.h, d.s, s.l}, or_, og, ob);
}

} // namespace hsl_blend_ops

// -- Float [0,1] HSL blend operations --
// Used by the linear-space compositing pipeline. Inputs/outputs are
// straight RGB floats in [0,1] (in whichever perceptual space the
// caller chooses — the HSL math is the same).

namespace hsl_blend_ops_f {

using hsl_blend_ops::HSL;

inline HSL rgb_to_hsl(float r, float g, float b) {
    float max_c = std::max({r, g, b});
    float min_c = std::min({r, g, b});
    float l = (max_c + min_c) * 0.5f;
    if (max_c == min_c)
        return {0.0f, 0.0f, l};
    float d = max_c - min_c;
    float s = (l > 0.5f) ? d / (2.0f - max_c - min_c) : d / (max_c + min_c);
    float h = 0.0f;
    if (max_c == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (max_c == g) h = (b - r) / d + 2.0f;
    else                 h = (r - g) / d + 4.0f;
    return {h / 6.0f, s, l};
}

inline void hsl_to_rgb(HSL hsl, float& r, float& g, float& b) {
    using hsl_blend_ops::hue_to_rgb;
    if (hsl.s == 0.0f) {
        r = g = b = hsl.l;
        return;
    }
    float q = (hsl.l < 0.5f) ? hsl.l * (1.0f + hsl.s)
                              : hsl.l + hsl.s - hsl.l * hsl.s;
    float p = 2.0f * hsl.l - q;
    r = hue_to_rgb(p, q, hsl.h + 1.0f / 3.0f);
    g = hue_to_rgb(p, q, hsl.h);
    b = hue_to_rgb(p, q, hsl.h - 1.0f / 3.0f);
}

inline void hue(float sr, float sg, float sb,
                float dr, float dg, float db,
                float& or_, float& og, float& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({s.h, d.s, d.l}, or_, og, ob);
}

inline void saturation(float sr, float sg, float sb,
                       float dr, float dg, float db,
                       float& or_, float& og, float& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({d.h, s.s, d.l}, or_, og, ob);
}

inline void color(float sr, float sg, float sb,
                  float dr, float dg, float db,
                  float& or_, float& og, float& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({s.h, s.s, d.l}, or_, og, ob);
}

inline void luminosity(float sr, float sg, float sb,
                       float dr, float dg, float db,
                       float& or_, float& og, float& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({d.h, d.s, s.l}, or_, og, ob);
}

} // namespace hsl_blend_ops_f

// -- sRGB / Linear RGB color space conversion --

namespace color_space_util {

// Float [0,1] ↔ float [0,1] conversions for gradient interpolation
// in linear space. Clamped at boundaries for robustness.

inline float srgb_to_linear_f(float s) {
    if (s <= 0.0f) return 0.0f;
    if (s >= 1.0f) return 1.0f;
    return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb_f(float v) {
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// uint8 sRGB → float linear (used by blend pipeline LUT)

inline float srgb_to_linear(uint8_t v) {
    float s = v / 255.0f;
    return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline uint8_t linear_to_srgb(float v) {
    float s = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(s * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline float linear_to_srgb_float(float v) {
    float s = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return std::clamp(s * 255.0f, 0.0f, 255.0f);
}

struct LUT {
    float table[256];

    constexpr LUT() : table{} {}

    void init() {
        for (int i = 0; i < 256; ++i)
            table[i] = srgb_to_linear(static_cast<uint8_t>(i));
    }

    float operator[](uint8_t i) const { return table[i]; }
};

} // namespace color_space_util

// -- Ordered dithering (4x4 Bayer matrix) --

namespace dither {

inline constexpr float bayer4x4[4][4] = {
    { -0.46875f,  0.03125f, -0.34375f,  0.15625f },
    {  0.28125f, -0.21875f,  0.40625f, -0.09375f },
    { -0.28125f,  0.21875f, -0.40625f,  0.09375f },
    {  0.46875f, -0.03125f,  0.34375f, -0.15625f }
};

inline uint8_t apply(float value, int x, int y) {
    return static_cast<uint8_t>(std::clamp(value + bayer4x4[y & 3][x & 3], 0.0f, 255.0f));
}

inline void apply_rgb(float r, float g, float b, int x, int y,
                      uint8_t& or_, uint8_t& og, uint8_t& ob) {
    float d = bayer4x4[y & 3][x & 3];
    or_ = static_cast<uint8_t>(std::clamp(r + d, 0.0f, 255.0f));
    og = static_cast<uint8_t>(std::clamp(g + d, 0.0f, 255.0f));
    ob = static_cast<uint8_t>(std::clamp(b + d, 0.0f, 255.0f));
}

} // namespace dither

// -- SVG / number parsing --

inline bool parse_number(const char*& it, const char* end, float& number) {
    const char* start = it;

    float integer = 0;
    float fraction = 0;
    float exponent = 0;
    int sign = 1;
    int expsign = 1;

    if (it < end && *it == '+') {
        ++it;
    } else if (it < end && *it == '-') {
        ++it;
        sign = -1;
    }

    if (it >= end || (*it != '.' && !is_num(*it))) {
        it = start;
        return false;
    }

    if (is_num(*it)) {
        do {
            integer = 10.f * integer + (*it++ - '0');
        } while (it < end && is_num(*it));
    }

    if (it < end && *it == '.') {
        ++it;
        if (it >= end || !is_num(*it)) {
            it = start;
            return false;
        }
        float divisor = 1.f;
        do {
            fraction = 10.f * fraction + (*it++ - '0');
            divisor *= 10.f;
        } while (it < end && is_num(*it));
        fraction /= divisor;
    }

    if (it < end && (*it == 'e' || *it == 'E')) {
        const char* exp_start = it;
        ++it;
        if (it < end && *it == '+') {
            ++it;
        } else if (it < end && *it == '-') {
            ++it;
            expsign = -1;
        }

        if (it >= end || !is_num(*it)) {
            // Malformed exponent — backtrack to before 'e'/'E',
            // return the mantissa successfully.
            it = exp_start;
            float result = static_cast<float>(sign) * (integer + fraction);
            if (!(result >= -FLT_MAX && result <= FLT_MAX))
                return false;
            number = result;
            return true;
        }
        do {
            exponent = 10 * exponent + (*it++ - '0');
        } while (it < end && is_num(*it));
    }

    float result = static_cast<float>(sign) * (integer + fraction);
    if (exponent)
        result *= std::pow(10.f, static_cast<float>(expsign) * exponent);
    if (!(result >= -FLT_MAX && result <= FLT_MAX))
        return false;
    number = result;
    return true;
}

inline bool skip_delim(const char*& it, const char* end, char delim) {
    if (it < end && *it == delim) {
        ++it;
        return true;
    }
    return false;
}

inline bool skip_string(const char*& it, const char* end, const char* data) {
    const char* start = it;
    while (it < end && *data && *it == *data) {
        ++data;
        ++it;
    }
    if (*data == '\0')
        return true;
    it = start;
    return false;
}

inline bool skip_ws(const char*& it, const char* end) {
    while (it < end && is_ws(*it))
        ++it;
    return it < end;
}

inline bool skip_ws_and_delim(const char*& it, const char* end, char delim) {
    const char* start = it;
    if (skip_ws(it, end)) {
        if (!skip_delim(it, end, delim)) {
            it = start;
            return false;
        }
        skip_ws(it, end);
    } else {
        return false;
    }
    return it < end;
}

inline bool skip_ws_and_comma(const char*& it, const char* end) {
    return skip_ws_and_delim(it, end, ',');
}

inline bool skip_ws_or_delim(const char*& it, const char* end, char delim, bool* has_delim = nullptr) {
    const char* start = it;
    if (has_delim)
        *has_delim = false;
    if (skip_ws(it, end)) {
        if (skip_delim(it, end, delim)) {
            if (has_delim)
                *has_delim = true;
            skip_ws(it, end);
        }
    }
    if (it == start)
        return false;
    return it < end;
}

inline bool skip_ws_or_comma(const char*& it, const char* end, bool* has_comma = nullptr) {
    return skip_ws_or_delim(it, end, ',', has_comma);
}

// -- Internal error state --
// Error is defined in kosmvg.hpp; forward-declared here so kvgutils.hpp
// stays self-contained for the parsing/pixel utilities above.

enum class Error;

namespace detail {
void set_last_error(Error e);
void clear_last_error();
} // namespace detail

} // namespace kvg

#endif // KVGUTILS_HPP
