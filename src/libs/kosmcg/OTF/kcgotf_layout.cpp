// kcgotf_layout.cpp - GPOS kerning + GSUB ligature substitution
// C++20, userspace

#include "kcgotf.hpp"
#include "kcgotf_internal.hpp"

#include <algorithm>

namespace kcg::otf {

namespace detail {

int coverage_index(const uint8_t* d, size_t dsz, uint32_t cov_off, uint16_t glyph) {
    if (cov_off + 4 > dsz) return -1;
    uint16_t fmt = read_u16(d + cov_off);

    if (fmt == 1) {
        int count = read_u16(d + cov_off + 2);
        int lo = 0, hi = count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >> 1;
            size_t off = cov_off + 4 + static_cast<size_t>(mid) * 2;
            if (off + 2 > dsz) return -1;
            uint16_t gid = read_u16(d + off);
            if (glyph < gid) hi = mid - 1;
            else if (glyph > gid) lo = mid + 1;
            else return mid;
        }
    } else if (fmt == 2) {
        int range_count = read_u16(d + cov_off + 2);
        int lo = 0, hi = range_count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >> 1;
            size_t roff = cov_off + 4 + static_cast<size_t>(mid) * 6;
            if (roff + 6 > dsz) return -1;
            uint16_t rstart = read_u16(d + roff);
            uint16_t rend   = read_u16(d + roff + 2);
            if (glyph < rstart) hi = mid - 1;
            else if (glyph > rend) lo = mid + 1;
            else return read_u16(d + roff + 4) + (glyph - rstart);
        }
    }
    return -1;
}

uint16_t classdef_lookup(const uint8_t* d, size_t dsz, uint32_t cd_off, uint16_t glyph) {
    if (cd_off + 4 > dsz) return 0;
    uint16_t fmt = read_u16(d + cd_off);

    if (fmt == 1) {
        uint16_t start = read_u16(d + cd_off + 2);
        uint16_t count = read_u16(d + cd_off + 4);
        if (glyph < start || glyph >= start + count) return 0;
        size_t off = cd_off + 6 + static_cast<size_t>(glyph - start) * 2;
        if (off + 2 > dsz) return 0;
        return read_u16(d + off);
    } else if (fmt == 2) {
        uint16_t range_count = read_u16(d + cd_off + 2);
        int lo = 0, hi = range_count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >> 1;
            size_t roff = cd_off + 4 + static_cast<size_t>(mid) * 6;
            if (roff + 6 > dsz) return 0;
            uint16_t rstart = read_u16(d + roff);
            uint16_t rend   = read_u16(d + roff + 2);
            if (glyph < rstart) hi = mid - 1;
            else if (glyph > rend) lo = mid + 1;
            else return read_u16(d + roff + 4);
        }
    }
    return 0;
}

int value_record_size(uint16_t fmt) {
    int sz = 0;
    for (int bit = 0; bit < 8; ++bit)
        if (fmt & (1 << bit)) sz += 2;
    return sz;
}

int extract_xadvance(const uint8_t* d, size_t dsz, size_t rec_off, uint16_t vfmt) {
    if (!(vfmt & 0x04)) return 0;
    int byte_off = 0;
    if (vfmt & 0x01) byte_off += 2;
    if (vfmt & 0x02) byte_off += 2;
    size_t off = rec_off + byte_off;
    if (off + 2 > dsz) return 0;
    return read_i16(d + off);
}

uint32_t resolve_extension(const uint8_t* d, size_t dsz,
                            uint32_t subtable, uint16_t expected_inner_type) {
    if (subtable + 8 > dsz) return 0;
    uint16_t ext_fmt = read_u16(d + subtable);
    if (ext_fmt != 1) return 0;
    uint16_t ext_type = read_u16(d + subtable + 2);
    if (ext_type != expected_inner_type) return 0;
    return subtable + read_u32(d + subtable + 4);
}

// ItemVariationStore lookup. Shared between HVAR/MVAR (in core),
// gvar (in outlines), and COLR v1 var-paints (in color_var).

/// Compute per-region scalar from normalized coords and region triple.
static float region_scalar(const uint8_t* region_ptr, int axis_count,
                            std::span<const float> norm_coords) {
    float scalar = 1.0f;
    for (int a = 0; a < axis_count; ++a) {
        float start = read_f2dot14(region_ptr + a * 6);
        float peak  = read_f2dot14(region_ptr + a * 6 + 2);
        float end   = read_f2dot14(region_ptr + a * 6 + 4);
        float coord = (a < static_cast<int>(norm_coords.size())) ? norm_coords[a] : 0.0f;

        if (peak == 0.0f) continue;
        if (coord == 0.0f) return 0.0f;
        if (start > peak || peak > end) return 0.0f;
        if (coord == peak) continue;
        if (coord < start || coord > end) return 0.0f;
        if (coord < peak) {
            if (peak == start) return 0.0f;
            scalar *= (coord - start) / (peak - start);
        } else {
            if (end == peak) return 0.0f;
            scalar *= (end - coord) / (end - peak);
        }
    }
    return scalar;
}

float ivs_delta(const uint8_t* d, size_t dsz, uint32_t ivs_off,
                 uint32_t outer, uint32_t inner,
                 std::span<const float> norm_coords) {
    if (!ivs_off || ivs_off + 8 > dsz) return 0.0f;
    if (read_u16(d + ivs_off) != 1) return 0.0f;

    uint32_t region_list_off = ivs_off + read_u32(d + ivs_off + 2);
    uint16_t ivd_count = read_u16(d + ivs_off + 6);
    if (outer >= ivd_count) return 0.0f;

    if (ivs_off + 8 + (outer + 1) * 4 > dsz) return 0.0f;
    uint32_t ivd_off = ivs_off + read_u32(d + ivs_off + 8 + outer * 4);
    if (ivd_off + 6 > dsz) return 0.0f;

    if (region_list_off + 4 > dsz) return 0.0f;
    uint16_t axis_count = read_u16(d + region_list_off);
    uint16_t region_count = read_u16(d + region_list_off + 2);
    int region_record_size = axis_count * 6;
    uint32_t regions_start = region_list_off + 4;
    if (regions_start + size_t(region_count) * region_record_size > dsz) return 0.0f;

    uint16_t item_count = read_u16(d + ivd_off);
    uint16_t word_delta_count_raw = read_u16(d + ivd_off + 2);
    uint16_t region_index_count = read_u16(d + ivd_off + 4);
    if (inner >= item_count) return 0.0f;

    bool long_words = (word_delta_count_raw & 0x8000) != 0;
    uint16_t word_delta_count = word_delta_count_raw & 0x7FFF;
    if (word_delta_count > region_index_count) return 0.0f;

    int wide_size = long_words ? 4 : 2;
    int small_size = long_words ? 2 : 1;
    int row_size = word_delta_count * wide_size
                 + (region_index_count - word_delta_count) * small_size;

    uint32_t region_indexes_off = ivd_off + 6;
    if (region_indexes_off + region_index_count * 2 > dsz) return 0.0f;
    uint32_t delta_set_off = region_indexes_off + region_index_count * 2
                             + size_t(inner) * row_size;
    if (delta_set_off + row_size > dsz) return 0.0f;

    float total = 0.0f;
    for (int r = 0; r < region_index_count; ++r) {
        uint16_t region_idx = read_u16(d + region_indexes_off + r * 2);
        if (region_idx >= region_count) continue;

        int32_t delta_val;
        if (r < word_delta_count) {
            if (long_words)
                delta_val = read_i32(d + delta_set_off + r * 4);
            else
                delta_val = read_i16(d + delta_set_off + r * 2);
        } else {
            int wide_bytes = word_delta_count * wide_size;
            int small_idx = r - word_delta_count;
            const uint8_t* small_base = d + delta_set_off + wide_bytes;
            if (long_words)
                delta_val = read_i16(small_base + small_idx * 2);
            else
                delta_val = static_cast<int8_t>(small_base[small_idx]);
        }

        const uint8_t* region_ptr = d + regions_start + region_idx * region_record_size;
        float scalar = region_scalar(region_ptr, axis_count, norm_coords);
        if (scalar == 0.0f) continue;
        total += scalar * static_cast<float>(delta_val);
    }
    return total;
}

IvsIndex delta_set_index(const uint8_t* d, size_t dsz,
                          uint32_t map_off, uint32_t entry_index) {
    if (!map_off || map_off + 4 > dsz) return {entry_index >> 16, entry_index & 0xFFFFu};

    uint8_t format = d[map_off];
    uint8_t entry_format = d[map_off + 1];
    uint32_t map_count = 0;
    uint32_t entries_off = 0;

    if (format == 0) {
        map_count = read_u16(d + map_off + 2);
        entries_off = map_off + 4;
    } else if (format == 1) {
        if (map_off + 8 > dsz) return {0, 0};
        map_count = read_u32(d + map_off + 4);
        entries_off = map_off + 8;
    } else {
        return {0, 0};
    }

    int inner_bits = (entry_format & 0x0F) + 1;
    int entry_size = ((entry_format >> 4) & 0x3) + 1;

    uint32_t idx = entry_index;
    if (idx >= map_count) idx = map_count - 1;
    size_t off = entries_off + size_t(idx) * entry_size;
    if (off + entry_size > dsz) return {0, 0};

    uint32_t raw = 0;
    for (int i = 0; i < entry_size; ++i)
        raw = (raw << 8) | d[off + i];

    uint32_t inner_mask = (inner_bits >= 32) ? 0xFFFFFFFFu : ((1u << inner_bits) - 1);
    return {raw >> inner_bits, raw & inner_mask};
}

} // namespace detail

using detail::coverage_index;
using detail::classdef_lookup;
using detail::value_record_size;
using detail::extract_xadvance;
using detail::resolve_extension;

// Legacy kern table

int FontData::kern_pair_count() const {
    uint32_t kern = m_tables.kern;
    if (!kern) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (kern + 12 > dsz) return 0;
    if (read_u16(d + kern + 2) < 1) return 0;
    if (read_u16(d + kern + 8) != 1) return 0;
    return read_u16(d + kern + 10);
}

int FontData::kern_pairs(std::span<KernPair> out) const {
    uint32_t kern = m_tables.kern;
    if (!kern) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (kern + 12 > dsz) return 0;
    if (read_u16(d + kern + 2) < 1) return 0;
    if (read_u16(d + kern + 8) != 1) return 0;

    int num_pairs = read_u16(d + kern + 10);
    int count = std::min(num_pairs, static_cast<int>(out.size()));
    for (int i = 0; i < count; ++i) {
        size_t entry = kern + 18 + static_cast<size_t>(i) * 6;
        if (entry + 6 > dsz) return i;
        out[i].g1 = read_u16(d + entry);
        out[i].g2 = read_u16(d + entry + 2);
        out[i].value = read_i16(d + entry + 4);
    }
    return count;
}

int FontData::kern_legacy(uint16_t g1, uint16_t g2) const {
    uint32_t kern = m_tables.kern;
    if (!kern) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (kern + 12 > dsz) return 0;
    if (read_u16(d + kern + 2) < 1) return 0;
    if (read_u16(d + kern + 8) != 1) return 0;

    int num_pairs = read_u16(d + kern + 10);
    if (num_pairs == 0) return 0;
    uint32_t needle = (static_cast<uint32_t>(g1) << 16) | g2;
    int lo = 0, hi = num_pairs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        size_t entry = kern + 18 + static_cast<size_t>(mid) * 6;
        if (entry + 6 > dsz) return 0;
        uint32_t key = read_u32(d + entry);
        if (needle < key) hi = mid - 1;
        else if (needle > key) lo = mid + 1;
        else return read_i16(d + entry + 4);
    }
    return 0;
}

// GPOS kern

int FontData::kern_advance(uint16_t g1, uint16_t g2) const {
    int val = kern_gpos(g1, g2);
    if (val != 0) return val;
    return kern_legacy(g1, g2);
}

int FontData::kern_gpos(uint16_t g1, uint16_t g2) const {
    if (m_kern_subtable_count == 0) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();

    for (int i = 0; i < m_kern_subtable_count; ++i) {
        const auto& ks = m_kern_subtables[i];
        uint32_t subtable = ks.offset;

        if (ks.format == 1) {
            if (subtable + 10 > dsz) continue;
            int vr1_sz = value_record_size(ks.vfmt1);
            int vr2_sz = value_record_size(ks.vfmt2);

            uint32_t cov_off = subtable + read_u16(d + subtable + 2);
            int cov_idx = coverage_index(d, dsz, cov_off, g1);
            if (cov_idx < 0) continue;

            uint16_t pair_set_count = read_u16(d + subtable + 8);
            if (cov_idx >= pair_set_count) continue;
            uint32_t pairset = subtable + read_u16(d + subtable + 10 + cov_idx * 2);
            if (pairset + 2 > dsz) continue;
            int pvr_count = read_u16(d + pairset);
            int pair_size = 2 + vr1_sz + vr2_sz;

            int lo = 0, hi = pvr_count - 1;
            while (lo <= hi) {
                int mid = (lo + hi) >> 1;
                size_t poff = pairset + 2 + static_cast<size_t>(mid) * pair_size;
                if (poff + 2 > dsz) break;
                uint16_t second = read_u16(d + poff);
                if (g2 < second) hi = mid - 1;
                else if (g2 > second) lo = mid + 1;
                else return extract_xadvance(d, dsz, poff + 2, ks.vfmt1);
            }

        } else if (ks.format == 2) {
            if (subtable + 16 > dsz) continue;

            uint32_t cov_off = subtable + read_u16(d + subtable + 2);
            if (coverage_index(d, dsz, cov_off, g1) < 0) continue;

            uint32_t cd1_off = subtable + read_u16(d + subtable + 8);
            uint32_t cd2_off = subtable + read_u16(d + subtable + 10);
            uint16_t c1count = read_u16(d + subtable + 12);
            uint16_t c2count = read_u16(d + subtable + 14);
            if (c1count == 0 || c2count == 0) continue;

            uint16_t cls1 = classdef_lookup(d, dsz, cd1_off, g1);
            uint16_t cls2 = classdef_lookup(d, dsz, cd2_off, g2);
            if (cls1 >= c1count || cls2 >= c2count) continue;

            int vr1_sz = value_record_size(ks.vfmt1);
            int vr2_sz = value_record_size(ks.vfmt2);
            int rec_sz = vr1_sz + vr2_sz;
            size_t rec_off = subtable + 16
                + static_cast<size_t>(cls1) * c2count * rec_sz
                + static_cast<size_t>(cls2) * rec_sz;

            int val = extract_xadvance(d, dsz, rec_off, ks.vfmt1);
            if (val != 0) return val;
        }
    }
    return 0;
}

// Generic helper: walk a GSUB/GPOS feature, calling `visit` for each subtable.
// `feature_tag` is e.g. "kern" or "liga".
// `expected_lookup_type` is the original (non-extension) type number.
// `visit` returns true to continue walking, false to stop.
template <typename Visit>
static void walk_feature_subtables(const uint8_t* d, size_t dsz,
                                     uint32_t table_off,
                                     const char* feature_tag,
                                     uint16_t expected_lookup_type,
                                     uint16_t extension_lookup_type,
                                     Visit visit) {
    if (!table_off || table_off + 10 > dsz) return;
    uint32_t feature_list = table_off + read_u16(d + table_off + 6);
    uint32_t lookup_list  = table_off + read_u16(d + table_off + 8);
    if (feature_list + 2 > dsz) return;

    int feature_count = read_u16(d + feature_list);
    for (int fi = 0; fi < feature_count; ++fi) {
        uint32_t frec = feature_list + 2 + static_cast<uint32_t>(fi) * 6;
        if (frec + 6 > dsz) return;
        if (!tag_eq(d + frec, feature_tag)) continue;

        uint32_t ftable = feature_list + read_u16(d + frec + 4);
        if (ftable + 4 > dsz) continue;
        int lookup_count = read_u16(d + ftable + 2);

        for (int li = 0; li < lookup_count; ++li) {
            if (ftable + 4 + (li + 1) * 2 > dsz) break;
            uint16_t lookup_idx = read_u16(d + ftable + 4 + li * 2);

            if (lookup_list + 2 > dsz) continue;
            int total_lookups = read_u16(d + lookup_list);
            if (lookup_idx >= total_lookups) continue;
            if (lookup_list + 2 + (lookup_idx + 1) * 2 > dsz) continue;

            uint32_t lookup = lookup_list + read_u16(d + lookup_list + 2 + lookup_idx * 2);
            if (lookup + 6 > dsz) continue;
            uint16_t lookup_type = read_u16(d + lookup);
            bool is_extension = (lookup_type == extension_lookup_type);
            if (lookup_type != expected_lookup_type && !is_extension) continue;

            int subtable_count = read_u16(d + lookup + 4);
            for (int si = 0; si < subtable_count; ++si) {
                if (lookup + 6 + (si + 1) * 2 > dsz) break;
                uint32_t subtable = lookup + read_u16(d + lookup + 6 + si * 2);
                if (subtable + 4 > dsz) continue;

                if (is_extension) {
                    subtable = resolve_extension(d, dsz, subtable, expected_lookup_type);
                    if (!subtable || subtable + 4 > dsz) continue;
                }
                if (!visit(subtable)) return;
            }
        }
    }
}

void FontData::resolve_kern_subtables() {
    m_kern_subtable_count = 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();

    walk_feature_subtables(d, dsz, m_tables.gpos, "kern",
        /*expected_lookup_type=*/2, /*extension=*/9,
        [&](uint32_t subtable) {
            if (m_kern_subtable_count >= kMaxKernSubtables) return false;
            uint16_t pos_fmt = read_u16(d + subtable);
            if (pos_fmt != 1 && pos_fmt != 2) return true;

            uint16_t vfmt1 = (subtable + 6 <= dsz) ? read_u16(d + subtable + 4) : 0;
            uint16_t vfmt2 = (subtable + 8 <= dsz) ? read_u16(d + subtable + 6) : 0;
            if (!(vfmt1 & 0x04)) return true;

            auto& ks = m_kern_subtables[m_kern_subtable_count++];
            ks.offset = subtable;
            ks.format = pos_fmt;
            ks.vfmt1 = vfmt1;
            ks.vfmt2 = vfmt2;
            return true;
        });
}

// GSUB ligature substitution

void FontData::resolve_liga_subtables() {
    m_liga_subtable_count = 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();

    walk_feature_subtables(d, dsz, m_tables.gsub, "liga",
        /*expected_lookup_type=*/4, /*extension=*/7,
        [&](uint32_t subtable) {
            if (m_liga_subtable_count >= kMaxLigaSubtables) return false;
            if (subtable + 2 > dsz) return true;
            uint16_t fmt = read_u16(d + subtable);
            if (fmt != 1) return true;
            m_liga_subtables[m_liga_subtable_count++] = subtable;
            return true;
        });
}

LigatureMatch FontData::liga_lookup(std::span<const uint16_t> glyphs) const {
    LigatureMatch best{};
    if (glyphs.size() < 2 || m_liga_subtable_count == 0) return best;

    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint16_t first = glyphs[0];

    for (int s = 0; s < m_liga_subtable_count; ++s) {
        uint32_t sub = m_liga_subtables[s];
        if (sub + 6 > dsz) continue;

        uint32_t cov_off = sub + read_u16(d + sub + 2);
        int cov_idx = coverage_index(d, dsz, cov_off, first);
        if (cov_idx < 0) continue;

        uint16_t set_count = read_u16(d + sub + 4);
        if (cov_idx >= set_count) continue;
        if (sub + 6 + (cov_idx + 1) * 2 > dsz) continue;

        uint32_t ligset = sub + read_u16(d + sub + 6 + cov_idx * 2);
        if (ligset + 2 > dsz) continue;

        uint16_t lig_count = read_u16(d + ligset);
        // Font convention: ligatures ordered with longer matches first.
        for (int li = 0; li < lig_count; ++li) {
            if (ligset + 2 + (li + 1) * 2 > dsz) break;
            uint32_t lig = ligset + read_u16(d + ligset + 2 + li * 2);
            if (lig + 4 > dsz) continue;

            uint16_t lig_glyph = read_u16(d + lig);
            uint16_t comp_count = read_u16(d + lig + 2);
            if (comp_count == 0 || comp_count > glyphs.size()) continue;
            if (lig + 4 + (comp_count - 1) * 2 > dsz) continue;

            bool match = true;
            for (int c = 1; c < comp_count; ++c) {
                uint16_t expected = read_u16(d + lig + 4 + (c - 1) * 2);
                if (glyphs[c] != expected) { match = false; break; }
            }
            if (match && comp_count > best.consumed) {
                best.glyph = lig_glyph;
                best.consumed = comp_count;
                break; // first match within set is the longest by convention
            }
        }
    }

    return best;
}

} // namespace kcg::otf
