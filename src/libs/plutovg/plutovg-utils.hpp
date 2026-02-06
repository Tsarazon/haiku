#ifndef PLUTOVG_UTILS_HPP
#define PLUTOVG_UTILS_HPP

#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <vector>

namespace plutovg {

inline constexpr bool is_num(char c) { return c >= '0' && c <= '9'; }
inline constexpr bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline constexpr bool is_alnum(char c) { return is_alpha(c) || is_num(c); }
inline constexpr bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

template<typename T>
constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline constexpr uint8_t alpha(uint32_t c) { return (c >> 24) & 0xff; }
inline constexpr uint8_t red(uint32_t c) { return (c >> 16) & 0xff; }
inline constexpr uint8_t green(uint32_t c) { return (c >> 8) & 0xff; }
inline constexpr uint8_t blue(uint32_t c) { return c & 0xff; }

template<typename T>
struct Array {
    T* data = nullptr;
    int size = 0;
    int capacity = 0;

    void init() { data = nullptr; size = 0; capacity = 0; }
    void clear() { size = 0; }

    void destroy() {
        std::free(data);
        data = nullptr;
        size = 0;
        capacity = 0;
    }

    void ensure(int count) {
        if (size + count > capacity) {
            int new_cap = capacity == 0 ? 8 : capacity;
            while (new_cap < size + count)
                new_cap *= 2;
            data = static_cast<T*>(std::realloc(data, new_cap * sizeof(T)));
            capacity = new_cap;
        }
    }

    void append(const T* items, int count) {
        if (items && count > 0) {
            ensure(count);
            std::memcpy(data + size, items, count * sizeof(T));
            size += count;
        }
    }

    void append(const Array& other) { append(other.data, other.size); }
};

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
    return (a << 24) | (r << 16) | (g << 8) | b;
}

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
        number *= std::powf(10.f, expsign * exponent);
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
