// kvgblend.cpp — Core blend/composite rendering engine
// C++20

#include "kvgprivate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace kvg {

namespace {

// sRGB<->linear LUT, lazily initialized (thread-safe)
static color_space_util::LUT s_srgb_lut;
static std::once_flag s_lut_flag;

inline const color_space_util::LUT& srgb_lut() {
    std::call_once(s_lut_flag, [] { s_srgb_lut.init(); });
    return s_srgb_lut;
}

// Gradient color sampling.
// Delegates to sample_gradient_argb() (kvgprivate.hpp) for spread / interpolation,
// then converts result to premultiplied ARGB32 for the blend pipeline.

inline uint32_t sample_gradient(const Gradient::Impl* grad, float t) {
    return sample_gradient_argb(grad, t);
}

// Bilinear lerp between two premultiplied ARGB pixels.
// frac is in [0, 256].
inline uint32_t bilerp_argb(uint32_t a, uint32_t b, uint32_t frac) {
    if (frac == 0)   return a;
    if (frac == 256) return b;
    uint32_t inv = 256 - frac;
    // Process RB and AG pairs in parallel (no overflow: each channel * 256 fits in 24 bits)
    uint32_t rb = (((a & 0x00FF00FF) * inv + (b & 0x00FF00FF) * frac) >> 8) & 0x00FF00FF;
    uint32_t ag = ((((a >> 8) & 0x00FF00FF) * inv + ((b >> 8) & 0x00FF00FF) * frac) >> 8) & 0x00FF00FF;
    return (ag << 8) | rb;
}

// Paint source: generate premultiplied ARGB for a pixel at (px, py)

inline uint32_t generate_color(const PaintSource& paint, int px, int py) {
    switch (paint.kind) {
    case PaintKind::Solid:
        return paint.solid_argb;

    case PaintKind::LinearGradient: {
        float x1 = paint.grad_values[0], y1 = paint.grad_values[1];
        float x2 = paint.grad_values[2], y2 = paint.grad_values[3];

        Point gp = paint.grad_inv_valid
            ? paint.grad_inv_transform.apply(Point(float(px) + 0.5f, float(py) + 0.5f))
            : Point(float(px) + 0.5f, float(py) + 0.5f);

        float dx = x2 - x1, dy = y2 - y1;
        float len_sq = dx * dx + dy * dy;
        if (len_sq < 1e-10f)
            return gradient_pixel(paint.gradient, 0.0f);

        float t = ((gp.x - x1) * dx + (gp.y - y1) * dy) / len_sq;
        return gradient_pixel(paint.gradient, t);
    }

    case PaintKind::RadialGradient: {
        float cx = paint.grad_values[0], cy = paint.grad_values[1], cr = paint.grad_values[2];
        float fx = paint.grad_values[3], fy = paint.grad_values[4], fr = paint.grad_values[5];

        Point gp = paint.grad_inv_valid
            ? paint.grad_inv_transform.apply(Point(float(px) + 0.5f, float(py) + 0.5f))
            : Point(float(px) + 0.5f, float(py) + 0.5f);

        float dx = gp.x - fx, dy = gp.y - fy;
        float cdx = cx - fx, cdy = cy - fy;
        float dr = cr - fr;

        float a = cdx * cdx + cdy * cdy - dr * dr;
        float b = 2.0f * (dx * cdx + dy * cdy + fr * dr);
        float c = dx * dx + dy * dy - fr * fr;

        float t = 0.0f;
        if (std::abs(a) < 1e-10f) {
            if (std::abs(b) > 1e-10f) t = -c / b;
        } else {
            float disc = b * b - 4.0f * a * c;
            if (disc >= 0.0f) {
                float sq = std::sqrt(disc);
                float t1 = (-b + sq) / (2.0f * a);
                float t2 = (-b - sq) / (2.0f * a);
                bool v1 = (fr + t1 * dr >= 0.0f), v2 = (fr + t2 * dr >= 0.0f);
                if (v1 && v2) t = (t1 > t2) ? t1 : t2;
                else if (v1) t = t1;
                else if (v2) t = t2;
            }
        }
        return gradient_pixel(paint.gradient, t);
    }

    case PaintKind::ConicGradient: {
        float cx = paint.grad_values[0], cy = paint.grad_values[1];
        float start_angle = paint.grad_values[2];

        Point gp = paint.grad_inv_valid
            ? paint.grad_inv_transform.apply(Point(float(px) + 0.5f, float(py) + 0.5f))
            : Point(float(px) + 0.5f, float(py) + 0.5f);

        float angle = std::atan2(gp.y - cy, gp.x - cx) - start_angle;
        float t = angle / two_pi;
        t = std::fmod(t, 1.0f);
        if (t < 0.0f) t += 1.0f;
        return gradient_pixel(paint.gradient, t);
    }

    case PaintKind::Texture: {
        if (!paint.image) return 0;

        auto inv = paint.tex_transform.inverted();
        Point tp = inv ? inv->apply(Point(float(px) + 0.5f, float(py) + 0.5f))
                       : Point(float(px) + 0.5f, float(py) + 0.5f);

        int w = paint.image->width;
        int h = paint.image->height;
        PixelFormat fmt = paint.image->format;

        // Helper: read one texel, handling wrap (pattern) or clamp (single image).
        auto read_texel = [&](int ix, int iy) -> uint32_t {
            if (paint.pattern) {
                // Wrap — same logic as the old nearest-neighbour tiled path.
                ix = ix % w; if (ix < 0) ix += w;
                iy = iy % h; if (iy < 0) iy += h;
            } else {
                // Clamp to transparent outside bounds.
                if (ix < 0 || iy < 0 || ix >= w || iy >= h) return 0;
            }
            if (fmt == PixelFormat::A8) {
                uint8_t a8 = paint.image->data[iy * paint.image->stride + ix];
                return pack_argb(a8, 0, 0, 0);
            }
            const auto* row = reinterpret_cast<const uint32_t*>(
                paint.image->data + iy * paint.image->stride);
            return pixel_to_argb_premul(row[ix], fmt);
        };

        uint32_t pixel;

        if (paint.interpolation == InterpolationQuality::None) {
            // Nearest neighbor: single texel, no filtering.
            int ix = static_cast<int>(std::floor(tp.x));
            int iy = static_cast<int>(std::floor(tp.y));
            pixel = read_texel(ix, iy);
        } else {
            // Bilinear interpolation over the four surrounding texels.
            // Shift to texel-centre space: pixel (i,j) covers [i, i+1) in image coords.
            // Subtracting 0.5 maps the pixel centre to the nearest texel centre.
            float tx = tp.x - 0.5f;
            float ty = tp.y - 0.5f;

            int x0 = static_cast<int>(std::floor(tx));
            int y0 = static_cast<int>(std::floor(ty));
            int x1 = x0 + 1;
            int y1 = y0 + 1;

            // Fixed-point fractions in [0, 256].
            uint32_t fx = static_cast<uint32_t>((tx - static_cast<float>(x0)) * 256.0f + 0.5f);
            uint32_t fy = static_cast<uint32_t>((ty - static_cast<float>(y0)) * 256.0f + 0.5f);
            if (fx > 256) fx = 256;
            if (fy > 256) fy = 256;

            uint32_t p00 = read_texel(x0, y0);
            uint32_t p10 = read_texel(x1, y0);
            uint32_t p01 = read_texel(x0, y1);
            uint32_t p11 = read_texel(x1, y1);

            uint32_t top    = bilerp_argb(p00, p10, fx);
            uint32_t bottom = bilerp_argb(p01, p11, fx);
            pixel = bilerp_argb(top, bottom, fy);
        }

        if (paint.tex_opacity < 1.0f) {
            uint32_t op8 = static_cast<uint32_t>(paint.tex_opacity * 255.0f + 0.5f);
            pixel = byte_mul(pixel, op8);
        }
        return pixel;
    }

    case PaintKind::PatternDraw: {
        // PatternDraw is pre-resolved to Texture by render_pattern_tile()
        // before the span loop. If we get here, something went wrong —
        // return transparent as a safe fallback.
        return 0;
    }

    case PaintKind::Shading: {
        if (!paint.shading || !paint.shading->eval) return 0;

        Point gp = paint.grad_inv_valid
            ? paint.grad_inv_transform.apply(Point(float(px) + 0.5f, float(py) + 0.5f))
            : Point(float(px) + 0.5f, float(py) + 0.5f);

        float t = 0.0f;

        if (paint.shading->kind == Shading::Impl::Kind::Axial) {
            float x1 = paint.grad_values[0], y1 = paint.grad_values[1];
            float x2 = paint.grad_values[2], y2 = paint.grad_values[3];

            float dx = x2 - x1;
            float dy = y2 - y1;
            float len_sq = dx * dx + dy * dy;
            if (len_sq >= 1e-10f)
                t = ((gp.x - x1) * dx + (gp.y - y1) * dy) / len_sq;
        } else {
            // Radial: same geometry as RadialGradient
            float cx = paint.grad_values[0], cy = paint.grad_values[1], cr = paint.grad_values[2];
            float fx = paint.grad_values[3], fy = paint.grad_values[4], fr = paint.grad_values[5];

            float dx = gp.x - fx;
            float dy = gp.y - fy;
            float cdx = cx - fx;
            float cdy = cy - fy;
            float dr = cr - fr;

            float a = cdx * cdx + cdy * cdy - dr * dr;
            float b = 2.0f * (dx * cdx + dy * cdy + fr * dr);
            float c = dx * dx + dy * dy - fr * fr;

            if (std::abs(a) < 1e-10f) {
                if (std::abs(b) > 1e-10f)
                    t = -c / b;
            } else {
                float disc = b * b - 4.0f * a * c;
                if (disc >= 0.0f) {
                    float sq = std::sqrt(disc);
                    float t1 = (-b + sq) / (2.0f * a);
                    float t2 = (-b - sq) / (2.0f * a);
                    bool t1_valid = (fr + t1 * dr >= 0.0f);
                    bool t2_valid = (fr + t2 * dr >= 0.0f);
                    if (t1_valid && t2_valid)
                        t = (t1 > t2) ? t1 : t2;
                    else if (t1_valid)
                        t = t1;
                    else if (t2_valid)
                        t = t2;
                }
            }
        }

        // Respect extend flags: outside [0,1] return transparent unless extended
        if (t < 0.0f && !paint.shading->extend_start) return 0;
        if (t > 1.0f && !paint.shading->extend_end)   return 0;
        t = std::clamp(t, 0.0f, 1.0f);

        Color result;
        paint.shading->eval(paint.shading->eval_info, t, &result);
        return result.premultiplied().to_argb32();
    }
    }
    return 0;
}

// Porter-Duff composite factors
// Returns (fa, fb) such that result = src * fa + dst * fb
// All in premultiplied space.

struct CompFactors { float fa, fb; };

inline CompFactors composite_factors(CompositeOp op, float sa, float da) {
    switch (op) {
    case CompositeOp::Clear:           return {0.0f, 0.0f};
    case CompositeOp::Copy:            return {1.0f, 0.0f};
    case CompositeOp::SourceOver:      return {1.0f, 1.0f - sa};
    case CompositeOp::SourceIn:        return {da,   0.0f};
    case CompositeOp::SourceOut:       return {1.0f - da, 0.0f};
    case CompositeOp::SourceAtop:      return {da,   1.0f - sa};
    case CompositeOp::DestinationOver: return {1.0f - da, 1.0f};
    case CompositeOp::DestinationIn:   return {0.0f, sa};
    case CompositeOp::DestinationOut:  return {0.0f, 1.0f - sa};
    case CompositeOp::DestinationAtop: return {1.0f - da, sa};
    case CompositeOp::Xor:            return {1.0f - da, 1.0f - sa};
    }
    return {1.0f, 1.0f - sa}; // SourceOver fallback
}

// Integer Porter-Duff factors scaled to 0..255

struct CompFactors8 { uint32_t fa, fb; };

inline CompFactors8 composite_factors_8(CompositeOp op, uint32_t sa, uint32_t da) {
    switch (op) {
    case CompositeOp::Clear:           return {0, 0};
    case CompositeOp::Copy:            return {255, 0};
    case CompositeOp::SourceOver:      return {255, 255 - sa};
    case CompositeOp::SourceIn:        return {da, 0};
    case CompositeOp::SourceOut:       return {255 - da, 0};
    case CompositeOp::SourceAtop:      return {da, 255 - sa};
    case CompositeOp::DestinationOver: return {255 - da, 255};
    case CompositeOp::DestinationIn:   return {0, sa};
    case CompositeOp::DestinationOut:  return {0, 255 - sa};
    case CompositeOp::DestinationAtop: return {255 - da, sa};
    case CompositeOp::Xor:            return {255 - da, 255 - sa};
    }
    return {255, 255 - sa};
}

// Apply blend mode to unpremultiplied RGB components.
// src and dst are straight (non-premultiplied) RGB.

inline void apply_blend_mode(BlendMode mode,
                             uint8_t sr, uint8_t sg, uint8_t sb,
                             uint8_t dr, uint8_t dg, uint8_t db,
                             uint8_t& or_, uint8_t& og, uint8_t& ob) {
    using namespace blend_ops;
    using namespace hsl_blend_ops;

    switch (mode) {
    case BlendMode::Normal:
        or_ = sr; og = sg; ob = sb;
        return;
    case BlendMode::Multiply:
        or_ = multiply(sr, dr); og = multiply(sg, dg); ob = multiply(sb, db);
        return;
    case BlendMode::Screen:
        or_ = screen(sr, dr); og = screen(sg, dg); ob = screen(sb, db);
        return;
    case BlendMode::Overlay:
        or_ = overlay(dr, sr); og = overlay(dg, sg); ob = overlay(db, sb);
        return;
    case BlendMode::Darken:
        or_ = darken(sr, dr); og = darken(sg, dg); ob = darken(sb, db);
        return;
    case BlendMode::Lighten:
        or_ = lighten(sr, dr); og = lighten(sg, dg); ob = lighten(sb, db);
        return;
    case BlendMode::ColorDodge:
        or_ = color_dodge(dr, sr); og = color_dodge(dg, sg); ob = color_dodge(db, sb);
        return;
    case BlendMode::ColorBurn:
        or_ = color_burn(dr, sr); og = color_burn(dg, sg); ob = color_burn(db, sb);
        return;
    case BlendMode::HardLight:
        or_ = hard_light(dr, sr); og = hard_light(dg, sg); ob = hard_light(db, sb);
        return;
    case BlendMode::SoftLight:
        or_ = soft_light(dr, sr); og = soft_light(dg, sg); ob = soft_light(db, sb);
        return;
    case BlendMode::Difference:
        or_ = difference(sr, dr); og = difference(sg, dg); ob = difference(sb, db);
        return;
    case BlendMode::Exclusion:
        or_ = exclusion(sr, dr); og = exclusion(sg, dg); ob = exclusion(sb, db);
        return;
    case BlendMode::Hue:
        hue(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Saturation:
        saturation(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Color:
        color(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Luminosity:
        luminosity(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Add:
        or_ = static_cast<uint8_t>(std::min(255u, uint32_t(sr) + dr));
        og = static_cast<uint8_t>(std::min(255u, uint32_t(sg) + dg));
        ob = static_cast<uint8_t>(std::min(255u, uint32_t(sb) + db));
        return;
    }
    or_ = sr; og = sg; ob = sb;
}

// Apply blend mode to unpremultiplied float RGB components in [0,1].
// Used by the linear-space pipeline to avoid uint8 quantisation round-trips.

inline void apply_blend_mode_f(BlendMode mode,
                               float sr, float sg, float sb,
                               float dr, float dg, float db,
                               float& or_, float& og, float& ob) {
    using namespace blend_ops_f;
    using namespace hsl_blend_ops_f;

    switch (mode) {
    case BlendMode::Normal:
        or_ = sr; og = sg; ob = sb;
        return;
    case BlendMode::Multiply:
        or_ = multiply(sr, dr); og = multiply(sg, dg); ob = multiply(sb, db);
        return;
    case BlendMode::Screen:
        or_ = screen(sr, dr); og = screen(sg, dg); ob = screen(sb, db);
        return;
    case BlendMode::Overlay:
        or_ = overlay(dr, sr); og = overlay(dg, sg); ob = overlay(db, sb);
        return;
    case BlendMode::Darken:
        or_ = darken(sr, dr); og = darken(sg, dg); ob = darken(sb, db);
        return;
    case BlendMode::Lighten:
        or_ = lighten(sr, dr); og = lighten(sg, dg); ob = lighten(sb, db);
        return;
    case BlendMode::ColorDodge:
        or_ = color_dodge(dr, sr); og = color_dodge(dg, sg); ob = color_dodge(db, sb);
        return;
    case BlendMode::ColorBurn:
        or_ = color_burn(dr, sr); og = color_burn(dg, sg); ob = color_burn(db, sb);
        return;
    case BlendMode::HardLight:
        or_ = hard_light(dr, sr); og = hard_light(dg, sg); ob = hard_light(db, sb);
        return;
    case BlendMode::SoftLight:
        or_ = soft_light(dr, sr); og = soft_light(dg, sg); ob = soft_light(db, sb);
        return;
    case BlendMode::Difference:
        or_ = difference(sr, dr); og = difference(sg, dg); ob = difference(sb, db);
        return;
    case BlendMode::Exclusion:
        or_ = exclusion(sr, dr); og = exclusion(sg, dg); ob = exclusion(sb, db);
        return;
    case BlendMode::Hue:
        hue(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Saturation:
        saturation(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Color:
        color(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Luminosity:
        luminosity(sr, sg, sb, dr, dg, db, or_, og, ob);
        return;
    case BlendMode::Add:
        or_ = add(sr, dr); og = add(sg, dg); ob = add(sb, db);
        return;
    }
    or_ = sr; og = sg; ob = sb;
}

// Complete per-pixel composite+blend in sRGB space (fast path)
//
// src_premul and dst_premul are premultiplied ARGB32.
// Returns premultiplied ARGB32 result.

inline uint32_t composite_pixel_srgb(uint32_t src_premul, uint32_t dst_premul,
                                     CompositeOp op, BlendMode blend) {
    uint32_t sa = pixel_alpha(src_premul);
    uint32_t da = pixel_alpha(dst_premul);

    // Fast path: Normal blend + SourceOver
    if (blend == BlendMode::Normal && op == CompositeOp::SourceOver) {
        if (sa == 255) return src_premul;
        if (sa == 0)   return dst_premul;
        // dst' = src + dst * (1 - sa)
        uint32_t inv_sa = 255 - sa;
        uint32_t t = byte_mul(dst_premul, inv_sa);
        // Add src + t component-wise with saturation
        uint32_t rb = ((src_premul & 0xFF00FF) + (t & 0xFF00FF));
        uint32_t ag = ((src_premul >> 8) & 0xFF00FF) + ((t >> 8) & 0xFF00FF);
        // No clamping needed: premul guarantees components ≤ alpha,
        // and sa + da*(1-sa/255) ≤ 255
        return (ag << 8 & 0xFF00FF00) | (rb & 0x00FF00FF);
    }

    // Fast path: Normal blend, any composite op
    if (blend == BlendMode::Normal) {
        auto [fa, fb] = composite_factors_8(op, sa, da);
        if (fa == 0 && fb == 0) return 0;
        if (fa == 255 && fb == 0) return src_premul;
        if (fa == 0 && fb == 255) return dst_premul;

        uint32_t s = (fa == 255) ? src_premul : byte_mul(src_premul, fa);
        uint32_t d = (fb == 255) ? dst_premul : byte_mul(dst_premul, fb);

        uint32_t rb = (s & 0xFF00FF) + (d & 0xFF00FF);
        uint32_t ag = ((s >> 8) & 0xFF00FF) + ((d >> 8) & 0xFF00FF);
        // Clamp
        rb = rb | (0x01000100 - ((rb >> 8) & 0xFF00FF));
        rb &= 0xFF00FF;
        ag = ag | (0x01000100 - ((ag >> 8) & 0xFF00FF));
        ag &= 0xFF00FF;
        return (ag << 8) | rb;
    }

    // Full path: non-Normal blend mode
    // 1. Unpremultiply both
    auto s = unpremultiply(src_premul);
    auto d = unpremultiply(dst_premul);

    // 2. Apply blend mode to get blended RGB (in sRGB straight)
    uint8_t br, bg, bb;
    apply_blend_mode(blend, s.r, s.g, s.b, d.r, d.g, d.b, br, bg, bb);

    // 3. Porter-Duff composite
    //    result_color = (1 - da) * src_color + da * blended_color    (for source part)
    //    then composite with destination using the PD factors
    float sa_f = sa / 255.0f;
    float da_f = da / 255.0f;
    auto [pfa, pfb] = composite_factors(op, sa_f, da_f);

    float out_a = std::min(1.0f, sa_f * pfa + da_f * pfb);
    if (out_a <= 0.0f)
        return 0;

    // The blended color replaces the source color in the PD formula
    // For separable blend modes: Cs' = (1-Da)*Cs + Da*B(Cs,Cd)
    // Then PD: Co = fa * Cs' + fb * Cd
    float brf = br / 255.0f, bgf = bg / 255.0f, bbf = bb / 255.0f;
    float srf = s.r / 255.0f, sgf = s.g / 255.0f, sbf = s.b / 255.0f;
    float drf = d.r / 255.0f, dgf = d.g / 255.0f, dbf = d.b / 255.0f;

    // Blended source: interpolate between original source and blended result by Da
    float cs_r = (1.0f - da_f) * srf + da_f * brf;
    float cs_g = (1.0f - da_f) * sgf + da_f * bgf;
    float cs_b = (1.0f - da_f) * sbf + da_f * bbf;

    float inv_out_a = 1.0f / out_a;
    float or_ = (sa_f * pfa * cs_r + da_f * pfb * drf) * inv_out_a;
    float og  = (sa_f * pfa * cs_g + da_f * pfb * dgf) * inv_out_a;
    float ob  = (sa_f * pfa * cs_b + da_f * pfb * dbf) * inv_out_a;

    // Convert back to premultiplied ARGB32
    uint8_t oa8 = static_cast<uint8_t>(std::clamp(out_a * 255.0f + 0.5f, 0.0f, 255.0f));
    uint8_t or8 = static_cast<uint8_t>(std::clamp(or_ * out_a * 255.0f + 0.5f, 0.0f, 255.0f));
    uint8_t og8 = static_cast<uint8_t>(std::clamp(og  * out_a * 255.0f + 0.5f, 0.0f, 255.0f));
    uint8_t ob8 = static_cast<uint8_t>(std::clamp(ob  * out_a * 255.0f + 0.5f, 0.0f, 255.0f));

    return pack_argb(oa8, or8, og8, ob8);
}

// Complete per-pixel composite+blend in linear sRGB space

inline uint32_t composite_pixel_linear(uint32_t src_premul, uint32_t dst_premul,
                                       CompositeOp op, BlendMode blend,
                                       bool do_dither, int px, int py) {
    auto& lut = srgb_lut();

    // Decode sRGB -> linear (on premultiplied channels)
    uint8_t sa8 = pixel_alpha(src_premul);
    uint8_t da8 = pixel_alpha(dst_premul);

    float sa = sa8 / 255.0f;
    float da = da8 / 255.0f;

    // For linear blending we need straight (unpremultiplied) linear RGB
    float s_lin_r, s_lin_g, s_lin_b;
    float d_lin_r, d_lin_g, d_lin_b;

    if (sa > 0.0f) {
        auto su = unpremultiply(src_premul);
        s_lin_r = lut[su.r]; s_lin_g = lut[su.g]; s_lin_b = lut[su.b];
    } else {
        s_lin_r = s_lin_g = s_lin_b = 0.0f;
    }

    if (da > 0.0f) {
        auto du = unpremultiply(dst_premul);
        d_lin_r = lut[du.r]; d_lin_g = lut[du.g]; d_lin_b = lut[du.b];
    } else {
        d_lin_r = d_lin_g = d_lin_b = 0.0f;
    }

    // Apply blend mode directly in float linear space.
    // All blend formulas operate on [0,1] floats — no quantisation.
    float br_lin, bg_lin, bb_lin;
    apply_blend_mode_f(blend,
                       s_lin_r, s_lin_g, s_lin_b,
                       d_lin_r, d_lin_g, d_lin_b,
                       br_lin, bg_lin, bb_lin);

    // Porter-Duff composite in linear space
    auto [pfa, pfb] = composite_factors(op, sa, da);
    float out_a = std::min(1.0f, sa * pfa + da * pfb);
    if (out_a <= 0.0f) return 0;

    // Blended source: (1-Da)*Cs + Da*B(Cs,Cd)
    float cs_r = (1.0f - da) * s_lin_r + da * br_lin;
    float cs_g = (1.0f - da) * s_lin_g + da * bg_lin;
    float cs_b = (1.0f - da) * s_lin_b + da * bb_lin;

    float inv_out_a = 1.0f / out_a;
    float or_lin = (sa * pfa * cs_r + da * pfb * d_lin_r) * inv_out_a;
    float og_lin = (sa * pfa * cs_g + da * pfb * d_lin_g) * inv_out_a;
    float ob_lin = (sa * pfa * cs_b + da * pfb * d_lin_b) * inv_out_a;

    // Linear -> sRGB
    uint8_t oa8 = static_cast<uint8_t>(std::clamp(out_a * 255.0f + 0.5f, 0.0f, 255.0f));
    uint8_t or8, og8, ob8;

    if (do_dither) {
        float r_srgb = color_space_util::linear_to_srgb_float(std::clamp(or_lin, 0.0f, 1.0f));
        float g_srgb = color_space_util::linear_to_srgb_float(std::clamp(og_lin, 0.0f, 1.0f));
        float b_srgb = color_space_util::linear_to_srgb_float(std::clamp(ob_lin, 0.0f, 1.0f));
        dither::apply_rgb(r_srgb, g_srgb, b_srgb, px, py, or8, og8, ob8);
    } else {
        or8 = color_space_util::linear_to_srgb(std::clamp(or_lin, 0.0f, 1.0f));
        og8 = color_space_util::linear_to_srgb(std::clamp(og_lin, 0.0f, 1.0f));
        ob8 = color_space_util::linear_to_srgb(std::clamp(ob_lin, 0.0f, 1.0f));
    }

    // Premultiply result
    if (oa8 < 255) {
        or8 = static_cast<uint8_t>((uint32_t(or8) * oa8 + 127) / 255);
        og8 = static_cast<uint8_t>((uint32_t(og8) * oa8 + 127) / 255);
        ob8 = static_cast<uint8_t>((uint32_t(ob8) * oa8 + 127) / 255);
    }

    return pack_argb(oa8, or8, og8, ob8);
}

} // namespace (anonymous)

// Dither in sRGB space (for sRGB working space with dithering enabled)

uint32_t dither_pixel_srgb(uint32_t premul, int px, int py) {
    auto u = unpremultiply_f(premul);
    if (u.a <= 0.0f) return 0;

    float r = u.r * 255.0f;
    float g = u.g * 255.0f;
    float b = u.b * 255.0f;

    uint8_t dr, dg, db;
    dither::apply_rgb(r, g, b, px, py, dr, dg, db);

    uint8_t a8 = pixel_alpha(premul);
    if (a8 < 255) {
        dr = static_cast<uint8_t>((uint32_t(dr) * a8 + 127) / 255);
        dg = static_cast<uint8_t>((uint32_t(dg) * a8 + 127) / 255);
        db = static_cast<uint8_t>((uint32_t(db) * a8 + 127) / 255);
    }
    return pack_argb(a8, dr, dg, db);
}

namespace {

// Row-indexed clip span access.
// Spans are sorted by y (from rasterizer). We build y -> [begin,end)
// index so per-pixel clip lookup is O(spans_in_row) not O(total_spans).

struct ClipRowIndex {
    int y_min = 0;
    int y_max = 0;
    std::vector<int> row_start; // row_start[y - y_min] = first span index for row y
    std::vector<int> row_end;   // row_end[y - y_min]   = one-past-last span index

    void build(const SpanBuffer& buf) {
        row_start.clear();
        row_end.clear();
        if (buf.spans.empty()) return;

        y_min = buf.spans.front().y;
        y_max = buf.spans.back().y + 1;
        int h = y_max - y_min;
        row_start.resize(h, 0);
        row_end.resize(h, 0);

        int n = static_cast<int>(buf.spans.size());
        int i = 0;
        while (i < n) {
            int y = buf.spans[i].y;
            int ri = y - y_min;
            row_start[ri] = i;
            while (i < n && buf.spans[i].y == y) ++i;
            row_end[ri] = i;
        }
    }

    bool empty() const { return row_start.empty(); }

    uint8_t coverage_at(const SpanBuffer& buf, int x, int y) const {
        if (y < y_min || y >= y_max) return 0;
        int ri = y - y_min;
        for (int i = row_start[ri]; i < row_end[ri]; ++i) {
            auto& s = buf.spans[i];
            if (x < s.x) return 0; // sorted by x, no point looking further
            if (x < s.x + s.len) return s.coverage;
        }
        return 0;
    }
};

// =========================================================================
//  Row-based gradient fetchers
//  Fill a scanline buffer with gradient colors, avoiding per-pixel overhead.
// =========================================================================

// Linear gradient: incremental t along scanline.
// Uses fixed-point when possible for maximum throughput.
void fetch_linear_row(uint32_t* buffer, const PaintSource& paint,
                      int y, int x, int length) {
    const auto* gi = paint.gradient;
    float x1 = paint.grad_values[0], y1 = paint.grad_values[1];
    float x2 = paint.grad_values[2], y2 = paint.grad_values[3];

    float gdx = x2 - x1, gdy = y2 - y1;
    float len_sq = gdx * gdx + gdy * gdy;

    if (len_sq < 1e-10f) {
        memfill32(buffer, length, gradient_pixel(gi, 0.0f));
        return;
    }

    float inv_len_sq = 1.0f / len_sq;

    // Transform first pixel through grad_inv_transform
    float px0 = float(x) + 0.5f, py = float(y) + 0.5f;
    float gx, gy;
    if (paint.grad_inv_valid) {
        const auto& m = paint.grad_inv_transform;
        gx = m.a * px0 + m.c * py + m.tx;
        gy = m.b * px0 + m.d * py + m.ty;
    } else {
        gx = px0;
        gy = py;
    }

    float t = ((gx - x1) * gdx + (gy - y1) * gdy) * inv_len_sq;
    // Per-pixel increment: transform step (1,0) -> (m.a, m.b)
    float inc;
    if (paint.grad_inv_valid) {
        const auto& m = paint.grad_inv_transform;
        inc = (m.a * gdx + m.b * gdy) * inv_len_sq;
    } else {
        inc = gdx * inv_len_sq;
    }

    // Scale to color table range
    float tbl = t * (kColorTableSize - 1);
    float inc_tbl = inc * (kColorTableSize - 1);

    const uint32_t* end = buffer + length;
    if (inc_tbl > -1e-5f && inc_tbl < 1e-5f) {
        // Constant-color row (gradient perpendicular to scanline)
        memfill32(buffer, length, gradient_pixel_fixed(gi,
            static_cast<int>(tbl * kFixptSize)));
    } else if (tbl + inc_tbl * length < float(INT_MAX >> (kFixptBits + 1))
            && tbl + inc_tbl * length > float(INT_MIN >> (kFixptBits + 1))) {
        // Fixed-point fast path
        int t_fixed = static_cast<int>(tbl * kFixptSize);
        int inc_fixed = static_cast<int>(inc_tbl * kFixptSize);
        while (buffer < end) {
            *buffer++ = gradient_pixel_fixed(gi, t_fixed);
            t_fixed += inc_fixed;
        }
    } else {
        // Float fallback (very large gradients)
        while (buffer < end) {
            *buffer++ = gradient_pixel(gi, tbl / (kColorTableSize - 1));
            tbl += inc_tbl;
        }
    }
}

// Radial gradient: incremental discriminant stepping.
// Pre-computes quadratic coefficients once per scanline, then steps
// through pixels with additions + one sqrt per pixel.
void fetch_radial_row(uint32_t* buffer, const PaintSource& paint,
                      int y, int x, int length) {
    const auto* gi = paint.gradient;
    float cx = paint.grad_values[0], cy = paint.grad_values[1], cr = paint.grad_values[2];
    float fx = paint.grad_values[3], fy = paint.grad_values[4], fr = paint.grad_values[5];

    float cdx = cx - fx, cdy = cy - fy;
    float dr = cr - fr;
    float a_coeff = cdx * cdx + cdy * cdy - dr * dr;

    // Transform first pixel through grad_inv_transform
    float px0 = float(x) + 0.5f, py = float(y) + 0.5f;
    float gx, gy;
    float step_x, step_y; // transform of (1, 0)
    if (paint.grad_inv_valid) {
        const auto& m = paint.grad_inv_transform;
        gx = m.a * px0 + m.c * py + m.tx;
        gy = m.b * px0 + m.d * py + m.ty;
        step_x = m.a;
        step_y = m.b;
    } else {
        gx = px0;
        gy = py;
        step_x = 1.0f;
        step_y = 0.0f;
    }

    float rx = gx - fx;
    float ry = gy - fy;

    if (std::abs(a_coeff) < 1e-10f) {
        // Degenerate case: per-pixel fallback
        for (int i = 0; i < length; ++i) {
            float dx = rx + float(i) * step_x;
            float dy = ry + float(i) * step_y;
            float b = 2.0f * (dx * cdx + dy * cdy + fr * dr);
            float c = dx * dx + dy * dy - fr * fr;
            float t = (std::abs(b) > 1e-10f) ? -c / b : 0.0f;
            buffer[i] = gradient_pixel(gi, t);
        }
        return;
    }

    float inv_a = 1.0f / (2.0f * a_coeff);

    float b_raw = 2.0f * (dr * fr + rx * cdx + ry * cdy);
    float delta_b_raw = 2.0f * (step_x * cdx + step_y * cdy);

    float bb = b_raw * b_raw;
    float db_sq = delta_b_raw * delta_b_raw;

    float b_scaled = b_raw * inv_a;
    float delta_b_scaled = delta_b_raw * inv_a;

    float rxrxryry = rx * rx + ry * ry;
    float step_sq = step_x * step_x + step_y * step_y;
    float rx_step = 2.0f * (rx * step_x + ry * step_y);

    float inv_a2 = inv_a * inv_a;

    // D(i) = b(i)^2 - 4*a*c(i);  det = D / (4a^2)
    float det = (bb - 4.0f * a_coeff * (rxrxryry - fr * fr)) * inv_a2;
    // delta_det = (2*b0*db + db^2 - 4*a*(rx_step + step_sq)) / (4a^2)
    float delta_det = (2.0f * b_raw * delta_b_raw + db_sq
                       - 4.0f * a_coeff * (rx_step + step_sq)) * inv_a2;
    // delta2_det = (2*db^2 - 8*a*step_sq) / (4a^2)
    float delta_delta_det = (2.0f * db_sq
                             - 8.0f * a_coeff * step_sq) * inv_a2;

    bool extended = (fr != 0.0f || dr != cr);

    for (int i = 0; i < length; ++i) {
        float w = 0.0f;
        if (det >= 0.0f) {
            float sq = std::sqrt(det);
            float w1 = sq - b_scaled;
            float w2 = -sq - b_scaled;
            bool v1 = !extended || (fr + dr * w1 >= 0.0f);
            bool v2 = !extended || (fr + dr * w2 >= 0.0f);
            if (v1 && v2)
                w = (w1 > w2) ? w1 : w2;
            else if (v1)
                w = w1;
            else if (v2)
                w = w2;
        }
        buffer[i] = gradient_pixel(gi, w);
        det += delta_det;
        delta_det += delta_delta_det;
        b_scaled += delta_b_scaled;
    }
}

// Conic gradient: atan2 per pixel (no incremental shortcut).
// LUT still provides the main speedup.
void fetch_conic_row(uint32_t* buffer, const PaintSource& paint,
                     int y, int x, int length) {
    const auto* gi = paint.gradient;
    float cx = paint.grad_values[0], cy = paint.grad_values[1];
    float start_angle = paint.grad_values[2];

    float px0 = float(x) + 0.5f, py = float(y) + 0.5f;
    float gx0, gy0;
    float step_x, step_y;
    if (paint.grad_inv_valid) {
        const auto& m = paint.grad_inv_transform;
        gx0 = m.a * px0 + m.c * py + m.tx;
        gy0 = m.b * px0 + m.d * py + m.ty;
        step_x = m.a;
        step_y = m.b;
    } else {
        gx0 = px0;
        gy0 = py;
        step_x = 1.0f;
        step_y = 0.0f;
    }

    float rx = gx0 - cx;
    float ry = gy0 - cy;

    for (int i = 0; i < length; ++i) {
        float angle = std::atan2(ry + float(i) * step_y,
                                 rx + float(i) * step_x) - start_angle;
        float t = angle / two_pi;
        t = std::fmod(t, 1.0f);
        if (t < 0.0f) t += 1.0f;
        buffer[i] = gradient_pixel(gi, t);
    }
}

// Specialized blend span for gradient + Normal + SourceOver + sRGB.
// Fetches a whole row of gradient colors via the appropriate row fetcher,
// then composites the row in a tight loop.
void blend_span_gradient_srcover(
        const BlendParams& params,
        const Span& span,
        const SpanBuffer* clip_spans,
        const ClipRowIndex* clip_idx,
        const IntRect& clip_rect,
        uint32_t* fetch_buf) {
    int y = span.y;
    if (y < clip_rect.y || y >= clip_rect.y + clip_rect.h) return;

    int x0 = std::max(span.x, clip_rect.x);
    int x1 = std::min(span.x + span.len, clip_rect.x + clip_rect.w);
    if (x0 >= x1) return;

    uint32_t coverage = span.coverage;
    if (coverage == 0) return;

    uint32_t global_opacity = static_cast<uint32_t>(
        std::clamp(params.opacity * 255.0f + 0.5f, 0.0f, 255.0f));

    int length = x1 - x0;

    // Fetch gradient colors for this row
    switch (params.paint->kind) {
    case PaintKind::LinearGradient:
        fetch_linear_row(fetch_buf, *params.paint, y, x0, length); break;
    case PaintKind::RadialGradient:
        fetch_radial_row(fetch_buf, *params.paint, y, x0, length); break;
    case PaintKind::ConicGradient:
        fetch_conic_row(fetch_buf, *params.paint, y, x0, length); break;
    default:
        return;
    }

    auto* row = reinterpret_cast<uint32_t*>(
        params.target_data + y * params.target_stride);

    for (int i = 0; i < length; ++i) {
        int px = x0 + i;

        uint8_t cc = (clip_idx && clip_spans)
            ? clip_idx->coverage_at(*clip_spans, px, y) : 255;
        if (cc == 0) continue;

        uint32_t src = fetch_buf[i];
        if (pixel_alpha(src) == 0) continue;

        // Apply coverage: span * clip * opacity
        uint32_t eff = (coverage * uint32_t(cc) * global_opacity + 32512) / 65025;
        if (eff == 0) continue;

        if (eff < 255)
            src = byte_mul(src, eff);

        uint32_t sa = pixel_alpha(src);
        if (sa == 0) continue;

        if (sa == 255) {
            row[px] = src;
        } else {
            uint32_t dst = row[px];
            uint32_t inv_sa = 255 - sa;
            uint32_t t = byte_mul(dst, inv_sa);
            uint32_t rb = ((src & 0xFF00FF) + (t & 0xFF00FF));
            uint32_t ag = ((src >> 8) & 0xFF00FF) + ((t >> 8) & 0xFF00FF);
            row[px] = (ag << 8 & 0xFF00FF00) | (rb & 0x00FF00FF);
        }
    }
}

// Core scanline blend function.
// Processes one span: reads paint colors, applies coverage, composites onto target.

void blend_span(const BlendParams& params,
                const Span& span,
                const SpanBuffer* clip_spans,
                const ClipRowIndex* clip_idx,
                const IntRect& clip_rect) {
    int y = span.y;
    if (y < clip_rect.y || y >= clip_rect.y + clip_rect.h)
        return;

    int x0 = std::max(span.x, clip_rect.x);
    int x1 = std::min(span.x + span.len, clip_rect.x + clip_rect.w);
    if (x0 >= x1) return;

    uint32_t coverage = span.coverage;
    if (coverage == 0) return;

    bool use_linear = is_linear_space(params.working_space);
    bool do_dither = params.dithering;

    // Opacity scaled coverage
    uint32_t global_opacity = static_cast<uint32_t>(
        std::clamp(params.opacity * 255.0f + 0.5f, 0.0f, 255.0f));

    auto* row = reinterpret_cast<uint32_t*>(
        params.target_data + y * params.target_stride);

    for (int px = x0; px < x1; ++px) {
        // Clip coverage
        uint8_t clip_cov = (clip_idx && clip_spans)
            ? clip_idx->coverage_at(*clip_spans, px, y) : 255;
        if (clip_cov == 0) continue;

        // Generate source color
        uint32_t src = generate_color(*params.paint, px, y);

        // Apply coverage: combine span coverage, clip coverage, global opacity
        uint32_t effective_cov = (coverage * clip_cov * global_opacity + 32512) / 65025;
        // 65025 = 255*255, 32512 = 65025/2 (round-to-nearest)
        if (effective_cov == 0) continue;

        if (effective_cov < 255) {
            src = byte_mul(src, effective_cov);
        }

        // Read destination
        uint32_t dst = pixel_to_argb_premul(row[px], params.target_format);

        // Composite
        uint32_t result;
        if (use_linear) {
            result = composite_pixel_linear(src, dst, params.op, params.blend_mode,
                                            do_dither, px, y);
        } else {
            result = composite_pixel_srgb(src, dst, params.op, params.blend_mode);
            if (do_dither)
                result = dither_pixel_srgb(result, px, y);
        }

        // Write back
        row[px] = pixel_from_argb_premul(result, params.target_format);
    }
}

// Optimized scanline for the common case:
// Solid color, Normal blend, SourceOver composite, sRGB space, no dithering.

void blend_span_solid_srcover(const BlendParams& params,
                              const Span& span,
                              const SpanBuffer* clip_spans,
                              const ClipRowIndex* clip_idx,
                              const IntRect& clip_rect) {
    int y = span.y;
    if (y < clip_rect.y || y >= clip_rect.y + clip_rect.h)
        return;

    int x0 = std::max(span.x, clip_rect.x);
    int x1 = std::min(span.x + span.len, clip_rect.x + clip_rect.w);
    if (x0 >= x1) return;

    uint32_t coverage = span.coverage;
    if (coverage == 0) return;

    uint32_t solid = params.paint->solid_argb;
    uint32_t global_opacity = static_cast<uint32_t>(
        std::clamp(params.opacity * 255.0f + 0.5f, 0.0f, 255.0f));

    // Early-exit check: compute effective coverage at full clip (cc=255)
    // using the same three-factor formula as the pixel loop.
    // 65025 = 255*255, 32512 = round(65025/2)
    uint32_t eff_cov_full = (coverage * 255u * global_opacity + 32512) / 65025;
    uint32_t src_probe = (eff_cov_full == 255) ? solid : byte_mul(solid, eff_cov_full);
    if (pixel_alpha(src_probe) == 0) return;

    auto* row = reinterpret_cast<uint32_t*>(
        params.target_data + y * params.target_stride);

    if (!clip_idx && eff_cov_full == 255 && pixel_alpha(solid) == 255 &&
        params.target_format == PixelFormat::ARGB32_Premultiplied) {
        // Fully opaque source, full coverage, no clip: overwrite the whole span.
        memfill32(row + x0, x1 - x0, solid);
        return;
    }

    for (int px = x0; px < x1; ++px) {
        uint8_t cc = (clip_idx && clip_spans)
            ? clip_idx->coverage_at(*clip_spans, px, y) : 255;
        if (cc == 0) continue;

        // Always use the three-factor formula (coverage * clip * opacity) to
        // match blend_span exactly and avoid rounding seams at clip boundaries.
        uint32_t full_cov = (coverage * uint32_t(cc) * global_opacity + 32512) / 65025;
        uint32_t actual_src = (full_cov == 255) ? solid : byte_mul(solid, full_cov);
        uint32_t actual_sa = pixel_alpha(actual_src);

        if (actual_sa == 0) continue;

        uint32_t dst = row[px];

        if (actual_sa == 255) {
            row[px] = actual_src;
        } else {
            uint32_t inv_a = 255 - actual_sa;
            uint32_t d = byte_mul(dst, inv_a);
            uint32_t rb = (actual_src & 0xFF00FF) + (d & 0xFF00FF);
            uint32_t ag = ((actual_src >> 8) & 0xFF00FF) + ((d >> 8) & 0xFF00FF);
            row[px] = (ag << 8 & 0xFF00FF00) | (rb & 0x00FF00FF);
        }
    }
}

// Resolve a PatternDraw paint into a Texture paint by rendering the pattern
// tile into a temporary image. For ImageBased patterns, just points at the
// source image directly (no render needed).
// Returns true if resolved (and modified_paint is filled in).

bool resolve_pattern_paint(const PaintSource& src, PaintSource& resolved,
                           Image& tile_storage) {
    if (src.kind != PaintKind::PatternDraw) return false;
    if (!src.pattern) return false;

    auto* pi = src.pattern;

    if (pi->kind == Pattern::Impl::Kind::ImageBased) {
        auto* img = image_impl(pi->source_image);
        if (!img || !img->data) return false;

        resolved = src;
        resolved.kind = PaintKind::Texture;
        resolved.image = img;
        resolved.tex_transform = pi->matrix * src.ctm;
        resolved.tex_opacity = 1.0f;
        resolved.pattern = pi; // signals tiled sampling
        return true;
    }

    // Colored pattern: render one tile via draw_func callback
    if (!pi->draw_func) return false;

    // Step determines the repeat distance between tiles.
    // Bounds determines the drawable area within a tile.
    float step_w = pi->x_step > 0.0f ? pi->x_step : pi->bounds.width();
    float step_h = pi->y_step > 0.0f ? pi->y_step : pi->bounds.height();

    int tw, th;
    float content_scale_x = 1.0f, content_scale_y = 1.0f;

    switch (pi->tiling) {
    case PatternTiling::NoDistortion:
        // Exact content dimensions, no scaling. Tile rounded up to device pixel.
        // Effective spacing may differ from requested by up to 1 pixel.
        tw = static_cast<int>(std::ceil(step_w));
        th = static_cast<int>(std::ceil(step_h));
        break;

    case PatternTiling::ConstantSpacingMinimalDistortion:
        // Round step to nearest pixel (minimal distortion).
        // Content scaled uniformly so step maps to integer tile.
        tw = std::max(1, static_cast<int>(std::round(step_w)));
        th = std::max(1, static_cast<int>(std::round(step_h)));
        if (step_w > 0.0f) content_scale_x = static_cast<float>(tw) / step_w;
        if (step_h > 0.0f) content_scale_y = static_cast<float>(th) / step_h;
        break;

    case PatternTiling::ConstantSpacing:
    default:
        // Ceil step to device pixel. Content scaled to fill tile exactly.
        tw = static_cast<int>(std::ceil(step_w));
        th = static_cast<int>(std::ceil(step_h));
        if (step_w > 0.0f) content_scale_x = static_cast<float>(tw) / step_w;
        if (step_h > 0.0f) content_scale_y = static_cast<float>(th) / step_h;
        break;
    }

    if (tw <= 0 || th <= 0) return false;

    // Create a temporary BitmapContext for the tile
    auto tile_ctx = BitmapContext::create(tw, th,
        PixelFormat::ARGB32_Premultiplied,
        ColorSpace::srgb(), ColorSpace::srgb());
    if (!tile_ctx) return false;

    tile_ctx.clear(Color::clear());

    // For tiling modes with distortion, scale content so step maps
    // to the integer tile. Applied before the bounds-origin translation
    // so the entire coordinate system (including bounds offset) scales.
    if (content_scale_x != 1.0f || content_scale_y != 1.0f)
        tile_ctx.scale_ctm(content_scale_x, content_scale_y);

    // Translate so pattern bounds origin maps to (0,0)
    tile_ctx.translate_ctm(-pi->bounds.x(), -pi->bounds.y());

    // Call the pattern draw callback
    pi->draw_func(pi->draw_info, tile_ctx);

    // Snapshot the rendered tile
    tile_storage = tile_ctx.to_image();
    auto* tile_impl = image_impl(tile_storage);
    if (!tile_impl || !tile_impl->data) return false;

    resolved = src;
    resolved.kind = PaintKind::Texture;
    resolved.image = tile_impl;
    resolved.tex_transform = pi->matrix * src.ctm;
    resolved.tex_opacity = 1.0f;
    resolved.pattern = pi; // signals tiled sampling
    return true;
}

} // anonymous namespace

uint32_t composite_pixel(uint32_t src_premul, uint32_t dst_premul,
                         CompositeOp op, BlendMode blend,
                         const ColorSpace* working_space) {
    if (working_space && is_linear_space(working_space))
        return composite_pixel_linear(src_premul, dst_premul, op, blend,
                                      false, 0, 0);
    return composite_pixel_srgb(src_premul, dst_premul, op, blend);
}

// -- Public API implementations --

void blend(const BlendParams& params, const SpanBuffer& span_buffer,
           const IntRect& clip_rect, const SpanBuffer* clip_spans) {
    if (span_buffer.spans.empty()) return;
    if (clip_rect.empty()) return;
    if (!params.paint) return;
    if (!params.target_data) return;

    // Resolve PatternDraw -> Texture before entering the span loop
    BlendParams effective = params;
    PaintSource resolved_paint;
    Image tile_storage;
    if (params.paint->kind == PaintKind::PatternDraw) {
        if (resolve_pattern_paint(*params.paint, resolved_paint, tile_storage))
            effective.paint = &resolved_paint;
        else
            return; // pattern couldn't be resolved
    }

    // Build row index for clip spans
    ClipRowIndex clip_idx;
    if (clip_spans && !clip_spans->spans.empty())
        clip_idx.build(*clip_spans);
    const ClipRowIndex* cidx = clip_idx.empty() ? nullptr : &clip_idx;

    // Common conditions for all fast paths
    bool fast_common = effective.blend_mode == BlendMode::Normal
                    && effective.op == CompositeOp::SourceOver
                    && !is_linear_space(effective.working_space)
                    && !effective.dithering
                    && effective.target_format == PixelFormat::ARGB32_Premultiplied;

    bool use_fast_solid = fast_common
                       && effective.paint->kind == PaintKind::Solid;

    bool use_fast_gradient = fast_common
                          && (effective.paint->kind == PaintKind::LinearGradient
                           || effective.paint->kind == PaintKind::RadialGradient
                           || effective.paint->kind == PaintKind::ConicGradient)
                          && effective.paint->gradient != nullptr;

    if (use_fast_solid) {
        for (auto& span : span_buffer.spans)
            blend_span_solid_srcover(effective, span, clip_spans, cidx, clip_rect);
    } else if (use_fast_gradient) {
        // Stack buffer for gradient row fetch; heap fallback for very wide spans
        constexpr int kStackBufSize = 4096;
        uint32_t stack_buf[kStackBufSize];
        std::unique_ptr<uint32_t[]> heap_buf;
        uint32_t* fetch_buf = stack_buf;

        int max_len = clip_rect.w;
        if (max_len > kStackBufSize) {
            heap_buf.reset(new uint32_t[max_len]);
            fetch_buf = heap_buf.get();
        }

        for (auto& span : span_buffer.spans)
            blend_span_gradient_srcover(effective, span, clip_spans, cidx,
                                        clip_rect, fetch_buf);
    } else {
        for (auto& span : span_buffer.spans)
            blend_span(effective, span, clip_spans, cidx, clip_rect);
    }
}

void blend_masked(const BlendParams& params, const SpanBuffer& span_buffer,
                  const IntRect& clip_rect, const SpanBuffer* clip_spans,
                  const Image& mask, MaskMode mode, int mask_ox, int mask_oy) {
    if (span_buffer.spans.empty()) return;
    if (clip_rect.empty()) return;
    if (!params.paint || !params.target_data) return;

    auto* mi = image_impl(mask);
    if (!mi || !mi->data) {
        blend(params, span_buffer, clip_rect, clip_spans);
        return;
    }

    // Resolve PatternDraw -> Texture
    BlendParams effective = params;
    PaintSource resolved_paint;
    Image tile_storage;
    if (params.paint->kind == PaintKind::PatternDraw) {
        if (resolve_pattern_paint(*params.paint, resolved_paint, tile_storage))
            effective.paint = &resolved_paint;
        else
            return;
    }

    // Build row index for clip spans — same pattern as blend().
    ClipRowIndex clip_idx;
    if (clip_spans && !clip_spans->spans.empty())
        clip_idx.build(*clip_spans);
    const ClipRowIndex* cidx = clip_idx.empty() ? nullptr : &clip_idx;

    bool use_linear = is_linear_space(effective.working_space);
    bool do_dither = effective.dithering;

    uint32_t global_opacity = static_cast<uint32_t>(
        std::clamp(effective.opacity * 255.0f + 0.5f, 0.0f, 255.0f));

    for (auto& span : span_buffer.spans) {
        int y = span.y;
        if (y < clip_rect.y || y >= clip_rect.y + clip_rect.h)
            continue;

        int x0 = std::max(span.x, clip_rect.x);
        int x1 = std::min(span.x + span.len, clip_rect.x + clip_rect.w);
        if (x0 >= x1) continue;

        uint32_t coverage = span.coverage;
        if (coverage == 0) continue;

        auto* row = reinterpret_cast<uint32_t*>(
            effective.target_data + y * effective.target_stride);

        for (int px = x0; px < x1; ++px) {
            // Clip coverage
            uint8_t clip_cov = (cidx && clip_spans)
                ? cidx->coverage_at(*clip_spans, px, y) : 255;
            if (clip_cov == 0) continue;

            // Mask coverage
            int my = y - mask_oy;
            int mx = px - mask_ox;
            uint8_t mask_cov = 0;
            if (mx >= 0 && mx < mi->width && my >= 0 && my < mi->height) {
                uint32_t mask_pixel;
                if (mi->format == PixelFormat::A8) {
                    mask_pixel = mi->data[my * mi->stride + mx];
                } else {
                    mask_pixel = reinterpret_cast<const uint32_t*>(
                        mi->data + my * mi->stride)[mx];
                }
                mask_cov = mask_ops::extract_coverage(
                    pixel_to_argb_premul(mask_pixel, mi->format), mode);
            }
            if (mask_cov == 0) continue;

            // Generate source color
            uint32_t src = generate_color(*effective.paint, px, y);

            // Combine coverage: span * clip * mask * global_opacity
            // Single-step division avoids intermediate rounding error.
            // 16581375 = 255^3, 8290687 = floor(255^3 / 2) for round-to-nearest.
            // Max numerator: 255^4 + 8290687 = 4,236,541,312 ≤ UINT32_MAX.
            uint32_t eff = (coverage * uint32_t(clip_cov) * uint32_t(mask_cov) * global_opacity
                            + 8290687) / 16581375;
            if (eff == 0) continue;

            if (eff < 255)
                src = byte_mul(src, eff);

            uint32_t dst = pixel_to_argb_premul(row[px], effective.target_format);

            uint32_t result;
            if (use_linear) {
                result = composite_pixel_linear(src, dst, effective.op,
                                                effective.blend_mode,
                                                do_dither, px, y);
            } else {
                result = composite_pixel_srgb(src, dst, effective.op,
                                              effective.blend_mode);
                if (do_dither)
                    result = dither_pixel_srgb(result, px, y);
            }

            row[px] = pixel_from_argb_premul(result, effective.target_format);
        }
    }
}

void blend(Context::Impl& ctx, const SpanBuffer& span_buffer) {
    if (span_buffer.spans.empty()) return;

    auto& st = ctx.state();

    // Build paint source from current state
    PaintSource paint;
    paint.interpolation = st.interpolation;

    if (st.has_fill_pattern) {
        auto* pi = pattern_impl(st.fill_pattern);
        if (pi) {
            paint.kind = PaintKind::PatternDraw;
            paint.pattern = pi;

            if (pi->kind == Pattern::Impl::Kind::ImageBased) {
                auto* img = image_impl(pi->source_image);
                if (img && img->data) {
                    paint.kind = PaintKind::Texture;
                    paint.image = img;
                    paint.tex_transform = pi->matrix * st.matrix;
                    paint.tex_opacity = 1.0f;
                    paint.pattern = pi; // tiled sampling
                }
            } else {
                paint.kind = PaintKind::PatternDraw;
                paint.pattern = pi;
                paint.ctm = st.matrix; // preserve CTM for resolve_pattern_paint
            }
        } else {
            paint.kind = PaintKind::Solid;
            paint.solid_argb = premultiply_argb(st.fill_color.to_argb32());
        }
    } else {
        paint.kind = PaintKind::Solid;
        paint.solid_argb = premultiply_argb(st.fill_color.to_argb32());
    }

    BlendParams params;
    params.target_data   = ctx.render_data;
    params.target_width  = ctx.render_width;
    params.target_height = ctx.render_height;
    params.target_stride = ctx.render_stride;
    params.target_format = ctx.render_format;
    params.paint         = &paint;
    params.op            = st.op;
    params.blend_mode    = st.blend_mode;
    params.opacity       = st.opacity * st.alpha;
    params.dithering     = st.dithering;
    params.working_space = ctx.working_space;
    params.interpolation = st.interpolation;

    const SpanBuffer* clip = st.clipping ? &st.clip_spans : nullptr;

    blend(params, span_buffer, ctx.clip_rect, clip);
}

} // namespace kvg
