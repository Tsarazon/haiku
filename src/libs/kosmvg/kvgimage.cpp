// kvgimage.cpp — Image class implementation
// C++20

#include "kvgprivate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <vector>

#include "stb_image.hpp"

namespace stbi = kosmvg::stbi;

namespace kvg {

// -- Helpers --

// Write accessor for free functions that construct Image objects.
// Member functions (create, load, etc.) have direct access to m_impl;
// free helpers (image_from_stbi*) use this instead.
static Image::Impl*& impl_ref(Image& img) {
    static_assert(sizeof(Image) == sizeof(void*));
    return *reinterpret_cast<Image::Impl**>(&img);
}

static Image::Impl* make_impl(int width, int height, int stride,
                               PixelFormat format, const ColorSpace& space) {
    auto* p = new Image::Impl;
    p->width = width;
    p->height = height;
    p->stride = stride;
    p->format = format;
    p->color_space_ = space;
    return p;
}

static int aligned_stride(int width, PixelFormat fmt) {
    int s = width * pixel_format_info(fmt).bpp;
    return (s + 15) & ~15;
}

// COW detach: ensure refcount == 1 with owned data.
static Image::Impl* cow_detach(Image::Impl*& impl) {
    if (!impl) return nullptr;
    if (impl->count() == 1 && impl->owns_data)
        return impl;

    auto* fresh = new Image::Impl;
    fresh->width = impl->width;
    fresh->height = impl->height;
    fresh->stride = impl->stride;
    fresh->format = impl->format;
    fresh->color_space_ = impl->color_space_;
    fresh->headroom_ = impl->headroom_;

    size_t buf_size = static_cast<size_t>(impl->stride) * impl->height;
    fresh->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!fresh->data) {
        delete fresh;
        detail::set_last_error(Error::OutOfMemory);
        return nullptr;
    }
    std::memcpy(fresh->data, impl->data, buf_size);
    fresh->owns_data = true;

    if (impl->deref()) delete impl;
    impl = fresh;
    return fresh;
}

// -- Lifecycle --

Image::Image() : m_impl(nullptr) {}

Image::~Image() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

Image::Image(const Image& o) : m_impl(o.m_impl) {
    if (m_impl) m_impl->ref();
}

Image::Image(Image&& o) noexcept : m_impl(o.m_impl) {
    o.m_impl = nullptr;
}

Image& Image::operator=(const Image& o) {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        if (m_impl) m_impl->ref();
    }
    return *this;
}

Image& Image::operator=(Image&& o) noexcept {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        o.m_impl = nullptr;
    }
    return *this;
}

Image::operator bool() const {
    return m_impl && m_impl->data;
}

// -- Accessors --

int Image::width() const  { return m_impl ? m_impl->width  : 0; }
int Image::height() const { return m_impl ? m_impl->height : 0; }
int Image::stride() const { return m_impl ? m_impl->stride : 0; }

PixelFormat Image::format() const {
    return m_impl ? m_impl->format : PixelFormat::ARGB32_Premultiplied;
}

ColorSpace Image::color_space() const {
    return m_impl ? m_impl->color_space_ : ColorSpace();
}

const unsigned char* Image::data() const {
    return m_impl ? m_impl->data : nullptr;
}

float Image::headroom() const {
    return m_impl ? m_impl->headroom_ : 1.0f;
}

// -- with_headroom (metadata-only COW) --

Image Image::with_headroom(float hr) const {
    detail::clear_last_error();
    if (!m_impl) return {};
    Image result(*this);
    if (auto* p = cow_detach(result.m_impl))
        p->headroom_ = hr;
    return result;
}

// -- Factory: copying create --

Image Image::create(int width, int height, int stride,
                    PixelFormat format, const ColorSpace& space,
                    std::span<const unsigned char> data) {
    detail::clear_last_error();

    if (width <= 0 || height <= 0 || stride <= 0) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }
    if (stride < min_stride(width, format)) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    size_t needed = static_cast<size_t>(stride) * height;
    if (data.size() < needed) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    auto* impl = make_impl(width, height, stride, format, space);
    impl->data = static_cast<unsigned char*>(std::malloc(needed));
    if (!impl->data) {
        delete impl;
        detail::set_last_error(Error::OutOfMemory);
        return {};
    }
    std::memcpy(impl->data, data.data(), needed);
    impl->owns_data = true;

    Image img;
    img.m_impl = impl;
    return img;
}

// -- Factory: zero-copy create --

Image Image::create(int width, int height, int stride,
                    PixelFormat format, const ColorSpace& space,
                    const unsigned char* data,
                    void (*release)(void*), void* info) {
    detail::clear_last_error();

    if (width <= 0 || height <= 0 || stride <= 0 || !data) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }
    if (stride < min_stride(width, format)) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    auto* impl = make_impl(width, height, stride, format, space);
    impl->data = const_cast<unsigned char*>(data);
    impl->owns_data = false;
    impl->release_fn = release;
    impl->release_ctx = info;

    Image img;
    img.m_impl = impl;
    return img;
}

// -- Info: query image metadata without decoding --

std::optional<ImageFileInfo> Image::info(const char* filename) {
    if (!filename) return std::nullopt;
    auto si = stbi::info(filename);
    if (!si) return std::nullopt;
    ImageFileInfo r;
    r.width = si->width;
    r.height = si->height;
    r.channels = si->channels;
    r.is_hdr = stbi::is_hdr(filename);
    r.is_16bit = stbi::is_16bit(filename);
    return r;
}

std::optional<ImageFileInfo> Image::info(std::span<const std::byte> data) {
    if (data.empty()) return std::nullopt;
    auto buf = std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};
    auto si = stbi::info(buf);
    if (!si) return std::nullopt;
    ImageFileInfo r;
    r.width = si->width;
    r.height = si->height;
    r.channels = si->channels;
    r.is_hdr = stbi::is_hdr(buf);
    r.is_16bit = stbi::is_16bit(buf);
    return r;
}

// -- Load: stbi → Image --

static Image image_from_stbi(stbi::Image<uint8_t> src) {
    int w = src.width();
    int h = src.height();
    int stride = aligned_stride(w, PixelFormat::ARGB32_Premultiplied);
    auto* impl = make_impl(w, h, stride, PixelFormat::ARGB32_Premultiplied,
                           ColorSpace::srgb());

    size_t buf_size = static_cast<size_t>(stride) * h;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) {
        delete impl;
        detail::set_last_error(Error::OutOfMemory);
        return {};
    }
    impl->owns_data = true;

    // stbi gives RGBA straight with stride = w*4 (no padding).
    // Our stride may differ due to alignment, so convert row by row.
    int src_stride = w * 4;
    for (int y = 0; y < h; ++y) {
        convert_rgba_to_argb(
            impl->data + static_cast<size_t>(y) * stride,
            src.data() + static_cast<size_t>(y) * src_stride,
            w, 1, src_stride);
    }

    Image img;
    impl_ref(img) = impl;
    return img;
}

// HDR path: load float data, tonemap to 8-bit ARGB premul,
// record headroom so the HDR range is at least documented.
static Image image_from_stbi_f(stbi::Image<float> src) {
    int w = src.width();
    int h = src.height();
    int ch = src.channels();
    int stride = aligned_stride(w, PixelFormat::ARGB32_Premultiplied);
    auto* impl = make_impl(w, h, stride, PixelFormat::ARGB32_Premultiplied,
                           ColorSpace::srgb());

    size_t buf_size = static_cast<size_t>(stride) * h;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) {
        delete impl;
        detail::set_last_error(Error::OutOfMemory);
        return {};
    }
    impl->owns_data = true;

    // Find peak luminance for headroom metadata.
    float peak = 1.0f;
    const float* fp = src.data();
    size_t pixel_count = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < pixel_count; ++i) {
        int base = static_cast<int>(i) * ch;
        float r = fp[base];
        float g = ch >= 3 ? fp[base + 1] : r;
        float b = ch >= 3 ? fp[base + 2] : r;
        float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        if (lum > peak) peak = lum;
    }

    // Tonemap: Reinhard per-channel, scale to [0..255].
    // This is a simple operator; preserves relative brightness.
    float inv_peak = 1.0f / peak;
    for (int y = 0; y < h; ++y) {
        auto* dst = reinterpret_cast<uint32_t*>(
            impl->data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * ch;
            float r = fp[idx];
            float g = ch >= 3 ? fp[idx + 1] : r;
            float b = ch >= 3 ? fp[idx + 2] : r;
            float a = ch == 4 ? fp[idx + 3] : (ch == 2 ? fp[idx + 1] : 1.0f);

            // Simple Reinhard tonemap.
            r = r * inv_peak;
            g = g * inv_peak;
            b = b * inv_peak;

            auto to_u8 = [](float v) -> uint8_t {
                return static_cast<uint8_t>(
                    std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
            };

            uint8_t a8 = to_u8(std::clamp(a, 0.0f, 1.0f));
            uint8_t r8 = to_u8(std::clamp(r, 0.0f, 1.0f));
            uint8_t g8 = to_u8(std::clamp(g, 0.0f, 1.0f));
            uint8_t b8 = to_u8(std::clamp(b, 0.0f, 1.0f));
            dst[x] = premultiply_argb(pack_argb(a8, r8, g8, b8));
        }
    }

    impl->headroom_ = peak;

    Image img;
    impl_ref(img) = impl;
    return img;
}

// -- load(filename) --

Image Image::load(const char* filename) {
    detail::clear_last_error();

    if (!filename) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    // HDR files: use float loader to preserve dynamic range.
    if (stbi::is_hdr(filename)) {
        auto src = stbi::loadf(filename,
            stbi::LoadOptions{.desired_format = stbi::PixelFormat::RgbAlpha});
        if (!src) {
            detail::set_last_error(Error::ImageDecodeError);
            return {};
        }
        return image_from_stbi_f(std::move(src));
    }

    auto src = stbi::load(filename,
        stbi::LoadOptions{.desired_format = stbi::PixelFormat::RgbAlpha});
    if (!src) {
        detail::set_last_error(Error::ImageDecodeError);
        return {};
    }
    return image_from_stbi(std::move(src));
}

// -- load(span) --

Image Image::load(std::span<const std::byte> data) {
    detail::clear_last_error();

    if (data.empty()) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    auto buf = std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    // HDR files: use float loader.
    if (stbi::is_hdr(buf)) {
        auto src = stbi::loadf(buf,
            stbi::LoadOptions{.desired_format = stbi::PixelFormat::RgbAlpha});
        if (!src) {
            detail::set_last_error(Error::ImageDecodeError);
            return {};
        }
        return image_from_stbi_f(std::move(src));
    }

    auto src = stbi::load(buf,
        stbi::LoadOptions{.desired_format = stbi::PixelFormat::RgbAlpha});
    if (!src) {
        detail::set_last_error(Error::ImageDecodeError);
        return {};
    }
    return image_from_stbi(std::move(src));
}

// -- load_base64 --

namespace {

int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<std::byte> b64_decode(std::string_view input) {
    auto comma = input.find(',');
    if (comma != std::string_view::npos)
        input = input.substr(comma + 1);

    std::vector<std::byte> out;
    out.reserve(input.size() * 3 / 4);

    uint32_t buf = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ')
            continue;
        int v = b64_val(c);
        if (v < 0) continue;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::byte>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

} // anonymous namespace

Image Image::load_base64(std::string_view data) {
    detail::clear_last_error();

    auto decoded = b64_decode(data);
    if (decoded.empty()) {
        detail::set_last_error(Error::ImageDecodeError);
        return {};
    }
    return load(std::span<const std::byte>(decoded));
}

// -- decode_failure_reason --

const char* Image::decode_failure_reason() {
    return stbi::failure_reason();
}

// -- cropped --

Image Image::cropped(const IntRect& rect) const {
    if (!m_impl) return {};

    IntRect clipped = rect.intersected({0, 0, m_impl->width, m_impl->height});
    if (clipped.empty()) return {};

    int bpp = pixel_format_info(m_impl->format).bpp;
    int new_stride = aligned_stride(clipped.w, m_impl->format);

    auto* impl = make_impl(clipped.w, clipped.h, new_stride,
                           m_impl->format, m_impl->color_space_);
    size_t buf_size = static_cast<size_t>(new_stride) * clipped.h;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) { delete impl; return {}; }
    impl->owns_data = true;
    impl->headroom_ = m_impl->headroom_;

    int row_bytes = clipped.w * bpp;
    for (int y = 0; y < clipped.h; ++y) {
        const auto* src = m_impl->data
            + static_cast<size_t>(clipped.y + y) * m_impl->stride
            + clipped.x * bpp;
        auto* dst = impl->data + static_cast<size_t>(y) * new_stride;
        std::memcpy(dst, src, row_bytes);
    }

    Image img;
    img.m_impl = impl;
    return img;
}

// -- converted --

Image Image::converted(PixelFormat fmt, const ColorSpace& space) const {
    if (!m_impl) return {};

    if (fmt == m_impl->format) {
        Image result(*this);
        if (auto* p = cow_detach(result.m_impl))
            p->color_space_ = space;
        return result;
    }

    int new_stride = aligned_stride(m_impl->width, fmt);
    auto* impl = make_impl(m_impl->width, m_impl->height, new_stride, fmt, space);
    size_t buf_size = static_cast<size_t>(new_stride) * m_impl->height;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) { delete impl; return {}; }
    impl->owns_data = true;
    impl->headroom_ = m_impl->headroom_;

    for (int y = 0; y < m_impl->height; ++y) {
        const auto* src = m_impl->data + static_cast<size_t>(y) * m_impl->stride;
        auto* dst = impl->data + static_cast<size_t>(y) * new_stride;
        convert_scanline(src, m_impl->format, dst, fmt, m_impl->width);
    }

    Image img;
    img.m_impl = impl;
    return img;
}

// -- filtered --

Image Image::filtered(const ColorMatrix& cm) const {
    if (!m_impl) return {};

    int w = m_impl->width;
    int h = m_impl->height;
    int new_stride = aligned_stride(w, m_impl->format);

    auto* impl = make_impl(w, h, new_stride, m_impl->format, m_impl->color_space_);
    size_t buf_size = static_cast<size_t>(new_stride) * h;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) { delete impl; return {}; }
    impl->owns_data = true;
    impl->headroom_ = m_impl->headroom_;

    bool is_a8 = (m_impl->format == PixelFormat::A8);

    for (int y = 0; y < h; ++y) {
        const auto* src_row = m_impl->data + static_cast<size_t>(y) * m_impl->stride;
        auto* dst_row = impl->data + static_cast<size_t>(y) * new_stride;

        if (is_a8) {
            for (int x = 0; x < w; ++x) {
                float a = src_row[x] / 255.0f;
                float na = cm.m[18] * a + cm.m[19];
                dst_row[x] = static_cast<uint8_t>(
                    std::clamp(na * 255.0f + 0.5f, 0.0f, 255.0f));
            }
            continue;
        }

        auto* s32 = reinterpret_cast<const uint32_t*>(src_row);
        auto* d32 = reinterpret_cast<uint32_t*>(dst_row);

        for (int x = 0; x < w; ++x) {
            uint32_t argb = pixel_to_argb_premul(s32[x], m_impl->format);
            auto up = unpremultiply_f(argb);
            float r = up.r, g = up.g, b = up.b, a = up.a;

            float nr = cm.m[0]*r  + cm.m[1]*g  + cm.m[2]*b  + cm.m[3]*a  + cm.m[4];
            float ng = cm.m[5]*r  + cm.m[6]*g  + cm.m[7]*b  + cm.m[8]*a  + cm.m[9];
            float nb = cm.m[10]*r + cm.m[11]*g + cm.m[12]*b + cm.m[13]*a + cm.m[14];
            float na = cm.m[15]*r + cm.m[16]*g + cm.m[17]*b + cm.m[18]*a + cm.m[19];

            auto to_u8 = [](float v) -> uint8_t {
                return static_cast<uint8_t>(
                    std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
            };

            uint32_t out = premultiply_argb(
                pack_argb(to_u8(na), to_u8(nr), to_u8(ng), to_u8(nb)));
            d32[x] = pixel_from_argb_premul(out, m_impl->format);
        }
    }

    Image img;
    img.m_impl = impl;
    return img;
}

// -- blurred --

Image Image::blurred(float radius) const {
    if (!m_impl || radius <= 0.0f) return *this;

    int w = m_impl->width;
    int h = m_impl->height;

    if (m_impl->format == PixelFormat::A8) {
        int tmp_stride = aligned_stride(w, PixelFormat::ARGB32_Premultiplied);
        size_t tmp_size = static_cast<size_t>(tmp_stride) * h;
        std::vector<unsigned char> tmp(tmp_size);

        for (int y = 0; y < h; ++y) {
            const auto* src = m_impl->data + static_cast<size_t>(y) * m_impl->stride;
            auto* dst = reinterpret_cast<uint32_t*>(
                tmp.data() + static_cast<size_t>(y) * tmp_stride);
            for (int x = 0; x < w; ++x)
                dst[x] = pack_argb(src[x], 0, 0, 0);
        }

        gaussian_blur(tmp.data(), w, h, tmp_stride, radius);

        int new_stride = aligned_stride(w, PixelFormat::A8);
        auto* impl = make_impl(w, h, new_stride, PixelFormat::A8, m_impl->color_space_);
        impl->data = static_cast<unsigned char*>(std::malloc(
            static_cast<size_t>(new_stride) * h));
        if (!impl->data) { delete impl; return *this; }
        impl->owns_data = true;

        for (int y = 0; y < h; ++y) {
            auto* src = reinterpret_cast<const uint32_t*>(
                tmp.data() + static_cast<size_t>(y) * tmp_stride);
            auto* dst = impl->data + static_cast<size_t>(y) * new_stride;
            for (int x = 0; x < w; ++x)
                dst[x] = pixel_alpha(src[x]);
        }

        Image img;
        img.m_impl = impl;
        return img;
    }

    auto* impl = make_impl(w, h, m_impl->stride, m_impl->format, m_impl->color_space_);
    size_t buf_size = static_cast<size_t>(m_impl->stride) * h;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) { delete impl; return *this; }
    impl->owns_data = true;
    impl->headroom_ = m_impl->headroom_;
    std::memcpy(impl->data, m_impl->data, buf_size);

    bool needs_conv = (m_impl->format != PixelFormat::ARGB32_Premultiplied);

    if (needs_conv) {
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(
                impl->data + static_cast<size_t>(y) * impl->stride);
            for (int x = 0; x < w; ++x)
                row[x] = pixel_to_argb_premul(row[x], m_impl->format);
        }
    }

    gaussian_blur(impl->data, w, h, impl->stride, radius);

    if (needs_conv) {
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(
                impl->data + static_cast<size_t>(y) * impl->stride);
            for (int x = 0; x < w; ++x)
                row[x] = pixel_from_argb_premul(row[x], m_impl->format);
        }
    }

    Image img;
    img.m_impl = impl;
    return img;
}

// -- alpha_mask --

Image Image::alpha_mask() const {
    if (!m_impl) return {};

    int w = m_impl->width;
    int h = m_impl->height;
    int new_stride = aligned_stride(w, PixelFormat::A8);

    auto* impl = make_impl(w, h, new_stride, PixelFormat::A8, m_impl->color_space_);
    size_t buf_size = static_cast<size_t>(new_stride) * h;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) { delete impl; return {}; }
    impl->owns_data = true;

    if (m_impl->format == PixelFormat::A8) {
        for (int y = 0; y < h; ++y)
            std::memcpy(impl->data + static_cast<size_t>(y) * new_stride,
                        m_impl->data + static_cast<size_t>(y) * m_impl->stride, w);
    } else {
        for (int y = 0; y < h; ++y) {
            auto* s = reinterpret_cast<const uint32_t*>(
                m_impl->data + static_cast<size_t>(y) * m_impl->stride);
            auto* d = impl->data + static_cast<size_t>(y) * new_stride;
            for (int x = 0; x < w; ++x) {
                uint32_t argb = pixel_to_argb_premul(s[x], m_impl->format);
                d[x] = pixel_alpha(argb);
            }
        }
    }

    Image img;
    img.m_impl = impl;
    return img;
}

// -- Resampling kernels --

namespace {

// Bilinear (used by InterpolationQuality::Low)
uint32_t sample_bilinear(const uint32_t* data, int w, int h,
                         int stride32, float fx, float fy) {
    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    float dx = fx - x0;
    float dy = fy - y0;

    int x1 = std::min(x0 + 1, w - 1);
    int y1 = std::min(y0 + 1, h - 1);
    x0 = std::clamp(x0, 0, w - 1);
    y0 = std::clamp(y0, 0, h - 1);

    uint32_t p00 = data[static_cast<size_t>(y0) * stride32 + x0];
    uint32_t p10 = data[static_cast<size_t>(y0) * stride32 + x1];
    uint32_t p01 = data[static_cast<size_t>(y1) * stride32 + x0];
    uint32_t p11 = data[static_cast<size_t>(y1) * stride32 + x1];

    float w00 = (1 - dx) * (1 - dy);
    float w10 = dx * (1 - dy);
    float w01 = (1 - dx) * dy;
    float w11 = dx * dy;

    auto mix = [&](int shift) -> uint8_t {
        float v = ((p00 >> shift) & 0xFF) * w00
                + ((p10 >> shift) & 0xFF) * w10
                + ((p01 >> shift) & 0xFF) * w01
                + ((p11 >> shift) & 0xFF) * w11;
        return static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
    };

    return pack_argb(mix(24), mix(16), mix(8), mix(0));
}

float sample_bilinear_a8(const unsigned char* data, int w, int h,
                         int stride, float fx, float fy) {
    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    float dx = fx - x0;
    float dy = fy - y0;

    int x1 = std::min(x0 + 1, w - 1);
    int y1 = std::min(y0 + 1, h - 1);
    x0 = std::clamp(x0, 0, w - 1);
    y0 = std::clamp(y0, 0, h - 1);

    float v = data[static_cast<size_t>(y0) * stride + x0] * (1-dx)*(1-dy)
            + data[static_cast<size_t>(y0) * stride + x1] * dx*(1-dy)
            + data[static_cast<size_t>(y1) * stride + x0] * (1-dx)*dy
            + data[static_cast<size_t>(y1) * stride + x1] * dx*dy;
    return v;
}

// Mitchell-Netravali cubic kernel (B=1/3, C=1/3).
// Standard for image resizing — balances sharpness and ringing.
float mitchell(float x) {
    constexpr float B = 1.0f / 3.0f;
    constexpr float C = 1.0f / 3.0f;
    x = std::abs(x);
    if (x < 1.0f) {
        return ((12 - 9*B - 6*C) * x*x*x
              + (-18 + 12*B + 6*C) * x*x
              + (6 - 2*B)) / 6.0f;
    }
    if (x < 2.0f) {
        return ((-B - 6*C) * x*x*x
              + (6*B + 30*C) * x*x
              + (-12*B - 48*C) * x
              + (8*B + 24*C)) / 6.0f;
    }
    return 0.0f;
}

// Bicubic: 4×4 kernel (used by InterpolationQuality::Medium)
uint32_t sample_bicubic(const uint32_t* data, int w, int h,
                        int stride32, float fx, float fy) {
    int ix = static_cast<int>(std::floor(fx));
    int iy = static_cast<int>(std::floor(fy));
    float dx = fx - ix;
    float dy = fy - iy;

    float channels[4] = {};
    for (int ky = -1; ky <= 2; ++ky) {
        float wy = mitchell(dy - ky);
        int sy = std::clamp(iy + ky, 0, h - 1);
        for (int kx = -1; kx <= 2; ++kx) {
            float wxy = wy * mitchell(dx - kx);
            int sx = std::clamp(ix + kx, 0, w - 1);
            uint32_t px = data[static_cast<size_t>(sy) * stride32 + sx];
            channels[0] += ((px >> 24) & 0xFF) * wxy;
            channels[1] += ((px >> 16) & 0xFF) * wxy;
            channels[2] += ((px >> 8)  & 0xFF) * wxy;
            channels[3] += (px         & 0xFF) * wxy;
        }
    }

    auto clamp_u8 = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
    };
    return pack_argb(clamp_u8(channels[0]), clamp_u8(channels[1]),
                     clamp_u8(channels[2]), clamp_u8(channels[3]));
}

float sample_bicubic_a8(const unsigned char* data, int w, int h,
                        int stride, float fx, float fy) {
    int ix = static_cast<int>(std::floor(fx));
    int iy = static_cast<int>(std::floor(fy));
    float dx = fx - ix;
    float dy = fy - iy;

    float v = 0.0f;
    for (int ky = -1; ky <= 2; ++ky) {
        float wy = mitchell(dy - ky);
        int sy = std::clamp(iy + ky, 0, h - 1);
        for (int kx = -1; kx <= 2; ++kx) {
            float wxy = wy * mitchell(dx - kx);
            int sx = std::clamp(ix + kx, 0, w - 1);
            v += data[static_cast<size_t>(sy) * stride + sx] * wxy;
        }
    }
    return v;
}

// Lanczos-3 kernel: windowed sinc with 3 lobes.
float lanczos3(float x) {
    constexpr float pi_f = std::numbers::pi_v<float>;
    x = std::abs(x);
    if (x < 1e-6f) return 1.0f;
    if (x >= 3.0f) return 0.0f;
    float px = pi_f * x;
    return (3.0f * std::sin(px) * std::sin(px / 3.0f)) / (px * px);
}

// Lanczos: 6×6 kernel (used by InterpolationQuality::High)
uint32_t sample_lanczos(const uint32_t* data, int w, int h,
                        int stride32, float fx, float fy) {
    int ix = static_cast<int>(std::floor(fx));
    int iy = static_cast<int>(std::floor(fy));
    float dx = fx - ix;
    float dy = fy - iy;

    float channels[4] = {};
    float weight_sum = 0.0f;

    for (int ky = -2; ky <= 3; ++ky) {
        float wy = lanczos3(dy - ky);
        int sy = std::clamp(iy + ky, 0, h - 1);
        for (int kx = -2; kx <= 3; ++kx) {
            float wxy = wy * lanczos3(dx - kx);
            weight_sum += wxy;
            int sx = std::clamp(ix + kx, 0, w - 1);
            uint32_t px = data[static_cast<size_t>(sy) * stride32 + sx];
            channels[0] += ((px >> 24) & 0xFF) * wxy;
            channels[1] += ((px >> 16) & 0xFF) * wxy;
            channels[2] += ((px >> 8)  & 0xFF) * wxy;
            channels[3] += (px         & 0xFF) * wxy;
        }
    }

    // Normalize to compensate for edge clamping.
    if (weight_sum > 1e-6f) {
        float inv = 1.0f / weight_sum;
        for (float& c : channels) c *= inv;
    }

    auto clamp_u8 = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
    };
    return pack_argb(clamp_u8(channels[0]), clamp_u8(channels[1]),
                     clamp_u8(channels[2]), clamp_u8(channels[3]));
}

float sample_lanczos_a8(const unsigned char* data, int w, int h,
                        int stride, float fx, float fy) {
    int ix = static_cast<int>(std::floor(fx));
    int iy = static_cast<int>(std::floor(fy));
    float dx = fx - ix;
    float dy = fy - iy;

    float v = 0.0f;
    float weight_sum = 0.0f;

    for (int ky = -2; ky <= 3; ++ky) {
        float wy = lanczos3(dy - ky);
        int sy = std::clamp(iy + ky, 0, h - 1);
        for (int kx = -2; kx <= 3; ++kx) {
            float wxy = wy * lanczos3(dx - kx);
            weight_sum += wxy;
            int sx = std::clamp(ix + kx, 0, w - 1);
            v += data[static_cast<size_t>(sy) * stride + sx] * wxy;
        }
    }

    if (weight_sum > 1e-6f)
        v /= weight_sum;
    return v;
}

} // anonymous namespace

// -- scaled (by dimensions) --

Image Image::scaled(int new_width, int new_height,
                    InterpolationQuality quality) const {
    if (!m_impl || new_width <= 0 || new_height <= 0) return {};

    int w = m_impl->width;
    int h = m_impl->height;
    if (new_width == w && new_height == h) return *this;

    // Default → Medium (bicubic).
    if (quality == InterpolationQuality::Default)
        quality = InterpolationQuality::Medium;

    // A8: single-channel path
    if (m_impl->format == PixelFormat::A8) {
        int new_stride = aligned_stride(new_width, PixelFormat::A8);
        auto* impl = make_impl(new_width, new_height, new_stride,
                               PixelFormat::A8, m_impl->color_space_);
        size_t buf_size = static_cast<size_t>(new_stride) * new_height;
        impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
        if (!impl->data) { delete impl; return {}; }
        impl->owns_data = true;

        float sx = static_cast<float>(w) / new_width;
        float sy = static_cast<float>(h) / new_height;

        for (int y = 0; y < new_height; ++y) {
            auto* dst = impl->data + static_cast<size_t>(y) * new_stride;
            float fy = (y + 0.5f) * sy - 0.5f;

            for (int x = 0; x < new_width; ++x) {
                float fx = (x + 0.5f) * sx - 0.5f;
                float v;

                switch (quality) {
                case InterpolationQuality::None: {
                    int ix = std::clamp(static_cast<int>(fx + 0.5f), 0, w - 1);
                    int iy = std::clamp(static_cast<int>(fy + 0.5f), 0, h - 1);
                    dst[x] = m_impl->data[static_cast<size_t>(iy) * m_impl->stride + ix];
                    continue;
                }
                case InterpolationQuality::Low:
                    v = sample_bilinear_a8(m_impl->data, w, h,
                                           m_impl->stride, fx, fy);
                    break;
                case InterpolationQuality::Medium:
                    v = sample_bicubic_a8(m_impl->data, w, h,
                                          m_impl->stride, fx, fy);
                    break;
                case InterpolationQuality::High:
                    v = sample_lanczos_a8(m_impl->data, w, h,
                                          m_impl->stride, fx, fy);
                    break;
                default:
                    v = sample_bilinear_a8(m_impl->data, w, h,
                                           m_impl->stride, fx, fy);
                    break;
                }
                dst[x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
        }

        Image img;
        img.m_impl = impl;
        return img;
    }

    // 32-bit path: convert to ARGB premul if needed, resample, convert back.
    int new_stride = aligned_stride(new_width, m_impl->format);
    auto* impl = make_impl(new_width, new_height, new_stride,
                           m_impl->format, m_impl->color_space_);
    size_t buf_size = static_cast<size_t>(new_stride) * new_height;
    impl->data = static_cast<unsigned char*>(std::malloc(buf_size));
    if (!impl->data) { delete impl; return {}; }
    impl->owns_data = true;
    impl->headroom_ = m_impl->headroom_;

    bool needs_conv = (m_impl->format != PixelFormat::ARGB32_Premultiplied);
    int src_stride32;
    const uint32_t* src32;
    std::vector<uint32_t> tmp_src;

    if (needs_conv) {
        tmp_src.resize(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            auto* s = reinterpret_cast<const uint32_t*>(
                m_impl->data + static_cast<size_t>(y) * m_impl->stride);
            auto* d = tmp_src.data() + static_cast<size_t>(y) * w;
            for (int x = 0; x < w; ++x)
                d[x] = pixel_to_argb_premul(s[x], m_impl->format);
        }
        src32 = tmp_src.data();
        src_stride32 = w;
    } else {
        src32 = reinterpret_cast<const uint32_t*>(m_impl->data);
        src_stride32 = m_impl->stride / 4;
    }

    float sx = static_cast<float>(w) / new_width;
    float sy = static_cast<float>(h) / new_height;

    for (int y = 0; y < new_height; ++y) {
        auto* dst_row = reinterpret_cast<uint32_t*>(
            impl->data + static_cast<size_t>(y) * new_stride);
        float fy = (y + 0.5f) * sy - 0.5f;

        for (int x = 0; x < new_width; ++x) {
            float fx = (x + 0.5f) * sx - 0.5f;

            uint32_t argb;
            switch (quality) {
            case InterpolationQuality::None: {
                int ix = std::clamp(static_cast<int>(fx + 0.5f), 0, w - 1);
                int iy = std::clamp(static_cast<int>(fy + 0.5f), 0, h - 1);
                argb = src32[static_cast<size_t>(iy) * src_stride32 + ix];
                break;
            }
            case InterpolationQuality::Low:
                argb = sample_bilinear(src32, w, h, src_stride32, fx, fy);
                break;
            case InterpolationQuality::Medium:
                argb = sample_bicubic(src32, w, h, src_stride32, fx, fy);
                break;
            case InterpolationQuality::High:
                argb = sample_lanczos(src32, w, h, src_stride32, fx, fy);
                break;
            default:
                argb = sample_bilinear(src32, w, h, src_stride32, fx, fy);
                break;
            }

            dst_row[x] = needs_conv
                ? pixel_from_argb_premul(argb, m_impl->format)
                : argb;
        }
    }

    Image img;
    img.m_impl = impl;
    return img;
}

// -- scaled (by factor) --

Image Image::scaled(float factor, InterpolationQuality quality) const {
    if (!m_impl || factor <= 0.0f) return {};
    int nw = std::max(1, static_cast<int>(std::round(m_impl->width * factor)));
    int nh = std::max(1, static_cast<int>(std::round(m_impl->height * factor)));
    return scaled(nw, nh, quality);
}

// -- Write helpers --

namespace {

bool infer_format(const char* filename, ImageFormat& fmt) {
    const char* dot = std::strrchr(filename, '.');
    if (!dot) return false;
    ++dot;

    auto ci_eq = [](const char* a, const char* b) -> bool {
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
            if (ca != cb) return false;
            ++a; ++b;
        }
        return *a == *b;
    };

    if (ci_eq(dot, "png"))                        { fmt = ImageFormat::PNG; return true; }
    if (ci_eq(dot, "jpg") || ci_eq(dot, "jpeg"))  { fmt = ImageFormat::JPG; return true; }
    if (ci_eq(dot, "bmp"))                        { fmt = ImageFormat::BMP; return true; }
    if (ci_eq(dot, "tga"))                        { fmt = ImageFormat::TGA; return true; }
    if (ci_eq(dot, "hdr"))                        { fmt = ImageFormat::HDR; return true; }
    return false;
}

// Any pixel format → RGBA straight alpha for stb_image_write
std::vector<unsigned char> to_rgba_straight(const Image::Impl* p) {
    int w = p->width;
    int h = p->height;
    int dst_stride = w * 4;
    std::vector<unsigned char> rgba(static_cast<size_t>(dst_stride) * h);

    for (int y = 0; y < h; ++y) {
        const auto* src_row = p->data + static_cast<size_t>(y) * p->stride;
        auto* dst_row = rgba.data() + static_cast<size_t>(y) * dst_stride;

        if (p->format == PixelFormat::A8) {
            for (int x = 0; x < w; ++x) {
                uint8_t a = src_row[x];
                dst_row[x * 4 + 0] = 255;
                dst_row[x * 4 + 1] = 255;
                dst_row[x * 4 + 2] = 255;
                dst_row[x * 4 + 3] = a;
            }
        } else {
            auto* s32 = reinterpret_cast<const uint32_t*>(src_row);
            for (int x = 0; x < w; ++x) {
                uint32_t argb = pixel_to_argb_premul(s32[x], p->format);
                auto [r, g, b] = unpremultiply(argb);
                dst_row[x * 4 + 0] = r;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = b;
                dst_row[x * 4 + 3] = pixel_alpha(argb);
            }
        }
    }
    return rgba;
}

// HDR write: reconstruct float data using headroom metadata.
std::vector<float> to_rgba_hdr(const Image::Impl* p) {
    int w = p->width;
    int h = p->height;
    std::vector<float> hdr(static_cast<size_t>(w) * h * 4);
    float scale = p->headroom_;

    for (int y = 0; y < h; ++y) {
        const auto* src_row = p->data + static_cast<size_t>(y) * p->stride;
        auto* dst = hdr.data() + static_cast<size_t>(y) * w * 4;

        if (p->format == PixelFormat::A8) {
            for (int x = 0; x < w; ++x) {
                float a = src_row[x] / 255.0f;
                dst[x * 4 + 0] = scale;
                dst[x * 4 + 1] = scale;
                dst[x * 4 + 2] = scale;
                dst[x * 4 + 3] = a;
            }
        } else {
            auto* s32 = reinterpret_cast<const uint32_t*>(src_row);
            for (int x = 0; x < w; ++x) {
                uint32_t argb = pixel_to_argb_premul(s32[x], p->format);
                auto [r, g, b] = unpremultiply(argb);
                dst[x * 4 + 0] = (r / 255.0f) * scale;
                dst[x * 4 + 1] = (g / 255.0f) * scale;
                dst[x * 4 + 2] = (b / 255.0f) * scale;
                dst[x * 4 + 3] = pixel_alpha(argb) / 255.0f;
            }
        }
    }
    return hdr;
}

bool write_to_func(const Image::Impl* p, ImageFormat format, int quality,
                   stbi::WriteFunc func) {
    int w = p->width;
    int h = p->height;

    stbi::WriteOptions opts;
    opts.stride_bytes = w * 4;
    opts.jpeg_quality = quality;

    if (format == ImageFormat::HDR) {
        auto hdr = to_rgba_hdr(p);
        return stbi::write_hdr_to_func(func, w, h, 4, hdr.data(), opts);
    }

    auto rgba = to_rgba_straight(p);

    switch (format) {
    case ImageFormat::PNG:
        return stbi::write_png_to_func(func, w, h, 4, rgba.data(), opts);
    case ImageFormat::JPG:
        return stbi::write_jpg_to_func(func, w, h, 4, rgba.data(), opts);
    case ImageFormat::BMP:
        return stbi::write_bmp_to_func(func, w, h, 4, rgba.data(), opts);
    case ImageFormat::TGA:
        return stbi::write_tga_to_func(func, w, h, 4, rgba.data(), opts);
    case ImageFormat::HDR:
        break; // handled above
    }
    return false;
}

} // anonymous namespace

// -- write(filename, format) --

bool Image::write(const char* filename, ImageFormat format) const {
    return write(filename, format, 90);
}

// -- write(filename, format, quality) --

bool Image::write(const char* filename, ImageFormat format, int quality) const {
    detail::clear_last_error();

    if (!m_impl || !filename) {
        detail::set_last_error(Error::InvalidArgument);
        return false;
    }

    FILE* f = std::fopen(filename, "wb");
    if (!f) {
        detail::set_last_error(Error::FileIOError);
        return false;
    }

    auto file_writer = [f](std::span<const uint8_t> data) {
        std::fwrite(data.data(), 1, data.size(), f);
    };
    bool ok = write_to_func(m_impl, format, quality, stbi::WriteFunc(file_writer));
    std::fclose(f);

    if (!ok) {
        std::remove(filename);
        detail::set_last_error(Error::FileIOError);
    }
    return ok;
}

// -- write(filename) auto-detect --

bool Image::write(const char* filename) const {
    detail::clear_last_error();

    if (!filename) {
        detail::set_last_error(Error::InvalidArgument);
        return false;
    }
    ImageFormat fmt;
    if (!infer_format(filename, fmt)) {
        detail::set_last_error(Error::UnsupportedFormat);
        return false;
    }
    return write(filename, fmt);
}

// -- write_stream --

bool Image::write_stream(WriteFunc func, void* info, ImageFormat format) const {
    return write_stream(func, info, format, 90);
}

bool Image::write_stream(WriteFunc func, void* info,
                         ImageFormat format, int quality) const {
    detail::clear_last_error();

    if (!m_impl || !func) {
        detail::set_last_error(Error::InvalidArgument);
        return false;
    }

    auto bridge = [&](std::span<const uint8_t> data) {
        func(info, const_cast<void*>(static_cast<const void*>(data.data())),
             static_cast<int>(data.size()));
    };
    bool ok = write_to_func(m_impl, format, quality, stbi::WriteFunc(bridge));
    if (!ok)
        detail::set_last_error(Error::FileIOError);
    return ok;
}

// -- encode: in-memory encoding --

std::vector<std::byte> Image::encode(ImageFormat format) const {
    return encode(format, 90);
}

std::vector<std::byte> Image::encode(ImageFormat format, int quality) const {
    detail::clear_last_error();

    if (!m_impl) {
        detail::set_last_error(Error::InvalidArgument);
        return {};
    }

    std::vector<std::byte> result;
    result.reserve(static_cast<size_t>(m_impl->width) * m_impl->height);

    auto collector = [&result](std::span<const uint8_t> chunk) {
        auto* p = reinterpret_cast<const std::byte*>(chunk.data());
        result.insert(result.end(), p, p + chunk.size());
    };
    bool ok = write_to_func(m_impl, format, quality, stbi::WriteFunc(collector));
    if (!ok) {
        detail::set_last_error(Error::FileIOError);
        return {};
    }
    return result;
}

} // namespace kvg
