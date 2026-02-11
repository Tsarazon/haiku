/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SIMD_H
#define _KOSM_SIMD_H

#include <cstdint>

#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__x86_64__)
#include <smmintrin.h>
#else
#error "Unsupported architecture"
#endif

namespace kosm::simd {

inline constexpr int width = 4;

// ============================================================
//  ARM64 (NEON) — native types are already distinct
// ============================================================

#if defined(__aarch64__)

using f32x4 = float32x4_t;
using i32x4 = int32x4_t;
using u32x4 = uint32x4_t;
using u16x8 = uint16x8_t;
using u8x16 = uint8x16_t;

// Load / Store
inline f32x4 load(const float* p)                    { return vld1q_f32(p); }
inline void  store(float* p, f32x4 v)                { vst1q_f32(p, v); }
inline f32x4 load_aligned(const float* p)             { return vld1q_f32(p); }
inline void  store_aligned(float* p, f32x4 v)         { vst1q_f32(p, v); }
inline i32x4 load(const int32_t* p)                   { return vld1q_s32(p); }
inline void  store(int32_t* p, i32x4 v)               { vst1q_s32(p, v); }
inline u8x16 load(const uint8_t* p)                   { return vld1q_u8(p); }
inline void  store(uint8_t* p, u8x16 v)               { vst1q_u8(p, v); }

// Splat
inline f32x4 splat(float x)                           { return vdupq_n_f32(x); }
inline i32x4 splat(int32_t x)                         { return vdupq_n_s32(x); }
inline u32x4 splat(uint32_t x)                        { return vdupq_n_u32(x); }
inline f32x4 zero_f32()                               { return vdupq_n_f32(0.0f); }
inline i32x4 zero_i32()                               { return vdupq_n_s32(0); }
inline u32x4 zero_u32()                               { return vdupq_n_u32(0); }

// Arithmetic float
inline f32x4 add(f32x4 a, f32x4 b)                   { return vaddq_f32(a, b); }
inline f32x4 sub(f32x4 a, f32x4 b)                   { return vsubq_f32(a, b); }
inline f32x4 mul(f32x4 a, f32x4 b)                   { return vmulq_f32(a, b); }
inline f32x4 div(f32x4 a, f32x4 b)                   { return vdivq_f32(a, b); }
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c)          { return vfmaq_f32(c, a, b); }
inline f32x4 fnma(f32x4 a, f32x4 b, f32x4 c)         { return vfmsq_f32(c, a, b); }
inline f32x4 neg(f32x4 a)                             { return vnegq_f32(a); }
inline f32x4 abs(f32x4 a)                             { return vabsq_f32(a); }
inline f32x4 sqrt(f32x4 a)                            { return vsqrtq_f32(a); }
inline f32x4 rcp(f32x4 a) {
    auto est = vrecpeq_f32(a);
    return vmulq_f32(est, vrecpsq_f32(a, est)); // ~23 bit
}

// Arithmetic signed integer
inline i32x4 add(i32x4 a, i32x4 b)                   { return vaddq_s32(a, b); }
inline i32x4 sub(i32x4 a, i32x4 b)                   { return vsubq_s32(a, b); }
inline i32x4 mul(i32x4 a, i32x4 b)                   { return vmulq_s32(a, b); }

// Arithmetic unsigned integer
inline u32x4 add(u32x4 a, u32x4 b)                   { return vaddq_u32(a, b); }
inline u32x4 sub(u32x4 a, u32x4 b)                   { return vsubq_u32(a, b); }
inline u32x4 mul(u32x4 a, u32x4 b)                   { return vmulq_u32(a, b); }

// Min / Max / Clamp
inline f32x4 min(f32x4 a, f32x4 b)                   { return vminq_f32(a, b); }
inline f32x4 max(f32x4 a, f32x4 b)                   { return vmaxq_f32(a, b); }
inline i32x4 min(i32x4 a, i32x4 b)                   { return vminq_s32(a, b); }
inline i32x4 max(i32x4 a, i32x4 b)                   { return vmaxq_s32(a, b); }
inline u32x4 min(u32x4 a, u32x4 b)                   { return vminq_u32(a, b); }
inline u32x4 max(u32x4 a, u32x4 b)                   { return vmaxq_u32(a, b); }
inline f32x4 clamp(f32x4 v, f32x4 lo, f32x4 hi)      { return vminq_f32(vmaxq_f32(v, lo), hi); }

// Rounding
inline f32x4 floor(f32x4 a)                           { return vrndmq_f32(a); }
inline f32x4 ceil(f32x4 a)                            { return vrndpq_f32(a); }
inline f32x4 round(f32x4 a)                           { return vrndnq_f32(a); }
inline f32x4 trunc(f32x4 a)                           { return vrndq_f32(a); }

// Conversion
inline i32x4 cvt_trunc(f32x4 a)                      { return vcvtq_s32_f32(a); }
inline i32x4 cvt_round(f32x4 a)                      { return vcvtnq_s32_f32(a); }
inline f32x4 cvt(i32x4 a)                             { return vcvtq_f32_s32(a); }
inline f32x4 cvt(u32x4 a)                             { return vcvtq_f32_u32(a); }

// Compare
inline u32x4 cmpeq(f32x4 a, f32x4 b)                 { return vceqq_f32(a, b); }
inline u32x4 cmplt(f32x4 a, f32x4 b)                 { return vcltq_f32(a, b); }
inline u32x4 cmple(f32x4 a, f32x4 b)                 { return vcleq_f32(a, b); }
inline u32x4 cmpgt(f32x4 a, f32x4 b)                 { return vcgtq_f32(a, b); }
inline u32x4 cmpge(f32x4 a, f32x4 b)                 { return vcgeq_f32(a, b); }
inline u32x4 cmpeq(i32x4 a, i32x4 b)                 { return vceqq_s32(a, b); }
inline u32x4 cmplt(i32x4 a, i32x4 b)                 { return vcltq_s32(a, b); }
inline u32x4 cmpgt(i32x4 a, i32x4 b)                 { return vcgtq_s32(a, b); }

// Bitwise
inline u32x4 bit_and(u32x4 a, u32x4 b)               { return vandq_u32(a, b); }
inline u32x4 bit_or(u32x4 a, u32x4 b)                { return vorrq_u32(a, b); }
inline u32x4 bit_xor(u32x4 a, u32x4 b)               { return veorq_u32(a, b); }
// bit_andnot(a, b) = ~a & b  (clear bits of a in b)
inline u32x4 bit_andnot(u32x4 a, u32x4 b)            { return vbicq_u32(b, a); }
inline u32x4 bit_not(u32x4 a)                         { return vmvnq_u32(a); }

inline f32x4 bit_and(f32x4 a, f32x4 b) {
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}
inline f32x4 bit_or(f32x4 a, f32x4 b) {
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}
inline f32x4 bit_xor(f32x4 a, f32x4 b) {
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
}

// Blend: mask ? b : a
inline f32x4 blend(f32x4 a, f32x4 b, u32x4 m)        { return vbslq_f32(m, b, a); }
inline i32x4 blend(i32x4 a, i32x4 b, u32x4 m)        { return vbslq_s32(m, b, a); }
inline u32x4 blend(u32x4 a, u32x4 b, u32x4 m)        { return vbslq_u32(m, b, a); }

// Shift
template<int N> inline i32x4 shl(i32x4 a)             { return vshlq_n_s32(a, N); }
template<int N> inline i32x4 shr(i32x4 a)             { return vshrq_n_s32(a, N); }
template<int N> inline u32x4 shl(u32x4 a)             { return vshlq_n_u32(a, N); }
template<int N> inline u32x4 shr(u32x4 a)             { return vshrq_n_u32(a, N); }

// Reinterpret casts
inline f32x4 as_f32(u32x4 a)                          { return vreinterpretq_f32_u32(a); }
inline f32x4 as_f32(i32x4 a)                          { return vreinterpretq_f32_s32(a); }
inline u32x4 as_u32(f32x4 a)                          { return vreinterpretq_u32_f32(a); }
inline u32x4 as_u32(i32x4 a)                          { return vreinterpretq_u32_s32(a); }
inline i32x4 as_i32(f32x4 a)                          { return vreinterpretq_s32_f32(a); }
inline i32x4 as_i32(u32x4 a)                          { return vreinterpretq_s32_u32(a); }

// Lane access
template<int I> inline float extract(f32x4 v)          { return vgetq_lane_f32(v, I); }
template<int I> inline f32x4 insert(f32x4 v, float x)  { return vsetq_lane_f32(x, v, I); }
template<int I> inline int32_t extract(i32x4 v)        { return vgetq_lane_s32(v, I); }
template<int I> inline uint32_t extract(u32x4 v)       { return vgetq_lane_u32(v, I); }

// Horizontal
inline float hmin(f32x4 a)                            { return vminvq_f32(a); }
inline float hmax(f32x4 a)                            { return vmaxvq_f32(a); }
inline float hsum(f32x4 a)                            { return vaddvq_f32(a); }
inline int32_t hsum(i32x4 a)                          { return vaddvq_s32(a); }
inline uint32_t hsum(u32x4 a)                         { return vaddvq_u32(a); }

// Mask queries
inline bool all(u32x4 m)                              { return vminvq_u32(m) != 0; }
inline bool any(u32x4 m)                              { return vmaxvq_u32(m) != 0; }
inline bool none(u32x4 m)                             { return vmaxvq_u32(m) == 0; }

// Sliding window
inline f32x4 shift_left_1(f32x4 a)                    { return vextq_f32(a, vdupq_n_f32(0), 1); }
inline f32x4 shift_right_1(f32x4 a)                   { return vextq_f32(vdupq_n_f32(0), a, 3); }

// Prefix sum
inline f32x4 prefix_sum(f32x4 v) {
    v = vaddq_f32(v, vextq_f32(vdupq_n_f32(0), v, 3));
    v = vaddq_f32(v, vextq_f32(vdupq_n_f32(0), v, 2));
    return v;
}

// Pixel pack / unpack
inline u32x4 unpack_u8_to_u32(u8x16 v) {
    return vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(v))));
}
inline u8x16 pack_u32_to_u8(u32x4 v) {
    auto n16 = vqmovn_u32(v);
    auto n8  = vqmovn_u16(vcombine_u16(n16, n16));
    return vcombine_u8(n8, n8);
}
inline void widen_u8_to_u16(u8x16 v, u16x8& lo, u16x8& hi) {
    lo = vmovl_u8(vget_low_u8(v));
    hi = vmovl_u8(vget_high_u8(v));
}

// Deinterleave 4 RGBA u8 pixels (16 bytes) -> float channels [0..1]
inline void unpack_rgba(const uint8_t* pixel, f32x4& r, f32x4& g, f32x4& b, f32x4& a) {
    // Load exactly 16 bytes (4 pixels × 4 bytes). Manual deinterleave.
    auto raw = vld1q_u32(reinterpret_cast<const uint32_t*>(pixel));
    auto scale = vdupq_n_f32(1.0f / 255.0f);
    auto mask8 = vdupq_n_u32(0xFF);
    r = vmulq_f32(vcvtq_f32_u32(vandq_u32(raw, mask8)), scale);
    g = vmulq_f32(vcvtq_f32_u32(vandq_u32(vshrq_n_u32(raw, 8), mask8)), scale);
    b = vmulq_f32(vcvtq_f32_u32(vandq_u32(vshrq_n_u32(raw, 16), mask8)), scale);
    a = vmulq_f32(vcvtq_f32_u32(vshrq_n_u32(raw, 24)), scale);
}

// Cross product 2D: ax*by - ay*bx
inline f32x4 cross2d(f32x4 ax, f32x4 ay, f32x4 bx, f32x4 by) {
    return vfmsq_f32(vmulq_f32(ax, by), ay, bx);
}

// Lerp: a + t*(b - a)
inline f32x4 lerp(f32x4 a, f32x4 b, f32x4 t) {
    return vfmaq_f32(a, t, vsubq_f32(b, a));
}

// Color matrix: r*cr + g*cg + b*cb
inline f32x4 dot3(f32x4 r, f32x4 g, f32x4 b, f32x4 cr, f32x4 cg, f32x4 cb) {
    auto result = vmulq_f32(r, cr);
    result = vfmaq_f32(result, g, cg);
    result = vfmaq_f32(result, b, cb);
    return result;
}

// sRGB <-> linear (fast approximation)
inline f32x4 srgb_to_linear(f32x4 v) {
    return vmulq_f32(vmulq_f32(v, v), vsqrtq_f32(vsqrtq_f32(v)));
}
inline f32x4 linear_to_srgb(f32x4 v) {
    auto vsq  = vsqrtq_f32(v);
    auto vqrt = vsqrtq_f32(vsq);
    return vfmaq_f32(vmulq_f32(vdupq_n_f32(0.82f), vsq), vdupq_n_f32(0.18f), vqrt);
}

// Premultiply alpha
inline void premultiply(f32x4 r, f32x4 g, f32x4 b, f32x4 a,
                         f32x4& pr, f32x4& pg, f32x4& pb) {
    pr = vmulq_f32(r, a);
    pg = vmulq_f32(g, a);
    pb = vmulq_f32(b, a);
}

// Unpremultiply alpha
inline void unpremultiply(f32x4 pr, f32x4 pg, f32x4 pb, f32x4 a,
                           f32x4& r, f32x4& g, f32x4& b) {
    auto inv_a = vrecpeq_f32(a);
    inv_a = vmulq_f32(inv_a, vrecpsq_f32(a, inv_a));
    auto mask = vcgtq_f32(a, vdupq_n_f32(0.0f));
    inv_a = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(inv_a), mask));
    r = vmulq_f32(pr, inv_a);
    g = vmulq_f32(pg, inv_a);
    b = vmulq_f32(pb, inv_a);
}

// ============================================================
//  x86_64 (SSE4.1) — wrapper structs for type safety
// ============================================================

#elif defined(__x86_64__)

using f32x4 = __m128;

struct i32x4 {
    __m128i v;
    i32x4() = default;
    i32x4(__m128i val) : v(val) {}
    operator __m128i() const { return v; }
};

struct u32x4 {
    __m128i v;
    u32x4() = default;
    u32x4(__m128i val) : v(val) {}
    operator __m128i() const { return v; }
};

struct u16x8 {
    __m128i v;
    u16x8() = default;
    u16x8(__m128i val) : v(val) {}
    operator __m128i() const { return v; }
};

struct u8x16 {
    __m128i v;
    u8x16() = default;
    u8x16(__m128i val) : v(val) {}
    operator __m128i() const { return v; }
};

// Load / Store
inline f32x4 load(const float* p)                     { return _mm_loadu_ps(p); }
inline void  store(float* p, f32x4 v)                 { _mm_storeu_ps(p, v); }
inline f32x4 load_aligned(const float* p)              { return _mm_load_ps(p); }
inline void  store_aligned(float* p, f32x4 v)          { _mm_store_ps(p, v); }
inline i32x4 load(const int32_t* p)                    { return _mm_loadu_si128((const __m128i*)p); }
inline void  store(int32_t* p, i32x4 v)                { _mm_storeu_si128((__m128i*)p, v.v); }
inline u8x16 load(const uint8_t* p)                    { return _mm_loadu_si128((const __m128i*)p); }
inline void  store(uint8_t* p, u8x16 v)                { _mm_storeu_si128((__m128i*)p, v.v); }

// Splat
inline f32x4 splat(float x)                            { return _mm_set1_ps(x); }
inline i32x4 splat(int32_t x)                          { return _mm_set1_epi32(x); }
inline u32x4 splat(uint32_t x)                         { return _mm_set1_epi32(static_cast<int>(x)); }
inline f32x4 zero_f32()                                { return _mm_setzero_ps(); }
inline i32x4 zero_i32()                                { return _mm_setzero_si128(); }
inline u32x4 zero_u32()                                { return _mm_setzero_si128(); }

// Arithmetic float
inline f32x4 add(f32x4 a, f32x4 b)                    { return _mm_add_ps(a, b); }
inline f32x4 sub(f32x4 a, f32x4 b)                    { return _mm_sub_ps(a, b); }
inline f32x4 mul(f32x4 a, f32x4 b)                    { return _mm_mul_ps(a, b); }
inline f32x4 div(f32x4 a, f32x4 b)                    { return _mm_div_ps(a, b); }
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c)           { return _mm_add_ps(_mm_mul_ps(a, b), c); }
inline f32x4 fnma(f32x4 a, f32x4 b, f32x4 c)          { return _mm_sub_ps(c, _mm_mul_ps(a, b)); }
inline f32x4 neg(f32x4 a)                              { return _mm_xor_ps(a, _mm_set1_ps(-0.0f)); }
inline f32x4 abs(f32x4 a)                              { return _mm_andnot_ps(_mm_set1_ps(-0.0f), a); }
inline f32x4 sqrt(f32x4 a)                             { return _mm_sqrt_ps(a); }
inline f32x4 rcp(f32x4 a) {
    auto est = _mm_rcp_ps(a);
    return _mm_mul_ps(est, _mm_sub_ps(_mm_set1_ps(2.0f), _mm_mul_ps(a, est))); // ~23 bit
}

// Arithmetic signed integer
inline i32x4 add(i32x4 a, i32x4 b)                    { return _mm_add_epi32(a.v, b.v); }
inline i32x4 sub(i32x4 a, i32x4 b)                    { return _mm_sub_epi32(a.v, b.v); }
inline i32x4 mul(i32x4 a, i32x4 b)                    { return _mm_mullo_epi32(a.v, b.v); }

// Arithmetic unsigned integer
inline u32x4 add(u32x4 a, u32x4 b)                    { return _mm_add_epi32(a.v, b.v); }
inline u32x4 sub(u32x4 a, u32x4 b)                    { return _mm_sub_epi32(a.v, b.v); }
inline u32x4 mul(u32x4 a, u32x4 b)                    { return _mm_mullo_epi32(a.v, b.v); }

// Min / Max / Clamp
inline f32x4 min(f32x4 a, f32x4 b)                    { return _mm_min_ps(a, b); }
inline f32x4 max(f32x4 a, f32x4 b)                    { return _mm_max_ps(a, b); }
inline i32x4 min(i32x4 a, i32x4 b)                    { return _mm_min_epi32(a.v, b.v); }
inline i32x4 max(i32x4 a, i32x4 b)                    { return _mm_max_epi32(a.v, b.v); }
inline u32x4 min(u32x4 a, u32x4 b)                    { return _mm_min_epu32(a.v, b.v); }
inline u32x4 max(u32x4 a, u32x4 b)                    { return _mm_max_epu32(a.v, b.v); }
inline f32x4 clamp(f32x4 v, f32x4 lo, f32x4 hi)       { return _mm_min_ps(_mm_max_ps(v, lo), hi); }

// Rounding
inline f32x4 floor(f32x4 a)                            { return _mm_floor_ps(a); }
inline f32x4 ceil(f32x4 a)                             { return _mm_ceil_ps(a); }
inline f32x4 round(f32x4 a)                            { return _mm_round_ps(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC); }
inline f32x4 trunc(f32x4 a)                            { return _mm_round_ps(a, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC); }

// Conversion
inline i32x4 cvt_trunc(f32x4 a)                       { return _mm_cvttps_epi32(a); }
inline i32x4 cvt_round(f32x4 a)                       { return _mm_cvtps_epi32(a); }
inline f32x4 cvt(i32x4 a)                              { return _mm_cvtepi32_ps(a.v); }
inline f32x4 cvt(u32x4 a) {
    // _mm_cvtepi32_ps treats bits as signed — broken for values >= 2^31.
    // Split into hi16 * 65536 + lo16, both halves fit in signed int32.
    auto hi = _mm_srli_epi32(a.v, 16);
    auto lo = _mm_and_si128(a.v, _mm_set1_epi32(0xFFFF));
    return _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(hi), _mm_set1_ps(65536.0f)),
                      _mm_cvtepi32_ps(lo));
}

// Compare (return u32x4 mask)
inline u32x4 cmpeq(f32x4 a, f32x4 b)                  { return _mm_castps_si128(_mm_cmpeq_ps(a, b)); }
inline u32x4 cmplt(f32x4 a, f32x4 b)                  { return _mm_castps_si128(_mm_cmplt_ps(a, b)); }
inline u32x4 cmple(f32x4 a, f32x4 b)                  { return _mm_castps_si128(_mm_cmple_ps(a, b)); }
inline u32x4 cmpgt(f32x4 a, f32x4 b)                  { return _mm_castps_si128(_mm_cmpgt_ps(a, b)); }
inline u32x4 cmpge(f32x4 a, f32x4 b)                  { return _mm_castps_si128(_mm_cmpge_ps(a, b)); }
inline u32x4 cmpeq(i32x4 a, i32x4 b)                  { return _mm_cmpeq_epi32(a.v, b.v); }
inline u32x4 cmplt(i32x4 a, i32x4 b)                  { return _mm_cmplt_epi32(a.v, b.v); }
inline u32x4 cmpgt(i32x4 a, i32x4 b)                  { return _mm_cmpgt_epi32(a.v, b.v); }

// Bitwise (on u32x4)
inline u32x4 bit_and(u32x4 a, u32x4 b)                { return _mm_and_si128(a.v, b.v); }
inline u32x4 bit_or(u32x4 a, u32x4 b)                 { return _mm_or_si128(a.v, b.v); }
inline u32x4 bit_xor(u32x4 a, u32x4 b)                { return _mm_xor_si128(a.v, b.v); }
// bit_andnot(a, b) = ~a & b  (clear bits of a in b)
inline u32x4 bit_andnot(u32x4 a, u32x4 b)             { return _mm_andnot_si128(a.v, b.v); }
inline u32x4 bit_not(u32x4 a)                          { return _mm_xor_si128(a.v, _mm_set1_epi32(-1)); }

// Bitwise (on f32x4)
inline f32x4 bit_and(f32x4 a, f32x4 b)                 { return _mm_and_ps(a, b); }
inline f32x4 bit_or(f32x4 a, f32x4 b)                  { return _mm_or_ps(a, b); }
inline f32x4 bit_xor(f32x4 a, f32x4 b)                 { return _mm_xor_ps(a, b); }

// Blend: mask ? b : a
inline f32x4 blend(f32x4 a, f32x4 b, u32x4 m)         { return _mm_blendv_ps(a, b, _mm_castsi128_ps(m.v)); }
inline i32x4 blend(i32x4 a, i32x4 b, u32x4 m)         { return _mm_blendv_epi8(a.v, b.v, m.v); }
inline u32x4 blend(u32x4 a, u32x4 b, u32x4 m)         { return _mm_blendv_epi8(a.v, b.v, m.v); }

// Shift (signed: arithmetic, unsigned: logical)
template<int N> inline i32x4 shl(i32x4 a)              { return _mm_slli_epi32(a.v, N); }
template<int N> inline i32x4 shr(i32x4 a)              { return _mm_srai_epi32(a.v, N); }
template<int N> inline u32x4 shl(u32x4 a)              { return _mm_slli_epi32(a.v, N); }
template<int N> inline u32x4 shr(u32x4 a)              { return _mm_srli_epi32(a.v, N); }

// Reinterpret casts
inline f32x4 as_f32(u32x4 a)                           { return _mm_castsi128_ps(a.v); }
inline f32x4 as_f32(i32x4 a)                           { return _mm_castsi128_ps(a.v); }
inline u32x4 as_u32(f32x4 a)                           { return _mm_castps_si128(a); }
inline u32x4 as_u32(i32x4 a)                           { return a.v; }
inline i32x4 as_i32(f32x4 a)                           { return _mm_castps_si128(a); }
inline i32x4 as_i32(u32x4 a)                           { return a.v; }

// Lane access
template<int I> inline float extract(f32x4 v) {
    if constexpr (I == 0) return _mm_cvtss_f32(v);
    else return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(I,I,I,I)));
}
template<int I> inline f32x4 insert(f32x4 v, float x)  { return _mm_insert_ps(v, _mm_set_ss(x), I << 4); }
template<int I> inline int32_t extract(i32x4 v)         { return _mm_extract_epi32(v.v, I); }
template<int I> inline uint32_t extract(u32x4 v)        { return static_cast<uint32_t>(_mm_extract_epi32(v.v, I)); }

// Horizontal
inline float hmin(f32x4 a) {
    auto t = _mm_min_ps(a, _mm_shuffle_ps(a, a, _MM_SHUFFLE(2,3,0,1)));
    return _mm_cvtss_f32(_mm_min_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(1,0,3,2))));
}
inline float hmax(f32x4 a) {
    auto t = _mm_max_ps(a, _mm_shuffle_ps(a, a, _MM_SHUFFLE(2,3,0,1)));
    return _mm_cvtss_f32(_mm_max_ps(t, _mm_shuffle_ps(t, t, _MM_SHUFFLE(1,0,3,2))));
}
inline float hsum(f32x4 a) {
    auto t = _mm_hadd_ps(a, a);
    return _mm_cvtss_f32(_mm_hadd_ps(t, t));
}
inline int32_t hsum(i32x4 a) {
    auto t = _mm_hadd_epi32(a.v, a.v);
    return _mm_extract_epi32(_mm_hadd_epi32(t, t), 0);
}
inline uint32_t hsum(u32x4 a) {
    auto t = _mm_hadd_epi32(a.v, a.v);
    return static_cast<uint32_t>(_mm_extract_epi32(_mm_hadd_epi32(t, t), 0));
}

// Mask queries
inline bool all(u32x4 m)                               { return _mm_movemask_epi8(m.v) == 0xFFFF; }
inline bool any(u32x4 m)                               { return _mm_movemask_epi8(m.v) != 0; }
inline bool none(u32x4 m)                              { return _mm_movemask_epi8(m.v) == 0; }

// Sliding window
inline f32x4 shift_left_1(f32x4 a) {
    return _mm_castsi128_ps(_mm_srli_si128(_mm_castps_si128(a), 4));
}
inline f32x4 shift_right_1(f32x4 a) {
    return _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(a), 4));
}

// Prefix sum
inline f32x4 prefix_sum(f32x4 v) {
    v = _mm_add_ps(v, _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(v), 4)));
    v = _mm_add_ps(v, _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(v), 8)));
    return v;
}

// Pixel pack / unpack
inline u32x4 unpack_u8_to_u32(u8x16 v)                 { return _mm_cvtepu8_epi32(v.v); }
inline u8x16 pack_u32_to_u8(u32x4 v) {
    auto p16 = _mm_packus_epi32(v.v, v.v);
    return _mm_packus_epi16(p16, p16);
}
inline void widen_u8_to_u16(u8x16 v, u16x8& lo, u16x8& hi) {
    lo = _mm_cvtepu8_epi16(v.v);
    hi = _mm_cvtepu8_epi16(_mm_srli_si128(v.v, 8));
}

// Deinterleave 4 RGBA pixels (no vld4 on x86)
inline void unpack_rgba(const uint8_t* pixel, f32x4& r, f32x4& g, f32x4& b, f32x4& a) {
    auto raw = _mm_loadu_si128((const __m128i*)pixel);
    auto scale = _mm_set1_ps(1.0f / 255.0f);
    auto mask8 = _mm_set1_epi32(0xFF);
    r = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(raw, mask8)), scale);
    g = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(raw, 8), mask8)), scale);
    b = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(raw, 16), mask8)), scale);
    a = _mm_mul_ps(_mm_cvtepi32_ps(_mm_srli_epi32(raw, 24)), scale);
}

// Cross product 2D
inline f32x4 cross2d(f32x4 ax, f32x4 ay, f32x4 bx, f32x4 by) {
    return _mm_sub_ps(_mm_mul_ps(ax, by), _mm_mul_ps(ay, bx));
}

// Lerp
inline f32x4 lerp(f32x4 a, f32x4 b, f32x4 t) {
    return _mm_add_ps(a, _mm_mul_ps(t, _mm_sub_ps(b, a)));
}

// Color matrix
inline f32x4 dot3(f32x4 r, f32x4 g, f32x4 b, f32x4 cr, f32x4 cg, f32x4 cb) {
    return _mm_add_ps(_mm_mul_ps(r, cr), _mm_add_ps(_mm_mul_ps(g, cg), _mm_mul_ps(b, cb)));
}

// sRGB <-> linear
inline f32x4 srgb_to_linear(f32x4 v) {
    return _mm_mul_ps(_mm_mul_ps(v, v), _mm_sqrt_ps(_mm_sqrt_ps(v)));
}
inline f32x4 linear_to_srgb(f32x4 v) {
    auto vsq  = _mm_sqrt_ps(v);
    auto vqrt = _mm_sqrt_ps(vsq);
    return _mm_add_ps(_mm_mul_ps(_mm_set1_ps(0.82f), vsq), _mm_mul_ps(_mm_set1_ps(0.18f), vqrt));
}

// Premultiply
inline void premultiply(f32x4 r, f32x4 g, f32x4 b, f32x4 a,
                         f32x4& pr, f32x4& pg, f32x4& pb) {
    pr = _mm_mul_ps(r, a);
    pg = _mm_mul_ps(g, a);
    pb = _mm_mul_ps(b, a);
}

// Unpremultiply
inline void unpremultiply(f32x4 pr, f32x4 pg, f32x4 pb, f32x4 a,
                           f32x4& r, f32x4& g, f32x4& b) {
    auto inv_a = _mm_rcp_ps(a);
    inv_a = _mm_mul_ps(inv_a, _mm_sub_ps(_mm_set1_ps(2.0f), _mm_mul_ps(a, inv_a)));
    auto mask = _mm_cmpgt_ps(a, _mm_setzero_ps());
    inv_a = _mm_and_ps(inv_a, mask);
    r = _mm_mul_ps(pr, inv_a);
    g = _mm_mul_ps(pg, inv_a);
    b = _mm_mul_ps(pb, inv_a);
}

#endif

// ============================================================
//  Compositing primitives (architecture-independent)
// ============================================================

// Fixed-point alpha multiply: 4 ARGB pixels × per-pixel alpha [0..255].
// Each pixel is 0xAARRGGBB premultiplied. Alpha in low byte of each lane.
inline u32x4 byte_mul_x4(u32x4 x, u32x4 a) {
    auto mask = splat(0x00FF00FFu);
    auto half = splat(0x00800080u);

    auto rb = bit_and(x, mask);
    rb = mul(rb, a);
    rb = shr<8>(add(add(rb, bit_and(shr<8>(rb), mask)), half));
    rb = bit_and(rb, mask);

    auto ag = bit_and(shr<8>(x), mask);
    ag = mul(ag, a);
    ag = add(add(ag, bit_and(shr<8>(ag), mask)), half);
    ag = bit_and(ag, bit_not(mask));

    return bit_or(rb, ag);
}

// Interpolate: byte_mul(x, a) + byte_mul(y, b). Core of SrcOver.
inline u32x4 interpolate_x4(u32x4 x, u32x4 a, u32x4 y, u32x4 b) {
    auto mask = splat(0x00FF00FFu);
    auto half = splat(0x00800080u);

    auto x_rb = bit_and(x, mask);
    auto y_rb = bit_and(y, mask);
    auto rb = add(mul(x_rb, a), mul(y_rb, b));
    rb = shr<8>(add(add(rb, bit_and(shr<8>(rb), mask)), half));
    rb = bit_and(rb, mask);

    auto x_ag = bit_and(shr<8>(x), mask);
    auto y_ag = bit_and(shr<8>(y), mask);
    auto ag = add(mul(x_ag, a), mul(y_ag, b));
    ag = add(add(ag, bit_and(shr<8>(ag), mask)), half);
    ag = bit_and(ag, bit_not(mask));

    return bit_or(rb, ag);
}

// Extract alpha from 4 packed ARGB pixels.
inline u32x4 alpha_x4(u32x4 pixels) {
    return shr<24>(pixels);
}

// Unpack 4 ARGB pixels (0xAARRGGBB) → float [0..1] channels.
inline void unpack_argb(const uint32_t* pixels, f32x4& r, f32x4& g, f32x4& b, f32x4& a) {
    auto raw = as_u32(load(reinterpret_cast<const int32_t*>(pixels)));
    auto scale = splat(1.0f / 255.0f);
    auto mask8 = splat(0xFFu);
    b = mul(cvt(as_i32(bit_and(raw, mask8))), scale);
    g = mul(cvt(as_i32(bit_and(shr<8>(raw), mask8))), scale);
    r = mul(cvt(as_i32(bit_and(shr<16>(raw), mask8))), scale);
    a = mul(cvt(as_i32(shr<24>(raw))), scale);
}

// Pack float [0..1] channels → 4 ARGB pixels (0xAARRGGBB).
inline u32x4 pack_argb(f32x4 r, f32x4 g, f32x4 b, f32x4 a) {
    auto scale = splat(255.0f);
    auto lo = zero_f32();
    auto ai = as_u32(cvt_round(clamp(mul(a, scale), lo, scale)));
    auto ri = as_u32(cvt_round(clamp(mul(r, scale), lo, scale)));
    auto gi = as_u32(cvt_round(clamp(mul(g, scale), lo, scale)));
    auto bi = as_u32(cvt_round(clamp(mul(b, scale), lo, scale)));
    return bit_or(bit_or(shl<24>(ai), shl<16>(ri)),
                  bit_or(shl<8>(gi), bi));
}

// ARGB → BGRA: 0xAARRGGBB → 0xBBGGRRAA (byte reverse per lane).
inline u32x4 argb_to_bgra(u32x4 px) {
    return bit_or(
        bit_or(shl<24>(px), bit_and(shl<8>(px), splat(0x00FF0000u))),
        bit_or(bit_and(shr<8>(px), splat(0x0000FF00u)), shr<24>(px))
    );
}

// ARGB → RGBA: 0xAARRGGBB → 0xRRGGBBAA (rotate left 8 bits).
inline u32x4 argb_to_rgba(u32x4 px) {
    return bit_or(shl<8>(px), shr<24>(px));
}

// RGBA → ARGB: 0xRRGGBBAA → 0xAARRGGBB (rotate right 8 bits).
inline u32x4 rgba_to_argb(u32x4 px) {
    return bit_or(shr<8>(px), shl<24>(px));
}

// BGRA → ARGB: byte reverse (self-inverse).
inline u32x4 bgra_to_argb(u32x4 px) {
    return argb_to_bgra(px);
}

} // namespace kosm::simd

#endif // _KOSM_SIMD_H
