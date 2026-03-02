// kvgprivate.hpp — Internal implementation details
// C++20

#ifndef KVGPRIVATE_HPP
#define KVGPRIVATE_HPP

#include "kosmvg.hpp"
#include "kvgutils.hpp"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kvg {

// -- Constants --

inline constexpr double sqrt2 = std::numbers::sqrt2;

// Gradient color table size.  2048 gives sub-pixel precision even on 4K
// displays while fitting comfortably in L1 cache (8 KB).
inline constexpr int kColorTableSize = 2048;

// Fixed-point precision for scanline-incremental gradient fetching.
inline constexpr int kFixptBits = 8;
inline constexpr int kFixptSize = 1 << kFixptBits; // 256

// -- Intrusive reference counting --

struct RefCounted {
    static constexpr int Immortal = 0x7FFFFFFF;

    std::atomic<int> ref_count{1};

    RefCounted() = default;
    explicit RefCounted(int initial) noexcept : ref_count(initial) {}

    void ref() noexcept {
        if (ref_count.load(std::memory_order_relaxed) == Immortal) return;
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    bool deref() noexcept {
        if (ref_count.load(std::memory_order_relaxed) == Immortal) return false;
        return ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    [[nodiscard]] int count() const noexcept {
        return ref_count.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool is_immortal() const noexcept {
        return ref_count.load(std::memory_order_relaxed) == Immortal;
    }
};

// -- Helpers: points-per-command --

inline constexpr int points_per_command(PathElement cmd) {
    switch (cmd) {
    case PathElement::MoveToPoint:      return 1;
    case PathElement::AddLineToPoint:   return 1;
    case PathElement::AddQuadCurve:     return 2;
    case PathElement::AddCurve:         return 3;
    case PathElement::CloseSubpath:     return 0;
    }
    return 0;
}

// -- Path::Impl --
//
// Separate arrays: commands[] + points[].
// Commands index into points sequentially:
//   MoveToPoint      → 1 point  (destination)
//   AddLineToPoint   → 1 point  (destination)
//   AddQuadCurve     → 2 points (control, end)
//   AddCurve         → 3 points (cp1, cp2, end)
//   CloseSubpath     → 0 points
//
// Data model: commands/points are always-valid spans used by all read
// paths.  In owning mode they point into cmd_storage/pt_storage; in
// borrowed mode (from_raw) they point to caller-owned external arrays.

struct Path::Impl : RefCounted {
    // Owning storage (empty when borrowed).
    std::vector<PathElement> cmd_storage;
    std::vector<Point>       pt_storage;

    // Always-valid read views — either into *_storage or external memory.
    std::span<const PathElement> commands;
    std::span<const Point>       points;

    int num_contours = 0;
    int num_curves   = 0;
    Point start_point;

    // Take ownership of pre-built vectors (from Builder::build).
    void own(std::vector<PathElement>&& c, std::vector<Point>&& p) {
        cmd_storage = std::move(c);
        pt_storage  = std::move(p);
        commands = cmd_storage;
        points   = pt_storage;
    }

    // Deep-copy from spans (COW, clone, transformed).
    void copy_from(std::span<const PathElement> c, std::span<const Point> p) {
        cmd_storage.assign(c.begin(), c.end());
        pt_storage.assign(p.begin(), p.end());
        commands = cmd_storage;
        points   = pt_storage;
    }

    // Borrow external arrays without copying (from_raw).
    void borrow(const PathElement* c, int cn, const Point* p, int pn) {
        commands = {c, static_cast<size_t>(cn)};
        points   = {p, static_cast<size_t>(pn)};
    }

    bool is_borrowed() const noexcept { return cmd_storage.empty() && !commands.empty(); }
};

// -- ColorSpace::Impl --

struct ColorSpace::Impl : RefCounted {
    enum class Type { SRGB, LinearSRGB, DisplayP3, ExtLinearSRGB, DeviceGray };
    Type type = Type::SRGB;
    int  components = 3;

    Impl() = default;
    Impl(int initial_rc, Type t, int c) noexcept
        : RefCounted(initial_rc), type(t), components(c) {}
};

// -- Image::Impl --
//
// Internal representation is always a mutable buffer.
// Public Image exposes const-only access. COW on mutation.

struct Image::Impl : RefCounted {
    int width  = 0;
    int height = 0;
    int stride = 0;
    unsigned char* data = nullptr;
    bool owns_data = false;
    PixelFormat format = PixelFormat::ARGB32_Premultiplied;
    ColorSpace color_space_;
    float headroom_ = 1.0f;

    // External data release callback (for zero-copy Image::create).
    void (*release_fn)(void*) = nullptr;
    void* release_ctx = nullptr;

    ~Impl() {
        if (owns_data)
            std::free(data);
        else if (release_fn)
            release_fn(release_ctx);
    }
};

// -- Gradient::Impl --

struct Gradient::Impl : RefCounted {
    std::vector<Gradient::Stop> stops;
    GradientSpread spread = GradientSpread::Pad;
    ColorSpace color_space_;

    // Pre-computed color table for fast per-pixel lookup.
    // Built lazily on first use via ensure_colortable().
    mutable uint32_t colortable[kColorTableSize];
    mutable bool colortable_valid = false;

    void ensure_colortable() const {
        if (colortable_valid) return;
        build_colortable();
        colortable_valid = true;
    }

    void invalidate_colortable() { colortable_valid = false; }

private:
    void build_colortable() const;
};

// -- Pattern::Impl --

struct Pattern::Impl : RefCounted {
    enum class Kind { Colored, ImageBased };
    Kind kind = Kind::Colored;

    // Colored pattern
    Rect bounds;
    AffineTransform matrix;
    float x_step = 0.0f;
    float y_step = 0.0f;
    PatternTiling tiling = PatternTiling::NoDistortion;
    Pattern::DrawFunc draw_func = nullptr;
    void* draw_info = nullptr;
    void (*release)(void*) = nullptr;

    // Image-based pattern
    Image source_image;

    ~Impl() {
        if (release)
            release(draw_info);
    }
};

// -- Shading::Impl --

struct Shading::Impl : RefCounted {
    enum class Kind { Axial, Radial };
    Kind kind = Kind::Axial;

    // Geometry
    Point start_point;
    Point end_point;
    float start_radius = 0.0f;
    float end_radius   = 0.0f;

    // Color function
    ColorSpace color_space_;
    Shading::EvalFunc eval = nullptr;
    void* eval_info = nullptr;
    void (*release)(void*) = nullptr;

    bool extend_start = false;
    bool extend_end   = false;

    ~Impl() {
        if (release)
            release(eval_info);
    }
};

// -- Font::Impl and FontCollection::Impl are defined in kvgfont.cpp --
// They depend on kvgotf.hpp and are not needed by other translation units.

// -- Internal span type --
// Layout-compatible with kosmvg::ft::Span for zero-copy callback.

struct Span {
    int           x;
    int           len;
    int           y;
    unsigned char coverage;
};

// -- SpanBuffer --

struct SpanBuffer {
    std::vector<Span> spans;
    // NOTE: bounds is valid after span_buffer_init_rect() or span_buffer_copy().
    // The FT rasterizer callback does NOT update this field.
    // Use span_buffer_extents() for actual bounds from spans.
    IntRect bounds;

    void reset() {
        spans.clear();
        bounds = {};
    }
};

// -- Stroke types --

struct StrokeStyle {
    float    width       = 1.0f;
    LineCap  cap         = LineCap::Butt;
    LineJoin join        = LineJoin::Miter;
    float    miter_limit = 10.0f;
};

struct StrokeDash {
    float offset = 0.0f;
    std::vector<float> array;
};

struct StrokeData {
    StrokeStyle style;
    StrokeDash  dash;
};

// -- Layer state for offscreen rendering (begin_transparency_layer / end) --

struct LayerInfo {
    Image     surface;            ///< Offscreen layer (drawing target).
    Image     parent_surface;     ///< Surface we were rendering to before the layer.
    IntRect   parent_clip_rect;   ///< Clip rect of the parent.
    IntRect   device_bounds;      ///< Layer bounding box in parent device space.
    float     alpha = 1.0f;       ///< Group opacity applied when compositing.
    BlendMode blend_mode = BlendMode::Normal;
    CompositeOp op = CompositeOp::SourceOver;
};

// -- Graphics state --

struct State {
    Color       fill_color;
    Color       stroke_color;
    Font        font;
    AffineTransform matrix;
    StrokeData  stroke;
    SpanBuffer  clip_spans;
    Shadow      shadow;
    MaskFilter  mask_filter;
    FillRule    winding              = FillRule::Winding;
    CompositeOp op                   = CompositeOp::SourceOver;
    BlendMode   blend_mode           = BlendMode::Normal;
    InterpolationQuality interpolation = InterpolationQuality::Default;
    TextDrawingMode   text_mode      = TextDrawingMode::Fill;
    TextAntialias     text_antialias = TextAntialias::Grayscale;
    float       font_size            = 12.0f;
    float       opacity              = 1.0f;
    float       alpha                = 1.0f;
    bool        clipping             = false;
    bool        dithering            = false;
    bool        antialias            = true;
    bool        pixel_snap           = false;
    bool        tone_mapping         = false;
    float       scale_factor         = 1.0f;
    int         color_palette_index  = 0;

    // Fill/stroke patterns (set via set_fill_pattern / set_stroke_pattern).
    Pattern     fill_pattern;
    Pattern     stroke_pattern;
    bool        has_fill_pattern     = false;
    bool        has_stroke_pattern   = false;

    std::optional<LayerInfo> layer;
};

// -- RAII state stack with copy-on-push --

class StateStack {
public:
    StateStack() {
        push(); // initial state
    }

    ~StateStack() = default;

    StateStack(const StateStack&) = delete;
    StateStack& operator=(const StateStack&) = delete;

    void push() {
        if (m_stack.empty()) {
            m_stack.emplace_back();
        } else {
            m_stack.push_back(m_stack.back());
            m_stack.back().layer.reset(); // only begin_transparency_layer sets this
        }
    }

    void pop() {
        if (m_stack.size() > 1)
            m_stack.pop_back();
    }

    [[nodiscard]] State& current() { return m_stack.back(); }
    [[nodiscard]] const State& current() const { return m_stack.back(); }

    [[nodiscard]] bool empty() const { return m_stack.empty(); }
    [[nodiscard]] std::size_t depth() const { return m_stack.size(); }

private:
    std::vector<State> m_stack;
};

// -- Context::Impl --

struct Context::Impl {
    // Active render target. For BitmapContext this points to
    // BitmapImpl's buffer. During layer operations it temporarily
    // points to the offscreen layer buffer.
    unsigned char* render_data   = nullptr;
    int            render_width  = 0;
    int            render_height = 0;
    int            render_stride = 0;
    PixelFormat    render_format = PixelFormat::ARGB32_Premultiplied;

    StateStack     states;
    FontCollection font_cache;
    IntRect        clip_rect;
    SpanBuffer     clip_spans;
    SpanBuffer     fill_spans;
    const ColorSpace* working_space = nullptr; // owned by BitmapImpl, lifetime >= Context

    // Temporary surface for shadow rendering (lazily allocated).
    Image          shadow_surface;

    // Reusable buffers to avoid per-frame heap allocations.
    std::vector<unsigned char> shadow_buf_cache;
    std::vector<unsigned char> blur_tmp_cache;

    State& state() { return states.current(); }
    const State& state() const { return states.current(); }
};

// -- BitmapContext::BitmapImpl --

struct BitmapContext::BitmapImpl {
    bool        owns_data = false;
    ColorSpace  color_space_;
    ColorSpace  working_space_;
    float       headroom_    = 1.0f;
};

// -- Mask coverage extraction --

namespace mask_ops {

inline uint8_t extract_coverage(uint32_t pixel, MaskMode mode) {
    switch (mode) {
    case MaskMode::Alpha:
        return pixel_alpha(pixel);
    case MaskMode::InvertedAlpha:
        return static_cast<uint8_t>(255 - pixel_alpha(pixel));
    case MaskMode::Luminance: {
        uint8_t a = pixel_alpha(pixel);
        if (a == 0) return 0;
        auto [r, g, b] = unpremultiply(pixel);
        return luminance_from_rgb(r, g, b);
    }
    case MaskMode::InvertedLuminance:
        return static_cast<uint8_t>(255 - extract_coverage(pixel, MaskMode::Luminance));
    default:
        // Composite mask modes (Add, Subtract, etc.) are handled
        // at a higher level — they combine two mask surfaces.
        return pixel_alpha(pixel);
    }
}

} // namespace mask_ops

// -- Internal paint source for blend dispatch --
//
// Replaces the old unified Paint. The blend function needs to know
// what color source to use for each span. Context methods set this
// up before calling blend().

enum class PaintKind {
    Solid,
    LinearGradient,
    RadialGradient,
    ConicGradient,
    Texture,
    PatternDraw,
    Shading
};

struct PaintSource {
    PaintKind kind = PaintKind::Solid;

    // Solid
    uint32_t solid_argb = 0xFF000000; // premultiplied ARGB32

    // Gradient
    const Gradient::Impl* gradient = nullptr;
    float grad_values[6] = {};
    // Linear: x1, y1, x2, y2
    // Radial: cx, cy, cr, fx, fy, fr
    // Conic:  cx, cy, start_angle
    AffineTransform grad_transform;
    AffineTransform grad_inv_transform; // pre-computed inverse (avoids per-pixel inversion)
    bool grad_inv_valid = false;
    GradientSpread grad_spread = GradientSpread::Pad;

    // Texture / image
    const Image::Impl* image = nullptr;
    AffineTransform tex_transform;
    float tex_opacity = 1.0f;

    // Colored pattern callback
    const Pattern::Impl* pattern = nullptr;

    // Shading (procedural color function)
    const Shading::Impl* shading = nullptr;

    // Current transformation matrix at draw time.
    // Used by resolve_pattern_paint to apply CTM to the tile transform.
    AffineTransform ctm;

    // Texture sampling quality
    InterpolationQuality interpolation = InterpolationQuality::Default;
};

// -- Blend parameters --

struct BlendParams {
    unsigned char*  target_data;
    int             target_width;
    int             target_height;
    int             target_stride;
    PixelFormat     target_format;
    const PaintSource* paint;
    CompositeOp     op;
    BlendMode       blend_mode;
    const ColorSpace* working_space; // nullptr = sRGB
    float           opacity;
    bool            dithering;
    InterpolationQuality interpolation = InterpolationQuality::Default;
};

// -- Internal pimpl accessors --
// Each public class stores a single Impl* as its sole data member.
// Internal code needs direct Impl access for rasterization / blend dispatch.

inline Path::Impl* path_impl(Path& p) {
    static_assert(sizeof(Path) == sizeof(void*));
    return *reinterpret_cast<Path::Impl**>(&p);
}

inline const Path::Impl* path_impl(const Path& p) {
    static_assert(sizeof(Path) == sizeof(void*));
    return *reinterpret_cast<Path::Impl* const*>(&p);
}

inline Image::Impl* image_impl(Image& img) {
    static_assert(sizeof(Image) == sizeof(void*));
    return *reinterpret_cast<Image::Impl**>(&img);
}

inline const Image::Impl* image_impl(const Image& img) {
    static_assert(sizeof(Image) == sizeof(void*));
    return *reinterpret_cast<Image::Impl* const*>(&img);
}

inline const Gradient::Impl* gradient_impl(const Gradient& g) {
    static_assert(sizeof(Gradient) == sizeof(void*));
    return *reinterpret_cast<Gradient::Impl* const*>(&g);
}

inline const ColorSpace::Impl* color_space_impl(const ColorSpace& cs) {
    static_assert(sizeof(ColorSpace) == sizeof(void*));
    return *reinterpret_cast<ColorSpace::Impl* const*>(&cs);
}

/// Returns true when the given working space requires linear-light blending.
/// A null pointer (= default sRGB) returns false.
inline bool is_linear_space(const ColorSpace* ws) {
    if (!ws) return false;
    auto* impl = color_space_impl(*ws);
    return impl && (impl->type == ColorSpace::Impl::Type::LinearSRGB
                 || impl->type == ColorSpace::Impl::Type::ExtLinearSRGB);
}

// Sample a gradient at parameter t, returning premultiplied ARGB32.
// Mirrors Gradient::sample() exactly (same spread formulas, binary search).
// When the gradient's color space is linear sRGB, interpolation happens
// in linear light for physically correct results.
// Clamp a LUT index according to spread mode.
inline int gradient_clamp(const Gradient::Impl* gi, int ipos) {
    switch (gi->spread) {
    case GradientSpread::Pad:
        return std::clamp(ipos, 0, kColorTableSize - 1);
    case GradientSpread::Repeat:
        ipos = ipos % kColorTableSize;
        if (ipos < 0) ipos += kColorTableSize;
        return ipos;
    case GradientSpread::Reflect: {
        const int limit = kColorTableSize * 2;
        ipos = ipos % limit;
        if (ipos < 0) ipos += limit;
        if (ipos >= kColorTableSize) ipos = limit - 1 - ipos;
        return ipos;
    }
    }
    return std::clamp(ipos, 0, kColorTableSize - 1);
}

// Fast gradient pixel lookup via pre-computed color table.
// pos is normalized [0,1] (or outside for repeat/reflect).
inline uint32_t gradient_pixel(const Gradient::Impl* gi, float pos) {
    int ipos = static_cast<int>(pos * (kColorTableSize - 1) + 0.5f);
    return gi->colortable[gradient_clamp(gi, ipos)];
}

// Fixed-point variant: pos is in fixed-point with kFixptBits fractional bits,
// scaled to kColorTableSize range.
inline uint32_t gradient_pixel_fixed(const Gradient::Impl* gi, int fixed_pos) {
    int ipos = (fixed_pos + (kFixptSize / 2)) >> kFixptBits;
    return gi->colortable[gradient_clamp(gi, ipos)];
}

// Legacy per-pixel sampling (used by Gradient::sample() public API and
// as fallback when colortable is not available).
inline uint32_t sample_gradient_argb(const Gradient::Impl* gi, float t) {
    if (!gi || gi->stops.empty())
        return 0xFF000000;

    // Fast path: use pre-computed LUT when available.
    if (gi->colortable_valid)
        return gradient_pixel(gi, t);

    // NaN/Inf guard
    if (t != t) return gi->stops.front().color.premultiplied().to_argb32();
    if (!std::isfinite(t)) t = (t > 0) ? 1.0f : 0.0f;

    // Apply spread mode
    switch (gi->spread) {
    case GradientSpread::Pad:
        t = std::clamp(t, 0.0f, 1.0f); break;
    case GradientSpread::Reflect: {
        t = std::fmod(t, 2.0f);
        if (t < 0.0f) t += 2.0f;
        if (t > 1.0f) t = 2.0f - t;
        break;
    }
    case GradientSpread::Repeat:
        t = t - std::floor(t); break;
    }

    auto& stops = gi->stops;
    if (stops.size() == 1 || t <= stops.front().offset)
        return stops.front().color.premultiplied().to_argb32();
    if (t >= stops.back().offset)
        return stops.back().color.premultiplied().to_argb32();

    size_t lo = 0, hi = stops.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (stops[mid].offset <= t) lo = mid;
        else hi = mid;
    }

    float range = stops[hi].offset - stops[lo].offset;
    float local_t = (range > 0.0f) ? (t - stops[lo].offset) / range : 0.0f;

    auto* cs = color_space_impl(gi->color_space_);
    bool linear = cs && (cs->type == ColorSpace::Impl::Type::LinearSRGB ||
                         cs->type == ColorSpace::Impl::Type::ExtLinearSRGB);

    if (linear) {
        const Color& c0 = stops[lo].color;
        const Color& c1 = stops[hi].color;
        using color_space_util::srgb_to_linear_f;
        using color_space_util::linear_to_srgb_f;
        float r = srgb_to_linear_f(c0.r()) + (srgb_to_linear_f(c1.r()) - srgb_to_linear_f(c0.r())) * local_t;
        float g = srgb_to_linear_f(c0.g()) + (srgb_to_linear_f(c1.g()) - srgb_to_linear_f(c0.g())) * local_t;
        float b = srgb_to_linear_f(c0.b()) + (srgb_to_linear_f(c1.b()) - srgb_to_linear_f(c0.b())) * local_t;
        float a = c0.a() + (c1.a() - c0.a()) * local_t;
        Color result(linear_to_srgb_f(r), linear_to_srgb_f(g), linear_to_srgb_f(b), a);
        return result.premultiplied().to_argb32();
    }

    Color c = stops[lo].color.lerp(stops[hi].color, local_t);
    return c.premultiplied().to_argb32();
}

inline const Pattern::Impl* pattern_impl(const Pattern& p) {
    static_assert(sizeof(Pattern) == sizeof(void*));
    return *reinterpret_cast<Pattern::Impl* const*>(&p);
}

inline const Shading::Impl* shading_impl(const Shading& s) {
    static_assert(sizeof(Shading) == sizeof(void*));
    return *reinterpret_cast<Shading::Impl* const*>(&s);
}

// -- Mutable image data accessor (internal use) --
// Image is read-only publicly, but internal rendering needs write access.

inline unsigned char* image_data_mut(Image& img) {
    auto* impl = image_impl(img);
    return impl ? impl->data : nullptr;
}

// -- Internal functions --

// Stroke-to-fill conversion (defined in kvgrasterize.cpp).
Path stroke_to_path(const Path& path, const AffineTransform& matrix,
                    const StrokeData& stroke_data);

// -- SpanBuffer operations --

void span_buffer_init_rect(SpanBuffer& buf, int x, int y, int width, int height);
void span_buffer_init_region(SpanBuffer& buf, std::span<const IntRect> rects);
void span_buffer_copy(SpanBuffer& dst, const SpanBuffer& src);
bool span_buffer_contains(const SpanBuffer& buf, float x, float y);
void span_buffer_extents(const SpanBuffer& buf, Rect& extents);
void span_buffer_intersect(SpanBuffer& dst, const SpanBuffer& a, const SpanBuffer& b);

// -- Rasterize path into spans --

void rasterize(SpanBuffer& span_buffer,
               const Path::Impl& path,
               const AffineTransform& matrix,
               const IntRect& clip_rect,
               const StrokeData* stroke_data,
               FillRule winding,
               bool antialias = true);

// -- Blend spans onto render target --

void blend(Context::Impl& ctx, const SpanBuffer& span_buffer);

void blend(const BlendParams& params, const SpanBuffer& span_buffer,
           const IntRect& clip_rect, const SpanBuffer* clip_spans);

void blend_masked(const BlendParams& params, const SpanBuffer& span_buffer,
                  const IntRect& clip_rect, const SpanBuffer* clip_spans,
                  const Image& mask, MaskMode mode, int mask_ox, int mask_oy);

// Per-pixel composite+blend (premultiplied ARGB32 in/out).
// When working_space is non-null and linear, blending is done in linear light;
// otherwise falls back to sRGB.  Exposed for layer compositing and direct
// pixel operations.
uint32_t composite_pixel(uint32_t src_premul, uint32_t dst_premul,
                         CompositeOp op, BlendMode blend,
                         const ColorSpace* working_space = nullptr);

// Dither a premultiplied ARGB32 pixel in sRGB space using an ordered dither
// pattern.  px/py are device coordinates for the dither matrix.
uint32_t dither_pixel_srgb(uint32_t premul, int px, int py);

// -- Post-processing --

void gaussian_blur(unsigned char* data, int width, int height, int stride, float radius,
                   std::vector<unsigned char>* tmp_buf = nullptr);

// Alpha-only blur: ~4x faster for shadow rendering (1 channel vs 4).
void gaussian_blur_alpha(unsigned char* data, int width, int height, int stride, float radius,
                         std::vector<unsigned char>* tmp_buf = nullptr);

void memfill32(uint32_t* dest, int length, uint32_t value);

// -- Pixel format conversion --

uint32_t pixel_to_argb_premul(uint32_t pixel, PixelFormat src_fmt);
uint32_t pixel_from_argb_premul(uint32_t argb_premul, PixelFormat dst_fmt);

void convert_scanline(const unsigned char* src, PixelFormat src_fmt,
                      unsigned char* dst, PixelFormat dst_fmt,
                      int width, uint32_t rgb_fill = 0xFFFFFFFF);

} // namespace kvg

#endif // KVGPRIVATE_HPP
