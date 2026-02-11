// plutovg-surface.cpp - Surface implementation
// C++20

#include "plutovg-private.hpp"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "plutovg-stb-image-write.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "plutovg-stb-image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace plutovg {

namespace {

constexpr int kMaxSurfaceSize = 1 << 15;

Surface::Impl* create_impl_uninitialized(int width, int height,
                                          PixelFormat format = PixelFormat::ARGB32_Premultiplied) {
    if (width <= 0 || height <= 0 || width >= kMaxSurfaceSize || height >= kMaxSurfaceSize)
        return nullptr;

    auto info = pixel_format_info(format);
    const int stride = width * info.bpp;
    const size_t data_size = static_cast<size_t>(stride) * height;
    auto* data = static_cast<unsigned char*>(std::malloc(data_size));
    if (!data)
        return nullptr;

    auto* impl = new Surface::Impl;
    impl->width = width;
    impl->height = height;
    impl->stride = stride;
    impl->data = data;
    impl->owns_data = true;
    impl->format = format;
    return impl;
}

} // namespace

// -- COW helpers --

static Surface::Impl* detach(Surface::Impl*& impl) {
    if (!impl)
        return nullptr;
    if (impl->count() == 1)
        return impl;

    // Wrapped surfaces must not be COW-detached. If we get here with a
    // writable_external surface that has refcount > 1, the caller has
    // incorrectly shared a wrapped surface and then tried to mutate it.
    if (impl->writable_external) {
        std::abort(); // Fatal: COW detach on a wrapped (zero-copy) surface.
    }

    auto* copy = create_impl_uninitialized(impl->width, impl->height, impl->format);
    if (copy) {
        std::memcpy(copy->data, impl->data,
                     static_cast<size_t>(impl->height) * impl->stride);
        copy->scale_factor = impl->scale_factor;
    }

    if (impl->deref())
        delete impl;
    impl = copy;
    return impl;
}

// -- Constructors / destructor / copy / move --

Surface::Surface() : m_impl(nullptr) {}

Surface::~Surface() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

Surface::Surface(const Surface& other) : m_impl(other.m_impl) {
    if (m_impl)
        m_impl->ref();
}

Surface::Surface(Surface&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

Surface& Surface::operator=(const Surface& other) {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        if (m_impl)
            m_impl->ref();
    }
    return *this;
}

Surface& Surface::operator=(Surface&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Surface::operator bool() const {
    return m_impl && m_impl->data;
}

// -- Static factories --

Surface Surface::create(int width, int height, PixelFormat format) {
    auto* impl = create_impl_uninitialized(width, height, format);
    if (!impl)
        return {};

    std::memset(impl->data, 0, static_cast<size_t>(impl->height) * impl->stride);

    Surface surface;
    surface.m_impl = impl;
    return surface;
}

Surface Surface::create_for_data(unsigned char* data, int width, int height, int stride,
                                  PixelFormat format) {
    if (!data || width <= 0 || height <= 0
        || width >= kMaxSurfaceSize || height >= kMaxSurfaceSize
        || stride < min_stride(width, format))
        return {};

    auto* impl = new Impl;
    impl->width = width;
    impl->height = height;
    impl->stride = stride;
    impl->data = data;
    impl->owns_data = false;
    impl->format = format;

    Surface surface;
    surface.m_impl = impl;
    return surface;
}

Surface Surface::wrap(unsigned char* data, int width, int height, int stride,
                       PixelFormat format) {
    if (!data || width <= 0 || height <= 0
        || width >= kMaxSurfaceSize || height >= kMaxSurfaceSize
        || stride < min_stride(width, format))
        return {};

    auto* impl = new Impl;
    impl->width = width;
    impl->height = height;
    impl->stride = stride;
    impl->data = data;
    impl->owns_data = false;
    impl->writable_external = true;
    impl->format = format;

    Surface surface;
    surface.m_impl = impl;
    return surface;
}

Surface Surface::load_from_image_file(const char* filename) {
    int width, height, channels;
    stbi_uc* image = stbi_load(filename, &width, &height, &channels, STBI_rgb_alpha);
    if (!image)
        return {};

    auto* impl = create_impl_uninitialized(width, height);
    if (!impl) {
        stbi_image_free(image);
        return {};
    }

    convert_rgba_to_argb(impl->data, image, impl->width, impl->height, impl->stride);
    stbi_image_free(image);

    Surface surface;
    surface.m_impl = impl;
    return surface;
}

Surface Surface::load_from_image_data(std::span<const std::byte> data) {
    int width, height, channels;
    stbi_uc* image = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(data.data()),
        static_cast<int>(data.size()),
        &width, &height, &channels, STBI_rgb_alpha);
    if (!image)
        return {};

    auto* impl = create_impl_uninitialized(width, height);
    if (!impl) {
        stbi_image_free(image);
        return {};
    }

    convert_rgba_to_argb(impl->data, image, impl->width, impl->height, impl->stride);
    stbi_image_free(image);

    Surface surface;
    surface.m_impl = impl;
    return surface;
}

Surface Surface::load_from_image_base64(std::string_view data) {
    if (data.empty())
        return {};

    constexpr uint8_t base64_table[128] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x3F,
        0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
        0x3C, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
        0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
        0x17, 0x18, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
        0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    auto output = std::make_unique<uint8_t[]>(data.size());
    size_t output_length = 0;
    size_t equals_sign_count = 0;

    for (size_t i = 0; i < data.size(); ++i) {
        auto cc = static_cast<uint8_t>(data[i]);
        if (cc == '=') {
            ++equals_sign_count;
        } else if (cc == '+' || cc == '/' || is_alnum(static_cast<char>(cc))) {
            if (equals_sign_count > 0)
                return {};
            output[output_length++] = (cc < 128) ? base64_table[cc] : 0;
        } else if (!is_ws(static_cast<char>(cc))) {
            return {};
        }
    }

    if (output_length == 0 || equals_sign_count > 2 || (output_length % 4) == 1)
        return {};

    output_length -= (output_length + 3) / 4;
    if (output_length == 0)
        return {};

    size_t sidx = 0;
    size_t didx = 0;

    if (output_length > 1) {
        while (didx < output_length - 2) {
            output[didx + 0] = ((output[sidx + 0] << 2) & 0xFF) | ((output[sidx + 1] >> 4) & 0x03);
            output[didx + 1] = ((output[sidx + 1] << 4) & 0xFF) | ((output[sidx + 2] >> 2) & 0x0F);
            output[didx + 2] = ((output[sidx + 2] << 6) & 0xFF) | ((output[sidx + 3] >> 0) & 0x3F);
            sidx += 4;
            didx += 3;
        }
    }

    if (didx < output_length)
        output[didx] = ((output[sidx + 0] << 2) & 0xFF) | ((output[sidx + 1] >> 4) & 0x03);
    if (++didx < output_length)
        output[didx] = ((output[sidx + 1] << 4) & 0xFF) | ((output[sidx + 2] >> 2) & 0x0F);

    return load_from_image_data({reinterpret_cast<const std::byte*>(output.get()),
                                  output_length});
}

// -- Accessors --

const unsigned char* Surface::data() const noexcept {
    return m_impl ? m_impl->data : nullptr;
}

unsigned char* Surface::mutable_data() {
    if (!m_impl)
        return nullptr;
    // Wrapped surfaces are always directly writable — skip COW detach.
    if (!m_impl->writable_external)
        detach(m_impl);
    return m_impl ? m_impl->data : nullptr;
}

int Surface::width() const noexcept {
    return m_impl ? m_impl->width : 0;
}

int Surface::height() const noexcept {
    return m_impl ? m_impl->height : 0;
}

int Surface::stride() const noexcept {
    return m_impl ? m_impl->stride : 0;
}

PixelFormat Surface::format() const noexcept {
    return m_impl ? m_impl->format : PixelFormat::ARGB32_Premultiplied;
}

int Surface::bytes_per_pixel() const noexcept {
    return m_impl ? pixel_format_info(m_impl->format).bpp : 0;
}

bool Surface::is_wrapped() const noexcept {
    return m_impl && m_impl->writable_external;
}

float Surface::scale_factor() const noexcept {
    return m_impl ? m_impl->scale_factor : 1.0f;
}

void Surface::set_scale_factor(float scale) {
    if (!m_impl || scale <= 0.0f) return;
    m_impl->scale_factor = scale;
}

float Surface::logical_width() const noexcept {
    if (!m_impl || m_impl->scale_factor <= 0.0f) return 0.0f;
    return static_cast<float>(m_impl->width) / m_impl->scale_factor;
}

float Surface::logical_height() const noexcept {
    if (!m_impl || m_impl->scale_factor <= 0.0f) return 0.0f;
    return static_cast<float>(m_impl->height) / m_impl->scale_factor;
}

// -- Clear --

void Surface::clear(const Color& color) {
    if (!m_impl)
        return;
    detach(m_impl);
    if (!m_impl)
        return;

    if (m_impl->format == PixelFormat::A8) {
        // A8: fill with alpha channel value only.
        uint8_t a = static_cast<uint8_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f + 0.5f);
        for (int y = 0; y < m_impl->height; ++y)
            std::memset(m_impl->data + y * m_impl->stride, a, m_impl->width);
        return;
    }

    // Compute the pixel value in the surface's native format.
    uint32_t argb_premul = premultiply_argb(color.to_argb32());
    uint32_t pixel = pixel_from_argb_premul(argb_premul, m_impl->format);

    for (int y = 0; y < m_impl->height; ++y) {
        auto* pixels = reinterpret_cast<uint32_t*>(m_impl->data + m_impl->stride * y);
        memfill32(pixels, m_impl->width, pixel);
    }
}

// -- Filters --

void Surface::apply_blur(float radius) {
    if (!m_impl || radius <= 0.0f)
        return;
    detach(m_impl);
    if (!m_impl)
        return;
    gaussian_blur(m_impl->data, m_impl->width, m_impl->height, m_impl->stride, radius);
}

Surface Surface::blurred(float radius) const {
    if (!m_impl || radius <= 0.0f)
        return *this;

    Surface copy = *this;
    copy.apply_blur(radius);
    return copy;
}

// -- Color matrix filter --

void Surface::apply_color_matrix(const float matrix[20]) {
    if (!m_impl)
        return;
    detach(m_impl);
    if (!m_impl)
        return;

    for (int y = 0; y < m_impl->height; ++y) {
        auto* pixels = reinterpret_cast<uint32_t*>(m_impl->data + m_impl->stride * y);
        for (int x = 0; x < m_impl->width; ++x) {
            auto [r, g, b, a] = unpremultiply_f(pixels[x]);

            float r_new = matrix[0]*r + matrix[1]*g + matrix[2]*b  + matrix[3]*a + matrix[4];
            float g_new = matrix[5]*r + matrix[6]*g + matrix[7]*b  + matrix[8]*a + matrix[9];
            float b_new = matrix[10]*r + matrix[11]*g + matrix[12]*b + matrix[13]*a + matrix[14];
            float a_new = matrix[15]*r + matrix[16]*g + matrix[17]*b + matrix[18]*a + matrix[19];

            r_new = std::clamp(r_new, 0.0f, 1.0f);
            g_new = std::clamp(g_new, 0.0f, 1.0f);
            b_new = std::clamp(b_new, 0.0f, 1.0f);
            a_new = std::clamp(a_new, 0.0f, 1.0f);

            auto a8 = static_cast<uint8_t>(a_new * 255.0f + 0.5f);
            auto r8 = static_cast<uint8_t>(r_new * a_new * 255.0f + 0.5f);
            auto g8 = static_cast<uint8_t>(g_new * a_new * 255.0f + 0.5f);
            auto b8 = static_cast<uint8_t>(b_new * a_new * 255.0f + 0.5f);
            pixels[x] = pack_argb(a8, r8, g8, b8);
        }
    }
}

void Surface::apply_color_matrix(const ColorMatrix& cm) {
    apply_color_matrix(cm.m);
}

Surface Surface::color_matrix_transformed(const float matrix[20]) const {
    if (!m_impl)
        return {};
    Surface copy = *this;
    copy.apply_color_matrix(matrix);
    return copy;
}

Surface Surface::color_matrix_transformed(const ColorMatrix& cm) const {
    return color_matrix_transformed(cm.m);
}

// -- ColorMatrix factories --

ColorMatrix ColorMatrix::grayscale() {
    return saturate(0.0f);
}

ColorMatrix ColorMatrix::sepia() {
    ColorMatrix cm;
    cm.m[0]  = 0.393f; cm.m[1]  = 0.769f; cm.m[2]  = 0.189f; cm.m[3]  = 0; cm.m[4]  = 0;
    cm.m[5]  = 0.349f; cm.m[6]  = 0.686f; cm.m[7]  = 0.168f; cm.m[8]  = 0; cm.m[9]  = 0;
    cm.m[10] = 0.272f; cm.m[11] = 0.534f; cm.m[12] = 0.131f; cm.m[13] = 0; cm.m[14] = 0;
    cm.m[15] = 0;      cm.m[16] = 0;      cm.m[17] = 0;      cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::saturate(float amount) {
    // SVG feColorMatrix saturate formula (BT.709 weights)
    constexpr float lr = 0.2126f;
    constexpr float lg = 0.7152f;
    constexpr float lb = 0.0722f;
    float s = amount;
    ColorMatrix cm;
    cm.m[0]  = lr + (1 - lr) * s;  cm.m[1]  = lg - lg * s;        cm.m[2]  = lb - lb * s;        cm.m[3]  = 0; cm.m[4]  = 0;
    cm.m[5]  = lr - lr * s;        cm.m[6]  = lg + (1 - lg) * s;  cm.m[7]  = lb - lb * s;        cm.m[8]  = 0; cm.m[9]  = 0;
    cm.m[10] = lr - lr * s;        cm.m[11] = lg - lg * s;        cm.m[12] = lb + (1 - lb) * s;  cm.m[13] = 0; cm.m[14] = 0;
    cm.m[15] = 0;                  cm.m[16] = 0;                  cm.m[17] = 0;                  cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::brightness(float amount) {
    ColorMatrix cm;
    cm.m[0]  = amount; cm.m[1]  = 0;      cm.m[2]  = 0;      cm.m[3]  = 0; cm.m[4]  = 0;
    cm.m[5]  = 0;      cm.m[6]  = amount; cm.m[7]  = 0;      cm.m[8]  = 0; cm.m[9]  = 0;
    cm.m[10] = 0;      cm.m[11] = 0;      cm.m[12] = amount; cm.m[13] = 0; cm.m[14] = 0;
    cm.m[15] = 0;      cm.m[16] = 0;      cm.m[17] = 0;      cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::contrast(float amount) {
    float bias = (1.0f - amount) * 0.5f;
    ColorMatrix cm;
    cm.m[0]  = amount; cm.m[1]  = 0;      cm.m[2]  = 0;      cm.m[3]  = 0; cm.m[4]  = bias;
    cm.m[5]  = 0;      cm.m[6]  = amount; cm.m[7]  = 0;      cm.m[8]  = 0; cm.m[9]  = bias;
    cm.m[10] = 0;      cm.m[11] = 0;      cm.m[12] = amount; cm.m[13] = 0; cm.m[14] = bias;
    cm.m[15] = 0;      cm.m[16] = 0;      cm.m[17] = 0;      cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::hue_rotate(float radians) {
    // SVG feColorMatrix hueRotate formula
    constexpr float lr = 0.2126f;
    constexpr float lg = 0.7152f;
    constexpr float lb = 0.0722f;
    float c = std::cos(radians);
    float s = std::sin(radians);
    ColorMatrix cm;
    cm.m[0]  = lr + (1-lr)*c - lr*s;
    cm.m[1]  = lg - lg*c - lg*s;
    cm.m[2]  = lb - lb*c + (1-lb)*s;
    cm.m[3]  = 0; cm.m[4] = 0;
    cm.m[5]  = lr - lr*c + 0.143f*s;
    cm.m[6]  = lg + (1-lg)*c + 0.140f*s;
    cm.m[7]  = lb - lb*c - 0.283f*s;
    cm.m[8]  = 0; cm.m[9] = 0;
    cm.m[10] = lr - lr*c - (1-lr)*s;
    cm.m[11] = lg - lg*c + lg*s;
    cm.m[12] = lb + (1-lb)*c + lb*s;
    cm.m[13] = 0; cm.m[14] = 0;
    cm.m[15] = 0; cm.m[16] = 0; cm.m[17] = 0; cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::invert() {
    ColorMatrix cm;
    cm.m[0]  = -1; cm.m[1]  = 0;  cm.m[2]  = 0;  cm.m[3]  = 0; cm.m[4]  = 1;
    cm.m[5]  = 0;  cm.m[6]  = -1; cm.m[7]  = 0;  cm.m[8]  = 0; cm.m[9]  = 1;
    cm.m[10] = 0;  cm.m[11] = 0;  cm.m[12] = -1; cm.m[13] = 0; cm.m[14] = 1;
    cm.m[15] = 0;  cm.m[16] = 0;  cm.m[17] = 0;  cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

// -- Pixel format conversion utilities --

uint32_t pixel_to_argb_premul(uint32_t pixel, PixelFormat src_fmt) {
    switch (src_fmt) {
    case PixelFormat::ARGB32_Premultiplied:
        return pixel;
    case PixelFormat::BGRA32_Premultiplied: {
        // Memory: B G R A → ARGB
        uint8_t b_val = (pixel >> 24) & 0xFF;
        uint8_t g_val = (pixel >> 16) & 0xFF;
        uint8_t r_val = (pixel >> 8)  & 0xFF;
        uint8_t a_val = pixel & 0xFF;
        return pack_argb(a_val, r_val, g_val, b_val);
    }
    case PixelFormat::RGBA32: {
        // Memory: R G B A → straight alpha, needs premultiply
        uint8_t r_val = (pixel >> 24) & 0xFF;
        uint8_t g_val = (pixel >> 16) & 0xFF;
        uint8_t b_val = (pixel >> 8)  & 0xFF;
        uint8_t a_val = pixel & 0xFF;
        return premultiply_argb(pack_argb(a_val, r_val, g_val, b_val));
    }
    case PixelFormat::BGRA32: {
        // Memory: B G R A → straight alpha, needs premultiply
        uint8_t b_val = (pixel >> 24) & 0xFF;
        uint8_t g_val = (pixel >> 16) & 0xFF;
        uint8_t r_val = (pixel >> 8)  & 0xFF;
        uint8_t a_val = pixel & 0xFF;
        return premultiply_argb(pack_argb(a_val, r_val, g_val, b_val));
    }
    case PixelFormat::A8:
        // Should not be called for A8 (1 byte), but handle gracefully
        return pack_argb(pixel & 0xFF, 0, 0, 0);
    }
    return pixel;
}

uint32_t pixel_from_argb_premul(uint32_t argb_premul, PixelFormat dst_fmt) {
    switch (dst_fmt) {
    case PixelFormat::ARGB32_Premultiplied:
        return argb_premul;
    case PixelFormat::BGRA32_Premultiplied: {
        uint8_t a_val = alpha(argb_premul);
        uint8_t r_val = red(argb_premul);
        uint8_t g_val = green(argb_premul);
        uint8_t b_val = blue(argb_premul);
        // Memory order: B G R A
        return (uint32_t(b_val) << 24) | (uint32_t(g_val) << 16) |
               (uint32_t(r_val) << 8)  | uint32_t(a_val);
    }
    case PixelFormat::RGBA32: {
        auto [r_val, g_val, b_val] = unpremultiply(argb_premul);
        uint8_t a_val = alpha(argb_premul);
        // Memory order: R G B A
        return (uint32_t(r_val) << 24) | (uint32_t(g_val) << 16) |
               (uint32_t(b_val) << 8)  | uint32_t(a_val);
    }
    case PixelFormat::BGRA32: {
        auto [r_val, g_val, b_val] = unpremultiply(argb_premul);
        uint8_t a_val = alpha(argb_premul);
        // Memory order: B G R A
        return (uint32_t(b_val) << 24) | (uint32_t(g_val) << 16) |
               (uint32_t(r_val) << 8)  | uint32_t(a_val);
    }
    case PixelFormat::A8:
        return alpha(argb_premul);
    }
    return argb_premul;
}

void convert_scanline(const unsigned char* src, PixelFormat src_fmt,
                      unsigned char* dst, PixelFormat dst_fmt,
                      int width, uint32_t rgb_fill) {
    if (src_fmt == dst_fmt) {
        auto bpp = pixel_format_info(src_fmt).bpp;
        std::memcpy(dst, src, static_cast<size_t>(width) * bpp);
        return;
    }

    // A8 source: expand to 32-bit using rgb_fill as base color
    if (src_fmt == PixelFormat::A8) {
        for (int x = 0; x < width; ++x) {
            uint8_t cov = src[x];
            uint32_t argb = (cov == 255) ? rgb_fill : byte_mul(rgb_fill, cov);
            if (dst_fmt == PixelFormat::ARGB32_Premultiplied) {
                reinterpret_cast<uint32_t*>(dst)[x] = argb;
            } else {
                reinterpret_cast<uint32_t*>(dst)[x] = pixel_from_argb_premul(argb, dst_fmt);
            }
        }
        return;
    }

    // 32-bit source to A8 destination
    if (dst_fmt == PixelFormat::A8) {
        for (int x = 0; x < width; ++x) {
            uint32_t pixel = reinterpret_cast<const uint32_t*>(src)[x];
            uint32_t argb = pixel_to_argb_premul(pixel, src_fmt);
            dst[x] = alpha(argb);
        }
        return;
    }

    // 32-bit to 32-bit conversion via ARGB premultiplied
    for (int x = 0; x < width; ++x) {
        uint32_t pixel = reinterpret_cast<const uint32_t*>(src)[x];
        uint32_t argb = pixel_to_argb_premul(pixel, src_fmt);
        reinterpret_cast<uint32_t*>(dst)[x] = pixel_from_argb_premul(argb, dst_fmt);
    }
}

// -- Surface pixel format conversion --

void Surface::convert_to(PixelFormat target_format) {
    if (!m_impl) return;
    if (m_impl->format == target_format) return;

    auto src_info = pixel_format_info(m_impl->format);
    auto dst_info = pixel_format_info(target_format);

    if (src_info.bpp == dst_info.bpp) {
        // Same BPP — convert in place.
        detach(m_impl);
        if (!m_impl) return;

        for (int y = 0; y < m_impl->height; ++y) {
            auto* row = m_impl->data + y * m_impl->stride;
            convert_scanline(row, m_impl->format, row, target_format, m_impl->width);
        }
        m_impl->format = target_format;
    } else {
        // Different BPP — need new buffer.
        auto* new_impl = create_impl_uninitialized(m_impl->width, m_impl->height, target_format);
        if (!new_impl) return;
        new_impl->scale_factor = m_impl->scale_factor;

        for (int y = 0; y < m_impl->height; ++y) {
            const auto* src_row = m_impl->data + y * m_impl->stride;
            auto* dst_row = new_impl->data + y * new_impl->stride;
            convert_scanline(src_row, m_impl->format, dst_row, target_format, m_impl->width);
        }

        if (m_impl->deref())
            delete m_impl;
        m_impl = new_impl;
    }
}

Surface Surface::converted(PixelFormat target_format) const {
    if (!m_impl) return {};
    if (m_impl->format == target_format) return *this;

    Surface copy = *this;
    copy.convert_to(target_format);
    return copy;
}

// -- Surface compositing --

// Apply a Porter-Duff operator to a single pixel pair (both premultiplied ARGB).
static inline uint32_t apply_operator(uint32_t src, uint32_t dst, Operator op) {
    uint32_t sa = alpha(src);
    uint32_t da = alpha(dst);
    uint32_t inv_sa = 255 - sa;
    uint32_t inv_da = 255 - da;

    switch (op) {
    case Operator::Clear:
        return 0;
    case Operator::Src:
        return src;
    case Operator::Dst:
        return dst;
    case Operator::SrcOver:
        return src + byte_mul(dst, inv_sa);
    case Operator::DstOver:
        return byte_mul(src, inv_da) + dst;
    case Operator::SrcIn:
        return byte_mul(src, da);
    case Operator::DstIn:
        return byte_mul(dst, sa);
    case Operator::SrcOut:
        return byte_mul(src, inv_da);
    case Operator::DstOut:
        return byte_mul(dst, inv_sa);
    case Operator::SrcAtop:
        return byte_mul(src, da) + byte_mul(dst, inv_sa);
    case Operator::DstAtop:
        return byte_mul(src, inv_da) + byte_mul(dst, sa);
    case Operator::Xor:
        return byte_mul(src, inv_da) + byte_mul(dst, inv_sa);
    }
    return src + byte_mul(dst, inv_sa); // default: SrcOver
}

// Apply a CSS blend mode to a single pixel pair (both premultiplied ARGB).
// Assumes SrcOver compositing underneath.
static inline uint32_t apply_blend_mode_pixel(uint32_t src, uint32_t dst, BlendMode mode) {
    if (mode == BlendMode::Normal)
        return src + byte_mul(dst, 255 - alpha(src));

    uint8_t sa = alpha(src);
    if (sa == 0) return dst;

    // Unpremultiply for blend math
    auto [sr, sg, sb] = unpremultiply(src);
    auto [dr, dg, db] = unpremultiply(dst);
    uint8_t da = alpha(dst);

    uint8_t br, bg, bb;
    switch (mode) {
    case BlendMode::Multiply:
        br = blend_ops::multiply(sr, dr);
        bg = blend_ops::multiply(sg, dg);
        bb = blend_ops::multiply(sb, db);
        break;
    case BlendMode::Screen:
        br = blend_ops::screen(sr, dr);
        bg = blend_ops::screen(sg, dg);
        bb = blend_ops::screen(sb, db);
        break;
    case BlendMode::Overlay:
        br = blend_ops::overlay(dr, sr);
        bg = blend_ops::overlay(dg, sg);
        bb = blend_ops::overlay(db, sb);
        break;
    case BlendMode::Darken:
        br = blend_ops::darken(sr, dr);
        bg = blend_ops::darken(sg, dg);
        bb = blend_ops::darken(sb, db);
        break;
    case BlendMode::Lighten:
        br = blend_ops::lighten(sr, dr);
        bg = blend_ops::lighten(sg, dg);
        bb = blend_ops::lighten(sb, db);
        break;
    case BlendMode::ColorDodge:
        br = blend_ops::color_dodge(dr, sr);
        bg = blend_ops::color_dodge(dg, sg);
        bb = blend_ops::color_dodge(db, sb);
        break;
    case BlendMode::ColorBurn:
        br = blend_ops::color_burn(dr, sr);
        bg = blend_ops::color_burn(dg, sg);
        bb = blend_ops::color_burn(db, sb);
        break;
    case BlendMode::HardLight:
        br = blend_ops::hard_light(dr, sr);
        bg = blend_ops::hard_light(dg, sg);
        bb = blend_ops::hard_light(db, sb);
        break;
    case BlendMode::SoftLight:
        br = blend_ops::soft_light(dr, sr);
        bg = blend_ops::soft_light(dg, sg);
        bb = blend_ops::soft_light(db, sb);
        break;
    case BlendMode::Difference:
        br = blend_ops::difference(sr, dr);
        bg = blend_ops::difference(sg, dg);
        bb = blend_ops::difference(sb, db);
        break;
    case BlendMode::Exclusion:
        br = blend_ops::exclusion(sr, dr);
        bg = blend_ops::exclusion(sg, dg);
        bb = blend_ops::exclusion(sb, db);
        break;
    case BlendMode::Hue:
        hsl_blend_ops::hue(sr, sg, sb, dr, dg, db, br, bg, bb);
        break;
    case BlendMode::Saturation:
        hsl_blend_ops::saturation(sr, sg, sb, dr, dg, db, br, bg, bb);
        break;
    case BlendMode::Color:
        hsl_blend_ops::color(sr, sg, sb, dr, dg, db, br, bg, bb);
        break;
    case BlendMode::Luminosity:
        hsl_blend_ops::luminosity(sr, sg, sb, dr, dg, db, br, bg, bb);
        break;
    default:
        return src + byte_mul(dst, 255 - sa);
    }

    // Composite blended result with SrcOver: result * sa + dst * (1 - sa)
    // Re-premultiply blended color
    uint8_t oa = sa + byte_mul(da, 255 - sa) > 255 ? 255 :
                 static_cast<uint8_t>(sa + ((da * (255 - sa)) / 255));
    uint8_t opr = static_cast<uint8_t>((br * sa) / 255);
    uint8_t opg = static_cast<uint8_t>((bg * sa) / 255);
    uint8_t opb = static_cast<uint8_t>((bb * sa) / 255);

    uint32_t blended = pack_argb(sa, opr, opg, opb);
    return blended + byte_mul(dst, 255 - sa);
}

void Surface::composite(const Surface& src, const IntRect& src_rect,
                         int dst_x, int dst_y,
                         Operator op, BlendMode blend_mode, float opacity) {
    if (!m_impl || !src) return;
    if (src_rect.empty()) return;
    if (opacity <= 0.0f) return;

    // Clip src_rect to source bounds.
    IntRect sr = src_rect.intersected(IntRect{0, 0, src.width(), src.height()});
    if (sr.empty()) return;

    // Clip to destination bounds.
    int dx0 = dst_x;
    int dy0 = dst_y;
    int dx1 = dst_x + sr.w;
    int dy1 = dst_y + sr.h;

    if (dx0 < 0) { sr.x -= dx0; sr.w += dx0; dx0 = 0; }
    if (dy0 < 0) { sr.y -= dy0; sr.h += dy0; dy0 = 0; }
    if (dx1 > width())  { sr.w -= (dx1 - width());  dx1 = width(); }
    if (dy1 > height()) { sr.h -= (dy1 - height()); dy1 = height(); }

    int blit_w = dx1 - dx0;
    int blit_h = dy1 - dy0;
    if (blit_w <= 0 || blit_h <= 0) return;

    // Get mutable access to destination (triggers COW detach if needed).
    unsigned char* dst_data = mutable_data();
    if (!dst_data) return;

    const unsigned char* src_data = src.data();
    int src_stride = src.stride();
    int dst_stride = stride();
    int src_bpp = src.bytes_per_pixel();
    int dst_bpp = bytes_per_pixel();

    PixelFormat src_fmt = src.format();
    PixelFormat dst_fmt = format();

    uint32_t opacity_256 = static_cast<uint32_t>(std::clamp(opacity, 0.0f, 1.0f) * 256.0f + 0.5f);

    // Fast path: Src operator, same format, full opacity, Normal blend — memcpy.
    if (op == Operator::Src && blend_mode == BlendMode::Normal
        && opacity_256 >= 256 && src_fmt == dst_fmt && dst_fmt != PixelFormat::A8) {
        for (int y = 0; y < blit_h; ++y) {
            const auto* srow = src_data + (sr.y + y) * src_stride + sr.x * src_bpp;
            auto* drow = dst_data + (dy0 + y) * dst_stride + dx0 * dst_bpp;
            std::memcpy(drow, srow, static_cast<size_t>(blit_w) * dst_bpp);
        }
        return;
    }

    // General path: work in ARGB32_Premultiplied, convert at boundaries.
    bool need_src_convert = (src_fmt != PixelFormat::ARGB32_Premultiplied);
    bool need_dst_convert = (dst_fmt != PixelFormat::ARGB32_Premultiplied);

    // Temporary scanline buffers for format conversion.
    constexpr int kMaxStackWidth = 2048;
    uint32_t src_buf_stack[kMaxStackWidth];
    uint32_t dst_buf_stack[kMaxStackWidth];
    std::unique_ptr<uint32_t[]> src_buf_heap, dst_buf_heap;

    uint32_t* src_buf = src_buf_stack;
    uint32_t* dst_buf = dst_buf_stack;

    if (blit_w > kMaxStackWidth) {
        src_buf_heap = std::make_unique<uint32_t[]>(blit_w);
        dst_buf_heap = std::make_unique<uint32_t[]>(blit_w);
        src_buf = src_buf_heap.get();
        dst_buf = dst_buf_heap.get();
    }

    for (int y = 0; y < blit_h; ++y) {
        const auto* srow = src_data + (sr.y + y) * src_stride + sr.x * src_bpp;
        auto* drow = dst_data + (dy0 + y) * dst_stride + dx0 * dst_bpp;

        // Read source scanline into ARGB premul.
        const uint32_t* src_argb;
        if (need_src_convert) {
            convert_scanline(srow, src_fmt,
                            reinterpret_cast<unsigned char*>(src_buf),
                            PixelFormat::ARGB32_Premultiplied, blit_w);
            src_argb = src_buf;
        } else {
            src_argb = reinterpret_cast<const uint32_t*>(srow);
        }

        // Read destination scanline into ARGB premul (needed for non-Src ops).
        uint32_t* dst_argb;
        if (need_dst_convert) {
            convert_scanline(drow, dst_fmt,
                            reinterpret_cast<unsigned char*>(dst_buf),
                            PixelFormat::ARGB32_Premultiplied, blit_w);
            dst_argb = dst_buf;
        } else {
            dst_argb = reinterpret_cast<uint32_t*>(drow);
        }

        // Composite each pixel.
        for (int x = 0; x < blit_w; ++x) {
            uint32_t s = src_argb[x];

            // Apply opacity to source.
            if (opacity_256 < 256)
                s = byte_mul(s, static_cast<uint32_t>(opacity_256));

            uint32_t d = dst_argb[x];
            uint32_t result;

            if (blend_mode != BlendMode::Normal) {
                result = apply_blend_mode_pixel(s, d, blend_mode);
            } else {
                result = apply_operator(s, d, op);
            }

            dst_argb[x] = result;
        }

        // Write back converted destination if needed.
        if (need_dst_convert) {
            convert_scanline(reinterpret_cast<const unsigned char*>(dst_buf),
                            PixelFormat::ARGB32_Premultiplied,
                            drow, dst_fmt, blit_w);
        }
    }
}

void Surface::composite(const Surface& src, int dst_x, int dst_y,
                         Operator op, BlendMode blend_mode, float opacity) {
    if (!src) return;
    composite(src, IntRect{0, 0, src.width(), src.height()}, dst_x, dst_y,
              op, blend_mode, opacity);
}

// -- Export helpers --

namespace {

/// Temporarily convert surface from premultiplied ARGB to RGBA for writing,
/// then convert back. RAII guard.
///
/// Takes const Impl* because the Impl struct itself isn't modified (width, height,
/// stride, refcount unchanged). The pixel buffer (impl->data, an unsigned char*)
/// is mutated in-place, which is valid because the pointer-to-mutable-data member
/// remains writable even through a pointer-to-const-Impl. The conversion is
/// strictly temporary and fully reversed in the destructor.
struct WriteGuard {
    const Surface::Impl* impl;
    unsigned char* rgba_data;
    bool owns_buffer;

    explicit WriteGuard(const Surface::Impl* impl) : impl(impl) {
        if (impl->count() > 1) {
            const size_t data_size = static_cast<size_t>(impl->height) * impl->stride;
            rgba_data = static_cast<unsigned char*>(std::malloc(data_size));
            if (rgba_data) {
                convert_argb_to_rgba(rgba_data, impl->data,
                                     impl->width, impl->height, impl->stride);
            }
            owns_buffer = true;
        } else {
            convert_argb_to_rgba(impl->data, impl->data,
                                  impl->width, impl->height, impl->stride);
            rgba_data = impl->data;
            owns_buffer = false;
        }
    }

    ~WriteGuard() {
        if (owns_buffer) {
            std::free(rgba_data);
        } else {
            convert_rgba_to_argb(impl->data, impl->data,
                                  impl->width, impl->height, impl->stride);
        }
    }

    explicit operator bool() const noexcept { return rgba_data != nullptr; }

    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;
};

} // namespace

bool Surface::write_to_png(const char* filename) const {
    if (!m_impl)
        return false;
    WriteGuard guard(m_impl);
    if (!guard)
        return false;
    return stbi_write_png(filename, m_impl->width, m_impl->height, 4,
                           guard.rgba_data, m_impl->stride) != 0;
}

bool Surface::write_to_jpg(const char* filename, int quality) const {
    if (!m_impl)
        return false;
    WriteGuard guard(m_impl);
    if (!guard)
        return false;
    return stbi_write_jpg(filename, m_impl->width, m_impl->height, 4,
                           guard.rgba_data, quality) != 0;
}

bool Surface::write_to_png_stream(WriteFunc func, void* closure) const {
    if (!m_impl)
        return false;

    struct CallbackCtx { WriteFunc fn; void* cl; };
    CallbackCtx ctx{func, closure};

    WriteGuard guard(m_impl);
    if (!guard)
        return false;
    return stbi_write_png_to_func(
        [](void* context, void* data, int size) {
            auto* c = static_cast<CallbackCtx*>(context);
            c->fn(c->cl, data, size);
        }, &ctx,
        m_impl->width, m_impl->height, 4,
        guard.rgba_data, m_impl->stride) != 0;
}

bool Surface::write_to_jpg_stream(WriteFunc func, void* closure, int quality) const {
    if (!m_impl)
        return false;

    struct CallbackCtx { WriteFunc fn; void* cl; };
    CallbackCtx ctx{func, closure};

    WriteGuard guard(m_impl);
    if (!guard)
        return false;
    return stbi_write_jpg_to_func(
        [](void* context, void* data, int size) {
            auto* c = static_cast<CallbackCtx*>(context);
            c->fn(c->cl, data, size);
        }, &ctx,
        m_impl->width, m_impl->height, 4,
        guard.rgba_data, quality) != 0;
}

// -- Pixel format conversion --

void convert_argb_to_rgba(unsigned char* dst, const unsigned char* src,
                           int width, int height, int stride) {
    for (int y = 0; y < height; ++y) {
        const auto* src_row = reinterpret_cast<const uint32_t*>(src + stride * y);
        auto* dst_row = dst + stride * y;
        for (int x = 0; x < width; ++x) {
            uint32_t pixel = src_row[x];
            auto [r, g, b] = unpremultiply(pixel);
            uint32_t a = alpha(pixel);
            dst_row[0] = r;
            dst_row[1] = g;
            dst_row[2] = b;
            dst_row[3] = static_cast<unsigned char>(a);
            dst_row += 4;
        }
    }
}

void convert_rgba_to_argb(unsigned char* dst, const unsigned char* src,
                           int width, int height, int stride) {
    for (int y = 0; y < height; ++y) {
        const auto* src_row = src + stride * y;
        auto* dst_row = reinterpret_cast<uint32_t*>(dst + stride * y);
        for (int x = 0; x < width; ++x) {
            uint32_t a = src_row[4 * x + 3];
            if (a == 0) {
                dst_row[x] = 0x00000000;
            } else {
                uint32_t r = src_row[4 * x + 0];
                uint32_t g = src_row[4 * x + 1];
                uint32_t b = src_row[4 * x + 2];
                if (a != 255) {
                    r = (r * a) / 255;
                    g = (g * a) / 255;
                    b = (b * a) / 255;
                }
                dst_row[x] = pack_argb(
                    static_cast<uint8_t>(a),
                    static_cast<uint8_t>(r),
                    static_cast<uint8_t>(g),
                    static_cast<uint8_t>(b));
            }
        }
    }
}

} // namespace plutovg
