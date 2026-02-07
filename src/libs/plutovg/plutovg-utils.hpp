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
