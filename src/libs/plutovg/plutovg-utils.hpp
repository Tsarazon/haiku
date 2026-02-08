// plutovg-utils.hpp - Internal parsing and pixel utilities
// C++20

#ifndef PLUTOVG_UTILS_HPP
#define PLUTOVG_UTILS_HPP

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace plutovg {

// -- Character classification --

inline constexpr bool is_num(char c) { return c >= '0' && c <= '9'; }
inline constexpr bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline constexpr bool is_alnum(char c) { return is_alpha(c) || is_num(c); }
inline constexpr bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// -- Pixel component access (premultiplied ARGB) --

inline constexpr uint8_t alpha(uint32_t c) { return (c >> 24) & 0xff; }
inline constexpr uint8_t red(uint32_t c)   { return (c >> 16) & 0xff; }
inline constexpr uint8_t green(uint32_t c) { return (c >> 8) & 0xff; }
inline constexpr uint8_t blue(uint32_t c)  { return c & 0xff; }

inline constexpr uint32_t pack_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

inline uint32_t premultiply_argb(uint32_t color) {
    uint32_t a = alpha(color);
    uint32_t r = red(color);
    uint32_t g = green(color);
    uint32_t b = blue(color);
    if (a != 255) {
        r = (r * a) / 255;
        g = (g * a) / 255;
        b = (b * a) / 255;
    }
    return pack_argb(a, r, g, b);
}

// -- Blend mode pixel operations --
// All inputs/outputs are in [0, 255] range.

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

inline constexpr uint8_t darken(uint8_t a, uint8_t b) {
    return std::min(a, b);
}

inline constexpr uint8_t lighten(uint8_t a, uint8_t b) {
    return std::max(a, b);
}

inline constexpr uint8_t color_dodge(uint8_t a, uint8_t b) {
    if (b == 255) return 255;
    return static_cast<uint8_t>(std::min(255u, (uint32_t(a) * 255) / (255 - b)));
}

inline constexpr uint8_t color_burn(uint8_t a, uint8_t b) {
    if (b == 0) return 0;
    return static_cast<uint8_t>(255 - std::min(255u, ((255u - a) * 255) / b));
}

inline constexpr uint8_t hard_light(uint8_t a, uint8_t b) {
    return overlay(b, a); // same as overlay with swapped args
}

inline uint8_t soft_light(uint8_t a, uint8_t b) {
    float fa = a / 255.0f;
    float fb = b / 255.0f;
    float result = (fb <= 0.5f)
        ? fa - (1.0f - 2.0f * fb) * fa * (1.0f - fa)
        : fa + (2.0f * fb - 1.0f) * (std::sqrt(fa) - fa);
    return static_cast<uint8_t>(std::clamp(result * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline constexpr uint8_t difference(uint8_t a, uint8_t b) {
    return (a > b) ? (a - b) : (b - a);
}

inline constexpr uint8_t exclusion(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(a + b - (2u * a * b) / 255);
}

} // namespace blend_ops

// -- HSL blend operations (work on RGB triples, not per-channel) --

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

/// Source hue, destination saturation and luminosity.
inline void hue(uint8_t sr, uint8_t sg, uint8_t sb,
                uint8_t dr, uint8_t dg, uint8_t db,
                uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({s.h, d.s, d.l}, or_, og, ob);
}

/// Source saturation, destination hue and luminosity.
inline void saturation(uint8_t sr, uint8_t sg, uint8_t sb,
                       uint8_t dr, uint8_t dg, uint8_t db,
                       uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({d.h, s.s, d.l}, or_, og, ob);
}

/// Source hue and saturation, destination luminosity.
inline void color(uint8_t sr, uint8_t sg, uint8_t sb,
                  uint8_t dr, uint8_t dg, uint8_t db,
                  uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({s.h, s.s, d.l}, or_, og, ob);
}

/// Source luminosity, destination hue and saturation.
inline void luminosity(uint8_t sr, uint8_t sg, uint8_t sb,
                       uint8_t dr, uint8_t dg, uint8_t db,
                       uint8_t& or_, uint8_t& og, uint8_t& ob) {
    auto s = rgb_to_hsl(sr, sg, sb);
    auto d = rgb_to_hsl(dr, dg, db);
    hsl_to_rgb({d.h, d.s, s.l}, or_, og, ob);
}

} // namespace hsl_blend_ops

// -- sRGB / Linear RGB color space conversion --

namespace color_space {

/// sRGB byte [0..255] to linear float [0..1].
/// Uses the standard IEC 61966-2-1 transfer function.
inline float srgb_to_linear(uint8_t v) {
    float s = v / 255.0f;
    return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

/// Linear float [0..1] to sRGB byte [0..255].
inline uint8_t linear_to_srgb(float v) {
    float s = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(s * 255.0f + 0.5f, 0.0f, 255.0f));
}

/// Linear float [0..1] to sRGB float [0..255] (pre-quantization, for dithering).
inline float linear_to_srgb_float(float v) {
    float s = (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return std::clamp(s * 255.0f, 0.0f, 255.0f);
}

/// Precomputed LUT: srgb_lut[i] = srgb_to_linear(i).
/// Call init_lut() once at startup, or rely on inline function above.
struct LUT {
    float table[256];

    constexpr LUT() : table{} {
        // Note: not truly constexpr due to std::pow; use runtime init.
    }

    void init() {
        for (int i = 0; i < 256; ++i)
            table[i] = srgb_to_linear(static_cast<uint8_t>(i));
    }

    float operator[](uint8_t i) const { return table[i]; }
};

} // namespace color_space

// -- Ordered dithering (4x4 Bayer matrix) --

namespace dither {

/// Bayer 4x4 threshold matrix, normalized to [-0.5, +0.5] range.
inline constexpr float bayer4x4[4][4] = {
    { -0.46875f,  0.03125f, -0.34375f,  0.15625f },
    {  0.28125f, -0.21875f,  0.40625f, -0.09375f },
    { -0.28125f,  0.21875f, -0.40625f,  0.09375f },
    {  0.46875f, -0.03125f,  0.34375f, -0.15625f }
};

/// Apply ordered dither to a float value in [0..255] before quantization.
inline uint8_t apply(float value, int x, int y) {
    return static_cast<uint8_t>(std::clamp(value + bayer4x4[y & 3][x & 3], 0.0f, 255.0f));
}

/// Apply ordered dither to an RGB triple (values in [0..255] float).
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

    if (it >= end || (*it != '.' && !is_num(*it)))
        return false;

    if (is_num(*it)) {
        do {
            integer = 10.f * integer + (*it++ - '0');
        } while (it < end && is_num(*it));
    }

    if (it < end && *it == '.') {
        ++it;
        if (it >= end || !is_num(*it))
            return false;
        float divisor = 1.f;
        do {
            fraction = 10.f * fraction + (*it++ - '0');
            divisor *= 10.f;
        } while (it < end && is_num(*it));
        fraction /= divisor;
    }

    if (it < end && (*it == 'e' || *it == 'E')) {
        ++it;
        if (it < end && *it == '+') {
            ++it;
        } else if (it < end && *it == '-') {
            ++it;
            expsign = -1;
        }

        if (it >= end || !is_num(*it))
            return false;
        do {
            exponent = 10 * exponent + (*it++ - '0');
        } while (it < end && is_num(*it));
    }

    number = sign * (integer + fraction);
    if (exponent)
        number *= std::pow(10.f, expsign * exponent);
    return number >= -FLT_MAX && number <= FLT_MAX;
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

} // namespace plutovg

#endif // PLUTOVG_UTILS_HPP
