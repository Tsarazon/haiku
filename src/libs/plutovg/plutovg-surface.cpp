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
#include <cstdlib>
#include <cstring>
#include <memory>

namespace plutovg {

namespace {

constexpr int kMaxSurfaceSize = 1 << 15;

Surface::Impl* create_impl_uninitialized(int width, int height) {
    if (width <= 0 || height <= 0 || width >= kMaxSurfaceSize || height >= kMaxSurfaceSize)
        return nullptr;

    const size_t data_size = static_cast<size_t>(width) * height * 4;
    auto* data = static_cast<unsigned char*>(std::malloc(data_size));
    if (!data)
        return nullptr;

    auto* impl = new Surface::Impl;
    impl->width = width;
    impl->height = height;
    impl->stride = width * 4;
    impl->data = data;
    impl->owns_data = true;
    return impl;
}

} // namespace

// -- COW helpers --

static Surface::Impl* detach(Surface::Impl*& impl) {
    if (!impl)
        return nullptr;
    if (impl->count() == 1)
        return impl;

    auto* copy = create_impl_uninitialized(impl->width, impl->height);
    if (copy) {
        std::memcpy(copy->data, impl->data,
                     static_cast<size_t>(impl->height) * impl->stride);
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

Surface Surface::create(int width, int height) {
    auto* impl = create_impl_uninitialized(width, height);
    if (!impl)
        return {};

    std::memset(impl->data, 0, static_cast<size_t>(impl->height) * impl->stride);

    Surface surface;
    surface.m_impl = impl;
    return surface;
}

Surface Surface::create_for_data(unsigned char* data, int width, int height, int stride) {
    if (!data || width <= 0 || height <= 0
        || width >= kMaxSurfaceSize || height >= kMaxSurfaceSize
        || stride < width * 4)
        return {};

    auto* impl = new Impl;
    impl->width = width;
    impl->height = height;
    impl->stride = stride;
    impl->data = data;
    impl->owns_data = false;

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

// -- Clear --

void Surface::clear(const Color& color) {
    if (!m_impl)
        return;
    detach(m_impl);
    if (!m_impl)
        return;

    uint32_t pixel = premultiply_argb(color.to_argb32());
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
            uint32_t a = alpha(pixel);
            if (a == 0) {
                dst_row[0] = 0;
                dst_row[1] = 0;
                dst_row[2] = 0;
                dst_row[3] = 0;
            } else {
                uint32_t r = red(pixel);
                uint32_t g = green(pixel);
                uint32_t b = blue(pixel);
                if (a != 255) {
                    r = (r * 255) / a;
                    g = (g * 255) / a;
                    b = (b * 255) / a;
                }
                dst_row[0] = static_cast<unsigned char>(r);
                dst_row[1] = static_cast<unsigned char>(g);
                dst_row[2] = static_cast<unsigned char>(b);
                dst_row[3] = static_cast<unsigned char>(a);
            }
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
