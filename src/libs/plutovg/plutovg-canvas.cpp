// plutovg-canvas.cpp - Drawing context
// C++20

#include "plutovg-private.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace plutovg {

// Version

int runtime_version()
{
    return version;
}

const char* runtime_version_string()
{
    return version_string;
}

// Internal helper: access pimpl pointer by layout assumption.
// Both Path and Paint store a single Impl* as their sole data member.
// (Actual definitions are in plutovg-private.hpp.)

// RAII guard: swap fill paint/color with stroke paint/color for stroke blend ops.
// When stroke_paint_set is true, stroke operations use stroke paint.
// The blend(Canvas::Impl&, ...) function always reads fill_paint/fill_color,
// so we swap them temporarily for stroke operations.

struct StrokePaintGuard {
    State& st;
    bool active;

    explicit StrokePaintGuard(State& s) : st(s), active(s.stroke_paint_set) {
        if (active) {
            std::swap(st.fill_paint, st.stroke_paint);
            std::swap(st.fill_color, st.stroke_color);
        }
    }

    ~StrokePaintGuard() {
        if (active) {
            std::swap(st.fill_paint, st.stroke_paint);
            std::swap(st.fill_color, st.stroke_color);
        }
    }

    StrokePaintGuard(const StrokePaintGuard&) = delete;
    StrokePaintGuard& operator=(const StrokePaintGuard&) = delete;
};

// Shadow rendering helper.
// Renders the given spans as a drop shadow (offset, blurred, with shadow color)
// onto the canvas surface, before the main draw.

static void render_shadow(Canvas::Impl& impl, const SpanBuffer& spans)
{
    const auto& shadow = impl.state().shadow;
    if (shadow.is_none())
        return;
    if (spans.spans.empty())
        return;

    // Compute span bounding box.
    Rect extents;
    span_buffer_extents(spans, extents);
    if (extents.empty())
        return;

    // Expand for blur and offset.
    float blur_pad = std::ceil(shadow.blur * 3.0f);
    int sx = static_cast<int>(std::floor(extents.x + shadow.offset_x - blur_pad));
    int sy = static_cast<int>(std::floor(extents.y + shadow.offset_y - blur_pad));
    int sr = static_cast<int>(std::ceil(extents.right() + shadow.offset_x + blur_pad)) + 1;
    int sb = static_cast<int>(std::ceil(extents.bottom() + shadow.offset_y + blur_pad)) + 1;

    // Clamp to surface bounds.
    sx = std::max(sx, 0);
    sy = std::max(sy, 0);
    sr = std::min(sr, impl.surface.width());
    sb = std::min(sb, impl.surface.height());
    int sw = sr - sx;
    int sh = sb - sy;
    if (sw <= 0 || sh <= 0)
        return;

    // Ensure shadow surface is large enough.
    if (!impl.shadow_surface
        || impl.shadow_surface.width() < sw
        || impl.shadow_surface.height() < sh) {
        impl.shadow_surface = Surface::create(sw, sh);
    }

    // Clear the shadow region.
    impl.shadow_surface.clear(Color::transparent());

    // Build offset spans: shift by shadow offset, translate into shadow surface coords.
    int ox = static_cast<int>(std::round(shadow.offset_x));
    int oy = static_cast<int>(std::round(shadow.offset_y));

    SpanBuffer offset_spans;
    for (const auto& span : spans.spans) {
        int dst_x = span.x + ox - sx;
        int dst_y = span.y + oy - sy;
        if (dst_y < 0 || dst_y >= sh)
            continue;
        int x0 = std::max(dst_x, 0);
        int x1 = std::min(dst_x + span.len, sw);
        if (x0 >= x1)
            continue;
        offset_spans.spans.push_back({x0, x1 - x0, dst_y, span.coverage});
    }

    if (offset_spans.spans.empty())
        return;

    // Blend shadow color onto shadow surface.
    Paint::Impl shadow_paint;
    shadow_paint.data = SolidPaintData{shadow.color};

    IntRect shadow_clip{0, 0, sw, sh};
    BlendParams params{
        impl.shadow_surface,
        &shadow_paint,
        Operator::SrcOver,
        BlendMode::Normal,
        ColorInterpolation::SRGB,
        1.0f,
        false
    };

    blend(params, offset_spans, shadow_clip, nullptr);

    // Apply gaussian blur.
    if (shadow.blur > 0.0f)
        gaussian_blur(impl.shadow_surface.mutable_data(), sw, sh, impl.shadow_surface.stride(), shadow.blur);

    // Composite shadow surface onto main surface at (sx, sy) using a texture paint.
    SpanBuffer rect_spans;
    span_buffer_init_rect(rect_spans, sx, sy, sw, sh);

    TexturePaintData tex_data;
    tex_data.type = TextureType::Plain;
    tex_data.opacity = 1.0f;
    tex_data.matrix = Matrix::translated(static_cast<float>(sx), static_cast<float>(sy));
    tex_data.surface = impl.shadow_surface;

    Paint::Impl tex_paint;
    tex_paint.data = tex_data;

    BlendParams blit_params{
        impl.surface,
        &tex_paint,
        impl.state().op,
        impl.state().blend_mode,
        impl.state().color_interp,
        impl.state().opacity,
        impl.state().dithering
    };

    blend(blit_params, rect_spans, impl.clip_rect,
          impl.state().clipping ? &impl.state().clip_spans : nullptr);
}

// Get rasterized spans with clipping applied. Returns pointer to the span buffer to use.

static const SpanBuffer& rasterize_and_clip(Canvas::Impl& impl, const StrokeData* stroke)
{
    auto& st = impl.state();
    rasterize(impl.fill_spans, *path_impl(impl.path), st.matrix,
              impl.clip_rect, stroke, st.winding);

    if (st.clipping) {
        span_buffer_intersect(impl.clip_spans, impl.fill_spans, st.clip_spans);
        return impl.clip_spans;
    }
    return impl.fill_spans;
}

// Canvas lifecycle

Canvas::Canvas(Surface surface)
    : m_impl(new Impl)
{
    m_impl->clip_rect = IntRect{0, 0, surface.width(), surface.height()};
    m_impl->surface = std::move(surface);
}

Canvas::~Canvas()
{
    delete m_impl;
}

Canvas::Canvas(Canvas&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

Canvas& Canvas::operator=(Canvas&& other) noexcept
{
    if (this != &other) {
        delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Canvas::operator bool() const
{
    return m_impl != nullptr;
}

Surface Canvas::surface() const
{
    if (!m_impl) return {};
    return m_impl->surface;
}

// State stack

void Canvas::save()
{
    if (!m_impl) return;
    m_impl->states.push();
}

void Canvas::restore()
{
    if (!m_impl) return;
    m_impl->states.pop();
}

// Unified paint setters (set both fill and stroke)

void Canvas::set_color(const Color& c)
{
    if (!m_impl) return;
    set_color(c.r, c.g, c.b, c.a);
}

void Canvas::set_color(float r, float g, float b, float a)
{
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.fill_color = Color(r, g, b, a);
    st.stroke_color = st.fill_color;
    st.fill_paint = Paint();
    st.stroke_paint = Paint();
    st.stroke_paint_set = false;
}

void Canvas::set_linear_gradient(float x1, float y1, float x2, float y2,
                                 SpreadMethod spread,
                                 std::span<const GradientStop> stops,
                                 const Matrix* matrix)
{
    if (!m_impl) return;
    auto paint = Paint::linear_gradient(x1, y1, x2, y2, spread, stops, matrix);
    set_paint(paint);
}

void Canvas::set_radial_gradient(float cx, float cy, float cr,
                                 float fx, float fy, float fr,
                                 SpreadMethod spread,
                                 std::span<const GradientStop> stops,
                                 const Matrix* matrix)
{
    if (!m_impl) return;
    auto paint = Paint::radial_gradient(cx, cy, cr, fx, fy, fr, spread, stops, matrix);
    set_paint(paint);
}

void Canvas::set_conic_gradient(float cx, float cy, float start_angle,
                                SpreadMethod spread,
                                std::span<const GradientStop> stops,
                                const Matrix* matrix)
{
    if (!m_impl) return;
    auto paint = Paint::conic_gradient(cx, cy, start_angle, spread, stops, matrix);
    set_paint(paint);
}

void Canvas::set_texture(const Surface& surface, TextureType type,
                         float opacity, const Matrix* matrix)
{
    if (!m_impl) return;
    auto paint = Paint::texture(surface, type, opacity, matrix);
    set_paint(paint);
}

void Canvas::set_paint(const Paint& paint)
{
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.fill_paint = paint;
    st.stroke_paint = paint;
    st.stroke_paint_set = false;
}

Color Canvas::get_color() const
{
    return m_impl->state().fill_color;
}

Paint Canvas::get_paint() const
{
    return m_impl->state().fill_paint;
}

// Separate fill / stroke paint

void Canvas::set_fill_color(const Color& c)
{
    if (!m_impl) return;
    set_fill_color(c.r, c.g, c.b, c.a);
}

void Canvas::set_fill_color(float r, float g, float b, float a)
{
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.fill_color = Color(r, g, b, a);
    st.fill_paint = Paint();
}

void Canvas::set_fill_paint(const Paint& paint)
{
    if (!m_impl) return;
    m_impl->state().fill_paint = paint;
}

void Canvas::set_stroke_color(const Color& c)
{
    if (!m_impl) return;
    set_stroke_color(c.r, c.g, c.b, c.a);
}

void Canvas::set_stroke_color(float r, float g, float b, float a)
{
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.stroke_color = Color(r, g, b, a);
    st.stroke_paint = Paint();
    st.stroke_paint_set = true;
}

void Canvas::set_stroke_paint(const Paint& paint)
{
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.stroke_paint = paint;
    st.stroke_paint_set = true;
}

Color Canvas::get_fill_color() const
{
    return m_impl->state().fill_color;
}

Paint Canvas::get_fill_paint() const
{
    return m_impl->state().fill_paint;
}

Color Canvas::get_stroke_color() const
{
    auto& st = m_impl->state();
    return st.stroke_paint_set ? st.stroke_color : st.fill_color;
}

Paint Canvas::get_stroke_paint() const
{
    auto& st = m_impl->state();
    return st.stroke_paint_set ? st.stroke_paint : st.fill_paint;
}

// Shadow

void Canvas::set_shadow(float offset_x, float offset_y, float blur, const Color& color)
{
    if (!m_impl) return;
    m_impl->state().shadow = Shadow(offset_x, offset_y, blur, color);
}

void Canvas::set_shadow(const Shadow& shadow)
{
    if (!m_impl) return;
    m_impl->state().shadow = shadow;
}

void Canvas::clear_shadow()
{
    if (!m_impl) return;
    m_impl->state().shadow = Shadow();
}

Shadow Canvas::get_shadow() const
{
    return m_impl->state().shadow;
}

// Blend mode

void Canvas::set_blend_mode(BlendMode mode)
{
    if (!m_impl) return;
    m_impl->state().blend_mode = mode;
}

BlendMode Canvas::get_blend_mode() const
{
    return m_impl->state().blend_mode;
}

// Masking

void Canvas::mask(const Surface& mask_surface, MaskMode mode, int ox, int oy)
{
    if (!m_impl) return;
    auto& impl = *m_impl;
    auto& st = impl.state();

    // Determine the clip region spans.
    const SpanBuffer* clip = nullptr;
    if (st.clipping) {
        clip = &st.clip_spans;
    } else {
        span_buffer_init_rect(impl.clip_spans, 0, 0,
                              impl.surface.width(), impl.surface.height());
        clip = &impl.clip_spans;
    }

    // Determine paint impl for the current fill paint.
    const Paint::Impl* p = paint_impl(st.fill_paint);

    // Build a temporary Impl for solid color fallback.
    Paint::Impl solid_fallback;
    if (!p) {
        solid_fallback.data = SolidPaintData{st.fill_color};
        p = &solid_fallback;
    }

    BlendParams params{
        impl.surface,
        p,
        st.op,
        st.blend_mode,
        st.color_interp,
        st.opacity,
        st.dithering
    };

    blend_masked(params, *clip, impl.clip_rect, nullptr,
                 mask_surface, mode, ox, oy);
}

// Color interpolation and dithering

void Canvas::set_color_interpolation(ColorInterpolation ci)
{
    if (!m_impl) return;
    m_impl->state().color_interp = ci;
}

ColorInterpolation Canvas::get_color_interpolation() const
{
    if (!m_impl) return ColorInterpolation::SRGB;
    return m_impl->state().color_interp;
}

void Canvas::set_dithering(bool enabled)
{
    if (!m_impl) return;
    m_impl->state().dithering = enabled;
}

bool Canvas::get_dithering() const
{
    if (!m_impl) return false;
    return m_impl->state().dithering;
}

// Font management

void Canvas::set_font_face_cache(const FontFaceCache& cache)
{
    if (!m_impl) return;
    m_impl->face_cache = cache;
}

FontFaceCache Canvas::get_font_face_cache() const
{
    return m_impl->face_cache;
}

void Canvas::add_font_face(std::string_view family, bool bold, bool italic, const FontFace& face)
{
    if (!m_impl) return;
    if (!m_impl->face_cache)
        m_impl->face_cache = FontFaceCache();
    m_impl->face_cache.add(family, bold, italic, face);
}

bool Canvas::add_font_file(std::string_view family, bool bold, bool italic,
                           const char* filename, int ttcindex)
{
    if (!m_impl) return false;
    if (!m_impl->face_cache)
        m_impl->face_cache = FontFaceCache();
    return m_impl->face_cache.add_file(family, bold, italic, filename, ttcindex);
}

bool Canvas::select_font_face(std::string_view family, bool bold, bool italic)
{
    if (!m_impl) return false;
    if (!m_impl->face_cache)
        return false;
    auto face = m_impl->face_cache.get(family, bold, italic);
    if (!face)
        return false;
    set_font_face(face);
    return true;
}

void Canvas::set_font(const FontFace& face, float size)
{
    if (!m_impl) return;
    set_font_face(face);
    set_font_size(size);
}

void Canvas::set_font_face(const FontFace& face)
{
    if (!m_impl) return;
    m_impl->state().font_face = face;
}

FontFace Canvas::get_font_face() const
{
    if (!m_impl) return {};
    return m_impl->state().font_face;
}

void Canvas::set_font_size(float size)
{
    if (!m_impl) return;
    m_impl->state().font_size = size;
}

float Canvas::get_font_size() const
{
    if (!m_impl) return 0;
    return m_impl->state().font_size;
}

// Fill / stroke settings

void Canvas::set_fill_rule(FillRule rule)
{
    if (!m_impl) return;
    m_impl->state().winding = rule;
}

FillRule Canvas::get_fill_rule() const
{
    if (!m_impl) return FillRule::NonZero;
    return m_impl->state().winding;
}

void Canvas::set_operator(Operator op)
{
    if (!m_impl) return;
    m_impl->state().op = op;
}

Operator Canvas::get_operator() const
{
    if (!m_impl) return Operator::SrcOver;
    return m_impl->state().op;
}

void Canvas::set_opacity(float opacity)
{
    if (!m_impl) return;
    m_impl->state().opacity = std::clamp(opacity, 0.0f, 1.0f);
}

float Canvas::get_opacity() const
{
    if (!m_impl) return 0;
    return m_impl->state().opacity;
}

void Canvas::set_line_width(float width)
{
    if (!m_impl) return;
    m_impl->state().stroke.style.width = width;
}

float Canvas::get_line_width() const
{
    if (!m_impl) return 0;
    return m_impl->state().stroke.style.width;
}

void Canvas::set_line_cap(LineCap cap)
{
    if (!m_impl) return;
    m_impl->state().stroke.style.cap = cap;
}

LineCap Canvas::get_line_cap() const
{
    if (!m_impl) return LineCap::Butt;
    return m_impl->state().stroke.style.cap;
}

void Canvas::set_line_join(LineJoin join)
{
    if (!m_impl) return;
    m_impl->state().stroke.style.join = join;
}

LineJoin Canvas::get_line_join() const
{
    if (!m_impl) return LineJoin::Miter;
    return m_impl->state().stroke.style.join;
}

void Canvas::set_miter_limit(float limit)
{
    if (!m_impl) return;
    m_impl->state().stroke.style.miter_limit = limit;
}

float Canvas::get_miter_limit() const
{
    if (!m_impl) return 0;
    return m_impl->state().stroke.style.miter_limit;
}

void Canvas::set_dash(float offset, std::span<const float> dashes)
{
    if (!m_impl) return;
    set_dash_offset(offset);
    set_dash_array(dashes);
}

void Canvas::set_dash_offset(float offset)
{
    if (!m_impl) return;
    m_impl->state().stroke.dash.offset = offset;
}

void Canvas::set_dash_array(std::span<const float> dashes)
{
    if (!m_impl) return;
    auto& arr = m_impl->state().stroke.dash.array;
    arr.assign(dashes.begin(), dashes.end());
}

float Canvas::get_dash_offset() const
{
    if (!m_impl) return 0;
    return m_impl->state().stroke.dash.offset;
}

int Canvas::get_dash_array(const float** dashes) const
{
    if (!m_impl) return 0;
    const auto& arr = m_impl->state().stroke.dash.array;
    if (dashes)
        *dashes = arr.data();
    return static_cast<int>(arr.size());
}

// Transform

void Canvas::translate(float tx, float ty)
{
    if (!m_impl) return;
    m_impl->state().matrix.translate(tx, ty);
}

void Canvas::scale(float sx, float sy)
{
    if (!m_impl) return;
    m_impl->state().matrix.scale(sx, sy);
}

void Canvas::shear(float shx, float shy)
{
    if (!m_impl) return;
    m_impl->state().matrix.shear(shx, shy);
}

void Canvas::rotate(float radians)
{
    if (!m_impl) return;
    m_impl->state().matrix.rotate(radians);
}

void Canvas::transform(const Matrix& m)
{
    if (!m_impl) return;
    m_impl->state().matrix = m * m_impl->state().matrix;
}

void Canvas::reset_matrix()
{
    if (!m_impl) return;
    m_impl->state().matrix = Matrix::identity();
}

void Canvas::set_matrix(const Matrix& m)
{
    if (!m_impl) return;
    m_impl->state().matrix = m;
}

Matrix Canvas::get_matrix() const
{
    if (!m_impl) return {};
    return m_impl->state().matrix;
}

Point Canvas::map(Point p) const
{
    if (!m_impl) return {};
    return m_impl->state().matrix.map(p);
}

void Canvas::map(float x, float y, float& ox, float& oy) const
{
    if (!m_impl) return;
    m_impl->state().matrix.map(x, y, ox, oy);
}

Rect Canvas::map_rect(const Rect& r) const
{
    if (!m_impl) return {};
    return m_impl->state().matrix.map_rect(r);
}

// Path construction

void Canvas::move_to(float x, float y)
{
    if (!m_impl) return;
    m_impl->path.move_to(x, y);
}

void Canvas::line_to(float x, float y)
{
    if (!m_impl) return;
    m_impl->path.line_to(x, y);
}

void Canvas::quad_to(float x1, float y1, float x2, float y2)
{
    if (!m_impl) return;
    m_impl->path.quad_to(x1, y1, x2, y2);
}

void Canvas::cubic_to(float x1, float y1, float x2, float y2, float x3, float y3)
{
    if (!m_impl) return;
    m_impl->path.cubic_to(x1, y1, x2, y2, x3, y3);
}

void Canvas::arc_to(float rx, float ry, float angle,
                    bool large_arc_flag, bool sweep_flag, float x, float y)
{
    if (!m_impl) return;
    m_impl->path.arc_to(rx, ry, angle, large_arc_flag, sweep_flag, x, y);
}

void Canvas::rect(float x, float y, float w, float h)
{
    if (!m_impl) return;
    m_impl->path.add_rect(x, y, w, h);
}

void Canvas::round_rect(float x, float y, float w, float h, float rx, float ry)
{
    if (!m_impl) return;
    m_impl->path.add_round_rect(x, y, w, h, rx, ry);
}

void Canvas::round_rect(float x, float y, float w, float h, const CornerRadii& radii)
{
    if (!m_impl) return;
    m_impl->path.add_round_rect(x, y, w, h, radii);
}

void Canvas::ellipse(float cx, float cy, float rx, float ry)
{
    if (!m_impl) return;
    m_impl->path.add_ellipse(cx, cy, rx, ry);
}

void Canvas::circle(float cx, float cy, float r)
{
    if (!m_impl) return;
    m_impl->path.add_circle(cx, cy, r);
}

void Canvas::arc(float cx, float cy, float r, float a0, float a1, bool ccw)
{
    if (!m_impl) return;
    m_impl->path.add_arc(cx, cy, r, a0, a1, ccw);
}

void Canvas::add_path(const Path& path)
{
    if (!m_impl) return;
    m_impl->path.add_path(path);
}

void Canvas::new_path()
{
    if (!m_impl) return;
    m_impl->path.reset();
}

void Canvas::close_path()
{
    if (!m_impl) return;
    m_impl->path.close();
}

Point Canvas::current_point() const
{
    if (!m_impl) return {};
    return m_impl->path.current_point();
}

Path Canvas::get_path() const
{
    if (!m_impl) return {};
    return m_impl->path;
}

// Hit testing

bool Canvas::fill_contains(float x, float y)
{
    if (!m_impl) return false;
    auto& impl = *m_impl;
    auto& st = impl.state();
    rasterize(impl.fill_spans, *path_impl(impl.path), st.matrix,
              impl.clip_rect, nullptr, st.winding);
    auto p = st.matrix.map(Point{x, y});
    return span_buffer_contains(impl.fill_spans, p.x, p.y);
}

bool Canvas::stroke_contains(float x, float y)
{
    if (!m_impl) return false;
    auto& impl = *m_impl;
    auto& st = impl.state();
    rasterize(impl.fill_spans, *path_impl(impl.path), st.matrix,
              impl.clip_rect, &st.stroke, FillRule::NonZero);
    auto p = st.matrix.map(Point{x, y});
    return span_buffer_contains(impl.fill_spans, p.x, p.y);
}

bool Canvas::clip_contains(float x, float y)
{
    if (!m_impl) return false;
    auto& st = m_impl->state();
    auto p = st.matrix.map(Point{x, y});
    if (st.clipping)
        return span_buffer_contains(st.clip_spans, p.x, p.y);

    const auto& cr = m_impl->clip_rect;
    return p.x >= cr.x && p.x < cr.right() && p.y >= cr.y && p.y < cr.bottom();
}

// Extents

Rect Canvas::fill_extents()
{
    if (!m_impl) return {};
    auto& impl = *m_impl;
    auto& st = impl.state();
    rasterize(impl.fill_spans, *path_impl(impl.path), st.matrix,
              impl.clip_rect, nullptr, st.winding);
    Rect r;
    span_buffer_extents(impl.fill_spans, r);
    return r;
}

Rect Canvas::stroke_extents()
{
    if (!m_impl) return {};
    auto& impl = *m_impl;
    auto& st = impl.state();
    rasterize(impl.fill_spans, *path_impl(impl.path), st.matrix,
              impl.clip_rect, &st.stroke, FillRule::NonZero);
    Rect r;
    span_buffer_extents(impl.fill_spans, r);
    return r;
}

Rect Canvas::clip_extents()
{
    if (!m_impl) return {};
    auto& st = m_impl->state();
    if (st.clipping) {
        Rect r;
        span_buffer_extents(st.clip_spans, r);
        return r;
    }
    const auto& cr = m_impl->clip_rect;
    return Rect(static_cast<float>(cr.x), static_cast<float>(cr.y),
                static_cast<float>(cr.w), static_cast<float>(cr.h));
}

// Drawing operations

void Canvas::fill()
{
    if (!m_impl) return;
    fill_preserve();
    new_path();
}

void Canvas::stroke()
{
    if (!m_impl) return;
    stroke_preserve();
    new_path();
}

void Canvas::clip()
{
    if (!m_impl) return;
    clip_preserve();
    new_path();
}

void Canvas::paint()
{
    if (!m_impl) return;
    auto& impl = *m_impl;
    auto& st = impl.state();
    if (st.clipping) {
        blend(impl, st.clip_spans);
    } else {
        span_buffer_init_rect(impl.clip_spans, 0, 0,
                              impl.surface.width(), impl.surface.height());
        blend(impl, impl.clip_spans);
    }
}

void Canvas::fill_preserve()
{
    if (!m_impl) return;
    auto& impl = *m_impl;
    const auto& spans = rasterize_and_clip(impl, nullptr);
    render_shadow(impl, spans);
    blend(impl, spans);
}

void Canvas::stroke_preserve()
{
    if (!m_impl) return;
    auto& impl = *m_impl;
    auto& st = impl.state();

    rasterize(impl.fill_spans, *path_impl(impl.path), st.matrix,
              impl.clip_rect, &st.stroke, FillRule::NonZero);

    const SpanBuffer* draw_spans;
    if (st.clipping) {
        span_buffer_intersect(impl.clip_spans, impl.fill_spans, st.clip_spans);
        draw_spans = &impl.clip_spans;
    } else {
        draw_spans = &impl.fill_spans;
    }

    render_shadow(impl, *draw_spans);

    // Use stroke paint if set, otherwise fall back to fill paint.
    StrokePaintGuard guard(st);
    blend(impl, *draw_spans);
}

void Canvas::clip_preserve()
{
    if (!m_impl) return;
    auto& impl = *m_impl;
    auto& st = impl.state();

    if (st.clipping) {
        rasterize(impl.fill_spans, *path_impl(impl.path), st.matrix,
                  impl.clip_rect, nullptr, st.winding);
        span_buffer_intersect(impl.clip_spans, impl.fill_spans, st.clip_spans);
        span_buffer_copy(st.clip_spans, impl.clip_spans);
    } else {
        rasterize(st.clip_spans, *path_impl(impl.path), st.matrix,
                  impl.clip_rect, nullptr, st.winding);
        st.clipping = true;
    }
}

// Convenience: fill/stroke/clip rect/path

void Canvas::fill_rect(float x, float y, float w, float h)
{
    if (!m_impl) return;
    new_path();
    rect(x, y, w, h);
    fill();
}

void Canvas::fill_path(const Path& path)
{
    if (!m_impl) return;
    new_path();
    add_path(path);
    fill();
}

void Canvas::stroke_rect(float x, float y, float w, float h)
{
    if (!m_impl) return;
    new_path();
    rect(x, y, w, h);
    stroke();
}

void Canvas::stroke_path(const Path& path)
{
    if (!m_impl) return;
    new_path();
    add_path(path);
    stroke();
}

void Canvas::clip_rect(float x, float y, float w, float h)
{
    if (!m_impl) return;
    new_path();
    rect(x, y, w, h);
    clip();
}

void Canvas::clip_path(const Path& path)
{
    if (!m_impl) return;
    new_path();
    add_path(path);
    clip();
}

// Text operations

float Canvas::add_glyph(Codepoint codepoint, float x, float y)
{
    if (!m_impl) return 0;
    auto& st = m_impl->state();
    if (st.font_face && st.font_size > 0.0f)
        return st.font_face.get_glyph_path(st.font_size, x, y, codepoint, m_impl->path);
    return 0.0f;
}

float Canvas::add_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    if (!m_impl) return 0;
    auto& st = m_impl->state();
    if (!st.font_face || st.font_size <= 0.0f)
        return 0.0f;

    TextIterator it(text, length, encoding);
    float advance = 0.0f;
    while (it.has_next()) {
        Codepoint cp = it.next();
        advance += st.font_face.get_glyph_path(st.font_size, x + advance, y, cp, m_impl->path);
    }
    return advance;
}

float Canvas::fill_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    if (!m_impl) return 0;
    new_path();
    float advance = add_text(text, length, encoding, x, y);
    fill();
    return advance;
}

float Canvas::stroke_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    if (!m_impl) return 0;
    new_path();
    float advance = add_text(text, length, encoding, x, y);
    stroke();
    return advance;
}

float Canvas::clip_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    if (!m_impl) return 0;
    new_path();
    float advance = add_text(text, length, encoding, x, y);
    clip();
    return advance;
}

// Text metrics

FontMetrics Canvas::font_metrics() const
{
    if (!m_impl) return {};
    auto& st = m_impl->state();
    if (st.font_face && st.font_size > 0.0f)
        return st.font_face.metrics(st.font_size);
    return {};
}

GlyphMetrics Canvas::glyph_metrics(Codepoint codepoint) const
{
    if (!m_impl) return {};
    auto& st = m_impl->state();
    if (st.font_face && st.font_size > 0.0f)
        return st.font_face.glyph_metrics(st.font_size, codepoint);
    return {};
}

float Canvas::text_extents(const void* text, int length, TextEncoding encoding, Rect* extents) const
{
    if (!m_impl) return 0;
    auto& st = m_impl->state();
    if (st.font_face && st.font_size > 0.0f)
        return st.font_face.text_extents(st.font_size, text, length, encoding, extents);
    if (extents)
        *extents = {};
    return 0.0f;
}

} // namespace plutovg
