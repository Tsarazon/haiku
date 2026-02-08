// plutovg-matrix.cpp - Matrix operations
// C++20

#include "plutovg.hpp"
#include "plutovg-utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace plutovg {

// -- Static factories (non-constexpr, require runtime math) --

Matrix Matrix::rotated(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return {c, s, -s, c, 0, 0};
}

Matrix Matrix::sheared(float shx, float shy) {
    return {1, std::tan(shy), std::tan(shx), 1, 0, 0};
}

// -- In-place mutators (pre-multiply) --

Matrix& Matrix::translate(float tx, float ty) {
    return *this = translated(tx, ty) * *this;
}

Matrix& Matrix::scale(float sx, float sy) {
    return *this = scaled(sx, sy) * *this;
}

Matrix& Matrix::rotate(float radians) {
    return *this = rotated(radians) * *this;
}

Matrix& Matrix::shear(float shx, float shy) {
    return *this = sheared(shx, shy) * *this;
}

// -- Mapping --

void Matrix::map_points(const Point* src, Point* dst, int count) const {
    for (int i = 0; i < count; ++i)
        dst[i] = map(src[i]);
}

Rect Matrix::map_rect(const Rect& src) const {
    Point p[4] = {
        {src.x,         src.y},
        {src.x + src.w, src.y},
        {src.x + src.w, src.y + src.h},
        {src.x,         src.y + src.h},
    };

    map_points(p, p, 4);

    float l = p[0].x, t = p[0].y;
    float r = p[0].x, b = p[0].y;
    for (int i = 1; i < 4; ++i) {
        l = std::min(l, p[i].x);
        r = std::max(r, p[i].x);
        t = std::min(t, p[i].y);
        b = std::max(b, p[i].y);
    }

    return {l, t, r - l, b - t};
}

// -- SVG transform attribute parsing --

namespace {

int parse_matrix_parameters(const char*& it, const char* end,
                            float* values, int required, int optional)
{
    if (!skip_ws_and_delim(it, end, '('))
        return 0;

    int count = 0;
    int max_count = required + optional;
    bool has_trailing_comma = false;
    for (; count < max_count; ++count) {
        if (!parse_number(it, end, values[count]))
            break;
        skip_ws_or_comma(it, end, &has_trailing_comma);
    }

    if (!has_trailing_comma && (count == required || count == max_count)
        && skip_delim(it, end, ')')) {
        return count;
    }

    return 0;
}

} // namespace

std::optional<Matrix> Matrix::parse(const char* data, int length) {
    if (length == -1)
        length = static_cast<int>(std::strlen(data));

    const char* it = data;
    const char* end = it + length;
    float values[6];
    Matrix result;
    bool has_trailing_comma = false;

    skip_ws(it, end);
    while (it < end) {
        if (skip_string(it, end, "matrix")) {
            int count = parse_matrix_parameters(it, end, values, 6, 0);
            if (count == 0)
                return std::nullopt;
            Matrix m{values[0], values[1], values[2], values[3], values[4], values[5]};
            result = m * result;
        } else if (skip_string(it, end, "translate")) {
            int count = parse_matrix_parameters(it, end, values, 1, 1);
            if (count == 0)
                return std::nullopt;
            result.translate(values[0], count == 2 ? values[1] : 0.0f);
        } else if (skip_string(it, end, "scale")) {
            int count = parse_matrix_parameters(it, end, values, 1, 1);
            if (count == 0)
                return std::nullopt;
            result.scale(values[0], count == 2 ? values[1] : values[0]);
        } else if (skip_string(it, end, "rotate")) {
            int count = parse_matrix_parameters(it, end, values, 1, 2);
            if (count == 0)
                return std::nullopt;
            if (count == 3)
                result.translate(values[1], values[2]);
            result.rotate(deg2rad(values[0]));
            if (count == 3)
                result.translate(-values[1], -values[2]);
        } else if (skip_string(it, end, "skewX")) {
            int count = parse_matrix_parameters(it, end, values, 1, 0);
            if (count == 0)
                return std::nullopt;
            result.shear(deg2rad(values[0]), 0);
        } else if (skip_string(it, end, "skewY")) {
            int count = parse_matrix_parameters(it, end, values, 1, 0);
            if (count == 0)
                return std::nullopt;
            result.shear(0, deg2rad(values[0]));
        } else {
            return std::nullopt;
        }

        skip_ws_or_comma(it, end, &has_trailing_comma);
    }

    if (has_trailing_comma)
        return std::nullopt;
    return result;
}

} // namespace plutovg
