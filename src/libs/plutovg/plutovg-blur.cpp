// plutovg-blur.cpp - Gaussian blur via box blur approximation
// C++20

#include "plutovg-private.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace plutovg {

// Compute box sizes for 3-pass approximation of gaussian blur.
// Based on: "Fastest Gaussian Blur (in linear time)" by Ivan Kutskir.
// Three box-blur passes with carefully chosen radii approximate a Gaussian.
void box_sizes_for_gaussian(float sigma, int sizes[3]) {
    float ideal = std::sqrt(12.0f * sigma * sigma / 3.0f + 1.0f);
    int wl = static_cast<int>(std::floor(ideal));
    if (wl % 2 == 0)
        --wl;
    int wu = wl + 2;

    float m_ideal = (12.0f * sigma * sigma
                     - 3.0f * static_cast<float>(wl * wl)
                     - 12.0f * static_cast<float>(wl) - 9.0f)
                    / (-4.0f * static_cast<float>(wl) - 4.0f);
    int m = static_cast<int>(std::round(m_ideal));

    for (int i = 0; i < 3; ++i)
        sizes[i] = (i < m) ? wl : wu;
}

// Horizontal box blur pass. Operates on a single channel stored as floats.
void box_blur_h(const float* src, float* dst, int w, int h, int r) {
    float inv = 1.0f / static_cast<float>(r + r + 1);
    for (int y = 0; y < h; ++y) {
        const float* row_in = src + y * w;
        float* row_out = dst + y * w;

        float acc = static_cast<float>(r + 1) * row_in[0];
        for (int x = 0; x < r; ++x)
            acc += row_in[x];
        for (int x = 0; x <= r; ++x) {
            acc += row_in[std::min(x + r, w - 1)] - row_in[0];
            row_out[x] = acc * inv;
        }
        for (int x = r + 1; x < w - r; ++x) {
            acc += row_in[x + r] - row_in[x - r - 1];
            row_out[x] = acc * inv;
        }
        for (int x = std::max(w - r, r + 1); x < w; ++x) {
            acc += row_in[w - 1] - row_in[x - r - 1];
            row_out[x] = acc * inv;
        }
    }
}

// Vertical box blur pass (transpose-style accumulation).
void box_blur_v(const float* src, float* dst, int w, int h, int r) {
    float inv = 1.0f / static_cast<float>(r + r + 1);
    for (int x = 0; x < w; ++x) {
        float acc = static_cast<float>(r + 1) * src[x];
        for (int y = 0; y < r; ++y)
            acc += src[y * w + x];
        for (int y = 0; y <= r; ++y) {
            acc += src[std::min(y + r, h - 1) * w + x] - src[x];
            dst[y * w + x] = acc * inv;
        }
        for (int y = r + 1; y < h - r; ++y) {
            acc += src[(y + r) * w + x] - src[(y - r - 1) * w + x];
            dst[y * w + x] = acc * inv;
        }
        for (int y = std::max(h - r, r + 1); y < h; ++y) {
            acc += src[(h - 1) * w + x] - src[(y - r - 1) * w + x];
            dst[y * w + x] = acc * inv;
        }
    }
}

// Single box-blur pass (horizontal + vertical).
void box_blur(float* channel, float* temp, int w, int h, int r) {
    if (r <= 0)
        return;
    box_blur_h(channel, temp, w, h, r);
    box_blur_v(temp, channel, w, h, r);
}

void gaussian_blur(unsigned char* data, int width, int height, int stride, float radius) {
    if (width <= 0 || height <= 0 || radius <= 0.0f)
        return;

    // Convert CSS-style blur radius to sigma.
    // CSS filter: blur(radius) uses sigma = radius / 2.
    float sigma = radius * 0.5f;
    if (sigma < 0.5f)
        return;

    int sizes[3];
    box_sizes_for_gaussian(sigma, sizes);

    const int npixels = width * height;

    // Four float channels: A, R, G, B + one temp buffer.
    auto channel = std::make_unique<float[]>(static_cast<size_t>(npixels));
    auto temp    = std::make_unique<float[]>(static_cast<size_t>(npixels));

    // Process each channel independently (data is premultiplied ARGB).
    for (int c = 0; c < 4; ++c) {
        // Unpack channel from ARGB32 pixels into float buffer.
        const int shift = (3 - c) * 8; // A=24, R=16, G=8, B=0
        for (int y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            float* dst = channel.get() + y * width;
            for (int x = 0; x < width; ++x)
                dst[x] = static_cast<float>((row[x] >> shift) & 0xFF);
        }

        // Three box-blur passes.
        box_blur(channel.get(), temp.get(), width, height, (sizes[0] - 1) / 2);
        box_blur(channel.get(), temp.get(), width, height, (sizes[1] - 1) / 2);
        box_blur(channel.get(), temp.get(), width, height, (sizes[2] - 1) / 2);

        // Pack channel back into ARGB32 pixels.
        const uint32_t mask = ~(0xFFU << shift);
        for (int y = 0; y < height; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(data + y * stride);
            const float* src = channel.get() + y * width;
            for (int x = 0; x < width; ++x) {
                auto val = static_cast<uint32_t>(std::clamp(std::round(src[x]), 0.0f, 255.0f));
                row[x] = (row[x] & mask) | (val << shift);
            }
        }
    }
}

} // namespace plutovg
