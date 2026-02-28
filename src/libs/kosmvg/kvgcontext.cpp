// kvgcontext.cpp — Abstract Context: state machine + draw methods
// C++20
//
// Implements every Context method declared in kosmvg.hpp except the
// BitmapContext lives in a separate translation unit.

#include "kvgprivate.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

namespace kvg {

// -- Lifecycle --

Context::Context() : m_impl(new Impl) {}

Context::Context(Impl* impl) : m_impl(impl) {}

Context::~Context() {
    delete m_impl;
}

Context::Context(Context&& o) noexcept : m_impl(o.m_impl) {
    o.m_impl = nullptr;
}

Context& Context::operator=(Context&& o) noexcept {
    if (this != &o) {
        delete m_impl;
        m_impl = o.m_impl;
        o.m_impl = nullptr;
    }
    return *this;
}

Context::operator bool() const {
    return m_impl != nullptr && m_impl->render_data != nullptr;
}

// -- Graphics state stack --

void Context::save_state() {
    if (!m_impl) return;
    m_impl->states.push();
}

void Context::restore_state() {
    if (!m_impl) return;

    auto& st = m_impl->state();

    // If the current state owns a layer, composite it back before popping.
    // end_transparency_layer() internally calls restore_state() which does
    // the pop, so we must return here to avoid a double pop.
    if (st.layer.has_value()) {
        end_transparency_layer();
        return;
    }

    m_impl->states.pop();
}

// -- CTM --

void Context::concat_ctm(const AffineTransform& t) {
    if (!m_impl) return;
    m_impl->state().matrix = m_impl->state().matrix * t;
}

void Context::translate_ctm(float tx, float ty) {
    concat_ctm(AffineTransform::translated(tx, ty));
}

void Context::scale_ctm(float sx, float sy) {
    concat_ctm(AffineTransform::scaled(sx, sy));
}

void Context::rotate_ctm(float radians) {
    concat_ctm(AffineTransform::rotated(radians));
}

AffineTransform Context::ctm() const {
    if (!m_impl) return AffineTransform::identity();
    return m_impl->state().matrix;
}

Point Context::convert_to_device(Point p) const {
    if (!m_impl) return p;
    return m_impl->state().matrix.apply(p);
}

Point Context::convert_to_user(Point p) const {
    if (!m_impl) return p;
    auto inv = m_impl->state().matrix.inverted();
    return inv ? inv->apply(p) : p;
}

// -- Clipping --

void Context::clip_to_path(const Path& path, FillRule rule) {
    if (!m_impl) return;
    const auto* pi = path_impl(path);
    if (!pi || pi->commands.empty()) return;

    auto& st = m_impl->state();

    SpanBuffer path_spans;
    rasterize(path_spans, *pi, st.matrix, m_impl->clip_rect, nullptr, rule, st.antialias);

    if (st.clipping) {
        SpanBuffer combined;
        span_buffer_intersect(combined, st.clip_spans, path_spans);
        st.clip_spans = std::move(combined);
    } else {
        st.clip_spans = std::move(path_spans);
        st.clipping = true;
    }
}

void Context::clip_to_rect(const Rect& rect) {
    auto path = Path::Builder{}.add_rect(rect).build();
    clip_to_path(path);
}

void Context::clip_to_rects(std::span<const Rect> rects) {
    if (rects.empty()) return;
    Path::Builder builder;
    for (const auto& r : rects)
        builder.add_rect(r);
    clip_to_path(builder.build());
}

void Context::clip_to_mask(const Image& mask, const Rect& rect, MaskMode mode) {
    if (!m_impl || !mask) return;

    auto& st = m_impl->state();
    const auto* mi = image_impl(mask);
    if (!mi) return;

    // Rasterize a solid rect covering the mask area.
    auto rect_path = Path::Builder{}.add_rect(rect).build();
    const auto* pi = path_impl(rect_path);
    if (!pi) return;

    SpanBuffer rect_spans;
    rasterize(rect_spans, *pi, st.matrix, m_impl->clip_rect, nullptr, FillRule::Winding, false);

    Point device_origin = st.matrix.apply({rect.x(), rect.y()});
    int mask_ox = static_cast<int>(std::floor(device_origin.x));
    int mask_oy = static_cast<int>(std::floor(device_origin.y));

    // Modulate coverage by mask pixels — split into per-pixel spans.
    SpanBuffer mask_spans;
    mask_spans.spans.reserve(rect_spans.spans.size() * 2);
    for (const auto& span : rect_spans.spans) {
        for (int i = 0; i < span.len; ++i) {
            int px = span.x + i;
            int mx = px - mask_ox;
            int my = span.y - mask_oy;
            uint8_t mask_cov = 0;
            if (mx >= 0 && mx < mi->width && my >= 0 && my < mi->height) {
                if (mi->format == PixelFormat::A8)
                    mask_cov = mi->data[my * mi->stride + mx];
                else {
                    auto* row = reinterpret_cast<const uint32_t*>(mi->data + my * mi->stride);
                    mask_cov = mask_ops::extract_coverage(row[mx], mode);
                }
            }
            uint8_t combined = static_cast<uint8_t>((uint32_t(span.coverage) * mask_cov) / 255);
            if (combined > 0)
                mask_spans.spans.push_back({px, 1, span.y, combined});
        }
    }

    if (st.clipping) {
        SpanBuffer combined;
        span_buffer_intersect(combined, st.clip_spans, mask_spans);
        st.clip_spans = std::move(combined);
    } else {
        st.clip_spans = std::move(mask_spans);
        st.clipping = true;
    }
}

void Context::clip_to_region(std::span<const IntRect> rects) {
    if (!m_impl || rects.empty()) return;

    auto& st = m_impl->state();
    SpanBuffer region_spans;
    span_buffer_init_region(region_spans, rects);

    if (st.clipping) {
        SpanBuffer combined;
        span_buffer_intersect(combined, st.clip_spans, region_spans);
        st.clip_spans = std::move(combined);
    } else {
        st.clip_spans = std::move(region_spans);
        st.clipping = true;
    }
}

Rect Context::clip_bounding_box() const {
    if (!m_impl) return {};
    const auto& st = m_impl->state();
    if (!st.clipping) {
        auto inv = st.matrix.inverted();
        if (!inv) return {};
        Rect device{0, 0,
                    static_cast<float>(m_impl->render_width),
                    static_cast<float>(m_impl->render_height)};
        return inv->apply_to_rect(device);
    }
    Rect extents;
    span_buffer_extents(st.clip_spans, extents);
    auto inv = st.matrix.inverted();
    return inv ? inv->apply_to_rect(extents) : extents;
}

// -- Fill / stroke colors --

void Context::set_color(const Color& c) {
    set_fill_color(c);
    set_stroke_color(c);
}

void Context::set_color(float r, float g, float b, float a) {
    set_color(Color{r, g, b, a});
}

void Context::set_fill_color(const Color& c) {
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.fill_color = c;
    st.has_fill_pattern = false;
}

void Context::set_fill_color(float r, float g, float b, float a) {
    set_fill_color(Color{r, g, b, a});
}

void Context::set_stroke_color(const Color& c) {
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.stroke_color = c;
    st.has_stroke_pattern = false;
}

void Context::set_stroke_color(float r, float g, float b, float a) {
    set_stroke_color(Color{r, g, b, a});
}

Color Context::fill_color() const {
    if (!m_impl) return Color::black();
    return m_impl->state().fill_color;
}

Color Context::stroke_color() const {
    if (!m_impl) return Color::black();
    return m_impl->state().stroke_color;
}

// -- Fill / stroke patterns --

void Context::set_fill_pattern(const Pattern& pattern) {
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.fill_pattern = pattern;
    st.has_fill_pattern = static_cast<bool>(pattern);
}

void Context::set_stroke_pattern(const Pattern& pattern) {
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.stroke_pattern = pattern;
    st.has_stroke_pattern = static_cast<bool>(pattern);
}

// -- Stroke parameters --

void Context::set_line_width(float width) {
    if (!m_impl) return;
    m_impl->state().stroke.style.width = width;
}

void Context::set_line_cap(LineCap cap) {
    if (!m_impl) return;
    m_impl->state().stroke.style.cap = cap;
}

void Context::set_line_join(LineJoin join) {
    if (!m_impl) return;
    m_impl->state().stroke.style.join = join;
}

void Context::set_miter_limit(float limit) {
    if (!m_impl) return;
    m_impl->state().stroke.style.miter_limit = limit;
}

void Context::set_line_dash(float phase, std::span<const float> lengths) {
    if (!m_impl) return;
    auto& dash = m_impl->state().stroke.dash;
    dash.offset = phase;
    dash.array.assign(lengths.begin(), lengths.end());
}

void Context::clear_line_dash() {
    if (!m_impl) return;
    auto& dash = m_impl->state().stroke.dash;
    dash.offset = 0.0f;
    dash.array.clear();
}

float Context::line_width() const {
    if (!m_impl) return 1.0f;
    return m_impl->state().stroke.style.width;
}

LineCap Context::line_cap() const {
    if (!m_impl) return LineCap::Butt;
    return m_impl->state().stroke.style.cap;
}

LineJoin Context::line_join() const {
    if (!m_impl) return LineJoin::Miter;
    return m_impl->state().stroke.style.join;
}

float Context::miter_limit() const {
    if (!m_impl) return 10.0f;
    return m_impl->state().stroke.style.miter_limit;
}

int Context::line_dash(float* phase, float* lengths, int max_lengths) const {
    if (!m_impl) {
        if (phase) *phase = 0.0f;
        return 0;
    }
    const auto& dash = m_impl->state().stroke.dash;
    if (phase) *phase = dash.offset;
    int count = static_cast<int>(dash.array.size());
    if (lengths && max_lengths > 0) {
        int n = std::min(count, max_lengths);
        std::memcpy(lengths, dash.array.data(), static_cast<size_t>(n) * sizeof(float));
    }
    return count;
}

// -- Compositing & blending --

void Context::set_composite_op(CompositeOp op) {
    if (!m_impl) return;
    m_impl->state().op = op;
}

CompositeOp Context::composite_op() const {
    if (!m_impl) return CompositeOp::SourceOver;
    return m_impl->state().op;
}

void Context::set_blend_mode(BlendMode mode) {
    if (!m_impl) return;
    m_impl->state().blend_mode = mode;
}

BlendMode Context::blend_mode() const {
    if (!m_impl) return BlendMode::Normal;
    return m_impl->state().blend_mode;
}

void Context::set_opacity(float o) {
    if (!m_impl) return;
    m_impl->state().opacity = std::clamp(o, 0.0f, 1.0f);
}

float Context::opacity() const {
    if (!m_impl) return 1.0f;
    return m_impl->state().opacity;
}

void Context::set_alpha(float a) {
    if (!m_impl) return;
    m_impl->state().alpha = std::clamp(a, 0.0f, 1.0f);
}

float Context::alpha() const {
    if (!m_impl) return 1.0f;
    return m_impl->state().alpha;
}

// -- Shadow --

void Context::set_shadow(const Shadow& s) {
    if (!m_impl) return;
    m_impl->state().shadow = s;
}

void Context::set_shadow(Point offset, float blur, const Color& color) {
    set_shadow(Shadow{offset, blur, color});
}

void Context::clear_shadow() {
    if (!m_impl) return;
    m_impl->state().shadow = Shadow{};
}

Shadow Context::shadow() const {
    if (!m_impl) return {};
    return m_impl->state().shadow;
}

// -- Anti-aliasing --

void Context::set_should_antialias(bool aa) {
    if (!m_impl) return;
    m_impl->state().antialias = aa;
}

bool Context::should_antialias() const {
    if (!m_impl) return true;
    return m_impl->state().antialias;
}

// -- Interpolation quality --

void Context::set_interpolation_quality(InterpolationQuality q) {
    if (!m_impl) return;
    m_impl->state().interpolation = q;
}

InterpolationQuality Context::interpolation_quality() const {
    if (!m_impl) return InterpolationQuality::Default;
    return m_impl->state().interpolation;
}

// -- Pixel snapping --

void Context::set_allows_pixel_snapping(bool snap) {
    if (!m_impl) return;
    m_impl->state().pixel_snap = snap;
}

bool Context::allows_pixel_snapping() const {
    if (!m_impl) return false;
    return m_impl->state().pixel_snap;
}

// -- Dithering --

void Context::set_dithering(bool enabled) {
    if (!m_impl) return;
    m_impl->state().dithering = enabled;
}

bool Context::dithering() const {
    if (!m_impl) return false;
    return m_impl->state().dithering;
}

// -- Tone mapping --

void Context::set_tone_mapping(bool enabled) {
    if (!m_impl) return;
    m_impl->state().tone_mapping = enabled;
}

bool Context::tone_mapping() const {
    if (!m_impl) return false;
    return m_impl->state().tone_mapping;
}


//  Internal helpers


namespace {

float effective_opacity(const State& st) {
    return st.opacity * st.alpha;
}

PaintSource make_solid_paint(const Color& c) {
    PaintSource ps;
    ps.kind = PaintKind::Solid;
    ps.solid_argb = c.premultiplied().to_argb32();
    return ps;
}

PaintSource make_fill_paint(const State& st) {
    if (st.has_fill_pattern) {
        PaintSource ps;
        ps.kind = PaintKind::PatternDraw;
        ps.pattern = pattern_impl(st.fill_pattern);
        return ps;
    }
    return make_solid_paint(st.fill_color);
}

PaintSource make_stroke_paint(const State& st) {
    if (st.has_stroke_pattern) {
        PaintSource ps;
        ps.kind = PaintKind::PatternDraw;
        ps.pattern = pattern_impl(st.stroke_pattern);
        return ps;
    }
    return make_solid_paint(st.stroke_color);
}

BlendParams make_blend_params(Context::Impl& ctx, const PaintSource& paint) {
    auto& st = ctx.state();
    return {
        ctx.render_data,
        ctx.render_width,
        ctx.render_height,
        ctx.render_stride,
        ctx.render_format,
        &paint,
        st.op,
        st.blend_mode,
        ctx.working_space,
        st.opacity * st.alpha,
        st.dithering
    };
}

// Source-over composite for a single pixel (premultiplied ARGB).
inline uint32_t composite_src_over(uint32_t src, uint32_t dst) {
    uint32_t sa = pixel_alpha(src);
    if (sa == 255) return src;
    if (sa == 0)   return dst;
    uint32_t inv_sa = 255 - sa;
    return pack_argb(
        static_cast<uint8_t>(std::min(255u, sa + (uint32_t(pixel_alpha(dst)) * inv_sa) / 255)),
        static_cast<uint8_t>(std::min(255u, red(src)   + (uint32_t(red(dst))   * inv_sa) / 255)),
        static_cast<uint8_t>(std::min(255u, green(src) + (uint32_t(green(dst)) * inv_sa) / 255)),
        static_cast<uint8_t>(std::min(255u, blue(src)  + (uint32_t(blue(dst))  * inv_sa) / 255)));
}

// Row index for O(1) scanline lookup into a SpanBuffer.
// Replaces per-pixel binary search with direct row access.
struct ClipRowIndex {
    int min_y = 0;
    int max_y = -1;
    // For each row y in [min_y, max_y]: row_starts[y - min_y] is the index
    // of the first span on that row, row_ends[y - min_y] is one past the last.
    std::vector<int> row_starts;
    std::vector<int> row_ends;

    void build(const SpanBuffer& buf) {
        if (buf.spans.empty()) return;
        min_y = buf.spans.front().y;
        max_y = buf.spans.back().y;
        int rows = max_y - min_y + 1;
        row_starts.assign(static_cast<size_t>(rows), -1);
        row_ends.assign(static_cast<size_t>(rows), -1);

        for (int i = 0, n = static_cast<int>(buf.spans.size()); i < n; ++i) {
            int ry = buf.spans[i].y - min_y;
            if (row_starts[ry] < 0)
                row_starts[ry] = i;
            row_ends[ry] = i + 1;
        }
    }

    // Find coverage for pixel (px, py). Returns 0 if not in clip.
    // out_in_clip is set to true if the pixel falls within a clip span.
    uint8_t lookup(const SpanBuffer& buf, int px, int py, bool& out_in_clip) const {
        out_in_clip = false;
        if (py < min_y || py > max_y) return 0;
        int ry = py - min_y;
        int start = row_starts[ry];
        if (start < 0) return 0;
        int end = row_ends[ry];
        for (int i = start; i < end; ++i) {
            const auto& s = buf.spans[i];
            if (px >= s.x && px < s.x + s.len) {
                out_in_clip = true;
                return s.coverage;
            }
            if (s.x > px) break;
        }
        return 0;
    }
};

void draw_shadow(Context::Impl& ctx, const SpanBuffer& shape_spans, const Shadow& shd) {
    if (shd.is_none()) return;

    Rect shape_extents;
    span_buffer_extents(shape_spans, shape_extents);
    if (shape_extents.empty()) return;

    int blur_expand = static_cast<int>(std::ceil(shd.blur * 2.0f));
    int sx = static_cast<int>(std::floor(shape_extents.x() + shd.offset.x)) - blur_expand;
    int sy = static_cast<int>(std::floor(shape_extents.y() + shd.offset.y)) - blur_expand;
    int sw = static_cast<int>(std::ceil(shape_extents.width()))  + 2 * blur_expand + 2;
    int sh = static_cast<int>(std::ceil(shape_extents.height())) + 2 * blur_expand + 2;
    if (sw <= 0 || sh <= 0) return;

    int stride = sw * 4;
    size_t buf_size = static_cast<size_t>(sh) * stride;

    // Reuse cached buffer to avoid per-frame heap allocation.
    ctx.shadow_buf_cache.resize(buf_size);
    std::memset(ctx.shadow_buf_cache.data(), 0, buf_size);

    Color shadow_premul = shd.color.premultiplied();
    uint32_t shadow_argb = shadow_premul.to_argb32();

    int off_x = static_cast<int>(std::round(shd.offset.x));
    int off_y = static_cast<int>(std::round(shd.offset.y));

    for (const auto& span : shape_spans.spans) {
        int dst_y = span.y + off_y - sy;
        int dst_x = span.x + off_x - sx;
        if (dst_y < 0 || dst_y >= sh) continue;
        auto* row = reinterpret_cast<uint32_t*>(ctx.shadow_buf_cache.data() + dst_y * stride);
        for (int i = 0; i < span.len; ++i) {
            int px = dst_x + i;
            if (px >= 0 && px < sw)
                row[px] = byte_mul(shadow_argb, span.coverage);
        }
    }

    if (shd.blur > 0.0f)
        gaussian_blur(ctx.shadow_buf_cache.data(), sw, sh, stride, shd.blur, &ctx.blur_tmp_cache);

    auto& st = ctx.state();

    // Build row index for O(1) clip lookup (replaces per-pixel binary search).
    ClipRowIndex clip_idx;
    if (st.clipping)
        clip_idx.build(st.clip_spans);

    for (int y = 0; y < sh; ++y) {
        int dst_y = sy + y;
        if (dst_y < 0 || dst_y >= ctx.render_height) continue;

        auto* s_row = reinterpret_cast<const uint32_t*>(ctx.shadow_buf_cache.data() + y * stride);
        auto* t_row = reinterpret_cast<uint32_t*>(ctx.render_data + dst_y * ctx.render_stride);

        for (int x = 0; x < sw; ++x) {
            int dst_x = sx + x;
            if (dst_x < 0 || dst_x >= ctx.render_width) continue;

            uint32_t src = s_row[x];
            if (pixel_alpha(src) == 0) continue;

            if (st.clipping) {
                bool in_clip = false;
                uint8_t cov = clip_idx.lookup(st.clip_spans, dst_x, dst_y, in_clip);
                if (!in_clip) continue;
                src = byte_mul(src, cov);
            }

            t_row[dst_x] = composite_pixel(src, t_row[dst_x], st.op, st.blend_mode,
                                           ctx.working_space);
        }
    }
}

void do_fill(Context::Impl& ctx, const Path::Impl& pi, FillRule rule, const MaskFilter* mf) {
    auto& st = ctx.state();

    SpanBuffer& spans = ctx.fill_spans;
    rasterize(spans, pi, st.matrix, ctx.clip_rect, nullptr, rule, st.antialias);
    if (spans.spans.empty()) return;

    // Apply MaskFilter (blur the coverage mask before colorization).
    if (mf && !mf->is_none()) {
        Rect extents;
        span_buffer_extents(spans, extents);
        if (extents.empty()) return;

        float blur_rad = mf->blur_radius();
        int expand = static_cast<int>(std::ceil(blur_rad));

        int bx = static_cast<int>(std::floor(extents.x())) - expand;
        int by = static_cast<int>(std::floor(extents.y())) - expand;
        int bw = static_cast<int>(std::ceil(extents.width())) + 2 * expand + 2;
        int bh = static_cast<int>(std::ceil(extents.height())) + 2 * expand + 2;
        if (bw <= 0 || bh <= 0) return;

        int mask_stride = bw;
        std::vector<unsigned char> mask_buf(static_cast<size_t>(bh) * mask_stride, 0);

        for (const auto& span : spans.spans) {
            int my = span.y - by;
            if (my < 0 || my >= bh) continue;
            for (int i = 0; i < span.len; ++i) {
                int mx = span.x + i - bx;
                if (mx >= 0 && mx < bw)
                    mask_buf[my * mask_stride + mx] = span.coverage;
            }
        }

        // Save original (pre-blur) mask for Style processing.
        std::vector<unsigned char> orig_mask;
        if (mf->style != MaskFilter::Style::Normal)
            orig_mask = mask_buf;

        if (blur_rad > 0.0f) {
            int argb_stride = bw * 4;
            std::vector<unsigned char> argb_buf(static_cast<size_t>(bh) * argb_stride, 0);
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    uint8_t c = mask_buf[y * mask_stride + x];
                    reinterpret_cast<uint32_t*>(argb_buf.data() + y * argb_stride)[x] =
                        pack_argb(c, 0, 0, 0);
                }
            gaussian_blur(argb_buf.data(), bw, bh, argb_stride, blur_rad);
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x)
                    mask_buf[y * mask_stride + x] =
                        pixel_alpha(reinterpret_cast<uint32_t*>(argb_buf.data() + y * argb_stride)[x]);
        }

        // Apply MaskFilter::Style.
        switch (mf->style) {
        case MaskFilter::Style::Normal:
            // Blurred mask used as-is.
            break;
        case MaskFilter::Style::Solid:
            // Interior stays fully opaque; outer blur fringe is kept.
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    int idx = y * mask_stride + x;
                    if (orig_mask[idx] > 0)
                        mask_buf[idx] = std::max(mask_buf[idx], orig_mask[idx]);
                }
            break;
        case MaskFilter::Style::Outer:
            // Only the blur region outside the original shape.
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    int idx = y * mask_stride + x;
                    if (orig_mask[idx] > 0)
                        mask_buf[idx] = 0;
                }
            break;
        case MaskFilter::Style::Inner:
            // Only the blur region inside the original shape.
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    int idx = y * mask_stride + x;
                    if (orig_mask[idx] == 0)
                        mask_buf[idx] = 0;
                    else
                        mask_buf[idx] = std::min(mask_buf[idx], orig_mask[idx]);
                }
            break;
        }

        // Rebuild spans from processed mask.
        spans.spans.clear();
        for (int y = 0; y < bh; ++y) {
            int abs_y = by + y;
            int run_start = -1;
            uint8_t run_cov = 0;
            for (int x = 0; x <= bw; ++x) {
                uint8_t c = (x < bw) ? mask_buf[y * mask_stride + x] : 0;
                if (c > 0 && c == run_cov) continue;
                if (run_start >= 0 && run_cov > 0)
                    spans.spans.push_back({bx + run_start, x - run_start, abs_y, run_cov});
                if (c > 0) { run_start = x; run_cov = c; }
                else { run_start = -1; run_cov = 0; }
            }
        }
        if (spans.spans.empty()) return;
    }

    SpanBuffer clipped;
    const SpanBuffer* draw_spans = &spans;
    if (st.clipping) {
        span_buffer_intersect(clipped, spans, st.clip_spans);
        draw_spans = &clipped;
    }
    if (draw_spans->spans.empty()) return;

    if (!st.shadow.is_none())
        draw_shadow(ctx, *draw_spans, st.shadow);

    PaintSource paint = make_fill_paint(st);
    BlendParams params = make_blend_params(ctx, paint);
    blend(params, *draw_spans, ctx.clip_rect, st.clipping ? &st.clip_spans : nullptr);
}

void do_stroke(Context::Impl& ctx, const Path::Impl& pi, const MaskFilter* mf) {
    auto& st = ctx.state();

    SpanBuffer& spans = ctx.fill_spans;
    rasterize(spans, pi, st.matrix, ctx.clip_rect, &st.stroke, FillRule::Winding, st.antialias);
    if (spans.spans.empty()) return;

    // Apply MaskFilter (blur the coverage mask before colorization).
    if (mf && !mf->is_none()) {
        Rect extents;
        span_buffer_extents(spans, extents);
        if (extents.empty()) return;

        float blur_rad = mf->blur_radius();
        int expand = static_cast<int>(std::ceil(blur_rad));

        int bx = static_cast<int>(std::floor(extents.x())) - expand;
        int by = static_cast<int>(std::floor(extents.y())) - expand;
        int bw = static_cast<int>(std::ceil(extents.width())) + 2 * expand + 2;
        int bh = static_cast<int>(std::ceil(extents.height())) + 2 * expand + 2;
        if (bw <= 0 || bh <= 0) return;

        int mask_stride = bw;
        std::vector<unsigned char> mask_buf(static_cast<size_t>(bh) * mask_stride, 0);

        for (const auto& span : spans.spans) {
            int my = span.y - by;
            if (my < 0 || my >= bh) continue;
            for (int i = 0; i < span.len; ++i) {
                int mx = span.x + i - bx;
                if (mx >= 0 && mx < bw)
                    mask_buf[my * mask_stride + mx] = span.coverage;
            }
        }

        std::vector<unsigned char> orig_mask;
        if (mf->style != MaskFilter::Style::Normal)
            orig_mask = mask_buf;

        if (blur_rad > 0.0f) {
            int argb_stride = bw * 4;
            std::vector<unsigned char> argb_buf(static_cast<size_t>(bh) * argb_stride, 0);
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    uint8_t c = mask_buf[y * mask_stride + x];
                    reinterpret_cast<uint32_t*>(argb_buf.data() + y * argb_stride)[x] =
                        pack_argb(c, 0, 0, 0);
                }
            gaussian_blur(argb_buf.data(), bw, bh, argb_stride, blur_rad);
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x)
                    mask_buf[y * mask_stride + x] =
                        pixel_alpha(reinterpret_cast<uint32_t*>(argb_buf.data() + y * argb_stride)[x]);
        }

        switch (mf->style) {
        case MaskFilter::Style::Normal:
            break;
        case MaskFilter::Style::Solid:
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    int idx = y * mask_stride + x;
                    if (orig_mask[idx] > 0)
                        mask_buf[idx] = std::max(mask_buf[idx], orig_mask[idx]);
                }
            break;
        case MaskFilter::Style::Outer:
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    int idx = y * mask_stride + x;
                    if (orig_mask[idx] > 0)
                        mask_buf[idx] = 0;
                }
            break;
        case MaskFilter::Style::Inner:
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    int idx = y * mask_stride + x;
                    if (orig_mask[idx] == 0)
                        mask_buf[idx] = 0;
                    else
                        mask_buf[idx] = std::min(mask_buf[idx], orig_mask[idx]);
                }
            break;
        }

        spans.spans.clear();
        for (int y = 0; y < bh; ++y) {
            int abs_y = by + y;
            int run_start = -1;
            uint8_t run_cov = 0;
            for (int x = 0; x <= bw; ++x) {
                uint8_t c = (x < bw) ? mask_buf[y * mask_stride + x] : 0;
                if (c > 0 && c == run_cov) continue;
                if (run_start >= 0 && run_cov > 0)
                    spans.spans.push_back({bx + run_start, x - run_start, abs_y, run_cov});
                if (c > 0) { run_start = x; run_cov = c; }
                else { run_start = -1; run_cov = 0; }
            }
        }
        if (spans.spans.empty()) return;
    }

    SpanBuffer clipped;
    const SpanBuffer* draw_spans = &spans;
    if (st.clipping) {
        span_buffer_intersect(clipped, spans, st.clip_spans);
        draw_spans = &clipped;
    }
    if (draw_spans->spans.empty()) return;

    if (!st.shadow.is_none())
        draw_shadow(ctx, *draw_spans, st.shadow);

    PaintSource paint = make_stroke_paint(st);
    BlendParams params = make_blend_params(ctx, paint);
    blend(params, *draw_spans, ctx.clip_rect, st.clipping ? &st.clip_spans : nullptr);
}

} // anonymous namespace


//  Path drawing


void Context::fill_path(const Path& path, FillRule rule) {
    if (!m_impl || !m_impl->render_data) return;
    const auto* pi = path_impl(path);
    if (!pi || pi->commands.empty()) return;
    do_fill(*m_impl, *pi, rule, nullptr);
}

void Context::stroke_path(const Path& path) {
    if (!m_impl || !m_impl->render_data) return;
    const auto* pi = path_impl(path);
    if (!pi || pi->commands.empty()) return;
    do_stroke(*m_impl, *pi, nullptr);
}

void Context::draw_path(const Path& path, FillRule rule) {
    fill_path(path, rule);
    stroke_path(path);
}

void Context::fill_path(const Path& path, const MaskFilter& filter, FillRule rule) {
    if (filter.is_none()) { fill_path(path, rule); return; }
    if (!m_impl || !m_impl->render_data) return;
    const auto* pi = path_impl(path);
    if (!pi || pi->commands.empty()) return;
    do_fill(*m_impl, *pi, rule, &filter);
}

void Context::stroke_path(const Path& path, const MaskFilter& filter) {
    if (filter.is_none()) { stroke_path(path); return; }
    if (!m_impl || !m_impl->render_data) return;
    const auto* pi = path_impl(path);
    if (!pi || pi->commands.empty()) return;
    do_stroke(*m_impl, *pi, &filter);
}

void Context::draw_path(const Path& path, const MaskFilter& filter, FillRule rule) {
    fill_path(path, filter, rule);
    stroke_path(path, filter);
}


//  Rectangle convenience


void Context::fill_rect(const Rect& rect) {
    auto path = Path::Builder{}.add_rect(rect).build();
    fill_path(path);
}

void Context::fill_rects(std::span<const Rect> rects) {
    for (const auto& r : rects)
        fill_rect(r);
}

void Context::stroke_rect(const Rect& rect) {
    auto path = Path::Builder{}.add_rect(rect).build();
    stroke_path(path);
}

void Context::clear_rect(const Rect& rect) {
    if (!m_impl || !m_impl->render_data) return;
    save_state();
    set_composite_op(CompositeOp::Clear);
    set_blend_mode(BlendMode::Normal);
    set_opacity(1.0f);
    set_alpha(1.0f);
    clear_shadow();
    set_fill_color(Color::clear());
    fill_rect(rect);
    restore_state();
}

// -- Ellipse convenience --

void Context::fill_ellipse(const Rect& rect) {
    auto path = Path::Builder{}.add_ellipse(rect).build();
    fill_path(path);
}

void Context::stroke_ellipse(const Rect& rect) {
    auto path = Path::Builder{}.add_ellipse(rect).build();
    stroke_path(path);
}

// -- Rounded rectangle convenience --

void Context::fill_round_rect(const Rect& rect, float radius) {
    auto path = Path::Builder{}.add_round_rect(rect, radius, radius).build();
    fill_path(path);
}

void Context::fill_round_rect(const Rect& rect, const CornerRadii& radii) {
    auto path = Path::Builder{}.add_round_rect(rect, radii).build();
    fill_path(path);
}

void Context::stroke_round_rect(const Rect& rect, float radius) {
    auto path = Path::Builder{}.add_round_rect(rect, radius, radius).build();
    stroke_path(path);
}

void Context::stroke_round_rect(const Rect& rect, const CornerRadii& radii) {
    auto path = Path::Builder{}.add_round_rect(rect, radii).build();
    stroke_path(path);
}


//  Gradient drawing


namespace {

// Prepare spans covering the full clip for gradient / shading fills.
void prepare_clip_spans(Context::Impl& ctx, SpanBuffer& out) {
    auto& st = ctx.state();
    if (st.clipping)
        out = st.clip_spans;
    else
        span_buffer_init_rect(out, 0, 0, ctx.render_width, ctx.render_height);
}

} // anonymous namespace

void Context::draw_linear_gradient(const Gradient& gradient,
                                   Point start, Point end,
                                   const AffineTransform* gradient_transform,
                                   GradientDrawingOptions options) {
    if (!m_impl || !m_impl->render_data || !gradient) return;
    auto& st = m_impl->state();

    SpanBuffer draw_spans;
    prepare_clip_spans(*m_impl, draw_spans);
    if (draw_spans.spans.empty()) return;

    AffineTransform xform = st.matrix;
    if (gradient_transform)
        xform = xform * (*gradient_transform);

    Point dev_start = xform.apply(start);
    Point dev_end   = xform.apply(end);

    bool draws_before = (options & GradientDrawingOptions::DrawsBeforeStart);
    bool draws_after  = (options & GradientDrawingOptions::DrawsAfterEnd);

    // When both extension flags are set, use the fast blend path.
    // When either is missing, we must skip pixels outside the gradient range.
    if (draws_before && draws_after) {
        if (!st.shadow.is_none())
            draw_shadow(*m_impl, draw_spans, st.shadow);

        PaintSource paint;
        paint.kind = PaintKind::LinearGradient;
        paint.gradient = gradient_impl(gradient);
        paint.grad_spread = gradient.spread();
        paint.grad_values[0] = dev_start.x;
        paint.grad_values[1] = dev_start.y;
        paint.grad_values[2] = dev_end.x;
        paint.grad_values[3] = dev_end.y;
        paint.grad_transform = AffineTransform::identity();
        paint.grad_inv_transform = AffineTransform::identity();
        paint.grad_inv_valid = true;

        BlendParams params = make_blend_params(*m_impl, paint);
        blend(params, draw_spans, m_impl->clip_rect, st.clipping ? &st.clip_spans : nullptr);
        return;
    }

    // Per-pixel path: compute t and skip out-of-range pixels.
    if (!st.shadow.is_none())
        draw_shadow(*m_impl, draw_spans, st.shadow);

    float dx = dev_end.x - dev_start.x;
    float dy = dev_end.y - dev_start.y;
    float len_sq = dx * dx + dy * dy;
    const auto* gi = gradient_impl(gradient);

    float eff_op = effective_opacity(st);
    uint32_t op8 = static_cast<uint32_t>(eff_op * 255.0f + 0.5f);
    bool apply_op = (eff_op < 1.0f);
    const ColorSpace* ws = m_impl->working_space;
    bool do_dither = st.dithering;

    for (const auto& span : draw_spans.spans) {
        auto* row = reinterpret_cast<uint32_t*>(
            m_impl->render_data + span.y * m_impl->render_stride);
        for (int i = 0; i < span.len; ++i) {
            int px = span.x + i;
            if (px < 0 || px >= m_impl->render_width) continue;

            float gpx = px + 0.5f, gpy = span.y + 0.5f;
            float t = (len_sq > 1e-10f)
                ? ((gpx - dev_start.x) * dx + (gpy - dev_start.y) * dy) / len_sq
                : 0.0f;

            if (t < 0.0f && !draws_before) continue;
            if (t > 1.0f && !draws_after)  continue;

            uint32_t src = sample_gradient_argb(gi, t);
            if (span.coverage < 255)
                src = byte_mul(src, span.coverage);
            if (apply_op)
                src = byte_mul(src, op8);
            if (pixel_alpha(src) == 0) continue;
            uint32_t result = composite_pixel(src, row[px], st.op, st.blend_mode, ws);
            if (do_dither)
                result = dither_pixel_srgb(result, px, span.y);
            row[px] = result;
        }
    }
}

void Context::draw_radial_gradient(const Gradient& gradient,
                                   Point start_center, float start_radius,
                                   Point end_center, float end_radius,
                                   const AffineTransform* gradient_transform,
                                   GradientDrawingOptions options) {
    if (!m_impl || !m_impl->render_data || !gradient) return;
    auto& st = m_impl->state();

    SpanBuffer draw_spans;
    prepare_clip_spans(*m_impl, draw_spans);
    if (draw_spans.spans.empty()) return;

    AffineTransform xform = st.matrix;
    if (gradient_transform)
        xform = xform * (*gradient_transform);

    float scale_x = std::sqrt(xform.a * xform.a + xform.b * xform.b);
    float scale_y = std::sqrt(xform.c * xform.c + xform.d * xform.d);
    float avg_scale = (scale_x + scale_y) * 0.5f;

    Point dev_end   = xform.apply(end_center);
    Point dev_start = xform.apply(start_center);
    float dev_er = end_radius * avg_scale;
    float dev_sr = start_radius * avg_scale;

    bool draws_before = (options & GradientDrawingOptions::DrawsBeforeStart);
    bool draws_after  = (options & GradientDrawingOptions::DrawsAfterEnd);

    if (draws_before && draws_after) {
        if (!st.shadow.is_none())
            draw_shadow(*m_impl, draw_spans, st.shadow);

        PaintSource paint;
        paint.kind = PaintKind::RadialGradient;
        paint.gradient = gradient_impl(gradient);
        paint.grad_spread = gradient.spread();
        paint.grad_values[0] = dev_end.x;
        paint.grad_values[1] = dev_end.y;
        paint.grad_values[2] = dev_er;
        paint.grad_values[3] = dev_start.x;
        paint.grad_values[4] = dev_start.y;
        paint.grad_values[5] = dev_sr;
        paint.grad_transform = AffineTransform::identity();
        paint.grad_inv_transform = AffineTransform::identity();
        paint.grad_inv_valid = true;

        BlendParams params = make_blend_params(*m_impl, paint);
        blend(params, draw_spans, m_impl->clip_rect, st.clipping ? &st.clip_spans : nullptr);
        return;
    }

    // Per-pixel path with two-point radial: solve for t.
    if (!st.shadow.is_none())
        draw_shadow(*m_impl, draw_spans, st.shadow);

    float cx = dev_end.x, cy = dev_end.y, cr = dev_er;
    float fx = dev_start.x, fy = dev_start.y, fr = dev_sr;
    const auto* gi = gradient_impl(gradient);

    float eff_op = effective_opacity(st);
    uint32_t op8 = static_cast<uint32_t>(eff_op * 255.0f + 0.5f);
    bool apply_op = (eff_op < 1.0f);
    const ColorSpace* ws = m_impl->working_space;
    bool do_dither = st.dithering;

    for (const auto& span : draw_spans.spans) {
        auto* row = reinterpret_cast<uint32_t*>(
            m_impl->render_data + span.y * m_impl->render_stride);
        for (int i = 0; i < span.len; ++i) {
            int px = span.x + i;
            if (px < 0 || px >= m_impl->render_width) continue;

            float gpx = px + 0.5f, gpy = span.y + 0.5f;
            float ddx = gpx - fx, ddy = gpy - fy;
            float cdx = cx - fx, cdy = cy - fy;
            float dr = cr - fr;

            float qa = cdx * cdx + cdy * cdy - dr * dr;
            float qb = 2.0f * (ddx * cdx + ddy * cdy + fr * dr);
            float qc = ddx * ddx + ddy * ddy - fr * fr;

            float t = 0.0f;
            if (std::abs(qa) < 1e-10f) {
                if (std::abs(qb) > 1e-10f) t = -qc / qb;
            } else {
                float disc = qb * qb - 4.0f * qa * qc;
                if (disc >= 0.0f) {
                    float sq = std::sqrt(disc);
                    float t1 = (-qb + sq) / (2.0f * qa);
                    float t2 = (-qb - sq) / (2.0f * qa);
                    t = t1;
                    if (fr + t1 * dr < 0.0f) t = t2;
                    if (t2 > t && fr + t2 * dr >= 0.0f) t = t2;
                }
            }

            if (t < 0.0f && !draws_before) continue;
            if (t > 1.0f && !draws_after)  continue;

            uint32_t src = sample_gradient_argb(gi, t);
            if (span.coverage < 255)
                src = byte_mul(src, span.coverage);
            if (apply_op)
                src = byte_mul(src, op8);
            if (pixel_alpha(src) == 0) continue;
            uint32_t result = composite_pixel(src, row[px], st.op, st.blend_mode, ws);
            if (do_dither)
                result = dither_pixel_srgb(result, px, span.y);
            row[px] = result;
        }
    }
}

void Context::draw_conic_gradient(const Gradient& gradient,
                                  Point center, float start_angle,
                                  const AffineTransform* gradient_transform,
                                  GradientDrawingOptions options) {
    if (!m_impl || !m_impl->render_data || !gradient) return;
    auto& st = m_impl->state();

    SpanBuffer draw_spans;
    prepare_clip_spans(*m_impl, draw_spans);
    if (draw_spans.spans.empty()) return;

    AffineTransform xform = st.matrix;
    if (gradient_transform)
        xform = xform * (*gradient_transform);

    Point dev_center = xform.apply(center);

    bool draws_before = (options & GradientDrawingOptions::DrawsBeforeStart);
    bool draws_after  = (options & GradientDrawingOptions::DrawsAfterEnd);

    if (draws_before && draws_after) {
        if (!st.shadow.is_none())
            draw_shadow(*m_impl, draw_spans, st.shadow);

        PaintSource paint;
        paint.kind = PaintKind::ConicGradient;
        paint.gradient = gradient_impl(gradient);
        paint.grad_spread = gradient.spread();
        paint.grad_values[0] = dev_center.x;
        paint.grad_values[1] = dev_center.y;
        paint.grad_values[2] = start_angle;
        paint.grad_transform = AffineTransform::identity();
        paint.grad_inv_transform = AffineTransform::identity();
        paint.grad_inv_valid = true;

        BlendParams params = make_blend_params(*m_impl, paint);
        blend(params, draw_spans, m_impl->clip_rect, st.clipping ? &st.clip_spans : nullptr);
        return;
    }

    // Per-pixel path with range checks.
    if (!st.shadow.is_none())
        draw_shadow(*m_impl, draw_spans, st.shadow);

    const auto* gi = gradient_impl(gradient);

    float eff_op = effective_opacity(st);
    uint32_t op8 = static_cast<uint32_t>(eff_op * 255.0f + 0.5f);
    bool apply_op = (eff_op < 1.0f);
    const ColorSpace* ws = m_impl->working_space;
    bool do_dither = st.dithering;

    for (const auto& span : draw_spans.spans) {
        auto* row = reinterpret_cast<uint32_t*>(
            m_impl->render_data + span.y * m_impl->render_stride);
        for (int i = 0; i < span.len; ++i) {
            int px = span.x + i;
            if (px < 0 || px >= m_impl->render_width) continue;

            float gpx = px + 0.5f, gpy = span.y + 0.5f;
            float angle = std::atan2(gpy - dev_center.y, gpx - dev_center.x) - start_angle;
            float t = angle / two_pi;
            t = std::fmod(t, 1.0f);
            if (t < 0.0f) t += 1.0f;

            uint32_t src = sample_gradient_argb(gi, t);
            if (span.coverage < 255)
                src = byte_mul(src, span.coverage);
            if (apply_op)
                src = byte_mul(src, op8);
            if (pixel_alpha(src) == 0) continue;
            uint32_t result = composite_pixel(src, row[px], st.op, st.blend_mode, ws);
            if (do_dither)
                result = dither_pixel_srgb(result, px, span.y);
            row[px] = result;
        }
    }
}


//  Shading drawing


void Context::draw_shading(const Shading& shading) {
    if (!m_impl || !m_impl->render_data || !shading) return;

    const auto* si = shading_impl(shading);
    if (!si || !si->eval) return;

    auto& st = m_impl->state();
    auto inv_ctm = st.matrix.inverted();
    if (!inv_ctm) return;

    SpanBuffer draw_spans;
    prepare_clip_spans(*m_impl, draw_spans);
    if (draw_spans.spans.empty()) return;

    if (!st.shadow.is_none())
        draw_shadow(*m_impl, draw_spans, st.shadow);

    float eff_op = effective_opacity(st);
    uint32_t op8 = static_cast<uint32_t>(eff_op * 255.0f + 0.5f);
    bool apply_op = (eff_op < 1.0f);
    const ColorSpace* ws = m_impl->working_space;
    bool do_dither = st.dithering;

    for (const auto& span : draw_spans.spans) {
        auto* row = reinterpret_cast<uint32_t*>(
            m_impl->render_data + span.y * m_impl->render_stride);

        for (int i = 0; i < span.len; ++i) {
            int px = span.x + i;
            if (px < 0 || px >= m_impl->render_width) continue;

            Point user_pt = inv_ctm->apply(Point{px + 0.5f, span.y + 0.5f});

            float t = 0.0f;
            if (si->kind == Shading::Impl::Kind::Axial) {
                Point dir = si->end_point - si->start_point;
                float len_sq = dir.dot(dir);
                if (len_sq > 0.0f)
                    t = (user_pt - si->start_point).dot(dir) / len_sq;
                if (!si->extend_start && t < 0.0f) continue;
                if (!si->extend_end && t > 1.0f) continue;
                t = std::clamp(t, 0.0f, 1.0f);
            } else {
                float fx = si->start_point.x, fy = si->start_point.y;
                float cx = si->end_point.x,   cy = si->end_point.y;
                float fr = si->start_radius,   cr = si->end_radius;

                float ddx = user_pt.x - fx, ddy = user_pt.y - fy;
                float cdx = cx - fx, cdy = cy - fy;
                float dr = cr - fr;

                float a = cdx * cdx + cdy * cdy - dr * dr;
                float b = 2.0f * (ddx * cdx + ddy * cdy + fr * dr);
                float c = ddx * ddx + ddy * ddy - fr * fr;

                if (std::abs(a) < 1e-10f) {
                    t = (std::abs(b) > 1e-10f) ? -c / b : 0.0f;
                } else {
                    float disc = b * b - 4.0f * a * c;
                    if (disc >= 0.0f) {
                        float sq = std::sqrt(disc);
                        float t1 = (-b + sq) / (2.0f * a);
                        float t2 = (-b - sq) / (2.0f * a);
                        t = t1;
                        if (fr + t1 * dr < 0.0f) t = t2;
                        if (t2 > t && fr + t2 * dr >= 0.0f) t = t2;
                    }
                }

                if (!si->extend_start && t < 0.0f) continue;
                if (!si->extend_end && t > 1.0f) continue;
                t = std::clamp(t, 0.0f, 1.0f);
            }

            Color color;
            si->eval(si->eval_info, t, &color);

            Color premul = color.premultiplied();
            uint32_t src = premul.to_argb32();
            if (span.coverage < 255)
                src = byte_mul(src, span.coverage);
            if (apply_op)
                src = byte_mul(src, op8);
            if (pixel_alpha(src) == 0) continue;

            uint32_t result = composite_pixel(src, row[px], st.op, st.blend_mode, ws);
            if (do_dither)
                result = dither_pixel_srgb(result, px, span.y);
            row[px] = result;
        }
    }
}


//  Image drawing


void Context::draw_image(const Image& image, const Rect& dest) {
    if (!m_impl || !m_impl->render_data || !image || dest.empty()) return;

    auto& st = m_impl->state();

    float img_w = static_cast<float>(image.width());
    float img_h = static_cast<float>(image.height());

    // Texture transform: user space → image UV space.
    // First translate so dest origin maps to (0,0), then scale to image dimensions.
    AffineTransform dest_to_image =
        AffineTransform::scaled(img_w / dest.width(), img_h / dest.height()) *
        AffineTransform::translated(-dest.x(), -dest.y());

    auto inv_ctm = st.matrix.inverted();
    if (!inv_ctm) return;

    PaintSource paint;
    paint.kind = PaintKind::Texture;
    paint.image = image_impl(image);
    paint.tex_opacity = 1.0f;
    paint.tex_transform = dest_to_image * (*inv_ctm);

    auto rect_path = Path::Builder{}.add_rect(dest).build();
    const auto* pi = path_impl(rect_path);
    if (!pi) return;

    SpanBuffer& spans = m_impl->fill_spans;
    rasterize(spans, *pi, st.matrix, m_impl->clip_rect, nullptr, FillRule::Winding, st.antialias);
    if (spans.spans.empty()) return;

    SpanBuffer clipped;
    const SpanBuffer* draw_spans = &spans;
    if (st.clipping) {
        span_buffer_intersect(clipped, spans, st.clip_spans);
        draw_spans = &clipped;
    }
    if (draw_spans->spans.empty()) return;

    if (!st.shadow.is_none())
        draw_shadow(*m_impl, *draw_spans, st.shadow);

    BlendParams params = make_blend_params(*m_impl, paint);
    blend(params, *draw_spans, m_impl->clip_rect, st.clipping ? &st.clip_spans : nullptr);
}

void Context::draw_image(const Image& image, const Rect& src_rect, const Rect& dest) {
    if (!m_impl || !m_impl->render_data || !image || dest.empty() || src_rect.empty()) return;

    IntRect crop{
        static_cast<int>(std::floor(src_rect.x())),
        static_cast<int>(std::floor(src_rect.y())),
        static_cast<int>(std::round(src_rect.width())),
        static_cast<int>(std::round(src_rect.height()))
    };
    Image cropped = image.cropped(crop);
    draw_image(cropped ? cropped : image, dest);
}

void Context::draw_image(const Image& image, Point origin) {
    if (!image) return;
    draw_image(image, Rect{origin.x, origin.y,
                           static_cast<float>(image.width()),
                           static_cast<float>(image.height())});
}


//  Pattern drawing


void Context::draw_pattern(const Pattern& pattern, const Rect& rect) {
    if (!m_impl || !m_impl->render_data || !pattern) return;

    auto& st = m_impl->state();

    PaintSource paint;
    paint.kind = PaintKind::PatternDraw;
    paint.pattern = pattern_impl(pattern);
    paint.ctm = st.matrix;

    auto rect_path = Path::Builder{}.add_rect(rect).build();
    const auto* pi = path_impl(rect_path);
    if (!pi) return;

    SpanBuffer& spans = m_impl->fill_spans;
    rasterize(spans, *pi, st.matrix, m_impl->clip_rect, nullptr, FillRule::Winding, st.antialias);
    if (spans.spans.empty()) return;

    SpanBuffer clipped;
    const SpanBuffer* draw_spans = &spans;
    if (st.clipping) {
        span_buffer_intersect(clipped, spans, st.clip_spans);
        draw_spans = &clipped;
    }
    if (draw_spans->spans.empty()) return;

    if (!st.shadow.is_none())
        draw_shadow(*m_impl, *draw_spans, st.shadow);

    BlendParams params = make_blend_params(*m_impl, paint);
    blend(params, *draw_spans, m_impl->clip_rect, st.clipping ? &st.clip_spans : nullptr);
}


//  Triangle mesh


void Context::draw_vertices(VertexMode mode,
                            std::span<const Point> positions,
                            std::span<const Color> colors,
                            std::span<const Point> tex_coords,
                            std::span<const uint16_t> indices) {
    if (!m_impl || !m_impl->render_data || positions.empty()) return;

    auto& st = m_impl->state();

    auto get_idx = [&](size_t i) -> size_t {
        return indices.empty() ? i : static_cast<size_t>(indices[i]);
    };

    size_t vert_count = indices.empty() ? positions.size() : indices.size();

    // Resolve fill texture for tex_coords sampling.
    const Image::Impl* fill_img = nullptr;
    if (!tex_coords.empty() && st.has_fill_pattern) {
        auto* pi = pattern_impl(st.fill_pattern);
        if (pi && pi->kind == Pattern::Impl::Kind::ImageBased) {
            fill_img = image_impl(pi->source_image);
            if (fill_img && !fill_img->data) fill_img = nullptr;
        }
    }
    bool has_tex = !tex_coords.empty() && fill_img;
    bool has_colors = !colors.empty();

    // Helper: binary search clip spans by y, then linear scan within row.
    auto clip_coverage_at = [&](int x, int y) -> uint8_t {
        if (!st.clipping) return 255;
        const auto& spans = st.clip_spans.spans;
        if (spans.empty()) return 0;

        // Binary search for first span with span.y >= y
        auto it = std::lower_bound(spans.begin(), spans.end(), y,
            [](const Span& s, int target_y) { return s.y < target_y; });

        for (; it != spans.end() && it->y == y; ++it) {
            if (x >= it->x && x < it->x + it->len)
                return it->coverage;
            if (it->x > x) break; // spans sorted by x within row
        }
        return 0;
    };

    // Helper: sample fill image at UV coordinates (nearest-neighbor, clamped).
    auto sample_fill = [&](float u, float v) -> uint32_t {
        int ix = static_cast<int>(std::floor(u));
        int iy = static_cast<int>(std::floor(v));
        if (ix < 0 || iy < 0 || ix >= fill_img->width || iy >= fill_img->height)
            return 0;
        const auto* row = reinterpret_cast<const uint32_t*>(
            fill_img->data + iy * fill_img->stride);
        return pixel_to_argb_premul(row[ix], fill_img->format);
    };

    const float vert_eff_op = effective_opacity(st);
    const uint32_t vert_op8 = static_cast<uint32_t>(vert_eff_op * 255.0f + 0.5f);
    const bool vert_apply_op = (vert_eff_op < 1.0f);

    auto emit_tri = [&](size_t i0, size_t i1, size_t i2) {
        Point p0 = st.matrix.apply(positions[i0]);
        Point p1 = st.matrix.apply(positions[i1]);
        Point p2 = st.matrix.apply(positions[i2]);

        float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
        if (std::abs(area) < 1e-6f) return;
        float inv_area = 1.0f / area;

        float min_x = std::min({p0.x, p1.x, p2.x});
        float min_y = std::min({p0.y, p1.y, p2.y});
        float max_x = std::max({p0.x, p1.x, p2.x});
        float max_y = std::max({p0.y, p1.y, p2.y});

        auto& cr = m_impl->clip_rect;
        int ix0 = std::max(cr.x, static_cast<int>(std::floor(min_x)));
        int iy0 = std::max(cr.y, static_cast<int>(std::floor(min_y)));
        int ix1 = std::min(cr.x + cr.w,  static_cast<int>(std::ceil(max_x)) + 1);
        int iy1 = std::min(cr.y + cr.h, static_cast<int>(std::ceil(max_y)) + 1);

        Color c0 = has_colors ? colors[i0] : st.fill_color;
        Color c1 = has_colors ? colors[i1] : st.fill_color;
        Color c2 = has_colors ? colors[i2] : st.fill_color;

        Point uv0, uv1, uv2;
        if (has_tex) {
            uv0 = tex_coords[i0];
            uv1 = tex_coords[i1];
            uv2 = tex_coords[i2];
        }

        for (int y = iy0; y < iy1; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(
                m_impl->render_data + y * m_impl->render_stride);
            for (int x = ix0; x < ix1; ++x) {
                float px = x + 0.5f;
                float py = y + 0.5f;
                float w0 = ((p1.x - px) * (p2.y - py) - (p2.x - px) * (p1.y - py)) * inv_area;
                float w1 = ((p2.x - px) * (p0.y - py) - (p0.x - px) * (p2.y - py)) * inv_area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;

                uint8_t clip_cov = clip_coverage_at(x, y);
                if (clip_cov == 0) continue;

                uint32_t src;
                if (has_tex) {
                    // Interpolate texture coordinates and sample fill image.
                    float u = uv0.x * w0 + uv1.x * w1 + uv2.x * w2;
                    float v = uv0.y * w0 + uv1.y * w1 + uv2.y * w2;
                    src = sample_fill(u, v);

                    // Modulate with interpolated vertex color if colors are provided.
                    if (has_colors) {
                        Color interp{
                            c0.r() * w0 + c1.r() * w1 + c2.r() * w2,
                            c0.g() * w0 + c1.g() * w1 + c2.g() * w2,
                            c0.b() * w0 + c1.b() * w1 + c2.b() * w2,
                            c0.a() * w0 + c1.a() * w1 + c2.a() * w2
                        };
                        // Modulate: multiply texture color by vertex color component-wise.
                        uint32_t vc = interp.premultiplied().to_argb32();
                        uint32_t sa = pixel_alpha(src);
                        if (sa > 0) {
                            uint32_t va = pixel_alpha(vc);
                            uint32_t ra = (sa * va + 127) / 255;
                            uint32_t rr = ((src >> 16 & 0xFF) * (vc >> 16 & 0xFF) + 127) / 255;
                            uint32_t rg = ((src >> 8 & 0xFF) * (vc >> 8 & 0xFF) + 127) / 255;
                            uint32_t rb = ((src & 0xFF) * (vc & 0xFF) + 127) / 255;
                            src = (ra << 24) | (rr << 16) | (rg << 8) | rb;
                        }
                    }
                } else {
                    // Color-only path (original behavior).
                    Color interp{
                        c0.r() * w0 + c1.r() * w1 + c2.r() * w2,
                        c0.g() * w0 + c1.g() * w1 + c2.g() * w2,
                        c0.b() * w0 + c1.b() * w1 + c2.b() * w2,
                        c0.a() * w0 + c1.a() * w1 + c2.a() * w2
                    };
                    src = interp.premultiplied().to_argb32();
                }

                // Apply opacity.
                if (vert_apply_op)
                    src = byte_mul(src, vert_op8);
                if (clip_cov < 255)
                    src = byte_mul(src, clip_cov);
                if (pixel_alpha(src) == 0) continue;
                row[x] = composite_pixel(src, row[x], st.op, st.blend_mode,
                                         m_impl->working_space);
            }
        }
    };

    switch (mode) {
    case VertexMode::Triangles:
        for (size_t i = 0; i + 2 < vert_count; i += 3)
            emit_tri(get_idx(i), get_idx(i + 1), get_idx(i + 2));
        break;
    case VertexMode::TriangleStrip:
        for (size_t i = 0; i + 2 < vert_count; ++i) {
            if (i & 1) emit_tri(get_idx(i + 1), get_idx(i), get_idx(i + 2));
            else        emit_tri(get_idx(i), get_idx(i + 1), get_idx(i + 2));
        }
        break;
    case VertexMode::TriangleFan:
        for (size_t i = 1; i + 1 < vert_count; ++i)
            emit_tri(get_idx(0), get_idx(i), get_idx(i + 1));
        break;
    }
}


//  Masking


void Context::draw_with_mask(const Image& mask, const Rect& mask_rect, MaskMode mode) {
    if (!m_impl || !m_impl->render_data || !mask) return;

    const auto* mi = image_impl(mask);
    if (!mi) return;

    auto& st = m_impl->state();
    Point dev_origin = st.matrix.apply({mask_rect.x(), mask_rect.y()});
    int mask_ox = static_cast<int>(std::floor(dev_origin.x));
    int mask_oy = static_cast<int>(std::floor(dev_origin.y));

    int x0 = std::max(0, mask_ox);
    int y0 = std::max(0, mask_oy);
    int x1 = std::min(m_impl->render_width,  mask_ox + mi->width);
    int y1 = std::min(m_impl->render_height, mask_oy + mi->height);

    for (int y = y0; y < y1; ++y) {
        auto* target_row = reinterpret_cast<uint32_t*>(
            m_impl->render_data + y * m_impl->render_stride);
        int my = y - mask_oy;
        for (int x = x0; x < x1; ++x) {
            int mx = x - mask_ox;
            uint8_t cov;
            if (mi->format == PixelFormat::A8) {
                uint8_t a = mi->data[my * mi->stride + mx];
                switch (mode) {
                case MaskMode::Alpha:
                    cov = a;
                    break;
                case MaskMode::InvertedAlpha:
                    cov = 255 - a;
                    break;
                case MaskMode::Luminance:
                    // A8 has no color; treat alpha as luminance.
                    cov = a;
                    break;
                case MaskMode::InvertedLuminance:
                    cov = 255 - a;
                    break;
                default:
                    cov = a;
                    break;
                }
            } else {
                auto* mask_row = reinterpret_cast<const uint32_t*>(mi->data + my * mi->stride);
                cov = mask_ops::extract_coverage(mask_row[mx], mode);
            }
            if (cov < 255)
                target_row[x] = byte_mul(target_row[x], cov);
        }
    }
}


//  Transparency layers


void Context::begin_transparency_layer(float layer_opacity) {
    begin_transparency_layer(layer_opacity, BlendMode::Normal);
}

void Context::begin_transparency_layer(float layer_opacity, const Rect& bounds) {
    if (!m_impl || !m_impl->render_data) return;

    save_state();
    auto& st = m_impl->state();

    int full_w = m_impl->render_width;
    int full_h = m_impl->render_height;
    int bpp = pixel_format_info(m_impl->render_format).bpp;

    // Transform bounds to device space and intersect with clip_rect.
    IntRect layer_rect = m_impl->clip_rect;
    {
        Rect dev_bounds = st.matrix.apply_to_rect(bounds);
        IntRect b;
        b.x = static_cast<int>(std::floor(dev_bounds.x()));
        b.y = static_cast<int>(std::floor(dev_bounds.y()));
        b.w = static_cast<int>(std::ceil(dev_bounds.x() + dev_bounds.width())) - b.x;
        b.h = static_cast<int>(std::ceil(dev_bounds.y() + dev_bounds.height())) - b.y;

        int ix0 = std::max(b.x, layer_rect.x);
        int iy0 = std::max(b.y, layer_rect.y);
        int ix1 = std::min(b.x + b.w, layer_rect.x + layer_rect.w);
        int iy1 = std::min(b.y + b.h, layer_rect.y + layer_rect.h);
        if (ix0 < ix1 && iy0 < iy1)
            layer_rect = {ix0, iy0, ix1 - ix0, iy1 - iy0};
        else {
            restore_state();
            return;
        }
    }

    // Allocate only the layer_rect region, not the full surface.
    int layer_w = layer_rect.w;
    int layer_h = layer_rect.h;
    int stride = (layer_w * bpp + 15) & ~15;

    LayerInfo layer;
    layer.alpha = std::clamp(layer_opacity, 0.0f, 1.0f);
    layer.blend_mode = BlendMode::Normal;
    layer.op = st.op;
    layer.device_bounds = layer_rect;
    layer.parent_clip_rect = m_impl->clip_rect;

    layer.parent_surface = Image::create(full_w, full_h, m_impl->render_stride,
                                         m_impl->render_format,
                                         ColorSpace::srgb(),
                                         {m_impl->render_data,
                                          static_cast<size_t>(full_h) * m_impl->render_stride});

    std::vector<unsigned char> zero(static_cast<size_t>(layer_h) * stride, 0);
    layer.surface = Image::create(layer_w, layer_h, stride, m_impl->render_format,
                                  ColorSpace::srgb(),
                                  {zero.data(), zero.size()});

    auto* layer_data = image_data_mut(layer.surface);
    if (!layer_data) {
        restore_state();
        return;
    }

    st.layer = std::move(layer);
    m_impl->render_data = image_data_mut(st.layer->surface);
    m_impl->render_width = layer_w;
    m_impl->render_height = layer_h;
    m_impl->render_stride = stride;
    m_impl->clip_rect = {0, 0, layer_w, layer_h};

    // Shift CTM so that device coordinates map into the sub-layer origin.
    // Without this, drawing at device (layer_rect.x, layer_rect.y) would
    // address pixel (layer_rect.x, layer_rect.y) in a buffer that is only
    // (layer_w × layer_h) pixels — producing either blank output or overflow.
    st.matrix = AffineTransform::translated(
        -static_cast<float>(layer_rect.x),
        -static_cast<float>(layer_rect.y)) * st.matrix;
}

void Context::begin_transparency_layer(float layer_opacity, BlendMode blend) {
    if (!m_impl || !m_impl->render_data) return;

    save_state();
    auto& st = m_impl->state();

    int w = m_impl->render_width;
    int h = m_impl->render_height;
    int bpp = pixel_format_info(m_impl->render_format).bpp;
    int stride = (w * bpp + 15) & ~15;

    LayerInfo layer;
    layer.alpha = std::clamp(layer_opacity, 0.0f, 1.0f);
    layer.blend_mode = blend;
    layer.op = st.op;
    layer.device_bounds = m_impl->clip_rect;
    layer.parent_clip_rect = m_impl->clip_rect;

    layer.parent_surface = Image::create(w, h, m_impl->render_stride,
                                         m_impl->render_format,
                                         ColorSpace::srgb(),
                                         {m_impl->render_data,
                                          static_cast<size_t>(h) * m_impl->render_stride});

    // std::vector zero-initializes, Image::create copies — no extra memset needed.
    std::vector<unsigned char> zero(static_cast<size_t>(h) * stride, 0);
    layer.surface = Image::create(w, h, stride, m_impl->render_format,
                                  ColorSpace::srgb(),
                                  {zero.data(), zero.size()});

    auto* layer_data = image_data_mut(layer.surface);
    if (!layer_data) {
        restore_state();
        return;
    }

    st.layer = std::move(layer);
    m_impl->render_data = image_data_mut(st.layer->surface);
    m_impl->render_stride = stride;
}

void Context::end_transparency_layer() {
    if (!m_impl) return;
    auto& st = m_impl->state();
    if (!st.layer.has_value()) return;

    auto& layer = *st.layer;

    auto* parent_data = image_data_mut(layer.parent_surface);
    if (!parent_data) {
        st.layer.reset();
        restore_state();
        return;
    }

    // Restore render target to parent.
    int parent_w = layer.parent_surface.width();
    int parent_h = layer.parent_surface.height();
    m_impl->render_data = parent_data;
    m_impl->render_width = parent_w;
    m_impl->render_height = parent_h;
    m_impl->render_stride = layer.parent_surface.stride();
    m_impl->clip_rect = layer.parent_clip_rect;

    // Composite layer onto parent within device_bounds only.
    const auto* li = image_impl(layer.surface);
    if (li && li->data) {
        int layer_stride = li->stride;
        int layer_w = li->width;
        int layer_h = li->height;

        uint32_t alpha_byte = static_cast<uint32_t>(layer.alpha * 255.0f + 0.5f);
        if (alpha_byte > 255) alpha_byte = 255;

        CompositeOp layer_op = layer.op;
        BlendMode layer_blend = layer.blend_mode;
        bool fast_path = (layer_op == CompositeOp::SourceOver
                       && layer_blend == BlendMode::Normal
                       && !is_linear_space(m_impl->working_space));

        // Determine composite region. For full-size layers device_bounds
        // covers the entire surface. For bounds-limited layers the surface
        // is smaller and offset to device_bounds origin.
        bool is_sub_layer = (layer_w < parent_w || layer_h < parent_h);
        IntRect db = layer.device_bounds;

        int y0 = is_sub_layer ? db.y : 0;
        int y1 = is_sub_layer ? db.y + db.h : parent_h;
        int x0 = is_sub_layer ? db.x : 0;
        int x1 = is_sub_layer ? db.x + db.w : parent_w;

        // Clamp to parent surface.
        y0 = std::max(y0, 0);  y1 = std::min(y1, parent_h);
        x0 = std::max(x0, 0);  x1 = std::min(x1, parent_w);

        for (int y = y0; y < y1; ++y) {
            int ly = is_sub_layer ? (y - db.y) : y;
            if (ly < 0 || ly >= layer_h) continue;

            auto* s_row = reinterpret_cast<const uint32_t*>(li->data + ly * layer_stride);
            auto* d_row = reinterpret_cast<uint32_t*>(
                m_impl->render_data + y * m_impl->render_stride);

            for (int x = x0; x < x1; ++x) {
                int lx = is_sub_layer ? (x - db.x) : x;
                if (lx < 0 || lx >= layer_w) continue;

                uint32_t src = s_row[lx];
                if (pixel_alpha(src) == 0) continue;
                if (alpha_byte < 255)
                    src = byte_mul(src, alpha_byte);
                if (pixel_alpha(src) == 0) continue;
                if (fast_path)
                    d_row[x] = composite_src_over(src, d_row[x]);
                else
                    d_row[x] = composite_pixel(src, d_row[x], layer_op, layer_blend,
                                               m_impl->working_space);
            }
        }
    }

    st.layer.reset();
    restore_state();
}


//  Text


void Context::set_font(const Font& f) {
    if (!m_impl) return;
    m_impl->state().font = f;
    if (f) m_impl->state().font_size = f.size();
}

Font Context::font() const {
    if (!m_impl) return {};
    return m_impl->state().font;
}

void Context::set_font_size(float size) {
    if (!m_impl) return;
    auto& st = m_impl->state();
    st.font_size = size;
    if (st.font) st.font = st.font.with_size(size);
}

float Context::font_size() const {
    if (!m_impl) return 12.0f;
    return m_impl->state().font_size;
}

void Context::set_text_drawing_mode(TextDrawingMode mode) {
    if (!m_impl) return;
    m_impl->state().text_mode = mode;
}

TextDrawingMode Context::text_drawing_mode() const {
    if (!m_impl) return TextDrawingMode::Fill;
    return m_impl->state().text_mode;
}

void Context::set_text_antialias(TextAntialias mode) {
    if (!m_impl) return;
    m_impl->state().text_antialias = mode;
}

TextAntialias Context::text_antialias() const {
    if (!m_impl) return TextAntialias::Grayscale;
    return m_impl->state().text_antialias;
}

void Context::set_color_palette_index(int index) {
    if (!m_impl) return;
    m_impl->state().color_palette_index = index;
}

int Context::color_palette_index() const {
    if (!m_impl) return 0;
    return m_impl->state().color_palette_index;
}

// -- Glyph drawing --

void Context::draw_glyphs(const Font& f,
                          std::span<const GlyphID> glyphs,
                          std::span<const Point> positions) {
    if (!m_impl || !m_impl->render_data) return;
    if (!f || glyphs.empty() || positions.empty()) return;
    if (glyphs.size() != positions.size()) return;

    auto& st = m_impl->state();
    bool do_fill = (st.text_mode == TextDrawingMode::Fill ||
                    st.text_mode == TextDrawingMode::FillStroke ||
                    st.text_mode == TextDrawingMode::FillClip ||
                    st.text_mode == TextDrawingMode::FillStrokeClip);
    bool do_stroke_text = (st.text_mode == TextDrawingMode::Stroke ||
                           st.text_mode == TextDrawingMode::FillStroke ||
                           st.text_mode == TextDrawingMode::StrokeClip ||
                           st.text_mode == TextDrawingMode::FillStrokeClip);

    for (size_t i = 0; i < glyphs.size(); ++i) {
        Path glyph_path = f.glyph_path(glyphs[i], positions[i].x, positions[i].y);
        if (!glyph_path) continue;
        if (do_fill) fill_path(glyph_path, st.winding);
        if (do_stroke_text) stroke_path(glyph_path);
    }
}

void Context::draw_glyphs(const Font& f, std::span<const GlyphPosition> run) {
    if (!f || run.empty()) return;

    std::vector<GlyphID> glyphs(run.size());
    std::vector<Point> positions(run.size());
    for (size_t i = 0; i < run.size(); ++i) {
        glyphs[i] = run[i].glyph;
        positions[i] = {run[i].position.x + run[i].offset.x,
                        run[i].position.y + run[i].offset.y};
    }
    draw_glyphs(f, glyphs, positions);
}

// -- Simple text (no shaping) --

namespace {

Codepoint decode_utf8(const char*& it, const char* end) {
    if (it >= end) return 0;
    auto byte = static_cast<unsigned char>(*it);
    if (byte < 0x80) { ++it; return byte; }

    int extra;
    Codepoint cp;
    if      ((byte & 0xE0) == 0xC0) { cp = byte & 0x1F; extra = 1; }
    else if ((byte & 0xF0) == 0xE0) { cp = byte & 0x0F; extra = 2; }
    else if ((byte & 0xF8) == 0xF0) { cp = byte & 0x07; extra = 3; }
    else { ++it; return 0xFFFD; }

    ++it;
    for (int j = 0; j < extra && it < end; ++j, ++it) {
        auto c = static_cast<unsigned char>(*it);
        if ((c & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (c & 0x3F);
    }
    return cp;
}

} // anonymous namespace

void Context::draw_text(std::string_view utf8, Point origin) {
    draw_text(utf8, origin.x, origin.y);
}

float Context::draw_text(std::string_view utf8, float x, float y) {
    if (!m_impl || !m_impl->render_data) return 0.0f;

    const Font& f = m_impl->state().font;
    if (!f) return 0.0f;

    std::vector<GlyphID> glyphs;
    std::vector<Point> positions;
    glyphs.reserve(utf8.size());
    positions.reserve(utf8.size());

    const char* it = utf8.data();
    const char* end = it + utf8.size();
    float cursor = x;

    while (it < end) {
        Codepoint cp = decode_utf8(it, end);
        if (cp == 0) break;
        GlyphID gid = f.glyph_for_codepoint(cp);
        glyphs.push_back(gid);
        positions.push_back({cursor, y});
        cursor += f.glyph_metrics(gid).advance;
    }

    if (!glyphs.empty())
        draw_glyphs(f, glyphs, positions);

    return cursor - x;
}

Rect Context::text_bounding_box(std::string_view utf8, Point origin) const {
    if (!m_impl) return {};
    const Font& f = m_impl->state().font;
    return f ? f.text_bounds(utf8, origin) : Rect{};
}

float Context::text_advance(std::string_view utf8) const {
    if (!m_impl) return 0.0f;
    const Font& f = m_impl->state().font;
    return f ? f.measure(utf8) : 0.0f;
}


//  Hit testing


bool Context::path_contains(const Path& path, Point p, FillRule rule) const {
    if (!m_impl || !path) return false;
    return path.contains(p, rule);
}

bool Context::path_contains(const Path& path, const AffineTransform& transform,
                            Point p, FillRule rule) const {
    if (!m_impl || !path) return false;
    auto inv = transform.inverted();
    if (!inv) return false;
    return path.contains(inv->apply(p), rule);
}

} // namespace kvg
