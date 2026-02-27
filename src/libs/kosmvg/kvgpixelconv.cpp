// kvgpixelconv.cpp — Pixel format conversion
// C++20

#include "kvgprivate.hpp"

#include <cstring>

namespace kvg {

// Byte layout reminder:
//   ARGB32_Premultiplied: uint32 = (A<<24)|(R<<16)|(G<<8)|B
//                         On LE memory: bytes are [B, G, R, A]
//   BGRA32_Premultiplied: bytes in memory are [B, G, R, A] — same as above on LE.
//                         Reading as uint32 on LE gives (A<<24)|(R<<16)|(G<<8)|B,
//                         which is identical to ARGB32_Premultiplied. No swizzle needed.
//   RGBA32:               uint32 = (R<<24)|(G<<16)|(B<<8)|A  (straight alpha)
//   BGRA32:               uint32 = (B<<24)|(G<<16)|(R<<8)|A  (straight alpha)
//   A8:                   single byte

uint32_t pixel_to_argb_premul(uint32_t pixel, PixelFormat src_fmt) {
    switch (src_fmt) {
    case PixelFormat::ARGB32_Premultiplied:
        return pixel;

    case PixelFormat::BGRA32_Premultiplied: {
        // On little-endian: bytes in memory are [B, G, R, A].
        // When read as uint32 on LE: A occupies bits 31-24, R 23-16, G 15-8, B 7-0.
        // That is exactly the same layout as ARGB32_Premultiplied — no swizzle needed.
        return pixel;
    }

    case PixelFormat::RGBA32: {
        // (R<<24)|(G<<16)|(B<<8)|A — straight alpha, needs premultiply
        uint8_t r = static_cast<uint8_t>((pixel >> 24) & 0xFF);
        uint8_t g = static_cast<uint8_t>((pixel >> 16) & 0xFF);
        uint8_t b = static_cast<uint8_t>((pixel >> 8) & 0xFF);
        uint8_t a = static_cast<uint8_t>(pixel & 0xFF);
        return premultiply_argb(pack_argb(a, r, g, b));
    }

    case PixelFormat::BGRA32: {
        // (B<<24)|(G<<16)|(R<<8)|A — straight alpha, needs premultiply
        uint8_t b = static_cast<uint8_t>((pixel >> 24) & 0xFF);
        uint8_t g = static_cast<uint8_t>((pixel >> 16) & 0xFF);
        uint8_t r = static_cast<uint8_t>((pixel >> 8) & 0xFF);
        uint8_t a = static_cast<uint8_t>(pixel & 0xFF);
        return premultiply_argb(pack_argb(a, r, g, b));
    }

    case PixelFormat::A8:
        // Alpha-only → premul black with that alpha
        return pack_argb(static_cast<uint8_t>(pixel & 0xFF), 0, 0, 0);
    }
    return pixel;
}

uint32_t pixel_from_argb_premul(uint32_t argb_premul, PixelFormat dst_fmt) {
    switch (dst_fmt) {
    case PixelFormat::ARGB32_Premultiplied:
        return argb_premul;

    case PixelFormat::BGRA32_Premultiplied: {
        // On little-endian: ARGB32 uint32 and BGRA32 uint32 have the same
        // bit layout ([A R G B] from bit31 to bit0 = [B G R A] in memory).
        return argb_premul;
    }

    case PixelFormat::RGBA32: {
        // Unpremultiply, then (R<<24)|(G<<16)|(B<<8)|A
        uint8_t a = pixel_alpha(argb_premul);
        auto [r, g, b] = unpremultiply(argb_premul);
        return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | a;
    }

    case PixelFormat::BGRA32: {
        // Unpremultiply, then (B<<24)|(G<<16)|(R<<8)|A
        uint8_t a = pixel_alpha(argb_premul);
        auto [r, g, b] = unpremultiply(argb_premul);
        return (uint32_t(b) << 24) | (uint32_t(g) << 16) | (uint32_t(r) << 8) | a;
    }

    case PixelFormat::A8:
        return pixel_alpha(argb_premul);
    }
    return argb_premul;
}

void convert_scanline(const unsigned char* src, PixelFormat src_fmt,
                      unsigned char* dst, PixelFormat dst_fmt,
                      int width, uint32_t rgb_fill) {
    if (width <= 0) return;

    if (src_fmt == dst_fmt) {
        int bpp = pixel_format_info(src_fmt).bpp;
        std::memcpy(dst, src, static_cast<size_t>(width) * bpp);
        return;
    }

    // A8 → color: expand using rgb_fill
    if (src_fmt == PixelFormat::A8 && dst_fmt != PixelFormat::A8) {
        uint8_t fr = red(rgb_fill);
        uint8_t fg = green(rgb_fill);
        uint8_t fb = blue(rgb_fill);
        auto* dst32 = reinterpret_cast<uint32_t*>(dst);
        for (int i = 0; i < width; ++i) {
            uint8_t a = src[i];
            uint32_t argb_premul = premultiply_argb(pack_argb(a, fr, fg, fb));
            dst32[i] = pixel_from_argb_premul(argb_premul, dst_fmt);
        }
        return;
    }

    // Color → A8: extract alpha
    if (dst_fmt == PixelFormat::A8) {
        auto* src32 = reinterpret_cast<const uint32_t*>(src);
        for (int i = 0; i < width; ++i) {
            uint32_t argb = pixel_to_argb_premul(src32[i], src_fmt);
            dst[i] = pixel_alpha(argb);
        }
        return;
    }

    // Color → color: go through ARGB premul
    auto* src32 = reinterpret_cast<const uint32_t*>(src);
    auto* dst32 = reinterpret_cast<uint32_t*>(dst);
    for (int i = 0; i < width; ++i)
        dst32[i] = pixel_from_argb_premul(pixel_to_argb_premul(src32[i], src_fmt), dst_fmt);
}

// -- Public bulk conversion --

void convert_argb_to_rgba(unsigned char* dst, const unsigned char* src,
                          int width, int height, int stride) {
    if (width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        auto* s = reinterpret_cast<const uint32_t*>(src + y * stride);
        auto* d = reinterpret_cast<uint32_t*>(dst + y * stride);
        for (int x = 0; x < width; ++x) {
            uint32_t px = s[x];
            // ARGB premul → RGBA straight
            uint8_t a = pixel_alpha(px);
            auto [r, g, b] = unpremultiply(px);
            d[x] = (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | a;
        }
    }
}

void convert_rgba_to_argb(unsigned char* dst, const unsigned char* src,
                          int width, int height, int stride) {
    if (width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        auto* s = reinterpret_cast<const uint32_t*>(src + y * stride);
        auto* d = reinterpret_cast<uint32_t*>(dst + y * stride);
        for (int x = 0; x < width; ++x) {
            uint32_t px = s[x];
            // RGBA straight → ARGB premul
            uint8_t r = static_cast<uint8_t>((px >> 24) & 0xFF);
            uint8_t g = static_cast<uint8_t>((px >> 16) & 0xFF);
            uint8_t b = static_cast<uint8_t>((px >> 8) & 0xFF);
            uint8_t a = static_cast<uint8_t>(px & 0xFF);
            d[x] = premultiply_argb(pack_argb(a, r, g, b));
        }
    }
}

} // namespace kvg
