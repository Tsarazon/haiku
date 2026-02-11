// plutovg.hpp - 2D vector graphics library
// C++20 idiomatic API

#ifndef PLUTOVG_HPP
#define PLUTOVG_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

// Export/import macros

#if defined(PLUTOVG_BUILD_STATIC)
#define PLUTOVG_EXPORT
#define PLUTOVG_IMPORT
#elif (defined(_WIN32) || defined(__CYGWIN__))
#define PLUTOVG_EXPORT __declspec(dllexport)
#define PLUTOVG_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) && (__GNUC__ >= 4)
#define PLUTOVG_EXPORT __attribute__((__visibility__("default")))
#define PLUTOVG_IMPORT
#else
#define PLUTOVG_EXPORT
#define PLUTOVG_IMPORT
#endif

#ifdef PLUTOVG_BUILD
#define PLUTOVG_API PLUTOVG_EXPORT
#else
#define PLUTOVG_API PLUTOVG_IMPORT
#endif

namespace plutovg {

// ---- Version ----

inline constexpr int version_major = 1;
inline constexpr int version_minor = 4;
inline constexpr int version_micro = 0;

inline constexpr int version_encode(int major, int minor, int micro) {
    return major * 10000 + minor * 100 + micro;
}

inline constexpr int version = version_encode(version_major, version_minor, version_micro);
inline constexpr const char* version_string = "1.4.0";

/// Runtime version of the compiled library.
PLUTOVG_API int runtime_version();

/// Runtime version string of the compiled library.
PLUTOVG_API const char* runtime_version_string();

// ---- Math constants ----

inline constexpr float pi      = 3.14159265358979323846f;
inline constexpr float two_pi  = 6.28318530717958647693f;
inline constexpr float half_pi = 1.57079632679489661923f;
inline constexpr float sqrt2   = 1.41421356237309504880f;
inline constexpr float kappa   = 0.55228474983079339840f; ///< Bezier approximation of a quarter circle.

inline constexpr float deg2rad(float x) { return x * (pi / 180.0f); }
inline constexpr float rad2deg(float x) { return x * (180.0f / pi); }

// ---- Unicode ----

/// Unicode code point.
using Codepoint = uint32_t;

// ---- Forward declarations ----

class Path;
class Surface;
class Paint;
class FontFace;
class FontFaceCache;
class Canvas;

// ---- Value types ----

/// 2D point.
struct Point {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Point() noexcept = default;
    constexpr Point(float x, float y) noexcept : x(x), y(y) {}

    constexpr Point operator+(Point o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Point operator-(Point o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Point operator*(float s) const noexcept { return {x * s, y * s}; }
    constexpr auto operator<=>(const Point&) const noexcept = default;
};

/// Axis-aligned rectangle (float).
struct Rect {
    float x = 0.0f; ///< Top-left x.
    float y = 0.0f; ///< Top-left y.
    float w = 0.0f; ///< Width.
    float h = 0.0f; ///< Height.

    constexpr Rect() noexcept = default;
    constexpr Rect(float x, float y, float w, float h) noexcept : x(x), y(y), w(w), h(h) {}

    constexpr float right()  const noexcept { return x + w; }
    constexpr float bottom() const noexcept { return y + h; }
    constexpr bool  empty()  const noexcept { return w <= 0 || h <= 0; }

    /// Check if a point lies inside the rectangle.
    constexpr bool contains(float px, float py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    /// Check if two rectangles overlap.
    constexpr bool intersects(const Rect& o) const noexcept {
        return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
    }

    /// Compute the intersection of two rectangles. Returns empty rect if no overlap.
    constexpr Rect intersected(const Rect& o) const noexcept {
        float l = x > o.x ? x : o.x;
        float t = y > o.y ? y : o.y;
        float r = right() < o.right() ? right() : o.right();
        float b = bottom() < o.bottom() ? bottom() : o.bottom();
        return (r > l && b > t) ? Rect{l, t, r - l, b - t} : Rect{};
    }

    /// Compute the bounding union of two rectangles.
    constexpr Rect united(const Rect& o) const noexcept {
        if (empty()) return o;
        if (o.empty()) return *this;
        float l = x < o.x ? x : o.x;
        float t = y < o.y ? y : o.y;
        float r = right() > o.right() ? right() : o.right();
        float b = bottom() > o.bottom() ? bottom() : o.bottom();
        return {l, t, r - l, b - t};
    }

    constexpr auto operator<=>(const Rect&) const noexcept = default;
};

/// Axis-aligned rectangle (integer, for pixel-level operations).
struct IntRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr IntRect() noexcept = default;
    constexpr IntRect(int x, int y, int w, int h) noexcept : x(x), y(y), w(w), h(h) {}

    constexpr int  right()  const noexcept { return x + w; }
    constexpr int  bottom() const noexcept { return y + h; }
    constexpr bool empty()  const noexcept { return w <= 0 || h <= 0; }

    /// Check if a point lies inside the rectangle.
    constexpr bool contains(int px, int py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    /// Check if two rectangles overlap.
    constexpr bool intersects(const IntRect& o) const noexcept {
        return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
    }

    /// Compute the intersection of two rectangles. Returns empty rect if no overlap.
    constexpr IntRect intersected(const IntRect& o) const noexcept {
        int l = x > o.x ? x : o.x;
        int t = y > o.y ? y : o.y;
        int r = right() < o.right() ? right() : o.right();
        int b = bottom() < o.bottom() ? bottom() : o.bottom();
        return (r > l && b > t) ? IntRect{l, t, r - l, b - t} : IntRect{};
    }

    /// Compute the bounding union of two rectangles.
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

/// Corner radii for rounded rectangles (per-corner).
///
/// When all four radii are equal, use the single-argument constructor.
/// For iOS-style per-corner rounding, specify each corner individually.
struct CornerRadii {
    float top_left     = 0.0f;
    float top_right    = 0.0f;
    float bottom_right = 0.0f;
    float bottom_left  = 0.0f;

    constexpr CornerRadii() noexcept = default;

    /// Uniform radius for all corners.
    constexpr explicit CornerRadii(float r) noexcept
        : top_left(r), top_right(r), bottom_right(r), bottom_left(r) {}

    /// Per-corner radii (TL, TR, BR, BL).
    constexpr CornerRadii(float tl, float tr, float br, float bl) noexcept
        : top_left(tl), top_right(tr), bottom_right(br), bottom_left(bl) {}

    /// True if all four corners have the same radius.
    constexpr bool is_uniform() const noexcept {
        return top_left == top_right && top_right == bottom_right && bottom_right == bottom_left;
    }

    /// True if all corners are zero (sharp rectangle).
    constexpr bool is_zero() const noexcept {
        return top_left == 0 && top_right == 0 && bottom_right == 0 && bottom_left == 0;
    }
};

/// 2D affine transformation matrix.
///
/// Layout: | a  c  e |
///         | b  d  f |
///         | 0  0  1 |
///
/// Hot-path operations (map, multiply) are inline constexpr.
struct Matrix {
    float a = 1.0f; ///< Horizontal scale.
    float b = 0.0f; ///< Vertical shear.
    float c = 0.0f; ///< Horizontal shear.
    float d = 1.0f; ///< Vertical scale.
    float e = 0.0f; ///< Horizontal translation.
    float f = 0.0f; ///< Vertical translation.

    constexpr Matrix() noexcept = default;
    constexpr Matrix(float a, float b, float c, float d, float e, float f) noexcept
        : a(a), b(b), c(c), d(d), e(e), f(f) {}

    // -- Static factories --

    static constexpr Matrix identity() noexcept { return {}; }

    static constexpr Matrix translated(float tx, float ty) noexcept {
        return {1, 0, 0, 1, tx, ty};
    }

    static constexpr Matrix scaled(float sx, float sy) noexcept {
        return {sx, 0, 0, sy, 0, 0};
    }

    /// Rotation matrix. Not constexpr (requires std::cos/sin at runtime).
    PLUTOVG_API static Matrix rotated(float radians);

    /// Shear matrix.
    PLUTOVG_API static Matrix sheared(float shx, float shy);

    /// Parse an SVG transform attribute string.
    /// @return The parsed matrix, or std::nullopt on failure.
    PLUTOVG_API static std::optional<Matrix> parse(const char* data, int length = -1);

    // -- In-place mutators --

    /// Pre-multiply by a translation.
    PLUTOVG_API Matrix& translate(float tx, float ty);

    /// Pre-multiply by a scale.
    PLUTOVG_API Matrix& scale(float sx, float sy);

    /// Pre-multiply by a rotation.
    PLUTOVG_API Matrix& rotate(float radians);

    /// Pre-multiply by a shear.
    PLUTOVG_API Matrix& shear(float shx, float shy);

    // -- Queries (hot path, inline) --

    /// Determinant of the matrix.
    constexpr float determinant() const noexcept { return a * d - b * c; }

    /// Whether the matrix is invertible.
    constexpr bool is_invertible() const noexcept { return determinant() != 0.0f; }

    /// Inverse of the matrix. Returns std::nullopt if singular.
    [[nodiscard]] constexpr std::optional<Matrix> inverted() const noexcept {
        float det = determinant();
        if (det == 0.0f) return std::nullopt;
        float inv_det = 1.0f / det;
        return Matrix{
             d * inv_det, -b * inv_det,
            -c * inv_det,  a * inv_det,
            (c * f - d * e) * inv_det,
            (b * e - a * f) * inv_det
        };
    }

    /// Transform a single point (hot path).
    [[nodiscard]] constexpr Point map(Point p) const noexcept {
        return {a * p.x + c * p.y + e,
                b * p.x + d * p.y + f};
    }

    /// Transform a single point given as (x, y) (hot path).
    constexpr void map(float x, float y, float& ox, float& oy) const noexcept {
        ox = a * x + c * y + e;
        oy = b * x + d * y + f;
    }

    /// Transform an array of points in-place or between buffers.
    /// `src` and `dst` may alias. `count` must be > 0.
    PLUTOVG_API void map_points(const Point* src, Point* dst, int count) const;

    /// Compute the axis-aligned bounding box of the transformed rectangle.
    [[nodiscard]] PLUTOVG_API Rect map_rect(const Rect& src) const;

    // -- Operators (hot path, inline) --

    /// Matrix multiplication: this * rhs.
    [[nodiscard]] constexpr Matrix operator*(const Matrix& rhs) const noexcept {
        return {
            a * rhs.a + c * rhs.b,
            b * rhs.a + d * rhs.b,
            a * rhs.c + c * rhs.d,
            b * rhs.c + d * rhs.d,
            a * rhs.e + c * rhs.f + e,
            b * rhs.e + d * rhs.f + f
        };
    }

    Matrix& operator*=(const Matrix& rhs) noexcept { *this = *this * rhs; return *this; }
    constexpr auto operator<=>(const Matrix&) const noexcept = default;
};

// ---- Enums ----

/// Path commands.
enum class PathCommand {
    MoveTo,  ///< Move the current point (1 point).
    LineTo,  ///< Straight line segment (1 point).
    CubicTo, ///< Cubic Bezier curve (3 points).
    Close    ///< Close subpath with a line to start (1 point).
};

/// Text encodings for text-to-codepoint conversion.
enum class TextEncoding {
    UTF8,
    UTF16,
    UTF32
};

/// Texture tiling mode.
enum class TextureType {
    Plain,
    Tiled
};

/// Gradient spread mode beyond the defined range.
enum class SpreadMethod {
    Pad,     ///< Clamp to edge colors.
    Reflect, ///< Mirror the gradient.
    Repeat   ///< Tile the gradient.
};

/// Fill rule for path filling.
enum class FillRule {
    NonZero,
    EvenOdd
};

/// Porter-Duff compositing operators.
enum class Operator {
    Clear,
    Src,
    Dst,
    SrcOver,
    DstOver,
    SrcIn,
    DstIn,
    SrcOut,
    DstOut,
    SrcAtop,
    DstAtop,
    Xor
};

/// Extended blend modes (applied after compositing).
///
/// These correspond to the SVG/CSS blend modes and are applied
/// per-pixel on top of the Porter-Duff compositing operator.
enum class BlendMode {
    Normal,     ///< No blending (default).
    Multiply,   ///< Darkens by multiplying colors.
    Screen,     ///< Lightens by inverting, multiplying, inverting.
    Overlay,    ///< Combines Multiply and Screen based on base color.
    Darken,     ///< Keeps the darker of the two colors.
    Lighten,    ///< Keeps the lighter of the two colors.
    ColorDodge, ///< Brightens base color to reflect source.
    ColorBurn,  ///< Darkens base color to reflect source.
    HardLight,  ///< Like Overlay but based on source color.
    SoftLight,  ///< Softer version of HardLight.
    Difference, ///< Absolute difference between colors.
    Exclusion,  ///< Similar to Difference but lower contrast.
    Hue,        ///< Source hue, destination saturation and luminosity.
    Saturation, ///< Source saturation, destination hue and luminosity.
    Color,      ///< Source hue and saturation, destination luminosity.
    Luminosity  ///< Source luminosity, destination hue and saturation.
};

/// Line cap style for open subpaths.
enum class LineCap {
    Butt,
    Round,
    Square
};

/// Line join style at path corners.
enum class LineJoin {
    Miter,
    Round,
    Bevel
};

/// Mask mode for alpha/luminance masking operations.
enum class MaskMode {
    Alpha,      ///< Use mask surface alpha channel as coverage.
    InvAlpha,   ///< Use inverted mask alpha (255 - alpha) as coverage.
    Luma,       ///< Use mask luminance (0.2126R + 0.7152G + 0.0722B) as coverage.
    InvLuma     ///< Use inverted mask luminance as coverage.
};

/// Color interpolation space for gradients.
enum class ColorInterpolation {
    SRGB,       ///< Interpolate in sRGB space (legacy, default).
    LinearRGB   ///< Interpolate in linear RGB space (physically correct).
};

/// Pixel format for surface data.
///
/// The internal rendering pipeline always works in ARGB32_Premultiplied.
/// Other formats are supported for interop with external systems (Wayland,
/// framebuffers, image loaders). Conversion happens at blit/composite boundaries.
enum class PixelFormat : uint8_t {
    ARGB32_Premultiplied,  ///< 0xAARRGGBB, premultiplied alpha. Native PlutoVG format.
    BGRA32_Premultiplied,  ///< 0xBBGGRRAA memory order, premultiplied. Wayland/Vulkan native.
    RGBA32,                ///< 0xRRGGBBAA memory order, straight alpha. PNG exchange format.
    BGRA32,                ///< 0xBBGGRRAA memory order, straight alpha. Windows DIB.
    A8                     ///< 8-bit alpha/coverage. Glyph bitmaps, masks.
};

/// Compile-time pixel format descriptor.
struct PixelFormatInfo {
    uint8_t bpp;           ///< Bytes per pixel.
    uint8_t r_shift;       ///< Bit offset for red (0 for A8).
    uint8_t g_shift;       ///< Bit offset for green (0 for A8).
    uint8_t b_shift;       ///< Bit offset for blue (0 for A8).
    uint8_t a_shift;       ///< Bit offset for alpha.
    bool    premultiplied; ///< Whether RGB is premultiplied by alpha.
    bool    has_color;     ///< False for A8 (alpha-only).
};

/// Get format descriptor for a pixel format.
inline constexpr PixelFormatInfo pixel_format_info(PixelFormat fmt) {
    switch (fmt) {
    case PixelFormat::ARGB32_Premultiplied:
        return {4, 16, 8, 0, 24, true, true};
    case PixelFormat::BGRA32_Premultiplied:
        return {4, 8, 16, 24, 0, true, true};
    case PixelFormat::RGBA32:
        return {4, 24, 16, 8, 0, false, true};
    case PixelFormat::BGRA32:
        return {4, 8, 16, 24, 0, false, true};
    case PixelFormat::A8:
        return {1, 0, 0, 0, 0, false, false};
    }
    return {4, 16, 8, 0, 24, true, true};
}

/// Minimum stride (bytes per row) for a given width and format.
inline constexpr int min_stride(int width, PixelFormat fmt) {
    return width * pixel_format_info(fmt).bpp;
}

// ---- Color ----

/// RGBA color with float components in [0, 1].
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() noexcept = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) noexcept : r(r), g(g), b(b), a(a) {}

    // -- Named colors --

    static constexpr Color black()       noexcept { return {0, 0, 0}; }
    static constexpr Color white()       noexcept { return {1, 1, 1}; }
    static constexpr Color red()         noexcept { return {1, 0, 0}; }
    static constexpr Color green()       noexcept { return {0, 1, 0}; }
    static constexpr Color blue()        noexcept { return {0, 0, 1}; }
    static constexpr Color yellow()      noexcept { return {1, 1, 0}; }
    static constexpr Color cyan()        noexcept { return {0, 1, 1}; }
    static constexpr Color magenta()     noexcept { return {1, 0, 1}; }
    static constexpr Color transparent() noexcept { return {0, 0, 0, 0}; }

    // -- Factories from integer components --

    /// Construct from 8-bit RGB [0..255]. Values are clamped.
    static constexpr Color from_rgb8(int r, int g, int b) noexcept {
        return {clamp_byte(r) / 255.0f, clamp_byte(g) / 255.0f, clamp_byte(b) / 255.0f, 1.0f};
    }

    /// Construct from 8-bit RGBA [0..255]. Values are clamped.
    static constexpr Color from_rgba8(int r, int g, int b, int a) noexcept {
        return {clamp_byte(r) / 255.0f, clamp_byte(g) / 255.0f, clamp_byte(b) / 255.0f, clamp_byte(a) / 255.0f};
    }

    /// Construct from packed 0xRRGGBBAA.
    static constexpr Color from_rgba32(uint32_t v) noexcept {
        return from_rgba8((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }

    /// Construct from packed 0xAARRGGBB.
    static constexpr Color from_argb32(uint32_t v) noexcept {
        return from_rgba8((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, (v >> 24) & 0xFF);
    }

    /// Construct from HSL (hue 0-360, saturation/lightness 0-1).
    PLUTOVG_API static Color from_hsl(float h, float s, float l);

    /// Construct from HSLA (hue 0-360, saturation/lightness/alpha 0-1).
    PLUTOVG_API static Color from_hsla(float h, float s, float l, float a);

    /// Parse a CSS color string (e.g. "#ff0000", "rgb(255,0,0)", "red").
    /// @return Parsed color and number of characters consumed, or std::nullopt on failure.
    PLUTOVG_API static std::optional<std::pair<Color, int>> parse(const char* data, int length = -1);

    // -- Conversion to packed integers --

    /// Pack to 0xRRGGBBAA.
    [[nodiscard]] constexpr uint32_t to_rgba32() const noexcept {
        return (uint32_t(clamp01(r)) << 24) | (uint32_t(clamp01(g)) << 16)
             | (uint32_t(clamp01(b)) << 8)  |  uint32_t(clamp01(a));
    }

    /// Pack to 0xAARRGGBB.
    [[nodiscard]] constexpr uint32_t to_argb32() const noexcept {
        return (uint32_t(clamp01(a)) << 24) | (uint32_t(clamp01(r)) << 16)
             | (uint32_t(clamp01(g)) << 8)  |  uint32_t(clamp01(b));
    }

    constexpr auto operator<=>(const Color&) const noexcept = default;

private:
    /// Clamp float [0,1] to uint8_t [0,255].
    static constexpr uint8_t clamp01(float v) noexcept {
        if (v <= 0.0f) return 0;
        if (v >= 1.0f) return 255;
        return static_cast<uint8_t>(v * 255.0f + 0.5f);
    }

    /// Clamp integer to [0, 255] and return as float.
    static constexpr float clamp_byte(int v) noexcept {
        return static_cast<float>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
};

/// Gradient color stop.
struct GradientStop {
    float offset = 0.0f; ///< Position within the gradient [0, 1].
    Color color;

    constexpr GradientStop() noexcept = default;
    constexpr GradientStop(float offset, const Color& color) noexcept : offset(offset), color(color) {}
};

/// Shadow parameters for drawing operations.
///
/// Shadows are drawn beneath the fill/stroke content. The shadow is produced
/// by rendering the same shape offset by (offset_x, offset_y), blurred by
/// `blur` radius, and filled with `color`. A transparent color disables the shadow.
struct Shadow {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float blur      = 0.0f;
    Color color     = {0.0f, 0.0f, 0.0f, 0.0f};

    constexpr Shadow() noexcept = default;
    constexpr Shadow(float ox, float oy, float blur, const Color& color) noexcept
        : offset_x(ox), offset_y(oy), blur(blur), color(color) {}

    /// True if the shadow would have no visible effect.
    constexpr bool is_none() const noexcept {
        return color.a <= 0.0f || (blur <= 0.0f && offset_x == 0.0f && offset_y == 0.0f);
    }
};

/// 4×5 color transformation matrix.
/// Transforms [R,G,B,A,1] → [R',G',B',A'] in straight (non-premultiplied) color space.
/// Layout: R' = m[0]*R + m[1]*G + m[2]*B  + m[3]*A + m[4]
///         G' = m[5]*R + m[6]*G + m[7]*B  + m[8]*A + m[9]
///         B' = m[10]*R + m[11]*G + m[12]*B + m[13]*A + m[14]
///         A' = m[15]*R + m[16]*G + m[17]*B + m[18]*A + m[19]
/// R, G, B, A and bias terms are in [0, 1].
struct ColorMatrix {
    float m[20] = {
        1,0,0,0,0,  // R' = R
        0,1,0,0,0,  // G' = G
        0,0,1,0,0,  // B' = B
        0,0,0,1,0   // A' = A
    };

    static constexpr ColorMatrix identity() { return {}; }

    /// Grayscale using BT.709 luminance weights.
    PLUTOVG_API static ColorMatrix grayscale();

    /// Sepia tone.
    PLUTOVG_API static ColorMatrix sepia();

    /// Saturate: 0 = grayscale, 1 = identity, >1 = oversaturated.
    PLUTOVG_API static ColorMatrix saturate(float amount);

    /// Brightness: 0 = black, 1 = identity, >1 = brighter.
    PLUTOVG_API static ColorMatrix brightness(float amount);

    /// Contrast: 0 = flat gray, 1 = identity, >1 = increased.
    PLUTOVG_API static ColorMatrix contrast(float amount);

    /// Hue rotation in radians.
    PLUTOVG_API static ColorMatrix hue_rotate(float radians);

    /// Invert colors.
    PLUTOVG_API static ColorMatrix invert();

    /// Multiply two matrices (apply this, then other).
    [[nodiscard]] constexpr ColorMatrix operator*(const ColorMatrix& other) const {
        ColorMatrix result;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 5; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += m[row * 5 + k] * other.m[k * 5 + col];
                if (col == 4)
                    sum += m[row * 5 + 4]; // bias passthrough
                result.m[row * 5 + col] = sum;
            }
        }
        return result;
    }
};

// ---- Font metrics (returned as value) ----

/// Metrics of a font face at a given size.
struct FontMetrics {
    float ascent    = 0.0f;
    float descent   = 0.0f;
    float line_gap  = 0.0f;
    Rect  extents;
};

/// Metrics of a single glyph.
struct GlyphMetrics {
    float advance_width    = 0.0f;
    float left_side_bearing = 0.0f;
    Rect  extents;
};

// ---- Path ----

/// Internal element in a path's storage.
/// A command header followed by its associated points.
union PathElement {
    struct {
        PathCommand command;
        int length; ///< Total elements (header + points).
    } header;
    Point point;

    constexpr PathElement() noexcept : header{} {}
};

static_assert(sizeof(PathElement) == sizeof(Point),
    "PathElement union must match Point size for correct path element iteration");

/// Segment yielded when iterating over a path.
/// @note `points` is a pointer into the path's internal buffer. The segment
///       is invalidated if the source path is mutated or destroyed.
struct PathSegment {
    PathCommand    command;
    const Point*   points;  ///< Pointer into the path's internal buffer.
    int            npoints;
};

/// 2D path for drawing operations.
///
/// Copy-on-write (COW) value semantics. Copies are cheap (shared data),
/// but any mutating operation (move_to, line_to, reset, transform, etc.)
/// detaches from shared storage first, so mutations never affect other copies.
/// Use clone() to force a deep copy without mutation.
class PLUTOVG_API Path {
public:
    Path();
    ~Path();
    Path(const Path& other);
    Path(Path&& other) noexcept;
    Path& operator=(const Path& other);
    Path& operator=(Path&& other) noexcept;

    /// True if the path is valid (has internal storage).
    explicit operator bool() const;

    /// Deep copy.
    [[nodiscard]] Path clone() const;

    /// Deep copy with cubic Bezier curves flattened to line segments.
    [[nodiscard]] Path clone_flatten() const;

    /// Deep copy with a dash pattern applied.
    [[nodiscard]] Path clone_dashed(float offset, std::span<const float> dashes) const;

    /// Deep copy trimmed to a sub-range of the path's total arc length.
    /// @param begin Normalized start position [0, 1].
    /// @param end   Normalized end position [0, 1].
    [[nodiscard]] Path trimmed(float begin, float end) const;

    /// Convert this path's stroke to a filled path (stroke-to-fill conversion).
    /// Uses identity matrix — stroke width is in user-space units.
    /// @param width      Stroke width.
    /// @param cap        Line cap style (default: Butt).
    /// @param join       Line join style (default: Miter).
    /// @param miter_limit Miter limit (default: 10).
    [[nodiscard]] PLUTOVG_API Path stroked(float width,
                                           LineCap cap = LineCap::Butt,
                                           LineJoin join = LineJoin::Miter,
                                           float miter_limit = 10.0f) const;

    /// Convert this path's stroke to a filled path, with dash pattern.
    /// @param width      Stroke width.
    /// @param cap        Line cap style.
    /// @param join       Line join style.
    /// @param miter_limit Miter limit.
    /// @param dash_offset Dash pattern offset.
    /// @param dashes     Dash pattern array.
    [[nodiscard]] PLUTOVG_API Path stroked(float width,
                                           LineCap cap,
                                           LineJoin join,
                                           float miter_limit,
                                           float dash_offset,
                                           std::span<const float> dashes) const;

    // -- Boolean operations (via polyclip) --
    // Paths are flattened (cubics → line segments) before clipping.
    // The result is always a flattened path.

    /// Union of this path and @p other.
    [[nodiscard]] PLUTOVG_API Path united(const Path& other,
                                          FillRule rule = FillRule::NonZero) const;

    /// Intersection of this path and @p other.
    [[nodiscard]] PLUTOVG_API Path intersected(const Path& other,
                                               FillRule rule = FillRule::NonZero) const;

    /// Subtract @p other from this path (this minus other).
    [[nodiscard]] PLUTOVG_API Path subtracted(const Path& other,
                                              FillRule rule = FillRule::NonZero) const;

    /// Symmetric difference (XOR) of this path and @p other.
    [[nodiscard]] PLUTOVG_API Path xored(const Path& other,
                                         FillRule rule = FillRule::NonZero) const;

    /// Construct a path from raw command and point arrays (zero-copy import).
    /// @param cmds   Array of path commands.
    /// @param cmd_count Number of commands.
    /// @param pts    Array of points referenced by the commands.
    /// @param pt_count Number of points.
    [[nodiscard]] static Path from_raw(const PathCommand* cmds, int cmd_count,
                                       const Point* pts, int pt_count);

    // -- Building --

    /// Move current point (SVG M).
    void move_to(float x, float y);

    /// Straight line to point (SVG L).
    void line_to(float x, float y);

    /// Quadratic Bezier (SVG Q). Internally converted to cubic.
    void quad_to(float x1, float y1, float x2, float y2);

    /// Cubic Bezier (SVG C).
    void cubic_to(float x1, float y1, float x2, float y2, float x3, float y3);

    /// Elliptical arc (SVG A).
    /// @param angle Rotation of the ellipse in radians.
    void arc_to(float rx, float ry, float angle, bool large_arc_flag, bool sweep_flag, float x, float y);

    /// Close the current subpath (SVG Z).
    void close();

    // -- Shapes --

    void add_rect(float x, float y, float w, float h);
    void add_round_rect(float x, float y, float w, float h, float rx, float ry);

    /// Add a rounded rectangle with per-corner radii.
    void add_round_rect(float x, float y, float w, float h, const CornerRadii& radii);

    void add_ellipse(float cx, float cy, float rx, float ry);
    void add_circle(float cx, float cy, float r);
    void add_arc(float cx, float cy, float r, float a0, float a1, bool ccw);

    /// Append another path, optionally transformed.
    void add_path(const Path& source, const Matrix* matrix = nullptr);

    // -- Transformation --

    /// Apply a transformation to all points in the path.
    void transform(const Matrix& matrix);

    // -- Queries --

    /// Current pen position.
    [[nodiscard]] Point current_point() const noexcept;

    /// Number of raw path elements (headers + points).
    [[nodiscard]] int element_count() const noexcept;

    /// Direct access to the raw element array.
    [[nodiscard]] const PathElement* elements() const noexcept;

    /// Reserve storage for at least `count` elements.
    void reserve(int count);

    /// Clear all path data.
    void reset();

    /// Compute bounding box and total arc length.
    /// @param tight If true, compute a tight (more expensive) bounding box.
    /// @return Total arc length.
    float extents(Rect& rect, bool tight) const;

    /// Total arc length of the path.
    [[nodiscard]] float length() const;

    /// Parse SVG path data string (e.g. "M10 20 L30 40 Z").
    bool parse(const char* data, int length = -1);

    /// Parse SVG path data from a string_view.
    bool parse(std::string_view svg) { return parse(svg.data(), static_cast<int>(svg.size())); }

    // -- Traversal --

    /// Raw callback type for traversal. No heap allocation, no virtual dispatch.
    using TraverseFunc = void(*)(void* closure, PathCommand cmd, const Point* points, int npoints);

    /// Traverse the path, calling `func` for each command.
    void traverse_raw(TraverseFunc func, void* closure) const;

    /// Traverse with curves flattened to line segments.
    void traverse_flatten_raw(TraverseFunc func, void* closure) const;

    /// Traverse with a dash pattern applied.
    void traverse_dashed_raw(float offset, const float* dashes, int ndashes,
                             TraverseFunc func, void* closure) const;

    /// Type-safe traversal (hot path friendly: the compiler can inline the callback).
    /// Usage: path.traverse([](PathCommand cmd, const Point* pts, int n) { ... });
    template<typename F>
    void traverse(F&& func) const {
        auto fn = std::forward<F>(func);
        traverse_raw([](void* ctx, PathCommand cmd, const Point* pts, int n) {
            (*static_cast<std::decay_t<F>*>(ctx))(cmd, pts, n);
        }, static_cast<void*>(&fn));
    }

    /// Type-safe flattened traversal.
    template<typename F>
    void traverse_flatten(F&& func) const {
        auto fn = std::forward<F>(func);
        traverse_flatten_raw([](void* ctx, PathCommand cmd, const Point* pts, int n) {
            (*static_cast<std::decay_t<F>*>(ctx))(cmd, pts, n);
        }, static_cast<void*>(&fn));
    }

    /// Type-safe dashed traversal.
    template<typename F>
    void traverse_dashed(float offset, std::span<const float> dashes, F&& func) const {
        auto fn = std::forward<F>(func);
        traverse_dashed_raw(offset, dashes.data(), static_cast<int>(dashes.size()),
            [](void* ctx, PathCommand cmd, const Point* pts, int n) {
                (*static_cast<std::decay_t<F>*>(ctx))(cmd, pts, n);
            }, static_cast<void*>(&fn));
    }

    // -- Range-based iteration --

    /// Forward iterator over path segments.
    /// @warning Invalidated by any mutation of the source path (same as std::vector).
    class Iterator {
    public:
        Iterator() = default;
        Iterator(const PathElement* elements, int size, int index)
            : m_elements(elements), m_size(size), m_index(index) {}

        PathSegment operator*() const;
        Iterator& operator++();
        bool operator!=(const Iterator& o) const { return m_index != o.m_index; }
        bool operator==(const Iterator& o) const { return m_index == o.m_index; }

    private:
        const PathElement* m_elements = nullptr;
        int m_size  = 0;
        int m_index = 0;
    };

    [[nodiscard]] Iterator begin() const;
    [[nodiscard]] Iterator end() const;

    // Pimpl (definition in plutovg-private.hpp)
    struct Impl;

private:
    Impl* m_impl = nullptr;
};

// ---- Text iteration ----

/// Iterator for decoding code points from encoded text.
class PLUTOVG_API TextIterator {
public:
    TextIterator(const void* text, int length, TextEncoding encoding);

    /// Convenience: iterate UTF-8 text.
    explicit TextIterator(std::string_view text)
        : TextIterator(text.data(), static_cast<int>(text.size()), TextEncoding::UTF8) {}

    /// True if more code points are available.
    [[nodiscard]] bool has_next() const;

    /// Decode the next code point and advance.
    Codepoint next();

private:
    const void* m_text;
    int m_length;
    TextEncoding m_encoding;
    int m_index = 0;
};

// ---- FontFace ----

/// A loaded font face (TrueType / OpenType).
///
/// Immutable after creation. Copies share the underlying data (cheap).
/// Thread-safe for concurrent read access.
class PLUTOVG_API FontFace {
public:
    FontFace();
    ~FontFace();
    FontFace(const FontFace& other);
    FontFace(FontFace&& other) noexcept;
    FontFace& operator=(const FontFace& other);
    FontFace& operator=(FontFace&& other) noexcept;

    explicit operator bool() const;

    /// Load a font from a file.
    /// @param ttcindex Face index within a TrueType Collection (0 for most fonts).
    [[nodiscard]] static FontFace load_from_file(const char* filename, int ttcindex = 0);

    /// Load a font from a memory buffer. Data is copied internally.
    [[nodiscard]] static FontFace load_from_data(std::span<const std::byte> data, int ttcindex = 0);

    /// Load a font from a heap-allocated buffer (zero-copy, ownership transfer).
    /// The buffer is freed with `delete[]` when the font face is destroyed.
    [[nodiscard]] static FontFace load_from_data(std::unique_ptr<std::byte[]> data,
                                                  size_t length, int ttcindex = 0);

    /// Load a font from an externally managed buffer (zero-copy).
    ///
    /// The caller provides a release callback that will be invoked exactly once
    /// when the font data is no longer needed. Pass `nullptr` for `release`
    /// if the data has static lifetime (e.g. embedded resources).
    ///
    /// @param data     Pointer to the font data. Must remain valid until `release` is called.
    /// @param length   Size of the font data in bytes.
    /// @param ttcindex Face index within a TrueType Collection (0 for most fonts).
    /// @param release  Cleanup function, or `nullptr` for static/unmanaged data.
    /// @param context  Opaque pointer passed to `release`.
    [[nodiscard]] static FontFace load_from_data(const void* data, size_t length,
                                                  int ttcindex,
                                                  void (*release)(void* context),
                                                  void* context);

    /// Font metrics at a given pixel size.
    [[nodiscard]] FontMetrics metrics(float size) const;

    /// Glyph metrics at a given pixel size.
    [[nodiscard]] GlyphMetrics glyph_metrics(float size, Codepoint codepoint) const;

    /// Append the glyph outline to `path` at the given position and size.
    /// @return The advance width of the glyph.
    float get_glyph_path(float size, float x, float y, Codepoint codepoint, Path& path) const;

    /// Traverse the glyph outline calling `func` for each command.
    template<typename F>
    float traverse_glyph_path(float size, float x, float y, Codepoint codepoint, F&& func) const {
        auto fn = std::forward<F>(func);
        return traverse_glyph_path_raw(size, x, y, codepoint,
            [](void* ctx, PathCommand cmd, const Point* pts, int n) {
                (*static_cast<std::decay_t<F>*>(ctx))(cmd, pts, n);
            }, static_cast<void*>(&fn));
    }

    /// Raw glyph path traversal.
    float traverse_glyph_path_raw(float size, float x, float y, Codepoint codepoint,
                                   Path::TraverseFunc func, void* closure) const;

    /// Compute the bounding box and advance width of a text string.
    float text_extents(float size, const void* text, int length, TextEncoding encoding, Rect* extents) const;

    /// Convenience: text extents for UTF-8.
    float text_extents(float size, std::string_view text, Rect* extents = nullptr) const {
        return text_extents(size, text.data(), static_cast<int>(text.size()), TextEncoding::UTF8, extents);
    }

    struct Impl;

private:
    Impl* m_impl = nullptr;
};

// ---- FontFaceCache ----

/// Cache of loaded font faces, indexed by family name and style.
///
/// Copy-on-write (COW) value semantics. Copies are cheap (shared data).
/// Mutating operations (add, reset, load_*) detach from shared storage first.
class PLUTOVG_API FontFaceCache {
public:
    FontFaceCache();
    ~FontFaceCache();
    FontFaceCache(const FontFaceCache& other);
    FontFaceCache(FontFaceCache&& other) noexcept;
    FontFaceCache& operator=(const FontFaceCache& other);
    FontFaceCache& operator=(FontFaceCache&& other) noexcept;

    explicit operator bool() const;

    /// Remove all entries.
    void reset();

    /// Add a font face with the given family and style.
    void add(std::string_view family, bool bold, bool italic, const FontFace& face);

    /// Load a font file and add it under the given family and style.
    /// @return true on success.
    bool add_file(std::string_view family, bool bold, bool italic, const char* filename, int ttcindex = 0);

    /// Look up a face by family and style.
    /// @return A font face handle. Check operator bool() for validity.
    [[nodiscard]] FontFace get(std::string_view family, bool bold, bool italic) const;

    /// Load all faces from a font file (TTC, OTF, TTF).
    /// @return Number of faces loaded, or -1 if font loading is disabled.
    int load_file(const char* filename);

    /// Recursively load all font files from a directory.
    int load_dir(const char* dirname);

    /// Load system fonts.
    int load_sys();

    struct Impl;

private:
    Impl* m_impl = nullptr;
};

// ---- Surface ----

/// Pixel surface for 2D rendering.
///
/// Default format is premultiplied ARGB (0xAARRGGBB). Other pixel formats
/// are supported for interop with external systems. The internal rendering
/// pipeline always works in ARGB32_Premultiplied; format conversion happens
/// at composite/blit boundaries.
///
/// Copy-on-write (COW) value semantics. Copies are cheap (shared data).
/// Mutating operations (clear, apply_blur, drawing via Canvas) detach from shared storage.
class PLUTOVG_API Surface {
public:
    Surface();
    ~Surface();
    Surface(const Surface& other);
    Surface(Surface&& other) noexcept;
    Surface& operator=(const Surface& other);
    Surface& operator=(Surface&& other) noexcept;

    explicit operator bool() const;

    /// Create a surface with the given dimensions (default: ARGB32_Premultiplied).
    [[nodiscard]] static Surface create(int width, int height,
                                        PixelFormat format = PixelFormat::ARGB32_Premultiplied);

    /// Create a surface wrapping existing pixel data (no ownership transfer).
    /// The data pointer must remain valid for the lifetime of this surface.
    /// COW detach may copy to a new buffer if the surface is shared.
    [[nodiscard]] static Surface create_for_data(unsigned char* data, int width, int height, int stride,
                                                  PixelFormat format = PixelFormat::ARGB32_Premultiplied);

    /// Wrap an external buffer with explicit non-owning, non-COW semantics.
    ///
    /// Unlike create_for_data(), wrap() guarantees zero-copy: all drawing
    /// goes directly into the provided buffer. COW detach is disabled —
    /// attempting to share and mutate a wrapped surface is a fatal error.
    ///
    /// Use this for framebuffers, shared memory, and compositor back-buffers
    /// where you need guaranteed control over the memory being written to.
    [[nodiscard]] static Surface wrap(unsigned char* data, int width, int height, int stride,
                                      PixelFormat format = PixelFormat::ARGB32_Premultiplied);

    /// Load a surface from an image file (PNG, JPEG, etc.).
    [[nodiscard]] static Surface load_from_image_file(const char* filename);

    /// Load a surface from raw image data in memory.
    [[nodiscard]] static Surface load_from_image_data(std::span<const std::byte> data);

    /// Load a surface from base64-encoded image data.
    [[nodiscard]] static Surface load_from_image_base64(std::string_view data);

    // -- Accessors --

    /// Read-only access to pixel data.
    [[nodiscard]] const unsigned char* data() const noexcept;

    /// Mutable access to pixel data. Triggers COW detach if the buffer is shared,
    /// which may allocate. Use data() for read-only access to avoid unnecessary copies.
    /// For wrapped surfaces, always returns the original buffer (no COW).
    [[nodiscard]] unsigned char* mutable_data();

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] int stride() const noexcept;

    /// Pixel format of this surface.
    [[nodiscard]] PixelFormat format() const noexcept;

    /// Bytes per pixel for this surface's format.
    [[nodiscard]] int bytes_per_pixel() const noexcept;

    /// True if this surface was created with wrap() (non-COW, external buffer).
    [[nodiscard]] bool is_wrapped() const noexcept;

    // -- HiDPI scale factor --

    /// The backing scale factor (default: 1.0).
    /// A scale factor of 2.0 means this surface has 2x the pixel density:
    /// a 200×200 pixel surface represents a 100×100 logical area.
    [[nodiscard]] float scale_factor() const noexcept;

    /// Set the backing scale factor.
    void set_scale_factor(float scale);

    /// Logical width (width / scale_factor), for coordinate mapping.
    [[nodiscard]] float logical_width() const noexcept;

    /// Logical height (height / scale_factor), for coordinate mapping.
    [[nodiscard]] float logical_height() const noexcept;

    // -- Direct compositing (no Canvas, no rasterization) --

    /// Composite a source surface onto this surface.
    ///
    /// This is the fast path for window managers and compositors: direct
    /// rect-to-rect pixel blit with Porter-Duff operator and optional blend mode.
    /// No path construction, no span rasterization, no Canvas overhead.
    ///
    /// If source and destination have different pixel formats, conversion
    /// happens per-scanline through the native ARGB32_Premultiplied format.
    ///
    /// @param src        Source surface to composite.
    /// @param src_rect   Region of the source to read (in source pixel coords).
    /// @param dst_x      Destination x (in this surface's pixel coords).
    /// @param dst_y      Destination y (in this surface's pixel coords).
    /// @param op         Porter-Duff compositing operator (default: SrcOver).
    /// @param blend_mode Extended blend mode (default: Normal).
    /// @param opacity    Global opacity [0..1] applied to source (default: 1).
    void composite(const Surface& src, const IntRect& src_rect,
                   int dst_x, int dst_y,
                   Operator op = Operator::SrcOver,
                   BlendMode blend_mode = BlendMode::Normal,
                   float opacity = 1.0f);

    /// Composite entire source surface at (dst_x, dst_y).
    void composite(const Surface& src, int dst_x, int dst_y,
                   Operator op = Operator::SrcOver,
                   BlendMode blend_mode = BlendMode::Normal,
                   float opacity = 1.0f);

    /// Clear the surface to the given color.
    void clear(const Color& color);

    // -- Pixel format conversion --

    /// Convert this surface to a different pixel format, in-place.
    /// Triggers COW detach. A8 → 32-bit fills RGB with white.
    void convert_to(PixelFormat target_format);

    /// Create a copy in a different pixel format.
    [[nodiscard]] Surface converted(PixelFormat target_format) const;

    // -- Filters --

    /// Apply gaussian blur in-place. Triggers COW detach if shared.
    void apply_blur(float radius);

    /// Create a blurred copy without modifying this surface.
    [[nodiscard]] Surface blurred(float radius) const;

    /// Apply a 4×5 color matrix filter in-place. Operates in straight (non-premultiplied) color space.
    PLUTOVG_API void apply_color_matrix(const float matrix[20]);

    /// Apply a ColorMatrix struct in-place.
    PLUTOVG_API void apply_color_matrix(const ColorMatrix& cm);

    /// Create a color-matrix-transformed copy without modifying this surface.
    [[nodiscard]] PLUTOVG_API Surface color_matrix_transformed(const float matrix[20]) const;

    /// Create a color-matrix-transformed copy from a ColorMatrix struct.
    [[nodiscard]] PLUTOVG_API Surface color_matrix_transformed(const ColorMatrix& cm) const;

    // -- Export --

    /// Write to a PNG file.
    bool write_to_png(const char* filename) const;

    /// Write to a JPEG file.
    bool write_to_jpg(const char* filename, int quality = 90) const;

    /// Write PNG data to a callback.
    /// `func(closure, data_ptr, byte_count)` is called one or more times.
    using WriteFunc = void(*)(void* closure, const void* data, int size);

    bool write_to_png_stream(WriteFunc func, void* closure) const;
    bool write_to_jpg_stream(WriteFunc func, void* closure, int quality = 90) const;

    /// Type-safe write to PNG stream.
    template<typename F>
        requires(!std::is_convertible_v<std::decay_t<F>, const char*>)
    bool write_to_png(F&& func) const {
        auto fn = std::forward<F>(func);
        return write_to_png_stream([](void* ctx, const void* data, int sz) {
            (*static_cast<std::decay_t<F>*>(ctx))(data, sz);
        }, static_cast<void*>(&fn));
    }

    /// Type-safe write to JPEG stream.
    template<typename F>
        requires(!std::is_convertible_v<std::decay_t<F>, const char*>)
    bool write_to_jpg(F&& func, int quality = 90) const {
        auto fn = std::forward<F>(func);
        return write_to_jpg_stream([](void* ctx, const void* data, int sz) {
            (*static_cast<std::decay_t<F>*>(ctx))(data, sz);
        }, static_cast<void*>(&fn), quality);
    }

    struct Impl;

private:
    Impl* m_impl = nullptr;
};

/// Convert pixel data from premultiplied ARGB to RGBA.
/// `src` and `dst` may alias.
PLUTOVG_API void convert_argb_to_rgba(unsigned char* dst, const unsigned char* src,
                                       int width, int height, int stride);

/// Convert pixel data from RGBA to premultiplied ARGB.
/// `src` and `dst` may alias.
PLUTOVG_API void convert_rgba_to_argb(unsigned char* dst, const unsigned char* src,
                                       int width, int height, int stride);

// ---- Paint ----

/// Paint object for solid colors, gradients, and textures.
///
/// Immutable after creation. Copies share the underlying data (cheap).
class PLUTOVG_API Paint {
public:
    Paint();
    ~Paint();
    Paint(const Paint& other);
    Paint(Paint&& other) noexcept;
    Paint& operator=(const Paint& other);
    Paint& operator=(Paint&& other) noexcept;

    explicit operator bool() const;

    /// Solid color paint.
    [[nodiscard]] static Paint color(const Color& c);
    [[nodiscard]] static Paint color(float r, float g, float b, float a = 1.0f);

    /// Linear gradient paint.
    /// @param stops Gradient stop array. Copied internally.
    /// @param matrix Optional paint-space transform.
    [[nodiscard]] static Paint linear_gradient(float x1, float y1, float x2, float y2,
                                               SpreadMethod spread,
                                               std::span<const GradientStop> stops,
                                               const Matrix* matrix = nullptr);

    /// Radial gradient paint.
    [[nodiscard]] static Paint radial_gradient(float cx, float cy, float cr,
                                               float fx, float fy, float fr,
                                               SpreadMethod spread,
                                               std::span<const GradientStop> stops,
                                               const Matrix* matrix = nullptr);

    /// Conic (sweep/angular) gradient paint.
    /// @param cx Center x.
    /// @param cy Center y.
    /// @param start_angle Starting angle in radians.
    [[nodiscard]] static Paint conic_gradient(float cx, float cy, float start_angle,
                                              SpreadMethod spread,
                                              std::span<const GradientStop> stops,
                                              const Matrix* matrix = nullptr);

    /// Texture paint from a surface.
    [[nodiscard]] static Paint texture(const Surface& surface, TextureType type,
                                       float opacity = 1.0f, const Matrix* matrix = nullptr);

    struct Impl;

private:
    Impl* m_impl = nullptr;
};

// ---- Canvas ----

/// Drawing context targeting a Surface.
///
/// Non-copyable, move-only.
class PLUTOVG_API Canvas {
public:
    /// Create a canvas for the given surface.
    ///
    /// The surface is taken by value. For surfaces created with
    /// Surface::create_for_data() or Surface::wrap() (zero-copy external buffer),
    /// pass the surface as a temporary or use std::move() so that the canvas
    /// holds the sole reference and draws directly into the external buffer:
    ///
    ///     Canvas c(Surface::create_for_data(buf, w, h, stride)); // OK
    ///     Canvas c(std::move(my_surface));                        // OK
    ///
    /// Passing an lvalue copies the handle (increments the refcount), which
    /// causes the first draw to COW-detach into a new buffer — the intended
    /// behaviour for owned surfaces but not for external buffers.
    ///
    /// If the surface has a scale_factor != 1.0, the initial CTM is set to
    /// scale by that factor, so all user-space coordinates are in logical units.
    /// For example, a 200×200 surface with scale=2 behaves as a 100×100 canvas
    /// at 2× resolution, identical to Core Graphics backing scale factor.
    explicit Canvas(Surface surface);
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    Canvas(Canvas&& other) noexcept;
    Canvas& operator=(Canvas&& other) noexcept;

    /// True if the canvas is valid (not moved-from).
    explicit operator bool() const;

    /// The target surface (returned by value; Surface is a cheap COW handle).
    [[nodiscard]] Surface surface() const;

    // -- State stack --

    /// Push the current state (paint, transform, clip, stroke/fill settings, shadow, blend mode)
    /// onto the stack.
    void save();

    /// Pop the most recently saved state.
    void restore();

    /// Push state and begin rendering to an offscreen layer.
    ///
    /// All drawing between save_layer() and the matching restore() is rendered
    /// onto a temporary surface. When restore() is called, the offscreen layer
    /// is composited onto the parent surface with the given opacity, respecting
    /// the current compositing operator, blend mode, and clip.
    ///
    /// @param alpha Group opacity applied when compositing the layer (default: 1).
    /// @param bounds Optional bounding rect in user space. If null or empty,
    ///               the full clip region is used.
    PLUTOVG_API void save_layer(float alpha, const Rect* bounds = nullptr);

    /// Push a layer with explicit blend mode override.
    PLUTOVG_API void save_layer(float alpha, BlendMode blend_mode, const Rect* bounds = nullptr);

    // -- Paint (default: opaque black) --
    //
    // set_color / set_paint / set_*_gradient / set_texture set both fill
    // and stroke paint simultaneously for backward compatibility.
    // Use the fill- / stroke-prefixed variants for independent control.

    void set_color(const Color& c);
    void set_color(float r, float g, float b, float a = 1.0f);

    void set_linear_gradient(float x1, float y1, float x2, float y2,
                             SpreadMethod spread,
                             std::span<const GradientStop> stops,
                             const Matrix* matrix = nullptr);

    void set_radial_gradient(float cx, float cy, float cr,
                             float fx, float fy, float fr,
                             SpreadMethod spread,
                             std::span<const GradientStop> stops,
                             const Matrix* matrix = nullptr);

    void set_conic_gradient(float cx, float cy, float start_angle,
                            SpreadMethod spread,
                            std::span<const GradientStop> stops,
                            const Matrix* matrix = nullptr);

    void set_texture(const Surface& surface, TextureType type,
                     float opacity = 1.0f, const Matrix* matrix = nullptr);

    void set_paint(const Paint& paint);

    /// Get the current solid color (or the paint's base color).
    [[nodiscard]] Color get_color() const;

    /// Get the current paint object, if a non-solid paint is set.
    [[nodiscard]] Paint get_paint() const;

    // -- Separate fill / stroke paint --

    void set_fill_color(const Color& c);
    void set_fill_color(float r, float g, float b, float a = 1.0f);
    void set_fill_paint(const Paint& paint);

    void set_stroke_color(const Color& c);
    void set_stroke_color(float r, float g, float b, float a = 1.0f);
    void set_stroke_paint(const Paint& paint);

    [[nodiscard]] Color get_fill_color() const;
    [[nodiscard]] Paint get_fill_paint() const;
    [[nodiscard]] Color get_stroke_color() const;
    [[nodiscard]] Paint get_stroke_paint() const;

    // -- Shadow --

    /// Set a drop shadow for subsequent drawing operations.
    void set_shadow(float offset_x, float offset_y, float blur, const Color& color);

    /// Set a drop shadow from a Shadow struct.
    void set_shadow(const Shadow& shadow);

    /// Disable the current shadow.
    void clear_shadow();

    /// Get the current shadow parameters.
    [[nodiscard]] Shadow get_shadow() const;

    // -- Blend mode --

    /// Set the extended blend mode (default: Normal).
    void set_blend_mode(BlendMode mode);

    [[nodiscard]] BlendMode get_blend_mode() const;

    // -- Masking --

    /// Paint the current clip region through an alpha/luminance mask.
    /// This is a one-shot operation (like cairo_mask_surface): the current
    /// paint is modulated by the mask coverage and composited onto the surface.
    /// Does not affect state. Does not clear the current path.
    /// @param mask   Surface used as the mask source.
    /// @param mode   How to extract coverage from the mask pixels.
    /// @param ox     Horizontal offset of the mask relative to the target surface.
    /// @param oy     Vertical offset of the mask relative to the target surface.
    void mask(const Surface& mask, MaskMode mode, int ox = 0, int oy = 0);

    // -- Color interpolation and dithering --

    /// Set gradient color interpolation space (default: SRGB).
    /// LinearRGB produces physically correct gradients without dark bands.
    void  set_color_interpolation(ColorInterpolation ci);
    [[nodiscard]] ColorInterpolation get_color_interpolation() const;

    /// Enable ordered dithering for gradient output (default: false).
    /// Reduces visible banding on 8-bit displays, especially in dark gradients.
    void  set_dithering(bool enabled);
    [[nodiscard]] bool get_dithering() const;

    /// Enable anti-aliasing for rasterization (default: true).
    /// When disabled, all spans have full coverage (hard edges).
    PLUTOVG_API void set_antialias(bool enabled);
    [[nodiscard]] PLUTOVG_API bool get_antialias() const;

    /// Enable pixel snapping for axis-aligned rectangles and lines (default: false).
    /// When enabled, rect() and round_rect() coordinates are snapped to pixel
    /// boundaries in device space, eliminating sub-pixel seams. Only active when
    /// the current transform is axis-aligned (no rotation or skew).
    PLUTOVG_API void set_pixel_snap(bool enabled);
    [[nodiscard]] PLUTOVG_API bool get_pixel_snap() const;

    // -- Font management --

    void set_font_face_cache(const FontFaceCache& cache);
    [[nodiscard]] FontFaceCache get_font_face_cache() const;

    /// Add a font face under a family name.
    void add_font_face(std::string_view family, bool bold, bool italic, const FontFace& face);

    /// Load a font file and add it under a family name.
    bool add_font_file(std::string_view family, bool bold, bool italic, const char* filename, int ttcindex = 0);

    /// Select a font from the cache by family and style.
    bool select_font_face(std::string_view family, bool bold, bool italic);

    /// Set both font face and size.
    void set_font(const FontFace& face, float size);

    void set_font_face(const FontFace& face);
    [[nodiscard]] FontFace get_font_face() const;

    /// Font size in pixels (default: 12).
    void  set_font_size(float size);
    [[nodiscard]] float get_font_size() const;

    // -- Fill / Stroke settings --

    /// Fill rule (default: NonZero).
    void     set_fill_rule(FillRule rule);
    [[nodiscard]] FillRule get_fill_rule() const;

    /// Compositing operator (default: SrcOver).
    void      set_operator(Operator op);
    [[nodiscard]] Operator get_operator() const;

    /// Global opacity (default: 1).
    void  set_opacity(float opacity);
    [[nodiscard]] float get_opacity() const;

    /// Line width (default: 1).
    void  set_line_width(float width);
    [[nodiscard]] float get_line_width() const;

    /// Line cap (default: Butt).
    void    set_line_cap(LineCap cap);
    [[nodiscard]] LineCap get_line_cap() const;

    /// Line join (default: Miter).
    void     set_line_join(LineJoin join);
    [[nodiscard]] LineJoin get_line_join() const;

    /// Miter limit (default: 10).
    void  set_miter_limit(float limit);
    [[nodiscard]] float get_miter_limit() const;

    /// Set dash pattern and offset.
    void set_dash(float offset, std::span<const float> dashes);
    void set_dash_offset(float offset);
    void set_dash_array(std::span<const float> dashes);

    [[nodiscard]] float get_dash_offset() const;

    /// Get the current dash array.
    /// @return Number of dashes. `dashes` is set to the internal array pointer.
    [[nodiscard]] int get_dash_array(const float** dashes) const;

    // -- Transform (operates on the current transformation matrix) --

    void translate(float tx, float ty);
    void scale(float sx, float sy);
    void shear(float shx, float shy);
    void rotate(float radians);

    /// Pre-multiply the CTM by the given matrix.
    void transform(const Matrix& m);

    /// Reset the CTM to identity.
    void reset_matrix();

    /// Replace the CTM.
    void set_matrix(const Matrix& m);

    /// Get the current transformation matrix.
    [[nodiscard]] Matrix get_matrix() const;

    /// Transform a point from user space to device space using the CTM.
    [[nodiscard]] Point map(Point p) const;

    /// Transform separate coordinates from user space to device space.
    void map(float x, float y, float& ox, float& oy) const;

    /// Transform a rectangle from user space to device space (bounding box).
    [[nodiscard]] Rect map_rect(const Rect& r) const;

    // -- Path construction (operates on the canvas's internal path) --

    /// Move current point (SVG M).
    void move_to(float x, float y);

    /// Line to point (SVG L).
    void line_to(float x, float y);

    /// Quadratic Bezier (SVG Q).
    void quad_to(float x1, float y1, float x2, float y2);

    /// Cubic Bezier (SVG C).
    void cubic_to(float x1, float y1, float x2, float y2, float x3, float y3);

    /// Elliptical arc (SVG A).
    /// @param angle Rotation of the ellipse in radians.
    void arc_to(float rx, float ry, float angle, bool large_arc_flag, bool sweep_flag, float x, float y);

    /// Add a rectangle to the current path.
    void rect(float x, float y, float w, float h);

    /// Add a rounded rectangle.
    void round_rect(float x, float y, float w, float h, float rx, float ry);

    /// Add a rounded rectangle with per-corner radii.
    void round_rect(float x, float y, float w, float h, const CornerRadii& radii);

    /// Add an ellipse.
    void ellipse(float cx, float cy, float rx, float ry);

    /// Add a circle.
    void circle(float cx, float cy, float r);

    /// Add an arc.
    void arc(float cx, float cy, float r, float a0, float a1, bool ccw);

    /// Append an external path to the current path.
    void add_path(const Path& path);

    /// Clear the current path.
    void new_path();

    /// Close the current subpath.
    void close_path();

    /// Current pen position on the internal path.
    [[nodiscard]] Point current_point() const;

    /// Get a snapshot of the current internal path.
    [[nodiscard]] Path get_path() const;

    // -- Hit testing --

    /// Test if a point is inside the current fill region.
    /// Does not consider clip or surface bounds.
    bool fill_contains(float x, float y);

    /// Test if a point is inside the current stroke region.
    bool stroke_contains(float x, float y);

    /// Test if a point is inside the current clip region.
    bool clip_contains(float x, float y);

    // -- Extents --

    /// Bounding box of the area affected by a fill.
    [[nodiscard]] Rect fill_extents();

    /// Bounding box of the area affected by a stroke.
    [[nodiscard]] Rect stroke_extents();

    /// Bounding box of the current clip region.
    [[nodiscard]] Rect clip_extents();

    // -- Drawing (clears the current path) --

    void fill();
    void stroke();
    void clip();

    /// Paint the current clip region with the current paint. Does not affect the path.
    void paint();

    // -- Drawing, preserving the current path --

    void fill_preserve();
    void stroke_preserve();
    void clip_preserve();

    // -- Convenience: fill/stroke/clip a specific rect or path (clears current path) --

    void fill_rect(float x, float y, float w, float h);
    void fill_path(const Path& path);

    void stroke_rect(float x, float y, float w, float h);
    void stroke_path(const Path& path);

    void clip_rect(float x, float y, float w, float h);
    void clip_path(const Path& path);

    /// Set clip to a set of axis-aligned rectangles (damage rects, window regions).
    ///
    /// This is the fast path for compositors and window managers: it sets the
    /// scanline clipper directly from rectangles without any path rasterization.
    /// Much faster than building a path from rectangles and calling clip().
    ///
    /// Intersects with the current clip (if any). Rectangles are in device space
    /// (post-CTM). Overlapping rectangles are merged automatically.
    void clip_region(std::span<const IntRect> rects);

    // -- Surface drawing --

    /// Draw a source surface at position (x, y) in user space.
    /// Respects current transform, clip, opacity, and blend mode.
    PLUTOVG_API void draw_surface(const Surface& src, float x, float y);

    /// Draw a source surface scaled to fit the destination rectangle.
    PLUTOVG_API void draw_surface(const Surface& src, const Rect& dst_rect);

    /// Draw a sub-region of the source surface into the destination rectangle.
    PLUTOVG_API void draw_surface(const Surface& src, const Rect& src_rect, const Rect& dst_rect);

    // -- Text (low-level, any encoding) --

    /// Add a single glyph outline to the current path.
    /// @return Advance width.
    float add_glyph(Codepoint codepoint, float x, float y);

    /// Add text outlines to the current path.
    /// @return Total advance width.
    float add_text(const void* text, int length, TextEncoding encoding, float x, float y);

    /// Fill text (clears current path).
    float fill_text(const void* text, int length, TextEncoding encoding, float x, float y);

    /// Stroke text (clears current path).
    float stroke_text(const void* text, int length, TextEncoding encoding, float x, float y);

    /// Clip to text outlines (clears current path).
    float clip_text(const void* text, int length, TextEncoding encoding, float x, float y);

    // -- Text (convenience: UTF-8 via string_view) --

    float add_text(std::string_view text, float x, float y) {
        return add_text(text.data(), static_cast<int>(text.size()), TextEncoding::UTF8, x, y);
    }

    float fill_text(std::string_view text, float x, float y) {
        return fill_text(text.data(), static_cast<int>(text.size()), TextEncoding::UTF8, x, y);
    }

    float stroke_text(std::string_view text, float x, float y) {
        return stroke_text(text.data(), static_cast<int>(text.size()), TextEncoding::UTF8, x, y);
    }

    float clip_text(std::string_view text, float x, float y) {
        return clip_text(text.data(), static_cast<int>(text.size()), TextEncoding::UTF8, x, y);
    }

    // -- Text metrics --

    /// Font metrics for the current font face and size.
    [[nodiscard]] FontMetrics font_metrics() const;

    /// Glyph metrics for the current font.
    [[nodiscard]] GlyphMetrics glyph_metrics(Codepoint codepoint) const;

    /// Compute text bounding box and advance width.
    float text_extents(const void* text, int length, TextEncoding encoding, Rect* extents) const;

    /// Convenience: text extents for UTF-8.
    float text_extents(std::string_view text, Rect* extents = nullptr) const {
        return text_extents(text.data(), static_cast<int>(text.size()), TextEncoding::UTF8, extents);
    }

    struct Impl;

private:
    Impl* m_impl = nullptr;
};

} // namespace plutovg

#endif // PLUTOVG_HPP
