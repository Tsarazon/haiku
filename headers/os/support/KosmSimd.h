// kosm_simd.h — KosmOS Support Kit
// Portable SIMD primitives: ARM64 (NEON) / x86_64 (SSE4.1) / RISC-V 64 (RVV 1.0)

#pragma once

#include <cstdint>

#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__x86_64__)
#include <smmintrin.h>
#elif defined(__riscv_v)
#include <riscv_vector.h>
#else
#error "Unsupported architecture"
#endif

namespace kosmsimd {

inline constexpr int width = 4;

// ARM64 (NEON) — native types are already distinct

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
inline u32x4 load(const uint32_t* p)                  { return vld1q_u32(p); }
inline void  store(uint32_t* p, u32x4 v)              { vst1q_u32(p, v); }
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

// Saturating u8 arithmetic
inline u8x16 adds_u8(u8x16 a, u8x16 b)              { return vqaddq_u8(a, b); }
inline u8x16 subs_u8(u8x16 a, u8x16 b)              { return vqsubq_u8(a, b); }

// Shuffle
inline f32x4 reverse(f32x4 v)                        { return vrev64q_f32(vcombine_f32(vget_high_f32(v), vget_low_f32(v))); }
inline u32x4 reverse(u32x4 v)                        { return vrev64q_u32(vcombine_u32(vget_high_u32(v), vget_low_u32(v))); }
inline i32x4 reverse(i32x4 v)                        { return vrev64q_s32(vcombine_s32(vget_high_s32(v), vget_low_s32(v))); }
template<int I> inline f32x4 broadcast_lane(f32x4 v)  { return vdupq_laneq_f32(v, I); }
template<int I> inline i32x4 broadcast_lane(i32x4 v)  { return vdupq_laneq_s32(v, I); }
template<int I> inline u32x4 broadcast_lane(u32x4 v)  { return vdupq_laneq_u32(v, I); }

// 4 pixels RGBA (R in byte 0). Each uint32 = 0xAABBGGRR.
// Deinterleave to float [0..1] per channel.
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

// sRGB -> linear, precise piecewise (max absolute error < 0.002 vs true sRGB).
// Low: x / 12.92. High: pow((x + 0.055) / 1.055, 2.4) via t^2.5 * poly(t).
inline f32x4 srgb_to_linear_precise(f32x4 x) {
    auto lo = vmulq_f32(x, vdupq_n_f32(1.0f / 12.92f));
    auto t  = vmulq_f32(vaddq_f32(x, vdupq_n_f32(0.055f)), vdupq_n_f32(1.0f / 1.055f));
    auto t2  = vmulq_f32(t, t);
    auto t25 = vmulq_f32(t2, vsqrtq_f32(t));
    auto corr = vfmaq_f32(vdupq_n_f32(-0.6866f), vdupq_n_f32(0.3617f), t);
    corr = vfmaq_f32(vdupq_n_f32(1.3249f), corr, t);
    auto hi = vmulq_f32(t25, corr);
    auto mask = vcleq_f32(x, vdupq_n_f32(0.04045f));
    return vbslq_f32(mask, lo, hi);
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

// x86_64 (SSE4.1) — wrapper structs for type safety

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
inline u32x4 load(const uint32_t* p)                   { return _mm_loadu_si128((const __m128i*)p); }
inline void  store(uint32_t* p, u32x4 v)               { _mm_storeu_si128((__m128i*)p, v.v); }
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

// Saturating u8 arithmetic
inline u8x16 adds_u8(u8x16 a, u8x16 b)               { return _mm_adds_epu8(a.v, b.v); }
inline u8x16 subs_u8(u8x16 a, u8x16 b)               { return _mm_subs_epu8(a.v, b.v); }

// Shuffle
inline f32x4 reverse(f32x4 v)                         { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(0,1,2,3)); }
inline u32x4 reverse(u32x4 v)                         { return _mm_shuffle_epi32(v.v, _MM_SHUFFLE(0,1,2,3)); }
inline i32x4 reverse(i32x4 v)                         { return _mm_shuffle_epi32(v.v, _MM_SHUFFLE(0,1,2,3)); }
template<int I> inline f32x4 broadcast_lane(f32x4 v)   { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(I,I,I,I)); }
template<int I> inline i32x4 broadcast_lane(i32x4 v)   { return _mm_shuffle_epi32(v.v, _MM_SHUFFLE(I,I,I,I)); }
template<int I> inline u32x4 broadcast_lane(u32x4 v)   { return _mm_shuffle_epi32(v.v, _MM_SHUFFLE(I,I,I,I)); }

// 4 pixels RGBA (R in byte 0). Each uint32 = 0xAABBGGRR.
// Deinterleave to float [0..1] per channel.
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

// sRGB -> linear, precise piecewise (max absolute error < 0.002 vs true sRGB).
// Low: x / 12.92. High: pow((x + 0.055) / 1.055, 2.4) via t^2.5 * poly(t).
inline f32x4 srgb_to_linear_precise(f32x4 x) {
    auto lo = _mm_mul_ps(x, _mm_set1_ps(1.0f / 12.92f));
    auto t  = _mm_mul_ps(_mm_add_ps(x, _mm_set1_ps(0.055f)), _mm_set1_ps(1.0f / 1.055f));
    auto t2  = _mm_mul_ps(t, t);
    auto t25 = _mm_mul_ps(t2, _mm_sqrt_ps(t));
    auto corr = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(0.3617f), t), _mm_set1_ps(-0.6866f));
    corr = _mm_add_ps(_mm_mul_ps(corr, t), _mm_set1_ps(1.3249f));
    auto hi = _mm_mul_ps(t25, corr);
    auto mask = _mm_cmple_ps(x, _mm_set1_ps(0.04045f));
    return _mm_blendv_ps(hi, lo, mask);
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

// Pixel format swaps (pshufb — single-instruction byte permute, available since SSSE3 ⊂ SSE4.1)

// ARGB → BGRA: 0xAARRGGBB → 0xBBGGRRAA (byte reverse per lane)
inline u32x4 argb_to_bgra(u32x4 px) {
    const __m128i shuf = _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
    return _mm_shuffle_epi8(px.v, shuf);
}

// ARGB → RGBA: 0xAARRGGBB → 0xRRGGBBAA (rotate left 8 bits per lane)
inline u32x4 argb_to_rgba(u32x4 px) {
    const __m128i shuf = _mm_setr_epi8(3,0,1,2, 7,4,5,6, 11,8,9,10, 15,12,13,14);
    return _mm_shuffle_epi8(px.v, shuf);
}

// RGBA → ARGB: 0xRRGGBBAA → 0xAARRGGBB (rotate right 8 bits per lane)
inline u32x4 rgba_to_argb(u32x4 px) {
    const __m128i shuf = _mm_setr_epi8(1,2,3,0, 5,6,7,4, 9,10,11,8, 13,14,15,12);
    return _mm_shuffle_epi8(px.v, shuf);
}

// BGRA → ARGB: byte reverse (self-inverse)
inline u32x4 bgra_to_argb(u32x4 px) {
    return argb_to_bgra(px);
}

// RISC-V 64 (RVV 1.0) — vl=4 fixed width, matching NEON/SSE semantics

#elif defined(__riscv_v)

inline constexpr size_t VL4  = 4;
inline constexpr size_t VL8  = 8;
inline constexpr size_t VL16 = 16;

using f32x4 = vfloat32m1_t;
using i32x4 = vint32m1_t;
using u32x4 = vuint32m1_t;
using u16x8 = vuint16m1_t;
using u8x16 = vuint8m1_t;

// Load / Store
inline f32x4 load(const float* p)                    { return __riscv_vle32_v_f32m1(p, VL4); }
inline void  store(float* p, f32x4 v)                { __riscv_vse32_v_f32m1(p, v, VL4); }
inline f32x4 load_aligned(const float* p)             { return __riscv_vle32_v_f32m1(p, VL4); }
inline void  store_aligned(float* p, f32x4 v)         { __riscv_vse32_v_f32m1(p, v, VL4); }
inline i32x4 load(const int32_t* p)                   { return __riscv_vle32_v_i32m1(p, VL4); }
inline void  store(int32_t* p, i32x4 v)               { __riscv_vse32_v_i32m1(p, v, VL4); }
inline u32x4 load(const uint32_t* p)                  { return __riscv_vle32_v_u32m1(p, VL4); }
inline void  store(uint32_t* p, u32x4 v)              { __riscv_vse32_v_u32m1(p, v, VL4); }
inline u8x16 load(const uint8_t* p)                   { return __riscv_vle8_v_u8m1(p, VL16); }
inline void  store(uint8_t* p, u8x16 v)               { __riscv_vse8_v_u8m1(p, v, VL16); }

// Splat
inline f32x4 splat(float x)                           { return __riscv_vfmv_v_f_f32m1(x, VL4); }
inline i32x4 splat(int32_t x)                         { return __riscv_vmv_v_x_i32m1(x, VL4); }
inline u32x4 splat(uint32_t x)                        { return __riscv_vmv_v_x_u32m1(x, VL4); }
inline f32x4 zero_f32()                               { return __riscv_vfmv_v_f_f32m1(0.0f, VL4); }
inline i32x4 zero_i32()                               { return __riscv_vmv_v_x_i32m1(0, VL4); }
inline u32x4 zero_u32()                               { return __riscv_vmv_v_x_u32m1(0, VL4); }

// Arithmetic float
inline f32x4 add(f32x4 a, f32x4 b)                   { return __riscv_vfadd_vv_f32m1(a, b, VL4); }
inline f32x4 sub(f32x4 a, f32x4 b)                   { return __riscv_vfsub_vv_f32m1(a, b, VL4); }
inline f32x4 mul(f32x4 a, f32x4 b)                   { return __riscv_vfmul_vv_f32m1(a, b, VL4); }
inline f32x4 div(f32x4 a, f32x4 b)                   { return __riscv_vfdiv_vv_f32m1(a, b, VL4); }
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c)          { return __riscv_vfmacc_vv_f32m1(c, a, b, VL4); }
inline f32x4 fnma(f32x4 a, f32x4 b, f32x4 c)         { return __riscv_vfnmsac_vv_f32m1(c, a, b, VL4); }
inline f32x4 neg(f32x4 a)                             { return __riscv_vfneg_v_f32m1(a, VL4); }
inline f32x4 abs(f32x4 a)                             { return __riscv_vfabs_v_f32m1(a, VL4); }
inline f32x4 sqrt(f32x4 a)                            { return __riscv_vfsqrt_v_f32m1(a, VL4); }
inline f32x4 rcp(f32x4 a) {
    auto est = __riscv_vfrec7_v_f32m1(a, VL4);
    est = __riscv_vfmul_vv_f32m1(est,
        __riscv_vfrsub_vf_f32m1(__riscv_vfmul_vv_f32m1(a, est, VL4), 2.0f, VL4), VL4);
    est = __riscv_vfmul_vv_f32m1(est,
        __riscv_vfrsub_vf_f32m1(__riscv_vfmul_vv_f32m1(a, est, VL4), 2.0f, VL4), VL4);
    return est; // ~28 bit via two NR steps from vfrec7
}

// Arithmetic signed integer
inline i32x4 add(i32x4 a, i32x4 b)                   { return __riscv_vadd_vv_i32m1(a, b, VL4); }
inline i32x4 sub(i32x4 a, i32x4 b)                   { return __riscv_vsub_vv_i32m1(a, b, VL4); }
inline i32x4 mul(i32x4 a, i32x4 b)                   { return __riscv_vmul_vv_i32m1(a, b, VL4); }

// Arithmetic unsigned integer
inline u32x4 add(u32x4 a, u32x4 b)                   { return __riscv_vadd_vv_u32m1(a, b, VL4); }
inline u32x4 sub(u32x4 a, u32x4 b)                   { return __riscv_vsub_vv_u32m1(a, b, VL4); }
inline u32x4 mul(u32x4 a, u32x4 b)                   { return __riscv_vmul_vv_u32m1(a, b, VL4); }

// Min / Max / Clamp
inline f32x4 min(f32x4 a, f32x4 b)                   { return __riscv_vfmin_vv_f32m1(a, b, VL4); }
inline f32x4 max(f32x4 a, f32x4 b)                   { return __riscv_vfmax_vv_f32m1(a, b, VL4); }
inline i32x4 min(i32x4 a, i32x4 b)                   { return __riscv_vmin_vv_i32m1(a, b, VL4); }
inline i32x4 max(i32x4 a, i32x4 b)                   { return __riscv_vmax_vv_i32m1(a, b, VL4); }
inline u32x4 min(u32x4 a, u32x4 b)                   { return __riscv_vminu_vv_u32m1(a, b, VL4); }
inline u32x4 max(u32x4 a, u32x4 b)                   { return __riscv_vmaxu_vv_u32m1(a, b, VL4); }
inline f32x4 clamp(f32x4 v, f32x4 lo, f32x4 hi) {
    return __riscv_vfmin_vv_f32m1(__riscv_vfmax_vv_f32m1(v, lo, VL4), hi, VL4);
}

// Rounding
// GCC 13 RVV intrinsics lack _rm suffix — implement floor/ceil via trunc + fixup.
// trunc uses vfcvt_rtz (explicit RTZ). round uses vfcvt_x (inherits fcsr, default RNE).
inline f32x4 trunc(f32x4 a) {
    return __riscv_vfcvt_f_x_v_f32m1(__riscv_vfcvt_rtz_x_f_v_i32m1(a, VL4), VL4);
}
inline f32x4 floor(f32x4 a) {
    auto t   = trunc(a);
    auto adj = __riscv_vfcvt_f_x_v_f32m1(
                   __riscv_vmerge_vxm_i32m1(__riscv_vmv_v_x_i32m1(0, VL4), -1,
                       __riscv_vmflt_vv_f32m1_b32(a, t, VL4), VL4), VL4);
    return __riscv_vfadd_vv_f32m1(t, adj, VL4);
}
inline f32x4 ceil(f32x4 a) {
    auto t   = trunc(a);
    auto adj = __riscv_vfcvt_f_x_v_f32m1(
                   __riscv_vmerge_vxm_i32m1(__riscv_vmv_v_x_i32m1(0, VL4), 1,
                       __riscv_vmfgt_vv_f32m1_b32(a, t, VL4), VL4), VL4);
    return __riscv_vfadd_vv_f32m1(t, adj, VL4);
}
inline f32x4 round(f32x4 a) {
    // vfcvt_x uses fcsr rounding mode (default RNE = round-to-nearest-even)
    return __riscv_vfcvt_f_x_v_f32m1(__riscv_vfcvt_x_f_v_i32m1(a, VL4), VL4);
}

// Conversion
inline i32x4 cvt_trunc(f32x4 a)                      { return __riscv_vfcvt_rtz_x_f_v_i32m1(a, VL4); }
inline i32x4 cvt_round(f32x4 a)                      { return __riscv_vfcvt_x_f_v_i32m1(a, VL4); }
inline f32x4 cvt(i32x4 a)                             { return __riscv_vfcvt_f_x_v_f32m1(a, VL4); }
inline f32x4 cvt(u32x4 a)                             { return __riscv_vfcvt_f_xu_v_f32m1(a, VL4); }

// Compare (return u32x4 all-ones / all-zeros per lane)
inline u32x4 cmpeq(f32x4 a, f32x4 b) {
    auto m = __riscv_vmfeq_vv_f32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}
inline u32x4 cmplt(f32x4 a, f32x4 b) {
    auto m = __riscv_vmflt_vv_f32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}
inline u32x4 cmple(f32x4 a, f32x4 b) {
    auto m = __riscv_vmfle_vv_f32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}
inline u32x4 cmpgt(f32x4 a, f32x4 b) {
    auto m = __riscv_vmfgt_vv_f32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}
inline u32x4 cmpge(f32x4 a, f32x4 b) {
    auto m = __riscv_vmfge_vv_f32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}
inline u32x4 cmpeq(i32x4 a, i32x4 b) {
    auto m = __riscv_vmseq_vv_i32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}
inline u32x4 cmplt(i32x4 a, i32x4 b) {
    auto m = __riscv_vmslt_vv_i32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}
inline u32x4 cmpgt(i32x4 a, i32x4 b) {
    auto m = __riscv_vmsgt_vv_i32m1_b32(a, b, VL4);
    return __riscv_vmerge_vxm_u32m1(__riscv_vmv_v_x_u32m1(0, VL4), 0xFFFFFFFFu, m, VL4);
}

// Bitwise
inline u32x4 bit_and(u32x4 a, u32x4 b)               { return __riscv_vand_vv_u32m1(a, b, VL4); }
inline u32x4 bit_or(u32x4 a, u32x4 b)                { return __riscv_vor_vv_u32m1(a, b, VL4); }
inline u32x4 bit_xor(u32x4 a, u32x4 b)               { return __riscv_vxor_vv_u32m1(a, b, VL4); }
inline u32x4 bit_andnot(u32x4 a, u32x4 b) {
    return __riscv_vand_vv_u32m1(__riscv_vnot_v_u32m1(a, VL4), b, VL4);
}
inline u32x4 bit_not(u32x4 a)                         { return __riscv_vnot_v_u32m1(a, VL4); }

inline f32x4 bit_and(f32x4 a, f32x4 b) {
    return __riscv_vreinterpret_v_u32m1_f32m1(
        __riscv_vand_vv_u32m1(__riscv_vreinterpret_v_f32m1_u32m1(a),
                              __riscv_vreinterpret_v_f32m1_u32m1(b), VL4));
}
inline f32x4 bit_or(f32x4 a, f32x4 b) {
    return __riscv_vreinterpret_v_u32m1_f32m1(
        __riscv_vor_vv_u32m1(__riscv_vreinterpret_v_f32m1_u32m1(a),
                             __riscv_vreinterpret_v_f32m1_u32m1(b), VL4));
}
inline f32x4 bit_xor(f32x4 a, f32x4 b) {
    return __riscv_vreinterpret_v_u32m1_f32m1(
        __riscv_vxor_vv_u32m1(__riscv_vreinterpret_v_f32m1_u32m1(a),
                              __riscv_vreinterpret_v_f32m1_u32m1(b), VL4));
}

// Blend: mask ? b : a (mask is all-ones or all-zeros per lane)
inline f32x4 blend(f32x4 a, f32x4 b, u32x4 m) {
    auto mask = __riscv_vmsne_vx_u32m1_b32(m, 0, VL4);
    return __riscv_vmerge_vvm_f32m1(a, b, mask, VL4);
}
inline i32x4 blend(i32x4 a, i32x4 b, u32x4 m) {
    auto mask = __riscv_vmsne_vx_u32m1_b32(m, 0, VL4);
    return __riscv_vmerge_vvm_i32m1(a, b, mask, VL4);
}
inline u32x4 blend(u32x4 a, u32x4 b, u32x4 m) {
    auto mask = __riscv_vmsne_vx_u32m1_b32(m, 0, VL4);
    return __riscv_vmerge_vvm_u32m1(a, b, mask, VL4);
}

// Shift
template<int N> inline i32x4 shl(i32x4 a)             { return __riscv_vsll_vx_i32m1(a, N, VL4); }
template<int N> inline i32x4 shr(i32x4 a)             { return __riscv_vsra_vx_i32m1(a, N, VL4); }
template<int N> inline u32x4 shl(u32x4 a)             { return __riscv_vsll_vx_u32m1(a, N, VL4); }
template<int N> inline u32x4 shr(u32x4 a)             { return __riscv_vsrl_vx_u32m1(a, N, VL4); }

// Reinterpret casts
inline f32x4 as_f32(u32x4 a)                          { return __riscv_vreinterpret_v_u32m1_f32m1(a); }
inline f32x4 as_f32(i32x4 a)                          { return __riscv_vreinterpret_v_i32m1_f32m1(a); }
inline u32x4 as_u32(f32x4 a)                          { return __riscv_vreinterpret_v_f32m1_u32m1(a); }
inline u32x4 as_u32(i32x4 a)                          { return __riscv_vreinterpret_v_i32m1_u32m1(a); }
inline i32x4 as_i32(f32x4 a)                          { return __riscv_vreinterpret_v_f32m1_i32m1(a); }
inline i32x4 as_i32(u32x4 a)                          { return __riscv_vreinterpret_v_u32m1_i32m1(a); }

// Lane access
template<int I> inline float extract(f32x4 v) {
    if constexpr (I == 0) return __riscv_vfmv_f_s_f32m1_f32(v);
    else return __riscv_vfmv_f_s_f32m1_f32(__riscv_vslidedown_vx_f32m1(v, I, VL4));
}
template<int I> inline f32x4 insert(f32x4 v, float x) {
    auto mask = __riscv_vmseq_vx_u32m1_b32(__riscv_vid_v_u32m1(VL4), I, VL4);
    return __riscv_vfmerge_vfm_f32m1(v, x, mask, VL4);
}
template<int I> inline int32_t extract(i32x4 v) {
    if constexpr (I == 0) return __riscv_vmv_x_s_i32m1_i32(v);
    else return __riscv_vmv_x_s_i32m1_i32(__riscv_vslidedown_vx_i32m1(v, I, VL4));
}
template<int I> inline uint32_t extract(u32x4 v) {
    if constexpr (I == 0) return __riscv_vmv_x_s_u32m1_u32(v);
    else return __riscv_vmv_x_s_u32m1_u32(__riscv_vslidedown_vx_u32m1(v, I, VL4));
}

// Horizontal
inline float hmin(f32x4 a) {
    auto init = __riscv_vfmv_v_f_f32m1(__builtin_huge_valf(), VL4);
    return __riscv_vfmv_f_s_f32m1_f32(__riscv_vfredmin_vs_f32m1_f32m1(a, init, VL4));
}
inline float hmax(f32x4 a) {
    auto init = __riscv_vfmv_v_f_f32m1(-__builtin_huge_valf(), VL4);
    return __riscv_vfmv_f_s_f32m1_f32(__riscv_vfredmax_vs_f32m1_f32m1(a, init, VL4));
}
inline float hsum(f32x4 a) {
    auto init = __riscv_vfmv_v_f_f32m1(0.0f, VL4);
    return __riscv_vfmv_f_s_f32m1_f32(__riscv_vfredusum_vs_f32m1_f32m1(a, init, VL4));
}
inline int32_t hsum(i32x4 a) {
    auto init = __riscv_vmv_v_x_i32m1(0, VL4);
    return __riscv_vmv_x_s_i32m1_i32(__riscv_vredsum_vs_i32m1_i32m1(a, init, VL4));
}
inline uint32_t hsum(u32x4 a) {
    auto init = __riscv_vmv_v_x_u32m1(0, VL4);
    return __riscv_vmv_x_s_u32m1_u32(__riscv_vredsum_vs_u32m1_u32m1(a, init, VL4));
}

// Mask queries
inline bool all(u32x4 m) {
    auto mask = __riscv_vmsne_vx_u32m1_b32(m, 0, VL4);
    return __riscv_vcpop_m_b32(mask, VL4) == VL4;
}
inline bool any(u32x4 m) {
    auto mask = __riscv_vmsne_vx_u32m1_b32(m, 0, VL4);
    return __riscv_vcpop_m_b32(mask, VL4) != 0;
}
inline bool none(u32x4 m) {
    auto mask = __riscv_vmsne_vx_u32m1_b32(m, 0, VL4);
    return __riscv_vcpop_m_b32(mask, VL4) == 0;
}

// Sliding window: [a1,a2,a3,0] and [0,a0,a1,a2]
inline f32x4 shift_left_1(f32x4 a) {
    return __riscv_vslidedown_vx_f32m1(a, 1, VL4);
}
inline f32x4 shift_right_1(f32x4 a) {
    auto z = __riscv_vfmv_v_f_f32m1(0.0f, VL4);
    return __riscv_vslideup_vx_f32m1(z, a, 1, VL4);
}

// Prefix sum
inline f32x4 prefix_sum(f32x4 v) {
    auto z = __riscv_vfmv_v_f_f32m1(0.0f, VL4);
    v = __riscv_vfadd_vv_f32m1(v, __riscv_vslideup_vx_f32m1(z, v, 1, VL4), VL4);
    v = __riscv_vfadd_vv_f32m1(v, __riscv_vslideup_vx_f32m1(z, v, 2, VL4), VL4);
    return v;
}

// Pixel pack / unpack
inline u32x4 unpack_u8_to_u32(u8x16 v) {
    auto v8 = __riscv_vlmul_trunc_v_u8m1_u8mf4(v);
    auto v16 = __riscv_vzext_vf2_u16mf2(v8, VL4);
    return __riscv_vzext_vf2_u32m1(v16, VL4);
}
inline u8x16 pack_u32_to_u8(u32x4 v) {
    // GCC 13 vnclipu: (src, shift, vl) — no explicit xrm, uses vxrm CSR (default truncate)
    auto v16 = __riscv_vnclipu_wx_u16mf2(v, 0, VL4);
    auto v8  = __riscv_vnclipu_wx_u8mf4(v16, 0, VL4);
    return __riscv_vlmul_ext_v_u8mf4_u8m1(v8);
}
inline void widen_u8_to_u16(u8x16 v, u16x8& lo, u16x8& hi) {
    auto lo8 = __riscv_vlmul_trunc_v_u8m1_u8mf2(v);
    lo = __riscv_vzext_vf2_u16m1(lo8, VL8);
    auto hi8 = __riscv_vlmul_trunc_v_u8m1_u8mf2(__riscv_vslidedown_vx_u8m1(v, 8, VL16));
    hi = __riscv_vzext_vf2_u16m1(hi8, VL8);
}

// Saturating u8 arithmetic
inline u8x16 adds_u8(u8x16 a, u8x16 b)              { return __riscv_vsaddu_vv_u8m1(a, b, VL16); }
inline u8x16 subs_u8(u8x16 a, u8x16 b)              { return __riscv_vssubu_vv_u8m1(a, b, VL16); }

// Shuffle
inline f32x4 reverse(f32x4 v) {
    auto idx = __riscv_vrsub_vx_u32m1(__riscv_vid_v_u32m1(VL4), 3, VL4);
    return __riscv_vrgather_vv_f32m1(v, idx, VL4);
}
inline u32x4 reverse(u32x4 v) {
    auto idx = __riscv_vrsub_vx_u32m1(__riscv_vid_v_u32m1(VL4), 3, VL4);
    return __riscv_vrgather_vv_u32m1(v, idx, VL4);
}
inline i32x4 reverse(i32x4 v) {
    auto idx = __riscv_vrsub_vx_u32m1(__riscv_vid_v_u32m1(VL4), 3, VL4);
    return __riscv_vrgather_vv_i32m1(v, idx, VL4);
}
template<int I> inline f32x4 broadcast_lane(f32x4 v) {
    return __riscv_vrgather_vx_f32m1(v, I, VL4);
}
template<int I> inline i32x4 broadcast_lane(i32x4 v) {
    return __riscv_vrgather_vx_i32m1(v, I, VL4);
}
template<int I> inline u32x4 broadcast_lane(u32x4 v) {
    return __riscv_vrgather_vx_u32m1(v, I, VL4);
}

// 4 pixels RGBA (R in byte 0). Each uint32 = 0xAABBGGRR.
// Deinterleave to float [0..1] per channel.
inline void unpack_rgba(const uint8_t* pixel, f32x4& r, f32x4& g, f32x4& b, f32x4& a) {
    auto raw = __riscv_vle32_v_u32m1(reinterpret_cast<const uint32_t*>(pixel), VL4);
    auto scale = __riscv_vfmv_v_f_f32m1(1.0f / 255.0f, VL4);
    auto mask8 = __riscv_vmv_v_x_u32m1(0xFF, VL4);
    r = __riscv_vfmul_vv_f32m1(__riscv_vfcvt_f_xu_v_f32m1(__riscv_vand_vv_u32m1(raw, mask8, VL4), VL4), scale, VL4);
    g = __riscv_vfmul_vv_f32m1(__riscv_vfcvt_f_xu_v_f32m1(__riscv_vand_vv_u32m1(__riscv_vsrl_vx_u32m1(raw, 8, VL4), mask8, VL4), VL4), scale, VL4);
    b = __riscv_vfmul_vv_f32m1(__riscv_vfcvt_f_xu_v_f32m1(__riscv_vand_vv_u32m1(__riscv_vsrl_vx_u32m1(raw, 16, VL4), mask8, VL4), VL4), scale, VL4);
    a = __riscv_vfmul_vv_f32m1(__riscv_vfcvt_f_xu_v_f32m1(__riscv_vsrl_vx_u32m1(raw, 24, VL4), VL4), scale, VL4);
}

// Cross product 2D: ax*by - ay*bx
inline f32x4 cross2d(f32x4 ax, f32x4 ay, f32x4 bx, f32x4 by) {
    return __riscv_vfnmsac_vv_f32m1(__riscv_vfmul_vv_f32m1(ax, by, VL4), ay, bx, VL4);
}

// Lerp: a + t*(b - a)
inline f32x4 lerp(f32x4 a, f32x4 b, f32x4 t) {
    return __riscv_vfmacc_vv_f32m1(a, t, __riscv_vfsub_vv_f32m1(b, a, VL4), VL4);
}

// Color matrix: r*cr + g*cg + b*cb
inline f32x4 dot3(f32x4 r, f32x4 g, f32x4 b, f32x4 cr, f32x4 cg, f32x4 cb) {
    auto result = __riscv_vfmul_vv_f32m1(r, cr, VL4);
    result = __riscv_vfmacc_vv_f32m1(result, g, cg, VL4);
    result = __riscv_vfmacc_vv_f32m1(result, b, cb, VL4);
    return result;
}

// sRGB <-> linear (fast approximation)
inline f32x4 srgb_to_linear(f32x4 v) {
    return __riscv_vfmul_vv_f32m1(
        __riscv_vfmul_vv_f32m1(v, v, VL4),
        __riscv_vfsqrt_v_f32m1(__riscv_vfsqrt_v_f32m1(v, VL4), VL4), VL4);
}
inline f32x4 linear_to_srgb(f32x4 v) {
    auto vsq  = __riscv_vfsqrt_v_f32m1(v, VL4);
    auto vqrt = __riscv_vfsqrt_v_f32m1(vsq, VL4);
    return __riscv_vfmacc_vv_f32m1(
        __riscv_vfmul_vf_f32m1(vsq, 0.82f, VL4),
        vqrt, __riscv_vfmv_v_f_f32m1(0.18f, VL4), VL4);
}

// sRGB -> linear, precise piecewise (max absolute error < 0.002 vs true sRGB).
// Low: x / 12.92. High: pow((x + 0.055) / 1.055, 2.4) via t^2.5 * poly(t).
inline f32x4 srgb_to_linear_precise(f32x4 x) {
    auto lo = __riscv_vfmul_vf_f32m1(x, 1.0f / 12.92f, VL4);
    auto t  = __riscv_vfmul_vf_f32m1(
        __riscv_vfadd_vf_f32m1(x, 0.055f, VL4), 1.0f / 1.055f, VL4);
    auto t2  = __riscv_vfmul_vv_f32m1(t, t, VL4);
    auto t25 = __riscv_vfmul_vv_f32m1(t2, __riscv_vfsqrt_v_f32m1(t, VL4), VL4);
    auto corr = __riscv_vfmacc_vf_f32m1(
        __riscv_vfmv_v_f_f32m1(-0.6866f, VL4), 0.3617f, t, VL4);
    corr = __riscv_vfmacc_vv_f32m1(
        __riscv_vfmv_v_f_f32m1(1.3249f, VL4), corr, t, VL4);
    auto hi = __riscv_vfmul_vv_f32m1(t25, corr, VL4);
    auto mask = __riscv_vmfle_vf_f32m1_b32(x, 0.04045f, VL4);
    return __riscv_vmerge_vvm_f32m1(hi, lo, mask, VL4);
}

// Premultiply alpha
inline void premultiply(f32x4 r, f32x4 g, f32x4 b, f32x4 a,
                         f32x4& pr, f32x4& pg, f32x4& pb) {
    pr = __riscv_vfmul_vv_f32m1(r, a, VL4);
    pg = __riscv_vfmul_vv_f32m1(g, a, VL4);
    pb = __riscv_vfmul_vv_f32m1(b, a, VL4);
}

// Unpremultiply alpha
inline void unpremultiply(f32x4 pr, f32x4 pg, f32x4 pb, f32x4 a,
                           f32x4& r, f32x4& g, f32x4& b) {
    auto est = __riscv_vfrec7_v_f32m1(a, VL4);
    auto inv_a = __riscv_vfmul_vv_f32m1(est,
        __riscv_vfrsub_vf_f32m1(__riscv_vfmul_vv_f32m1(a, est, VL4), 2.0f, VL4), VL4);
    auto mask = __riscv_vmfgt_vf_f32m1_b32(a, 0.0f, VL4);
    inv_a = __riscv_vfmerge_vfm_f32m1(inv_a, 0.0f, __riscv_vmnot_m_b32(mask, VL4), VL4);
    r = __riscv_vfmul_vv_f32m1(pr, inv_a, VL4);
    g = __riscv_vfmul_vv_f32m1(pg, inv_a, VL4);
    b = __riscv_vfmul_vv_f32m1(pb, inv_a, VL4);
}

#endif

// Compositing primitives (architecture-independent)
//
// Pixel format: ARGB packed as 0xAARRGGBB (alpha in bits 31..24, blue in bits 7..0).
// All compositing operates on 4 premultiplied ARGB pixels in parallel.

// Fixed-point alpha multiply: (x * a + 128) / 255, per channel. a is [0..255] per lane.
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

// Unpack 4 ARGB pixels (0xAARRGGBB, B in byte 0) -> float [0..1] channels.
inline void unpack_argb(const uint32_t* pixels, f32x4& r, f32x4& g, f32x4& b, f32x4& a) {
    auto raw = as_u32(load(reinterpret_cast<const int32_t*>(pixels)));
    auto scale = splat(1.0f / 255.0f);
    auto mask8 = splat(0xFFu);
    b = mul(cvt(as_i32(bit_and(raw, mask8))), scale);
    g = mul(cvt(as_i32(bit_and(shr<8>(raw), mask8))), scale);
    r = mul(cvt(as_i32(bit_and(shr<16>(raw), mask8))), scale);
    a = mul(cvt(as_i32(shr<24>(raw))), scale);
}

// Pack float [0..1] channels -> 4 ARGB pixels (0xAARRGGBB, B in byte 0).
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

// Pixel format swaps — generic fallback for architectures without byte-level shuffle.
// x86_64 provides pshufb-optimized overrides above.
#if !defined(__x86_64__)

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

#endif // !__x86_64__

} // namespace kosmsimd
