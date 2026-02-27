// stbi_core.cpp - Shared helpers, dispatch, and public API
// Part of kosmvg::stbi image loader library

#include "stbi_internal.hpp"

namespace kosmvg::stbi {
namespace detail {

// Buffer refill (declared in stbi_internal.hpp, definition here because
// it is referenced before inline bodies that call it)
void stbi__refill_buffer(stbi__context* s) {
    int n = (s->io.read)(s->io_user_data, reinterpret_cast<char*>(s->buffer_start), s->buflen);
    s->callback_already_read += static_cast<int>(s->img_buffer - s->img_buffer_original);
    if (n == 0) {
        s->read_from_callbacks = 0;
        s->img_buffer = s->buffer_start;
        s->img_buffer_end = s->buffer_start + 1;
        *s->img_buffer = 0;
    } else {
        s->img_buffer = s->buffer_start;
        s->img_buffer_end = s->buffer_start + n;
    }
}

// Main dispatch
void* stbi__load_main(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri, int bpc) {
    *ri = {};
    ri->bits_per_channel = 8;
    ri->channel_order = STBI_ORDER_RGB;
    ri->num_channels = 0;

    #ifndef STBI_NO_PNG
    if (stbi__png_test(s))  return stbi__png_load(s, x, y, comp, req_comp, ri);
    #endif
    #ifndef STBI_NO_BMP
    if (stbi__bmp_test(s))  return stbi__bmp_load(s, x, y, comp, req_comp, ri);
    #endif
    #ifndef STBI_NO_GIF
    if (stbi__gif_test(s))  return stbi__gif_load(s, x, y, comp, req_comp, ri);
    #endif
    #ifndef STBI_NO_PSD
    if (stbi__psd_test(s))  return stbi__psd_load(s, x, y, comp, req_comp, ri, bpc);
    #else
    stbi_notused(bpc);
    #endif
    #ifndef STBI_NO_PIC
    if (stbi__pic_test(s))  return stbi__pic_load(s, x, y, comp, req_comp, ri);
    #endif
    #ifndef STBI_NO_JPEG
    if (stbi__jpeg_test(s)) return stbi__jpeg_load(s, x, y, comp, req_comp, ri);
    #endif
    #ifndef STBI_NO_PNM
    if (stbi__pnm_test(s))  return stbi__pnm_load(s, x, y, comp, req_comp, ri);
    #endif
    #ifndef STBI_NO_HDR
    if (stbi__hdr_test(s)) {
        float* hdr = stbi__hdr_load(s, x, y, comp, req_comp, ri);
        return stbi__hdr_to_ldr(hdr, *x, *y, req_comp ? req_comp : *comp);
    }
    #endif
    #ifndef STBI_NO_TGA
    if (stbi__tga_test(s)) return stbi__tga_load(s, x, y, comp, req_comp, ri);
    #endif
    return stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
}

unsigned char* stbi__load_and_postprocess_8bit(stbi__context* s, int* x, int* y, int* comp, int req_comp) {
    stbi__result_info ri;
    void* result = stbi__load_main(s, x, y, comp, req_comp, &ri, 8);
    if (!result) return nullptr;
    STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);
    if (ri.bits_per_channel != 8) {
        result = stbi__convert_16_to_8(reinterpret_cast<uint16_t*>(result), *x, *y, req_comp == 0 ? *comp : req_comp);
    }
    if (stbi__vertically_flip_on_load()) {
        int ch = req_comp ? req_comp : *comp;
        stbi__vertical_flip(result, *x, *y, ch * sizeof(stbi_uc));
    }
    if (stbi__apply_exif_on_load() && ri.exif_orientation) {
        int ch = req_comp ? req_comp : *comp;
        stbi__apply_exif_orientation(&result, x, y, ch * static_cast<int>(sizeof(stbi_uc)), ri.exif_orientation);
    }
    return reinterpret_cast<unsigned char*>(result);
}

uint16_t* stbi__load_and_postprocess_16bit(stbi__context* s, int* x, int* y, int* comp, int req_comp) {
    stbi__result_info ri;
    void* result = stbi__load_main(s, x, y, comp, req_comp, &ri, 16);
    if (!result) return nullptr;
    STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);
    if (ri.bits_per_channel != 16) {
        result = stbi__convert_8_to_16(reinterpret_cast<stbi_uc*>(result), *x, *y, req_comp == 0 ? *comp : req_comp);
    }
    if (stbi__vertically_flip_on_load()) {
        int ch = req_comp ? req_comp : *comp;
        stbi__vertical_flip(result, *x, *y, ch * sizeof(uint16_t));
    }
    if (stbi__apply_exif_on_load() && ri.exif_orientation) {
        int ch = req_comp ? req_comp : *comp;
        stbi__apply_exif_orientation(&result, x, y, ch * static_cast<int>(sizeof(uint16_t)), ri.exif_orientation);
    }
    return reinterpret_cast<uint16_t*>(result);
}

#if !defined(STBI_NO_HDR) && !defined(STBI_NO_LINEAR)
float* stbi__float_postprocess(float* result, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri) {
    if (stbi__vertically_flip_on_load() && result) {
        int ch = req_comp ? req_comp : *comp;
        stbi__vertical_flip(result, *x, *y, ch * sizeof(float));
    }
    if (stbi__apply_exif_on_load() && result && ri && ri->exif_orientation) {
        int ch = req_comp ? req_comp : *comp;
        void* p = result;
        stbi__apply_exif_orientation(&p, x, y, ch * static_cast<int>(sizeof(float)), ri->exif_orientation);
        result = static_cast<float*>(p);
    }
    return result;
}
#endif

#ifndef STBI_NO_LINEAR
float* stbi__loadf_main(stbi__context* s, int* x, int* y, int* comp, int req_comp) {
    #ifndef STBI_NO_HDR
    if (stbi__hdr_test(s)) {
        stbi__result_info ri;
        float* hdr = stbi__hdr_load(s, x, y, comp, req_comp, &ri);
        if (hdr) hdr = stbi__float_postprocess(hdr, x, y, comp, req_comp, &ri);
        return hdr;
    }
    #endif
    unsigned char* data = stbi__load_and_postprocess_8bit(s, x, y, comp, req_comp);
    if (data) return stbi__ldr_to_hdr(data, *x, *y, req_comp ? req_comp : *comp);
    return stbi__errpf("unknown image type", "Image not of any known type, or corrupt");
}
#endif

int stbi__info_main(stbi__context* s, int* x, int* y, int* comp) {
    #ifndef STBI_NO_JPEG
    if (stbi__jpeg_info(s, x, y, comp)) return 1;
    #endif
    #ifndef STBI_NO_PNG
    if (stbi__png_info(s, x, y, comp))  return 1;
    #endif
    #ifndef STBI_NO_GIF
    if (stbi__gif_info(s, x, y, comp))  return 1;
    #endif
    #ifndef STBI_NO_BMP
    if (stbi__bmp_info(s, x, y, comp))  return 1;
    #endif
    #ifndef STBI_NO_PSD
    if (stbi__psd_info(s, x, y, comp))  return 1;
    #endif
    #ifndef STBI_NO_PIC
    if (stbi__pic_info(s, x, y, comp))  return 1;
    #endif
    #ifndef STBI_NO_PNM
    if (stbi__pnm_info(s, x, y, comp)) return 1;
    #endif
    #ifndef STBI_NO_HDR
    if (stbi__hdr_info(s, x, y, comp)) return 1;
    #endif
    #ifndef STBI_NO_TGA
    if (stbi__tga_info(s, x, y, comp)) return 1;
    #endif
    return stbi__err("unknown image type", "Image not of any known type, or corrupt");
}

int stbi__is_16_main(stbi__context* s) {
    #ifndef STBI_NO_PNG
    if (stbi__png_is16(s)) return 1;
    #endif
    #ifndef STBI_NO_PSD
    if (stbi__psd_is16(s)) return 1;
    #endif
    #ifndef STBI_NO_PNM
    if (stbi__pnm_is16(s)) return 1;
    #endif
    return 0;
}

} // namespace detail

// Public API implementation

const char* failure_reason() noexcept {
    return detail::stbi__g_failure_reason;
}

void set_gamma_config(const GammaConfig& config) {
    detail::stbi__h2l_gamma_i = 1.0f / config.hdr_to_ldr_gamma;
    detail::stbi__h2l_scale_i = 1.0f / config.hdr_to_ldr_scale;
    detail::stbi__l2h_gamma = config.ldr_to_hdr_gamma;
    detail::stbi__l2h_scale = config.ldr_to_hdr_scale;
}

std::optional<ImageInfo> info(std::span<const uint8_t> buffer) {
    detail::stbi__context s{};
    detail::stbi__start_mem(&s, buffer.data(), static_cast<int>(buffer.size()));
    int x, y, comp;
    if (!detail::stbi__info_main(&s, &x, &y, &comp)) return std::nullopt;
    return ImageInfo{x, y, comp};
}

std::optional<ImageInfo> info(IOCallbacks& io) {
    detail::IOAdapter adapter{&io};
    detail::stbi__context s{};
    detail::start_io(&s, &adapter);
    int x, y, comp;
    if (!detail::stbi__info_main(&s, &x, &y, &comp)) return std::nullopt;
    return ImageInfo{x, y, comp};
}

bool is_hdr(std::span<const uint8_t> buffer) {
#ifndef STBI_NO_HDR
    detail::stbi__context s{};
    detail::stbi__start_mem(&s, buffer.data(), static_cast<int>(buffer.size()));
    return detail::stbi__hdr_test(&s);
#else
    (void)buffer;
    return false;
#endif
}

bool is_16bit(std::span<const uint8_t> buffer) {
    detail::stbi__context s{};
    detail::stbi__start_mem(&s, buffer.data(), static_cast<int>(buffer.size()));
    return detail::stbi__is_16_main(&s);
}

#ifndef KOSMVG_STBI_NO_STDIO
std::optional<ImageInfo> info(const char* filename) {
    FILE* f = detail::stbi__fopen(filename, "rb");
    if (!f) return std::nullopt;
    auto result = info(f);
    std::fclose(f);
    return result;
}

std::optional<ImageInfo> info(FILE* f) {
    long pos = std::ftell(f);
    detail::stbi__context s{};
    detail::stbi__start_file(&s, f);
    int x, y, comp;
    auto ok = detail::stbi__info_main(&s, &x, &y, &comp);
    std::fseek(f, pos, SEEK_SET);
    if (!ok) return std::nullopt;
    return ImageInfo{x, y, comp};
}

bool is_hdr(const char* filename) {
    FILE* f = detail::stbi__fopen(filename, "rb");
    if (!f) return false;
    bool result = is_hdr(f);
    std::fclose(f);
    return result;
}

bool is_hdr(FILE* f) {
#ifndef STBI_NO_HDR
    long pos = std::ftell(f);
    detail::stbi__context s{};
    detail::stbi__start_file(&s, f);
    bool result = detail::stbi__hdr_test(&s);
    std::fseek(f, pos, SEEK_SET);
    return result;
#else
    (void)f;
    return false;
#endif
}

bool is_16bit(const char* filename) {
    FILE* f = detail::stbi__fopen(filename, "rb");
    if (!f) return false;
    bool result = is_16bit(f);
    std::fclose(f);
    return result;
}

bool is_16bit(FILE* f) {
    long pos = std::ftell(f);
    detail::stbi__context s{};
    detail::stbi__start_file(&s, f);
    bool result = detail::stbi__is_16_main(&s);
    std::fseek(f, pos, SEEK_SET);
    return result;
}
#endif

// load() 8-bit from memory
Image<uint8_t> load(std::span<const uint8_t> buffer, const LoadOptions& opts) {
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::stbi__context s{};
    detail::stbi__start_mem(&s, buffer.data(), static_cast<int>(buffer.size()));
    int x, y, comp;
    auto* data = detail::stbi__load_and_postprocess_8bit(&s, &x, &y, &comp, req);
    if (!data) return {};
    return Image<uint8_t>(data, x, y, req ? req : comp);
}

// load() 8-bit from IOCallbacks
Image<uint8_t> load(IOCallbacks& io, const LoadOptions& opts) {
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::IOAdapter adapter{&io};
    detail::stbi__context s{};
    detail::start_io(&s, &adapter);
    int x, y, comp;
    auto* data = detail::stbi__load_and_postprocess_8bit(&s, &x, &y, &comp, req);
    if (!data) return {};
    return Image<uint8_t>(data, x, y, req ? req : comp);
}

// load16() from memory
Image<uint16_t> load16(std::span<const uint8_t> buffer, const LoadOptions& opts) {
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::stbi__context s{};
    detail::stbi__start_mem(&s, buffer.data(), static_cast<int>(buffer.size()));
    int x, y, comp;
    auto* data = detail::stbi__load_and_postprocess_16bit(&s, &x, &y, &comp, req);
    if (!data) return {};
    return Image<uint16_t>(data, x, y, req ? req : comp);
}

// load16() from IOCallbacks
Image<uint16_t> load16(IOCallbacks& io, const LoadOptions& opts) {
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::IOAdapter adapter{&io};
    detail::stbi__context s{};
    detail::start_io(&s, &adapter);
    int x, y, comp;
    auto* data = detail::stbi__load_and_postprocess_16bit(&s, &x, &y, &comp, req);
    if (!data) return {};
    return Image<uint16_t>(data, x, y, req ? req : comp);
}

// loadf() from memory
Image<float> loadf(std::span<const uint8_t> buffer, const LoadOptions& opts) {
#ifndef STBI_NO_LINEAR
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::stbi__context s{};
    detail::stbi__start_mem(&s, buffer.data(), static_cast<int>(buffer.size()));
    int x, y, comp;
    auto* data = detail::stbi__loadf_main(&s, &x, &y, &comp, req);
    if (!data) return {};
    return Image<float>(data, x, y, req ? req : comp);
#else
    (void)buffer; (void)opts;
    return {};
#endif
}

// loadf() from IOCallbacks
Image<float> loadf(IOCallbacks& io, const LoadOptions& opts) {
#ifndef STBI_NO_LINEAR
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::IOAdapter adapter{&io};
    detail::stbi__context s{};
    detail::start_io(&s, &adapter);
    int x, y, comp;
    auto* data = detail::stbi__loadf_main(&s, &x, &y, &comp, req);
    if (!data) return {};
    return Image<float>(data, x, y, req ? req : comp);
#else
    (void)io; (void)opts;
    return {};
#endif
}

#ifndef KOSMVG_STBI_NO_STDIO
Image<uint8_t> load(const char* filename, const LoadOptions& opts) {
    FILE* f = detail::stbi__fopen(filename, "rb");
    if (!f) return {};
    auto result = load(f, opts);
    std::fclose(f);
    return result;
}

Image<uint8_t> load(FILE* f, const LoadOptions& opts) {
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::stbi__context s{};
    detail::stbi__start_file(&s, f);
    int x, y, comp;
    auto* data = detail::stbi__load_and_postprocess_8bit(&s, &x, &y, &comp, req);
    if (data) std::fseek(f, -static_cast<int>(s.img_buffer_end - s.img_buffer), SEEK_CUR);
    if (!data) return {};
    return Image<uint8_t>(data, x, y, req ? req : comp);
}

Image<uint16_t> load16(const char* filename, const LoadOptions& opts) {
    FILE* f = detail::stbi__fopen(filename, "rb");
    if (!f) return {};
    auto result = load16(f, opts);
    std::fclose(f);
    return result;
}

Image<uint16_t> load16(FILE* f, const LoadOptions& opts) {
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::stbi__context s{};
    detail::stbi__start_file(&s, f);
    int x, y, comp;
    auto* data = detail::stbi__load_and_postprocess_16bit(&s, &x, &y, &comp, req);
    if (data) std::fseek(f, -static_cast<int>(s.img_buffer_end - s.img_buffer), SEEK_CUR);
    if (!data) return {};
    return Image<uint16_t>(data, x, y, req ? req : comp);
}

Image<float> loadf(const char* filename, const LoadOptions& opts) {
    FILE* f = detail::stbi__fopen(filename, "rb");
    if (!f) return {};
    auto result = loadf(f, opts);
    std::fclose(f);
    return result;
}

Image<float> loadf(FILE* f, const LoadOptions& opts) {
#ifndef STBI_NO_LINEAR
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::stbi__context s{};
    detail::stbi__start_file(&s, f);
    int x, y, comp;
    auto* data = detail::stbi__loadf_main(&s, &x, &y, &comp, req);
    if (!data) return {};
    return Image<float>(data, x, y, req ? req : comp);
#else
    (void)f; (void)opts;
    return {};
#endif
}
#endif

// GIF
GifResult load_gif(std::span<const uint8_t> buffer, const LoadOptions& opts) {
#ifndef STBI_NO_GIF
    detail::OptionsScope scope(opts);
    int req = static_cast<int>(opts.desired_format);
    detail::stbi__context s{};
    detail::stbi__start_mem(&s, buffer.data(), static_cast<int>(buffer.size()));
    int* delays_raw = nullptr;
    int x, y, z, comp;
    auto* data = static_cast<unsigned char*>(detail::stbi__load_gif_main(&s, &delays_raw, &x, &y, &z, &comp, req));
    if (detail::stbi__vertically_flip_on_load() && data)
        detail::stbi__vertical_flip_slices(data, x, y, z, req ? req : comp);
    if (!data) return {};
    GifResult result;
    result.pixels = Image<uint8_t>(data, x, y * z, req ? req : comp);
    if (delays_raw) {
        result.delays.assign(delays_raw, delays_raw + z);
        std::free(delays_raw);
    }
    result.frame_count = z;
    return result;
#else
    (void)buffer; (void)opts;
    return {};
#endif
}

// ZLIB utilities
namespace zlib {

DecodeResult decode(std::span<const char> input, int initial_size) {
#ifndef STBI_NO_ZLIB
    int outlen = 0;
    int guess = initial_size > 0 ? initial_size : 16384;
    char* data = detail::stbi_zlib_decode_malloc_guesssize(
        input.data(), static_cast<int>(input.size()), guess, &outlen);
    if (!data) return {};
    return {std::unique_ptr<char, FreeDeleter>(data), outlen};
#else
    (void)input; (void)initial_size;
    return {};
#endif
}

DecodeResult decode_noheader(std::span<const char> input) {
#ifndef STBI_NO_ZLIB
    int outlen = 0;
    char* data = detail::stbi_zlib_decode_noheader_malloc(
        input.data(), static_cast<int>(input.size()), &outlen);
    if (!data) return {};
    return {std::unique_ptr<char, FreeDeleter>(data), outlen};
#else
    (void)input;
    return {};
#endif
}

int decode_to(std::span<char> output, std::span<const char> input) {
#ifndef STBI_NO_ZLIB
    return detail::stbi_zlib_decode_buffer(
        output.data(), static_cast<int>(output.size()),
        input.data(), static_cast<int>(input.size()));
#else
    (void)output; (void)input;
    return -1;
#endif
}

int decode_noheader_to(std::span<char> output, std::span<const char> input) {
#ifndef STBI_NO_ZLIB
    return detail::stbi_zlib_decode_noheader_buffer(
        output.data(), static_cast<int>(output.size()),
        input.data(), static_cast<int>(input.size()));
#else
    (void)output; (void)input;
    return -1;
#endif
}

} // namespace zlib
} // namespace kosmvg::stbi
