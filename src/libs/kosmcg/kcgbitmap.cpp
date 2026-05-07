// kcgbitmap.cpp — BitmapContext: pixel-buffer-backed drawing context
// C++20

#include "kcgprivate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

namespace kcg {


//  BitmapContextImpl — extends Context::Impl with BitmapImpl
//
// BitmapContext declares no extra data members. We co-allocate
// BitmapImpl alongside Context::Impl using an extended struct,
// and delete via static_cast in ~BitmapContext (Impl has no virtual dtor).

namespace {

struct BitmapContextImpl : Context::Impl {
    BitmapContext::BitmapImpl bitmap;
};

BitmapContext::BitmapImpl* bitmap_impl(Context::Impl* impl) {
    return &static_cast<BitmapContextImpl*>(impl)->bitmap;
}

const BitmapContext::BitmapImpl* bitmap_impl(const Context::Impl* impl) {
    return &static_cast<const BitmapContextImpl*>(impl)->bitmap;
}

} // anonymous namespace


//  Lifecycle


// Context::Impl has no virtual destructor. BitmapContext manually deletes
// via static_cast<BitmapContextImpl*>. If someone adds a virtual dtor to
// Impl, the manual cast scheme will double-free. Catch that at compile time.
static_assert(!std::has_virtual_destructor_v<Context::Impl>,
              "Context::Impl must not have a virtual destructor; "
              "BitmapContext relies on static_cast deletion");

BitmapContext::BitmapContext()
    : Context(static_cast<Context::Impl*>(new BitmapContextImpl)) {
    // No double alloc: base Context receives the pre-allocated extended Impl.
}

BitmapContext::~BitmapContext() {
    // If we own the pixel buffer, free it.
    if (m_impl) {
        auto* bi = bitmap_impl(m_impl);
        if (bi->owns_data && m_impl->render_data) {
            std::free(m_impl->render_data);
            m_impl->render_data = nullptr;
        }
    }
    // ~Context() (base dtor) deletes m_impl via `delete m_impl`.
    // Since m_impl is actually a BitmapContextImpl*, we need the right
    // destructor to run. BitmapContextImpl inherits Context::Impl
    // non-virtually. delete on Context::Impl* won't call
    // ~BitmapContextImpl unless we handle it here.
    //
    // Fix: we delete ourselves and null m_impl so the base dtor skips it.
    delete static_cast<BitmapContextImpl*>(m_impl);
    m_impl = nullptr;
}

BitmapContext::BitmapContext(BitmapContext&& o) noexcept : Context(std::move(o)) {
    // Base move-ctor already moved m_impl.
}

BitmapContext& BitmapContext::operator=(BitmapContext&& o) noexcept {
    if (this != &o) {
        // Free our current buffer if we own it.
        if (m_impl) {
            auto* bi = bitmap_impl(m_impl);
            if (bi->owns_data && m_impl->render_data) {
                std::free(m_impl->render_data);
                m_impl->render_data = nullptr;
            }
            delete static_cast<BitmapContextImpl*>(m_impl);
            m_impl = nullptr;
        }
        // Base move-assign.
        Context::operator=(std::move(o));
    }
    return *this;
}


//  Factory: internal buffer


BitmapContext BitmapContext::create(int width, int height,
                                   PixelFormat format,
                                   const ColorSpace& space,
                                   const ColorSpace& working_space) {
    if (width <= 0 || height <= 0) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    int bpp = pixel_format_info(format).bpp;
    int stride = width * bpp;
    // Align stride to 16 bytes for SIMD friendliness.
    stride = (stride + 15) & ~15;

    size_t buf_size = static_cast<size_t>(height) * stride;
    auto* data = static_cast<unsigned char*>(std::calloc(1, buf_size));
    if (!data) {
        detail::set_last_error(Error::OutOfMemory);
        return {};
    }

    BitmapContext ctx;
    if (!ctx.m_impl) {
        std::free(data);
        detail::set_last_error(Error::OutOfMemory);
        return {};
    }

    ctx.m_impl->render_data   = data;
    ctx.m_impl->render_width  = width;
    ctx.m_impl->render_height = height;
    ctx.m_impl->render_stride = stride;
    ctx.m_impl->render_format = format;
    ctx.m_impl->clip_rect     = {0, 0, width, height};

    auto* bi = bitmap_impl(ctx.m_impl);
    bi->owns_data      = true;
    bi->color_space_   = space;
    bi->working_space_ = working_space;
    bi->headroom_      = 1.0f;

    ctx.m_impl->working_space = &bi->working_space_;
    ctx.m_impl->state().scale_factor = 1.0f;

    detail::clear_last_error();
    return ctx;
}


//  Factory: external buffer (zero-copy)


BitmapContext BitmapContext::create(unsigned char* data, int width, int height, int stride,
                                   PixelFormat format,
                                   const ColorSpace& space,
                                   const ColorSpace& working_space) {
    if (!data || width <= 0 || height <= 0 || stride < width * pixel_format_info(format).bpp) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    BitmapContext ctx;
    if (!ctx.m_impl) {
        detail::set_last_error(Error::OutOfMemory);
        return {};
    }

    ctx.m_impl->render_data   = data;
    ctx.m_impl->render_width  = width;
    ctx.m_impl->render_height = height;
    ctx.m_impl->render_stride = stride;
    ctx.m_impl->render_format = format;
    ctx.m_impl->clip_rect     = {0, 0, width, height};

    auto* bi = bitmap_impl(ctx.m_impl);
    bi->owns_data      = false;
    bi->color_space_   = space;
    bi->working_space_ = working_space;
    bi->headroom_      = 1.0f;

    ctx.m_impl->working_space = &bi->working_space_;
    ctx.m_impl->state().scale_factor = 1.0f;

    detail::clear_last_error();
    return ctx;
}


//  Accessors


unsigned char* BitmapContext::data() {
    return m_impl ? m_impl->render_data : nullptr;
}

const unsigned char* BitmapContext::data() const {
    return m_impl ? m_impl->render_data : nullptr;
}

int BitmapContext::width() const {
    return m_impl ? m_impl->render_width : 0;
}

int BitmapContext::height() const {
    return m_impl ? m_impl->render_height : 0;
}

int BitmapContext::stride() const {
    return m_impl ? m_impl->render_stride : 0;
}

PixelFormat BitmapContext::format() const {
    return m_impl ? m_impl->render_format : PixelFormat::ARGB32_Premultiplied;
}

ColorSpace BitmapContext::color_space() const {
    if (!m_impl) return ColorSpace::srgb();
    return bitmap_impl(m_impl)->color_space_;
}

ColorSpace BitmapContext::working_color_space() const {
    if (!m_impl) return ColorSpace::linear_srgb();
    return bitmap_impl(m_impl)->working_space_;
}


//  Scale factor / logical dimensions


float BitmapContext::scale_factor() const {
    if (!m_impl) return 1.0f;
    return m_impl->state().scale_factor;
}

void BitmapContext::set_scale_factor(float scale) {
    if (!m_impl || scale <= 0.0f) return;
    auto& st = m_impl->state();
    float old_scale = st.scale_factor;
    st.scale_factor = scale;

    // Adjust the base CTM so that user-space coordinates scale properly.
    if (old_scale != scale) {
        float ratio = scale / old_scale;
        st.matrix = AffineTransform::scaled(ratio, ratio) * st.matrix;
    }
}

float BitmapContext::logical_width() const {
    if (!m_impl) return 0.0f;
    return static_cast<float>(m_impl->render_width) / m_impl->state().scale_factor;
}

float BitmapContext::logical_height() const {
    if (!m_impl) return 0.0f;
    return static_cast<float>(m_impl->render_height) / m_impl->state().scale_factor;
}


//  HDR headroom


float BitmapContext::headroom() const {
    if (!m_impl) return 1.0f;
    return bitmap_impl(m_impl)->headroom_;
}

void BitmapContext::set_headroom(float h) {
    if (!m_impl) return;
    bitmap_impl(m_impl)->headroom_ = std::max(0.0f, h);
}


//  to_image — snapshot the buffer


Image BitmapContext::to_image() const {
    if (!m_impl || !m_impl->render_data) return {};

    int w = m_impl->render_width;
    int h = m_impl->render_height;
    int s = m_impl->render_stride;
    const auto* bi = bitmap_impl(m_impl);

    Image img = Image::create(w, h, s, m_impl->render_format,
                              bi->color_space_,
                              {m_impl->render_data, static_cast<size_t>(h) * s});

    if (img && bi->headroom_ != 1.0f)
        img = img.with_headroom(bi->headroom_);

    return img;
}

Image BitmapContext::to_image(const IntRect& rect) const {
    if (!m_impl || !m_impl->render_data) return {};
    if (rect.empty()) return {};

    // Clamp to buffer bounds.
    int x0 = std::max(0, rect.x);
    int y0 = std::max(0, rect.y);
    int x1 = std::min(m_impl->render_width,  rect.x + rect.w);
    int y1 = std::min(m_impl->render_height, rect.y + rect.h);
    int rw = x1 - x0;
    int rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return {};

    const auto* bi = bitmap_impl(m_impl);
    int bpp = pixel_format_info(m_impl->render_format).bpp;

    // Copy the sub-rectangle into a contiguous buffer.
    int dst_stride = (rw * bpp + 15) & ~15;
    std::vector<unsigned char> buf;
    try {
        buf.resize(static_cast<size_t>(rh) * dst_stride);
    } catch (const std::bad_alloc&) {
        detail::set_last_error(Error::OutOfMemory);
        return {};
    }

    for (int y = 0; y < rh; ++y) {
        const auto* src = m_impl->render_data + (y0 + y) * m_impl->render_stride + x0 * bpp;
        auto* dst = buf.data() + y * dst_stride;
        std::memcpy(dst, src, static_cast<size_t>(rw * bpp));
    }

    Image img = Image::create(rw, rh, dst_stride, m_impl->render_format,
                              bi->color_space_,
                              {buf.data(), buf.size()});

    if (img && bi->headroom_ != 1.0f)
        img = img.with_headroom(bi->headroom_);

    return img;
}


//  composite — direct pixel-to-pixel blit
//
//  NOTE: composite() intentionally bypasses the clip region and CTM.
//  It operates in raw device coordinates for maximum performance.
//  This is the expected behaviour for compositor fast-paths where the
//  caller manages clipping externally.  If you need clip-aware
//  compositing, use draw_image() instead.


void BitmapContext::composite(const Image& src, const IntRect& src_rect,
                              int dst_x, int dst_y,
                              CompositeOp op, BlendMode blend, float opacity) {
    if (!m_impl || !m_impl->render_data || !src) return;

    const auto* si = image_impl(src);
    if (!si || !si->data) return;

    // Clamp source rect to image bounds.
    int sx0 = std::max(0, src_rect.x);
    int sy0 = std::max(0, src_rect.y);
    int sx1 = std::min(si->width,  src_rect.x + src_rect.w);
    int sy1 = std::min(si->height, src_rect.y + src_rect.h);
    int sw = sx1 - sx0;
    int sh = sy1 - sy0;
    if (sw <= 0 || sh <= 0) return;

    // Clamp to destination bounds.
    if (dst_x < 0) { sx0 -= dst_x; sw += dst_x; dst_x = 0; }
    if (dst_y < 0) { sy0 -= dst_y; sh += dst_y; dst_y = 0; }
    sw = std::min(sw, m_impl->render_width  - dst_x);
    sh = std::min(sh, m_impl->render_height - dst_y);
    if (sw <= 0 || sh <= 0) return;

    uint32_t opa_byte = static_cast<uint32_t>(std::clamp(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    if (opa_byte == 0) return;

    int src_bpp = pixel_format_info(si->format).bpp;
    int dst_bpp = pixel_format_info(m_impl->render_format).bpp;

    // Fast path: both ARGB32_Premultiplied — skip per-pixel format conversion.
    bool src_is_argb = (si->format == PixelFormat::ARGB32_Premultiplied
                     || si->format == PixelFormat::BGRA32_Premultiplied);
    bool dst_is_argb = (m_impl->render_format == PixelFormat::ARGB32_Premultiplied
                     || m_impl->render_format == PixelFormat::BGRA32_Premultiplied);

    if (src_is_argb && dst_is_argb && src_bpp == 4 && dst_bpp == 4) {
        for (int y = 0; y < sh; ++y) {
            const auto* s32 = reinterpret_cast<const uint32_t*>(
                si->data + (sy0 + y) * si->stride);
            auto* d32 = reinterpret_cast<uint32_t*>(
                m_impl->render_data + (dst_y + y) * m_impl->render_stride);

            for (int x = 0; x < sw; ++x) {
                uint32_t src_px = s32[sx0 + x];
                if (opa_byte < 255)
                    src_px = byte_mul(src_px, opa_byte);
                d32[dst_x + x] = composite_pixel(src_px, d32[dst_x + x], op, blend,
                                                  m_impl->working_space);
            }
        }
        return;
    }

    for (int y = 0; y < sh; ++y) {
        const auto* s_row = si->data + (sy0 + y) * si->stride;
        auto* d_row = m_impl->render_data + (dst_y + y) * m_impl->render_stride;

        for (int x = 0; x < sw; ++x) {
            // Read source pixel, convert to ARGB premul.
            uint32_t src_px;
            if (src_bpp == 1)
                src_px = pack_argb(s_row[sx0 + x], 0, 0, 0);
            else
                src_px = pixel_to_argb_premul(
                    reinterpret_cast<const uint32_t*>(s_row)[sx0 + x], si->format);

            // Apply opacity.
            if (opa_byte < 255)
                src_px = byte_mul(src_px, opa_byte);

            // Read destination pixel.
            uint32_t dst_px;
            if (dst_bpp == 1)
                dst_px = pack_argb(d_row[dst_x + x], 0, 0, 0);
            else
                dst_px = pixel_to_argb_premul(
                    reinterpret_cast<const uint32_t*>(d_row)[dst_x + x], m_impl->render_format);

            // Composite with blend mode support.
            uint32_t result = composite_pixel(src_px, dst_px, op, blend,
                                              m_impl->working_space);

            // Write back in destination format.
            if (dst_bpp == 1) {
                d_row[dst_x + x] = pixel_alpha(result);
            } else {
                reinterpret_cast<uint32_t*>(d_row)[dst_x + x] =
                    pixel_from_argb_premul(result, m_impl->render_format);
            }
        }
    }
}

void BitmapContext::composite(const Image& src, int dst_x, int dst_y,
                              CompositeOp op, BlendMode blend, float opacity) {
    if (!src) return;
    composite(src, IntRect{0, 0, src.width(), src.height()},
              dst_x, dst_y, op, blend, opacity);
}


//  clear — fill entire buffer with a solid color


void BitmapContext::clear(const Color& color) {
    if (!m_impl || !m_impl->render_data) return;

    int w = m_impl->render_width;
    int h = m_impl->render_height;

    Color premul = color.premultiplied();
    uint32_t argb = premul.to_argb32();
    uint32_t px = pixel_from_argb_premul(argb, m_impl->render_format);

    int bpp = pixel_format_info(m_impl->render_format).bpp;

    if (bpp == 1) {
        uint8_t a_val = pixel_alpha(argb);
        for (int y = 0; y < h; ++y)
            std::memset(m_impl->render_data + y * m_impl->render_stride, a_val, static_cast<size_t>(w));
    } else {
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(
                m_impl->render_data + y * m_impl->render_stride);
            memfill32(row, w, px);
        }
    }
}


//  apply_blur


void BitmapContext::apply_blur(float radius) {
    if (!m_impl || !m_impl->render_data || radius <= 0.0f) return;

    int w = m_impl->render_width;
    int h = m_impl->render_height;
    PixelFormat fmt = m_impl->render_format;

    if (fmt == PixelFormat::A8) {
        gaussian_blur_alpha(m_impl->render_data, w, h, m_impl->render_stride, radius);
        return;
    }

    bool needs_conv = (fmt != PixelFormat::ARGB32_Premultiplied);

    // Convert to ARGB32_Premultiplied for the blur pass.
    // The guard ensures we convert back even if gaussian_blur throws.
    auto convert_to_argb = [&]() {
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(
                m_impl->render_data + y * m_impl->render_stride);
            for (int x = 0; x < w; ++x)
                row[x] = pixel_to_argb_premul(row[x], fmt);
        }
    };

    auto convert_from_argb = [&]() {
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(
                m_impl->render_data + y * m_impl->render_stride);
            for (int x = 0; x < w; ++x)
                row[x] = pixel_from_argb_premul(row[x], fmt);
        }
    };

    struct FormatGuard {
        bool active;
        decltype(convert_from_argb)& restore;
        ~FormatGuard() { if (active) restore(); }
    };

    if (needs_conv) convert_to_argb();
    FormatGuard guard{needs_conv, convert_from_argb};

    gaussian_blur(m_impl->render_data, w, h, m_impl->render_stride, radius);
}


//  apply_color_matrix


void BitmapContext::apply_color_matrix(const ColorMatrix& cm) {
    if (!m_impl || !m_impl->render_data) return;

    // Identity early-out.
    const auto& m = cm.m;
    bool is_identity = (m[0]==1 && m[1]==0 && m[2]==0 && m[3]==0 && m[4]==0
                     && m[5]==0 && m[6]==1 && m[7]==0 && m[8]==0 && m[9]==0
                     && m[10]==0 && m[11]==0 && m[12]==1 && m[13]==0 && m[14]==0
                     && m[15]==0 && m[16]==0 && m[17]==0 && m[18]==1 && m[19]==0);
    if (is_identity) return;

    int w = m_impl->render_width;
    int h = m_impl->render_height;
    int bpp = pixel_format_info(m_impl->render_format).bpp;

    // Alpha-preserving fast-path: when the matrix does not modify alpha
    // (row 3 = [0,0,0,1,0]) and format is ARGB32_Premultiplied, we can
    // apply the 3x3 RGB sub-matrix directly in premultiplied integer space
    // with fixed-point arithmetic, skipping float unpremultiply/repremultiply.
    bool alpha_identity = (m[15]==0 && m[16]==0 && m[17]==0 && m[18]==1 && m[19]==0);
    bool no_bias = (m[4]==0 && m[9]==0 && m[14]==0);
    bool no_alpha_coupling = (m[3]==0 && m[8]==0 && m[13]==0);
    bool is_argb = (m_impl->render_format == PixelFormat::ARGB32_Premultiplied
                 || m_impl->render_format == PixelFormat::BGRA32_Premultiplied);

    if (alpha_identity && no_bias && no_alpha_coupling && is_argb && bpp == 4) {
        // Fixed-point 8.8: multiply coefficients by 256.
        int m00 = static_cast<int>(std::lroundf(m[0] * 256.0f));
        int m01 = static_cast<int>(std::lroundf(m[1] * 256.0f));
        int m02 = static_cast<int>(std::lroundf(m[2] * 256.0f));
        int m10 = static_cast<int>(std::lroundf(m[5] * 256.0f));
        int m11 = static_cast<int>(std::lroundf(m[6] * 256.0f));
        int m12 = static_cast<int>(std::lroundf(m[7] * 256.0f));
        int m20 = static_cast<int>(std::lroundf(m[10] * 256.0f));
        int m21 = static_cast<int>(std::lroundf(m[11] * 256.0f));
        int m22 = static_cast<int>(std::lroundf(m[12] * 256.0f));

        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(
                m_impl->render_data + y * m_impl->render_stride);
            for (int x = 0; x < w; ++x) {
                uint32_t px = row[x];
                int pr = red(px), pg = green(px), pb = blue(px);

                int nr = (m00 * pr + m01 * pg + m02 * pb + 128) >> 8;
                int ng = (m10 * pr + m11 * pg + m12 * pb + 128) >> 8;
                int nb = (m20 * pr + m21 * pg + m22 * pb + 128) >> 8;

                int a = pixel_alpha(px);
                nr = std::clamp(nr, 0, a);
                ng = std::clamp(ng, 0, a);
                nb = std::clamp(nb, 0, a);

                row[x] = pack_argb(static_cast<uint8_t>(a),
                                   static_cast<uint8_t>(nr),
                                   static_cast<uint8_t>(ng),
                                   static_cast<uint8_t>(nb));
            }
        }
        return;
    }

    // General path: float unpremultiply → matrix → clamp → repremultiply.
    for (int y = 0; y < h; ++y) {
        auto* row = m_impl->render_data + y * m_impl->render_stride;

        for (int x = 0; x < w; ++x) {
            float r, g, b, a;

            if (bpp == 1) {
                a = row[x] / 255.0f;
                r = g = b = 0.0f;
            } else {
                uint32_t px = pixel_to_argb_premul(
                    reinterpret_cast<uint32_t*>(row)[x], m_impl->render_format);
                auto rgba = unpremultiply_f(px);
                r = rgba.r; g = rgba.g; b = rgba.b; a = rgba.a;
            }

            float nr = m[0]*r + m[1]*g + m[2]*b + m[3]*a + m[4];
            float ng = m[5]*r + m[6]*g + m[7]*b + m[8]*a + m[9];
            float nb = m[10]*r + m[11]*g + m[12]*b + m[13]*a + m[14];
            float na = m[15]*r + m[16]*g + m[17]*b + m[18]*a + m[19];

            nr = std::clamp(nr, 0.0f, 1.0f);
            ng = std::clamp(ng, 0.0f, 1.0f);
            nb = std::clamp(nb, 0.0f, 1.0f);
            na = std::clamp(na, 0.0f, 1.0f);

            Color result{nr, ng, nb, na};
            uint32_t out = result.premultiplied().to_argb32();

            if (bpp == 1) {
                row[x] = pixel_alpha(out);
            } else {
                reinterpret_cast<uint32_t*>(row)[x] =
                    pixel_from_argb_premul(out, m_impl->render_format);
            }
        }
    }
}


//  convert_to — in-place pixel format conversion


void BitmapContext::convert_to(PixelFormat target) {
    if (!m_impl || !m_impl->render_data) return;
    if (m_impl->render_format == target) return;

    int w = m_impl->render_width;
    int h = m_impl->render_height;
    int old_bpp = pixel_format_info(m_impl->render_format).bpp;
    int new_bpp = pixel_format_info(target).bpp;

    if (old_bpp == new_bpp) {
        auto* bi = bitmap_impl(m_impl);
        if (bi->owns_data) {
            // Same BPP, owned buffer — convert scanlines in-place.
            for (int y = 0; y < h; ++y) {
                auto* scanline = m_impl->render_data + y * m_impl->render_stride;
                convert_scanline(scanline, m_impl->render_format,
                                 scanline, target, w);
            }
        } else {
            // Same BPP, external buffer — must allocate to avoid writing
            // to memory we don't own.
            size_t buf_size = static_cast<size_t>(h) * m_impl->render_stride;
            auto* new_data = static_cast<unsigned char*>(std::malloc(buf_size));
            if (!new_data) return;
            for (int y = 0; y < h; ++y) {
                auto* src = m_impl->render_data + y * m_impl->render_stride;
                auto* dst = new_data + y * m_impl->render_stride;
                convert_scanline(src, m_impl->render_format, dst, target, w);
            }
            m_impl->render_data = new_data;
            bi->owns_data = true;
        }
        m_impl->render_format = target;
    } else {
        // BPP changed — need to reallocate if we own the buffer.
        auto* bi = bitmap_impl(m_impl);
        int new_stride = w * new_bpp;
        new_stride = (new_stride + 15) & ~15;
        size_t new_size = static_cast<size_t>(h) * new_stride;

        auto* new_data = static_cast<unsigned char*>(std::calloc(1, new_size));
        if (!new_data) return;

        for (int y = 0; y < h; ++y) {
            auto* src = m_impl->render_data + y * m_impl->render_stride;
            auto* dst = new_data + y * new_stride;
            convert_scanline(src, m_impl->render_format, dst, target, w);
        }

        if (bi->owns_data)
            std::free(m_impl->render_data);

        m_impl->render_data   = new_data;
        m_impl->render_stride = new_stride;
        m_impl->render_format = target;
        bi->owns_data = true;
    }
}


//  write — image file output


bool BitmapContext::write(const char* filename, ImageFormat format) const {
    Image img = to_image();
    if (!img) return false;
    return img.write(filename, format);
}

bool BitmapContext::write(const char* filename, ImageFormat format, int quality) const {
    Image img = to_image();
    if (!img) return false;
    return img.write(filename, format, quality);
}

bool BitmapContext::write(const char* filename) const {
    Image img = to_image();
    if (!img) return false;
    return img.write(filename);
}

} // namespace kcg
