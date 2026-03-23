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

// -- Block transpose helper --
// Transposes a 2D buffer in 8x8 cache-friendly blocks.
// src_stride / dst_stride are in element count (not bytes).

constexpr int kTransposeBlock = 8;

template<typename T>
void transpose_block(const T* src, int src_stride, T* dst, int dst_stride,
                     int width, int height) {
    for (int by = 0; by < height; by += kTransposeBlock) {
        int bh = std::min(kTransposeBlock, height - by);
        for (int bx = 0; bx < width; bx += kTransposeBlock) {
            int bw = std::min(kTransposeBlock, width - bx);
            for (int dy = 0; dy < bh; ++dy)
                for (int dx = 0; dx < bw; ++dx)
                    dst[(bx + dx) * dst_stride + (by + dy)]
                        = src[(by + dy) * src_stride + (bx + dx)];
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
                      int width, int height, int stride, int radius,
                      unsigned char* scratch_a = nullptr,
                      unsigned char* scratch_b = nullptr) {
    if (radius <= 0) {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(height) * stride);
        return;
    }
    int diam = 2 * radius + 1;
    int t_stride = height;
    size_t t_size = static_cast<size_t>(width) * height;

    // Use caller-provided scratch or allocate locally.
    std::vector<unsigned char> local_a, local_b;
    if (!scratch_a) { local_a.resize(t_size); scratch_a = local_a.data(); }
    if (!scratch_b) { local_b.resize(t_size); scratch_b = local_b.data(); }

    transpose_block(src, stride, scratch_a, t_stride, width, height);

    for (int col = 0; col < width; ++col) {
        const unsigned char* row_src = scratch_a + col * t_stride;
        unsigned char* row_dst_p = scratch_b + col * t_stride;

        uint32_t acc = 0;
        for (int y = -radius; y <= radius; ++y)
            acc += row_src[std::clamp(y, 0, height - 1)];

        for (int y = 0; y < height; ++y) {
            row_dst_p[y] = static_cast<unsigned char>(acc / diam);
            acc += row_src[std::min(y + radius + 1, height - 1)]
                 - row_src[std::max(y - radius, 0)];
        }
    }

    transpose_block(scratch_b, t_stride, dst, stride, height, width);
}

// XY-flip optimization for ARGB vertical blur pass.
void box_blur_v_flip(const uint32_t* src, uint32_t* dst,
                     int width, int height, int stride_bytes, int radius,
                     uint32_t* scratch_a = nullptr,
                     uint32_t* scratch_b = nullptr) {
    if (radius <= 0) {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(height) * stride_bytes);
        return;
    }
    int diam = 2 * radius + 1;
    int stride32 = stride_bytes / 4;
    int t_stride = height;
    size_t t_count = static_cast<size_t>(width) * height;

    std::vector<uint32_t> local_a, local_b;
    if (!scratch_a) { local_a.resize(t_count); scratch_a = local_a.data(); }
    if (!scratch_b) { local_b.resize(t_count); scratch_b = local_b.data(); }

    transpose_block(src, stride32, scratch_a, t_stride, width, height);

    for (int col = 0; col < width; ++col) {
        const uint32_t* row_src = scratch_a + col * t_stride;
        uint32_t* row_dst_p = scratch_b + col * t_stride;

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

    transpose_block(scratch_b, t_stride, dst, stride32, height, width);
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
    // Pre-allocate transpose scratch once for all 3 vertical passes.
    size_t t_size = static_cast<size_t>(width) * height;
    std::vector<unsigned char> scratch_a(t_size), scratch_b(t_size);

    box_blur_h_alpha(data, tmp.data(), width, height, stride, radii[0]);
    box_blur_v_alpha(tmp.data(), data, width, height, stride, radii[0], scratch_a.data(), scratch_b.data());
    box_blur_h_alpha(data, tmp.data(), width, height, stride, radii[1]);
    box_blur_v_alpha(tmp.data(), data, width, height, stride, radii[1], scratch_a.data(), scratch_b.data());
    box_blur_h_alpha(data, tmp.data(), width, height, stride, radii[2]);
    box_blur_v_alpha(tmp.data(), data, width, height, stride, radii[2], scratch_a.data(), scratch_b.data());
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
    // Pre-allocate transpose scratch once for all 3 vertical passes.
    size_t t_count = static_cast<size_t>(width) * height;
    std::vector<uint32_t> scratch_a(t_count), scratch_b(t_count);

    // Pass 1
    box_blur_h(src, dst, width, height, stride, radii[0]);
    box_blur_v_flip(dst, src, width, height, stride, radii[0], scratch_a.data(), scratch_b.data());
    // Pass 2
    box_blur_h(src, dst, width, height, stride, radii[1]);
    box_blur_v_flip(dst, src, width, height, stride, radii[1], scratch_a.data(), scratch_b.data());
    // Pass 3
    box_blur_h(src, dst, width, height, stride, radii[2]);
    box_blur_v_flip(dst, src, width, height, stride, radii[2], scratch_a.data(), scratch_b.data());

    // Result is back in src (= data), done.
}

} // namespace kvg
