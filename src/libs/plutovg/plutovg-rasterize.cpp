// plutovg-rasterize.cpp - Span buffer operations and rasterization
// C++20

#include "plutovg-private.hpp"

#include "plutovg-ft-raster.h"
#include "plutovg-ft-stroker.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <span>

namespace plutovg {

// -- SpanBuffer operations --

void span_buffer_init_rect(SpanBuffer& buf, int x, int y, int width, int height) {
    buf.spans.clear();
    buf.spans.resize(height);
    for (int i = 0; i < height; ++i) {
        buf.spans[i] = {x, width, y + i, 255};
    }
    buf.bounds = {x, y, width, height};
}

void span_buffer_init_region(SpanBuffer& buf, std::span<const IntRect> rects) {
    buf.reset();
    if (rects.empty()) return;

    // Collect all scanline contributions: for each rect, for each Y in [y, y+h),
    // we have a horizontal span [x, x+w). We sort by Y then X, and merge overlapping.

    struct ScanSpan { int y, x0, x1; };
    std::vector<ScanSpan> raw_spans;
    raw_spans.reserve(rects.size() * 4); // rough estimate

    int min_y = INT_MAX, max_y = INT_MIN;
    int min_x = INT_MAX, max_x = INT_MIN;

    for (const auto& r : rects) {
        if (r.empty()) continue;
        for (int y = r.y; y < r.y + r.h; ++y)
            raw_spans.push_back({y, r.x, r.x + r.w});
        min_y = std::min(min_y, r.y);
        max_y = std::max(max_y, r.y + r.h - 1);
        min_x = std::min(min_x, r.x);
        max_x = std::max(max_x, r.x + r.w);
    }

    if (raw_spans.empty()) return;

    // Sort by Y, then by X.
    std::sort(raw_spans.begin(), raw_spans.end(), [](const ScanSpan& a, const ScanSpan& b) {
        return (a.y != b.y) ? (a.y < b.y) : (a.x0 < b.x0);
    });

    // Merge overlapping/adjacent spans on same scanline.
    buf.spans.reserve(raw_spans.size());
    int cur_y = raw_spans[0].y;
    int cur_x0 = raw_spans[0].x0;
    int cur_x1 = raw_spans[0].x1;

    for (size_t i = 1; i < raw_spans.size(); ++i) {
        const auto& s = raw_spans[i];
        if (s.y == cur_y && s.x0 <= cur_x1) {
            // Overlapping or adjacent on same scanline — extend.
            cur_x1 = std::max(cur_x1, s.x1);
        } else {
            // Emit previous span.
            buf.spans.push_back({cur_x0, cur_x1 - cur_x0, cur_y, 255});
            cur_y = s.y;
            cur_x0 = s.x0;
            cur_x1 = s.x1;
        }
    }
    // Emit last span.
    buf.spans.push_back({cur_x0, cur_x1 - cur_x0, cur_y, 255});

    buf.bounds = {min_x, min_y, max_x - min_x, max_y - min_y + 1};
}

void span_buffer_copy(SpanBuffer& dst, const SpanBuffer& src) {
    dst.spans = src.spans;
    dst.bounds = src.bounds;
}

bool span_buffer_contains(const SpanBuffer& buf, float x, float y) {
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    for (const auto& span : buf.spans) {
        if (span.y != iy)
            continue;
        if (ix >= span.x && ix < span.x + span.len)
            return true;
    }
    return false;
}

void span_buffer_extents(const SpanBuffer& buf, Rect& extents) {
    // Compute from spans (const version, no caching).
    if (buf.spans.empty()) {
        extents = {};
        return;
    }

    int x1 = INT_MAX;
    int y1 = buf.spans.front().y;
    int x2 = 0;
    int y2 = buf.spans.back().y;
    for (const auto& span : buf.spans) {
        if (span.x < x1) x1 = span.x;
        if (span.x + span.len > x2) x2 = span.x + span.len;
    }

    extents.x = static_cast<float>(x1);
    extents.y = static_cast<float>(y1);
    extents.w = static_cast<float>(x2 - x1);
    extents.h = static_cast<float>(y2 - y1 + 1);
}

void span_buffer_intersect(SpanBuffer& dst, const SpanBuffer& a, const SpanBuffer& b) {
    dst.reset();
    dst.spans.reserve(std::max(a.spans.size(), b.spans.size()));

    auto* a_it = a.spans.data();
    auto* a_end = a_it + a.spans.size();
    auto* b_it = b.spans.data();
    auto* b_end = b_it + b.spans.size();

    while (a_it < a_end && b_it < b_end) {
        if (b_it->y > a_it->y) {
            ++a_it;
            continue;
        }
        if (a_it->y != b_it->y) {
            ++b_it;
            continue;
        }

        int ax1 = a_it->x;
        int ax2 = ax1 + a_it->len;
        int bx1 = b_it->x;
        int bx2 = bx1 + b_it->len;

        if (bx1 < ax1 && bx2 < ax1) { ++b_it; continue; }
        if (ax1 < bx1 && ax2 < bx1) { ++a_it; continue; }

        int x = std::max(ax1, bx1);
        int len = std::min(ax2, bx2) - x;
        if (len > 0) {
            dst.spans.push_back({
                x, len, a_it->y,
                static_cast<unsigned char>((a_it->coverage * b_it->coverage) / 255)
            });
        }

        if (ax2 < bx2)
            ++a_it;
        else
            ++b_it;
    }
}

// -- FT outline helpers --

// Forward declarations for exposed functions (defined below).
PVG_FT_Outline* ft_outline_create(int points, int contours);
void ft_outline_destroy(PVG_FT_Outline* outline);
PVG_FT_Outline* ft_outline_from_path(const Path& path, const Matrix& matrix);
PVG_FT_Outline* ft_outline_stroke(const Path& path, const Matrix& matrix,
                                    const StrokeData& stroke_data);

namespace {

constexpr size_t align_size(size_t size) {
    return (size + 7u) & ~7u;
}

inline PVG_FT_Pos ft_coord(float x) {
    return static_cast<PVG_FT_Pos>(std::round(x * 64.0f));
}

void ft_outline_move_to(PVG_FT_Outline* ft, float x, float y) {
    ft->points[ft->n_points].x = ft_coord(x);
    ft->points[ft->n_points].y = ft_coord(y);
    ft->tags[ft->n_points] = PVG_FT_CURVE_TAG_ON;
    if (ft->n_points) {
        ft->contours[ft->n_contours] = ft->n_points - 1;
        ft->n_contours++;
    }
    ft->contours_flag[ft->n_contours] = 1;
    ft->n_points++;
}

void ft_outline_line_to(PVG_FT_Outline* ft, float x, float y) {
    ft->points[ft->n_points].x = ft_coord(x);
    ft->points[ft->n_points].y = ft_coord(y);
    ft->tags[ft->n_points] = PVG_FT_CURVE_TAG_ON;
    ft->n_points++;
}

void ft_outline_cubic_to(PVG_FT_Outline* ft,
                         float x1, float y1, float x2, float y2, float x3, float y3) {
    ft->points[ft->n_points].x = ft_coord(x1);
    ft->points[ft->n_points].y = ft_coord(y1);
    ft->tags[ft->n_points] = PVG_FT_CURVE_TAG_CUBIC;
    ft->n_points++;

    ft->points[ft->n_points].x = ft_coord(x2);
    ft->points[ft->n_points].y = ft_coord(y2);
    ft->tags[ft->n_points] = PVG_FT_CURVE_TAG_CUBIC;
    ft->n_points++;

    ft->points[ft->n_points].x = ft_coord(x3);
    ft->points[ft->n_points].y = ft_coord(y3);
    ft->tags[ft->n_points] = PVG_FT_CURVE_TAG_ON;
    ft->n_points++;
}

void ft_outline_close(PVG_FT_Outline* ft) {
    ft->contours_flag[ft->n_contours] = 0;
    int index = ft->n_contours ? ft->contours[ft->n_contours - 1] + 1 : 0;
    if (index == ft->n_points)
        return;
    ft->points[ft->n_points].x = ft->points[index].x;
    ft->points[ft->n_points].y = ft->points[index].y;
    ft->tags[ft->n_points] = PVG_FT_CURVE_TAG_ON;
    ft->n_points++;
}

void ft_outline_end(PVG_FT_Outline* ft) {
    if (ft->n_points) {
        ft->contours[ft->n_contours] = ft->n_points - 1;
        ft->n_contours++;
    }
}

// Forward declaration for stroke conversion.
PVG_FT_Outline* ft_outline_convert_stroke(const Path::Impl& path, const Matrix& matrix,
                                           const StrokeData& stroke_data);

PVG_FT_Outline* ft_outline_convert(const Path::Impl& path, const Matrix& matrix,
                                    const StrokeData* stroke_data) {
    if (stroke_data)
        return ft_outline_convert_stroke(path, matrix, *stroke_data);

    const auto* elems = path.elements.data();
    int size = static_cast<int>(path.elements.size());
    int index = 0;

    auto* outline = ft_outline_create(path.num_points, path.num_contours);
    while (index < size) {
        auto cmd = elems[index].header.command;
        int len = elems[index].header.length;
        const Point* pts = &elems[index + 1].point;

        switch (cmd) {
        case PathCommand::MoveTo: {
            Point p = matrix.map(pts[0]);
            ft_outline_move_to(outline, p.x, p.y);
            break;
        }
        case PathCommand::LineTo: {
            Point p = matrix.map(pts[0]);
            ft_outline_line_to(outline, p.x, p.y);
            break;
        }
        case PathCommand::CubicTo: {
            Point mapped[3];
            matrix.map_points(pts, mapped, 3);
            ft_outline_cubic_to(outline,
                mapped[0].x, mapped[0].y,
                mapped[1].x, mapped[1].y,
                mapped[2].x, mapped[2].y);
            break;
        }
        case PathCommand::Close:
            ft_outline_close(outline);
            break;
        }
        index += len;
    }

    ft_outline_end(outline);
    return outline;
}

/// Rebuild a Path from raw Impl elements (used to bridge Impl -> public Path API).
Path path_from_impl(const Path::Impl& impl) {
    Path p;
    const auto* el = impl.elements.data();
    int sz = static_cast<int>(impl.elements.size());
    p.reserve(sz);
    int idx = 0;
    while (idx < sz) {
        auto cmd = el[idx].header.command;
        int ln = el[idx].header.length;
        const Point* pts = &el[idx + 1].point;
        switch (cmd) {
        case PathCommand::MoveTo:  p.move_to(pts[0].x, pts[0].y); break;
        case PathCommand::LineTo:  p.line_to(pts[0].x, pts[0].y); break;
        case PathCommand::CubicTo: p.cubic_to(pts[0].x, pts[0].y, pts[1].x, pts[1].y, pts[2].x, pts[2].y); break;
        case PathCommand::Close:   p.close(); break;
        }
        idx += ln;
    }
    return p;
}

PVG_FT_Outline* ft_outline_convert_dash(const Path::Impl& path, const Matrix& matrix,
                                         const StrokeDash& dash) {
    if (dash.array.empty())
        return ft_outline_convert(path, matrix, nullptr);

    Path src = path_from_impl(path);
    Path dashed = src.clone_dashed(dash.offset, {dash.array.data(), dash.array.size()});
    return ft_outline_from_path(dashed, matrix);
}

PVG_FT_Outline* ft_outline_convert_stroke(const Path::Impl& path, const Matrix& matrix,
                                           const StrokeData& stroke_data) {
    double scale_x = std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b);
    double scale_y = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
    double scale = std::hypot(scale_x, scale_y) / sqrt2;
    double width = stroke_data.style.width * scale;

    auto ftWidth = static_cast<PVG_FT_Fixed>(width * 0.5 * (1 << 6));
    auto ftMiterLimit = static_cast<PVG_FT_Fixed>(stroke_data.style.miter_limit * (1 << 16));

    PVG_FT_Stroker_LineCap ftCap;
    switch (stroke_data.style.cap) {
    case LineCap::Square: ftCap = PVG_FT_STROKER_LINECAP_SQUARE; break;
    case LineCap::Round:  ftCap = PVG_FT_STROKER_LINECAP_ROUND; break;
    default:              ftCap = PVG_FT_STROKER_LINECAP_BUTT; break;
    }

    PVG_FT_Stroker_LineJoin ftJoin;
    switch (stroke_data.style.join) {
    case LineJoin::Bevel: ftJoin = PVG_FT_STROKER_LINEJOIN_BEVEL; break;
    case LineJoin::Round: ftJoin = PVG_FT_STROKER_LINEJOIN_ROUND; break;
    default:              ftJoin = PVG_FT_STROKER_LINEJOIN_MITER_FIXED; break;
    }

    PVG_FT_Stroker stroker;
    PVG_FT_Stroker_New(&stroker);
    PVG_FT_Stroker_Set(stroker, ftWidth, ftCap, ftJoin, ftMiterLimit);

    PVG_FT_Outline* outline = ft_outline_convert_dash(path, matrix, stroke_data.dash);
    PVG_FT_Stroker_ParseOutline(stroker, outline);

    PVG_FT_UInt points;
    PVG_FT_UInt contours;
    PVG_FT_Stroker_GetCounts(stroker, &points, &contours);

    PVG_FT_Outline* stroke_outline = ft_outline_create(points, contours);
    PVG_FT_Stroker_Export(stroker, stroke_outline);

    PVG_FT_Stroker_Done(stroker);
    ft_outline_destroy(outline);
    return stroke_outline;
}

void spans_generation_callback(int count, const PVG_FT_Span* ft_spans, void* user) {
    auto* buf = static_cast<SpanBuffer*>(user);
    size_t old_size = buf->spans.size();
    buf->spans.resize(old_size + count);
    for (int i = 0; i < count; ++i) {
        buf->spans[old_size + i] = {
            ft_spans[i].x,
            ft_spans[i].len,
            ft_spans[i].y,
            ft_spans[i].coverage
        };
    }
}

} // namespace

// -- Exposed FT outline functions --

PVG_FT_Outline* ft_outline_create(int points, int contours) {
    size_t points_size = align_size((points + contours) * sizeof(PVG_FT_Vector));
    size_t tags_size = align_size((points + contours) * sizeof(char));
    size_t contours_size = align_size(contours * sizeof(int));
    size_t contours_flag_size = align_size(contours * sizeof(char));
    size_t total = points_size + tags_size + contours_size + contours_flag_size + sizeof(PVG_FT_Outline);

    auto* outline = static_cast<PVG_FT_Outline*>(std::malloc(total));
    auto* data = reinterpret_cast<PVG_FT_Byte*>(outline + 1);

    outline->points = reinterpret_cast<PVG_FT_Vector*>(data);
    outline->tags = reinterpret_cast<char*>(data + points_size);
    outline->contours = reinterpret_cast<int*>(data + points_size + tags_size);
    outline->contours_flag = reinterpret_cast<char*>(data + points_size + tags_size + contours_size);
    outline->n_points = 0;
    outline->n_contours = 0;
    outline->flags = 0x0;
    return outline;
}

void ft_outline_destroy(PVG_FT_Outline* outline) {
    std::free(outline);
}

PVG_FT_Outline* ft_outline_from_path(const Path& path, const Matrix& matrix) {
    const auto* elems = path.elements();
    int size = path.element_count();
    if (!elems || size == 0)
        return ft_outline_create(0, 0);

    // Count points and contours for allocation.
    int num_points = 0, num_contours = 0;
    for (int i = 0; i < size; i += elems[i].header.length) {
        if (elems[i].header.command == PathCommand::MoveTo)
            num_contours++;
        num_points += elems[i].header.length - 1;
    }

    auto* outline = ft_outline_create(num_points, num_contours);
    int idx = 0;
    while (idx < size) {
        auto cmd = elems[idx].header.command;
        int len = elems[idx].header.length;
        const Point* pts = &elems[idx + 1].point;
        switch (cmd) {
        case PathCommand::MoveTo: {
            Point p = matrix.map(pts[0]);
            ft_outline_move_to(outline, p.x, p.y);
            break;
        }
        case PathCommand::LineTo: {
            Point p = matrix.map(pts[0]);
            ft_outline_line_to(outline, p.x, p.y);
            break;
        }
        case PathCommand::CubicTo: {
            Point mapped[3];
            matrix.map_points(pts, mapped, 3);
            ft_outline_cubic_to(outline,
                mapped[0].x, mapped[0].y,
                mapped[1].x, mapped[1].y,
                mapped[2].x, mapped[2].y);
            break;
        }
        case PathCommand::Close:
            ft_outline_close(outline);
            break;
        }
        idx += len;
    }
    ft_outline_end(outline);
    return outline;
}

PVG_FT_Outline* ft_outline_stroke(const Path& path, const Matrix& matrix,
                                    const StrokeData& stroke_data) {
    const auto* impl = path_impl(path);
    if (!impl)
        return ft_outline_create(0, 0);
    return ft_outline_convert_stroke(*impl, matrix, stroke_data);
}

Path ft_outline_to_path(const PVG_FT_Outline* outline) {
    Path result;
    if (!outline || outline->n_contours == 0)
        return result;

    int point_idx = 0;
    for (int c = 0; c < outline->n_contours; ++c) {
        int contour_end = outline->contours[c];
        bool first = true;
        while (point_idx <= contour_end) {
            float x = outline->points[point_idx].x / 64.0f;
            float y = outline->points[point_idx].y / 64.0f;

            if (first) {
                result.move_to(x, y);
                first = false;
                ++point_idx;
                continue;
            }

            char tag = outline->tags[point_idx] & 0x03;

            if (tag == PVG_FT_CURVE_TAG_ON) {
                result.line_to(x, y);
                ++point_idx;
            } else if (tag == PVG_FT_CURVE_TAG_CONIC) {
                if (point_idx + 1 > contour_end) {
                    ++point_idx;
                    break;
                }
                float x2 = outline->points[point_idx + 1].x / 64.0f;
                float y2 = outline->points[point_idx + 1].y / 64.0f;
                result.quad_to(x, y, x2, y2);
                point_idx += 2;
            } else if (tag == PVG_FT_CURVE_TAG_CUBIC) {
                if (point_idx + 2 > contour_end) {
                    ++point_idx;
                    break;
                }
                float x2 = outline->points[point_idx + 1].x / 64.0f;
                float y2 = outline->points[point_idx + 1].y / 64.0f;
                float x3 = outline->points[point_idx + 2].x / 64.0f;
                float y3 = outline->points[point_idx + 2].y / 64.0f;
                result.cubic_to(x, y, x2, y2, x3, y3);
                point_idx += 3;
            } else {
                ++point_idx;
            }
        }
        result.close();
    }
    return result;
}

// -- Rasterize --

void rasterize(SpanBuffer& span_buffer,
               const Path::Impl& path,
               const Matrix& matrix,
               const IntRect& clip_rect,
               const StrokeData* stroke_data,
               FillRule winding,
               bool antialias) {
    PVG_FT_Outline* outline = ft_outline_convert(path, matrix, stroke_data);
    if (stroke_data) {
        outline->flags = PVG_FT_OUTLINE_NONE;
    } else {
        switch (winding) {
        case FillRule::EvenOdd:
            outline->flags = PVG_FT_OUTLINE_EVEN_ODD_FILL;
            break;
        default:
            outline->flags = PVG_FT_OUTLINE_NONE;
            break;
        }
    }

    PVG_FT_Raster_Params params{};
    params.flags = PVG_FT_RASTER_FLAG_DIRECT;
    if (antialias)
        params.flags |= PVG_FT_RASTER_FLAG_AA;
    params.gray_spans = spans_generation_callback;
    params.user = &span_buffer;
    params.source = outline;

    if (!clip_rect.empty()) {
        params.flags |= PVG_FT_RASTER_FLAG_CLIP;
        params.clip_box.xMin = static_cast<PVG_FT_Pos>(clip_rect.x);
        params.clip_box.yMin = static_cast<PVG_FT_Pos>(clip_rect.y);
        params.clip_box.xMax = static_cast<PVG_FT_Pos>(clip_rect.x + clip_rect.w);
        params.clip_box.yMax = static_cast<PVG_FT_Pos>(clip_rect.y + clip_rect.h);
    }

    span_buffer.reset();
    PVG_FT_Raster_Render(&params);
    ft_outline_destroy(outline);
}

} // namespace plutovg
