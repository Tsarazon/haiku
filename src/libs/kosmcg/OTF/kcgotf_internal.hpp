// kcgotf_internal.hpp - shared helpers across kcgotf TUs
// C++20, userspace, not part of public API

#pragma once

#include "kcgotf.hpp"

namespace kcg::otf::detail {

// Coverage table lookup. Returns coverage index, or -1 if glyph not present.
int coverage_index(const uint8_t* d, size_t dsz,
                    uint32_t cov_off, uint16_t glyph);

// ClassDef lookup. Returns class value (0 = default class).
uint16_t classdef_lookup(const uint8_t* d, size_t dsz,
                          uint32_t cd_off, uint16_t glyph);

// ValueRecord helpers (GPOS).
int value_record_size(uint16_t fmt);
int extract_xadvance(const uint8_t* d, size_t dsz,
                      size_t rec_off, uint16_t vfmt);

// F2DOT14 fixed-point as float.
inline float read_f2dot14(const uint8_t* p) {
    return static_cast<float>(read_i16(p)) / 16384.0f;
}

/// Resolve a GSUB/GPOS LookupType 9/7 (Extension Position/Substitution) subtable.
/// `expected_inner_type` is the original lookup type (e.g. 4 for ligatures).
/// Returns resolved subtable offset, or 0 if not applicable/invalid.
uint32_t resolve_extension(const uint8_t* d, size_t dsz,
                            uint32_t subtable, uint16_t expected_inner_type);

// ItemVariationStore

/// Compute a delta value from an IVS table for the given outer/inner indices,
/// using the supplied normalized variation coordinates.
float ivs_delta(const uint8_t* d, size_t dsz, uint32_t ivs_off,
                 uint32_t outer, uint32_t inner,
                 std::span<const float> norm_coords);

/// Result of a DeltaSetIndexMap lookup.
struct IvsIndex {
    uint32_t outer = 0;
    uint32_t inner = 0;
};

/// Look up a (outer, inner) pair from a DeltaSetIndexMap.
IvsIndex delta_set_index(const uint8_t* d, size_t dsz,
                           uint32_t map_off, uint32_t entry_index);

} // namespace kcg::otf::detail

// CFF helpers — implementation details for the outline interpreter.
// Moved out of public hpp; only the outline TU and core (for init) need these.

namespace kcg::otf::cff {

inline CFFBuffer make_buf(const void* p, int size) {
    if (size < 0 || size >= 0x40000000) size = 0;
    return {static_cast<const uint8_t*>(p), 0, size};
}

inline CFFBuffer buf_range(const CFFBuffer& b, int offset, int len) {
    CFFBuffer r{};
    if (offset < 0 || len < 0 || offset > b.size || len > b.size - offset) return r;
    r.data = b.data + offset;
    r.size = len;
    return r;
}

inline uint8_t buf_get8(CFFBuffer& b) {
    if (b.cursor >= b.size) return 0;
    return b.data[b.cursor++];
}

inline uint8_t buf_peek8(const CFFBuffer& b) {
    if (b.cursor >= b.size) return 0;
    return b.data[b.cursor];
}

inline void buf_seek(CFFBuffer& b, int o) {
    b.cursor = (o > b.size || o < 0) ? b.size : o;
}

inline void buf_skip(CFFBuffer& b, int o) {
    buf_seek(b, b.cursor + o);
}

inline uint32_t buf_get(CFFBuffer& b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v = (v << 8) | buf_get8(b);
    return v;
}

inline uint16_t buf_get16(CFFBuffer& b) { return static_cast<uint16_t>(buf_get(b, 2)); }
inline uint32_t buf_get32(CFFBuffer& b) { return buf_get(b, 4); }

CFFBuffer get_index(CFFBuffer& b);
int index_count(CFFBuffer& b);
CFFBuffer index_get(CFFBuffer b, int i);
uint32_t dict_int(CFFBuffer& b);
void dict_skip_operand(CFFBuffer& b);
CFFBuffer dict_get(CFFBuffer& b, int key);
void dict_get_ints(CFFBuffer& b, int key, int outcount, uint32_t* out);
CFFBuffer get_subrs(const CFFBuffer& cff_buf, CFFBuffer fontdict);

} // namespace kcg::otf::cff
