// KosmSimd.h — KosmOS Support Kit
// Portable SIMD primitives: ARM64 (NEON) / x86_64 (SSE4.1) / RISC-V 64 (RVV 1.0)
//
// Structure:
//   1. Platform backend (types, load/store, arithmetic, bitwise, shuffle ...)
//      — x86_64 (SSE4.1)
//      — ARM64 (NEON)
//      — RISC-V 64 (RVV 1.0)
//   2. Architecture-independent helpers (math, color, alpha, pixel pack/unpack ...)
//   3. Compositing and blend modes

#pragma once

#include <cstdint>

#if defined(__x86_64__)
#include <smmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__riscv_v)
#include <riscv_vector.h>
#else
#error "Unsupported architecture"
#endif

namespace kosmsimd {

inline constexpr int width = 4;

// 1. Platform backend — x86_64 (SSE4.1)

#if defined(__x86_64__)

// Wrapper structs for type safety (__m128i is shared by i32/u32/u16/u8)

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

// Pixel format swaps (pshufb — single-instruction byte permute)

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

// BGRA → RGBA: 0xBBGGRRAA → 0xRRGGBBAA (swap R and B, keep G and A)
inline u32x4 bgra_to_rgba(u32x4 px) {
    const __m128i shuf = _mm_setr_epi8(0,3,2,1, 4,7,6,5, 8,11,10,9, 12,15,14,13);
    return _mm_shuffle_epi8(px.v, shuf);
}

// RGBA → BGRA: same swap (self-inverse)
inline u32x4 rgba_to_bgra(u32x4 px) {
    return bgra_to_rgba(px);
}

// 1. Platform backend — ARM64 (NEON)

#elif defined(__aarch64__)

// Native NEON types (already distinct, no wrappers needed)

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

// 1. Platform backend — RISC-V 64 (RVV 1.0)

#elif defined(__riscv_v)

// Fixed vector lengths matching NEON/SSE lane counts

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

#endif // platform backend

// 2. Architecture-independent helpers

// Math

// Cross product 2D: ax*by - ay*bx
inline f32x4 cross2d(f32x4 ax, f32x4 ay, f32x4 bx, f32x4 by) {
    return sub(mul(ax, by), mul(ay, bx));
}

// Lerp: a + t*(b - a)
inline f32x4 lerp(f32x4 a, f32x4 b, f32x4 t) {
    return add(a, mul(t, sub(b, a)));
}

// Color matrix: r*cr + g*cg + b*cb
inline f32x4 dot3(f32x4 r, f32x4 g, f32x4 b, f32x4 cr, f32x4 cg, f32x4 cb) {
    return add(mul(r, cr), add(mul(g, cg), mul(b, cb)));
}

// 4-component dot: r*cr + g*cg + b*cb + a*ca
inline f32x4 dot4(f32x4 r, f32x4 g, f32x4 b, f32x4 a,
                   f32x4 cr, f32x4 cg, f32x4 cb, f32x4 ca) {
    return add(add(mul(r, cr), mul(g, cg)), add(mul(b, cb), mul(a, ca)));
}

// Clamp to [0, 1]
inline f32x4 clamp01(f32x4 v) {
    return clamp(v, zero_f32(), splat(1.0f));
}

// Bilinear interpolation of 4 taps at fractional position (fx, fy)
inline f32x4 bilerp(f32x4 tl, f32x4 tr, f32x4 bl, f32x4 br, f32x4 fx, f32x4 fy) {
    return lerp(lerp(tl, tr, fx), lerp(bl, br, fx), fy);
}

// Color space

// sRGB <-> linear (fast approximation)
inline f32x4 srgb_to_linear(f32x4 v) {
    return mul(mul(v, v), sqrt(sqrt(v)));
}
inline f32x4 linear_to_srgb(f32x4 v) {
    auto vsq  = sqrt(v);
    auto vqrt = sqrt(vsq);
    return add(mul(splat(0.82f), vsq), mul(splat(0.18f), vqrt));
}

// sRGB -> linear, precise piecewise (max absolute error < 0.002 vs true sRGB).
// Low: x / 12.92. High: pow((x + 0.055) / 1.055, 2.4) via t^2.5 * poly(t).
inline f32x4 srgb_to_linear_precise(f32x4 x) {
    auto lo = mul(x, splat(1.0f / 12.92f));
    auto t  = mul(add(x, splat(0.055f)), splat(1.0f / 1.055f));
    auto t2  = mul(t, t);
    auto t25 = mul(t2, sqrt(t));
    auto corr = add(mul(splat(0.3617f), t), splat(-0.6866f));
    corr = add(mul(corr, t), splat(1.3249f));
    auto hi = mul(t25, corr);
    auto mask = cmple(x, splat(0.04045f));
    return blend(hi, lo, mask);
}

// linear -> sRGB, precise piecewise (matches srgb_to_linear_precise roundtrip).
// Low: x * 12.92. High: 1.055 * x^(1/2.4) - 0.055 via iterated Newton cbrt.
inline f32x4 linear_to_srgb_precise(f32x4 x) {
    auto lo = mul(x, splat(12.92f));

    auto one_third = splat(1.0f / 3.0f);
    auto two = splat(2.0f);
    auto s = sqrt(x);
    auto q = sqrt(s);

    // cbrt(x): Newton from seed x^(1/4), 3 iterations
    auto z = q;
    z = mul(add(mul(two, z), div(x, mul(z, z))), one_third);
    z = mul(add(mul(two, z), div(x, mul(z, z))), one_third);
    z = mul(add(mul(two, z), div(x, mul(z, z))), one_third);

    // cbrt(x^(1/4)): Newton from seed x^(1/8), 3 iterations
    auto w = sqrt(q);
    w = mul(add(mul(two, w), div(q, mul(w, w))), one_third);
    w = mul(add(mul(two, w), div(q, mul(w, w))), one_third);
    w = mul(add(mul(two, w), div(q, mul(w, w))), one_third);

    // x^(5/12) = x^(1/3) * x^(1/12) = cbrt(x) * cbrt(x^(1/4))
    auto hi = sub(mul(splat(1.055f), mul(z, w)), splat(0.055f));

    auto mask = cmple(x, splat(0.0031308f));
    return blend(hi, lo, mask);
}

// Alpha

// Premultiply alpha
inline void premultiply(f32x4 r, f32x4 g, f32x4 b, f32x4 a,
                         f32x4& pr, f32x4& pg, f32x4& pb) {
    pr = mul(r, a);
    pg = mul(g, a);
    pb = mul(b, a);
}

// Unpremultiply alpha
inline void unpremultiply(f32x4 pr, f32x4 pg, f32x4 pb, f32x4 a,
                           f32x4& r, f32x4& g, f32x4& b) {
    auto inv_a = rcp(a);
    auto mask = cmpgt(a, zero_f32());
    inv_a = as_f32(bit_and(as_u32(inv_a), mask));
    r = mul(pr, inv_a);
    g = mul(pg, inv_a);
    b = mul(pb, inv_a);
}

// Extract alpha from 4 packed ARGB pixels.
inline u32x4 alpha_x4(u32x4 pixels) {
    return shr<24>(pixels);
}

// Pixel pack / unpack

// 4 pixels RGBA (R in byte 0). Each uint32 = 0xAABBGGRR.
// Deinterleave to float [0..1] per channel.
inline void unpack_rgba(const uint8_t* pixel, f32x4& r, f32x4& g, f32x4& b, f32x4& a) {
    auto raw   = load(reinterpret_cast<const uint32_t*>(pixel));
    auto scale = splat(1.0f / 255.0f);
    auto mask8 = splat(0xFFu);
    r = mul(cvt(as_i32(bit_and(raw, mask8))), scale);
    g = mul(cvt(as_i32(bit_and(shr<8>(raw), mask8))), scale);
    b = mul(cvt(as_i32(bit_and(shr<16>(raw), mask8))), scale);
    a = mul(cvt(as_i32(shr<24>(raw))), scale);
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

// Unpack 4 BGRA pixels (0xBBGGRRAA, A in byte 0) -> float [0..1] channels.
inline void unpack_bgra(const uint32_t* pixels, f32x4& r, f32x4& g, f32x4& b, f32x4& a) {
    auto raw = as_u32(load(reinterpret_cast<const int32_t*>(pixels)));
    auto scale = splat(1.0f / 255.0f);
    auto mask8 = splat(0xFFu);
    a = mul(cvt(as_i32(bit_and(raw, mask8))), scale);
    r = mul(cvt(as_i32(bit_and(shr<8>(raw), mask8))), scale);
    g = mul(cvt(as_i32(bit_and(shr<16>(raw), mask8))), scale);
    b = mul(cvt(as_i32(shr<24>(raw))), scale);
}

// Pack float [0..1] channels -> 4 RGBA pixels (0xRRGGBBAA, A in byte 0).
inline u32x4 pack_rgba(f32x4 r, f32x4 g, f32x4 b, f32x4 a) {
    auto scale = splat(255.0f);
    auto lo = zero_f32();
    auto ai = as_u32(cvt_round(clamp(mul(a, scale), lo, scale)));
    auto ri = as_u32(cvt_round(clamp(mul(r, scale), lo, scale)));
    auto gi = as_u32(cvt_round(clamp(mul(g, scale), lo, scale)));
    auto bi = as_u32(cvt_round(clamp(mul(b, scale), lo, scale)));
    return bit_or(bit_or(shl<24>(ri), shl<16>(gi)),
                  bit_or(shl<8>(bi), ai));
}

// Pack float [0..1] channels -> 4 BGRA pixels (0xBBGGRRAA, A in byte 0).
inline u32x4 pack_bgra(f32x4 r, f32x4 g, f32x4 b, f32x4 a) {
    auto scale = splat(255.0f);
    auto lo = zero_f32();
    auto ai = as_u32(cvt_round(clamp(mul(a, scale), lo, scale)));
    auto ri = as_u32(cvt_round(clamp(mul(r, scale), lo, scale)));
    auto gi = as_u32(cvt_round(clamp(mul(g, scale), lo, scale)));
    auto bi = as_u32(cvt_round(clamp(mul(b, scale), lo, scale)));
    return bit_or(bit_or(shl<24>(bi), shl<16>(gi)),
                  bit_or(shl<8>(ri), ai));
}

// Pixel format swaps (generic bit-shift fallback for ARM64 and RISC-V)
#if !defined(__x86_64__)

// ARGB -> BGRA: 0xAARRGGBB -> 0xBBGGRRAA (byte reverse per lane).
inline u32x4 argb_to_bgra(u32x4 px) {
    return bit_or(
        bit_or(shl<24>(px), bit_and(shl<8>(px), splat(0x00FF0000u))),
        bit_or(bit_and(shr<8>(px), splat(0x0000FF00u)), shr<24>(px))
    );
}

// ARGB -> RGBA: 0xAARRGGBB -> 0xRRGGBBAA (rotate left 8 bits).
inline u32x4 argb_to_rgba(u32x4 px) {
    return bit_or(shl<8>(px), shr<24>(px));
}

// RGBA -> ARGB: 0xRRGGBBAA -> 0xAARRGGBB (rotate right 8 bits).
inline u32x4 rgba_to_argb(u32x4 px) {
    return bit_or(shr<8>(px), shl<24>(px));
}

// BGRA -> ARGB: byte reverse (self-inverse).
inline u32x4 bgra_to_argb(u32x4 px) {
    return argb_to_bgra(px);
}

// BGRA -> RGBA: 0xBBGGRRAA -> 0xRRGGBBAA (swap R and B, keep G and A).
inline u32x4 bgra_to_rgba(u32x4 px) {
    auto hi = shr<16>(bit_and(px, splat(0xFF000000u)));
    auto lo = shl<16>(bit_and(px, splat(0x0000FF00u)));
    return bit_or(bit_or(hi, lo), bit_and(px, splat(0x00FF00FFu)));
}

// RGBA -> BGRA: same swap (self-inverse).
inline u32x4 rgba_to_bgra(u32x4 px) {
    return bgra_to_rgba(px);
}

#endif // !__x86_64__

// 3. Compositing and blend modes

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

// Affine transform
//
// Batch-transform 4 points (SoA) by an affine matrix.
// | a  c  tx |   | x |   | a*x + c*y + tx |
// | b  d  ty | × | y | = | b*x + d*y + ty |

inline void affine_apply_x4(f32x4 x, f32x4 y,
                              float a, float b, float c, float d, float tx, float ty,
                              f32x4& ox, f32x4& oy) {
    ox = add(add(mul(splat(a), x), mul(splat(c), y)), splat(tx));
    oy = add(add(mul(splat(b), x), mul(splat(d), y)), splat(ty));
}

// Color matrix
//
// Apply a 4×5 color matrix to RGBA channels. Matrix layout (row-major):
// m[0..4]   = R row:  R' = m[0]*R + m[1]*G + m[2]*B + m[3]*A + m[4]
// m[5..9]   = G row:  G' = m[5]*R + m[6]*G + m[7]*B + m[8]*A + m[9]
// m[10..14] = B row:  B' = m[10]*R + m[11]*G + m[12]*B + m[13]*A + m[14]
// m[15..19] = A row:  A' = m[15]*R + m[16]*G + m[17]*B + m[18]*A + m[19]

inline void color_matrix_apply(f32x4& r, f32x4& g, f32x4& b, f32x4& a, const float m[20]) {
    auto r0 = r, g0 = g, b0 = b, a0 = a;
    r = add(dot4(r0, g0, b0, a0, splat(m[0]),  splat(m[1]),  splat(m[2]),  splat(m[3])),  splat(m[4]));
    g = add(dot4(r0, g0, b0, a0, splat(m[5]),  splat(m[6]),  splat(m[7]),  splat(m[8])),  splat(m[9]));
    b = add(dot4(r0, g0, b0, a0, splat(m[10]), splat(m[11]), splat(m[12]), splat(m[13])), splat(m[14]));
    a = add(dot4(r0, g0, b0, a0, splat(m[15]), splat(m[16]), splat(m[17]), splat(m[18])), splat(m[19]));
}

// Blend modes (separable)
//
// Per-channel blend functions on float [0..1]. These compute the raw blend
// result B(s, d); the caller is responsible for Porter-Duff alpha compositing:
//   out = (1-da)*sc + (1-sa)*dc + sa*da*B(sc/sa, dc/da)

inline f32x4 blend_multiply(f32x4 s, f32x4 d) {
    return mul(s, d);
}

inline f32x4 blend_screen(f32x4 s, f32x4 d) {
    return sub(add(s, d), mul(s, d));
}

inline f32x4 blend_darken(f32x4 s, f32x4 d) {
    return min(s, d);
}

inline f32x4 blend_lighten(f32x4 s, f32x4 d) {
    return max(s, d);
}

inline f32x4 blend_difference(f32x4 s, f32x4 d) {
    return abs(sub(s, d));
}

inline f32x4 blend_exclusion(f32x4 s, f32x4 d) {
    return sub(add(s, d), mul(splat(2.0f), mul(s, d)));
}

inline f32x4 blend_add(f32x4 s, f32x4 d) {
    return min(add(s, d), splat(1.0f));
}

// Overlay: 2*s*d if d < 0.5, else 1 - 2*(1-s)*(1-d)
inline f32x4 blend_overlay(f32x4 s, f32x4 d) {
    auto one = splat(1.0f);
    auto two = splat(2.0f);
    auto lo = mul(two, mul(s, d));
    auto hi = sub(one, mul(two, mul(sub(one, s), sub(one, d))));
    auto mask = cmplt(d, splat(0.5f));
    return blend(hi, lo, mask);
}

// HardLight: overlay with s and d swapped in the condition
inline f32x4 blend_hard_light(f32x4 s, f32x4 d) {
    auto one = splat(1.0f);
    auto two = splat(2.0f);
    auto lo = mul(two, mul(s, d));
    auto hi = sub(one, mul(two, mul(sub(one, s), sub(one, d))));
    auto mask = cmplt(s, splat(0.5f));
    return blend(hi, lo, mask);
}

// ColorDodge: d / (1-s), clamped
inline f32x4 blend_color_dodge(f32x4 s, f32x4 d) {
    auto one = splat(1.0f);
    auto inv_s = sub(one, s);
    auto result = min(div(d, inv_s), one);
    // s >= 1 && d > 0 -> 1.0
    auto s_one = cmpge(s, one);
    result = blend(result, one, s_one);
    // d == 0 -> 0 regardless of s
    auto d_zero = cmple(d, zero_f32());
    return blend(result, zero_f32(), d_zero);
}

// ColorBurn: 1 - (1-d) / s, clamped
inline f32x4 blend_color_burn(f32x4 s, f32x4 d) {
    auto one = splat(1.0f);
    auto zero = zero_f32();
    auto result = max(sub(one, div(sub(one, d), s)), zero);
    // s <= 0 && d < 1 -> 0.0
    auto s_zero = cmple(s, zero);
    result = blend(result, zero, s_zero);
    // d >= 1 -> 1 regardless of s
    auto d_one = cmpge(d, one);
    return blend(result, one, d_one);
}

// SoftLight (W3C compositing spec):
// Low (s <= 0.5):  d - (1-2s)*d*(1-d)
// High (s > 0.5):  d + (2s-1)*(D-d)
//   where D = sqrt(d) if d > 0.25, else ((16d-12)*d+4)*d
inline f32x4 blend_soft_light(f32x4 s, f32x4 d) {
    auto one = splat(1.0f);
    auto two_s = mul(splat(2.0f), s);

    auto lo = sub(d, mul(mul(sub(one, two_s), d), sub(one, d)));

    auto d_poly = mul(add(mul(sub(mul(splat(16.0f), d), splat(12.0f)), d), splat(4.0f)), d);
    auto d_sqrt = sqrt(d);
    auto d_mask = cmple(d, splat(0.25f));
    auto D = blend(d_sqrt, d_poly, d_mask);
    auto hi = add(d, mul(sub(two_s, one), sub(D, d)));

    auto s_mask = cmple(s, splat(0.5f));
    return blend(hi, lo, s_mask);
}

// SrcOver in float domain (non-premultiplied RGBA)
//
// Composites source over destination, returning result in or/og/ob/oa.
// Both inputs are non-premultiplied float [0..1] channels.
inline void src_over_f32(f32x4 sr, f32x4 sg, f32x4 sb, f32x4 sa,
                          f32x4 dr, f32x4 dg, f32x4 db, f32x4 da,
                          f32x4& or_, f32x4& og, f32x4& ob, f32x4& oa) {
    auto one_minus_sa = sub(splat(1.0f), sa);
    oa = add(sa, mul(da, one_minus_sa));
    auto inv_oa = rcp(oa);
    auto has_alpha = cmpgt(oa, zero_f32());
    inv_oa = as_f32(bit_and(as_u32(inv_oa), has_alpha));
    or_ = mul(add(mul(sr, sa), mul(mul(dr, da), one_minus_sa)), inv_oa);
    og  = mul(add(mul(sg, sa), mul(mul(dg, da), one_minus_sa)), inv_oa);
    ob  = mul(add(mul(sb, sa), mul(mul(db, da), one_minus_sa)), inv_oa);
}

} // namespace kosmsimd
