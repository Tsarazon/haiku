// plutovg-private.hpp - Internal implementation details
// C++20

#ifndef PLUTOVG_PRIVATE_HPP
#define PLUTOVG_PRIVATE_HPP

#include "plutovg.hpp"
#include "plutovg-utils.hpp"

#include <atomic>
#include <memory>
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

struct FontFace::Impl : RefCounted {
    std::unique_ptr<StbttFontInfo> font_info;
    std::unique_ptr<std::byte[]>   owned_data;
    unsigned int data_length = 0;

    ~Impl() = default;
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

// -- Canvas state stack --

struct State {
    Paint       paint;
    FontFace    font_face;
    Color       color;
    Matrix      matrix;
    StrokeData  stroke;
    SpanBuffer  clip_spans;
    Shadow      shadow;
    FillRule    winding    = FillRule::NonZero;
    Operator    op         = Operator::SrcOver;
    BlendMode   blend_mode = BlendMode::Normal;
    float       font_size  = 12.0f;
    float       opacity    = 1.0f;
    bool        clipping   = false;
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

// -- Blend parameters (narrowed interface for blend function) --

struct BlendParams {
    const Surface&    target;
    const Paint::Impl* paint;
    Operator          op;
    BlendMode         blend_mode;
    float             opacity;
};

// -- Internal functions --

void span_buffer_init_rect(SpanBuffer& buf, int x, int y, int width, int height);
void span_buffer_copy(SpanBuffer& dst, const SpanBuffer& src);
bool span_buffer_contains(const SpanBuffer& buf, float x, float y);
void span_buffer_extents(const SpanBuffer& buf, Rect& extents);
void span_buffer_intersect(SpanBuffer& dst, const SpanBuffer& a, const SpanBuffer& b);

void rasterize(SpanBuffer& span_buffer,
               const Path::Impl& path,
               const Matrix& matrix,
               const IntRect& clip_rect,
               const StrokeData* stroke_data,
               FillRule winding);

/// Blend span buffer onto canvas using current state.
void blend(Canvas::Impl& canvas, const SpanBuffer& span_buffer);

/// Blend span buffer with explicit parameters (for shadow/offscreen passes).
void blend(const BlendParams& params, const SpanBuffer& span_buffer,
           const IntRect& clip_rect, const SpanBuffer* clip_spans);

/// Apply gaussian blur to a surface region.
void gaussian_blur(unsigned char* data, int width, int height, int stride, float radius);

void memfill32(uint32_t* dest, int length, uint32_t value);

} // namespace plutovg

#endif // PLUTOVG_PRIVATE_HPP
