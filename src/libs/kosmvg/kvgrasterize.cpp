// kvgrasterize.cpp — Span buffer operations and rasterization
// C++20

#include "kvgprivate.hpp"

#include "kosmvg-ft.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <span>

namespace kvg {

// -- SpanBuffer operations --

void span_buffer_init_rect(SpanBuffer& buf, int x, int y, int width, int height) {
    buf.spans.clear();
    buf.spans.resize(static_cast<size_t>(height));
    for (int i = 0; i < height; ++i) {
        buf.spans[static_cast<size_t>(i)] = {x, width, y + i, 255};
    }
    buf.bounds = {x, y, width, height};
}

void span_buffer_init_region(SpanBuffer& buf, std::span<const IntRect> rects) {
    buf.reset();
    if (rects.empty()) return;

    struct ScanSpan { int y, x0, x1; };
    std::vector<ScanSpan> raw_spans;
    raw_spans.reserve(rects.size() * 4);

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

    std::sort(raw_spans.begin(), raw_spans.end(), [](const ScanSpan& a, const ScanSpan& b) {
        return (a.y != b.y) ? (a.y < b.y) : (a.x0 < b.x0);
    });

    buf.spans.reserve(raw_spans.size());
    int cur_y = raw_spans[0].y;
    int cur_x0 = raw_spans[0].x0;
    int cur_x1 = raw_spans[0].x1;

    for (size_t i = 1; i < raw_spans.size(); ++i) {
        const auto& s = raw_spans[i];
        if (s.y == cur_y && s.x0 <= cur_x1) {
            cur_x1 = std::max(cur_x1, s.x1);
        } else {
            buf.spans.push_back({cur_x0, cur_x1 - cur_x0, cur_y, 255});
            cur_y = s.y;
            cur_x0 = s.x0;
            cur_x1 = s.x1;
        }
    }
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

    auto it = std::lower_bound(buf.spans.begin(), buf.spans.end(), iy,
        [](const Span& span, int target_y) { return span.y < target_y; });

    for (; it != buf.spans.end() && it->y == iy; ++it) {
        if (ix >= it->x && ix < it->x + it->len)
            return true;
    }
    return false;
}

void span_buffer_extents(const SpanBuffer& buf, Rect& extents) {
    if (buf.spans.empty()) {
        extents = {};
        return;
    }

    int x1 = INT_MAX;
    int y1 = buf.spans.front().y;
    int x2 = INT_MIN;
    int y2 = buf.spans.back().y;
    for (const auto& span : buf.spans) {
        if (span.x < x1) x1 = span.x;
        if (span.x + span.len > x2) x2 = span.x + span.len;
    }

    extents = Rect{static_cast<float>(x1), static_cast<float>(y1),
                   static_cast<float>(x2 - x1), static_cast<float>(y2 - y1 + 1)};
}

void span_buffer_intersect(SpanBuffer& dst, const SpanBuffer& a, const SpanBuffer& b) {
    dst.reset();
    dst.spans.reserve(std::max(a.spans.size(), b.spans.size()));

    auto* a_it  = a.spans.data();
    auto* a_end = a_it + a.spans.size();
    auto* b_it  = b.spans.data();
    auto* b_end = b_it + b.spans.size();

    while (a_it < a_end && b_it < b_end) {
        if (b_it->y > a_it->y) { ++a_it; continue; }
        if (a_it->y != b_it->y) { ++b_it; continue; }

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
                static_cast<unsigned char>(
                    (uint32_t(a_it->coverage) * b_it->coverage + 127) / 255)
            });
        }

        if (ax2 < bx2)
            ++a_it;
        else
            ++b_it;
    }
}

// -- FT outline helpers --

namespace {

namespace ft = kvg::ft;

inline ft::Pos ft_coord(float x) {
    return static_cast<ft::Pos>(std::round(x * 64.0f));
}

void ft_outline_move_to(ft::Outline* ft_ol, float x, float y) {
    ft_ol->points[ft_ol->n_points].x = ft_coord(x);
    ft_ol->points[ft_ol->n_points].y = ft_coord(y);
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::On);
    if (ft_ol->n_points) {
        ft_ol->contours[ft_ol->n_contours] = ft_ol->n_points - 1;
        ft_ol->n_contours++;
    }
    ft_ol->contours_flag[ft_ol->n_contours] = 1;
    ft_ol->n_points++;
}

void ft_outline_line_to(ft::Outline* ft_ol, float x, float y) {
    ft_ol->points[ft_ol->n_points].x = ft_coord(x);
    ft_ol->points[ft_ol->n_points].y = ft_coord(y);
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::On);
    ft_ol->n_points++;
}

void ft_outline_quad_to(ft::Outline* ft_ol,
                        float x1, float y1, float x2, float y2) {
    ft_ol->points[ft_ol->n_points].x = ft_coord(x1);
    ft_ol->points[ft_ol->n_points].y = ft_coord(y1);
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::Conic);
    ft_ol->n_points++;

    ft_ol->points[ft_ol->n_points].x = ft_coord(x2);
    ft_ol->points[ft_ol->n_points].y = ft_coord(y2);
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::On);
    ft_ol->n_points++;
}

void ft_outline_cubic_to(ft::Outline* ft_ol,
                         float x1, float y1, float x2, float y2, float x3, float y3) {
    ft_ol->points[ft_ol->n_points].x = ft_coord(x1);
    ft_ol->points[ft_ol->n_points].y = ft_coord(y1);
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::Cubic);
    ft_ol->n_points++;

    ft_ol->points[ft_ol->n_points].x = ft_coord(x2);
    ft_ol->points[ft_ol->n_points].y = ft_coord(y2);
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::Cubic);
    ft_ol->n_points++;

    ft_ol->points[ft_ol->n_points].x = ft_coord(x3);
    ft_ol->points[ft_ol->n_points].y = ft_coord(y3);
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::On);
    ft_ol->n_points++;
}

void ft_outline_close(ft::Outline* ft_ol) {
    if (!ft_ol->n_points) return;
    ft_ol->contours_flag[ft_ol->n_contours] = 0;
    int index = ft_ol->n_contours ? ft_ol->contours[ft_ol->n_contours - 1] + 1 : 0;
    if (index == ft_ol->n_points)
        return;
    // guard against double-close: don't duplicate if last point already matches start
    const auto& start = ft_ol->points[index];
    const auto& last  = ft_ol->points[ft_ol->n_points - 1];
    if (last.x == start.x && last.y == start.y)
        return;
    ft_ol->points[ft_ol->n_points].x = start.x;
    ft_ol->points[ft_ol->n_points].y = start.y;
    ft_ol->tags[ft_ol->n_points] = static_cast<char>(ft::CurveTag::On);
    ft_ol->n_points++;
}

void ft_outline_end(ft::Outline* ft_ol) {
    if (ft_ol->n_points) {
        ft_ol->contours[ft_ol->n_contours] = ft_ol->n_points - 1;
        ft_ol->n_contours++;
    }
}

// -- Convert Path::Impl → FT outline --

// Forward declaration for stroke conversion.
ft::Outline ft_outline_convert_stroke(const Path::Impl& path, const AffineTransform& matrix,
                                      const StrokeData& stroke_data);

// Count total FT points needed (quads → 2 points, cubics → 3, + close duplicates first point).
int ft_point_count(const Path::Impl& path) {
    int count = 0;
    for (auto cmd : path.commands) {
        switch (cmd) {
        case PathElement::MoveToPoint:    count += 1; break;
        case PathElement::AddLineToPoint: count += 1; break;
        case PathElement::AddQuadCurve:   count += 2; break;
        case PathElement::AddCurve:       count += 3; break;
        case PathElement::CloseSubpath:   count += 1; break; // closing point
        }
    }
    return count;
}

ft::Outline ft_outline_convert(const Path::Impl& path, const AffineTransform& matrix,
                                const StrokeData* stroke_data) {
    if (stroke_data)
        return ft_outline_convert_stroke(path, matrix, *stroke_data);

    if (path.commands.empty())
        return ft::Outline{};

    auto outline = ft::Outline::create(ft_point_count(path), path.num_contours);
    if (!outline) {
        detail::set_last_error(Error::OutOfMemory);
        return outline;
    }

    const auto* cmds = path.commands.data();
    const auto* pts  = path.points.data();
    int cmd_count = static_cast<int>(path.commands.size());
    int pt_idx = 0;

    for (int i = 0; i < cmd_count; ++i) {
        switch (cmds[i]) {
        case PathElement::MoveToPoint: {
            Point p = matrix.apply(pts[pt_idx]);
            ft_outline_move_to(&outline, p.x, p.y);
            pt_idx += 1;
            break;
        }
        case PathElement::AddLineToPoint: {
            Point p = matrix.apply(pts[pt_idx]);
            ft_outline_line_to(&outline, p.x, p.y);
            pt_idx += 1;
            break;
        }
        case PathElement::AddQuadCurve: {
            Point mapped[2];
            matrix.apply(&pts[pt_idx], mapped, 2);
            ft_outline_quad_to(&outline,
                mapped[0].x, mapped[0].y,
                mapped[1].x, mapped[1].y);
            pt_idx += 2;
            break;
        }
        case PathElement::AddCurve: {
            Point mapped[3];
            matrix.apply(&pts[pt_idx], mapped, 3);
            ft_outline_cubic_to(&outline,
                mapped[0].x, mapped[0].y,
                mapped[1].x, mapped[1].y,
                mapped[2].x, mapped[2].y);
            pt_idx += 3;
            break;
        }
        case PathElement::CloseSubpath:
            ft_outline_close(&outline);
            break;
        }
    }

    ft_outline_end(&outline);
    return outline;
}

// -- Rebuild a Path from Impl (for dash conversion) --

Path path_from_impl(const Path::Impl& impl) {
    Path::Builder builder;
    int pt_idx = 0;
    for (auto cmd : impl.commands) {
        const auto* pts = impl.points.data() + pt_idx;
        switch (cmd) {
        case PathElement::MoveToPoint:
            builder.move_to(pts[0].x, pts[0].y);
            pt_idx += 1;
            break;
        case PathElement::AddLineToPoint:
            builder.line_to(pts[0].x, pts[0].y);
            pt_idx += 1;
            break;
        case PathElement::AddQuadCurve:
            builder.quad_to(pts[0].x, pts[0].y, pts[1].x, pts[1].y);
            pt_idx += 2;
            break;
        case PathElement::AddCurve:
            builder.cubic_to(pts[0].x, pts[0].y, pts[1].x, pts[1].y, pts[2].x, pts[2].y);
            pt_idx += 3;
            break;
        case PathElement::CloseSubpath:
            builder.close();
            break;
        }
    }
    return builder.build();
}

// -- Dash conversion --

ft::Outline ft_outline_convert_dash(const Path::Impl& path, const AffineTransform& matrix,
                                    const StrokeDash& dash) {
    if (dash.array.empty())
        return ft_outline_convert(path, matrix, nullptr);

    Path src = path_from_impl(path);
    Path dashed = src.dashed(dash.offset, {dash.array.data(), dash.array.size()});
    const auto* dashed_impl = path_impl(dashed);
    if (!dashed_impl)
        return ft::Outline::create(0, 0);
    return ft_outline_convert(*dashed_impl, matrix, nullptr);
}

// -- Stroke conversion --

ft::Outline ft_outline_convert_stroke(const Path::Impl& path, const AffineTransform& matrix,
                                      const StrokeData& stroke_data) {
    double scale_x = std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b);
    double scale_y = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
    double scale = std::hypot(scale_x, scale_y) / sqrt2;
    double width = stroke_data.style.width * scale;

    auto ftWidth = static_cast<ft::Fixed>(width * 0.5 * (1 << 6));
    auto ftMiterLimit = static_cast<ft::Fixed>(stroke_data.style.miter_limit * (1 << 16));

    ft::LineCap ftCap;
    switch (stroke_data.style.cap) {
    case LineCap::Square: ftCap = ft::LineCap::Square; break;
    case LineCap::Round:  ftCap = ft::LineCap::Round; break;
    default:              ftCap = ft::LineCap::Butt; break;
    }

    ft::LineJoin ftJoin;
    switch (stroke_data.style.join) {
    case LineJoin::Bevel: ftJoin = ft::LineJoin::Bevel; break;
    case LineJoin::Round: ftJoin = ft::LineJoin::Round; break;
    default:              ftJoin = ft::LineJoin::MiterFixed; break;
    }

    ft::Stroker stroker;
    stroker.set(ftWidth, ftCap, ftJoin, ftMiterLimit);

    auto outline = ft_outline_convert_dash(path, matrix, stroke_data.dash);
    if (!outline) return outline;

    ft::Error err = stroker.parse_outline(&outline);
    if (err) {
        detail::set_last_error(Error::InvalidArgument);
        return ft::Outline::create(0, 0);
    }

    ft::UInt points = 0;
    ft::UInt contours = 0;
    err = stroker.get_counts(&points, &contours);
    if (err) {
        detail::set_last_error(Error::InvalidArgument);
        return ft::Outline::create(0, 0);
    }

    auto stroke_outline = ft::Outline::create(static_cast<int>(points),
                                               static_cast<int>(contours));
    if (!stroke_outline) {
        detail::set_last_error(Error::OutOfMemory);
        return stroke_outline;
    }
    stroker.export_outline(&stroke_outline);

    return stroke_outline;
}

// -- Span generation callback --

void spans_generation_callback(int count, const ft::Span* ft_spans, void* user) {
    static_assert(sizeof(Span) == sizeof(ft::Span));
    static_assert(offsetof(Span, x) == offsetof(ft::Span, x));
    static_assert(offsetof(Span, len) == offsetof(ft::Span, len));
    static_assert(offsetof(Span, y) == offsetof(ft::Span, y));
    static_assert(offsetof(Span, coverage) == offsetof(ft::Span, coverage));

    auto* buf = static_cast<SpanBuffer*>(user);
    size_t old_size = buf->spans.size();
    buf->spans.resize(old_size + static_cast<size_t>(count));
    std::memcpy(buf->spans.data() + old_size, ft_spans, count * sizeof(Span));
}

} // anonymous namespace

// -- FT outline → Path (public API) --

static ft::Outline ft_outline_stroke(const Path& path, const AffineTransform& matrix,
                                     const StrokeData& stroke_data) {
    const auto* impl = path_impl(path);
    if (!impl)
        return ft::Outline::create(0, 0);
    return ft_outline_convert_stroke(*impl, matrix, stroke_data);
}

static Path ft_outline_to_path(const ft::Outline* outline) {
    if (!outline || outline->n_contours == 0)
        return {};

    auto from_26_6 = [](ft::Pos v) { return static_cast<float>(v) / 64.0f; };

    Path::Builder builder;

    int point_idx = 0;
    for (int c = 0; c < outline->n_contours; ++c) {
        int contour_end = outline->contours[c];
        bool first = true;
        while (point_idx <= contour_end) {
            float x = from_26_6(outline->points[point_idx].x);
            float y = from_26_6(outline->points[point_idx].y);

            if (first) {
                builder.move_to(x, y);
                first = false;
                ++point_idx;
                continue;
            }

            auto tag = ft::curve_tag(outline->tags[point_idx]);

            if (tag == ft::CurveTag::On) {
                builder.line_to(x, y);
                ++point_idx;
            } else if (tag == ft::CurveTag::Conic) {
                if (point_idx + 1 > contour_end) {
                    ++point_idx;
                    break;
                }
                float x2 = from_26_6(outline->points[point_idx + 1].x);
                float y2 = from_26_6(outline->points[point_idx + 1].y);
                builder.quad_to(x, y, x2, y2);
                point_idx += 2;
            } else if (tag == ft::CurveTag::Cubic) {
                if (point_idx + 2 > contour_end) {
                    ++point_idx;
                    break;
                }
                float x2 = from_26_6(outline->points[point_idx + 1].x);
                float y2 = from_26_6(outline->points[point_idx + 1].y);
                float x3 = from_26_6(outline->points[point_idx + 2].x);
                float y3 = from_26_6(outline->points[point_idx + 2].y);
                builder.cubic_to(x, y, x2, y2, x3, y3);
                point_idx += 3;
            } else {
                ++point_idx;
            }
        }
        builder.close();
    }
    return builder.build();
}

// -- Stroke-to-fill conversion (exposed in private.hpp) --
// Input path is transformed by matrix into device space before stroking.
// The returned Path is therefore in device (post-transform) coordinates.

Path stroke_to_path(const Path& path, const AffineTransform& matrix,
                    const StrokeData& stroke_data) {
    auto outline = ft_outline_stroke(path, matrix, stroke_data);
    return ft_outline_to_path(&outline);
}

// -- Rasterize --

void rasterize(SpanBuffer& span_buffer,
               const Path::Impl& path,
               const AffineTransform& matrix,
               const IntRect& clip_rect,
               const StrokeData* stroke_data,
               FillRule winding,
               bool antialias) {
    auto outline = ft_outline_convert(path, matrix, stroke_data);
    if (stroke_data) {
        outline.flags = static_cast<int>(ft::OutlineFlags::None);
    } else {
        switch (winding) {
        case FillRule::EvenOdd:
            outline.flags = static_cast<int>(ft::OutlineFlags::EvenOddFill);
            break;
        default:
            outline.flags = static_cast<int>(ft::OutlineFlags::None);
            break;
        }
    }

    span_buffer.reset();
    if (!clip_rect.empty())
        span_buffer.spans.reserve(static_cast<size_t>(clip_rect.h) * 2);

    if (antialias) {
        ft::RasterParams params{};
        params.flags     = ft::RasterFlags::Direct | ft::RasterFlags::AA;
        params.gray_spans = spans_generation_callback;
        params.user       = &span_buffer;
        params.source     = &outline;

        if (!clip_rect.empty()) {
            params.flags = params.flags | ft::RasterFlags::Clip;
            params.clip_box.xMin = static_cast<ft::Pos>(clip_rect.x);
            params.clip_box.yMin = static_cast<ft::Pos>(clip_rect.y);
            params.clip_box.xMax = static_cast<ft::Pos>(clip_rect.x + clip_rect.w);
            params.clip_box.yMax = static_cast<ft::Pos>(clip_rect.y + clip_rect.h);
        }

        ft::raster_render(&params);
    } else {
        ft::MonoRasterParams mono_params{};
        mono_params.source     = &outline;
        mono_params.mono_spans = spans_generation_callback;
        mono_params.user       = &span_buffer;

        if (!clip_rect.empty()) {
            mono_params.clip = true;
            mono_params.clip_box.xMin = static_cast<ft::Pos>(clip_rect.x);
            mono_params.clip_box.yMin = static_cast<ft::Pos>(clip_rect.y);
            mono_params.clip_box.xMax = static_cast<ft::Pos>(clip_rect.x + clip_rect.w);
            mono_params.clip_box.yMax = static_cast<ft::Pos>(clip_rect.y + clip_rect.h);
        }

        ft::Error mono_err = ft::raster_render_mono(&mono_params);
        if (mono_err)
            detail::set_last_error(Error::OutOfMemory);
    }
}

} // namespace kvg
