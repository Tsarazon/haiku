// stbi_internal.hpp - Internal shared header for kosmvg::stbi decoder modules
// Not for public use. Include stb_image.hpp for the public API.
#pragma once

#include "stb_image.hpp"

#include <cstdarg>
#include <cstddef>
#include <climits>
#include <algorithm>
#include <cassert>

#if !defined(KOSMVG_STBI_NO_LINEAR) || !defined(KOSMVG_STBI_NO_HDR)
#include <cmath>
#endif

#ifndef KOSMVG_STBI_NO_STDIO
#include <cstdio>
#endif

#ifndef STBI_ASSERT
#define STBI_ASSERT(x) assert(x)
#endif

// Map public config macros to internal STBI_ macros used by decoder code
#ifdef KOSMVG_STBI_NO_JPEG
#define STBI_NO_JPEG
#endif
#ifdef KOSMVG_STBI_NO_PNG
#define STBI_NO_PNG
#endif
#ifdef KOSMVG_STBI_NO_BMP
#define STBI_NO_BMP
#endif
#ifdef KOSMVG_STBI_NO_PSD
#define STBI_NO_PSD
#endif
#ifdef KOSMVG_STBI_NO_TGA
#define STBI_NO_TGA
#endif
#ifdef KOSMVG_STBI_NO_GIF
#define STBI_NO_GIF
#endif
#ifdef KOSMVG_STBI_NO_HDR
#define STBI_NO_HDR
#endif
#ifdef KOSMVG_STBI_NO_PIC
#define STBI_NO_PIC
#endif
#ifdef KOSMVG_STBI_NO_PNM
#define STBI_NO_PNM
#endif
#ifdef KOSMVG_STBI_NO_LINEAR
#define STBI_NO_LINEAR
#endif
#ifdef KOSMVG_STBI_NO_ZLIB
#define STBI_NO_ZLIB
#endif
#ifdef KOSMVG_STBI_NO_STDIO
#define STBI_NO_STDIO
#endif

#if defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB) && !defined(STBI_NO_ZLIB)
#define STBI_NO_ZLIB
#endif

namespace kosmvg::stbi {
namespace detail {

using stbi_uc = uint8_t;
using stbi_us = uint16_t;

static_assert(sizeof(uint32_t) == 4);

#define stbi_lrot(x,y) std::rotl(static_cast<uint32_t>(x), static_cast<int>(y))

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           std::malloc(sz)
#define STBI_REALLOC(p,newsz)     std::realloc(p,newsz)
#define STBI_FREE(p)              std::free(p)
#endif
#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

// SIMD detection
#if defined(__x86_64__)
#define STBI__X64_TARGET
#elif defined(__i386)
#define STBI__X86_TARGET
#endif

#if defined(__GNUC__) && defined(STBI__X86_TARGET) && !defined(__SSE2__) && !defined(STBI_NO_SIMD)
#define STBI_NO_SIMD
#endif

#if !defined(STBI_NO_SIMD) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET))
#define STBI_SSE2
#include <emmintrin.h>
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
inline int stbi__sse2_available() { return 1; }
#endif
#endif

#if defined(STBI_NO_SIMD) && defined(STBI_NEON)
#undef STBI_NEON
#endif
#ifdef STBI_NEON
#include <arm_neon.h>
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#endif

#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif

#ifndef STBI_MAX_DIMENSIONS
inline constexpr int STBI_MAX_DIMENSIONS = (1 << 24);
#endif

// Thread-local options, set per-load-call via OptionsScope
struct ThreadOptions {
    bool flip_vertically = false;
    bool unpremultiply = false;
    bool iphone_to_rgb = false;
    bool apply_exif_orientation = false;
};

inline thread_local ThreadOptions tl_options;

inline bool stbi__vertically_flip_on_load() { return tl_options.flip_vertically; }
inline bool stbi__unpremultiply_on_load()   { return tl_options.unpremultiply; }
inline bool stbi__de_iphone_flag()          { return tl_options.iphone_to_rgb; }
inline bool stbi__apply_exif_on_load()      { return tl_options.apply_exif_orientation; }

struct OptionsScope {
    ThreadOptions prev;
    explicit OptionsScope(const LoadOptions& opts) : prev(tl_options) {
        tl_options.flip_vertically        = opts.flip_vertically;
        tl_options.unpremultiply          = opts.unpremultiply_alpha;
        tl_options.iphone_to_rgb          = opts.iphone_png_to_rgb;
        tl_options.apply_exif_orientation = opts.apply_exif_orientation;
    }
    ~OptionsScope() { tl_options = prev; }
};

// Context: IO state for image reading
struct stbi__context {
    uint32_t img_x = 0, img_y = 0;
    int img_n = 0, img_out_n = 0;

    struct { int (*read)(void*,char*,int); void (*skip)(void*,int); int (*eof)(void*); } io = {};
    void* io_user_data = nullptr;

    int read_from_callbacks = 0;
    int buflen = 0;
    stbi_uc buffer_start[128] = {};
    int callback_already_read = 0;

    stbi_uc *img_buffer = nullptr, *img_buffer_end = nullptr;
    stbi_uc *img_buffer_original = nullptr, *img_buffer_original_end = nullptr;
};

struct stbi__result_info {
    int bits_per_channel = 8;
    int num_channels = 0;
    int channel_order = 0;
    int exif_orientation = 0;
};

template<typename T>
constexpr void stbi_notused([[maybe_unused]] T&&) {}

// Error handling
inline thread_local const char* stbi__g_failure_reason = nullptr;

inline int stbi__err_fn(const char* str) {
    stbi__g_failure_reason = str;
    return 0;
}

#ifdef STBI_FAILURE_USERMSG
#define stbi__err(x,y) detail::stbi__err_fn(y)
#else
#define stbi__err(x,y) detail::stbi__err_fn(x)
#endif

inline float* stbi__errpf(const char* str, [[maybe_unused]] const char* extra) {
    stbi__err_fn(str);
    return nullptr;
}

inline unsigned char* stbi__errpuc(const char* str, [[maybe_unused]] const char* extra) {
    stbi__err_fn(str);
    return nullptr;
}

// Allocation
[[nodiscard]] inline void* stbi__malloc(size_t size) { return STBI_MALLOC(size); }

static constexpr int stbi__addsizes_valid(int a, int b) {
    if (b < 0) return 0;
    return a <= INT_MAX - b;
}

static constexpr int stbi__mul2sizes_valid(int a, int b) {
    if (a < 0 || b < 0) return 0;
    if (b == 0) return 1;
    return a <= INT_MAX / b;
}

static constexpr int stbi__mad2sizes_valid(int a, int b, int add) {
    return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a * b, add);
}

static constexpr int stbi__mad3sizes_valid(int a, int b, int c, int add) {
    return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a * b, c) && stbi__addsizes_valid(a * b * c, add);
}

static constexpr int stbi__mad4sizes_valid(int a, int b, int c, int d, int add) {
    return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a * b, c) &&
           stbi__mul2sizes_valid(a * b * c, d) && stbi__addsizes_valid(a * b * c * d, add);
}

[[nodiscard]] inline void* stbi__malloc_mad2(int a, int b, int add) {
    if (!stbi__mad2sizes_valid(a, b, add)) return nullptr;
    return stbi__malloc(a * b + add);
}

[[nodiscard]] inline void* stbi__malloc_mad3(int a, int b, int c, int add) {
    if (!stbi__mad3sizes_valid(a, b, c, add)) return nullptr;
    return stbi__malloc(a * b * c + add);
}

[[nodiscard]] inline void* stbi__malloc_mad4(int a, int b, int c, int d, int add) {
    if (!stbi__mad4sizes_valid(a, b, c, d, add)) return nullptr;
    return stbi__malloc(a * b * c * d + add);
}

static constexpr int stbi__addints_valid(int a, int b) {
    if ((a >= 0) != (b >= 0)) return 1;
    if (a < 0 && b < 0) return a >= INT_MIN - b;
    return a <= INT_MAX - b;
}

static constexpr int stbi__mul2shorts_valid(int a, int b) {
    if (b == 0 || b == -1) return 1;
    if ((a >= 0) == (b >= 0)) return a <= SHRT_MAX / b;
    if (b < 0) return a <= SHRT_MIN / b;
    return a >= SHRT_MIN / b;
}

// Context initialization
void stbi__refill_buffer(stbi__context* s);

inline void stbi__start_mem(stbi__context* s, const stbi_uc* buffer, int len) {
    s->io.read = nullptr;
    s->read_from_callbacks = 0;
    s->callback_already_read = 0;
    s->img_buffer = s->img_buffer_original = const_cast<stbi_uc*>(buffer);
    s->img_buffer_end = s->img_buffer_original_end = const_cast<stbi_uc*>(buffer) + len;
}

inline void stbi__start_callbacks(stbi__context* s, decltype(stbi__context::io)* c, void* user) {
    s->io = *c;
    s->io_user_data = user;
    s->buflen = sizeof(s->buffer_start);
    s->read_from_callbacks = 1;
    s->callback_already_read = 0;
    s->img_buffer = s->img_buffer_original = s->buffer_start;
    stbi__refill_buffer(s);
    s->img_buffer_original_end = s->img_buffer_end;
}

#ifndef STBI_NO_STDIO
inline int stbi__stdio_read(void* user, char* data, int size) {
    return static_cast<int>(std::fread(data, 1, size, static_cast<FILE*>(user)));
}

inline void stbi__stdio_skip(void* user, int n) {
    std::fseek(static_cast<FILE*>(user), n, SEEK_CUR);
    int ch = std::fgetc(static_cast<FILE*>(user));
    if (ch != EOF) std::ungetc(ch, static_cast<FILE*>(user));
}

inline int stbi__stdio_eof(void* user) {
    return std::feof(static_cast<FILE*>(user)) || std::ferror(static_cast<FILE*>(user));
}

inline decltype(stbi__context::io) stbi__stdio_callbacks = {
    stbi__stdio_read, stbi__stdio_skip, stbi__stdio_eof
};

inline void stbi__start_file(stbi__context* s, FILE* f) {
    stbi__start_callbacks(s, &stbi__stdio_callbacks, static_cast<void*>(f));
}

inline FILE* stbi__fopen(const char* filename, const char* mode) {
    return std::fopen(filename, mode);
}
#endif

inline void stbi__rewind(stbi__context* s) {
    s->img_buffer = s->img_buffer_original;
    s->img_buffer_end = s->img_buffer_original_end;
}

// IOCallbacks adapter
struct IOAdapter {
    kosmvg::stbi::IOCallbacks* cb;
};

inline int io_adapter_read(void* user, char* data, int size) {
    return static_cast<IOAdapter*>(user)->cb->read(data, size);
}
inline void io_adapter_skip(void* user, int n) {
    static_cast<IOAdapter*>(user)->cb->skip(n);
}
inline int io_adapter_eof(void* user) {
    return static_cast<IOAdapter*>(user)->cb->eof() ? 1 : 0;
}

inline void start_io(stbi__context* s, IOAdapter* adapter) {
    decltype(stbi__context::io) cb = { io_adapter_read, io_adapter_skip, io_adapter_eof };
    stbi__start_callbacks(s, &cb, adapter);
}

// Byte-level IO
inline stbi_uc stbi__get8(stbi__context* s) {
    if (s->img_buffer < s->img_buffer_end) return *s->img_buffer++;
    if (s->read_from_callbacks) { stbi__refill_buffer(s); return *s->img_buffer++; }
    return 0;
}

inline int stbi__at_eof(stbi__context* s) {
    if (s->io.read) {
        if (!(s->io.eof)(s->io_user_data)) return 0;
        if (s->read_from_callbacks == 0) return 1;
    }
    return s->img_buffer >= s->img_buffer_end;
}

inline void stbi__skip(stbi__context* s, int n) {
    if (n == 0) return;
    if (n < 0) { s->img_buffer = s->img_buffer_end; return; }
    if (s->io.read) {
        int blen = static_cast<int>(s->img_buffer_end - s->img_buffer);
        if (blen < n) { s->img_buffer = s->img_buffer_end; (s->io.skip)(s->io_user_data, n - blen); return; }
    }
    s->img_buffer += n;
}

inline int stbi__getn(stbi__context* s, stbi_uc* buffer, int n) {
    if (s->io.read) {
        int blen = static_cast<int>(s->img_buffer_end - s->img_buffer);
        if (blen < n) {
            std::memcpy(buffer, s->img_buffer, blen);
            int count = (s->io.read)(s->io_user_data, reinterpret_cast<char*>(buffer) + blen, n - blen);
            s->img_buffer = s->img_buffer_end;
            return (count == (n - blen));
        }
    }
    if (s->img_buffer + n <= s->img_buffer_end) {
        std::memcpy(buffer, s->img_buffer, n);
        s->img_buffer += n;
        return 1;
    }
    return 0;
}

inline int stbi__get16be(stbi__context* s) {
    int z = stbi__get8(s); return (z << 8) + stbi__get8(s);
}
inline uint32_t stbi__get32be(stbi__context* s) {
    uint32_t z = stbi__get16be(s); return (z << 16) + stbi__get16be(s);
}
inline int stbi__get16le(stbi__context* s) {
    int z = stbi__get8(s); return z + (stbi__get8(s) << 8);
}
inline uint32_t stbi__get32le(stbi__context* s) {
    uint32_t z = stbi__get16le(s);
    z += static_cast<uint32_t>(stbi__get16le(s)) << 16;
    return z;
}

constexpr stbi_uc stbi__bytecast(int x) { return static_cast<stbi_uc>(x & 255); }

static constexpr stbi_uc stbi__compute_y(int r, int g, int b) {
    return static_cast<stbi_uc>(((r * 77) + (g * 150) + (29 * b)) >> 8);
}

inline uint16_t stbi__compute_y_16(int r, int g, int b) {
    return static_cast<uint16_t>(((r * 77) + (g * 150) + (29 * b)) >> 8);
}

#define STBI_ORDER_RGB 0
#define STBI_ORDER_BGR 1
#define STBI__SCAN_load   0
#define STBI__SCAN_type   1
#define STBI__SCAN_header 2

// Format constants used by decoder code
inline constexpr int STBI_default    = 0;
inline constexpr int STBI_grey       = 1;
inline constexpr int STBI_grey_alpha = 2;
inline constexpr int STBI_rgb        = 3;
inline constexpr int STBI_rgb_alpha  = 4;

// Forward declarations for decoder functions

#ifndef STBI_NO_JPEG
int      stbi__jpeg_test(stbi__context* s);
void*    stbi__jpeg_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
int      stbi__jpeg_info(stbi__context* s, int* x, int* y, int* comp);
#endif
#ifndef STBI_NO_PNG
int      stbi__png_test(stbi__context* s);
void*    stbi__png_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
int      stbi__png_info(stbi__context* s, int* x, int* y, int* comp);
int      stbi__png_is16(stbi__context* s);
#endif
#ifndef STBI_NO_BMP
int      stbi__bmp_test(stbi__context* s);
void*    stbi__bmp_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
int      stbi__bmp_info(stbi__context* s, int* x, int* y, int* comp);
#endif
#ifndef STBI_NO_TGA
int      stbi__tga_test(stbi__context* s);
void*    stbi__tga_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
int      stbi__tga_info(stbi__context* s, int* x, int* y, int* comp);
#endif
#ifndef STBI_NO_PSD
int      stbi__psd_test(stbi__context* s);
void*    stbi__psd_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri, int bpc);
int      stbi__psd_info(stbi__context* s, int* x, int* y, int* comp);
int      stbi__psd_is16(stbi__context* s);
#endif
#ifndef STBI_NO_HDR
int      stbi__hdr_test(stbi__context* s);
float*   stbi__hdr_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
int      stbi__hdr_info(stbi__context* s, int* x, int* y, int* comp);
#endif
#ifndef STBI_NO_PIC
int      stbi__pic_test(stbi__context* s);
void*    stbi__pic_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
int      stbi__pic_info(stbi__context* s, int* x, int* y, int* comp);
#endif
#ifndef STBI_NO_GIF
int      stbi__gif_test(stbi__context* s);
void*    stbi__gif_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
void*    stbi__load_gif_main(stbi__context* s, int** delays, int* x, int* y, int* z, int* comp, int req_comp);
int      stbi__gif_info(stbi__context* s, int* x, int* y, int* comp);
#endif
#ifndef STBI_NO_PNM
int      stbi__pnm_test(stbi__context* s);
void*    stbi__pnm_load(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
int      stbi__pnm_info(stbi__context* s, int* x, int* y, int* comp);
int      stbi__pnm_is16(stbi__context* s);
#endif

// Zlib public utility functions (defined in stbi_png.cpp)
#ifndef STBI_NO_ZLIB
char* stbi_zlib_decode_malloc_guesssize(const char* buffer, int len, int initial_size, int* outlen);
char* stbi_zlib_decode_malloc(char const* buffer, int len, int* outlen);
char* stbi_zlib_decode_malloc_guesssize_headerflag(const char* buffer, int len, int initial_size, int* outlen, int parse_header);
int   stbi_zlib_decode_buffer(char* obuffer, int olen, char const* ibuffer, int ilen);
char* stbi_zlib_decode_noheader_malloc(char const* buffer, int len, int* outlen);
int   stbi_zlib_decode_noheader_buffer(char* obuffer, int olen, const char* ibuffer, int ilen);
#endif

// Format conversion (used by multiple decoders)
inline unsigned char* stbi__convert_format(unsigned char* data, int img_n, int req_comp, unsigned int x, unsigned int y) {
    int i, j;
    if (req_comp == img_n) return data;
    STBI_ASSERT(req_comp >= 1 && req_comp <= 4);
    auto* good = static_cast<unsigned char*>(stbi__malloc_mad3(req_comp, x, y, 0));
    if (!good) { STBI_FREE(data); return stbi__errpuc("outofmem", "Out of memory"); }

    for (j = 0; j < static_cast<int>(y); ++j) {
        unsigned char* src  = data + j * x * img_n;
        unsigned char* dest = good + j * x * req_comp;
        #define STBI__COMBO(a,b) ((a)*8+(b))
        #define STBI__CASE(a,b)  case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
        switch (STBI__COMBO(img_n, req_comp)) {
            STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
            STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
            STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
            STBI__CASE(2,1) { dest[0]=src[0];                                                  } break;
            STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
            STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
            STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
            STBI__CASE(3,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
            STBI__CASE(3,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1]=255;      } break;
            STBI__CASE(4,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
            STBI__CASE(4,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1]=src[3];   } break;
            STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;
            default: STBI_ASSERT(0); STBI_FREE(data); STBI_FREE(good);
                     return stbi__errpuc("unsupported", "Unsupported format conversion");
        }
        #undef STBI__CASE
        #undef STBI__COMBO
    }
    STBI_FREE(data);
    return good;
}

inline uint16_t* stbi__convert_format16(uint16_t* data, int img_n, int req_comp, unsigned int x, unsigned int y) {
    int i, j;
    if (req_comp == img_n) return data;
    STBI_ASSERT(req_comp >= 1 && req_comp <= 4);
    auto* good = static_cast<uint16_t*>(stbi__malloc(req_comp * x * y * 2));
    if (!good) { STBI_FREE(data); return reinterpret_cast<uint16_t*>(stbi__errpuc("outofmem", "Out of memory")); }

    for (j = 0; j < static_cast<int>(y); ++j) {
        uint16_t* src  = data + j * x * img_n;
        uint16_t* dest = good + j * x * req_comp;
        #define STBI__COMBO(a,b) ((a)*8+(b))
        #define STBI__CASE(a,b)  case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
        switch (STBI__COMBO(img_n, req_comp)) {
            STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=0xffff;                                     } break;
            STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
            STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=0xffff;                     } break;
            STBI__CASE(2,1) { dest[0]=src[0];                                                     } break;
            STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
            STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                     } break;
            STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=0xffff;        } break;
            STBI__CASE(3,1) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]);                   } break;
            STBI__CASE(3,2) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]); dest[1]=0xffff;   } break;
            STBI__CASE(4,1) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]);                   } break;
            STBI__CASE(4,2) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]); dest[1]=src[3];   } break;
            STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                       } break;
            default: STBI_ASSERT(0); STBI_FREE(data); STBI_FREE(good);
                     return reinterpret_cast<uint16_t*>(stbi__errpuc("unsupported", "Unsupported format conversion"));
        }
        #undef STBI__CASE
        #undef STBI__COMBO
    }
    STBI_FREE(data);
    return good;
}

// Bit-depth conversion
inline stbi_uc* stbi__convert_16_to_8(uint16_t* orig, int w, int h, int channels) {
    int img_len = w * h * channels;
    auto* reduced = static_cast<stbi_uc*>(stbi__malloc(img_len));
    if (!reduced) return stbi__errpuc("outofmem", "Out of memory");
    for (int i = 0; i < img_len; ++i)
        reduced[i] = static_cast<stbi_uc>((orig[i] >> 8) & 0xFF);
    STBI_FREE(orig);
    return reduced;
}

inline uint16_t* stbi__convert_8_to_16(stbi_uc* orig, int w, int h, int channels) {
    int img_len = w * h * channels;
    auto* enlarged = static_cast<uint16_t*>(stbi__malloc(img_len * 2));
    if (!enlarged) return reinterpret_cast<uint16_t*>(stbi__errpuc("outofmem", "Out of memory"));
    for (int i = 0; i < img_len; ++i)
        enlarged[i] = static_cast<uint16_t>((orig[i] << 8) + orig[i]);
    STBI_FREE(orig);
    return enlarged;
}

// Vertical flip
inline void stbi__vertical_flip(void* image, int w, int h, int bytes_per_pixel) {
    size_t bpr = static_cast<size_t>(w) * bytes_per_pixel;
    auto temp = std::make_unique<stbi_uc[]>(bpr);
    auto* bytes = static_cast<stbi_uc*>(image);
    for (int row = 0; row < (h >> 1); row++) {
        stbi_uc* r0 = bytes + row * bpr;
        stbi_uc* r1 = bytes + (h - row - 1) * bpr;
        std::memcpy(temp.get(), r0, bpr);
        std::memcpy(r0, r1, bpr);
        std::memcpy(r1, temp.get(), bpr);
    }
}

#ifndef STBI_NO_GIF
inline void stbi__vertical_flip_slices(void* image, int w, int h, int z, int bytes_per_pixel) {
    int slice_size = w * h * bytes_per_pixel;
    auto* bytes = reinterpret_cast<stbi_uc*>(image);
    for (int slice = 0; slice < z; ++slice) {
        stbi__vertical_flip(bytes, w, h, bytes_per_pixel);
        bytes += slice_size;
    }
}
#endif

inline void stbi__horizontal_flip(void* image, int w, int h, int bytes_per_pixel) {
    auto* bytes = static_cast<stbi_uc*>(image);
    auto bpp = static_cast<size_t>(bytes_per_pixel);
    size_t stride = static_cast<size_t>(w) * bpp;
    for (int row = 0; row < h; ++row) {
        stbi_uc* r = bytes + row * stride;
        for (int col = 0; col < w / 2; ++col) {
            stbi_uc* a = r + col * bpp;
            stbi_uc* b = r + (w - 1 - col) * bpp;
            for (size_t k = 0; k < bpp; ++k)
                std::swap(a[k], b[k]);
        }
    }
}

inline void* stbi__transpose(void* image, int w, int h, int bytes_per_pixel) {
    auto* src = static_cast<stbi_uc*>(image);
    auto bpp = static_cast<size_t>(bytes_per_pixel);
    auto* dst = static_cast<stbi_uc*>(STBI_MALLOC(static_cast<size_t>(w) * h * bpp));
    if (!dst) return nullptr;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            std::memcpy(dst + (static_cast<size_t>(x) * h + y) * bpp,
                        src + (static_cast<size_t>(y) * w + x) * bpp, bpp);
    return dst;
}

// EXIF orientation decomposition:
//   1: identity          5: transpose
//   2: flipH             6: transpose + flipH
//   3: flipH + flipV     7: transpose + flipH + flipV
//   4: flipV             8: transpose + flipV
inline void stbi__apply_exif_orientation(void** data, int* x, int* y, int bpp, int orient) {
    if (orient <= 1 || orient > 8 || !*data) return;
    int w = *x, h = *y;
    if (orient >= 5) {
        void* tr = stbi__transpose(*data, w, h, bpp);
        if (!tr) return;
        STBI_FREE(*data);
        *data = tr;
        *x = h; *y = w;
        w = *x; h = *y;
    }
    bool flipH = (orient == 2 || orient == 3 || orient == 6 || orient == 7);
    bool flipV = (orient == 3 || orient == 4 || orient == 7 || orient == 8);
    if (flipH) stbi__horizontal_flip(*data, w, h, bpp);
    if (flipV) stbi__vertical_flip(*data, w, h, bpp);
}

// HDR/LDR gamma conversion
inline float stbi__l2h_gamma = 2.2f, stbi__l2h_scale = 1.0f;
inline float stbi__h2l_gamma_i = 1.0f / 2.2f, stbi__h2l_scale_i = 1.0f;

#ifndef STBI_NO_LINEAR
inline float* stbi__ldr_to_hdr(stbi_uc* data, int x, int y, int comp) {
    if (!data) return nullptr;
    auto* output = static_cast<float*>(stbi__malloc_mad4(x, y, comp, sizeof(float), 0));
    if (!output) { STBI_FREE(data); return stbi__errpf("outofmem", "Out of memory"); }
    int n = (comp & 1) ? comp : comp - 1;
    for (int i = 0; i < x * y; ++i)
        for (int k = 0; k < n; ++k)
            output[i * comp + k] = static_cast<float>(std::pow(data[i * comp + k] / 255.0f, stbi__l2h_gamma) * stbi__l2h_scale);
    if (n < comp)
        for (int i = 0; i < x * y; ++i)
            output[i * comp + n] = data[i * comp + n] / 255.0f;
    STBI_FREE(data);
    return output;
}
#endif

#ifndef STBI_NO_HDR
constexpr int stbi__float2int(float x) { return static_cast<int>(x); }

inline stbi_uc* stbi__hdr_to_ldr(float* data, int x, int y, int comp) {
    if (!data) return nullptr;
    auto* output = static_cast<stbi_uc*>(stbi__malloc_mad3(x, y, comp, 0));
    if (!output) { STBI_FREE(data); return stbi__errpuc("outofmem", "Out of memory"); }
    int n = (comp & 1) ? comp : comp - 1;
    for (int i = 0; i < x * y; ++i) {
        int k;
        for (k = 0; k < n; ++k) {
            float z = std::clamp(static_cast<float>(std::pow(data[i*comp+k]*stbi__h2l_scale_i, stbi__h2l_gamma_i)) * 255 + 0.5f, 0.0f, 255.0f);
            output[i*comp+k] = static_cast<stbi_uc>(stbi__float2int(z));
        }
        if (k < comp) {
            float z = std::clamp(data[i*comp+k] * 255 + 0.5f, 0.0f, 255.0f);
            output[i*comp+k] = static_cast<stbi_uc>(stbi__float2int(z));
        }
    }
    STBI_FREE(data);
    return output;
}
#endif

// Dispatch functions (defined in stbi_core.cpp)
void*          stbi__load_main(stbi__context* s, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri, int bpc);
unsigned char* stbi__load_and_postprocess_8bit(stbi__context* s, int* x, int* y, int* comp, int req_comp);
uint16_t*      stbi__load_and_postprocess_16bit(stbi__context* s, int* x, int* y, int* comp, int req_comp);
#if !defined(STBI_NO_HDR) && !defined(STBI_NO_LINEAR)
float*         stbi__float_postprocess(float* result, int* x, int* y, int* comp, int req_comp, stbi__result_info* ri);
#endif
#ifndef STBI_NO_LINEAR
float*         stbi__loadf_main(stbi__context* s, int* x, int* y, int* comp, int req_comp);
#endif
int            stbi__info_main(stbi__context* s, int* x, int* y, int* comp);
int            stbi__is_16_main(stbi__context* s);

} // namespace detail
} // namespace kosmvg::stbi
