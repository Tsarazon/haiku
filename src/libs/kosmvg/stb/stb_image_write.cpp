// stb_image_write.cpp - Image writer implementation
// Derived from stb_image_write v2.0 by Sean Barrett (public domain)
// Rewritten as C++20 internal implementation for KosmVG

#include "stb_image.hpp"
#include "stbi_internal.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace kosmvg::stbi {

// WriteContext

using InternalWriteFn = void(*)(void* context, const void* data, int size);

struct WriteContext {
    InternalWriteFn func = nullptr;
    void* context = nullptr;
    unsigned char buffer[4096]{};
    int buf_used = 0;

    void flush() {
        if (buf_used) {
            func(context, buffer, buf_used);
            buf_used = 0;
        }
    }

    void put(uint8_t c) {
        func(context, &c, 1);
    }

    void write(const void* data, int size) {
        func(context, data, size);
    }

    void write_buffered(uint8_t a) {
        if (static_cast<size_t>(buf_used) + 1 > sizeof(buffer))
            flush();
        buffer[buf_used++] = a;
    }

    void write_buffered3(uint8_t a, uint8_t b, uint8_t c) {
        if (static_cast<size_t>(buf_used) + 3 > sizeof(buffer))
            flush();
        int n = buf_used;
        buf_used = n + 3;
        buffer[n + 0] = a;
        buffer[n + 1] = b;
        buffer[n + 2] = c;
    }

    void write_u8(int v) {
        uint8_t x = static_cast<uint8_t>(v & 0xff);
        func(context, &x, 1);
    }

    void write_u16_le(int v) {
        uint8_t b[2];
        b[0] = static_cast<uint8_t>(v & 0xff);
        b[1] = static_cast<uint8_t>((v >> 8) & 0xff);
        func(context, b, 2);
    }

    void write_u32_le(int v) {
        auto x = static_cast<uint32_t>(v);
        uint8_t b[4];
        b[0] = static_cast<uint8_t>(x & 0xff);
        b[1] = static_cast<uint8_t>((x >> 8) & 0xff);
        b[2] = static_cast<uint8_t>((x >> 16) & 0xff);
        b[3] = static_cast<uint8_t>((x >> 24) & 0xff);
        func(context, b, 4);
    }
};

namespace detail {

static constexpr auto uchar(int x) -> uint8_t {
    return static_cast<uint8_t>(x & 0xff);
}

static WriteContext make_callback_context(InternalWriteFn fn, void* ctx) {
    WriteContext s{};
    s.func = fn;
    s.context = ctx;
    return s;
}

static void stdio_write(void* context, const void* data, int size) {
    std::fwrite(data, 1, static_cast<size_t>(size), static_cast<FILE*>(context));
}

static WriteContext make_file_context(FILE* f) {
    WriteContext s{};
    s.func = stdio_write;
    s.context = static_cast<void*>(f);
    return s;
}

static FILE* fopen_wb(const char* filename) {
    return std::fopen(filename, "wb");
}

// Pixel writing helpers

static void write_pixel(WriteContext& s, int rgb_dir, int comp, int write_alpha,
                        int expand_mono, uint8_t* d)
{
    uint8_t bg[3] = { 255, 0, 255 }, px[3];

    if (write_alpha < 0)
        s.write_buffered(d[comp - 1]);

    switch (comp) {
    case 2:
    case 1:
        if (expand_mono)
            s.write_buffered3(d[0], d[0], d[0]);
        else
            s.write_buffered(d[0]);
        break;
    case 4:
        if (!write_alpha) {
            for (int k = 0; k < 3; ++k)
                px[k] = static_cast<uint8_t>(bg[k] + ((d[k] - bg[k]) * d[3]) / 255);
            s.write_buffered3(px[1 - rgb_dir], px[1], px[1 + rgb_dir]);
            break;
        }
        [[fallthrough]];
    case 3:
        s.write_buffered3(d[1 - rgb_dir], d[1], d[1 + rgb_dir]);
        break;
    }
    if (write_alpha > 0)
        s.write_buffered(d[comp - 1]);
}

static void write_pixels(WriteContext& s, int rgb_dir, int vdir, int x, int y,
                         int comp, void* data, int write_alpha, int scanline_pad,
                         int expand_mono, bool flip)
{
    uint32_t zero = 0;

    if (y <= 0)
        return;

    if (flip)
        vdir *= -1;

    int j, j_end;
    if (vdir < 0) {
        j_end = -1; j = y - 1;
    } else {
        j_end = y; j = 0;
    }

    for (; j != j_end; j += vdir) {
        for (int i = 0; i < x; ++i) {
            auto* d = static_cast<uint8_t*>(data) + (j * x + i) * comp;
            write_pixel(s, rgb_dir, comp, write_alpha, expand_mono, d);
        }
        s.flush();
        s.func(s.context, &zero, scanline_pad);
    }
}

static bool pixels_equal(const uint8_t* a, const uint8_t* b, int comp)
{
    switch (comp) {
    case 4: if (a[3] != b[3]) return false; [[fallthrough]];
    case 3: if (a[2] != b[2]) return false; [[fallthrough]];
    case 2: if (a[1] != b[1]) return false; [[fallthrough]];
    case 1: return a[0] == b[0];
    default: return std::memcmp(a, b, static_cast<size_t>(comp)) == 0;
    }
}

// BMP writer

static int write_bmp_core(WriteContext& s, int x, int y, int comp, const void* data,
                          const WriteOptions& opts)
{
    if (!data || x <= 0 || y <= 0 || comp < 1 || comp > 4)
        return 0;

    if (comp != 4) {
        int pad = (-x * 3) & 3;
        s.write_u8('B');
        s.write_u8('M');
        s.write_u32_le(14 + 40 + (x * 3 + pad) * y);
        s.write_u16_le(0);
        s.write_u16_le(0);
        s.write_u32_le(14 + 40);
        s.write_u32_le(40);
        s.write_u32_le(x);
        s.write_u32_le(y);
        s.write_u16_le(1);
        s.write_u16_le(24);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        write_pixels(s, -1, -1, x, y, comp, const_cast<void*>(data), 0, pad, 1, opts.flip_vertically);
        return 1;
    } else {
        s.write_u8('B');
        s.write_u8('M');
        s.write_u32_le(14 + 108 + x * y * 4);
        s.write_u16_le(0);
        s.write_u16_le(0);
        s.write_u32_le(14 + 108);
        s.write_u32_le(108);
        s.write_u32_le(x);
        s.write_u32_le(y);
        s.write_u16_le(1);
        s.write_u16_le(32);
        s.write_u32_le(3);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0x00ff0000);
        s.write_u32_le(0x0000ff00);
        s.write_u32_le(0x000000ff);
        s.write_u32_le(static_cast<int>(0xff000000u));
        s.write_u32_le(0);
        for (int i = 0; i < 9; ++i)
            s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        s.write_u32_le(0);
        write_pixels(s, -1, -1, x, y, comp, const_cast<void*>(data), 1, 0, 1, opts.flip_vertically);
        return 1;
    }
}

// TGA writer

static int write_tga_core(WriteContext& s, int x, int y, int comp, void* data,
                          const WriteOptions& opts)
{
    if (!data || x <= 0 || y <= 0 || comp < 1 || comp > 4)
        return 0;

    int has_alpha = (comp == 2 || comp == 4);
    int colorbytes = has_alpha ? comp - 1 : comp;
    int format = colorbytes < 2 ? 3 : 2;

    if (!opts.tga_rle) {
        s.write_u8(0); s.write_u8(0); s.write_u8(format);
        s.write_u16_le(0); s.write_u16_le(0); s.write_u8(0);
        s.write_u16_le(0); s.write_u16_le(0);
        s.write_u16_le(x); s.write_u16_le(y);
        s.write_u8((colorbytes + has_alpha) * 8);
        s.write_u8(has_alpha * 8);
        write_pixels(s, -1, -1, x, y, comp, data, has_alpha, 0, 0, opts.flip_vertically);
        return 1;
    }

    s.write_u8(0); s.write_u8(0); s.write_u8(format + 8);
    s.write_u16_le(0); s.write_u16_le(0); s.write_u8(0);
    s.write_u16_le(0); s.write_u16_le(0);
    s.write_u16_le(x); s.write_u16_le(y);
    s.write_u8((colorbytes + has_alpha) * 8);
    s.write_u8(has_alpha * 8);

    int j, jend, jdir;
    if (opts.flip_vertically) {
        j = 0; jend = y; jdir = 1;
    } else {
        j = y - 1; jend = -1; jdir = -1;
    }
    for (; j != jend; j += jdir) {
        auto* row = static_cast<uint8_t*>(data) + j * x * comp;

        for (int i = 0; i < x; ) {
            auto* begin = row + i * comp;
            int diff = 1;
            int len = 1;

            if (i < x - 1) {
                ++len;
                diff = !pixels_equal(begin, row + (i + 1) * comp, comp);
                if (diff) {
                    const uint8_t* prev = begin;
                    for (int k = i + 2; k < x && len < 128; ++k) {
                        if (!pixels_equal(prev, row + k * comp, comp)) {
                            prev += comp;
                            ++len;
                        } else {
                            --len;
                            break;
                        }
                    }
                } else {
                    for (int k = i + 2; k < x && len < 128; ++k) {
                        if (pixels_equal(begin, row + k * comp, comp))
                            ++len;
                        else
                            break;
                    }
                }
            }

            if (diff) {
                s.write_buffered(uchar(len - 1));
                for (int k = 0; k < len; ++k)
                    write_pixel(s, -1, comp, has_alpha, 0, begin + k * comp);
            } else {
                s.write_buffered(uchar(len - 129));
                write_pixel(s, -1, comp, has_alpha, 0, begin);
            }
            i += len;
        }
    }
    s.flush();
    return 1;
}

// HDR writer

static void linear_to_rgbe(uint8_t* rgbe, float* linear)
{
    float maxcomp = std::max({linear[0], linear[1], linear[2]});
    if (maxcomp < 1e-32f) {
        rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
    } else {
        int exponent;
        float normalize = std::frexp(maxcomp, &exponent) * 256.0f / maxcomp;
        rgbe[0] = static_cast<uint8_t>(linear[0] * normalize);
        rgbe[1] = static_cast<uint8_t>(linear[1] * normalize);
        rgbe[2] = static_cast<uint8_t>(linear[2] * normalize);
        rgbe[3] = static_cast<uint8_t>(exponent + 128);
    }
}

static void write_run_data(WriteContext& s, int length, uint8_t databyte)
{
    uint8_t lengthbyte = uchar(length + 128);
    STBI_ASSERT(length + 128 <= 255);
    s.func(s.context, &lengthbyte, 1);
    s.func(s.context, &databyte, 1);
}

static void write_dump_data(WriteContext& s, int length, uint8_t* data)
{
    uint8_t lengthbyte = uchar(length);
    STBI_ASSERT(length <= 128);
    s.func(s.context, &lengthbyte, 1);
    s.func(s.context, data, length);
}

static void write_hdr_scanline(WriteContext& s, int width, int ncomp,
                               uint8_t* scratch, float* scanline)
{
    uint8_t scanlineheader[4] = { 2, 2, 0, 0 };
    uint8_t rgbe[4];
    float linear[3];

    scanlineheader[2] = static_cast<uint8_t>((width & 0xff00) >> 8);
    scanlineheader[3] = static_cast<uint8_t>(width & 0x00ff);

    if (width < 8 || width >= 32768) {
        for (int x = 0; x < width; x++) {
            switch (ncomp) {
            case 4: [[fallthrough]];
            case 3: linear[2] = scanline[x * ncomp + 2];
                    linear[1] = scanline[x * ncomp + 1];
                    linear[0] = scanline[x * ncomp + 0];
                    break;
            default:
                    linear[0] = linear[1] = linear[2] = scanline[x * ncomp + 0];
                    break;
            }
            linear_to_rgbe(rgbe, linear);
            s.func(s.context, rgbe, 4);
        }
    } else {
        for (int x = 0; x < width; x++) {
            switch (ncomp) {
            case 4: [[fallthrough]];
            case 3: linear[2] = scanline[x * ncomp + 2];
                    linear[1] = scanline[x * ncomp + 1];
                    linear[0] = scanline[x * ncomp + 0];
                    break;
            default:
                    linear[0] = linear[1] = linear[2] = scanline[x * ncomp + 0];
                    break;
            }
            linear_to_rgbe(rgbe, linear);
            scratch[x + width * 0] = rgbe[0];
            scratch[x + width * 1] = rgbe[1];
            scratch[x + width * 2] = rgbe[2];
            scratch[x + width * 3] = rgbe[3];
        }

        s.func(s.context, scanlineheader, 4);

        for (int c = 0; c < 4; c++) {
            uint8_t* comp_data = &scratch[width * c];
            int x = 0;
            while (x < width) {
                int r = x;
                while (r + 2 < width) {
                    if (comp_data[r] == comp_data[r + 1] && comp_data[r] == comp_data[r + 2])
                        break;
                    ++r;
                }
                if (r + 2 >= width) r = width;
                while (x < r) {
                    int len = r - x;
                    if (len > 128) len = 128;
                    write_dump_data(s, len, &comp_data[x]);
                    x += len;
                }
                if (r + 2 < width) {
                    while (r < width && comp_data[r] == comp_data[x])
                        ++r;
                    while (x < r) {
                        int len = r - x;
                        if (len > 127) len = 127;
                        write_run_data(s, len, comp_data[x]);
                        x += len;
                    }
                }
            }
        }
    }
}

static int write_hdr_core(WriteContext& s, int x, int y, int comp, float* data,
                          const WriteOptions& opts)
{
    if (x <= 0 || y <= 0 || !data || comp < 1 || comp > 4)
        return 0;

    auto* scratch = static_cast<uint8_t*>(STBI_MALLOC(static_cast<size_t>(x) * 4));
    char buffer[128];
    char header[] = "#?RADIANCE\n# Written by stb_image_write.h\nFORMAT=32-bit_rle_rgbe\n";
    s.func(s.context, header, sizeof(header) - 1);

    int len = std::snprintf(buffer, sizeof(buffer),
        "EXPOSURE=          1.0000000000000\n\n-Y %d +X %d\n", y, x);
    s.func(s.context, buffer, len);

    for (int i = 0; i < y; i++)
        write_hdr_scanline(s, x, comp, scratch,
            data + comp * x * (opts.flip_vertically ? y - 1 - i : i));
    STBI_FREE(scratch);
    return 1;
}

// PNG writer: zlib compression

struct ByteBuffer {
    uint8_t* data = nullptr;
    int count = 0;
    int capacity = 0;

    void push(uint8_t v) {
        if (count >= capacity) grow(1);
        data[count++] = v;
    }

    void grow(int increment) {
        int m = data ? 2 * capacity + increment : increment + 1;
        auto* p = static_cast<uint8_t*>(
            STBI_REALLOC_SIZED(data, static_cast<size_t>(capacity), static_cast<size_t>(m)));
        STBI_ASSERT(p);
        data = p;
        capacity = m;
    }

    void reset() {
        if (data) STBI_FREE(data);
        data = nullptr;
        count = 0;
        capacity = 0;
    }

    [[nodiscard]] uint8_t* release(int* out_len) {
        *out_len = count;
        auto* result = static_cast<uint8_t*>(
            STBI_REALLOC_SIZED(data, static_cast<size_t>(capacity), static_cast<size_t>(count)));
        data = nullptr;
        count = 0;
        capacity = 0;
        return result;
    }
};

struct PtrBucket {
    uint8_t** data = nullptr;
    int count = 0;
    int capacity = 0;

    void push(uint8_t* v) {
        if (count >= capacity) {
            int m = data ? 2 * capacity + 1 : 2;
            auto* p = static_cast<uint8_t**>(STBI_REALLOC_SIZED(
                data,
                static_cast<size_t>(capacity) * sizeof(uint8_t*),
                static_cast<size_t>(m) * sizeof(uint8_t*)));
            STBI_ASSERT(p);
            data = p;
            capacity = m;
        }
        data[count++] = v;
    }

    void reset() {
        if (data) STBI_FREE(data);
        data = nullptr;
        count = 0;
        capacity = 0;
    }
};

#ifndef STBI_ZLIB_COMPRESS

static uint8_t* zlib_flushf(ByteBuffer& out, unsigned int* bitbuffer, int* bitcount)
{
    while (*bitcount >= 8) {
        out.push(uchar(*bitbuffer));
        *bitbuffer >>= 8;
        *bitcount -= 8;
    }
    return out.data;
}

static int zlib_bitrev(int code, int codebits)
{
    int res = 0;
    while (codebits--) {
        res = (res << 1) | (code & 1);
        code >>= 1;
    }
    return res;
}

static unsigned int zlib_countm(uint8_t* a, uint8_t* b, int limit)
{
    int i;
    for (i = 0; i < limit && i < 258; ++i)
        if (a[i] != b[i]) break;
    return static_cast<unsigned int>(i);
}

static unsigned int zhash(uint8_t* data)
{
    uint32_t hash = data[0] + (data[1] << 8) + (data[2] << 16);
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash;
}

static constexpr int kZHashSize = 16384;

#define STBI__ZLIB_FLUSH() (zlib_flushf(out, &bitbuf, &bitcount))
#define STBI__ZLIB_ADD(code,codebits) \
    (bitbuf |= (code) << bitcount, bitcount += (codebits), STBI__ZLIB_FLUSH())
#define STBI__ZLIB_HUFFA(b,c)  STBI__ZLIB_ADD(zlib_bitrev(b,c),c)
#define STBI__ZLIB_HUFF1(n)  STBI__ZLIB_HUFFA(0x30 + (n), 8)
#define STBI__ZLIB_HUFF2(n)  STBI__ZLIB_HUFFA(0x190 + (n)-144, 9)
#define STBI__ZLIB_HUFF3(n)  STBI__ZLIB_HUFFA(0 + (n)-256,7)
#define STBI__ZLIB_HUFF4(n)  STBI__ZLIB_HUFFA(0xc0 + (n)-280,8)
#define STBI__ZLIB_HUFF(n)  ((n) <= 143 ? STBI__ZLIB_HUFF1(n) : (n) <= 255 ? STBI__ZLIB_HUFF2(n) : (n) <= 279 ? STBI__ZLIB_HUFF3(n) : STBI__ZLIB_HUFF4(n))
#define STBI__ZLIB_HUFFB(n) ((n) <= 143 ? STBI__ZLIB_HUFF1(n) : STBI__ZLIB_HUFF2(n))

#endif // STBI_ZLIB_COMPRESS

static uint8_t* zlib_compress_impl(uint8_t* data, int data_len, int* out_len, int quality)
{
    if (!data || data_len <= 0)
        return nullptr;

#ifdef STBI_ZLIB_COMPRESS
    return STBI_ZLIB_COMPRESS(data, data_len, out_len, quality);
#else
    static constexpr unsigned short lengthc[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258, 259 };
    static constexpr uint8_t  lengtheb[]= { 0,0,0,0,0,0,0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  0 };
    static constexpr unsigned short distc[]   = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577, 32768 };
    static constexpr uint8_t  disteb[]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

    unsigned int bitbuf = 0;
    int i, j, bitcount = 0;
    ByteBuffer out;

    auto hash_table = std::make_unique<PtrBucket[]>(kZHashSize);

    if (quality < 5) quality = 5;

    out.push(0x78);
    out.push(0x5e);
    STBI__ZLIB_ADD(1, 1);
    STBI__ZLIB_ADD(1, 2);

    i = 0;
    while (i < data_len - 3) {
        int h = static_cast<int>(zhash(data + i) & (kZHashSize - 1)), best = 3;
        uint8_t* bestloc = nullptr;
        auto& bucket = hash_table[h];
        int n = bucket.count;
        for (j = 0; j < n; ++j) {
            if (bucket.data[j] - data > i - 32768) {
                int d = static_cast<int>(zlib_countm(bucket.data[j], data + i, data_len - i));
                if (d >= best) { best = d; bestloc = bucket.data[j]; }
            }
        }
        if (bucket.count == 2 * quality) {
            std::memmove(bucket.data, bucket.data + quality,
                sizeof(bucket.data[0]) * static_cast<size_t>(quality));
            bucket.count = quality;
        }
        bucket.push(data + i);

        if (bestloc) {
            h = static_cast<int>(zhash(data + i + 1) & (kZHashSize - 1));
            auto& bucket2 = hash_table[h];
            n = bucket2.count;
            for (j = 0; j < n; ++j) {
                if (bucket2.data[j] - data > i - 32767) {
                    int e = static_cast<int>(zlib_countm(bucket2.data[j], data + i + 1, data_len - i - 1));
                    if (e > best) {
                        bestloc = nullptr;
                        break;
                    }
                }
            }
        }

        if (bestloc) {
            int d = static_cast<int>(data + i - bestloc);
            STBI_ASSERT(d <= 32767 && best <= 258);
            for (j = 0; best > lengthc[j + 1] - 1; ++j);
            STBI__ZLIB_HUFF(j + 257);
            if (lengtheb[j]) STBI__ZLIB_ADD(best - lengthc[j], lengtheb[j]);
            for (j = 0; d > distc[j + 1] - 1; ++j);
            STBI__ZLIB_ADD(zlib_bitrev(j, 5), 5);
            if (disteb[j]) STBI__ZLIB_ADD(d - distc[j], disteb[j]);
            i += best;
        } else {
            STBI__ZLIB_HUFFB(data[i]);
            ++i;
        }
    }
    for (; i < data_len; ++i)
        STBI__ZLIB_HUFFB(data[i]);
    STBI__ZLIB_HUFF(256);
    while (bitcount)
        STBI__ZLIB_ADD(0, 1);

    for (i = 0; i < kZHashSize; ++i)
        hash_table[i].reset();
    hash_table.reset();

    if (out.count > data_len + 2 + ((data_len + 32766) / 32767) * 5) {
        out.count = 2;
        for (j = 0; j < data_len;) {
            int blocklen = data_len - j;
            if (blocklen > 32767) blocklen = 32767;
            out.push(static_cast<uint8_t>(data_len - j == blocklen));
            out.push(uchar(blocklen));
            out.push(uchar(blocklen >> 8));
            out.push(uchar(~blocklen));
            out.push(uchar(~blocklen >> 8));
            if (out.count + blocklen > out.capacity)
                out.grow(blocklen);
            std::memcpy(out.data + out.count, data + j, static_cast<size_t>(blocklen));
            out.count += blocklen;
            j += blocklen;
        }
    }

    {
        unsigned int s1 = 1, s2 = 0;
        int blocklen = data_len % 5552;
        j = 0;
        while (j < data_len) {
            for (i = 0; i < blocklen; ++i) { s1 += data[j + i]; s2 += s1; }
            s1 %= 65521; s2 %= 65521;
            j += blocklen;
            blocklen = 5552;
        }
        out.push(uchar(s2 >> 8));
        out.push(uchar(s2));
        out.push(uchar(s1 >> 8));
        out.push(uchar(s1));
    }

    return out.release(out_len);
#endif // STBI_ZLIB_COMPRESS
}

#ifndef STBI_ZLIB_COMPRESS
#undef STBI__ZLIB_FLUSH
#undef STBI__ZLIB_ADD
#undef STBI__ZLIB_HUFFA
#undef STBI__ZLIB_HUFF1
#undef STBI__ZLIB_HUFF2
#undef STBI__ZLIB_HUFF3
#undef STBI__ZLIB_HUFF4
#undef STBI__ZLIB_HUFF
#undef STBI__ZLIB_HUFFB
#endif

// PNG writer: CRC32

static unsigned int crc32(uint8_t* buffer, int len)
{
#ifdef STBI_CRC32
    return STBI_CRC32(buffer, len);
#else
    static constexpr std::array<unsigned int, 256> crc_table = {{
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0eDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    }};

    unsigned int crc = ~0u;
    for (int i = 0; i < len; ++i)
        crc = (crc >> 8) ^ crc_table[buffer[i] ^ (crc & 0xff)];
    return ~crc;
#endif
}

// PNG writer: chunk helpers

static void wpng4(uint8_t*& o, int a, int b, int c, int d)
{
    o[0] = uchar(a); o[1] = uchar(b); o[2] = uchar(c); o[3] = uchar(d);
    o += 4;
}

static void wp32(uint8_t*& o, int v)
{
    wpng4(o, (v) >> 24, (v) >> 16, (v) >> 8, (v));
}

static void wptag(uint8_t*& o, const char* s)
{
    wpng4(o, s[0], s[1], s[2], s[3]);
}

static void wpcrc(uint8_t*& data, int len)
{
    unsigned int c = crc32(data - len - 4, len + 4);
    wp32(data, c);
}

// PNG writer: filter encoding

static uint8_t paeth(int a, int b, int c)
{
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return uchar(a);
    if (pb <= pc) return uchar(b);
    return uchar(c);
}

static void encode_png_line(uint8_t* pixels, int stride_bytes, int width, int height,
                            int y, int n, int filter_type, signed char* line_buffer,
                            bool flip)
{
    static constexpr int mapping[]  = { 0, 1, 2, 3, 4 };
    static constexpr int firstmap[] = { 0, 1, 0, 5, 6 };
    const int* mymap = (y != 0) ? mapping : firstmap;
    int type = mymap[filter_type];
    uint8_t* z = pixels + stride_bytes * (flip ? height - 1 - y : y);
    int signed_stride = flip ? -stride_bytes : stride_bytes;

    if (type == 0) {
        std::memcpy(line_buffer, z, static_cast<size_t>(width * n));
        return;
    }

    for (int i = 0; i < n; ++i) {
        switch (type) {
        case 1: line_buffer[i] = static_cast<signed char>(z[i]); break;
        case 2: line_buffer[i] = static_cast<signed char>(z[i] - z[i - signed_stride]); break;
        case 3: line_buffer[i] = static_cast<signed char>(z[i] - (z[i - signed_stride] >> 1)); break;
        case 4: line_buffer[i] = static_cast<signed char>(z[i] - paeth(0, z[i - signed_stride], 0)); break;
        case 5: line_buffer[i] = static_cast<signed char>(z[i]); break;
        case 6: line_buffer[i] = static_cast<signed char>(z[i]); break;
        }
    }
    switch (type) {
    case 1: for (int i = n; i < width * n; ++i) line_buffer[i] = static_cast<signed char>(z[i] - z[i - n]); break;
    case 2: for (int i = n; i < width * n; ++i) line_buffer[i] = static_cast<signed char>(z[i] - z[i - signed_stride]); break;
    case 3: for (int i = n; i < width * n; ++i) line_buffer[i] = static_cast<signed char>(z[i] - ((z[i - n] + z[i - signed_stride]) >> 1)); break;
    case 4: for (int i = n; i < width * n; ++i) line_buffer[i] = static_cast<signed char>(z[i] - paeth(z[i - n], z[i - signed_stride], z[i - signed_stride - n])); break;
    case 5: for (int i = n; i < width * n; ++i) line_buffer[i] = static_cast<signed char>(z[i] - (z[i - n] >> 1)); break;
    case 6: for (int i = n; i < width * n; ++i) line_buffer[i] = static_cast<signed char>(z[i] - paeth(z[i - n], 0, 0)); break;
    }
}

// PNG writer: encode to memory

static uint8_t* write_png_to_mem_impl(const uint8_t* pixels, int stride_bytes,
                                      int x, int y, int n, int* out_len,
                                      const WriteOptions& opts)
{
    if (!pixels || x <= 0 || y <= 0 || n < 1 || n > 4)
        return nullptr;

    int ctype[5] = { -1, 0, 4, 2, 6 };
    uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    uint8_t *out, *o, *filt, *zlib;
    signed char* line_buffer;
    int j, zlen;
    int f_filter = static_cast<int>(opts.png_filter);

    if (stride_bytes == 0)
        stride_bytes = x * n;

    if (f_filter >= 5)
        f_filter = -1;

    filt = static_cast<uint8_t*>(STBI_MALLOC(static_cast<size_t>(x * n + 1) * static_cast<size_t>(y)));
    if (!filt) return nullptr;
    line_buffer = static_cast<signed char*>(STBI_MALLOC(static_cast<size_t>(x * n)));
    if (!line_buffer) { STBI_FREE(filt); return nullptr; }

    auto* best_buffer = static_cast<signed char*>(STBI_MALLOC(static_cast<size_t>(x * n)));
    if (!best_buffer) { STBI_FREE(line_buffer); STBI_FREE(filt); return nullptr; }

    int const line_len = x * n;

    for (j = 0; j < y; ++j) {
        int filter_type;
        if (f_filter > -1) {
            filter_type = f_filter;
            encode_png_line(const_cast<uint8_t*>(pixels), stride_bytes, x, y, j, n, f_filter, line_buffer, opts.flip_vertically);
            std::memcpy(best_buffer, line_buffer, static_cast<size_t>(line_len));
        } else {
            int best_filter = 0, best_filter_val = 0x7fffffff;
            for (filter_type = 0; filter_type < 5; filter_type++) {
                encode_png_line(const_cast<uint8_t*>(pixels), stride_bytes, x, y, j, n, filter_type, line_buffer, opts.flip_vertically);
                int est = 0;
                bool early_out = false;
                for (int i = 0; i < line_len; ++i) {
                    est += std::abs(static_cast<int>(line_buffer[i]));
                    if (est >= best_filter_val) { early_out = true; break; }
                }
                if (!early_out && est < best_filter_val) {
                    best_filter_val = est;
                    best_filter = filter_type;
                    std::swap(line_buffer, best_buffer);
                }
            }
            filter_type = best_filter;
        }
        filt[j * (line_len + 1)] = static_cast<uint8_t>(filter_type);
        std::memcpy(filt + j * (line_len + 1) + 1, best_buffer, static_cast<size_t>(line_len));
    }
    STBI_FREE(best_buffer);
    STBI_FREE(line_buffer);
    zlib = zlib_compress_impl(filt, y * (x * n + 1), &zlen, opts.png_compression_level);
    STBI_FREE(filt);
    if (!zlib) return nullptr;

    int extra = 0;
    if (opts.png_srgb) extra += 13;
    if (opts.png_ppm_x > 0 && opts.png_ppm_y > 0) extra += 21;

    out = static_cast<uint8_t*>(STBI_MALLOC(static_cast<size_t>(8 + 12 + 13 + extra + 12 + zlen + 12)));
    if (!out) return nullptr;
    *out_len = 8 + 12 + 13 + extra + 12 + zlen + 12;

    o = out;
    std::memmove(o, sig, 8); o += 8;
    wp32(o, 13);
    wptag(o, "IHDR");
    wp32(o, x);
    wp32(o, y);
    *o++ = 8;
    *o++ = uchar(ctype[n]);
    *o++ = 0;
    *o++ = 0;
    *o++ = 0;
    wpcrc(o, 13);

    if (opts.png_srgb) {
        wp32(o, 1);
        wptag(o, "sRGB");
        *o++ = 0;
        wpcrc(o, 1);
    }

    if (opts.png_ppm_x > 0 && opts.png_ppm_y > 0) {
        wp32(o, 9);
        wptag(o, "pHYs");
        wp32(o, opts.png_ppm_x);
        wp32(o, opts.png_ppm_y);
        *o++ = 1;
        wpcrc(o, 9);
    }

    wp32(o, zlen);
    wptag(o, "IDAT");
    std::memmove(o, zlib, static_cast<size_t>(zlen));
    o += zlen;
    STBI_FREE(zlib);
    wpcrc(o, zlen);

    wp32(o, 0);
    wptag(o, "IEND");
    wpcrc(o, 0);

    STBI_ASSERT(o == out + *out_len);
    return out;
}

// JPEG writer

static constexpr uint8_t jpg_ZigZag[] = {
    0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,
    24,31,40,44,53,10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63
};

static void jpg_writeBits(WriteContext& s, int* bitBufP, int* bitCntP, const unsigned short* bs)
{
    int bitBuf = *bitBufP, bitCnt = *bitCntP;
    bitCnt += bs[1];
    bitBuf |= bs[0] << (24 - bitCnt);
    while (bitCnt >= 8) {
        auto c = static_cast<uint8_t>((bitBuf >> 16) & 255);
        s.write_buffered(c);
        if (c == 255) s.write_buffered(0);
        bitBuf <<= 8;
        bitCnt -= 8;
    }
    *bitBufP = bitBuf;
    *bitCntP = bitCnt;
}

static void jpg_DCT(float* d0p, float* d1p, float* d2p, float* d3p,
                    float* d4p, float* d5p, float* d6p, float* d7p)
{
    float d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p;
    float d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
    float z1, z2, z3, z4, z5, z11, z13;

    float tmp0 = d0 + d7, tmp7 = d0 - d7;
    float tmp1 = d1 + d6, tmp6 = d1 - d6;
    float tmp2 = d2 + d5, tmp5 = d2 - d5;
    float tmp3 = d3 + d4, tmp4 = d3 - d4;

    float tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;

    d0 = tmp10 + tmp11;
    d4 = tmp10 - tmp11;
    z1 = (tmp12 + tmp13) * 0.707106781f;
    d2 = tmp13 + z1;
    d6 = tmp13 - z1;

    tmp10 = tmp4 + tmp5; tmp11 = tmp5 + tmp6; tmp12 = tmp6 + tmp7;
    z5 = (tmp10 - tmp12) * 0.382683433f;
    z2 = tmp10 * 0.541196100f + z5;
    z4 = tmp12 * 1.306562965f + z5;
    z3 = tmp11 * 0.707106781f;
    z11 = tmp7 + z3; z13 = tmp7 - z3;

    *d5p = z13 + z2; *d3p = z13 - z2;
    *d1p = z11 + z4; *d7p = z11 - z4;
    *d0p = d0; *d2p = d2; *d4p = d4; *d6p = d6;
}

static void jpg_calcBits(int val, unsigned short bits[2])
{
    int tmp1 = val < 0 ? -val : val;
    val = val < 0 ? val - 1 : val;
    bits[1] = 1;
    while (tmp1 >>= 1) ++bits[1];
    bits[0] = static_cast<unsigned short>(val & ((1 << bits[1]) - 1));
}

static int jpg_processDU(WriteContext& s, int* bitBuf, int* bitCnt, float* CDU,
                         int du_stride, float* fdtbl, int DC,
                         const unsigned short HTDC[256][2],
                         const unsigned short HTAC[256][2])
{
    const unsigned short EOB[2] = { HTAC[0x00][0], HTAC[0x00][1] };
    const unsigned short M16zeroes[2] = { HTAC[0xF0][0], HTAC[0xF0][1] };
    int dataOff, i, j, n, diff, end0pos, x, y;
    int DU[64];

    for (dataOff = 0, n = du_stride * 8; dataOff < n; dataOff += du_stride)
        jpg_DCT(&CDU[dataOff], &CDU[dataOff+1], &CDU[dataOff+2], &CDU[dataOff+3],
                &CDU[dataOff+4], &CDU[dataOff+5], &CDU[dataOff+6], &CDU[dataOff+7]);
    for (dataOff = 0; dataOff < 8; ++dataOff)
        jpg_DCT(&CDU[dataOff], &CDU[dataOff+du_stride], &CDU[dataOff+du_stride*2], &CDU[dataOff+du_stride*3],
                &CDU[dataOff+du_stride*4], &CDU[dataOff+du_stride*5], &CDU[dataOff+du_stride*6], &CDU[dataOff+du_stride*7]);
    for (y = 0, j = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x, ++j) {
            i = y * du_stride + x;
            float v = CDU[i] * fdtbl[j];
            DU[jpg_ZigZag[j]] = static_cast<int>(v < 0 ? v - 0.5f : v + 0.5f);
        }
    }

    diff = DU[0] - DC;
    if (diff == 0) {
        jpg_writeBits(s, bitBuf, bitCnt, HTDC[0]);
    } else {
        unsigned short bits[2];
        jpg_calcBits(diff, bits);
        jpg_writeBits(s, bitBuf, bitCnt, HTDC[bits[1]]);
        jpg_writeBits(s, bitBuf, bitCnt, bits);
    }

    end0pos = 63;
    for (; (end0pos > 0) && (DU[end0pos] == 0); --end0pos) {}
    if (end0pos == 0) {
        jpg_writeBits(s, bitBuf, bitCnt, EOB);
        return DU[0];
    }
    for (i = 1; i <= end0pos; ++i) {
        int startpos = i;
        unsigned short bits[2];
        for (; DU[i] == 0 && i <= end0pos; ++i) {}
        int nrzeroes = i - startpos;
        if (nrzeroes >= 16) {
            int lng = nrzeroes >> 4;
            for (int nrmarker = 1; nrmarker <= lng; ++nrmarker)
                jpg_writeBits(s, bitBuf, bitCnt, M16zeroes);
            nrzeroes &= 15;
        }
        jpg_calcBits(DU[i], bits);
        jpg_writeBits(s, bitBuf, bitCnt, HTAC[(nrzeroes << 4) + bits[1]]);
        jpg_writeBits(s, bitBuf, bitCnt, bits);
    }
    if (end0pos != 63)
        jpg_writeBits(s, bitBuf, bitCnt, EOB);
    return DU[0];
}

static int write_jpg_core(WriteContext& s, int width, int height, int comp,
                          const void* data, const WriteOptions& opts)
{
    static constexpr uint8_t std_dc_luminance_nrcodes[] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
    static constexpr uint8_t std_dc_luminance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static constexpr uint8_t std_ac_luminance_nrcodes[] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
    static constexpr uint8_t std_ac_luminance_values[] = {
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
        0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
        0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
        0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
        0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
        0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
    };
    static constexpr uint8_t std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
    static constexpr uint8_t std_dc_chrominance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static constexpr uint8_t std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
    static constexpr uint8_t std_ac_chrominance_values[] = {
        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
        0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
        0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
        0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
    };
    static constexpr unsigned short YDC_HT[256][2] = { {0,2},{2,3},{3,3},{4,3},{5,3},{6,3},{14,4},{30,5},{62,6},{126,7},{254,8},{510,9}};
    static constexpr unsigned short UVDC_HT[256][2] = { {0,2},{1,2},{2,2},{6,3},{14,4},{30,5},{62,6},{126,7},{254,8},{510,9},{1022,10},{2046,11}};
    static constexpr unsigned short YAC_HT[256][2] = {
      {10,4},{0,2},{1,2},{4,3},{11,4},{26,5},{120,7},{248,8},{1014,10},{65410,16},{65411,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {12,4},{27,5},{121,7},{502,9},{2038,11},{65412,16},{65413,16},{65414,16},{65415,16},{65416,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {28,5},{249,8},{1015,10},{4084,12},{65417,16},{65418,16},{65419,16},{65420,16},{65421,16},{65422,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {58,6},{503,9},{4085,12},{65423,16},{65424,16},{65425,16},{65426,16},{65427,16},{65428,16},{65429,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {59,6},{1016,10},{65430,16},{65431,16},{65432,16},{65433,16},{65434,16},{65435,16},{65436,16},{65437,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {122,7},{2039,11},{65438,16},{65439,16},{65440,16},{65441,16},{65442,16},{65443,16},{65444,16},{65445,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {123,7},{4086,12},{65446,16},{65447,16},{65448,16},{65449,16},{65450,16},{65451,16},{65452,16},{65453,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {250,8},{4087,12},{65454,16},{65455,16},{65456,16},{65457,16},{65458,16},{65459,16},{65460,16},{65461,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {504,9},{32704,15},{65462,16},{65463,16},{65464,16},{65465,16},{65466,16},{65467,16},{65468,16},{65469,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {505,9},{65470,16},{65471,16},{65472,16},{65473,16},{65474,16},{65475,16},{65476,16},{65477,16},{65478,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {506,9},{65479,16},{65480,16},{65481,16},{65482,16},{65483,16},{65484,16},{65485,16},{65486,16},{65487,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1017,10},{65488,16},{65489,16},{65490,16},{65491,16},{65492,16},{65493,16},{65494,16},{65495,16},{65496,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1018,10},{65497,16},{65498,16},{65499,16},{65500,16},{65501,16},{65502,16},{65503,16},{65504,16},{65505,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2040,11},{65506,16},{65507,16},{65508,16},{65509,16},{65510,16},{65511,16},{65512,16},{65513,16},{65514,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {65515,16},{65516,16},{65517,16},{65518,16},{65519,16},{65520,16},{65521,16},{65522,16},{65523,16},{65524,16},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2041,11},{65525,16},{65526,16},{65527,16},{65528,16},{65529,16},{65530,16},{65531,16},{65532,16},{65533,16},{65534,16},{0,0},{0,0},{0,0},{0,0},{0,0}
   };
    static constexpr unsigned short UVAC_HT[256][2] = {
      {0,2},{1,2},{4,3},{10,4},{24,5},{25,5},{56,6},{120,7},{500,9},{1014,10},{4084,12},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {11,4},{57,6},{246,8},{501,9},{2038,11},{4085,12},{65416,16},{65417,16},{65418,16},{65419,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {26,5},{247,8},{1015,10},{4086,12},{32706,15},{65420,16},{65421,16},{65422,16},{65423,16},{65424,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {27,5},{248,8},{1016,10},{4087,12},{65425,16},{65426,16},{65427,16},{65428,16},{65429,16},{65430,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {58,6},{502,9},{65431,16},{65432,16},{65433,16},{65434,16},{65435,16},{65436,16},{65437,16},{65438,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {59,6},{1017,10},{65439,16},{65440,16},{65441,16},{65442,16},{65443,16},{65444,16},{65445,16},{65446,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {121,7},{2039,11},{65447,16},{65448,16},{65449,16},{65450,16},{65451,16},{65452,16},{65453,16},{65454,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {122,7},{2040,11},{65455,16},{65456,16},{65457,16},{65458,16},{65459,16},{65460,16},{65461,16},{65462,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {249,8},{65463,16},{65464,16},{65465,16},{65466,16},{65467,16},{65468,16},{65469,16},{65470,16},{65471,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {503,9},{65472,16},{65473,16},{65474,16},{65475,16},{65476,16},{65477,16},{65478,16},{65479,16},{65480,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {504,9},{65481,16},{65482,16},{65483,16},{65484,16},{65485,16},{65486,16},{65487,16},{65488,16},{65489,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {505,9},{65490,16},{65491,16},{65492,16},{65493,16},{65494,16},{65495,16},{65496,16},{65497,16},{65498,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {506,9},{65499,16},{65500,16},{65501,16},{65502,16},{65503,16},{65504,16},{65505,16},{65506,16},{65507,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {2041,11},{65508,16},{65509,16},{65510,16},{65511,16},{65512,16},{65513,16},{65514,16},{65515,16},{65516,16},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
      {16352,14},{65517,16},{65518,16},{65519,16},{65520,16},{65521,16},{65522,16},{65523,16},{65524,16},{65525,16},{0,0},{0,0},{0,0},{0,0},{0,0},
      {1018,10},{32707,15},{65526,16},{65527,16},{65528,16},{65529,16},{65530,16},{65531,16},{65532,16},{65533,16},{65534,16},{0,0},{0,0},{0,0},{0,0},{0,0}
   };
    static constexpr int YQT[] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,
                                   37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
    static constexpr int UVQT[] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
                                    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
    static constexpr float aasf[] = { 1.0f * 2.828427125f, 1.387039845f * 2.828427125f, 1.306562965f * 2.828427125f, 1.175875602f * 2.828427125f,
                                       1.0f * 2.828427125f, 0.785694958f * 2.828427125f, 0.541196100f * 2.828427125f, 0.275899379f * 2.828427125f };

    int row, col, i, k, subsample;
    float fdtbl_Y[64], fdtbl_UV[64];
    uint8_t YTable[64], UVTable[64];

    if (!data || !width || !height || comp > 4 || comp < 1)
        return 0;

    int quality = opts.jpeg_quality ? opts.jpeg_quality : 90;
    subsample = quality <= 90 ? 1 : 0;
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

    for (i = 0; i < 64; ++i) {
        int uvti, yti = (YQT[i] * quality + 50) / 100;
        YTable[jpg_ZigZag[i]] = static_cast<uint8_t>(yti < 1 ? 1 : yti > 255 ? 255 : yti);
        uvti = (UVQT[i] * quality + 50) / 100;
        UVTable[jpg_ZigZag[i]] = static_cast<uint8_t>(uvti < 1 ? 1 : uvti > 255 ? 255 : uvti);
    }

    for (row = 0, k = 0; row < 8; ++row) {
        for (col = 0; col < 8; ++col, ++k) {
            fdtbl_Y[k]  = 1.0f / (YTable[jpg_ZigZag[k]] * aasf[row] * aasf[col]);
            fdtbl_UV[k] = 1.0f / (UVTable[jpg_ZigZag[k]] * aasf[row] * aasf[col]);
        }
    }

    // Write Headers
    {
        static constexpr uint8_t head0[] = { 0xFF,0xD8,0xFF,0xE0,0,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0,0xFF,0xDB,0,0x84,0 };
        static constexpr uint8_t head2[] = { 0xFF,0xDA,0,0xC,3,1,0,2,0x11,3,0x11,0,0x3F,0 };
        const uint8_t head1[] = { 0xFF,0xC0,0,0x11,8,
            static_cast<uint8_t>(height >> 8), uchar(height),
            static_cast<uint8_t>(width >> 8), uchar(width),
            3,1,static_cast<uint8_t>(subsample ? 0x22 : 0x11),0,2,0x11,1,3,0x11,1,0xFF,0xC4,0x01,0xA2,0 };
        s.write(head0, sizeof(head0));
        s.write(YTable, sizeof(YTable));
        s.put(1);
        s.write(UVTable, sizeof(UVTable));
        s.write(head1, sizeof(head1));
        s.write(std_dc_luminance_nrcodes + 1, sizeof(std_dc_luminance_nrcodes) - 1);
        s.write(std_dc_luminance_values, sizeof(std_dc_luminance_values));
        s.put(0x10);
        s.write(std_ac_luminance_nrcodes + 1, sizeof(std_ac_luminance_nrcodes) - 1);
        s.write(std_ac_luminance_values, sizeof(std_ac_luminance_values));
        s.put(1);
        s.write(std_dc_chrominance_nrcodes + 1, sizeof(std_dc_chrominance_nrcodes) - 1);
        s.write(std_dc_chrominance_values, sizeof(std_dc_chrominance_values));
        s.put(0x11);
        s.write(std_ac_chrominance_nrcodes + 1, sizeof(std_ac_chrominance_nrcodes) - 1);
        s.write(std_ac_chrominance_values, sizeof(std_ac_chrominance_values));
        s.write(head2, sizeof(head2));
    }

    // Encode 8x8 macroblocks
    {
        static constexpr unsigned short fillBits[] = {0x7F, 7};
        int DCY = 0, DCU = 0, DCV = 0;
        int bitBuf = 0, bitCnt = 0;
        int ofsG = comp > 2 ? 1 : 0, ofsB = comp > 2 ? 2 : 0;
        const auto* dataR = static_cast<const uint8_t*>(data);
        const auto* dataG = dataR + ofsG;
        const auto* dataB = dataR + ofsB;
        int x, y, pos;

        if (subsample) {
            for (y = 0; y < height; y += 16) {
                for (x = 0; x < width; x += 16) {
                    float Y[256], U[256], V[256];
                    for (row = y, pos = 0; row < y + 16; ++row) {
                        int clamped_row = (row < height) ? row : height - 1;
                        int base_p = (opts.flip_vertically ? (height - 1 - clamped_row) : clamped_row) * width * comp;
                        for (col = x; col < x + 16; ++col, ++pos) {
                            int p = base_p + ((col < width) ? col : (width - 1)) * comp;
                            float r = dataR[p], g = dataG[p], b = dataB[p];
                            Y[pos] = +0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
                            U[pos] = -0.16874f*r - 0.33126f*g + 0.50000f*b;
                            V[pos] = +0.50000f*r - 0.41869f*g - 0.08131f*b;
                        }
                    }
                    DCY = jpg_processDU(s, &bitBuf, &bitCnt, Y+0,   16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
                    DCY = jpg_processDU(s, &bitBuf, &bitCnt, Y+8,   16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
                    DCY = jpg_processDU(s, &bitBuf, &bitCnt, Y+128, 16, fdtbl_Y, DCY, YDC_HT, YAC_HT);
                    DCY = jpg_processDU(s, &bitBuf, &bitCnt, Y+136, 16, fdtbl_Y, DCY, YDC_HT, YAC_HT);

                    {
                        float subU[64], subV[64];
                        for (int yy = 0, spos = 0; yy < 8; ++yy) {
                            for (int xx = 0; xx < 8; ++xx, ++spos) {
                                int j = yy * 32 + xx * 2;
                                subU[spos] = (U[j+0] + U[j+1] + U[j+16] + U[j+17]) * 0.25f;
                                subV[spos] = (V[j+0] + V[j+1] + V[j+16] + V[j+17]) * 0.25f;
                            }
                        }
                        DCU = jpg_processDU(s, &bitBuf, &bitCnt, subU, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
                        DCV = jpg_processDU(s, &bitBuf, &bitCnt, subV, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
                    }
                }
            }
        } else {
            for (y = 0; y < height; y += 8) {
                for (x = 0; x < width; x += 8) {
                    float Y[64], U[64], V[64];
                    for (row = y, pos = 0; row < y + 8; ++row) {
                        int clamped_row = (row < height) ? row : height - 1;
                        int base_p = (opts.flip_vertically ? (height - 1 - clamped_row) : clamped_row) * width * comp;
                        for (col = x; col < x + 8; ++col, ++pos) {
                            int p = base_p + ((col < width) ? col : (width - 1)) * comp;
                            float r = dataR[p], g = dataG[p], b = dataB[p];
                            Y[pos] = +0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
                            U[pos] = -0.16874f*r - 0.33126f*g + 0.50000f*b;
                            V[pos] = +0.50000f*r - 0.41869f*g - 0.08131f*b;
                        }
                    }
                    DCY = jpg_processDU(s, &bitBuf, &bitCnt, Y, 8, fdtbl_Y,  DCY, YDC_HT, YAC_HT);
                    DCU = jpg_processDU(s, &bitBuf, &bitCnt, U, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
                    DCV = jpg_processDU(s, &bitBuf, &bitCnt, V, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
                }
            }
        }

        jpg_writeBits(s, &bitBuf, &bitCnt, fillBits);
    }

    // EOI
    s.flush();
    s.put(0xFF);
    s.put(0xD9);
    return 1;
}

} // namespace detail

// Adapter: bridge public WriteFunc (function_ref with span) to internal callback

static void write_func_adapter(void* ctx, const void* data, int size) {
    auto* fn = static_cast<WriteFunc*>(ctx);
    (*fn)({static_cast<const uint8_t*>(data), static_cast<size_t>(size)});
}

// Public API: file writers

#ifndef KOSMVG_STBI_NO_STDIO

bool write_png(const char* filename, int w, int h, int comp,
               const void* data, const WriteOptions& opts)
{
    int len;
    auto* png = detail::write_png_to_mem_impl(
        static_cast<const uint8_t*>(data), opts.stride_bytes, w, h, comp, &len, opts);
    if (!png) return false;
    FILE* f = detail::fopen_wb(filename);
    if (!f) { STBI_FREE(png); return false; }
    std::fwrite(png, 1, static_cast<size_t>(len), f);
    std::fclose(f);
    STBI_FREE(png);
    return true;
}

bool write_bmp(const char* filename, int w, int h, int comp, const void* data,
               const WriteOptions& opts)
{
    FILE* f = detail::fopen_wb(filename);
    if (!f) return false;
    auto s = detail::make_file_context(f);
    bool r = detail::write_bmp_core(s, w, h, comp, data, opts) != 0;
    std::fclose(f);
    return r;
}

bool write_tga(const char* filename, int w, int h, int comp, const void* data,
               const WriteOptions& opts)
{
    FILE* f = detail::fopen_wb(filename);
    if (!f) return false;
    auto s = detail::make_file_context(f);
    bool r = detail::write_tga_core(s, w, h, comp, const_cast<void*>(data), opts) != 0;
    std::fclose(f);
    return r;
}

bool write_hdr(const char* filename, int w, int h, int comp, const float* data,
               const WriteOptions& opts)
{
    FILE* f = detail::fopen_wb(filename);
    if (!f) return false;
    auto s = detail::make_file_context(f);
    bool r = detail::write_hdr_core(s, w, h, comp, const_cast<float*>(data), opts) != 0;
    std::fclose(f);
    return r;
}

bool write_jpg(const char* filename, int w, int h, int comp, const void* data,
               const WriteOptions& opts)
{
    FILE* f = detail::fopen_wb(filename);
    if (!f) return false;
    auto s = detail::make_file_context(f);
    bool r = detail::write_jpg_core(s, w, h, comp, data, opts) != 0;
    std::fclose(f);
    return r;
}

bool write_hdr(const char* filename, const Image<float>& img, const WriteOptions& opts)
{
    return img ? write_hdr(filename, img.width(), img.height(), img.channels(),
                           img.data(), opts) : false;
}

#endif // KOSMVG_STBI_NO_STDIO

// Public API: callback writers

bool write_png_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts)
{
    int len;
    auto* png = detail::write_png_to_mem_impl(
        static_cast<const uint8_t*>(data), opts.stride_bytes, w, h, comp, &len, opts);
    if (!png) return false;
    func({png, static_cast<size_t>(len)});
    STBI_FREE(png);
    return true;
}

bool write_bmp_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts)
{
    auto s = detail::make_callback_context(write_func_adapter, &func);
    return detail::write_bmp_core(s, w, h, comp, data, opts) != 0;
}

bool write_tga_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts)
{
    auto s = detail::make_callback_context(write_func_adapter, &func);
    return detail::write_tga_core(s, w, h, comp, const_cast<void*>(data), opts) != 0;
}

bool write_hdr_to_func(WriteFunc func, int w, int h, int comp,
                       const float* data, const WriteOptions& opts)
{
    auto s = detail::make_callback_context(write_func_adapter, &func);
    return detail::write_hdr_core(s, w, h, comp, const_cast<float*>(data), opts) != 0;
}

bool write_jpg_to_func(WriteFunc func, int w, int h, int comp,
                       const void* data, const WriteOptions& opts)
{
    auto s = detail::make_callback_context(write_func_adapter, &func);
    return detail::write_jpg_core(s, w, h, comp, data, opts) != 0;
}

// Public API: in-memory encoding

EncodeResult encode_png(int w, int h, int comp, const void* data,
                        const WriteOptions& opts)
{
    int len = 0;
    auto* png = detail::write_png_to_mem_impl(
        static_cast<const uint8_t*>(data), opts.stride_bytes, w, h, comp, &len, opts);
    EncodeResult result;
    result.data.reset(png);
    result.length = len;
    return result;
}

struct MemCollector {
    uint8_t* data = nullptr;
    int count = 0;
    int capacity = 0;
};

static void mem_collector_write(void* ctx, const void* src, int size) {
    auto* mc = static_cast<MemCollector*>(ctx);
    if (mc->count + size > mc->capacity) {
        int newcap = mc->capacity ? mc->capacity * 2 : 4096;
        while (newcap < mc->count + size) newcap *= 2;
        auto* p = static_cast<uint8_t*>(STBI_REALLOC_SIZED(
            mc->data, static_cast<size_t>(mc->capacity), static_cast<size_t>(newcap)));
        if (!p) return;
        mc->data = p;
        mc->capacity = newcap;
    }
    std::memcpy(mc->data + mc->count, src, static_cast<size_t>(size));
    mc->count += size;
}

EncodeResult encode_jpg(int w, int h, int comp, const void* data,
                        const WriteOptions& opts)
{
    MemCollector mc{};
    auto s = detail::make_callback_context(mem_collector_write, &mc);
    if (!detail::write_jpg_core(s, w, h, comp, data, opts)) {
        if (mc.data) STBI_FREE(mc.data);
        return {};
    }
    EncodeResult result;
    result.data.reset(mc.data);
    result.length = mc.count;
    return result;
}

// Public API: zlib compression

namespace zlib {

CompressResult compress(std::span<const uint8_t> input, int quality)
{
    int len = 0;
    auto* compressed = detail::zlib_compress_impl(
        const_cast<uint8_t*>(input.data()),
        static_cast<int>(input.size()), &len, quality);
    CompressResult result;
    result.data.reset(reinterpret_cast<char*>(compressed));
    result.length = len;
    return result;
}

} // namespace zlib
} // namespace kosmvg::stbi
