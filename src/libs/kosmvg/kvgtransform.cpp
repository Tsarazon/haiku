// kvgtransform.cpp — AffineTransform implementation
// C++20

#include "kosmvg.hpp"
#include "kvgutils.hpp"

#include <cmath>
#include <algorithm>

namespace kvg {

AffineTransform AffineTransform::rotated(float radians) {
    float cs = std::cos(radians);
    float sn = std::sin(radians);
    return {cs, sn, -sn, cs, 0, 0};
}

AffineTransform AffineTransform::sheared(float shx, float shy) {
    return {1, shy, shx, 1, 0, 0};
}

AffineTransform& AffineTransform::translate(float x, float y) {
    *this = *this * translated(x, y);
    return *this;
}

AffineTransform& AffineTransform::scale(float sx, float sy) {
    *this = *this * scaled(sx, sy);
    return *this;
}

AffineTransform& AffineTransform::rotate(float radians) {
    *this = *this * rotated(radians);
    return *this;
}

AffineTransform& AffineTransform::shear(float shx, float shy) {
    *this = *this * sheared(shx, shy);
    return *this;
}

void AffineTransform::apply(const Point* src, Point* dst, int count) const {
    for (int i = 0; i < count; ++i)
        dst[i] = apply(src[i]);
}

Rect AffineTransform::apply_to_rect(const Rect& r) const {
    Point corners[4] = {
        apply({r.min_x(), r.min_y()}),
        apply({r.max_x(), r.min_y()}),
        apply({r.max_x(), r.max_y()}),
        apply({r.min_x(), r.max_y()})
    };
    float min_x = corners[0].x, max_x = corners[0].x;
    float min_y = corners[0].y, max_y = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }
    return {min_x, min_y, max_x - min_x, max_y - min_y};
}

// -- SVG transform attribute parser --
// Supports: matrix, translate, scale, rotate, skewX, skewY
// Concatenates transforms left-to-right (SVG spec).

std::optional<AffineTransform> AffineTransform::parse_svg(std::string_view svg) {
    const char* it = svg.data();
    const char* end = it + svg.size();

    AffineTransform result = identity();

    auto skip = [&] {
        while (it < end && (is_ws(*it) || *it == ','))
            ++it;
    };

    auto expect_char = [&](char c) -> bool {
        skip();
        if (it < end && *it == c) { ++it; return true; }
        return false;
    };

    auto read_float = [&](float& v) -> bool {
        skip();
        return parse_number(it, end, v);
    };

    auto read_floats = [&](float* out, int min_count, int max_count) -> int {
        int n = 0;
        for (int i = 0; i < max_count; ++i) {
            float v;
            if (!read_float(v)) break;
            out[n++] = v;
            skip();
            if (it < end && *it == ',') ++it;
        }
        return n >= min_count ? n : -1;
    };

    while (it < end) {
        skip();
        if (it >= end) break;

        AffineTransform t = identity();
        float args[6];

        if (skip_string(it, end, "matrix")) {
            if (!expect_char('(')) return std::nullopt;
            if (read_floats(args, 6, 6) < 0) return std::nullopt;
            if (!expect_char(')')) return std::nullopt;
            t = {args[0], args[1], args[2], args[3], args[4], args[5]};
        }
        else if (skip_string(it, end, "translate")) {
            if (!expect_char('(')) return std::nullopt;
            int n = read_floats(args, 1, 2);
            if (n < 0) return std::nullopt;
            if (!expect_char(')')) return std::nullopt;
            float tx = args[0];
            float ty = (n >= 2) ? args[1] : 0.0f;
            t = translated(tx, ty);
        }
        else if (skip_string(it, end, "scale")) {
            if (!expect_char('(')) return std::nullopt;
            int n = read_floats(args, 1, 2);
            if (n < 0) return std::nullopt;
            if (!expect_char(')')) return std::nullopt;
            float sx = args[0];
            float sy = (n >= 2) ? args[1] : sx;
            t = scaled(sx, sy);
        }
        else if (skip_string(it, end, "rotate")) {
            if (!expect_char('(')) return std::nullopt;
            int n = read_floats(args, 1, 3);
            if (n < 0) return std::nullopt;
            if (!expect_char(')')) return std::nullopt;
            float angle = deg2rad(args[0]);
            if (n == 3) {
                float cx = args[1], cy = args[2];
                t = translated(cx, cy);
                t.rotate(angle);
                t.translate(-cx, -cy);
            } else {
                t = rotated(angle);
            }
        }
        else if (skip_string(it, end, "skewX")) {
            if (!expect_char('(')) return std::nullopt;
            if (read_floats(args, 1, 1) < 0) return std::nullopt;
            if (!expect_char(')')) return std::nullopt;
            t = {1, 0, std::tan(deg2rad(args[0])), 1, 0, 0};
        }
        else if (skip_string(it, end, "skewY")) {
            if (!expect_char('(')) return std::nullopt;
            if (read_floats(args, 1, 1) < 0) return std::nullopt;
            if (!expect_char(')')) return std::nullopt;
            t = {1, std::tan(deg2rad(args[0])), 0, 1, 0, 0};
        }
        else {
            return std::nullopt;
        }

        result = result * t;
    }

    return result;
}

} // namespace kvg
