// kosmvg::stbi - Image loader/writer library
// Based on stb_image v2.30 and stb_image_write v2.0 by Sean Barrett (public domain / MIT)
// Rewritten as C++20 API for KosmOS Render Kit
//
// Supported read formats:  JPEG, PNG, BMP, PSD, TGA, GIF, HDR, PIC, PNM
// Supported write formats: JPEG, PNG, BMP, TGA, HDR
//
// Limitations:
//   - no 12-bit-per-channel JPEG
//   - no JPEGs with arithmetic coding
//   - GIF always returns comp=4
//
// Pixel layout (N = number of components):
//   1: grey
//   2: grey, alpha
//   3: red, green, blue
//   4: red, green, blue, alpha
//
// Build:
//   Compile and link the .cpp files:
//     stbi_core.cpp, stbi_jpeg.cpp, stbi_png.cpp, stbi_other.cpp, stbi_write.cpp
//
//   auto img = kosmvg::stbi::load("photo.jpg");
//   if (img) { /* use img.data(), img.width(), etc. */ }
//
//   kosmvg::stbi::write_png("out.png", img);
//   kosmvg::stbi::write_jpg("out.jpg", img, {.jpeg_quality = 90});

#pragma once

#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <bit>
#include <array>
#include <optional>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef KOSMVG_STBI_NO_STDIO
#include <cstdio>
#endif

namespace kosmvg::stbi {

inline constexpr int version = 1;

struct FreeDeleter {
    void operator()(void* p) const noexcept { std::free(p); }
};

template<typename Sig>
class function_ref;

template<typename R, typename... Args>
class function_ref<R(Args...)> {
public:
    template<typename F>
        requires (!std::is_same_v<std::decay_t<F>, function_ref>)
    constexpr function_ref(F&& f) noexcept
        : obj_(const_cast<void*>(static_cast<const void*>(&f)))
        , invoke_([](void* o, Args... args) -> R {
            return (*static_cast<std::add_pointer_t<std::remove_reference_t<F>>>(o))(
                std::forward<Args>(args)...);
        }) {}

    R operator()(Args... args) const {
        return invoke_(obj_, std::forward<Args>(args)...);
    }

private:
    void* obj_;
    R (*invoke_)(void*, Args...);
};

enum class PixelFormat : int {
    Default   = 0,
    Grey      = 1,
    GreyAlpha = 2,
    Rgb       = 3,
    RgbAlpha  = 4
};

enum class ChannelOrder : int {
    Rgb = 0,
    Bgr = 1
};

struct ImageInfo {
    int width  = 0;
    int height = 0;
    int channels = 0;
    int bits_per_channel = 8;
    ChannelOrder order = ChannelOrder::Rgb;
};

struct GammaConfig {
    float hdr_to_ldr_gamma = 2.2f;
    float hdr_to_ldr_scale = 1.0f;
    float ldr_to_hdr_gamma = 2.2f;
    float ldr_to_hdr_scale = 1.0f;
};

struct LoadOptions {
    PixelFormat desired_format = PixelFormat::Default;
    bool flip_vertically = false;
    bool unpremultiply_alpha = false;
    bool iphone_png_to_rgb = false;
    bool apply_exif_orientation = false;
    GammaConfig gamma = {};
};

template<typename T>
class Image {
public:
    Image() = default;

    Image(T* data, int w, int h, int ch)
        : data_(data), width_(w), height_(h), channels_(ch) {}

    Image(Image&& o) noexcept
        : data_(std::exchange(o.data_, nullptr))
        , width_(std::exchange(o.width_, 0))
        , height_(std::exchange(o.height_, 0))
        , channels_(std::exchange(o.channels_, 0)) {}

    Image& operator=(Image&& o) noexcept {
        if (this != &o) {
            reset();
            data_     = std::exchange(o.data_, nullptr);
            width_    = std::exchange(o.width_, 0);
            height_   = std::exchange(o.height_, 0);
            channels_ = std::exchange(o.channels_, 0);
        }
        return *this;
    }

    ~Image() { reset(); }

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    explicit operator bool() const noexcept { return data_ != nullptr; }

    T*       data()       noexcept { return data_; }
    const T* data() const noexcept { return data_; }

    int width()    const noexcept { return width_; }
    int height()   const noexcept { return height_; }
    int channels() const noexcept { return channels_; }

    size_t size_bytes() const noexcept {
        return static_cast<size_t>(width_) * height_ * channels_ * sizeof(T);
    }

    std::span<T>       pixels()       noexcept { return {data_, pixel_count()}; }
    std::span<const T> pixels() const noexcept { return {data_, pixel_count()}; }

    T*       at(int x, int y)       noexcept {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        return data_ + (y * width_ + x) * channels_;
    }
    const T* at(int x, int y) const noexcept {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        return data_ + (y * width_ + x) * channels_;
    }

    T* release() noexcept {
        auto* p = data_;
        data_ = nullptr;
        width_ = height_ = channels_ = 0;
        return p;
    }

    void reset() noexcept {
        if (data_) { std::free(data_); data_ = nullptr; }
        width_ = height_ = channels_ = 0;
    }

    ImageInfo info() const noexcept { return {width_, height_, channels_}; }

private:
    size_t pixel_count() const noexcept {
        return static_cast<size_t>(width_) * height_ * channels_;
    }

    T*  data_     = nullptr;
    int width_    = 0;
    int height_   = 0;
    int channels_ = 0;
};

struct IOCallbacks {
    function_ref<int(char* data, int size)>  read;
    function_ref<void(int n)>                skip;
    function_ref<bool()>                     eof;
};

const char* failure_reason() noexcept;

std::optional<ImageInfo> info(std::span<const uint8_t> buffer);
std::optional<ImageInfo> info(IOCallbacks& io);
bool is_hdr(std::span<const uint8_t> buffer);
bool is_16bit(std::span<const uint8_t> buffer);

#ifndef KOSMVG_STBI_NO_STDIO
std::optional<ImageInfo> info(const char* filename);
std::optional<ImageInfo> info(FILE* f);
bool is_hdr(const char* filename);
bool is_hdr(FILE* f);
bool is_16bit(const char* filename);
bool is_16bit(FILE* f);
#endif

Image<uint8_t>  load(std::span<const uint8_t> buffer, const LoadOptions& opts = {});
Image<uint8_t>  load(IOCallbacks& io, const LoadOptions& opts = {});
Image<uint16_t> load16(std::span<const uint8_t> buffer, const LoadOptions& opts = {});
Image<uint16_t> load16(IOCallbacks& io, const LoadOptions& opts = {});
Image<float>    loadf(std::span<const uint8_t> buffer, const LoadOptions& opts = {});
Image<float>    loadf(IOCallbacks& io, const LoadOptions& opts = {});

#ifndef KOSMVG_STBI_NO_STDIO
Image<uint8_t>  load(const char* filename, const LoadOptions& opts = {});
Image<uint8_t>  load(FILE* f, const LoadOptions& opts = {});
Image<uint16_t> load16(const char* filename, const LoadOptions& opts = {});
Image<uint16_t> load16(FILE* f, const LoadOptions& opts = {});
Image<float>    loadf(const char* filename, const LoadOptions& opts = {});
Image<float>    loadf(FILE* f, const LoadOptions& opts = {});
#endif

struct GifResult {
    Image<uint8_t> pixels;
    std::vector<int> delays;
    int frame_count = 0;
};

GifResult load_gif(std::span<const uint8_t> buffer, const LoadOptions& opts = {});

enum class PngFilter : int {
    Auto     = -1,
    None     =  0,
    Sub      =  1,
    Up       =  2,
    Average  =  3,
    Paeth    =  4
};

struct WriteOptions {
    bool flip_vertically = false;
    int png_compression_level = 8;
    PngFilter png_filter = PngFilter::Auto;
    bool png_srgb = false;
    int png_ppm_x = 0;
    int png_ppm_y = 0;
    bool tga_rle = true;
    int jpeg_quality = 90;
    int stride_bytes = 0;
};

using WriteFunc = function_ref<void(std::span<const uint8_t> data)>;

#ifndef KOSMVG_STBI_NO_STDIO

bool write_png(const char* filename, int w, int h, int comp,
               const void* data, const WriteOptions& opts = {});
bool write_bmp(const char* filename, int w, int h, int comp, const void* data,
               const WriteOptions& opts = {});
bool write_tga(const char* filename, int w, int h, int comp, const void* data,
               const WriteOptions& opts = {});
bool write_hdr(const char* filename, int w, int h, int comp, const float* data,
               const WriteOptions& opts = {});
bool write_jpg(const char* filename, int w, int h, int comp, const void* data,
               const WriteOptions& opts = {});

template<typename T>
bool write_png(const char* filename, const Image<T>& img, const WriteOptions& opts = {}) {
    return img ? write_png(filename, img.width(), img.height(), img.channels(),
                           img.data(), opts) : false;
}

template<typename T>
bool write_bmp(const char* filename, const Image<T>& img, const WriteOptions& opts = {}) {
    return img ? write_bmp(filename, img.width(), img.height(), img.channels(),
                           img.data(), opts) : false;
}

template<typename T>
bool write_tga(const char* filename, const Image<T>& img, const WriteOptions& opts = {}) {
    return img ? write_tga(filename, img.width(), img.height(), img.channels(),
                           img.data(), opts) : false;
}

bool write_hdr(const char* filename, const Image<float>& img, const WriteOptions& opts = {});

template<typename T>
bool write_jpg(const char* filename, const Image<T>& img, const WriteOptions& opts = {}) {
    return img ? write_jpg(filename, img.width(), img.height(), img.channels(),
                           img.data(), opts) : false;
}

#endif // KOSMVG_STBI_NO_STDIO

bool write_png_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts = {});
bool write_bmp_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts = {});
bool write_tga_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts = {});
bool write_hdr_to_func(WriteFunc func, int w, int h, int comp,
                       const float* data, const WriteOptions& opts = {});
bool write_jpg_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts = {});

struct EncodeResult {
    std::unique_ptr<uint8_t, FreeDeleter> data;
    int length = 0;
    explicit operator bool() const noexcept { return data != nullptr; }
    std::span<const uint8_t> span() const noexcept {
        return {data.get(), static_cast<size_t>(length)};
    }
};

EncodeResult encode_png(int w, int h, int comp, const void* data,
                        const WriteOptions& opts = {});

template<typename T>
EncodeResult encode_png(const Image<T>& img, const WriteOptions& opts = {}) {
    return img ? encode_png(img.width(), img.height(), img.channels(),
                            img.data(), opts) : EncodeResult{};
}

EncodeResult encode_jpg(int w, int h, int comp, const void* data,
                        const WriteOptions& opts = {});

template<typename T>
EncodeResult encode_jpg(const Image<T>& img, const WriteOptions& opts = {}) {
    return img ? encode_jpg(img.width(), img.height(), img.channels(),
                            img.data(), opts) : EncodeResult{};
}

namespace zlib {

struct DecodeResult {
    std::unique_ptr<char, FreeDeleter> data;
    int length = 0;
    explicit operator bool() const noexcept { return data != nullptr; }
};

struct CompressResult {
    std::unique_ptr<char, FreeDeleter> data;
    int length = 0;
    explicit operator bool() const noexcept { return data != nullptr; }
};

CompressResult compress(std::span<const uint8_t> input, int quality = 8);

DecodeResult decode(std::span<const char> input, int initial_size = 0);
DecodeResult decode_noheader(std::span<const char> input);
int decode_to(std::span<char> output, std::span<const char> input);
int decode_noheader_to(std::span<char> output, std::span<const char> input);

} // namespace zlib
} // namespace kosmvg::stbi
