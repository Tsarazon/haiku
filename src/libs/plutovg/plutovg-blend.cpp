// plutovg-blend.cpp - Compositing and blending
// C++20

#include "plutovg-private.hpp"
#include "plutovg-utils.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

namespace plutovg {

static constexpr int kColorTableSize = 1024;
static constexpr int kBufferSize = 1024;

static constexpr int kFixptBits = 8;
static constexpr int kFixptSize = 1 << kFixptBits;
static constexpr int kFixedScale = 1 << 16;
static constexpr int kHalfPoint = 1 << 15;

// Rendering options threaded through the blend pipeline.
struct BlendOpts {
    BlendMode          blend_mode  = BlendMode::Normal;
    ColorInterpolation color_interp = ColorInterpolation::SRGB;
    bool               dithering   = false;
};

// Internal surface accessor for blend targets.
// Blend always writes to a surface that the caller owns mutably,
// even when passed through const Surface& in BlendParams.
struct SurfaceRef {
    unsigned char* data;
    int width;
    int height;
    int stride;

    explicit SurfaceRef(Surface& s)
        : data(s.mutable_data()), width(s.width()), height(s.height()), stride(s.stride()) {}
};

// Gradient data for rasterization

struct GradientData {
    GradientType type = GradientType::Linear;
    Matrix matrix;
    SpreadMethod spread = SpreadMethod::Pad;
    uint32_t colortable[kColorTableSize] = {};

    union {
        struct { float x1, y1, x2, y2; } linear;
        struct { float cx, cy, cr, fx, fy, fr; } radial;
        struct { float cx, cy, start_angle; } conic;
    } values = {};
};

struct TextureData {
    Matrix matrix;
    const uint8_t* data = nullptr;
    int width  = 0;
    int height = 0;
    int stride = 0;
    int const_alpha = 256;
};

struct LinearGradientValues {
    float dx, dy, l, off;
};

struct RadialGradientValues {
    float dx, dy, dr, sqrfr, a;
    bool extended = false;
};

// Pixel math (SIMD-friendly fixed-point)

static inline uint32_t interpolate_pixel_255(uint32_t x, uint32_t a, uint32_t y, uint32_t b)
{
    uint32_t t = (x & 0xff00ff) * a + (y & 0xff00ff) * b;
    t = (t + ((t >> 8) & 0xff00ff) + 0x800080) >> 8;
    t &= 0xff00ff;

    x = ((x >> 8) & 0xff00ff) * a + ((y >> 8) & 0xff00ff) * b;
    x = (x + ((x >> 8) & 0xff00ff) + 0x800080);
    x &= 0xff00ff00;
    x |= t;
    return x;
}

static inline uint32_t interpolate_pixel_256(uint32_t x, uint32_t a, uint32_t y, uint32_t b)
{
    uint32_t t = (x & 0xff00ff) * a + (y & 0xff00ff) * b;
    t >>= 8;
    t &= 0xff00ff;

    x = ((x >> 8) & 0xff00ff) * a + ((y >> 8) & 0xff00ff) * b;
    x &= 0xff00ff00;
    x |= t;
    return x;
}

// byte_mul is now in plutovg-utils.hpp as an inline function.

// memfill32

#ifdef __SSE2__

void memfill32(uint32_t* dest, int length, uint32_t value)
{
    __m128i vec = _mm_set1_epi32(static_cast<int>(value));

    while (length && (reinterpret_cast<uintptr_t>(dest) & 0xf)) {
        *dest++ = value;
        --length;
    }

    while (length >= 32) {
        _mm_store_si128(reinterpret_cast<__m128i*>(dest),      vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 4),  vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 8),  vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 12), vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 16), vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 20), vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 24), vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 28), vec);
        dest += 32;
        length -= 32;
    }

    if (length >= 16) {
        _mm_store_si128(reinterpret_cast<__m128i*>(dest),      vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 4),  vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 8),  vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 12), vec);
        dest += 16;
        length -= 16;
    }

    if (length >= 8) {
        _mm_store_si128(reinterpret_cast<__m128i*>(dest),     vec);
        _mm_store_si128(reinterpret_cast<__m128i*>(dest + 4), vec);
        dest += 8;
        length -= 8;
    }

    if (length >= 4) {
        _mm_store_si128(reinterpret_cast<__m128i*>(dest), vec);
        dest += 4;
        length -= 4;
    }

    while (length--) {
        *dest++ = value;
    }
}

#else

void memfill32(uint32_t* dest, int length, uint32_t value)
{
    while (length--) {
        *dest++ = value;
    }
}

#endif // __SSE2__

// Premultiply color with opacity

static inline uint32_t premultiply_color_with_opacity(const Color& color, float opacity)
{
    uint32_t a = static_cast<uint32_t>(std::clamp(std::lround(color.a * opacity * 255), 0L, 255L));
    uint32_t pr = static_cast<uint32_t>(std::clamp(std::lround(color.r * a), 0L, 255L));
    uint32_t pg = static_cast<uint32_t>(std::clamp(std::lround(color.g * a), 0L, 255L));
    uint32_t pb = static_cast<uint32_t>(std::clamp(std::lround(color.b * a), 0L, 255L));
    return (a << 24) | (pr << 16) | (pg << 8) | pb;
}

// CSS blend mode helpers

// Apply a separable blend mode per-channel.
using ChannelBlendFn = uint8_t(*)(uint8_t, uint8_t);

static ChannelBlendFn separable_blend_fn(BlendMode mode)
{
    using namespace blend_ops;
    switch (mode) {
    case BlendMode::Multiply:   return multiply;
    case BlendMode::Screen:     return screen;
    case BlendMode::Overlay:    return overlay;
    case BlendMode::Darken:     return darken;
    case BlendMode::Lighten:    return lighten;
    case BlendMode::ColorDodge: return color_dodge;
    case BlendMode::ColorBurn:  return color_burn;
    case BlendMode::HardLight:  return hard_light;
    case BlendMode::SoftLight:  return soft_light;
    case BlendMode::Difference: return difference;
    case BlendMode::Exclusion:  return exclusion;
    default: return nullptr; // Normal, Hue, Saturation, Color, Luminosity
    }
}

// Apply CSS blend mode to a source buffer against destination (SrcOver only).
// Modifies src[] in-place so that standard SrcOver composition gives the correct CSS result.
//
// CSS spec:  Co = (1-αb)·Cs + αb·B(Cb, Cs)
//            result = αs·Co + (1-αs)·Cb_premul   (standard SrcOver with modified source)
//
// So we set:  src_premul_new = αs · Co = αs·(1-αb)·Cs + αs·αb·B(Cb, Cs)

static void apply_blend_mode_buffer(uint32_t* src, const uint32_t* dest, int length, BlendMode mode)
{
    if (mode == BlendMode::Normal)
        return;

    auto sep = separable_blend_fn(mode);
    bool is_hsl = (mode == BlendMode::Hue || mode == BlendMode::Saturation
                || mode == BlendMode::Color || mode == BlendMode::Luminosity);

    for (int i = 0; i < length; ++i) {
        uint32_t s = src[i];
        uint32_t d = dest[i];

        uint32_t sa = alpha(s);
        uint32_t da = alpha(d);
        if (sa == 0) continue;
        if (da == 0) continue; // Co = Cs when αb=0, so no change needed

        auto [sr, sg, sb] = unpremultiply(s);
        auto [dr, dg, db] = unpremultiply(d);

        uint8_t br, bg, bb;
        if (sep) {
            br = sep(dr, sr);
            bg = sep(dg, sg);
            bb = sep(db, sb);
        } else if (is_hsl) {
            switch (mode) {
            case BlendMode::Hue:        hsl_blend_ops::hue(sr,sg,sb, dr,dg,db, br,bg,bb); break;
            case BlendMode::Saturation: hsl_blend_ops::saturation(sr,sg,sb, dr,dg,db, br,bg,bb); break;
            case BlendMode::Color:      hsl_blend_ops::color(sr,sg,sb, dr,dg,db, br,bg,bb); break;
            case BlendMode::Luminosity: hsl_blend_ops::luminosity(sr,sg,sb, dr,dg,db, br,bg,bb); break;
            default: br = sr; bg = sg; bb = sb; break;
            }
        } else {
            br = sr; bg = sg; bb = sb;
        }

        // Co = (1-αb)·Cs + αb·B(Cb, Cs)
        uint32_t ida = 255 - da;
        uint32_t cor = (ida * sr + da * br) / 255;
        uint32_t cog = (ida * sg + da * bg) / 255;
        uint32_t cob = (ida * sb + da * bb) / 255;

        // Premultiply Co with source alpha
        uint32_t pr = (sa * cor) / 255;
        uint32_t pg = (sa * cog) / 255;
        uint32_t pb = (sa * cob) / 255;

        src[i] = (sa << 24) | (pr << 16) | (pg << 8) | pb;
    }
}

// Apply blend mode to a solid color against each destination pixel.
// Writes blended premultiplied pixels into out[].

static void apply_blend_mode_solid(uint32_t solid, const uint32_t* dest, uint32_t* out,
                                   int length, BlendMode mode)
{
    for (int i = 0; i < length; ++i)
        out[i] = solid;
    apply_blend_mode_buffer(out, dest, length, mode);
}

// Gradient clamping

static inline int gradient_clamp(const GradientData& gradient, int ipos)
{
    switch (gradient.spread) {
    case SpreadMethod::Repeat:
        ipos = ipos % kColorTableSize;
        if (ipos < 0) ipos += kColorTableSize;
        break;
    case SpreadMethod::Reflect: {
        const int limit = kColorTableSize * 2;
        ipos = ipos % limit;
        if (ipos < 0) ipos += limit;
        if (ipos >= kColorTableSize) ipos = limit - 1 - ipos;
        break;
    }
    case SpreadMethod::Pad:
        ipos = std::clamp(ipos, 0, kColorTableSize - 1);
        break;
    }
    return ipos;
}

static inline uint32_t gradient_pixel_fixed(const GradientData& gradient, int fixed_pos)
{
    int ipos = (fixed_pos + (kFixptSize / 2)) >> kFixptBits;
    return gradient.colortable[gradient_clamp(gradient, ipos)];
}

static inline uint32_t gradient_pixel(const GradientData& gradient, float pos)
{
    int ipos = static_cast<int>(pos * (kColorTableSize - 1) + 0.5f);
    return gradient.colortable[gradient_clamp(gradient, ipos)];
}

// Gradient fetchers

static void fetch_linear_gradient(uint32_t* buffer, const LinearGradientValues& v,
                                  const GradientData& gradient, int y, int x, int length)
{
    float t, inc;

    if (v.l == 0.0f) {
        t = inc = 0;
    } else {
        float rx = gradient.matrix.c * (y + 0.5f) + gradient.matrix.a * (x + 0.5f) + gradient.matrix.e;
        float ry = gradient.matrix.d * (y + 0.5f) + gradient.matrix.b * (x + 0.5f) + gradient.matrix.f;
        t = v.dx * rx + v.dy * ry + v.off;
        inc = v.dx * gradient.matrix.a + v.dy * gradient.matrix.b;
        t *= (kColorTableSize - 1);
        inc *= (kColorTableSize - 1);
    }

    const uint32_t* end = buffer + length;
    if (inc > -1e-5f && inc < 1e-5f) {
        memfill32(buffer, length, gradient_pixel_fixed(gradient, static_cast<int>(t * kFixptSize)));
    } else {
        if (t + inc * length < static_cast<float>(INT_MAX >> (kFixptBits + 1))
         && t + inc * length > static_cast<float>(INT_MIN >> (kFixptBits + 1))) {
            int t_fixed = static_cast<int>(t * kFixptSize);
            int inc_fixed = static_cast<int>(inc * kFixptSize);
            while (buffer < end) {
                *buffer++ = gradient_pixel_fixed(gradient, t_fixed);
                t_fixed += inc_fixed;
            }
        } else {
            while (buffer < end) {
                *buffer++ = gradient_pixel(gradient, t / kColorTableSize);
                t += inc;
            }
        }
    }
}

static void fetch_radial_gradient(uint32_t* buffer, const RadialGradientValues& v,
                                  const GradientData& gradient, int y, int x, int length)
{
    if (v.a == 0.0f) {
        memfill32(buffer, length, 0);
        return;
    }

    float rx = gradient.matrix.c * (y + 0.5f) + gradient.matrix.e + gradient.matrix.a * (x + 0.5f);
    float ry = gradient.matrix.d * (y + 0.5f) + gradient.matrix.f + gradient.matrix.b * (x + 0.5f);

    rx -= gradient.values.radial.fx;
    ry -= gradient.values.radial.fy;

    float inv_a = 1.0f / (2.0f * v.a);
    float delta_rx = gradient.matrix.a;
    float delta_ry = gradient.matrix.b;

    float b = 2 * (v.dr * gradient.values.radial.fr + rx * v.dx + ry * v.dy);
    float delta_b = 2 * (delta_rx * v.dx + delta_ry * v.dy);
    float b_delta_b = 2 * b * delta_b;
    float delta_b_delta_b = 2 * delta_b * delta_b;

    float bb = b * b;
    float delta_bb = delta_b * delta_b;

    b *= inv_a;
    delta_b *= inv_a;

    float rxrxryry = rx * rx + ry * ry;
    float delta_rxrxryry = delta_rx * delta_rx + delta_ry * delta_ry;
    float rx_plus_ry = 2 * (rx * delta_rx + ry * delta_ry);
    float delta_rx_plus_ry = 2 * delta_rxrxryry;

    inv_a *= inv_a;

    float det = (bb - 4 * v.a * (v.sqrfr - rxrxryry)) * inv_a;
    float delta_det = (b_delta_b + delta_bb + 4 * v.a * (rx_plus_ry + delta_rxrxryry)) * inv_a;
    float delta_delta_det = (delta_b_delta_b + 4 * v.a * delta_rx_plus_ry) * inv_a;

    const uint32_t* end = buffer + length;
    if (v.extended) {
        while (buffer < end) {
            uint32_t result = 0;
            if (det >= 0) {
                float w = std::sqrt(det) - b;
                if (gradient.values.radial.fr + v.dr * w >= 0)
                    result = gradient_pixel(gradient, w);
            }
            *buffer++ = result;
            det += delta_det;
            delta_det += delta_delta_det;
            b += delta_b;
        }
    } else {
        while (buffer < end) {
            *buffer++ = gradient_pixel(gradient, std::sqrt(det) - b);
            det += delta_det;
            delta_det += delta_delta_det;
            b += delta_b;
        }
    }
}

// Conic (sweep) gradient fetcher

static void fetch_conic_gradient(uint32_t* buffer, const GradientData& gradient,
                                 int y, int x, int length)
{
    float cx = gradient.values.conic.cx;
    float cy = gradient.values.conic.cy;
    float start = gradient.values.conic.start_angle;

    for (int i = 0; i < length; ++i) {
        // Transform pixel center through inverse matrix to gradient space
        float px = x + i + 0.5f;
        float py = y + 0.5f;
        float gx = gradient.matrix.a * px + gradient.matrix.c * py + gradient.matrix.e;
        float gy = gradient.matrix.b * px + gradient.matrix.d * py + gradient.matrix.f;

        float angle = std::atan2(gy - cy, gx - cx) - start;
        // Normalize to [0, 1)
        float t = angle / two_pi;
        t = t - std::floor(t);
        buffer[i] = gradient_pixel(gradient, t);
    }
}

// Porter-Duff composition: solid color

using CompositionSolidFn = void(*)(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha);

static void composition_solid_clear(uint32_t* dest, int length, [[maybe_unused]] uint32_t color, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        memfill32(dest, length, 0);
    } else {
        uint32_t ialpha = 255 - const_alpha;
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(dest[i], ialpha);
    }
}

static void composition_solid_source(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        memfill32(dest, length, color);
    } else {
        uint32_t ialpha = 255 - const_alpha;
        color = byte_mul(color, const_alpha);
        for (int i = 0; i < length; ++i)
            dest[i] = color + byte_mul(dest[i], ialpha);
    }
}

static void composition_solid_destination(uint32_t*, int, uint32_t, uint32_t) {}

static void composition_solid_source_over(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if (const_alpha != 255)
        color = byte_mul(color, const_alpha);
    uint32_t ialpha = 255 - alpha(color);
    for (int i = 0; i < length; ++i)
        dest[i] = color + byte_mul(dest[i], ialpha);
}

static void composition_solid_destination_over(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if (const_alpha != 255)
        color = byte_mul(color, const_alpha);
    for (int i = 0; i < length; ++i) {
        uint32_t d = dest[i];
        dest[i] = d + byte_mul(color, alpha(~d));
    }
}

static void composition_solid_source_in(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(color, alpha(dest[i]));
    } else {
        color = byte_mul(color, const_alpha);
        uint32_t cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint32_t d = dest[i];
            dest[i] = interpolate_pixel_255(color, alpha(d), d, cia);
        }
    }
}

static void composition_solid_destination_in(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    uint32_t a = alpha(color);
    if (const_alpha != 255)
        a = byte_mul(a, const_alpha) + 255 - const_alpha;
    for (int i = 0; i < length; ++i)
        dest[i] = byte_mul(dest[i], a);
}

static void composition_solid_source_out(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(color, alpha(~dest[i]));
    } else {
        color = byte_mul(color, const_alpha);
        uint32_t cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint32_t d = dest[i];
            dest[i] = interpolate_pixel_255(color, alpha(~d), d, cia);
        }
    }
}

static void composition_solid_destination_out(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    uint32_t a = alpha(~color);
    if (const_alpha != 255)
        a = byte_mul(a, const_alpha) + 255 - const_alpha;
    for (int i = 0; i < length; ++i)
        dest[i] = byte_mul(dest[i], a);
}

static void composition_solid_source_atop(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if (const_alpha != 255)
        color = byte_mul(color, const_alpha);
    uint32_t sia = alpha(~color);
    for (int i = 0; i < length; ++i) {
        uint32_t d = dest[i];
        dest[i] = interpolate_pixel_255(color, alpha(d), d, sia);
    }
}

static void composition_solid_destination_atop(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    uint32_t a = alpha(color);
    if (const_alpha != 255) {
        color = byte_mul(color, const_alpha);
        a = alpha(color) + 255 - const_alpha;
    }
    for (int i = 0; i < length; ++i) {
        uint32_t d = dest[i];
        dest[i] = interpolate_pixel_255(d, a, color, alpha(~d));
    }
}

static void composition_solid_xor(uint32_t* dest, int length, uint32_t color, uint32_t const_alpha)
{
    if (const_alpha != 255)
        color = byte_mul(color, const_alpha);
    uint32_t sia = alpha(~color);
    for (int i = 0; i < length; ++i) {
        uint32_t d = dest[i];
        dest[i] = interpolate_pixel_255(color, alpha(~d), d, sia);
    }
}

static const CompositionSolidFn composition_solid_table[] = {
    composition_solid_clear,
    composition_solid_source,
    composition_solid_destination,
    composition_solid_source_over,
    composition_solid_destination_over,
    composition_solid_source_in,
    composition_solid_destination_in,
    composition_solid_source_out,
    composition_solid_destination_out,
    composition_solid_source_atop,
    composition_solid_destination_atop,
    composition_solid_xor
};

// Porter-Duff composition: source buffer

using CompositionFn = void(*)(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha);

static void composition_clear(uint32_t* dest, int length, [[maybe_unused]] const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        memfill32(dest, length, 0);
    } else {
        uint32_t ialpha = 255 - const_alpha;
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(dest[i], ialpha);
    }
}

static void composition_source(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        std::memcpy(dest, src, length * sizeof(uint32_t));
    } else {
        uint32_t ialpha = 255 - const_alpha;
        for (int i = 0; i < length; ++i)
            dest[i] = interpolate_pixel_255(src[i], const_alpha, dest[i], ialpha);
    }
}

static void composition_destination(uint32_t*, int, const uint32_t*, uint32_t) {}

static void composition_source_over(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            uint32_t s = src[i];
            if (s >= 0xff000000)
                dest[i] = s;
            else if (s != 0)
                dest[i] = s + byte_mul(dest[i], alpha(~s));
        }
    } else {
        for (int i = 0; i < length; ++i) {
            uint32_t s = byte_mul(src[i], const_alpha);
            dest[i] = s + byte_mul(dest[i], alpha(~s));
        }
    }
}

static void composition_destination_over(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            uint32_t d = dest[i];
            dest[i] = d + byte_mul(src[i], alpha(~d));
        }
    } else {
        for (int i = 0; i < length; ++i) {
            uint32_t d = dest[i];
            uint32_t s = byte_mul(src[i], const_alpha);
            dest[i] = d + byte_mul(s, alpha(~d));
        }
    }
}

static void composition_source_in(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(src[i], alpha(dest[i]));
    } else {
        uint32_t cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint32_t d = dest[i];
            uint32_t s = byte_mul(src[i], const_alpha);
            dest[i] = interpolate_pixel_255(s, alpha(d), d, cia);
        }
    }
}

static void composition_destination_in(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(dest[i], alpha(src[i]));
    } else {
        uint32_t cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint32_t a = byte_mul(alpha(src[i]), const_alpha) + cia;
            dest[i] = byte_mul(dest[i], a);
        }
    }
}

static void composition_source_out(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(src[i], alpha(~dest[i]));
    } else {
        uint32_t cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint32_t s = byte_mul(src[i], const_alpha);
            uint32_t d = dest[i];
            dest[i] = interpolate_pixel_255(s, alpha(~d), d, cia);
        }
    }
}

static void composition_destination_out(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i)
            dest[i] = byte_mul(dest[i], alpha(~src[i]));
    } else {
        uint32_t cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint32_t sia = byte_mul(alpha(~src[i]), const_alpha) + cia;
            dest[i] = byte_mul(dest[i], sia);
        }
    }
}

static void composition_source_atop(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            uint32_t s = src[i];
            uint32_t d = dest[i];
            dest[i] = interpolate_pixel_255(s, alpha(d), d, alpha(~s));
        }
    } else {
        for (int i = 0; i < length; ++i) {
            uint32_t s = byte_mul(src[i], const_alpha);
            uint32_t d = dest[i];
            dest[i] = interpolate_pixel_255(s, alpha(d), d, alpha(~s));
        }
    }
}

static void composition_destination_atop(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            uint32_t s = src[i];
            uint32_t d = dest[i];
            dest[i] = interpolate_pixel_255(d, alpha(s), s, alpha(~d));
        }
    } else {
        uint32_t cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint32_t s = byte_mul(src[i], const_alpha);
            uint32_t d = dest[i];
            uint32_t a = alpha(s) + cia;
            dest[i] = interpolate_pixel_255(d, a, s, alpha(~d));
        }
    }
}

static void composition_xor(uint32_t* dest, int length, const uint32_t* src, uint32_t const_alpha)
{
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            uint32_t d = dest[i];
            uint32_t s = src[i];
            dest[i] = interpolate_pixel_255(s, alpha(~d), d, alpha(~s));
        }
    } else {
        for (int i = 0; i < length; ++i) {
            uint32_t d = dest[i];
            uint32_t s = byte_mul(src[i], const_alpha);
            dest[i] = interpolate_pixel_255(s, alpha(~d), d, alpha(~s));
        }
    }
}

static const CompositionFn composition_table[] = {
    composition_clear,
    composition_source,
    composition_destination,
    composition_source_over,
    composition_destination_over,
    composition_source_in,
    composition_destination_in,
    composition_source_out,
    composition_destination_out,
    composition_source_atop,
    composition_destination_atop,
    composition_xor
};

// Blend solid color onto spans

static void blend_solid(const SurfaceRef& surface, Operator op, uint32_t solid, const SpanBuffer& span_buffer)
{
    auto func = composition_solid_table[static_cast<int>(op)];
    for (const auto& span : span_buffer.spans) {
        auto* target = reinterpret_cast<uint32_t*>(surface.data + span.y * surface.stride) + span.x;
        func(target, span.len, solid, span.coverage);
    }
}

// Texture blending

static void blend_untransformed_argb(const SurfaceRef& surface, Operator op,
                                     const TextureData& texture, const SpanBuffer& span_buffer)
{
    auto func = composition_table[static_cast<int>(op)];

    int xoff = static_cast<int>(texture.matrix.e);
    int yoff = static_cast<int>(texture.matrix.f);

    for (const auto& span : span_buffer.spans) {
        int x = span.x;
        int length = span.len;
        int sx = xoff + x;
        int sy = yoff + span.y;

        if (sy >= 0 && sy < texture.height && sx < texture.width) {
            if (sx < 0) {
                x -= sx;
                length += sx;
                sx = 0;
            }
            if (sx + length > texture.width)
                length = texture.width - sx;
            if (length > 0) {
                int coverage = (span.coverage * texture.const_alpha) >> 8;
                const auto* src = reinterpret_cast<const uint32_t*>(texture.data + sy * texture.stride) + sx;
                auto* dest = reinterpret_cast<uint32_t*>(surface.data + span.y * surface.stride) + x;
                func(dest, length, src, coverage);
            }
        }
    }
}


static void blend_untransformed_tiled_argb(const SurfaceRef& surface, Operator op,
                                           const TextureData& texture, const SpanBuffer& span_buffer)
{
    auto func = composition_table[static_cast<int>(op)];

    int xoff = static_cast<int>(texture.matrix.e) % texture.width;
    int yoff = static_cast<int>(texture.matrix.f) % texture.height;
    if (xoff < 0) xoff += texture.width;
    if (yoff < 0) yoff += texture.height;

    for (const auto& span : span_buffer.spans) {
        int x = span.x;
        int length = span.len;
        int sx = (xoff + span.x) % texture.width;
        int sy = (span.y + yoff) % texture.height;
        if (sx < 0) sx += texture.width;
        if (sy < 0) sy += texture.height;

        int coverage = (span.coverage * texture.const_alpha) >> 8;
        while (length) {
            int l = std::min({texture.width - sx, length, kBufferSize});
            const auto* src = reinterpret_cast<const uint32_t*>(texture.data + sy * texture.stride) + sx;
            auto* dest = reinterpret_cast<uint32_t*>(surface.data + span.y * surface.stride) + x;
            func(dest, l, src, coverage);
            x += l;
            sx += l;
            length -= l;
            if (sx >= texture.width) sx = 0;
        }
    }
}

// Build GradientData with colortable from paint parameters.

static GradientData build_gradient_data(const GradientPaintData& gradient,
                                        const Matrix& ctm, float opacity,
                                        ColorInterpolation interp = ColorInterpolation::SRGB)
{
    GradientData data;
    data.spread = gradient.spread;
    data.matrix = gradient.matrix * ctm;

    const auto& stops = gradient.stops;
    int nstops = static_cast<int>(stops.size());
    if (nstops == 0) return data;

    float incr = 1.0f / kColorTableSize;
    float fpos = 1.5f * incr;

    if (interp == ColorInterpolation::LinearRGB) {
        // Interpolate in linear (pre-multiplied) space, convert back to sRGB.
        struct LinearColor { float r, g, b, a; };

        auto to_linear = [&](const Color& c) -> LinearColor {
            float a = c.a * opacity;
            float r = color_space::srgb_to_linear(static_cast<uint8_t>(std::clamp(c.r * 255.0f + 0.5f, 0.0f, 255.0f)));
            float g = color_space::srgb_to_linear(static_cast<uint8_t>(std::clamp(c.g * 255.0f + 0.5f, 0.0f, 255.0f)));
            float b = color_space::srgb_to_linear(static_cast<uint8_t>(std::clamp(c.b * 255.0f + 0.5f, 0.0f, 255.0f)));
            return {r * a, g * a, b * a, a};
        };

        auto to_srgb_pixel = [](const LinearColor& lc) -> uint32_t {
            float a = lc.a;
            if (a <= 0.0f) return 0;
            uint8_t a8 = static_cast<uint8_t>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
            float inv_a = 1.0f / a;
            uint8_t r = color_space::linear_to_srgb(std::clamp(lc.r * inv_a, 0.0f, 1.0f));
            uint8_t g = color_space::linear_to_srgb(std::clamp(lc.g * inv_a, 0.0f, 1.0f));
            uint8_t b = color_space::linear_to_srgb(std::clamp(lc.b * inv_a, 0.0f, 1.0f));
            // Re-premultiply in sRGB space for the pixel buffer
            if (a8 != 255) {
                r = static_cast<uint8_t>((uint32_t(r) * a8) / 255);
                g = static_cast<uint8_t>((uint32_t(g) * a8) / 255);
                b = static_cast<uint8_t>((uint32_t(b) * a8) / 255);
            }
            return pack_argb(a8, r, g, b);
        };

        int pos = 0;
        LinearColor curr_lc = to_linear(stops[0].color);
        data.colortable[pos++] = to_srgb_pixel(curr_lc);

        while (fpos <= stops[0].offset && pos < kColorTableSize) {
            data.colortable[pos] = data.colortable[pos - 1];
            ++pos;
            fpos += incr;
        }

        for (int i = 0; i < nstops - 1; ++i) {
            const auto* curr = &stops[i];
            const auto* next = &stops[i + 1];
            if (curr->offset == next->offset) continue;

            LinearColor next_lc = to_linear(next->color);
            float delta = 1.0f / (next->offset - curr->offset);
            while (fpos < next->offset && pos < kColorTableSize) {
                float t = (fpos - curr->offset) * delta;
                float it = 1.0f - t;
                LinearColor mixed = {
                    curr_lc.r * it + next_lc.r * t,
                    curr_lc.g * it + next_lc.g * t,
                    curr_lc.b * it + next_lc.b * t,
                    curr_lc.a * it + next_lc.a * t
                };
                data.colortable[pos] = to_srgb_pixel(mixed);
                ++pos;
                fpos += incr;
            }
            curr_lc = next_lc;
        }

        uint32_t last = to_srgb_pixel(to_linear(stops.back().color));
        for (; pos < kColorTableSize; ++pos)
            data.colortable[pos] = last;
    } else {
        // sRGB interpolation (original path).
        int pos = 0;
        const auto* curr = &stops[0];
        uint32_t curr_color = premultiply_color_with_opacity(curr->color, opacity);
        data.colortable[pos++] = curr_color;

        while (fpos <= curr->offset && pos < kColorTableSize) {
            data.colortable[pos] = data.colortable[pos - 1];
            ++pos;
            fpos += incr;
        }

        for (int i = 0; i < nstops - 1; ++i) {
            curr = &stops[i];
            const auto* next = &stops[i + 1];
            if (curr->offset == next->offset) continue;

            float delta = 1.0f / (next->offset - curr->offset);
            uint32_t next_color = premultiply_color_with_opacity(next->color, opacity);
            while (fpos < next->offset && pos < kColorTableSize) {
                float t = (fpos - curr->offset) * delta;
                uint32_t dist = static_cast<uint32_t>(255 * t);
                uint32_t idist = 255 - dist;
                data.colortable[pos] = interpolate_pixel_255(curr_color, idist, next_color, dist);
                ++pos;
                fpos += incr;
            }
            curr_color = next_color;
        }

        uint32_t last_color = premultiply_color_with_opacity(stops.back().color, opacity);
        for (; pos < kColorTableSize; ++pos)
            data.colortable[pos] = last_color;
    }

    // Geometry
    data.type = gradient.type;
    if (gradient.type == GradientType::Linear) {
        data.values.linear.x1 = gradient.values[0];
        data.values.linear.y1 = gradient.values[1];
        data.values.linear.x2 = gradient.values[2];
        data.values.linear.y2 = gradient.values[3];
    } else if (gradient.type == GradientType::Radial) {
        data.values.radial.cx = gradient.values[0];
        data.values.radial.cy = gradient.values[1];
        data.values.radial.cr = gradient.values[2];
        data.values.radial.fx = gradient.values[3];
        data.values.radial.fy = gradient.values[4];
        data.values.radial.fr = gradient.values[5];
    } else {
        // Conic
        data.values.conic.cx = gradient.values[0];
        data.values.conic.cy = gradient.values[1];
        data.values.conic.start_angle = gradient.values[2];
    }

    return data;
}

// Build TextureData from paint parameters.

static std::optional<TextureData> build_texture_data(const TexturePaintData& texture,
                                                     const Matrix& ctm, float opacity)
{
    if (!texture.surface)
        return std::nullopt;

    TextureData data;
    data.matrix = texture.matrix * ctm;
    data.data   = texture.surface.data();
    data.width  = texture.surface.width();
    data.height = texture.surface.height();
    data.stride = texture.surface.stride();
    data.const_alpha = std::lroundf(opacity * texture.opacity * 256);

    auto inv = data.matrix.inverted();
    if (!inv) return std::nullopt;
    data.matrix = *inv;

    return data;
}

// Fetch gradient source pixels for a span row into buffer.
// Handles both linear and radial. Returns pixels written (<= length).

static void fetch_gradient_row(uint32_t* buffer, const GradientPaintData& paint,
                               const GradientData& data, int y, int x, int length)
{
    if (paint.type == GradientType::Linear) {
        LinearGradientValues v;
        v.dx = data.values.linear.x2 - data.values.linear.x1;
        v.dy = data.values.linear.y2 - data.values.linear.y1;
        v.l  = v.dx * v.dx + v.dy * v.dy;
        v.off = 0.0f;
        if (v.l != 0.0f) {
            v.dx /= v.l;
            v.dy /= v.l;
            v.off = -v.dx * data.values.linear.x1 - v.dy * data.values.linear.y1;
        }
        fetch_linear_gradient(buffer, v, data, y, x, length);
    } else if (paint.type == GradientType::Conic) {
        fetch_conic_gradient(buffer, data, y, x, length);
    } else {
        RadialGradientValues v;
        v.dx    = data.values.radial.cx - data.values.radial.fx;
        v.dy    = data.values.radial.cy - data.values.radial.fy;
        v.dr    = data.values.radial.cr - data.values.radial.fr;
        v.sqrfr = data.values.radial.fr * data.values.radial.fr;
        v.a     = v.dr * v.dr - v.dx * v.dx - v.dy * v.dy;
        v.extended = data.values.radial.fr != 0.0f || v.a <= 0.0f;
        fetch_radial_gradient(buffer, v, data, y, x, length);
    }
}

static inline uint32_t interpolate_4_pixels(uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br,
                                            uint32_t distx, uint32_t disty)
{
    uint32_t idistx = 256 - distx;
    uint32_t idisty = 256 - disty;
    uint32_t xtop = interpolate_pixel_256(tl, idistx, tr, distx);
    uint32_t xbot = interpolate_pixel_256(bl, idistx, br, distx);
    return interpolate_pixel_256(xtop, idisty, xbot, disty);
}

// Texture fetch functions.
// Each fetches `length` pixels into `buffer` using the inverse transform stored in `tex.matrix`.

using TextureFetchFn = void(*)(uint32_t* buffer, const TextureData& tex,
                                int y, int x, int length);

/// Nearest-neighbor, plain (clamp to edges, transparent outside).
static void fetch_nearest_plain(uint32_t* buffer, const TextureData& tex, int y, int x, int length)
{
    int fdx = static_cast<int>(tex.matrix.a * kFixedScale);
    int fdy = static_cast<int>(tex.matrix.b * kFixedScale);

    float cx = x + 0.5f;
    float cy = y + 0.5f;

    int tx = static_cast<int>((tex.matrix.c * cy + tex.matrix.a * cx + tex.matrix.e) * kFixedScale);
    int ty = static_cast<int>((tex.matrix.d * cy + tex.matrix.b * cx + tex.matrix.f) * kFixedScale);

    for (int i = 0; i < length; ++i) {
        int px = tx >> 16;
        int py = ty >> 16;
        if (px < 0 || px >= tex.width || py < 0 || py >= tex.height)
            buffer[i] = 0x00000000;
        else
            buffer[i] = reinterpret_cast<const uint32_t*>(tex.data + py * tex.stride)[px];
        tx += fdx;
        ty += fdy;
    }
}

/// Nearest-neighbor, tiled (wrap coordinates).
static void fetch_nearest_tiled(uint32_t* buffer, const TextureData& tex, int y, int x, int length)
{
    int fdx = static_cast<int>(tex.matrix.a * kFixedScale);
    int fdy = static_cast<int>(tex.matrix.b * kFixedScale);

    int scanline_offset = tex.stride / 4;
    const auto* image_bits = reinterpret_cast<const uint32_t*>(tex.data);

    float cx = x + 0.5f;
    float cy = y + 0.5f;

    int fx = static_cast<int>((tex.matrix.c * cy + tex.matrix.a * cx + tex.matrix.e) * kFixedScale);
    int fy = static_cast<int>((tex.matrix.d * cy + tex.matrix.b * cx + tex.matrix.f) * kFixedScale);

    for (int i = 0; i < length; ++i) {
        int px = (fx >> 16) % tex.width;
        int py = (fy >> 16) % tex.height;
        if (px < 0) px += tex.width;
        if (py < 0) py += tex.height;
        buffer[i] = image_bits[py * scanline_offset + px];
        fx += fdx;
        fy += fdy;
    }
}

/// Bilinear, tiled (wrap coordinates, bilinear interpolation).
static void fetch_bilinear_tiled(uint32_t* buffer, const TextureData& tex, int y, int x, int length)
{
    int fdx = static_cast<int>(tex.matrix.a * kFixedScale);
    int fdy = static_cast<int>(tex.matrix.b * kFixedScale);

    float cx = x + 0.5f;
    float cy = y + 0.5f;

    int fx = static_cast<int>((tex.matrix.c * cy + tex.matrix.a * cx + tex.matrix.e) * kFixedScale);
    int fy = static_cast<int>((tex.matrix.d * cy + tex.matrix.b * cx + tex.matrix.f) * kFixedScale);

    fx -= kHalfPoint;
    fy -= kHalfPoint;

    for (int i = 0; i < length; ++i) {
        int x1 = (fx >> 16) % tex.width;
        int y1 = (fy >> 16) % tex.height;
        if (x1 < 0) x1 += tex.width;
        if (y1 < 0) y1 += tex.height;

        int x2 = (x1 + 1) % tex.width;
        int y2 = (y1 + 1) % tex.height;

        const auto* s1 = reinterpret_cast<const uint32_t*>(tex.data + y1 * tex.stride);
        const auto* s2 = reinterpret_cast<const uint32_t*>(tex.data + y2 * tex.stride);

        uint32_t tl = s1[x1];
        uint32_t tr = s1[x2];
        uint32_t bl = s2[x1];
        uint32_t br = s2[x2];

        int distx = (fx & 0x0000ffff) >> 8;
        int disty = (fy & 0x0000ffff) >> 8;
        buffer[i] = interpolate_4_pixels(tl, tr, bl, br, distx, disty);

        fx += fdx;
        fy += fdy;
    }
}

/// Select texture fetch function based on texture type and transform.
static TextureFetchFn select_texture_fetch(TextureType type, const Matrix& m) {
    if (type == TextureType::Tiled) {
        // Preserve current behavior: tiled + rotation → bilinear
        if (std::fabs(m.b) > 1e-6f || std::fabs(m.c) > 1e-6f)
            return fetch_bilinear_tiled;
        return fetch_nearest_tiled;
    }
    return fetch_nearest_plain;
}

// Clip spans to an IntRect, appending results to `out`.

static void clip_spans_to_rect(const SpanBuffer& src, const IntRect& rect, SpanBuffer& out)
{
    for (const auto& span : src.spans) {
        if (span.y < rect.y || span.y >= rect.bottom())
            continue;
        int x0 = std::max(span.x, rect.x);
        int x1 = std::min(span.x + span.len, rect.right());
        if (x0 >= x1)
            continue;
        out.spans.push_back({x0, x1 - x0, span.y, span.coverage});
    }
}

// High-level blend dispatch (solid color, gradient, texture)

static void blend_color(const SurfaceRef& surface, Operator op, const Color& color, float opacity,
                        const SpanBuffer& span_buffer, BlendMode mode = BlendMode::Normal)
{
    uint32_t solid = premultiply_color_with_opacity(color, opacity);
    uint32_t a = alpha(solid);

    if (mode == BlendMode::Normal) {
        if (a == 255 && op == Operator::SrcOver)
            blend_solid(surface, Operator::Src, solid, span_buffer);
        else
            blend_solid(surface, op, solid, span_buffer);
        return;
    }

    // Blend mode: per-pixel blend against destination, then SrcOver.
    auto func = composition_table[static_cast<int>(Operator::SrcOver)];
    uint32_t buffer[kBufferSize];

    for (const auto& span : span_buffer.spans) {
        int length = span.len;
        int x = span.x;
        while (length) {
            int l = std::min(length, kBufferSize);
            auto* target = reinterpret_cast<uint32_t*>(surface.data + span.y * surface.stride) + x;
            apply_blend_mode_solid(solid, target, buffer, l, mode);
            func(target, l, buffer, span.coverage);
            x += l;
            length -= l;
        }
    }
}

static void blend_gradient(const SurfaceRef& surface, Operator op, const GradientPaintData& gradient,
                           const Matrix& ctm, float opacity, const SpanBuffer& span_buffer,
                           const BlendOpts& opts = {})
{
    if (gradient.stops.empty())
        return;

    GradientData data = build_gradient_data(gradient, ctm, opacity, opts.color_interp);
    auto inv = data.matrix.inverted();
    if (!inv) return;
    data.matrix = *inv;

    bool use_mode = (opts.blend_mode != BlendMode::Normal);
    auto func = composition_table[static_cast<int>(use_mode ? Operator::SrcOver : op)];
    uint32_t buffer[kBufferSize];

    for (const auto& span : span_buffer.spans) {
        int length = span.len;
        int x = span.x;
        while (length) {
            int l = std::min(length, kBufferSize);
            fetch_gradient_row(buffer, gradient, data, span.y, x, l);
            auto* target = reinterpret_cast<uint32_t*>(surface.data + span.y * surface.stride) + x;
            if (opts.dithering) {
                for (int i = 0; i < l; ++i) {
                    uint32_t px = buffer[i];
                    uint8_t pa = alpha(px);
                    if (pa == 0) continue;
                    auto [pr, pg, pb] = unpremultiply(px);
                    float d = dither::bayer4x4[span.y & 3][(x + i) & 3];
                    uint8_t dr = static_cast<uint8_t>(std::clamp(float(pr) + d, 0.0f, 255.0f));
                    uint8_t dg = static_cast<uint8_t>(std::clamp(float(pg) + d, 0.0f, 255.0f));
                    uint8_t db = static_cast<uint8_t>(std::clamp(float(pb) + d, 0.0f, 255.0f));
                    if (pa != 255) {
                        dr = static_cast<uint8_t>((uint32_t(dr) * pa) / 255);
                        dg = static_cast<uint8_t>((uint32_t(dg) * pa) / 255);
                        db = static_cast<uint8_t>((uint32_t(db) * pa) / 255);
                    }
                    buffer[i] = pack_argb(pa, dr, dg, db);
                }
            }
            if (use_mode)
                apply_blend_mode_buffer(buffer, target, l, opts.blend_mode);
            func(target, l, buffer, span.coverage);
            x += l;
            length -= l;
        }
    }
}

static void blend_texture(const SurfaceRef& surface, Operator op, const TexturePaintData& texture,
                          const Matrix& ctm, float opacity, const SpanBuffer& span_buffer,
                          BlendMode mode = BlendMode::Normal)
{
    auto tex = build_texture_data(texture, ctm, opacity);
    if (!tex) return;

    bool use_mode = (mode != BlendMode::Normal);
    const auto& m = tex->matrix;
    bool is_untransformed = (m.a == 1 && m.b == 0 && m.c == 0 && m.d == 1);

    if (!use_mode && is_untransformed) {
        // Fast path: direct scanline blit, no intermediate buffer.
        if (texture.type == TextureType::Plain)
            blend_untransformed_argb(surface, op, *tex, span_buffer);
        else
            blend_untransformed_tiled_argb(surface, op, *tex, span_buffer);
        return;
    }

    // Transformed or blend-mode path: fetch → [blend mode] → compose.
    auto fetch = select_texture_fetch(texture.type, tex->matrix);
    Operator comp_op = use_mode ? Operator::SrcOver : op;
    auto func = composition_table[static_cast<int>(comp_op)];
    uint32_t buffer[kBufferSize];

    for (const auto& span : span_buffer.spans) {
        auto* target = reinterpret_cast<uint32_t*>(surface.data + span.y * surface.stride) + span.x;
        int coverage = use_mode ? span.coverage : ((span.coverage * tex->const_alpha) >> 8);
        int length = span.len;
        int x = span.x;
        while (length) {
            int l = std::min(length, kBufferSize);
            fetch(buffer, *tex, span.y, x, l);
            if (use_mode)
                apply_blend_mode_buffer(buffer, target, l, mode);
            func(target, l, buffer, coverage);
            target += l;
            x += l;
            length -= l;
        }
    }
}

// Dispatch blend for a given paint Impl

static void dispatch_blend(const SurfaceRef& surf, const Paint::Impl* impl,
                           Operator op, const Matrix& ctm, float opacity,
                           const Color& fallback_color, const SpanBuffer& span_buffer,
                           const BlendOpts& opts = {})
{
    if (!impl) {
        blend_color(surf, op, fallback_color, opacity, span_buffer, opts.blend_mode);
        return;
    }

    switch (impl->type()) {
    case PaintType::Color:
        blend_color(surf, op, impl->as_solid().color, opacity, span_buffer, opts.blend_mode);
        break;
    case PaintType::Gradient:
        blend_gradient(surf, op, impl->as_gradient(), ctm, opacity, span_buffer, opts);
        break;
    case PaintType::Texture:
        blend_texture(surf, op, impl->as_texture(), ctm, opacity, span_buffer, opts.blend_mode);
        break;
    }
}

// Masked blend: composites source paint onto target, modulated by mask coverage at each pixel.
// Works for all paint types: solid, gradient, texture.

static void blend_masked_solid(const SurfaceRef& surf, Operator op, uint32_t solid,
                               const SpanBuffer& span_buffer,
                               const uint32_t* mask_pixels, int mask_w, int mask_h, int mask_stride_px,
                               MaskMode mode, int mask_ox, int mask_oy,
                               BlendMode blend_mode = BlendMode::Normal)
{
    if (blend_mode != BlendMode::Normal) {
        // Blend mode path: fill buffer with blended solid, then per-pixel masked composite.
        auto func = composition_table[static_cast<int>(Operator::SrcOver)];
        uint32_t buffer[kBufferSize];
        for (const auto& span : span_buffer.spans) {
            int my = span.y - mask_oy;
            if (my < 0 || my >= mask_h)
                continue;

            auto* dest = reinterpret_cast<uint32_t*>(surf.data + span.y * surf.stride) + span.x;
            const uint32_t* mask_row = mask_pixels + my * mask_stride_px;

            int length = span.len;
            int x_off = 0;
            while (length) {
                int l = std::min(length, kBufferSize);
                auto* d = dest + x_off;
                apply_blend_mode_solid(solid, d, buffer, l, blend_mode);
                for (int i = 0; i < l; ++i) {
                    int mx = span.x + x_off + i - mask_ox;
                    if (mx < 0 || mx >= mask_w)
                        continue;
                    uint8_t mask_cov = mask_ops::extract_coverage(mask_row[mx], mode);
                    uint32_t combined = (span.coverage * mask_cov) / 255;
                    if (combined == 0)
                        continue;
                    func(d + i, 1, &buffer[i], combined);
                }
                x_off += l;
                length -= l;
            }
        }
        return;
    }

    // Normal blend mode: direct solid compositing (fast path).
    auto func = composition_solid_table[static_cast<int>(op)];
    for (const auto& span : span_buffer.spans) {
        int my = span.y - mask_oy;
        if (my < 0 || my >= mask_h)
            continue;

        auto* dest = reinterpret_cast<uint32_t*>(surf.data + span.y * surf.stride) + span.x;
        const uint32_t* mask_row = mask_pixels + my * mask_stride_px;

        for (int i = 0; i < span.len; ++i) {
            int mx = span.x + i - mask_ox;
            if (mx < 0 || mx >= mask_w)
                continue;
            uint8_t mask_cov = mask_ops::extract_coverage(mask_row[mx], mode);
            uint32_t combined = (span.coverage * mask_cov) / 255;
            if (combined == 0)
                continue;
            func(dest + i, 1, solid, combined);
        }
    }
}

static void blend_masked_source([[maybe_unused]] const SurfaceRef& surf, Operator op,
                                const uint32_t* src_row, [[maybe_unused]] int src_x,
                                uint32_t* dest, int length,
                                uint8_t span_coverage,
                                const uint32_t* mask_row, int mask_x0, int mask_w,
                                MaskMode mode)
{
    auto func = composition_table[static_cast<int>(op)];
    // Per-pixel: modulate source by mask coverage, then composite.
    // We write into a temp so we can use the batched composition function
    // with per-pixel const_alpha.
    for (int i = 0; i < length; ++i) {
        int mx = mask_x0 + i;
        uint8_t mask_cov = 0;
        if (mx >= 0 && mx < mask_w)
            mask_cov = mask_ops::extract_coverage(mask_row[mx], mode);
        uint32_t combined = (span_coverage * mask_cov) / 255;
        if (combined == 0)
            continue;
        func(dest + i, 1, &src_row[i], combined);
    }
}

static void blend_masked_gradient(const SurfaceRef& surf, Operator op,
                                  const GradientPaintData& gradient, const Matrix& ctm,
                                  float opacity, const SpanBuffer& span_buffer,
                                  const uint32_t* mask_pixels, int mask_w, int mask_h, int mask_stride_px,
                                  MaskMode mode, int mask_ox, int mask_oy,
                                  const BlendOpts& opts = {})
{
    if (gradient.stops.empty())
        return;

    GradientData data = build_gradient_data(gradient, ctm, opacity, opts.color_interp);
    auto inv = data.matrix.inverted();
    if (!inv) return;
    data.matrix = *inv;

    bool use_mode = (opts.blend_mode != BlendMode::Normal);
    Operator comp_op = use_mode ? Operator::SrcOver : op;

    uint32_t buffer[kBufferSize];

    for (const auto& span : span_buffer.spans) {
        int my = span.y - mask_oy;
        if (my < 0 || my >= mask_h)
            continue;

        const uint32_t* mask_row = mask_pixels + my * mask_stride_px;
        auto* dest = reinterpret_cast<uint32_t*>(surf.data + span.y * surf.stride) + span.x;

        int length = span.len;
        int x = span.x;
        while (length) {
            int l = std::min(length, kBufferSize);
            fetch_gradient_row(buffer, gradient, data, span.y, x, l);

            if (opts.dithering) {
                for (int i = 0; i < l; ++i) {
                    uint32_t px = buffer[i];
                    uint8_t pa = alpha(px);
                    if (pa == 0) continue;
                    auto [pr, pg, pb] = unpremultiply(px);
                    float d = dither::bayer4x4[span.y & 3][(x + i) & 3];
                    uint8_t dr = static_cast<uint8_t>(std::clamp(float(pr) + d, 0.0f, 255.0f));
                    uint8_t dg = static_cast<uint8_t>(std::clamp(float(pg) + d, 0.0f, 255.0f));
                    uint8_t db = static_cast<uint8_t>(std::clamp(float(pb) + d, 0.0f, 255.0f));
                    if (pa != 255) {
                        dr = static_cast<uint8_t>((uint32_t(dr) * pa) / 255);
                        dg = static_cast<uint8_t>((uint32_t(dg) * pa) / 255);
                        db = static_cast<uint8_t>((uint32_t(db) * pa) / 255);
                    }
                    buffer[i] = pack_argb(pa, dr, dg, db);
                }
            }

            if (use_mode)
                apply_blend_mode_buffer(buffer, dest, l, opts.blend_mode);

            blend_masked_source(surf, comp_op, buffer, 0,
                                dest, l, span.coverage,
                                mask_row, x - mask_ox, mask_w, mode);
            dest += l;
            x += l;
            length -= l;
        }
    }
}

static void blend_masked_texture(const SurfaceRef& surf, Operator op,
                                 const TexturePaintData& texture, const Matrix& ctm,
                                 float opacity, const SpanBuffer& span_buffer,
                                 const uint32_t* mask_pixels, int mask_w, int mask_h, int mask_stride_px,
                                 MaskMode mode, int mask_ox, int mask_oy,
                                 BlendMode blend_mode = BlendMode::Normal)
{
    auto tex = build_texture_data(texture, ctm, opacity);
    if (!tex) return;

    bool use_mode = (blend_mode != BlendMode::Normal);
    Operator comp_op = use_mode ? Operator::SrcOver : op;
    auto fetch = select_texture_fetch(texture.type, tex->matrix);

    uint32_t buffer[kBufferSize];

    for (const auto& span : span_buffer.spans) {
        int my = span.y - mask_oy;
        if (my < 0 || my >= mask_h)
            continue;

        const uint32_t* mask_row = mask_pixels + my * mask_stride_px;
        auto* dest = reinterpret_cast<uint32_t*>(surf.data + span.y * surf.stride) + span.x;

        int length = span.len;
        int x = span.x;
        while (length) {
            int l = std::min(length, kBufferSize);
            fetch(buffer, *tex, span.y, x, l);

            if (use_mode)
                apply_blend_mode_buffer(buffer, dest, l, blend_mode);

            blend_masked_source(surf, comp_op, buffer, 0,
                                dest, l, span.coverage,
                                mask_row, x - mask_ox, mask_w, mode);
            dest += l;
            x += l;
            length -= l;
        }
    }
}

// Public blend API

void blend(Canvas::Impl& canvas, const SpanBuffer& span_buffer)
{
    if (span_buffer.spans.empty())
        return;

    SurfaceRef surf(canvas.surface);
    const auto& st = canvas.state();

    BlendOpts opts;
    opts.blend_mode  = st.blend_mode;
    opts.color_interp = st.color_interp;
    opts.dithering   = st.dithering;

    dispatch_blend(surf, paint_impl(st.fill_paint),
                   st.op, st.matrix, st.opacity,
                   st.fill_color, span_buffer, opts);
}

void blend(const BlendParams& params, const SpanBuffer& span_buffer,
           const IntRect& clip_rect, const SpanBuffer* clip_spans)
{
    if (span_buffer.spans.empty())
        return;

    // Step 1: intersect with clip_spans (path-based clipping).
    SpanBuffer intersected;
    const SpanBuffer* source = &span_buffer;

    if (clip_spans && !clip_spans->spans.empty()) {
        span_buffer_intersect(intersected, span_buffer, *clip_spans);
        source = &intersected;
        if (source->spans.empty())
            return;
    }

    // Step 2: clip to rectangular clip_rect.
    SpanBuffer clipped;
    if (!clip_rect.empty()) {
        clip_spans_to_rect(*source, clip_rect, clipped);
        source = &clipped;
        if (source->spans.empty())
            return;
    }

    // Step 3: blend.
    SurfaceRef surf(params.target);

    BlendOpts opts;
    opts.blend_mode  = params.blend_mode;
    opts.color_interp = params.color_interp;
    opts.dithering   = params.dithering;

    dispatch_blend(surf, params.paint,
                   params.op, Matrix::identity(), params.opacity,
                   Color::black(), *source, opts);
}

void blend_masked(const BlendParams& params, const SpanBuffer& span_buffer,
                  const IntRect& clip_rect, const SpanBuffer* clip_spans,
                  const Surface& mask, MaskMode mode, int mask_ox, int mask_oy)
{
    if (span_buffer.spans.empty() || !params.paint)
        return;

    // Apply clipping to spans first.
    SpanBuffer intersected;
    const SpanBuffer* source = &span_buffer;

    if (clip_spans && !clip_spans->spans.empty()) {
        span_buffer_intersect(intersected, span_buffer, *clip_spans);
        source = &intersected;
        if (source->spans.empty())
            return;
    }

    SpanBuffer clipped;
    if (!clip_rect.empty()) {
        clip_spans_to_rect(*source, clip_rect, clipped);
        source = &clipped;
        if (source->spans.empty())
            return;
    }

    auto& target = params.target;
    SurfaceRef surf(target);

    const auto* mask_pixels = reinterpret_cast<const uint32_t*>(mask.data());
    int mask_w = mask.width();
    int mask_h = mask.height();
    int mask_stride_px = mask.stride() / 4;

    BlendOpts opts;
    opts.blend_mode  = params.blend_mode;
    opts.color_interp = params.color_interp;
    opts.dithering   = params.dithering;

    switch (params.paint->type()) {
    case PaintType::Color: {
        uint32_t solid = premultiply_color_with_opacity(params.paint->as_solid().color, params.opacity);
        blend_masked_solid(surf, params.op, solid, *source,
                           mask_pixels, mask_w, mask_h, mask_stride_px,
                           mode, mask_ox, mask_oy, opts.blend_mode);
        break;
    }
    case PaintType::Gradient:
        blend_masked_gradient(surf, params.op, params.paint->as_gradient(),
                              Matrix::identity(), params.opacity, *source,
                              mask_pixels, mask_w, mask_h, mask_stride_px,
                              mode, mask_ox, mask_oy, opts);
        break;
    case PaintType::Texture:
        blend_masked_texture(surf, params.op, params.paint->as_texture(),
                             Matrix::identity(), params.opacity, *source,
                             mask_pixels, mask_w, mask_h, mask_stride_px,
                             mode, mask_ox, mask_oy, opts.blend_mode);
        break;
    }
}

} // namespace plutovg
