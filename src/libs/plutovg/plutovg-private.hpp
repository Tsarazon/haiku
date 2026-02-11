// plutovg-private.hpp - Internal implementation details
// C++20

#ifndef PLUTOVG_PRIVATE_HPP
#define PLUTOVG_PRIVATE_HPP

#include "plutovg.hpp"
#include "plutovg-utils.hpp"
#include "plutovg-ft-raster.h"

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace plutovg {

// -- Intrusive reference counting --

struct RefCounted {
    std::atomic<int> ref_count{1};

    void ref() noexcept {
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    bool deref() noexcept {
        return ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    [[nodiscard]] int count() const noexcept {
        return ref_count.load(std::memory_order_relaxed);
    }
};

// -- Path::Impl --

struct Path::Impl : RefCounted {
    int num_points   = 0;
    int num_contours = 0;
    int num_curves   = 0;
    Point start_point;
    std::vector<PathElement> elements;
};

// -- Surface::Impl --

struct Surface::Impl : RefCounted {
    int width  = 0;
    int height = 0;
    int stride = 0;
    unsigned char* data = nullptr;
    bool owns_data = false;
    bool writable_external = false; ///< True for wrap(): non-owning, COW disabled.
    PixelFormat format = PixelFormat::ARGB32_Premultiplied;
    float scale_factor = 1.0f;

    ~Impl() {
        if (owns_data)
            std::free(data);
    }
};

// -- Paint internals --

enum class PaintType {
    Color,
    Gradient,
    Texture
};

enum class GradientType {
    Linear,
    Radial,
    Conic
};

struct SolidPaintData {
    Color color;
};

struct GradientPaintData {
    GradientType type = GradientType::Linear;
    SpreadMethod spread = SpreadMethod::Pad;
    Matrix matrix;
    std::vector<GradientStop> stops;
    float values[6] = {};
    // Linear: x1, y1, x2, y2
    // Radial: cx, cy, cr, fx, fy, fr
    // Conic:  cx, cy, start_angle
};

struct TexturePaintData {
    TextureType type = TextureType::Plain;
    float opacity = 1.0f;
    Matrix matrix;
    Surface surface;
};

struct Paint::Impl : RefCounted {
    std::variant<SolidPaintData, GradientPaintData, TexturePaintData> data;

    [[nodiscard]] PaintType type() const noexcept {
        return static_cast<PaintType>(data.index());
    }

    auto& as_solid()         { return std::get<SolidPaintData>(data); }
    auto& as_gradient()      { return std::get<GradientPaintData>(data); }
    auto& as_texture()       { return std::get<TexturePaintData>(data); }

    const auto& as_solid()   const { return std::get<SolidPaintData>(data); }
    const auto& as_gradient()const { return std::get<GradientPaintData>(data); }
    const auto& as_texture() const { return std::get<TexturePaintData>(data); }
};

// -- FontFace::Impl --

// Forward declare stb_truetype info; actual definition in .cpp
struct StbttFontInfo;

// -- FontFace data ownership --

/// Type-erased release callback for externally owned font data.
struct DataRelease {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;

    void operator()(const std::byte*) const noexcept {
        if (fn) fn(ctx);
    }
};

struct FontFace::Impl : RefCounted {
    std::unique_ptr<StbttFontInfo> font_info;
    const std::byte* data = nullptr;
    unsigned int data_length = 0;
    DataRelease release;

    ~Impl();
};

// -- FontFaceCache::Impl --

struct FontFaceCache::Impl : RefCounted {
    struct Entry {
        std::string family;
        bool bold   = false;
        bool italic = false;
        FontFace face;
    };

    std::vector<Entry> entries;
};

// -- Rasterizer types --

struct Span {
    int x;
    int len;
    int y;
    unsigned char coverage;
};

struct SpanBuffer {
    std::vector<Span> spans;
    // NOTE: bounds is only valid after span_buffer_init_rect() or span_buffer_copy().
    // The FreeType rasterizer callback (spans_generation_callback) does NOT update
    // this field, so after rasterize() it remains {0,0,0,0}. Use span_buffer_extents()
    // to compute actual bounds from spans when needed.
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

/// Layer state for offscreen rendering (saveLayer / restore).
/// Stored in the State that owns the layer. On restore, the offscreen surface
/// is composited back onto the parent surface.
struct LayerInfo {
    Surface   surface;            ///< The offscreen layer surface (drawing target).
    Surface   parent_surface;     ///< The surface we were rendering to before the layer.
    IntRect   parent_clip_rect;   ///< The clip_rect of the parent canvas.
    IntRect   device_bounds;      ///< Bounding box of the layer in parent device space.
    float     alpha = 1.0f;       ///< Group opacity applied when compositing.
    BlendMode blend_mode = BlendMode::Normal; ///< Blend mode for compositing.
    Operator  op = Operator::SrcOver;         ///< Compositing operator.
};

// -- Canvas state stack --

struct State {
    Paint       fill_paint;
    Paint       stroke_paint;
    FontFace    font_face;
    Color       fill_color;
    Color       stroke_color;
    Matrix      matrix;
    StrokeData  stroke;
    SpanBuffer  clip_spans;
    Shadow      shadow;
    FillRule    winding            = FillRule::NonZero;
    Operator    op                 = Operator::SrcOver;
    BlendMode   blend_mode         = BlendMode::Normal;
    ColorInterpolation color_interp = ColorInterpolation::SRGB;
    float       font_size          = 12.0f;
    float       opacity            = 1.0f;
    bool        clipping           = false;
    bool        stroke_paint_set   = false; ///< If false, stroke uses fill paint.
    bool        dithering          = false;
    bool        antialias          = true;
    bool        pixel_snap         = false;
    std::optional<LayerInfo> layer; ///< Set only by save_layer(), checked by restore().
};

/// RAII state stack with freelist pooling.
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
            m_stack.push_back(m_stack.back()); // copy current
            m_stack.back().layer.reset();       // only save_layer() sets this
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

// -- Canvas::Impl --

struct Canvas::Impl {
    Surface         surface;
    Path            path;
    StateStack      states;
    FontFaceCache   face_cache;
    IntRect         clip_rect;
    SpanBuffer      clip_spans;
    SpanBuffer      fill_spans;

    // Temporary surface for shadow rendering.
    // Lazily allocated on first shadow draw, reused across frames.
    Surface         shadow_surface;

    State& state() { return states.current(); }
    const State& state() const { return states.current(); }
};

// -- Mask coverage extraction --

namespace mask_ops {

/// Extract coverage [0..255] from a premultiplied ARGB pixel using the given mode.
inline uint8_t extract_coverage(uint32_t pixel, MaskMode mode) {
    switch (mode) {
    case MaskMode::Alpha:
        return alpha(pixel);
    case MaskMode::InvAlpha:
        return 255 - alpha(pixel);
    case MaskMode::Luma: {
        uint8_t a = alpha(pixel);
        if (a == 0) return 0;
        auto [r, g, b] = unpremultiply(pixel);
        return luminance_from_rgb(r, g, b);
    }
    case MaskMode::InvLuma:
        return 255 - extract_coverage(pixel, MaskMode::Luma);
    }
    return 0;
}

} // namespace mask_ops

// -- Blend parameters (narrowed interface for blend function) --

struct BlendParams {
    Surface&           target;
    const Paint::Impl* paint;
    Operator           op;
    BlendMode          blend_mode;
    ColorInterpolation color_interp;
    float              opacity;
    bool               dithering;
};

// -- Internal pimpl accessors --
// Path, Paint, etc. store a single Impl* as their sole data member.
// Internal code needs direct Impl access for blend dispatch / rasterization.

inline Path::Impl* path_impl(Path& p) {
    static_assert(sizeof(Path) == sizeof(void*));
    return *reinterpret_cast<Path::Impl**>(&p);
}

inline const Path::Impl* path_impl(const Path& p) {
    static_assert(sizeof(Path) == sizeof(void*));
    return *reinterpret_cast<Path::Impl* const*>(&p);
}

inline const Paint::Impl* paint_impl(const Paint& p) {
    static_assert(sizeof(Paint) == sizeof(void*));
    return *reinterpret_cast<Paint::Impl* const*>(&p);
}

// -- Internal functions --

// -- FT outline helpers (defined in plutovg-rasterize.cpp) --

struct PVG_FT_Outline; // forward declare

PVG_FT_Outline* ft_outline_create(int points, int contours);
void ft_outline_destroy(PVG_FT_Outline* outline);

/// Convert a Path to an FT outline (transformed by matrix).
PVG_FT_Outline* ft_outline_from_path(const Path& path, const Matrix& matrix);

/// Run the full stroke pipeline: apply dash → convert to FT outline →
/// FT_Stroker → return stroked outline. Caller must ft_outline_destroy().
PVG_FT_Outline* ft_outline_stroke(const Path& path, const Matrix& matrix,
                                    const StrokeData& stroke_data);

/// Convert an FT outline back to a Path. Handles on-curve, conic, and cubic segments.
Path ft_outline_to_path(const PVG_FT_Outline* outline);

// -- Span / blend functions --

void span_buffer_init_rect(SpanBuffer& buf, int x, int y, int width, int height);
void span_buffer_init_region(SpanBuffer& buf, std::span<const IntRect> rects);
void span_buffer_copy(SpanBuffer& dst, const SpanBuffer& src);
bool span_buffer_contains(const SpanBuffer& buf, float x, float y);
void span_buffer_extents(const SpanBuffer& buf, Rect& extents);
void span_buffer_intersect(SpanBuffer& dst, const SpanBuffer& a, const SpanBuffer& b);

void rasterize(SpanBuffer& span_buffer,
               const Path::Impl& path,
               const Matrix& matrix,
               const IntRect& clip_rect,
               const StrokeData* stroke_data,
               FillRule winding,
               bool antialias = true);

/// Blend span buffer onto canvas using current state.
void blend(Canvas::Impl& canvas, const SpanBuffer& span_buffer);

/// Blend span buffer with explicit parameters (for shadow/offscreen passes).
void blend(const BlendParams& params, const SpanBuffer& span_buffer,
           const IntRect& clip_rect, const SpanBuffer* clip_spans);

/// Blend span buffer modulated by a mask surface.
/// Coverage at each pixel is multiplied by the mask value extracted via `mode`.
void blend_masked(const BlendParams& params, const SpanBuffer& span_buffer,
                  const IntRect& clip_rect, const SpanBuffer* clip_spans,
                  const Surface& mask, MaskMode mode, int mask_ox, int mask_oy);

/// Apply gaussian blur to a surface region.
void gaussian_blur(unsigned char* data, int width, int height, int stride, float radius);

void memfill32(uint32_t* dest, int length, uint32_t value);

// -- Pixel format conversion utilities --

/// Convert a single pixel from any 32-bit format to ARGB32_Premultiplied.
uint32_t pixel_to_argb_premul(uint32_t pixel, PixelFormat src_fmt);

/// Convert a single pixel from ARGB32_Premultiplied to any 32-bit format.
uint32_t pixel_from_argb_premul(uint32_t argb_premul, PixelFormat dst_fmt);

/// Convert a scanline between pixel formats.
/// Both src and dst must have at least `width` pixels.
/// For A8 source, rgb_fill is used as the color (ARGB32 premul); coverage modulates alpha.
/// For A8 destination, luminance is extracted.
void convert_scanline(const unsigned char* src, PixelFormat src_fmt,
                      unsigned char* dst, PixelFormat dst_fmt,
                      int width, uint32_t rgb_fill = 0xFFFFFFFF);

} // namespace plutovg

#endif // PLUTOVG_PRIVATE_HPP
