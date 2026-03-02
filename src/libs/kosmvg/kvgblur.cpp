// kvgblur.cpp — Gaussian blur and memory fill
// C++20

#include "kvgprivate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace kvg {

void memfill32(uint32_t* dest, int length, uint32_t value) {
    if (length <= 0) return;
    if (length <= 16) {
        for (int i = 0; i < length; ++i)
            dest[i] = value;
        return;
    }
    if (value == 0) {
        std::memset(dest, 0, static_cast<size_t>(length) * 4);
        return;
    }
    // General case: unrolled pair writes.
    // No aliasing/alignment concern — both stores go through uint32_t*.
    int pairs = length / 2;
    for (int i = 0; i < pairs; ++i) {
        dest[i * 2]     = value;
        dest[i * 2 + 1] = value;
    }
    if (length & 1)
        dest[length - 1] = value;
}

// -- Gaussian blur via 3-pass box blur (Boxy approximation) --
//
// Three box-blur passes with carefully chosen radii approximate a
// Gaussian kernel to within ~3% error. This is O(n) per pass
// regardless of radius (sliding window accumulator).

namespace {

// Compute three box radii that approximate a Gaussian with given sigma.
// See: http://blog.ivank.net/fastest-gaussian-blur.html
void box_radii_for_gaussian(float sigma, int radii[3]) {
    float w_ideal = std::sqrt(12.0f * sigma * sigma / 3.0f + 1.0f);
    int wl = static_cast<int>(std::floor(w_ideal));
    if ((wl & 1) == 0) --wl; // must be odd
    int wu = wl + 2;

    float m_ideal = (12.0f * sigma * sigma
                     - 3.0f * wl * wl - 12.0f * wl - 9.0f)
                    / (-4.0f * wl - 4.0f);
    int m = static_cast<int>(std::round(m_ideal));

    for (int i = 0; i < 3; ++i)
        radii[i] = ((i < m) ? wl : wu) / 2;
}

// Horizontal box blur pass on premultiplied ARGB32 scanlines.
// Supports in-place operation (src == dst) via per-row temp buffer.
void box_blur_h(const uint32_t* src, uint32_t* dst,
                int width, int height, int stride_bytes, int radius) {
    if (radius <= 0) {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(height) * stride_bytes);
        return;
    }
    int diam = 2 * radius + 1;
    int stride32 = stride_bytes / 4;

    // When src == dst the sliding window would read already-written
    // pixels on the left edge.  Snapshot each row before overwriting.
    std::vector<uint32_t> row_buf;
    if (src == dst)
        row_buf.resize(static_cast<size_t>(width));

    for (int y = 0; y < height; ++y) {
        const uint32_t* row_src;
        if (src == dst) {
            std::memcpy(row_buf.data(), src + y * stride32,
                        static_cast<size_t>(width) * 4);
            row_src = row_buf.data();
        } else {
            row_src = src + y * stride32;
        }
        uint32_t* row_dst = dst + y * stride32;

        uint32_t acc_a = 0, acc_r = 0, acc_g = 0, acc_b = 0;

        // Seed accumulator: [-radius, radius]
        for (int x = -radius; x <= radius; ++x) {
            int ix = std::clamp(x, 0, width - 1);
            uint32_t px = row_src[ix];
            acc_a += pixel_alpha(px);
            acc_r += red(px);
            acc_g += green(px);
            acc_b += blue(px);
        }

        for (int x = 0; x < width; ++x) {
            row_dst[x] = pack_argb(
                static_cast<uint8_t>(acc_a / diam),
                static_cast<uint8_t>(acc_r / diam),
                static_cast<uint8_t>(acc_g / diam),
                static_cast<uint8_t>(acc_b / diam));

            // Slide window: add right edge, remove left edge
            int add_x = std::min(x + radius + 1, width - 1);
            int rem_x = std::max(x - radius, 0);
            uint32_t add_px = row_src[add_x];
            uint32_t rem_px = row_src[rem_x];
            acc_a += pixel_alpha(add_px) - pixel_alpha(rem_px);
            acc_r += red(add_px)   - red(rem_px);
            acc_g += green(add_px) - green(rem_px);
            acc_b += blue(add_px)  - blue(rem_px);
        }
    }
}

// Vertical box blur pass on premultiplied ARGB32 columns.
// Supports in-place operation (src == dst) via per-column temp buffer.
void box_blur_v(const uint32_t* src, uint32_t* dst,
                int width, int height, int stride_bytes, int radius) {
    if (radius <= 0) {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(height) * stride_bytes);
        return;
    }
    int diam = 2 * radius + 1;
    int stride32 = stride_bytes / 4;

    // When src == dst the sliding window would read already-written
    // pixels above.  Snapshot each column before overwriting.
    std::vector<uint32_t> col_buf;
    if (src == dst)
        col_buf.resize(static_cast<size_t>(height));

    for (int x = 0; x < width; ++x) {
        const uint32_t* col_src;
        int col_stride;

        if (src == dst) {
            for (int y = 0; y < height; ++y)
                col_buf[static_cast<size_t>(y)] = src[y * stride32 + x];
            col_src = col_buf.data();
            col_stride = 1;
        } else {
            col_src = src + x;
            col_stride = stride32;
        }

        uint32_t acc_a = 0, acc_r = 0, acc_g = 0, acc_b = 0;

        for (int y = -radius; y <= radius; ++y) {
            int iy = std::clamp(y, 0, height - 1);
            uint32_t px = col_src[iy * col_stride];
            acc_a += pixel_alpha(px);
            acc_r += red(px);
            acc_g += green(px);
            acc_b += blue(px);
        }

        for (int y = 0; y < height; ++y) {
            dst[y * stride32 + x] = pack_argb(
                static_cast<uint8_t>(acc_a / diam),
                static_cast<uint8_t>(acc_r / diam),
                static_cast<uint8_t>(acc_g / diam),
                static_cast<uint8_t>(acc_b / diam));

            int add_y = std::min(y + radius + 1, height - 1);
            int rem_y = std::max(y - radius, 0);
            uint32_t add_px = col_src[add_y * col_stride];
            uint32_t rem_px = col_src[rem_y * col_stride];
            acc_a += pixel_alpha(add_px) - pixel_alpha(rem_px);
            acc_r += red(add_px)   - red(rem_px);
            acc_g += green(add_px) - green(rem_px);
            acc_b += blue(add_px)  - blue(rem_px);
        }
    }
}

// -- Alpha-only (single-channel) box blur for shadow optimization --
// ~4x faster than ARGB blur since only 1 byte per pixel.

void box_blur_h_alpha(const unsigned char* src, unsigned char* dst,
                      int width, int height, int stride, int radius) {
    if (radius <= 0) {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(height) * stride);
        return;
    }
    int diam = 2 * radius + 1;

    std::vector<unsigned char> row_buf;
    if (src == dst)
        row_buf.resize(static_cast<size_t>(width));

    for (int y = 0; y < height; ++y) {
        const unsigned char* row_src;
        if (src == dst) {
            std::memcpy(row_buf.data(), src + y * stride, static_cast<size_t>(width));
            row_src = row_buf.data();
        } else {
            row_src = src + y * stride;
        }
        unsigned char* row_dst = dst + y * stride;

        uint32_t acc = 0;
        for (int x = -radius; x <= radius; ++x)
            acc += row_src[std::clamp(x, 0, width - 1)];

        for (int x = 0; x < width; ++x) {
            row_dst[x] = static_cast<unsigned char>(acc / diam);
            acc += row_src[std::min(x + radius + 1, width - 1)]
                 - row_src[std::max(x - radius, 0)];
        }
    }
}

void box_blur_v_alpha(const unsigned char* src, unsigned char* dst,
                      int width, int height, int stride, int radius) {
    if (radius <= 0) {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(height) * stride);
        return;
    }
    int diam = 2 * radius + 1;

    // XY-flip optimization: transpose 8x8 blocks before vertical pass
    // to make column access cache-friendly (sequential memory access).
    constexpr int kBlock = 8;
    std::vector<unsigned char> transposed(static_cast<size_t>(width) * height);
    int t_stride = height; // transposed: width and height swapped

    // Transpose src into transposed (height x width → width x height)
    for (int by = 0; by < height; by += kBlock) {
        int bh = std::min(kBlock, height - by);
        for (int bx = 0; bx < width; bx += kBlock) {
            int bw = std::min(kBlock, width - bx);
            for (int dy = 0; dy < bh; ++dy)
                for (int dx = 0; dx < bw; ++dx)
                    transposed[(bx + dx) * t_stride + (by + dy)]
                        = src[(by + dy) * stride + (bx + dx)];
        }
    }

    // Now blur "horizontally" on the transposed buffer (= vertical in original space)
    std::vector<unsigned char> t_dst(transposed.size());
    std::vector<unsigned char> row_buf(static_cast<size_t>(height));

    for (int col = 0; col < width; ++col) {
        const unsigned char* row_src = transposed.data() + col * t_stride;
        unsigned char* row_dst_p = t_dst.data() + col * t_stride;

        uint32_t acc = 0;
        for (int y = -radius; y <= radius; ++y)
            acc += row_src[std::clamp(y, 0, height - 1)];

        for (int y = 0; y < height; ++y) {
            row_dst_p[y] = static_cast<unsigned char>(acc / diam);
            acc += row_src[std::min(y + radius + 1, height - 1)]
                 - row_src[std::max(y - radius, 0)];
        }
    }

    // Transpose back
    for (int by = 0; by < height; by += kBlock) {
        int bh = std::min(kBlock, height - by);
        for (int bx = 0; bx < width; bx += kBlock) {
            int bw = std::min(kBlock, width - bx);
            for (int dy = 0; dy < bh; ++dy)
                for (int dx = 0; dx < bw; ++dx)
                    dst[(by + dy) * stride + (bx + dx)]
                        = t_dst[(bx + dx) * t_stride + (by + dy)];
        }
    }
}

// XY-flip optimization for ARGB vertical blur pass.
// Transposes in 8x8 blocks for cache-coherent column access.
void box_blur_v_flip(const uint32_t* src, uint32_t* dst,
                     int width, int height, int stride_bytes, int radius) {
    if (radius <= 0) {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(height) * stride_bytes);
        return;
    }
    int diam = 2 * radius + 1;
    int stride32 = stride_bytes / 4;

    constexpr int kBlock = 8;
    int t_stride = height; // transposed dimensions
    std::vector<uint32_t> transposed(static_cast<size_t>(width) * height);

    // Transpose src into transposed buffer (8x8 blocks)
    for (int by = 0; by < height; by += kBlock) {
        int bh = std::min(kBlock, height - by);
        for (int bx = 0; bx < width; bx += kBlock) {
            int bw = std::min(kBlock, width - bx);
            for (int dy = 0; dy < bh; ++dy)
                for (int dx = 0; dx < bw; ++dx)
                    transposed[(bx + dx) * t_stride + (by + dy)]
                        = src[(by + dy) * stride32 + (bx + dx)];
        }
    }

    // Blur "horizontally" on transposed data (= vertical in original space)
    std::vector<uint32_t> t_dst(transposed.size());

    for (int col = 0; col < width; ++col) {
        const uint32_t* row_src = transposed.data() + col * t_stride;
        uint32_t* row_dst_p = t_dst.data() + col * t_stride;

        uint32_t acc_a = 0, acc_r = 0, acc_g = 0, acc_b = 0;
        for (int y = -radius; y <= radius; ++y) {
            int iy = std::clamp(y, 0, height - 1);
            uint32_t px = row_src[iy];
            acc_a += pixel_alpha(px);
            acc_r += red(px);
            acc_g += green(px);
            acc_b += blue(px);
        }

        for (int y = 0; y < height; ++y) {
            row_dst_p[y] = pack_argb(
                static_cast<uint8_t>(acc_a / diam),
                static_cast<uint8_t>(acc_r / diam),
                static_cast<uint8_t>(acc_g / diam),
                static_cast<uint8_t>(acc_b / diam));

            int add_y = std::min(y + radius + 1, height - 1);
            int rem_y = std::max(y - radius, 0);
            uint32_t add_px = row_src[add_y];
            uint32_t rem_px = row_src[rem_y];
            acc_a += pixel_alpha(add_px) - pixel_alpha(rem_px);
            acc_r += red(add_px)   - red(rem_px);
            acc_g += green(add_px) - green(rem_px);
            acc_b += blue(add_px)  - blue(rem_px);
        }
    }

    // Transpose back to dst
    for (int by = 0; by < height; by += kBlock) {
        int bh = std::min(kBlock, height - by);
        for (int bx = 0; bx < width; bx += kBlock) {
            int bw = std::min(kBlock, width - bx);
            for (int dy = 0; dy < bh; ++dy)
                for (int dx = 0; dx < bw; ++dx)
                    dst[(by + dy) * stride32 + (bx + dx)]
                        = t_dst[(bx + dx) * t_stride + (by + dy)];
        }
    }
}

} // anonymous namespace

void gaussian_blur_alpha(unsigned char* data, int width, int height,
                         int stride, float radius,
                         std::vector<unsigned char>* tmp_buf) {
    if (width <= 0 || height <= 0 || radius <= 0.0f)
        return;

    float sigma = radius * 0.5f;
    if (sigma < 0.5f) sigma = 0.5f;

    int radii[3];
    box_radii_for_gaussian(sigma, radii);

    size_t buf_size = static_cast<size_t>(height) * stride;
    std::vector<unsigned char> local_tmp;
    std::vector<unsigned char>& tmp = tmp_buf ? *tmp_buf : local_tmp;
    tmp.resize(buf_size);

    // 3 passes: H→V with XY-flip for vertical
    box_blur_h_alpha(data, tmp.data(), width, height, stride, radii[0]);
    box_blur_v_alpha(tmp.data(), data, width, height, stride, radii[0]);
    box_blur_h_alpha(data, tmp.data(), width, height, stride, radii[1]);
    box_blur_v_alpha(tmp.data(), data, width, height, stride, radii[1]);
    box_blur_h_alpha(data, tmp.data(), width, height, stride, radii[2]);
    box_blur_v_alpha(tmp.data(), data, width, height, stride, radii[2]);
}

void gaussian_blur(unsigned char* data, int width, int height,
                   int stride, float radius,
                   std::vector<unsigned char>* tmp_buf) {
    if (width <= 0 || height <= 0 || radius <= 0.0f)
        return;

    // Convert CSS blur radius to Gaussian sigma.
    // CSS spec: radius = 2 * sigma (approximately).
    float sigma = radius * 0.5f;
    if (sigma < 0.5f) sigma = 0.5f;

    int radii[3];
    box_radii_for_gaussian(sigma, radii);

    // Temporary buffer for ping-pong.
    // Reuse caller-provided buffer when available to avoid per-call allocation.
    size_t buf_size = static_cast<size_t>(height) * stride;
    std::vector<unsigned char> local_tmp;
    std::vector<unsigned char>& tmp = tmp_buf ? *tmp_buf : local_tmp;
    tmp.resize(buf_size);

    auto* src = reinterpret_cast<uint32_t*>(data);
    auto* dst = reinterpret_cast<uint32_t*>(tmp.data());

    // 3 passes: H→V with XY-flip for cache-friendly vertical access
    // Pass 1
    box_blur_h(src, dst, width, height, stride, radii[0]);
    box_blur_v_flip(dst, src, width, height, stride, radii[0]);
    // Pass 2
    box_blur_h(src, dst, width, height, stride, radii[1]);
    box_blur_v_flip(dst, src, width, height, stride, radii[1]);
    // Pass 3
    box_blur_h(src, dst, width, height, stride, radii[2]);
    box_blur_v_flip(dst, src, width, height, stride, radii[2]);

    // Result is back in src (= data), done.
}

} // namespace kvg
