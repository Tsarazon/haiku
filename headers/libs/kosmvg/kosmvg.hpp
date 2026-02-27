// kosmvg.hpp — KosmOS 2D vector graphics library
// C++20, software rasterization

#ifndef KVG_HPP
#define KVG_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(KVG_BUILD_STATIC)
#define KVG_API
#elif defined(KVG_BUILD)
#define KVG_API __attribute__((__visibility__("default")))
#else
#define KVG_API
#endif

namespace kvg {

// -- Math constants --

inline constexpr float pi      = 3.14159265358979323846f;
inline constexpr float two_pi  = 6.28318530717958647693f;
inline constexpr float half_pi = 1.57079632679489661923f;
inline constexpr float kappa   = 0.55228474983079339840f;

inline constexpr float deg2rad(float x) { return x * (pi / 180.0f); }
inline constexpr float rad2deg(float x) { return x * (180.0f / pi); }

// -- Forward declarations --

class Path;
class Image;
class Context;
class BitmapContext;
class Gradient;
class Pattern;
class Shading;
class Font;
class FontCollection;
class ColorSpace;

// -- Error reporting --

enum class Error {
    None,
    OutOfMemory,
    InvalidArgument,
    UnsupportedFormat,
    FileNotFound,
    FileIOError,
    FontParseError,
    ImageDecodeError,
    PathParseError
};

/// Last error from a factory method that returned a null object.
/// Thread-local; reset on each factory call.
[[nodiscard]] KVG_API Error last_error();

/// Human-readable description of an error code.
[[nodiscard]] KVG_API const char* error_string(Error e);

// -- Unicode --

using Codepoint = uint32_t;
using GlyphID   = uint16_t;

// -- Value types --

struct Point {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Point() noexcept = default;
    constexpr Point(float x, float y) noexcept : x(x), y(y) {}

    constexpr Point operator+(Point o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Point operator-(Point o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Point operator*(float s) const noexcept { return {x * s, y * s}; }
    constexpr Point operator/(float s) const noexcept { return {x / s, y / s}; }
    constexpr Point operator-()        const noexcept { return {-x, -y}; }

    constexpr float dot(Point o)   const noexcept { return x * o.x + y * o.y; }
    constexpr float cross(Point o) const noexcept { return x * o.y - y * o.x; }

    float length() const noexcept { return __builtin_sqrtf(x * x + y * y); }
    float distance_to(Point o) const noexcept { return (*this - o).length(); }

    Point normalized() const noexcept {
        float len = length();
        return len > 0.0f ? Point{x / len, y / len} : Point{};
    }

    constexpr auto operator<=>(const Point&) const noexcept = default;
};

struct Size {
    float width  = 0.0f;
    float height = 0.0f;

    constexpr Size() noexcept = default;
    constexpr Size(float w, float h) noexcept : width(w), height(h) {}

    constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }

    constexpr float aspect_ratio() const noexcept {
        return height > 0.0f ? width / height : 0.0f;
    }

    constexpr Size aspect_fit(const Size& bounds) const noexcept {
        if (empty()) return {};
        float sw = bounds.width / width;
        float sh = bounds.height / height;
        float s = sw < sh ? sw : sh;
        return {width * s, height * s};
    }

    constexpr Size aspect_fill(const Size& bounds) const noexcept {
        if (empty()) return {};
        float sw = bounds.width / width;
        float sh = bounds.height / height;
        float s = sw > sh ? sw : sh;
        return {width * s, height * s};
    }

    constexpr auto operator<=>(const Size&) const noexcept = default;
};

struct Rect {
    Point origin;
    Size  size;

    constexpr Rect() noexcept = default;
    constexpr Rect(float x, float y, float w, float h) noexcept
        : origin(x, y), size(w, h) {}
    constexpr Rect(Point o, Size s) noexcept : origin(o), size(s) {}

    constexpr float x()      const noexcept { return origin.x; }
    constexpr float y()      const noexcept { return origin.y; }
    constexpr float width()  const noexcept { return size.width; }
    constexpr float height() const noexcept { return size.height; }

    constexpr float min_x() const noexcept { return origin.x; }
    constexpr float min_y() const noexcept { return origin.y; }
    constexpr float max_x() const noexcept { return origin.x + size.width; }
    constexpr float max_y() const noexcept { return origin.y + size.height; }
    constexpr float mid_x() const noexcept { return origin.x + size.width * 0.5f; }
    constexpr float mid_y() const noexcept { return origin.y + size.height * 0.5f; }

    constexpr Point center() const noexcept { return {mid_x(), mid_y()}; }

    constexpr bool empty() const noexcept { return size.empty(); }

    constexpr bool contains(Point p) const noexcept {
        return p.x >= min_x() && p.x < max_x() && p.y >= min_y() && p.y < max_y();
    }

    constexpr bool intersects(const Rect& o) const noexcept {
        return min_x() < o.max_x() && max_x() > o.min_x()
            && min_y() < o.max_y() && max_y() > o.min_y();
    }

    constexpr Rect intersected(const Rect& o) const noexcept {
        float l = min_x() > o.min_x() ? min_x() : o.min_x();
        float t = min_y() > o.min_y() ? min_y() : o.min_y();
        float r = max_x() < o.max_x() ? max_x() : o.max_x();
        float b = max_y() < o.max_y() ? max_y() : o.max_y();
        return (r > l && b > t) ? Rect{l, t, r - l, b - t} : Rect{};
    }

    constexpr Rect united(const Rect& o) const noexcept {
        if (empty()) return o;
        if (o.empty()) return *this;
        float l = min_x() < o.min_x() ? min_x() : o.min_x();
        float t = min_y() < o.min_y() ? min_y() : o.min_y();
        float r = max_x() > o.max_x() ? max_x() : o.max_x();
        float b = max_y() > o.max_y() ? max_y() : o.max_y();
        return {l, t, r - l, b - t};
    }

    constexpr Rect inset(float dx, float dy) const noexcept {
        return {origin.x + dx, origin.y + dy,
                size.width - 2 * dx, size.height - 2 * dy};
    }

    constexpr Rect outset(float dx, float dy) const noexcept {
        return inset(-dx, -dy);
    }

    constexpr Rect offset(float dx, float dy) const noexcept {
        return {origin.x + dx, origin.y + dy, size.width, size.height};
    }

    constexpr auto operator<=>(const Rect&) const noexcept = default;
};

struct IntRect {
    int x = 0, y = 0, w = 0, h = 0;

    constexpr IntRect() noexcept = default;
    constexpr IntRect(int x, int y, int w, int h) noexcept : x(x), y(y), w(w), h(h) {}

    constexpr int  right()  const noexcept { return x + w; }
    constexpr int  bottom() const noexcept { return y + h; }
    constexpr bool empty()  const noexcept { return w <= 0 || h <= 0; }

    constexpr IntRect intersected(const IntRect& o) const noexcept {
        int l = x > o.x ? x : o.x;
        int t = y > o.y ? y : o.y;
        int r = right() < o.right() ? right() : o.right();
        int b = bottom() < o.bottom() ? bottom() : o.bottom();
        return (r > l && b > t) ? IntRect{l, t, r - l, b - t} : IntRect{};
    }

    constexpr IntRect united(const IntRect& o) const noexcept {
        if (empty()) return o;
        if (o.empty()) return *this;
        int l = x < o.x ? x : o.x;
        int t = y < o.y ? y : o.y;
        int r = right() > o.right() ? right() : o.right();
        int b = bottom() > o.bottom() ? bottom() : o.bottom();
        return {l, t, r - l, b - t};
    }

    constexpr auto operator<=>(const IntRect&) const noexcept = default;
};

struct CornerRadii {
    float top_left     = 0.0f;
    float top_right    = 0.0f;
    float bottom_right = 0.0f;
    float bottom_left  = 0.0f;

    constexpr CornerRadii() noexcept = default;
    constexpr explicit CornerRadii(float r) noexcept
        : top_left(r), top_right(r), bottom_right(r), bottom_left(r) {}
    constexpr CornerRadii(float tl, float tr, float br, float bl) noexcept
        : top_left(tl), top_right(tr), bottom_right(br), bottom_left(bl) {}

    constexpr bool is_uniform() const noexcept {
        return top_left == top_right && top_right == bottom_right && bottom_right == bottom_left;
    }
    constexpr bool is_zero() const noexcept {
        return top_left == 0 && top_right == 0 && bottom_right == 0 && bottom_left == 0;
    }
};

// -- Affine transform --

/// Layout: | a  c  tx |
///         | b  d  ty |
///         | 0  0  1  |
struct AffineTransform {
    float a  = 1.0f;
    float b  = 0.0f;
    float c  = 0.0f;
    float d  = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;

    constexpr AffineTransform() noexcept = default;
    constexpr AffineTransform(float a, float b, float c, float d, float tx, float ty) noexcept
        : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}

    static constexpr AffineTransform identity() noexcept { return {}; }

    static constexpr AffineTransform translated(float x, float y) noexcept {
        return {1, 0, 0, 1, x, y};
    }

    static constexpr AffineTransform scaled(float sx, float sy) noexcept {
        return {sx, 0, 0, sy, 0, 0};
    }

    KVG_API static AffineTransform rotated(float radians);
    KVG_API static AffineTransform sheared(float shx, float shy);

    /// Parse an SVG transform attribute string (e.g. "translate(10,20) rotate(45)").
    /// Returns std::nullopt on parse failure.
    [[nodiscard]] KVG_API static std::optional<AffineTransform> parse_svg(std::string_view svg);

    KVG_API AffineTransform& translate(float x, float y);
    KVG_API AffineTransform& scale(float sx, float sy);
    KVG_API AffineTransform& rotate(float radians);
    KVG_API AffineTransform& shear(float shx, float shy);

    constexpr float determinant() const noexcept { return a * d - b * c; }
    constexpr bool  is_invertible() const noexcept { return determinant() != 0.0f; }
    constexpr bool  is_identity() const noexcept {
        return a == 1 && b == 0 && c == 0 && d == 1 && tx == 0 && ty == 0;
    }

    [[nodiscard]] constexpr std::optional<AffineTransform> inverted() const noexcept {
        float det = determinant();
        if (det == 0.0f) return std::nullopt;
        float inv = 1.0f / det;
        return AffineTransform{
             d * inv, -b * inv,
            -c * inv,  a * inv,
            (c * ty - d * tx) * inv,
            (b * tx - a * ty) * inv
        };
    }

    [[nodiscard]] constexpr Point apply(Point p) const noexcept {
        return {a * p.x + c * p.y + tx,
                b * p.x + d * p.y + ty};
    }

    KVG_API void apply(const Point* src, Point* dst, int count) const;
    [[nodiscard]] KVG_API Rect apply_to_rect(const Rect& r) const;

    [[nodiscard]] constexpr AffineTransform operator*(const AffineTransform& rhs) const noexcept {
        return {
            a * rhs.a + c * rhs.b,
            b * rhs.a + d * rhs.b,
            a * rhs.c + c * rhs.d,
            b * rhs.c + d * rhs.d,
            a * rhs.tx + c * rhs.ty + tx,
            b * rhs.tx + d * rhs.ty + ty
        };
    }

    AffineTransform& operator*=(const AffineTransform& rhs) noexcept {
        *this = *this * rhs;
        return *this;
    }

    constexpr auto operator<=>(const AffineTransform&) const noexcept = default;
};

// -- Path enums --

enum class PathElement : uint8_t {
    MoveToPoint,
    AddLineToPoint,
    AddQuadCurve,    // 2 points (control, end)
    AddCurve,        // 3 points (cp1, cp2, end)
    CloseSubpath
};

enum class FillRule {
    Winding,
    EvenOdd,
    NonZero = Winding
};

/// Controls how Path::trimmed() distributes the visible segment
/// across multiple sub-paths.
enum class TrimMode {
    /// Each sub-path is trimmed individually by the same [begin, end]
    /// fraction of its own length.
    Simultaneous,
    /// All sub-paths are concatenated into a single virtual length;
    /// the [begin, end] range selects a continuous segment across
    /// the combined geometry.
    Sequential
};

// -- Stroke enums --

enum class LineCap  { Butt, Round, Square };
enum class LineJoin { Miter, Round, Bevel };

// -- Compositing & blending --

/// Porter-Duff compositing operator. Determines how source and destination
/// alpha channels combine. CompositeOp and BlendMode are independent:
/// CompositeOp controls alpha compositing, BlendMode controls color mixing.
enum class CompositeOp {
    Clear,
    Copy,
    SourceOver,      // Default. Most common for UI layering.
    SourceIn,
    SourceOut,
    SourceAtop,
    DestinationOver,
    DestinationIn,
    DestinationOut,
    DestinationAtop,
    Xor
};

/// Extended blend mode applied per-pixel on top of the compositing operator.
/// Controls how source and destination colors are mixed.
enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    SoftLight,
    HardLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
    Add          ///< Additive blending: S + D. Useful for light effects, glows, particles.
};

// -- Rendering quality --

enum class InterpolationQuality {
    Default,
    None,     // Nearest neighbor
    Low,      // Bilinear
    Medium,   // Bicubic
    High      // Lanczos
};

// -- Masking --

enum class MaskMode {
    Alpha,
    InvertedAlpha,
    Luminance,
    InvertedLuminance,
    Add,               // (T * TA) + (S * (1 - TA))
    Subtract,          // (T * TA) - (S * (1 - TA))
    Intersect,         // T * min(TA, SA)
    Difference,        // abs(T - S * (1 - TA))
    Lighten,           // max(TA, SA) — highest transparency wins
    Darken             // min(TA, SA) — lowest transparency wins
};

// -- Text enums --

enum class TextDrawingMode {
    Fill,
    Stroke,
    FillStroke,
    Invisible,
    FillClip,
    StrokeClip,
    FillStrokeClip,
    Clip
};

enum class TextAntialias {
    Grayscale,       // Standard coverage-based AA (default)
    SubpixelRGB,     // LCD subpixel rendering (RGB stripe order)
    SubpixelBGR,     // LCD subpixel rendering (BGR stripe order)
    None             // No text anti-aliasing
};

// -- Gradient enums --

enum class GradientDrawingOptions : uint32_t {
    None             = 0,
    DrawsBeforeStart = 1 << 0,
    DrawsAfterEnd    = 1 << 1
};

inline constexpr GradientDrawingOptions operator|(GradientDrawingOptions a, GradientDrawingOptions b) {
    return static_cast<GradientDrawingOptions>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr bool operator&(GradientDrawingOptions a, GradientDrawingOptions b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

/// Specifies how the gradient fills the area outside its defined bounds.
/// Matches SVG spreadMethod attribute.
enum class GradientSpread {
    Pad,       // Extend the closest stop color (default, same as SVG "pad").
    Reflect,   // Mirror the gradient pattern at each boundary.
    Repeat     // Tile the gradient pattern continuously.
};

// -- Pattern enums --

enum class PatternTiling {
    NoDistortion,
    ConstantSpacing,
    ConstantSpacingMinimalDistortion
};

// -- Vertex enums --

/// How vertex positions are assembled into triangles for draw_vertices().
enum class VertexMode {
    Triangles,       // Every 3 vertices form an independent triangle.
    TriangleStrip,   // Each vertex after the first two forms a triangle
                     // with the preceding two vertices.
    TriangleFan      // All triangles share the first vertex as a common
                     // pivot (useful for convex polygons, pie slices).
};

// -- Pixel format --

enum class PixelFormat : uint8_t {
    ARGB32_Premultiplied,
    BGRA32_Premultiplied,
    RGBA32,
    BGRA32,
    A8
};

struct PixelFormatInfo {
    uint8_t bpp;
    bool    premultiplied;
    bool    has_color;
};

// -- Image file formats --

enum class ImageFormat : uint8_t {
    PNG,
    JPG,
    BMP,
    TGA,
    HDR
};

inline constexpr PixelFormatInfo pixel_format_info(PixelFormat fmt) {
    switch (fmt) {
    case PixelFormat::ARGB32_Premultiplied: return {4, true,  true};
    case PixelFormat::BGRA32_Premultiplied: return {4, true,  true};
    case PixelFormat::RGBA32:               return {4, false, true};
    case PixelFormat::BGRA32:               return {4, false, true};
    case PixelFormat::A8:                   return {1, false, false};
    }
    return {4, true, true};
}

inline constexpr int min_stride(int width, PixelFormat fmt) {
    return width * pixel_format_info(fmt).bpp;
}

// -- ColorSpace --

/// Describes the color model and transfer function for color interpretation.
/// sRGB is the default; linear sRGB gives physically correct blending and
/// gradient interpolation; Display P3 covers wide-gamut mobile displays.
/// Immutable, cheap to copy (shared ownership).
class KVG_API ColorSpace {
public:
    constexpr ColorSpace() noexcept : m_impl(nullptr) {}
    ~ColorSpace();
    ColorSpace(const ColorSpace&);
    ColorSpace(ColorSpace&&) noexcept;
    ColorSpace& operator=(const ColorSpace&);
    ColorSpace& operator=(ColorSpace&&) noexcept;

    explicit operator bool() const;

    [[nodiscard]] static ColorSpace srgb();
    [[nodiscard]] static ColorSpace linear_srgb();
    [[nodiscard]] static ColorSpace display_p3();
    [[nodiscard]] static ColorSpace extended_linear_srgb();
    [[nodiscard]] static ColorSpace device_gray();

    [[nodiscard]] int component_count() const;

    struct Impl;
private:
    Impl* m_impl = nullptr;
};

// -- Color --

/// RGBA color with float components in [0, 1], tagged with a ColorSpace.
/// The default (no space specified) is sRGB. Use the ColorSpace-taking
/// constructor for Display P3 or linear sRGB colors.
struct Color {
    float components[4] = {0, 0, 0, 1};
    ColorSpace space;

    Color() = default;

    constexpr Color(float r, float g, float b, float a = 1.0f)
        : components{r, g, b, a} {}

    Color(const ColorSpace& cs, float c0, float c1, float c2, float a = 1.0f)
        : components{c0, c1, c2, a}, space(cs) {}

    constexpr float r() const { return components[0]; }
    constexpr float g() const { return components[1]; }
    constexpr float b() const { return components[2]; }
    constexpr float a() const { return components[3]; }

    static inline Color black()  { return {0, 0, 0, 1}; }
    static inline Color white()  { return {1, 1, 1, 1}; }
    static inline Color clear()  { return {0, 0, 0, 0}; }
    static inline Color red()    { return {1, 0, 0, 1}; }
    static inline Color green()  { return {0, 1, 0, 1}; }
    static inline Color blue()   { return {0, 0, 1, 1}; }

    static inline Color from_rgb8(int r, int g, int b) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
    }

    static inline Color from_rgba8(int r, int g, int b, int a) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }

    static inline Color from_argb32(uint32_t v) {
        return from_rgba8((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, (v >> 24) & 0xFF);
    }

    KVG_API static Color from_hsl(float h, float s, float l, float a = 1.0f);
    KVG_API static std::optional<Color> parse(std::string_view css_color);

    [[nodiscard]] KVG_API Color converted(const ColorSpace& target) const;

    // -- Modifications --

    Color with_alpha(float new_a) const {
        Color r{components[0], components[1], components[2], new_a};
        r.space = space;
        return r;
    }

    Color premultiplied() const {
        float pa = components[3];
        Color r{components[0] * pa, components[1] * pa, components[2] * pa, pa};
        r.space = space;
        return r;
    }

    Color unpremultiplied() const {
        float pa = components[3];
        if (pa <= 0.0f) return {0, 0, 0, 0};
        float inv = 1.0f / pa;
        Color r{components[0] * inv, components[1] * inv, components[2] * inv, pa};
        r.space = space;
        return r;
    }

    Color clamped() const {
        // NaN-safe: !(v > 0) catches both v<=0 and NaN → 0.
        auto cl = [](float v) { return !(v > 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v); };
        Color r{cl(components[0]), cl(components[1]), cl(components[2]), cl(components[3])};
        r.space = space;
        return r;
    }

    Color inverted() const {
        Color r{1.0f - components[0], 1.0f - components[1],
                1.0f - components[2], components[3]};
        r.space = space;
        return r;
    }

    // -- Blending --

    Color lerp(const Color& other, float t) const {
        Color r{components[0] + (other.components[0] - components[0]) * t,
                components[1] + (other.components[1] - components[1]) * t,
                components[2] + (other.components[2] - components[2]) * t,
                components[3] + (other.components[3] - components[3]) * t};
        r.space = space;
        return r;
    }

    /// Porter-Duff source-over: this color composited over background.
    Color over(const Color& bg) const {
        float sa = a(), ba = bg.a();
        float out_a = sa + ba * (1.0f - sa);
        if (out_a <= 0.0f) return {0, 0, 0, 0};
        float inv = 1.0f / out_a;
        float one_minus_sa = 1.0f - sa;
        Color result{(r() * sa + bg.r() * ba * one_minus_sa) * inv,
                (g() * sa + bg.g() * ba * one_minus_sa) * inv,
                (b() * sa + bg.b() * ba * one_minus_sa) * inv,
                out_a};
        result.space = space;
        return result;
    }

    // -- Queries --

    /// sRGB luminance (Rec. 709 coefficients).
    constexpr float luminance() const {
        return 0.2126f * components[0] + 0.7152f * components[1]
             + 0.0722f * components[2];
    }

    [[nodiscard]] inline uint32_t to_argb32() const {
        // NaN-safe: !(v > 0) catches both v<=0 and NaN → 0.
        auto clamp = [](float v) -> uint8_t {
            if (!(v > 0)) return 0;
            if (v >= 1) return 255;
            return static_cast<uint8_t>(v * 255.0f + 0.5f);
        };
        return (uint32_t(clamp(a())) << 24) | (uint32_t(clamp(r())) << 16)
             | (uint32_t(clamp(g())) << 8)  |  uint32_t(clamp(b()));
    }
};

// -- Shadow --

struct Shadow {
    Point offset;
    float blur  = 0.0f;
    Color color = {0, 0, 0, 0.33f};

    constexpr Shadow() noexcept = default;
    Shadow(Point offset, float blur, const Color& color)
        : offset(offset), blur(blur), color(color) {}

    inline bool is_none() const noexcept {
        return color.a() <= 0.0f || (blur <= 0.0f && offset.x == 0.0f && offset.y == 0.0f);
    }
};

// -- Color matrix --

struct ColorMatrix {
    float m[20] = {
        1,0,0,0,0,
        0,1,0,0,0,
        0,0,1,0,0,
        0,0,0,1,0
    };

    static constexpr ColorMatrix identity() { return {}; }

    KVG_API static ColorMatrix grayscale();
    KVG_API static ColorMatrix sepia();
    KVG_API static ColorMatrix saturate(float amount);
    KVG_API static ColorMatrix brightness(float amount);
    KVG_API static ColorMatrix contrast(float amount);
    KVG_API static ColorMatrix hue_rotate(float radians);
    KVG_API static ColorMatrix invert();

    [[nodiscard]] constexpr ColorMatrix operator*(const ColorMatrix& o) const {
        ColorMatrix r;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 5; ++col) {
                float sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += m[row * 5 + k] * o.m[k * 5 + col];
                if (col == 4) sum += m[row * 5 + 4];
                r.m[row * 5 + col] = sum;
            }
        return r;
    }
};

// -- Mask filter --

/// Blur applied to the coverage mask between rasterization and colorization.
/// Unlike Shadow (which draws a separate offset copy), MaskFilter softens
/// the edges of the shape itself.
struct MaskFilter {
    /// Controls which parts of the blurred mask are drawn.
    enum class Style {
        Normal,    // Blur both inner and outer edges.
        Solid,     // Keep interior fully opaque, blur only outer edge.
        Outer,     // Draw only the blurred outer fringe (no interior).
        Inner      // Draw only the blurred inner fringe (no exterior).
    };

    float sigma = 0.0f;
    Style style = Style::Normal;

    constexpr MaskFilter() noexcept = default;
    constexpr explicit MaskFilter(float sigma,
                                  Style style = Style::Normal) noexcept
        : sigma(sigma), style(style) {}

    constexpr bool is_none() const noexcept { return sigma <= 0.0f; }

    /// Pixel radius of the blur kernel derived from sigma.
    /// Equivalent to ceil(2 * sigma); returns 0 when sigma <= 0.
    constexpr float blur_radius() const noexcept {
        return sigma > 0.0f ? sigma * 2.0f : 0.0f;
    }
};

// -- Font metrics --

struct FontMetrics {
    float ascent     = 0;
    float descent    = 0;
    float leading    = 0;
    float x_height   = 0;
    float cap_height = 0;
    Rect  bounding_box;
    float units_per_em = 0;
};

struct GlyphMetrics {
    float advance = 0;
    Rect  bounds;
};

// -- Glyph positioning (shaper output) --

/// A single positioned glyph for drawing.
/// This is the output format expected from a text shaper.
/// position is the baseline origin; offset is an additional per-glyph
/// adjustment (e.g. for combining marks, diacritics).
struct GlyphPosition {
    GlyphID glyph;
    Point   position;   // Baseline origin
    Point   offset;     // Shaper adjustment (mark positioning, etc.)
};

// -- Font variations (OpenType fvar/gvar) --

/// OpenType font variation axis value (fvar/gvar).
/// Tag is a 4-byte OpenType tag, e.g. 'wght', 'wdth', 'slnt'.
/// Construct a 4-byte OpenType tag from four ASCII characters.
/// Usable everywhere: FontVariation axes, Font::table_data(),
/// GPOS/GSUB lookups, any OpenType table access.
inline constexpr uint32_t make_otf_tag(char a, char b, char c, char d) {
    return (uint32_t(a) << 24) | (uint32_t(b) << 16)
         | (uint32_t(c) << 8)  |  uint32_t(d);
}

/// OpenType font variation axis value (fvar/gvar).
/// Tag is a 4-byte OpenType tag, e.g. 'wght', 'wdth', 'slnt'.
struct FontVariation {
    uint32_t tag;
    float    value;

    static constexpr uint32_t Weight      = make_otf_tag('w','g','h','t');
    static constexpr uint32_t Width       = make_otf_tag('w','d','t','h');
    static constexpr uint32_t Slant       = make_otf_tag('s','l','n','t');
    static constexpr uint32_t Italic      = make_otf_tag('i','t','a','l');
    static constexpr uint32_t OpticalSize = make_otf_tag('o','p','s','z');
};

// -- Color font formats --

/// Describes which color font technology a glyph uses, if any.
enum class ColorFontFormat {
    None,        // Monochrome outline
    COLR_v0,     // Layered color outlines (COLR + CPAL, version 0)
    COLR_v1,     // Paint-based color outlines (COLR v1, gradients etc.)
    Sbix,        // Embedded bitmap (Apple sbix)
    CBDT         // Embedded bitmap (Google CBDT/CBLC)
};

// -- Path (immutable geometry) --

/// Immutable path. Build with Path::Builder, then finalize.
/// Copies are cheap (shared data). Thread-safe for reads.
///
/// Immutable 2D path. Copies are cheap (shared data, COW). Thread-safe for reads.
///
/// Paths are built using Path::Builder, then finalized into an immutable Path.
/// Path is always separate from the drawing context — all draw calls
/// receive it explicitly.
class KVG_API Path {
public:
    Path();
    ~Path();
    Path(const Path& other);
    Path(Path&& other) noexcept;
    Path& operator=(const Path& other);
    Path& operator=(Path&& other) noexcept;

    explicit operator bool() const;

    [[nodiscard]] Path clone() const;

    // -- Queries --

    [[nodiscard]] bool  is_empty() const;
    [[nodiscard]] Rect  bounding_box() const;
    [[nodiscard]] Rect  path_bounding_box() const;
    [[nodiscard]] float length() const;
    [[nodiscard]] bool  contains(Point p, FillRule rule = FillRule::Winding) const;

    [[nodiscard]] bool stroke_contains(Point p, float line_width,
                                       LineCap cap = LineCap::Butt,
                                       LineJoin join = LineJoin::Miter,
                                       float miter_limit = 10.0f) const;

    // -- Derived paths --

    [[nodiscard]] Path transformed(const AffineTransform& t) const;

    [[nodiscard]] Path stroked(float width,
                               LineCap cap = LineCap::Butt,
                               LineJoin join = LineJoin::Miter,
                               float miter_limit = 10.0f) const;

    [[nodiscard]] Path dashed(float offset, std::span<const float> dashes) const;
    [[nodiscard]] Path trimmed(float begin, float end,
                               TrimMode mode = TrimMode::Simultaneous) const;
    [[nodiscard]] Path flattened(float tolerance = 0.25f) const;
    [[nodiscard]] Path reversed() const;

    // -- Boolean ops --

    [[nodiscard]] Path united(const Path& other, FillRule rule = FillRule::Winding) const;
    [[nodiscard]] Path intersected(const Path& other, FillRule rule = FillRule::Winding) const;
    [[nodiscard]] Path subtracted(const Path& other, FillRule rule = FillRule::Winding) const;
    [[nodiscard]] Path xored(const Path& other, FillRule rule = FillRule::Winding) const;

    // -- Traversal --

    struct Element {
        PathElement type;
        const Point* points;
    };

    using ApplyFunc = void(*)(void* info, const Element* element);
    void apply(ApplyFunc func, void* info) const;

    template<typename F>
    void apply(F&& func) const {
        auto fn = std::forward<F>(func);
        apply([](void* ctx, const Element* e) {
            (*static_cast<std::decay_t<F>*>(ctx))(*e);
        }, static_cast<void*>(&fn));
    }

    // -- Iteration --

    class Iterator {
    public:
        Iterator() = default;
        Element operator*() const;
        Iterator& operator++();
        bool operator==(const Iterator&) const;
        bool operator!=(const Iterator& o) const { return !(*this == o); }
    private:
        friend class Path;
        const void* m_data = nullptr;
        int m_index = 0;
        int m_pt_offset = 0;
    };

    [[nodiscard]] Iterator begin() const;
    [[nodiscard]] Iterator end() const;

    // -- SVG parsing --

    [[nodiscard]] static std::optional<Path> parse_svg(std::string_view data);

    /// Construct a path from pre-existing command and point arrays without copying.
    /// The arrays must remain valid for the lifetime of this Path.
    /// Use this for hot paths where geometry is stored externally (compositor,
    /// retained UI tree).
    [[nodiscard]] static Path from_raw(const PathElement* elements, int element_count,
                                       const Point* points, int point_count);

    // -- Builder --

    /// Mutable path builder. Finalize with build() to get an immutable Path.
    ///
    ///     auto path = Path::Builder{}
    ///         .move_to(10, 20)
    ///         .line_to(100, 200)
    ///         .close()
    ///         .build();
    class KVG_API Builder {
    public:
        Builder();
        ~Builder();
        Builder(const Builder&);
        Builder(Builder&&) noexcept;
        Builder& operator=(const Builder&);
        Builder& operator=(Builder&&) noexcept;

        explicit Builder(const Path& source);

        Builder& move_to(float x, float y);
        Builder& line_to(float x, float y);
        Builder& quad_to(float cpx, float cpy, float x, float y);
        Builder& cubic_to(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);
        Builder& arc_to(float rx, float ry, float angle, bool large_arc, bool sweep, float x, float y);
        Builder& close();

        Builder& add_rect(const Rect& r);
        Builder& add_round_rect(const Rect& r, float rx, float ry);
        Builder& add_round_rect(const Rect& r, const CornerRadii& radii);
        Builder& add_ellipse(const Rect& r);
        Builder& add_circle(Point center, float radius);
        Builder& add_arc(Point center, float radius, float start, float end, bool clockwise);
        Builder& add_path(const Path& path, const AffineTransform* t = nullptr);

        Builder& move_to_relative(float dx, float dy);
        Builder& line_to_relative(float dx, float dy);
        Builder& quad_to_relative(float dcpx, float dcpy, float dx, float dy);
        Builder& cubic_to_relative(float dcp1x, float dcp1y, float dcp2x, float dcp2y, float dx, float dy);

        Builder& reserve(int elements);
        Builder& transform(const AffineTransform& t);

        [[nodiscard]] Point current_point() const;
        [[nodiscard]] bool  is_empty() const;

        /// Finalize into immutable Path. Builder is reset after this.
        [[nodiscard]] Path build();

        struct Impl;
    private:
        Impl* m_impl = nullptr;
    };

    struct Impl;
private:
    Impl* m_impl = nullptr;
};

// -- Image file info (metadata without decoding) --

struct ImageFileInfo {
    int width    = 0;
    int height   = 0;
    int channels = 0;
    bool is_hdr    = false;
    bool is_16bit  = false;
};

// -- Image (read-only pixel data) --

/// Immutable pixel buffer. Cheap to copy (shared data), thread-safe for reads.
class KVG_API Image {
public:
    Image();
    ~Image();
    Image(const Image&);
    Image(Image&&) noexcept;
    Image& operator=(const Image&);
    Image& operator=(Image&&) noexcept;

    explicit operator bool() const;

    /// Create from raw pixel data. The data is copied into internal storage.
    [[nodiscard]] static Image create(int width, int height, int stride,
                                      PixelFormat format,
                                      const ColorSpace& space,
                                      std::span<const unsigned char> data);

    /// Create from raw pixel data without copying. The caller provides a
    /// release callback invoked when the Image no longer needs the data.
    /// Pass nullptr for release if the data has static lifetime.
    [[nodiscard]] static Image create(int width, int height, int stride,
                                      PixelFormat format,
                                      const ColorSpace& space,
                                      const unsigned char* data,
                                      void (*release)(void*), void* info);

    [[nodiscard]] static Image load(const char* filename);
    [[nodiscard]] static Image load(std::span<const std::byte> data);
    [[nodiscard]] static Image load_base64(std::string_view data);

    /// Query image file metadata without decoding pixels.
    /// Returns std::nullopt if the file cannot be read or recognized.
    [[nodiscard]] static std::optional<ImageFileInfo> info(const char* filename);
    [[nodiscard]] static std::optional<ImageFileInfo> info(std::span<const std::byte> data);

    /// Last stbi decode failure reason (thread-local).
    /// Human-readable string after a failed load(), e.g. "corrupt JPEG".
    [[nodiscard]] static const char* decode_failure_reason();

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int stride() const;
    [[nodiscard]] PixelFormat format() const;
    [[nodiscard]] ColorSpace  color_space() const;
    [[nodiscard]] const unsigned char* data() const;

    /// HDR headroom: ratio of peak luminance to SDR white point.
    /// 1.0 = standard dynamic range. Values above 1.0 indicate HDR content
    /// (e.g. 4.0 means peak is 4x brighter than SDR white).
    /// Stored as metadata — does not affect pixel values.
    [[nodiscard]] float headroom() const;

    /// Return a copy of this Image tagged with a new headroom value.
    /// Pixel data is shared (COW); only the metadata changes.
    [[nodiscard]] Image with_headroom(float headroom) const;

    [[nodiscard]] Image cropped(const IntRect& rect) const;
    [[nodiscard]] Image converted(PixelFormat fmt, const ColorSpace& space) const;
    [[nodiscard]] Image filtered(const ColorMatrix& cm) const;
    [[nodiscard]] Image blurred(float radius) const;
    [[nodiscard]] Image alpha_mask() const;

    /// Resize the image to exact pixel dimensions.
    /// Uses the specified interpolation quality for resampling.
    [[nodiscard]] Image scaled(int new_width, int new_height,
                               InterpolationQuality quality = InterpolationQuality::Medium) const;

    /// Resize by a uniform scale factor (e.g. 0.5 = half size, 2.0 = double).
    [[nodiscard]] Image scaled(float factor,
                               InterpolationQuality quality = InterpolationQuality::Medium) const;

    // -- Writing --

    bool write(const char* filename, ImageFormat format) const;
    bool write(const char* filename, ImageFormat format, int quality) const;

    using WriteFunc = void(*)(void* info, const void* data, int size);
    bool write_stream(WriteFunc func, void* info, ImageFormat format) const;
    bool write_stream(WriteFunc func, void* info, ImageFormat format, int quality) const;

    template<typename F>
        requires(!std::is_convertible_v<std::decay_t<F>, const char*>)
    bool write(F&& func, ImageFormat format) const {
        auto fn = std::forward<F>(func);
        return write_stream([](void* ctx, const void* data, int sz) {
            (*static_cast<std::decay_t<F>*>(ctx))(data, sz);
        }, static_cast<void*>(&fn), format);
    }

    /// Convenience: infer format from filename extension.
    /// Returns false if the extension is not recognized.
    bool write(const char* filename) const;

    /// Encode the image to the specified format in memory.
    /// Returns an empty vector on failure (check last_error()).
    [[nodiscard]] std::vector<std::byte> encode(ImageFormat format) const;
    [[nodiscard]] std::vector<std::byte> encode(ImageFormat format, int quality) const;

    // Legacy convenience (PNG/JPG shortcuts).
    bool write_png(const char* filename) const { return write(filename, ImageFormat::PNG); }
    bool write_jpg(const char* filename, int quality = 90) const { return write(filename, ImageFormat::JPG, quality); }

    struct Impl;
private:
    friend Impl*       image_impl(Image&);
    friend const Impl* image_impl(const Image&);
    Impl* m_impl = nullptr;
};

// -- Gradient (immutable color stops) --

/// Immutable list of color stops. Geometry (start/end points) is given at draw time.
class KVG_API Gradient {
public:
    struct Stop {
        float offset;
        Color color;
    };

    Gradient();
    ~Gradient();
    Gradient(const Gradient&);
    Gradient(Gradient&&) noexcept;
    Gradient& operator=(const Gradient&);
    Gradient& operator=(Gradient&&) noexcept;

    explicit operator bool() const;

    [[nodiscard]] static Gradient create(const ColorSpace& space,
                                         std::span<const Stop> stops,
                                         GradientSpread spread = GradientSpread::Pad);
    [[nodiscard]] static Gradient create(std::span<const Stop> stops,
                                         GradientSpread spread = GradientSpread::Pad);
    [[nodiscard]] static Gradient create(const Color& start, const Color& end,
                                         GradientSpread spread = GradientSpread::Pad);

    [[nodiscard]] int  stop_count() const;
    [[nodiscard]] Stop stop_at(int index) const;
    [[nodiscard]] GradientSpread spread() const;

    /// Interpolate the color at position t (0..1) along the gradient.
    [[nodiscard]] KVG_API Color sample(float t) const;

    struct Impl;
private:
    Impl* m_impl = nullptr;
};

// -- Pattern (tiled drawing) --

/// Tiled drawing primitive. Can be either a callback that draws one tile
/// (colored pattern) or a tiled image. Immutable after creation, cheap to copy.
class KVG_API Pattern {
public:
    using DrawFunc = void(*)(void* info, Context& ctx);

    Pattern();
    ~Pattern();
    Pattern(const Pattern&);
    Pattern(Pattern&&) noexcept;
    Pattern& operator=(const Pattern&);
    Pattern& operator=(Pattern&&) noexcept;

    explicit operator bool() const;

    [[nodiscard]] static Pattern create_colored(
        Rect bounds, AffineTransform matrix,
        float x_step, float y_step,
        PatternTiling tiling,
        DrawFunc draw, void* info,
        void (*release)(void*) = nullptr);

    template<typename F>
    [[nodiscard]] static Pattern create_colored(
        Rect bounds, AffineTransform matrix,
        float x_step, float y_step,
        PatternTiling tiling, F&& draw_func)
    {
        auto* fn = new std::decay_t<F>(std::forward<F>(draw_func));
        return create_colored(bounds, matrix, x_step, y_step, tiling,
            [](void* ctx, Context& c) { (*static_cast<std::decay_t<F>*>(ctx))(c); },
            fn,
            [](void* ctx) { delete static_cast<std::decay_t<F>*>(ctx); });
    }

    [[nodiscard]] static Pattern create_from_image(
        const Image& image,
        AffineTransform matrix = AffineTransform::identity());

    struct Impl;
private:
    Impl* m_impl = nullptr;
};

// -- Shading (procedural color function) --

/// Procedural color generator driven by a user callback. Analogous to
/// CGShading / CGFunction in CoreGraphics. Instead of discrete color
/// stops, the callback receives a parametric value in [0, 1] and
/// returns a color — enabling arbitrary color functions (perlin noise,
/// spectral gradients, procedural textures, scientific visualization
/// palettes, etc.).
///
/// Immutable after creation, cheap to copy (shared data).
///
///     auto shading = Shading::create_axial(
///         start, end,
///         [](float t) -> Color {
///             float hue = t * 360.0f;
///             return Color::from_hsl(hue, 1.0f, 0.5f);
///         });
///     ctx.draw_shading(shading);
class KVG_API Shading {
public:
    using EvalFunc = void(*)(void* info, float input, Color* output);

    Shading();
    ~Shading();
    Shading(const Shading&);
    Shading(Shading&&) noexcept;
    Shading& operator=(const Shading&);
    Shading& operator=(Shading&&) noexcept;

    explicit operator bool() const;

    /// Axial (linear) shading between two points.
    [[nodiscard]] static Shading create_axial(
        Point start, Point end,
        const ColorSpace& space,
        EvalFunc eval, void* info,
        void (*release)(void*) = nullptr,
        bool extend_start = false, bool extend_end = false);

    /// Radial shading between two circles.
    [[nodiscard]] static Shading create_radial(
        Point start_center, float start_radius,
        Point end_center, float end_radius,
        const ColorSpace& space,
        EvalFunc eval, void* info,
        void (*release)(void*) = nullptr,
        bool extend_start = false, bool extend_end = false);

    /// Convenience: axial shading with a C++ callable.
    template<typename F>
    [[nodiscard]] static Shading create_axial(
        Point start, Point end, F&& eval_func,
        const ColorSpace& space = ColorSpace::srgb(),
        bool extend_start = false, bool extend_end = false)
    {
        auto* fn = new std::decay_t<F>(std::forward<F>(eval_func));
        return create_axial(start, end, space,
            [](void* ctx, float t, Color* out) {
                *out = (*static_cast<std::decay_t<F>*>(ctx))(t);
            }, fn,
            [](void* ctx) { delete static_cast<std::decay_t<F>*>(ctx); },
            extend_start, extend_end);
    }

    /// Convenience: radial shading with a C++ callable.
    template<typename F>
    [[nodiscard]] static Shading create_radial(
        Point start_center, float start_radius,
        Point end_center, float end_radius,
        F&& eval_func,
        const ColorSpace& space = ColorSpace::srgb(),
        bool extend_start = false, bool extend_end = false)
    {
        auto* fn = new std::decay_t<F>(std::forward<F>(eval_func));
        return create_radial(start_center, start_radius,
            end_center, end_radius, space,
            [](void* ctx, float t, Color* out) {
                *out = (*static_cast<std::decay_t<F>*>(ctx))(t);
            }, fn,
            [](void* ctx) { delete static_cast<std::decay_t<F>*>(ctx); },
            extend_start, extend_end);
    }

    struct Impl;
private:
    Impl* m_impl = nullptr;
};

/// Immutable font face at a specific pixel size. Provides glyph outlines,
/// metrics, and cmap lookups (codepoint → glyph ID). Text shaping is NOT
/// part of this class — use an external shaper (e.g. HarfBuzz) and feed
/// the resulting glyph IDs + positions to Context::draw_glyphs().
/// Cheap to copy (shared face data).
class KVG_API Font {
public:
    Font();
    ~Font();
    Font(const Font&);
    Font(Font&&) noexcept;
    Font& operator=(const Font&);
    Font& operator=(Font&&) noexcept;

    explicit operator bool() const;

    [[nodiscard]] static Font load(const char* filename, float size, int face_index = 0);
    [[nodiscard]] static Font load(std::span<const std::byte> data, float size, int face_index = 0);
    [[nodiscard]] static Font load(const void* data, size_t length, float size,
                                   int face_index,
                                   void (*release)(void*), void* info);

    /// Create a new Font sharing the same underlying face data but at
    /// a different pixel size. Cheap (no re-parsing of font tables).
    [[nodiscard]] Font with_size(float new_size) const;

    /// Create a new Font with the specified variation axis values applied.
    /// Only meaningful for variable fonts (those with an fvar table).
    /// Returns a null Font if the face is not variable.
    [[nodiscard]] Font with_variations(std::span<const FontVariation> axes) const;

    /// True if this font has an fvar table (is a variable font).
    [[nodiscard]] bool is_variable() const;

    /// Number of variation axes defined in fvar.
    [[nodiscard]] int variation_axis_count() const;

    /// Query the tag, min, default, max for a variation axis by index.
    struct VariationAxis {
        uint32_t tag;
        float    min_value;
        float    default_value;
        float    max_value;
    };
    [[nodiscard]] VariationAxis variation_axis(int index) const;

    // -- Color font support --

    /// Which color font technology this glyph uses, if any.
    /// Returns ColorFontFormat::None for standard monochrome glyphs.
    [[nodiscard]] ColorFontFormat color_format(GlyphID glyph) const;

    /// True if the font has any color glyph data (COLR, sbix, CBDT).
    [[nodiscard]] bool has_color_glyphs() const;

    /// Number of color palettes in CPAL table (0 if no CPAL).
    [[nodiscard]] int palette_count() const;

    /// Retrieve a CPAL palette. Returns palette_size colors.
    /// palette_index 0 is the default palette.
    [[nodiscard]] int palette_size() const;
    [[nodiscard]] Color palette_color(int palette_index, int color_index) const;

    [[nodiscard]] float       size() const;
    [[nodiscard]] FontMetrics metrics() const;
    [[nodiscard]] GlyphMetrics glyph_metrics(GlyphID glyph) const;

    /// Map a Unicode codepoint to a glyph ID via the font's cmap table.
    /// Returns 0 (.notdef) if the codepoint is not covered by this font.
    [[nodiscard]] GlyphID glyph_for_codepoint(Codepoint cp) const;

    /// True if the font has a real glyph (not .notdef) for this codepoint.
    /// Use this to implement font fallback chains.
    [[nodiscard]] bool has_glyph(Codepoint cp) const;

    [[nodiscard]] Path glyph_path(GlyphID glyph) const;
    [[nodiscard]] Path glyph_path(GlyphID glyph, float x, float y) const;

    /// Build a single combined path from an array of pre-positioned glyphs.
    /// Useful for text-along-path, outlined text effects, and hit testing.
    /// Feed it the output of your shaper (glyph IDs + baseline positions).
    [[nodiscard]] Path glyph_run_path(std::span<const GlyphID> glyphs,
                                      std::span<const Point> positions) const;

    /// Same as above but takes GlyphPosition structs (includes per-glyph offsets).
    [[nodiscard]] Path glyph_run_path(std::span<const GlyphPosition> run) const;

    /// Simple left-to-right advance width with pair kerning.
    /// No shaping or bidirectional layout — for accurate measurement
    /// of complex scripts, shape text externally and sum glyph advances.
    [[nodiscard]] float measure(std::string_view utf8) const;
    [[nodiscard]] Rect  text_bounds(std::string_view utf8, Point origin = {}) const;

    /// Pair kerning between two glyphs. Reads both GPOS PairPos
    /// (format 1 and 2) and the legacy kern table. Returns the
    /// horizontal adjustment in scaled units.
    /// Note: this covers simple pair kerning only; complex GPOS
    /// features (contextual, extension lookups) require a shaper.
    [[nodiscard]] float kerning(GlyphID left, GlyphID right) const;

    // -- Raw OpenType table access --
    // For external consumers (HarfBuzz, text shapers, accessibility tools)
    // that need direct access to font tables without copying.

    /// Pointer to the raw data of an OpenType table, or nullptr if not present.
    /// Tag is a 4-byte OpenType tag (e.g. make_otf_tag('G','P','O','S')).
    /// The returned pointer is valid for the lifetime of this Font.
    [[nodiscard]] const void* table_data(uint32_t tag) const;

    /// Size in bytes of an OpenType table, or 0 if not present.
    [[nodiscard]] size_t table_size(uint32_t tag) const;

    struct Impl;
private:
    Impl* m_impl = nullptr;
};

// -- FontCollection (font registry) --

/// Registry of fonts indexed by family name, weight, and style.
/// Use to look up fonts by name at runtime, load directories of fonts,
/// or scan system fonts. Cheap to copy (COW).
class KVG_API FontCollection {
public:
    enum class Weight { Thin, Light, Regular, Medium, Semibold, Bold, Heavy, Black };
    enum class Style  { Normal, Italic, Oblique };

    FontCollection();
    ~FontCollection();
    FontCollection(const FontCollection&);
    FontCollection(FontCollection&&) noexcept;
    FontCollection& operator=(const FontCollection&);
    FontCollection& operator=(FontCollection&&) noexcept;

    explicit operator bool() const;

    void add(std::string_view family, Weight weight, Style style, const Font& font);
    bool add_file(std::string_view family, Weight weight, Style style,
                  const char* filename, float default_size = 12.0f, int face_index = 0);

    [[nodiscard]] Font get(std::string_view family, float size,
                           Weight weight = Weight::Regular,
                           Style style = Style::Normal) const;

    int load_file(const char* filename, float default_size = 12.0f);
    int load_dir(const char* dirname, float default_size = 12.0f);
    int load_system(float default_size = 12.0f);

    void reset();

    struct Impl;
private:
    Impl* m_impl = nullptr;
};

// -- Context (drawing state machine) --

/// Abstract drawing context. Holds the graphics state stack (CTM, clip,
/// colors, line style, blend mode, shadow, font). Path is always external —
/// all draw calls take an explicit const Path&.
///
/// Coordinate system: origin at top-left, X increases rightward, Y increases
/// downward. Units are logical pixels (device-independent). The mapping from
/// logical pixels to physical pixels is controlled by BitmapContext::scale_factor().
/// This differs from CoreGraphics (which uses bottom-left origin inherited from PDF).
///
/// Cannot be instantiated directly. Use BitmapContext for software rendering.
/// The abstract base exists so a future GPU backend can share the same API.
class KVG_API Context {
public:
    virtual ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) noexcept;
    Context& operator=(Context&&) noexcept;

    explicit operator bool() const;

    // -- Graphics state stack --

    /// Push a copy of the current graphics state onto the stack.
    /// Saves: CTM, clip, fill/stroke colors, line style, blend mode,
    /// alpha, shadow, font, text drawing mode, interpolation quality.
    void save_state();

    /// Pop and restore the most recently saved graphics state.
    void restore_state();

    /// RAII guard: calls save_state() on construction, restore_state() on destruction.
    class StateGuard {
    public:
        explicit StateGuard(Context& ctx) : m_ctx(ctx) { m_ctx.save_state(); }
        ~StateGuard() { m_ctx.restore_state(); }
        StateGuard(const StateGuard&) = delete;
        StateGuard& operator=(const StateGuard&) = delete;
    private:
        Context& m_ctx;
    };

    // -- Current transformation matrix (CTM) --
    // The CTM maps user-space coordinates to device-space pixels.
    // All transform operations pre-multiply the existing CTM.

    /// Pre-multiply the CTM by an arbitrary affine transform.
    void concat_ctm(const AffineTransform& t);
    void translate_ctm(float tx, float ty);
    void scale_ctm(float sx, float sy);
    void rotate_ctm(float radians);

    /// Get a copy of the current transformation matrix.
    [[nodiscard]] AffineTransform ctm() const;

    /// Map a point from user space to device (pixel) space.
    [[nodiscard]] Point convert_to_device(Point p) const;

    /// Map a point from device (pixel) space to user space.
    [[nodiscard]] Point convert_to_user(Point p) const;

    // -- Clipping --
    // Clip operations always intersect with the existing clip region.
    // There is no "un-clip" — use save_state/restore_state to widen it.

    /// Clip to the interior of a path.
    void clip_to_path(const Path& path, FillRule rule = FillRule::Winding);
    void clip_to_rect(const Rect& rect);
    void clip_to_rects(std::span<const Rect> rects);

    /// Clip using an image as a coverage mask. The mask's alpha or luminance
    /// channel modulates what passes through, depending on the mode.
    void clip_to_mask(const Image& mask, const Rect& rect, MaskMode mode = MaskMode::Alpha);

    /// Fast path for compositors: set clip from device-space integer rects
    /// directly, bypassing path rasterization.
    void clip_to_region(std::span<const IntRect> rects);

    /// Bounding box of the current clip in user-space coordinates.
    [[nodiscard]] Rect clip_bounding_box() const;

    // -- Fill / stroke colors --

    /// Set both fill and stroke color at once (common case in UI code).
    void set_color(const Color& c);
    void set_color(float r, float g, float b, float a = 1.0f);

    void set_fill_color(const Color& c);
    void set_fill_color(float r, float g, float b, float a = 1.0f);
    void set_stroke_color(const Color& c);
    void set_stroke_color(float r, float g, float b, float a = 1.0f);

    [[nodiscard]] Color fill_color() const;
    [[nodiscard]] Color stroke_color() const;

    // -- Fill / stroke patterns --

    void set_fill_pattern(const Pattern& pattern);
    void set_stroke_pattern(const Pattern& pattern);

    // -- Stroke parameters --

    void set_line_width(float width);
    void set_line_cap(LineCap cap);
    void set_line_join(LineJoin join);
    void set_miter_limit(float limit);
    void set_line_dash(float phase, std::span<const float> lengths);
    void clear_line_dash();

    [[nodiscard]] float    line_width() const;
    [[nodiscard]] LineCap  line_cap() const;
    [[nodiscard]] LineJoin line_join() const;
    [[nodiscard]] float    miter_limit() const;

    /// Retrieve the current dash pattern. Returns the number of lengths
    /// written. If lengths is nullptr, returns the count without writing.
    int line_dash(float* phase, float* lengths, int max_lengths) const;

    // -- Compositing & blending --
    // CompositeOp and BlendMode are independent. CompositeOp controls
    // how alpha channels combine (SrcOver for normal layering), BlendMode
    // controls how colors mix (Normal = no mixing, Multiply = darken, etc.).

    void set_composite_op(CompositeOp op);
    [[nodiscard]] CompositeOp composite_op() const;

    void set_blend_mode(BlendMode mode);
    [[nodiscard]] BlendMode blend_mode() const;

    /// Global opacity multiplier for all drawing operations.
    /// Independent from color alpha — use this for animating fade-in/fade-out
    /// of entire UI elements without touching individual colors.
    void  set_opacity(float opacity);
    [[nodiscard]] float opacity() const;

    /// Per-draw alpha. Applied on top of opacity. Together with opacity,
    /// effective alpha = opacity * alpha * color.a.
    void  set_alpha(float alpha);
    [[nodiscard]] float alpha() const;

    // -- Shadow --

    void set_shadow(const Shadow& shadow);
    void set_shadow(Point offset, float blur, const Color& color);
    void clear_shadow();

    [[nodiscard]] Shadow shadow() const;

    // -- Anti-aliasing --

    void set_should_antialias(bool aa);
    [[nodiscard]] bool should_antialias() const;

    // -- Interpolation quality --

    void set_interpolation_quality(InterpolationQuality q);
    [[nodiscard]] InterpolationQuality interpolation_quality() const;

    // -- Pixel snapping --

    void set_allows_pixel_snapping(bool snap);
    [[nodiscard]] bool allows_pixel_snapping() const;

    // -- Dithering --

    /// Enable ordered dithering for gradient output (default: false).
    /// Reduces visible color banding on 8-bit displays, especially in
    /// dark gradients and smooth transitions.
    void set_dithering(bool enabled);
    [[nodiscard]] bool dithering() const;

    // -- Tone mapping --

    /// Enable HDR tone mapping when drawing images (default: false).
    /// When enabled, draw_image() compresses the dynamic range of source
    /// images with headroom > 1.0 to fit the SDR output range [0, 1].
    /// When disabled, HDR values above 1.0 are clipped.
    void set_tone_mapping(bool enabled);
    [[nodiscard]] bool tone_mapping() const;

    // -- Path drawing --
    // All draw calls take an explicit Path. The context never stores a path.

    /// Fill the interior of path with the current fill color or pattern.
    void fill_path(const Path& path, FillRule rule = FillRule::Winding);

    /// Stroke the outline of path with the current stroke color/pattern
    /// and line style (width, cap, join, dash).
    void stroke_path(const Path& path);

    /// Fill then stroke in one call. Avoids double-coverage artifacts
    /// at fill/stroke boundaries.
    void draw_path(const Path& path, FillRule rule = FillRule::Winding);

    // -- Path drawing with mask filter --
    // These overloads blur the coverage mask before colorization.
    // Use for soft edges, ambient shadows, glow, feathered strokes.

    void fill_path(const Path& path, const MaskFilter& filter,
                   FillRule rule = FillRule::Winding);
    void stroke_path(const Path& path, const MaskFilter& filter);
    void draw_path(const Path& path, const MaskFilter& filter,
                   FillRule rule = FillRule::Winding);

    // -- Rectangle convenience --

    void fill_rect(const Rect& rect);
    void fill_rects(std::span<const Rect> rects);
    void stroke_rect(const Rect& rect);

    /// Paint the rect with transparent black, punching a hole through
    /// existing content (ignores blend mode).
    void clear_rect(const Rect& rect);

    // -- Ellipse convenience --

    void fill_ellipse(const Rect& rect);
    void stroke_ellipse(const Rect& rect);

    // -- Rounded rectangle convenience --

    void fill_round_rect(const Rect& rect, float radius);
    void fill_round_rect(const Rect& rect, const CornerRadii& radii);
    void stroke_round_rect(const Rect& rect, float radius);
    void stroke_round_rect(const Rect& rect, const CornerRadii& radii);

    // -- Gradient drawing --
    // Gradients are drawn into the current clip. Typical pattern:
    //     ctx.save_state();
    //     ctx.clip_to_path(shape);
    //     ctx.draw_linear_gradient(grad, start, end);
    //     ctx.restore_state();
    //
    // The optional gradient_transform applies an additional affine
    // transformation to the gradient coordinate space (SVG gradientTransform).
    // It is composed with the CTM but does not affect the clip or geometry.
    //
    // The gradient's spread mode (Pad / Reflect / Repeat) is stored in the
    // Gradient object and controls how the area outside the defined start/end
    // bounds is filled. GradientDrawingOptions::DrawsBeforeStart / DrawsAfterEnd
    // control whether those outer regions are drawn at all.

    void draw_linear_gradient(const Gradient& gradient,
                              Point start, Point end,
                              const AffineTransform* gradient_transform = nullptr,
                              GradientDrawingOptions options = GradientDrawingOptions::None);

    /// Two-point radial gradient. Interpolates between circle (start_center,
    /// start_radius) and circle (end_center, end_radius).
    void draw_radial_gradient(const Gradient& gradient,
                              Point start_center, float start_radius,
                              Point end_center, float end_radius,
                              const AffineTransform* gradient_transform = nullptr,
                              GradientDrawingOptions options = GradientDrawingOptions::None);

    /// Conic (angular/sweep) gradient centered at a point.
    void draw_conic_gradient(const Gradient& gradient,
                             Point center, float start_angle,
                             const AffineTransform* gradient_transform = nullptr,
                             GradientDrawingOptions options = GradientDrawingOptions::None);

    // -- Shading drawing --
    // Procedural color function. Unlike Gradient (discrete stops),
    // Shading evaluates a callback for each sample point. Use for
    // complex procedural fills that cannot be expressed as stop lists.

    void draw_shading(const Shading& shading);

    // -- Image drawing --
    // Images are drawn respecting the current CTM, clip, blend mode,
    // alpha, shadow, and interpolation quality.

    /// Draw image scaled to fill the destination rect.
    void draw_image(const Image& image, const Rect& dest);

    /// Draw a sub-region of the image into the destination rect.
    void draw_image(const Image& image, const Rect& src_rect, const Rect& dest);

    /// Draw image at its natural pixel size at the given origin.
    void draw_image(const Image& image, Point origin);

    // -- Pattern drawing --

    void draw_pattern(const Pattern& pattern, const Rect& rect);

    // -- Triangle mesh --
    // Rasterizes a triangle mesh with per-vertex attribute interpolation.
    // This is a fundamentally different rasterization path from path fill —
    // uses barycentric interpolation instead of scanline edge coverage.
    //
    // Use cases: mesh gradients, image warp (page curl, rubber band),
    // perspective transforms in the compositor, gradient mesh approximation.
    //
    // If colors is non-empty, vertex colors are interpolated across each
    // triangle. If tex_coords is non-empty, they sample the current fill
    // pattern. When both are provided, the vertex color modulates the
    // texture sample (tinted mesh).

    /// Draw a triangle mesh.
    /// @param mode         How positions are assembled into triangles.
    /// @param positions    Vertex positions in user space.
    /// @param colors       Per-vertex colors, interpolated across triangles.
    ///                     Empty span = use current fill color for all vertices.
    /// @param tex_coords   Per-vertex UV coordinates into the current fill
    ///                     pattern/image. Empty span = no texture mapping.
    /// @param indices      Index buffer. Empty span = draw vertices in order.
    void draw_vertices(VertexMode mode,
                       std::span<const Point> positions,
                       std::span<const Color> colors = {},
                       std::span<const Point> tex_coords = {},
                       std::span<const uint16_t> indices = {});

    // -- Masking --

    void draw_with_mask(const Image& mask, const Rect& mask_rect, MaskMode mode);

    // -- Transparency layers --
    // All drawing between begin/end is rendered onto a temporary buffer,
    // then composited back as a single unit with the given opacity and
    // blend mode. Nesting is allowed.

    void begin_transparency_layer(float opacity = 1.0f);
    void begin_transparency_layer(float opacity, const Rect& bounds);
    void begin_transparency_layer(float opacity, BlendMode blend);
    void end_transparency_layer();

    /// RAII guard: begins a transparency layer on construction,
    /// ends it on destruction.

    class LayerGuard {
    public:
        explicit LayerGuard(Context& ctx, float opacity = 1.0f)
            : m_ctx(ctx) { m_ctx.begin_transparency_layer(opacity); }
        LayerGuard(Context& ctx, float opacity, BlendMode blend)
            : m_ctx(ctx) { m_ctx.begin_transparency_layer(opacity, blend); }
        ~LayerGuard() { m_ctx.end_transparency_layer(); }
        LayerGuard(const LayerGuard&) = delete;
        LayerGuard& operator=(const LayerGuard&) = delete;
    private:
        Context& m_ctx;
    };

    // -- Text (glyph-level) --
    // The primary text API is draw_glyphs(), which takes pre-shaped,
    // pre-positioned glyph arrays (output of an external shaper).
    // draw_text() is a simple convenience for left-to-right Latin text
    // that does no shaping, kerning, or bidirectional layout.

    void set_font(const Font& font);
    [[nodiscard]] Font font() const;

    void set_font_size(float size);
    [[nodiscard]] float font_size() const;

    void set_text_drawing_mode(TextDrawingMode mode);
    [[nodiscard]] TextDrawingMode text_drawing_mode() const;

    void set_text_antialias(TextAntialias mode);
    [[nodiscard]] TextAntialias text_antialias() const;

    /// Set the CPAL palette index used when rendering color font glyphs
    /// (COLR/CPAL). Default is 0. Ignored for monochrome glyphs.
    void set_color_palette_index(int index);
    [[nodiscard]] int color_palette_index() const;

    /// Draw pre-shaped glyphs at given positions. This is the primary
    /// text rendering entry point. Feed it the output of your shaper.
    /// @param font      Font to rasterize glyphs from.
    /// @param glyphs    Glyph IDs (from shaper, not codepoints).
    /// @param positions Baseline origin for each glyph (same length as glyphs).
    void draw_glyphs(const Font& font,
                     std::span<const GlyphID> glyphs,
                     std::span<const Point> positions);

    /// Same as above but takes GlyphPosition structs that include
    /// per-glyph offsets (useful for diacritics and mark positioning).
    void draw_glyphs(const Font& font,
                     std::span<const GlyphPosition> run);

    /// Simple text rendering (no shaping). Walks the UTF-8 string
    /// left-to-right, maps each codepoint to a glyph, advances by
    /// glyph width. Suitable for debug output and simple Latin text.
    void draw_text(std::string_view utf8, Point origin);

    /// Same as above, returns advance width.
    float draw_text(std::string_view utf8, float x, float y);

    [[nodiscard]] Rect  text_bounding_box(std::string_view utf8, Point origin = {}) const;
    [[nodiscard]] float text_advance(std::string_view utf8) const;

    // -- Hit testing --

    /// Test whether a point (in user space) falls inside the filled
    /// region of a path. Both path and point are in user space;
    /// the CTM is not applied. Use convert_to_user() to map
    /// device-space coordinates before calling.
    [[nodiscard]] bool path_contains(const Path& path, Point p,
                                     FillRule rule = FillRule::Winding) const;

    /// Test whether a point falls inside the filled region of a path
    /// after applying an explicit transform to the path. Useful when
    /// the path is defined in a local coordinate space and the point
    /// is in a parent or world space.
    [[nodiscard]] bool path_contains(const Path& path,
                                     const AffineTransform& transform,
                                     Point p,
                                     FillRule rule = FillRule::Winding) const;

    struct Impl;

protected:
    Context();

    /// Subclass injection: adopt a pre-allocated Impl (e.g. BitmapContextImpl).
    /// Passing nullptr is valid — m_impl will be nullptr.
    explicit Context(Impl* impl);

    Impl* m_impl = nullptr;
};

// -- BitmapContext (pixel buffer context) --

/// Drawing context backed by a writable pixel buffer.
/// Non-copyable, move-only.
///
/// Two creation modes:
///   - create(w, h)         — allocates internal buffer
///   - create(data, w, h, stride) — wraps external buffer (framebuffer, shm)
///
/// color_space is the output space — the format of pixels in the buffer.
/// working_color_space is the internal space used during blending, gradient
/// interpolation, and compositing. Default is linear_srgb, which gives
/// physically correct results. Set to srgb() only if you need legacy
/// behaviour or pixel-exact compatibility with non-linearised blending.
///
/// Use to_image() to take a read-only snapshot of the current contents.
class KVG_API BitmapContext final : public Context {
public:
    ~BitmapContext() override;

    BitmapContext(const BitmapContext&) = delete;
    BitmapContext& operator=(const BitmapContext&) = delete;
    BitmapContext(BitmapContext&&) noexcept;
    BitmapContext& operator=(BitmapContext&&) noexcept;

    /// Create with internally allocated buffer.
    [[nodiscard]] static BitmapContext create(
        int width, int height,
        PixelFormat format = PixelFormat::ARGB32_Premultiplied,
        const ColorSpace& space = ColorSpace::srgb(),
        const ColorSpace& working_space = ColorSpace::linear_srgb());

    /// Create wrapping an external buffer (zero-copy). All drawing goes
    /// directly into the provided memory. The caller must keep the buffer
    /// alive for the lifetime of this context.
    [[nodiscard]] static BitmapContext create(
        unsigned char* data, int width, int height, int stride,
        PixelFormat format = PixelFormat::ARGB32_Premultiplied,
        const ColorSpace& space = ColorSpace::srgb(),
        const ColorSpace& working_space = ColorSpace::linear_srgb());

    [[nodiscard]] unsigned char*       data();
    [[nodiscard]] const unsigned char* data() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int stride() const;
    [[nodiscard]] PixelFormat format() const;
    [[nodiscard]] ColorSpace  color_space() const;

    /// The colorspace used internally for blending and compositing.
    /// Defaults to linear_srgb for physically correct results.
    [[nodiscard]] ColorSpace  working_color_space() const;

    [[nodiscard]] float scale_factor() const;
    void  set_scale_factor(float scale);
    [[nodiscard]] float logical_width() const;
    [[nodiscard]] float logical_height() const;

    /// HDR headroom for this surface. Propagated automatically to images
    /// produced by to_image(). Default is 1.0 (SDR).
    [[nodiscard]] float headroom() const;
    void set_headroom(float headroom);

    /// Create a read-only Image snapshot of the current buffer contents.
    /// The returned Image shares pixel data until either side mutates.
    /// Headroom is propagated from this context to the returned Image.
    [[nodiscard]] Image to_image() const;

    /// Snapshot a sub-rectangle of the buffer. Headroom is propagated.
    [[nodiscard]] Image to_image(const IntRect& rect) const;

    /// Direct pixel-to-pixel compositing without path rasterization.
    /// Fast path for window compositors and UI layering.
    ///
    /// NOTE: This method bypasses the current clip region and CTM.
    /// It operates entirely in device (pixel) coordinates.  Use
    /// draw_image() if you need clip- and transform-aware compositing.
    void composite(const Image& src, const IntRect& src_rect,
                   int dst_x, int dst_y,
                   CompositeOp op = CompositeOp::SourceOver,
                   BlendMode blend = BlendMode::Normal,
                   float opacity = 1.0f);

    void composite(const Image& src, int dst_x, int dst_y,
                   CompositeOp op = CompositeOp::SourceOver,
                   BlendMode blend = BlendMode::Normal,
                   float opacity = 1.0f);

    void clear(const Color& color);

    void apply_blur(float radius);
    void apply_color_matrix(const ColorMatrix& cm);

    void convert_to(PixelFormat target);

    bool write(const char* filename, ImageFormat format) const;
    bool write(const char* filename, ImageFormat format, int quality) const;
    bool write(const char* filename) const;

    // Legacy convenience.
    bool write_png(const char* filename) const { return write(filename, ImageFormat::PNG); }
    bool write_jpg(const char* filename, int quality = 90) const { return write(filename, ImageFormat::JPG, quality); }

    struct BitmapImpl;

private:
    BitmapContext();
};

// -- Pixel format conversion --

KVG_API void convert_argb_to_rgba(unsigned char* dst, const unsigned char* src,
                                     int width, int height, int stride);

KVG_API void convert_rgba_to_argb(unsigned char* dst, const unsigned char* src,
                                     int width, int height, int stride);

} // namespace kvg

#endif // KVG_HPP
