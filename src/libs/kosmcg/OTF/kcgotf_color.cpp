// kcgotf_color.cpp - color font tables and variation tables
// COLR v0/v1, sbix, CBDT/CBLC, color format dispatch
// ItemVariationStore, HVAR, MVAR, gvar
// C++20, userspace

#include "kcgotf.hpp"
#include "kcgotf_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace kcg::otf {

// Offset24 helper (COLR v1 sub-paint offsets are 3 bytes, big-endian).
static inline uint32_t read_offset24(const uint8_t* p) {
    return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
}

// ItemVariationStore

// ItemVariationStore helpers (detail::ivs_delta, detail::delta_set_index)
// live in kcgotf_layout.cpp alongside other binary-table lookups.

// HVAR / MVAR per-glyph and per-font metric deltas live in kcgotf_core.cpp
// alongside the metric accessors that consume them.

// COLR resolve

void FontData::resolve_colr() {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint32_t colr = m_tables.colr;
    if (!colr || colr + 14 > dsz) return;

    uint16_t version = read_u16(d + colr);
    m_colr_v0_num_base = read_u16(d + colr + 2);
    uint32_t br_off = read_u32(d + colr + 4);
    uint32_t lr_off = read_u32(d + colr + 8);
    m_colr_v0_num_layers = read_u16(d + colr + 12);

    m_colr_v0_base_records = br_off ? (colr + br_off) : 0;
    m_colr_v0_layer_records = lr_off ? (colr + lr_off) : 0;

    if (version >= 1 && colr + 34 <= dsz) {
        uint32_t base_list_rel = read_u32(d + colr + 14);
        uint32_t layer_list_rel = read_u32(d + colr + 18);
        uint32_t clip_list_rel = read_u32(d + colr + 22);
        uint32_t var_idx_map_rel = read_u32(d + colr + 26);
        uint32_t var_store_rel = read_u32(d + colr + 30);

        m_colr_v1_base_list = base_list_rel ? colr + base_list_rel : 0;
        m_colr_v1_layer_list = layer_list_rel ? colr + layer_list_rel : 0;
        m_colr_v1_clip_list = clip_list_rel ? colr + clip_list_rel : 0;
        m_colr_v1_var_index_map = var_idx_map_rel ? colr + var_idx_map_rel : 0;
        m_colr_v1_var_store = var_store_rel ? colr + var_store_rel : 0;

        if (m_colr_v1_base_list && m_colr_v1_base_list + 4 <= dsz)
            m_colr_v1_num_base = static_cast<uint16_t>(
                std::min<uint32_t>(read_u32(d + m_colr_v1_base_list), 0xFFFF));
    }
}

bool FontData::has_colr_v0() const {
    return m_tables.colr != 0 && m_colr_v0_num_base > 0;
}

bool FontData::has_colr_v1() const {
    return m_tables.colr != 0 && m_colr_v1_num_base > 0;
}

// COLR v0

static int colr_v0_find_base(const uint8_t* d, size_t dsz,
                              uint32_t base_records, uint16_t num_base,
                              uint16_t glyph) {
    if (!base_records || num_base == 0) return -1;
    if (base_records + size_t(num_base) * 6 > dsz) return -1;
    int lo = 0, hi = int(num_base) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t g = read_u16(d + base_records + mid * 6);
        if (glyph < g) hi = mid - 1;
        else if (glyph > g) lo = mid + 1;
        else return mid;
    }
    return -1;
}

int FontData::colr_v0_layer_count(uint16_t base_glyph) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    int idx = colr_v0_find_base(d, dsz, m_colr_v0_base_records,
                                  m_colr_v0_num_base, base_glyph);
    if (idx < 0) return 0;
    return read_u16(d + m_colr_v0_base_records + idx * 6 + 4);
}

int FontData::colr_v0_layers(uint16_t base_glyph, std::span<ColrLayer> out) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    int idx = colr_v0_find_base(d, dsz, m_colr_v0_base_records,
                                  m_colr_v0_num_base, base_glyph);
    if (idx < 0) return 0;

    uint16_t first = read_u16(d + m_colr_v0_base_records + idx * 6 + 2);
    uint16_t count = read_u16(d + m_colr_v0_base_records + idx * 6 + 4);
    if (uint32_t(first) + count > m_colr_v0_num_layers) return 0;
    if (m_colr_v0_layer_records + size_t(first + count) * 4 > dsz) return 0;

    int written = std::min<int>(count, static_cast<int>(out.size()));
    for (int i = 0; i < written; ++i) {
        size_t rec = m_colr_v0_layer_records + size_t(first + i) * 4;
        out[i].glyph_id      = read_u16(d + rec);
        out[i].palette_index = read_u16(d + rec + 2);
    }
    return written;
}

// sbix

int FontData::sbix_strike_count() const {
    uint32_t sbix = m_tables.sbix;
    if (!sbix) return 0;
    auto* d = m_data.data();
    if (sbix + 8 > m_data.size()) return 0;
    return read_u32(d + sbix + 4);
}

uint16_t FontData::sbix_strike_ppem(int strike) const {
    uint32_t sbix = m_tables.sbix;
    if (!sbix || strike < 0) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (sbix + 8 + size_t(strike + 1) * 4 > dsz) return 0;
    uint32_t off = sbix + read_u32(d + sbix + 8 + strike * 4);
    if (off + 4 > dsz) return 0;
    return read_u16(d + off);
}

uint16_t FontData::sbix_strike_ppi(int strike) const {
    uint32_t sbix = m_tables.sbix;
    if (!sbix || strike < 0) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (sbix + 8 + size_t(strike + 1) * 4 > dsz) return 0;
    uint32_t off = sbix + read_u32(d + sbix + 8 + strike * 4);
    if (off + 4 > dsz) return 0;
    return read_u16(d + off + 2);
}

int FontData::sbix_best_strike(uint16_t target_ppem) const {
    int n = sbix_strike_count();
    if (n == 0) return -1;
    int best = -1;
    uint16_t best_ppem = 0;
    for (int i = 0; i < n; ++i) {
        uint16_t p = sbix_strike_ppem(i);
        if (p >= target_ppem) {
            if (best < 0 || p < best_ppem) {
                best = i;
                best_ppem = p;
            }
        }
    }
    if (best < 0) {
        // No strike >= target; pick largest.
        for (int i = 0; i < n; ++i) {
            uint16_t p = sbix_strike_ppem(i);
            if (p > best_ppem) {
                best = i;
                best_ppem = p;
            }
        }
    }
    return best;
}

SbixGlyph FontData::sbix_glyph(int strike, uint16_t glyph) const {
    SbixGlyph result{};
    uint32_t sbix = m_tables.sbix;
    if (!sbix || strike < 0) return result;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (sbix + 8 + size_t(strike + 1) * 4 > dsz) return result;

    uint32_t strike_off = sbix + read_u32(d + sbix + 8 + strike * 4);
    if (strike_off + 4 > dsz) return result;
    uint16_t ppem = read_u16(d + strike_off);
    uint16_t ppi  = read_u16(d + strike_off + 2);

    uint32_t glyph_data_offsets = strike_off + 4;
    size_t need = size_t(glyph + 2) * 4;
    if (glyph_data_offsets + need > dsz) return result;

    uint32_t start = read_u32(d + glyph_data_offsets + glyph * 4);
    uint32_t end = read_u32(d + glyph_data_offsets + (glyph + 1) * 4);
    if (start == end) return result;
    if (end < start) return result;

    uint32_t glyph_off = strike_off + start;
    if (glyph_off + 8 > dsz) return result;
    uint32_t data_size = end - start;
    if (data_size < 8 || glyph_off + data_size > dsz) return result;

    int16_t origin_x = read_i16(d + glyph_off);
    int16_t origin_y = read_i16(d + glyph_off + 2);
    uint32_t format = read_u32(d + glyph_off + 4);

    // Format 'dupe': data is a uint16 glyph ID to dereference.
    if (format == make_tag("dupe")) {
        if (data_size < 10) return result;
        uint16_t target = read_u16(d + glyph_off + 8);
        if (target == glyph) return result;
        return sbix_glyph(strike, target);
    }

    result.data = {d + glyph_off + 8, data_size - 8};
    result.format = format;
    result.origin_x = origin_x;
    result.origin_y = origin_y;
    result.ppem = ppem;
    result.ppi = ppi;
    return result;
}

// CBDT / CBLC

int FontData::cblc_strike_count() const {
    uint32_t cblc = m_tables.cblc;
    if (!cblc) return 0;
    auto* d = m_data.data();
    if (cblc + 8 > m_data.size()) return 0;
    return read_u32(d + cblc + 4);
}

uint16_t FontData::cblc_strike_ppem(int strike) const {
    uint32_t cblc = m_tables.cblc;
    if (!cblc || strike < 0) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    size_t off = size_t(cblc) + 8 + size_t(strike) * 48;
    if (off + 48 > dsz) return 0;
    // BitmapSize ppemX at offset 44 (uint8)
    return d[off + 44];
}

int FontData::cblc_best_strike(uint16_t target_ppem) const {
    int n = cblc_strike_count();
    if (n == 0) return -1;
    int best = -1;
    uint16_t best_ppem = 0;
    for (int i = 0; i < n; ++i) {
        uint16_t p = cblc_strike_ppem(i);
        if (p >= target_ppem) {
            if (best < 0 || p < best_ppem) { best = i; best_ppem = p; }
        }
    }
    if (best < 0) {
        for (int i = 0; i < n; ++i) {
            uint16_t p = cblc_strike_ppem(i);
            if (p > best_ppem) { best = i; best_ppem = p; }
        }
    }
    return best;
}

CbdtGlyph FontData::cbdt_glyph(int strike, uint16_t glyph) const {
    CbdtGlyph result{};
    uint32_t cblc = m_tables.cblc;
    uint32_t cbdt = m_tables.cbdt;
    if (!cblc || !cbdt || strike < 0) return result;
    auto* d = m_data.data();
    size_t dsz = m_data.size();

    // BitmapSize: 48 bytes starting at cblc + 8.
    size_t bs_off = size_t(cblc) + 8 + size_t(strike) * 48;
    if (bs_off + 48 > dsz) return result;

    uint32_t index_subtable_array_off = cblc + read_u32(d + bs_off);
    uint32_t num_index_subtables = read_u32(d + bs_off + 8);
    uint8_t ppem_x = d[bs_off + 44];
    uint8_t ppem_y = d[bs_off + 45];

    // Find IndexSubTableArray entry containing `glyph`.
    if (index_subtable_array_off + num_index_subtables * 8 > dsz) return result;
    for (uint32_t i = 0; i < num_index_subtables; ++i) {
        size_t entry = index_subtable_array_off + size_t(i) * 8;
        uint16_t first_glyph = read_u16(d + entry);
        uint16_t last_glyph  = read_u16(d + entry + 2);
        if (glyph < first_glyph || glyph > last_glyph) continue;
        uint32_t st_off = index_subtable_array_off + read_u32(d + entry + 4);
        if (st_off + 8 > dsz) return result;

        uint16_t index_format = read_u16(d + st_off);
        uint16_t image_format = read_u16(d + st_off + 2);
        uint32_t image_data_off = cbdt + read_u32(d + st_off + 4);

        uint32_t glyph_image_off = 0;
        uint32_t glyph_image_len = 0;
        uint16_t idx_in_set = glyph - first_glyph;

        if (index_format == 1) {
            // 4-byte offsets per glyph (count = last - first + 2)
            size_t off_array = st_off + 8;
            size_t need = size_t(idx_in_set + 2) * 4;
            if (off_array + need > dsz) return result;
            uint32_t lo = read_u32(d + off_array + idx_in_set * 4);
            uint32_t hi = read_u32(d + off_array + (idx_in_set + 1) * 4);
            if (lo == hi) return result;
            glyph_image_off = image_data_off + lo;
            glyph_image_len = hi - lo;
        } else if (index_format == 2) {
            // constant-size, all glyphs same metrics
            if (st_off + 8 + 4 + 12 > dsz) return result;
            uint32_t image_size = read_u32(d + st_off + 8);
            // BigGlyphMetrics at st_off + 12 (8 bytes for height/width/horiBearingX/Y/horiAdvance)
            int8_t bx = static_cast<int8_t>(d[st_off + 12 + 2]);
            int8_t by = static_cast<int8_t>(d[st_off + 12 + 3]);
            uint8_t adv = d[st_off + 12 + 4];
            glyph_image_off = image_data_off + idx_in_set * image_size;
            glyph_image_len = image_size;
            result.bearing_x = bx;
            result.bearing_y = by;
            result.advance = adv;
        } else if (index_format == 3) {
            // 2-byte offsets per glyph
            size_t off_array = st_off + 8;
            size_t need = size_t(idx_in_set + 2) * 2;
            if (off_array + need > dsz) return result;
            uint32_t lo = read_u16(d + off_array + idx_in_set * 2);
            uint32_t hi = read_u16(d + off_array + (idx_in_set + 1) * 2);
            if (lo == hi) return result;
            glyph_image_off = image_data_off + lo;
            glyph_image_len = hi - lo;
        } else {
            // Format 4/5 (sparse) not supported in this minimal implementation.
            return result;
        }

        if (glyph_image_off + glyph_image_len > dsz) return result;

        // Image formats 17/18/19 are PNG with embedded metrics.
        if (image_format == 17) {
            // SmallGlyphMetrics (5 bytes) + uint32 dataLen + PNG
            if (glyph_image_len < 9) return result;
            const uint8_t* p = d + glyph_image_off;
            // height, width, bearingX (int8), bearingY (int8), advance (uint8)
            if (index_format != 2) {
                result.bearing_x = static_cast<int8_t>(p[2]);
                result.bearing_y = static_cast<int8_t>(p[3]);
                result.advance = p[4];
            }
            uint32_t data_len = read_u32(p + 5);
            if (9 + data_len > glyph_image_len) return result;
            result.data = {p + 9, data_len};
            result.format = 17;
        } else if (image_format == 18) {
            // BigGlyphMetrics (8 bytes) + uint32 dataLen + PNG
            if (glyph_image_len < 12) return result;
            const uint8_t* p = d + glyph_image_off;
            if (index_format != 2) {
                result.bearing_x = static_cast<int8_t>(p[2]);
                result.bearing_y = static_cast<int8_t>(p[3]);
                result.advance = p[4];
            }
            uint32_t data_len = read_u32(p + 8);
            if (12 + data_len > glyph_image_len) return result;
            result.data = {p + 12, data_len};
            result.format = 18;
        } else if (image_format == 19) {
            // uint32 dataLen + PNG (metrics from index format 2)
            if (glyph_image_len < 4) return result;
            const uint8_t* p = d + glyph_image_off;
            uint32_t data_len = read_u32(p);
            if (4 + data_len > glyph_image_len) return result;
            result.data = {p + 4, data_len};
            result.format = 19;
        } else {
            return result;
        }

        result.ppem_x = ppem_x;
        result.ppem_y = ppem_y;
        return result;
    }
    return result;
}

// color_format dispatch with selectable preference policy.

ColorFontFormat FontData::color_format(uint16_t glyph,
                                         ColorFormatPreference pref,
                                         uint16_t target_ppem) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();

    // Probe presence of each color format for this specific glyph.
    // For bitmap formats we also remember the chosen strike's ppem to evaluate
    // the auto_by_size policy.
    uint16_t sbix_strike_ppem_val = 0;
    bool has_sbix_glyph = false;
    if (has_sbix()) {
        uint16_t pick = target_ppem ? target_ppem
                                     : static_cast<uint16_t>(m_units_per_em);
        int strike = sbix_best_strike(pick);
        if (strike >= 0) {
            auto g = sbix_glyph(strike, glyph);
            if (!g.data.empty()) {
                has_sbix_glyph = true;
                sbix_strike_ppem_val = g.ppem;
            }
        }
    }

    uint16_t cbdt_strike_ppem_val = 0;
    bool has_cbdt_glyph = false;
    if (has_cbdt()) {
        uint16_t pick = target_ppem ? target_ppem
                                     : static_cast<uint16_t>(m_units_per_em);
        int strike = cblc_best_strike(pick);
        if (strike >= 0) {
            auto g = cbdt_glyph(strike, glyph);
            if (!g.data.empty()) {
                has_cbdt_glyph = true;
                cbdt_strike_ppem_val = g.ppem_x;
            }
        }
    }

    bool has_colr_v1_glyph = false;
    if (has_colr_v1() && m_colr_v1_base_list && m_colr_v1_base_list + 4 <= dsz) {
        uint32_t n = read_u32(d + m_colr_v1_base_list);
        if (m_colr_v1_base_list + 4 + size_t(n) * 6 <= dsz) {
            int lo = 0, hi = int(n) - 1;
            while (lo <= hi) {
                int mid = (lo + hi) >> 1;
                uint16_t g = read_u16(d + m_colr_v1_base_list + 4 + mid * 6);
                if (glyph < g) hi = mid - 1;
                else if (glyph > g) lo = mid + 1;
                else { has_colr_v1_glyph = true; break; }
            }
        }
    }

    bool has_colr_v0_glyph = has_colr_v0() && colr_v0_layer_count(glyph) > 0;
    bool has_svg_glyph = has_svg() && !glyph_svg(glyph).empty();

    // For auto_by_size: a bitmap strike is "acceptable" if its ppem isn't
    // dramatically far from target — using upscale ≤ 4× and downscale ≤ 4×
    // as the comfort window. Beyond that, vector tends to look cleaner.
    auto acceptable_bitmap = [&](uint16_t strike_ppem) {
        if (!target_ppem || !strike_ppem) return true;
        uint16_t hi = strike_ppem > target_ppem ? strike_ppem : target_ppem;
        uint16_t lo = strike_ppem > target_ppem ? target_ppem : strike_ppem;
        return hi <= lo * 4u;
    };

    switch (pref) {
    case ColorFormatPreference::bitmap_first:
        if (has_sbix_glyph)    return ColorFontFormat::sbix;
        if (has_cbdt_glyph)    return ColorFontFormat::cbdt;
        if (has_colr_v1_glyph) return ColorFontFormat::colr_v1;
        if (has_colr_v0_glyph) return ColorFontFormat::colr_v0;
        if (has_svg_glyph)     return ColorFontFormat::svg;
        return ColorFontFormat::none;

    case ColorFormatPreference::vector_first:
        if (has_colr_v1_glyph) return ColorFontFormat::colr_v1;
        if (has_colr_v0_glyph) return ColorFontFormat::colr_v0;
        if (has_svg_glyph)     return ColorFontFormat::svg;
        if (has_sbix_glyph)    return ColorFontFormat::sbix;
        if (has_cbdt_glyph)    return ColorFontFormat::cbdt;
        return ColorFontFormat::none;

    case ColorFormatPreference::auto_by_size:
        if (has_sbix_glyph && acceptable_bitmap(sbix_strike_ppem_val))
            return ColorFontFormat::sbix;
        if (has_cbdt_glyph && acceptable_bitmap(cbdt_strike_ppem_val))
            return ColorFontFormat::cbdt;
        if (has_colr_v1_glyph) return ColorFontFormat::colr_v1;
        if (has_colr_v0_glyph) return ColorFontFormat::colr_v0;
        if (has_svg_glyph)     return ColorFontFormat::svg;
        // Last-resort bitmap fallback even if scaling is poor.
        if (has_sbix_glyph)    return ColorFontFormat::sbix;
        if (has_cbdt_glyph)    return ColorFontFormat::cbdt;
        return ColorFontFormat::none;
    }
    return ColorFontFormat::none;
}

// gvar (TT outline variation) moved to kcgotf_outlines.cpp alongside the TT
// parser that consumes it.

// COLR v1 paint walker

namespace {

// Helper to compute final ARGB color from palette index + alpha, applying CPAL.
// `foreground_color` is used when pal_idx == 0xFFFF (COLR "current text color").
uint32_t resolve_color(const FontData& fd, int palette_index,
                        uint16_t pal_idx, float alpha,
                        uint32_t foreground_color) {
    uint32_t base;
    if (pal_idx == 0xFFFF) {
        base = foreground_color;
    } else {
        base = fd.cpal_color(palette_index, pal_idx);
    }
    uint32_t a = (base >> 24) & 0xFFu;
    float fa = (alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha));
    a = static_cast<uint32_t>(static_cast<float>(a) * fa + 0.5f);
    if (a > 255) a = 255;
    return (base & 0x00FFFFFFu) | (a << 24);
}

// Look up COLR v1 BaseGlyphList paint offset for glyph.
uint32_t find_v1_base_paint(const uint8_t* d, size_t dsz,
                              uint32_t base_list, uint16_t glyph) {
    if (!base_list || base_list + 4 > dsz) return 0;
    uint32_t n = read_u32(d + base_list);
    if (base_list + 4 + size_t(n) * 6 > dsz) return 0;
    int lo = 0, hi = int(n) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        size_t rec = base_list + 4 + size_t(mid) * 6;
        uint16_t g = read_u16(d + rec);
        if (glyph < g) hi = mid - 1;
        else if (glyph > g) lo = mid + 1;
        else return base_list + read_u32(d + rec + 2);
    }
    return 0;
}

// Read a color line (or var color line) and build a ColorStop array.
// If is_var, applies IVS deltas. Returns number of stops written to `out`.
int read_color_line(const FontData& fd,
                     const uint8_t* d, size_t dsz,
                     uint32_t color_line_off, int palette_index,
                     uint32_t var_index_map, uint32_t var_store,
                     std::span<const float> norm,
                     bool is_var, uint32_t var_index_base_at_line_start,
                     ColorStop* out, int max_out,
                     GradientExtend& extend_out,
                     uint32_t foreground_color) {
    if (color_line_off + 3 > dsz) return 0;
    uint8_t extend_byte = d[color_line_off];
    uint16_t num_stops = read_u16(d + color_line_off + 1);
    extend_out = (extend_byte <= 2) ? static_cast<GradientExtend>(extend_byte)
                                       : GradientExtend::pad;

    int stop_size = is_var ? 10 : 6;
    if (color_line_off + 3 + size_t(num_stops) * stop_size > dsz) return 0;

    int n = std::min<int>(num_stops, max_out);
    for (int i = 0; i < n; ++i) {
        size_t off = color_line_off + 3 + size_t(i) * stop_size;
        float stop_offset = detail::read_f2dot14(d + off);
        uint16_t pal = read_u16(d + off + 2);
        float alpha = detail::read_f2dot14(d + off + 4);

        if (is_var) {
            uint32_t var_index_base = read_u32(d + off + 6);
            // Variable indices per VarColorStop: 0=stopOffset, 1=alpha
            auto idx0 = detail::delta_set_index(d, dsz, var_index_map, var_index_base);
            auto idx1 = detail::delta_set_index(d, dsz, var_index_map, var_index_base + 1);
            stop_offset += detail::ivs_delta(d, dsz, var_store,
                                              idx0.outer, idx0.inner, norm) / 16384.0f;
            alpha += detail::ivs_delta(d, dsz, var_store,
                                        idx1.outer, idx1.inner, norm) / 16384.0f;
        }
        (void)var_index_base_at_line_start;

        out[i].offset = stop_offset;
        out[i].argb = resolve_color(fd, palette_index, pal, alpha, foreground_color);
    }
    return n;
}

constexpr int kPaintStopsMax = 16;
constexpr int kPaintDepthMax = 64;

// Look up a single IVS delta for COLR v1 var-paints.
// Returns 0 when no variation coords are active.
static float colr_var_delta(const uint8_t* d, size_t dsz,
                             uint32_t var_idx_map, uint32_t var_store,
                             uint32_t var_idx_base, uint32_t var_idx,
                             std::span<const float> norm) {
    if (norm.empty() || !var_store) return 0.0f;
    detail::IvsIndex idx;
    if (var_idx_map)
        idx = detail::delta_set_index(d, dsz, var_idx_map, var_idx_base + var_idx);
    else
        idx = {(var_idx_base + var_idx) >> 16,
                (var_idx_base + var_idx) & 0xFFFFu};
    return detail::ivs_delta(d, dsz, var_store, idx.outer, idx.inner, norm);
}

} // namespace

bool FontData::walk_paint_v1(uint32_t paint_off, int palette_index,
                               const PaintSink& sink,
                               uint32_t foreground_color,
                               const VariationInstance* var, int depth) const {
    if (depth > kPaintDepthMax) return false;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (!paint_off || paint_off + 1 > dsz) return false;

    std::span<const float> norm;
    if (var && !var->empty()) norm = var->normalized();

    // Convenience: var-delta lookup curried with current font's IVS pointers.
    auto vd = [&](uint32_t base, uint32_t idx) {
        return colr_var_delta(d, dsz, m_colr_v1_var_index_map,
                                m_colr_v1_var_store, base, idx, norm);
    };

    uint8_t fmt = d[paint_off];

    switch (fmt) {
    case 1: { // PaintColrLayers
        if (paint_off + 6 > dsz) return false;
        uint8_t num_layers = d[paint_off + 1];
        uint32_t first_layer = read_u32(d + paint_off + 2);
        if (!m_colr_v1_layer_list || m_colr_v1_layer_list + 4 > dsz) return false;
        uint32_t total = read_u32(d + m_colr_v1_layer_list);
        if (uint32_t(first_layer) + num_layers > total) return false;
        for (int i = 0; i < num_layers; ++i) {
            size_t entry = m_colr_v1_layer_list + 4 + size_t(first_layer + i) * 4;
            if (entry + 4 > dsz) return false;
            uint32_t sub = m_colr_v1_layer_list + read_u32(d + entry);
            walk_paint_v1(sub, palette_index, sink, foreground_color, var, depth + 1);
        }
        return true;
    }
    case 2: { // PaintSolid
        if (paint_off + 5 > dsz) return false;
        uint16_t pal = read_u16(d + paint_off + 1);
        float alpha = detail::read_f2dot14(d + paint_off + 3);
        if (sink.set_solid)
            sink.set_solid(sink.ctx, resolve_color(*this, palette_index, pal, alpha, foreground_color));
        return true;
    }
    case 3: { // PaintVarSolid
        if (paint_off + 9 > dsz) return false;
        uint16_t pal = read_u16(d + paint_off + 1);
        float alpha = detail::read_f2dot14(d + paint_off + 3);
        uint32_t var_base = read_u32(d + paint_off + 5);
        alpha += vd(var_base, 0) / 16384.0f;
        if (sink.set_solid)
            sink.set_solid(sink.ctx, resolve_color(*this, palette_index, pal, alpha, foreground_color));
        return true;
    }
    case 4: { // PaintLinearGradient
        if (paint_off + 16 > dsz) return false;
        uint32_t cl_off = paint_off + read_offset24(d + paint_off + 1);
        float x0 = read_i16(d + paint_off + 4);
        float y0 = read_i16(d + paint_off + 6);
        float x1 = read_i16(d + paint_off + 8);
        float y1 = read_i16(d + paint_off + 10);
        float x2 = read_i16(d + paint_off + 12);
        float y2 = read_i16(d + paint_off + 14);

        ColorStop stops[kPaintStopsMax];
        GradientExtend extend = GradientExtend::pad;
        int nstops = read_color_line(*this, d, dsz, cl_off, palette_index,
                                       m_colr_v1_var_index_map, m_colr_v1_var_store,
                                       norm, false, 0, stops, kPaintStopsMax, extend,
                                       foreground_color);
        if (sink.set_linear_gradient)
            sink.set_linear_gradient(sink.ctx, x0, y0, x1, y1, x2, y2,
                                      extend, nstops, stops);
        return true;
    }
    case 5: { // PaintVarLinearGradient
        if (paint_off + 20 > dsz) return false;
        uint32_t cl_off = paint_off + read_offset24(d + paint_off + 1);
        float x0 = read_i16(d + paint_off + 4);
        float y0 = read_i16(d + paint_off + 6);
        float x1 = read_i16(d + paint_off + 8);
        float y1 = read_i16(d + paint_off + 10);
        float x2 = read_i16(d + paint_off + 12);
        float y2 = read_i16(d + paint_off + 14);
        uint32_t var_base = read_u32(d + paint_off + 16);
        x0 += vd(var_base, 0); y0 += vd(var_base, 1);
        x1 += vd(var_base, 2); y1 += vd(var_base, 3);
        x2 += vd(var_base, 4); y2 += vd(var_base, 5);

        ColorStop stops[kPaintStopsMax];
        GradientExtend extend = GradientExtend::pad;
        int nstops = read_color_line(*this, d, dsz, cl_off, palette_index,
                                       m_colr_v1_var_index_map, m_colr_v1_var_store,
                                       norm, true, 0, stops, kPaintStopsMax, extend,
                                       foreground_color);
        if (sink.set_linear_gradient)
            sink.set_linear_gradient(sink.ctx, x0, y0, x1, y1, x2, y2,
                                      extend, nstops, stops);
        return true;
    }
    case 6: { // PaintRadialGradient
        if (paint_off + 16 > dsz) return false;
        uint32_t cl_off = paint_off + read_offset24(d + paint_off + 1);
        float x0 = read_i16(d + paint_off + 4);
        float y0 = read_i16(d + paint_off + 6);
        float r0 = read_u16(d + paint_off + 8);
        float x1 = read_i16(d + paint_off + 10);
        float y1 = read_i16(d + paint_off + 12);
        float r1 = read_u16(d + paint_off + 14);

        ColorStop stops[kPaintStopsMax];
        GradientExtend extend = GradientExtend::pad;
        int nstops = read_color_line(*this, d, dsz, cl_off, palette_index,
                                       m_colr_v1_var_index_map, m_colr_v1_var_store,
                                       norm, false, 0, stops, kPaintStopsMax, extend,
                                       foreground_color);
        if (sink.set_radial_gradient)
            sink.set_radial_gradient(sink.ctx, x0, y0, r0, x1, y1, r1,
                                      extend, nstops, stops);
        return true;
    }
    case 7: { // PaintVarRadialGradient
        if (paint_off + 20 > dsz) return false;
        uint32_t cl_off = paint_off + read_offset24(d + paint_off + 1);
        float x0 = read_i16(d + paint_off + 4);
        float y0 = read_i16(d + paint_off + 6);
        float r0 = read_u16(d + paint_off + 8);
        float x1 = read_i16(d + paint_off + 10);
        float y1 = read_i16(d + paint_off + 12);
        float r1 = read_u16(d + paint_off + 14);
        uint32_t var_base = read_u32(d + paint_off + 16);
        x0 += vd(var_base, 0); y0 += vd(var_base, 1); r0 += vd(var_base, 2);
        x1 += vd(var_base, 3); y1 += vd(var_base, 4); r1 += vd(var_base, 5);

        ColorStop stops[kPaintStopsMax];
        GradientExtend extend = GradientExtend::pad;
        int nstops = read_color_line(*this, d, dsz, cl_off, palette_index,
                                       m_colr_v1_var_index_map, m_colr_v1_var_store,
                                       norm, true, 0, stops, kPaintStopsMax, extend,
                                       foreground_color);
        if (sink.set_radial_gradient)
            sink.set_radial_gradient(sink.ctx, x0, y0, r0, x1, y1, r1,
                                      extend, nstops, stops);
        return true;
    }
    case 8: { // PaintSweepGradient
        if (paint_off + 12 > dsz) return false;
        uint32_t cl_off = paint_off + read_offset24(d + paint_off + 1);
        float cx = read_i16(d + paint_off + 4);
        float cy = read_i16(d + paint_off + 6);
        float a0 = detail::read_f2dot14(d + paint_off + 8) * 3.14159265358979f;
        float a1 = detail::read_f2dot14(d + paint_off + 10) * 3.14159265358979f;

        ColorStop stops[kPaintStopsMax];
        GradientExtend extend = GradientExtend::pad;
        int nstops = read_color_line(*this, d, dsz, cl_off, palette_index,
                                       m_colr_v1_var_index_map, m_colr_v1_var_store,
                                       norm, false, 0, stops, kPaintStopsMax, extend,
                                       foreground_color);
        if (sink.set_sweep_gradient)
            sink.set_sweep_gradient(sink.ctx, cx, cy, a0, a1,
                                     extend, nstops, stops);
        return true;
    }
    case 9: { // PaintVarSweepGradient
        if (paint_off + 16 > dsz) return false;
        uint32_t cl_off = paint_off + read_offset24(d + paint_off + 1);
        float cx = read_i16(d + paint_off + 4);
        float cy = read_i16(d + paint_off + 6);
        float a0 = detail::read_f2dot14(d + paint_off + 8);
        float a1 = detail::read_f2dot14(d + paint_off + 10);
        uint32_t var_base = read_u32(d + paint_off + 12);
        cx += vd(var_base, 0); cy += vd(var_base, 1);
        a0 += vd(var_base, 2) / 16384.0f;
        a1 += vd(var_base, 3) / 16384.0f;

        ColorStop stops[kPaintStopsMax];
        GradientExtend extend = GradientExtend::pad;
        int nstops = read_color_line(*this, d, dsz, cl_off, palette_index,
                                       m_colr_v1_var_index_map, m_colr_v1_var_store,
                                       norm, true, 0, stops, kPaintStopsMax, extend,
                                       foreground_color);
        if (sink.set_sweep_gradient)
            sink.set_sweep_gradient(sink.ctx, cx, cy,
                                     a0 * 3.14159265358979f,
                                     a1 * 3.14159265358979f,
                                     extend, nstops, stops);
        return true;
    }
    case 10: { // PaintGlyph
        if (paint_off + 6 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        uint16_t glyph = read_u16(d + paint_off + 4);
        if (sink.push_clip_glyph)
            sink.push_clip_glyph(sink.ctx, glyph);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.fill_glyph)
            sink.fill_glyph(sink.ctx, glyph);
        if (sink.pop_clip)
            sink.pop_clip(sink.ctx);
        return true;
    }
    case 11: { // PaintColrGlyph
        if (paint_off + 3 > dsz) return false;
        uint16_t glyph = read_u16(d + paint_off + 1);
        uint32_t base_paint = find_v1_base_paint(d, dsz, m_colr_v1_base_list, glyph);
        if (!base_paint) return false;
        return walk_paint_v1(base_paint, palette_index, sink, foreground_color, var, depth + 1);
    }
    case 12: { // PaintTransform
        if (paint_off + 7 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        uint32_t xform_off = paint_off + read_offset24(d + paint_off + 4);
        if (xform_off + 24 > dsz) return false;
        float m[6] = {
            read_fixed(d + xform_off + 0),
            read_fixed(d + xform_off + 4),
            read_fixed(d + xform_off + 8),
            read_fixed(d + xform_off + 12),
            read_fixed(d + xform_off + 16),
            read_fixed(d + xform_off + 20)
        };
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 13: { // PaintVarTransform
        if (paint_off + 7 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        uint32_t xform_off = paint_off + read_offset24(d + paint_off + 4);
        // VarAffine2x3: 6 Fixed + uint32 varIndexBase = 28 bytes
        if (xform_off + 28 > dsz) return false;
        uint32_t var_base = read_u32(d + xform_off + 24);
        float m[6] = {
            read_fixed(d + xform_off + 0)  + vd(var_base, 0) / 65536.0f,
            read_fixed(d + xform_off + 4)  + vd(var_base, 1) / 65536.0f,
            read_fixed(d + xform_off + 8)  + vd(var_base, 2) / 65536.0f,
            read_fixed(d + xform_off + 12) + vd(var_base, 3) / 65536.0f,
            read_fixed(d + xform_off + 16) + vd(var_base, 4) / 65536.0f,
            read_fixed(d + xform_off + 20) + vd(var_base, 5) / 65536.0f,
        };
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 14: { // PaintTranslate
        if (paint_off + 8 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float dx = read_i16(d + paint_off + 4);
        float dy = read_i16(d + paint_off + 6);
        float m[6] = {1, 0, 0, 1, dx, dy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 15: { // PaintVarTranslate
        if (paint_off + 12 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float dx = read_i16(d + paint_off + 4);
        float dy = read_i16(d + paint_off + 6);
        uint32_t var_base = read_u32(d + paint_off + 8);
        dx += vd(var_base, 0); dy += vd(var_base, 1);
        float m[6] = {1, 0, 0, 1, dx, dy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 16: { // PaintScale
        if (paint_off + 8 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float sx = detail::read_f2dot14(d + paint_off + 4);
        float sy = detail::read_f2dot14(d + paint_off + 6);
        float m[6] = {sx, 0, 0, sy, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 17: { // PaintVarScale
        if (paint_off + 12 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float sx = detail::read_f2dot14(d + paint_off + 4);
        float sy = detail::read_f2dot14(d + paint_off + 6);
        uint32_t var_base = read_u32(d + paint_off + 8);
        sx += vd(var_base, 0) / 16384.0f;
        sy += vd(var_base, 1) / 16384.0f;
        float m[6] = {sx, 0, 0, sy, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 18: { // PaintScaleAroundCenter
        if (paint_off + 12 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float sx = detail::read_f2dot14(d + paint_off + 4);
        float sy = detail::read_f2dot14(d + paint_off + 6);
        float cx = read_i16(d + paint_off + 8);
        float cy = read_i16(d + paint_off + 10);
        float m[6] = {sx, 0, 0, sy, cx - sx * cx, cy - sy * cy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 19: { // PaintVarScaleAroundCenter
        if (paint_off + 16 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float sx = detail::read_f2dot14(d + paint_off + 4);
        float sy = detail::read_f2dot14(d + paint_off + 6);
        float cx = read_i16(d + paint_off + 8);
        float cy = read_i16(d + paint_off + 10);
        uint32_t var_base = read_u32(d + paint_off + 12);
        sx += vd(var_base, 0) / 16384.0f;
        sy += vd(var_base, 1) / 16384.0f;
        cx += vd(var_base, 2); cy += vd(var_base, 3);
        float m[6] = {sx, 0, 0, sy, cx - sx * cx, cy - sy * cy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 20: { // PaintScaleUniform
        if (paint_off + 6 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float s = detail::read_f2dot14(d + paint_off + 4);
        float m[6] = {s, 0, 0, s, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 21: { // PaintVarScaleUniform
        if (paint_off + 10 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float s = detail::read_f2dot14(d + paint_off + 4);
        uint32_t var_base = read_u32(d + paint_off + 6);
        s += vd(var_base, 0) / 16384.0f;
        float m[6] = {s, 0, 0, s, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 22: { // PaintScaleUniformAroundCenter
        if (paint_off + 10 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float s = detail::read_f2dot14(d + paint_off + 4);
        float cx = read_i16(d + paint_off + 6);
        float cy = read_i16(d + paint_off + 8);
        float m[6] = {s, 0, 0, s, cx - s * cx, cy - s * cy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 23: { // PaintVarScaleUniformAroundCenter
        if (paint_off + 14 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float s = detail::read_f2dot14(d + paint_off + 4);
        float cx = read_i16(d + paint_off + 6);
        float cy = read_i16(d + paint_off + 8);
        uint32_t var_base = read_u32(d + paint_off + 10);
        s += vd(var_base, 0) / 16384.0f;
        cx += vd(var_base, 1); cy += vd(var_base, 2);
        float m[6] = {s, 0, 0, s, cx - s * cx, cy - s * cy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 24: { // PaintRotate
        if (paint_off + 6 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float angle_norm = detail::read_f2dot14(d + paint_off + 4);
        float radians = angle_norm * 3.14159265358979f;
        float c = std::cos(radians), s = std::sin(radians);
        float m[6] = {c, s, -s, c, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 25: { // PaintVarRotate
        if (paint_off + 10 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float angle_norm = detail::read_f2dot14(d + paint_off + 4);
        uint32_t var_base = read_u32(d + paint_off + 6);
        angle_norm += vd(var_base, 0) / 16384.0f;
        float radians = angle_norm * 3.14159265358979f;
        float c = std::cos(radians), s = std::sin(radians);
        float m[6] = {c, s, -s, c, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 26: { // PaintRotateAroundCenter
        if (paint_off + 10 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float angle_norm = detail::read_f2dot14(d + paint_off + 4);
        float cx = read_i16(d + paint_off + 6);
        float cy = read_i16(d + paint_off + 8);
        float radians = angle_norm * 3.14159265358979f;
        float c = std::cos(radians), si = std::sin(radians);
        float m[6] = {c, si, -si, c,
                       cx - c * cx + si * cy,
                       cy - si * cx - c * cy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 27: { // PaintVarRotateAroundCenter
        if (paint_off + 14 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float angle_norm = detail::read_f2dot14(d + paint_off + 4);
        float cx = read_i16(d + paint_off + 6);
        float cy = read_i16(d + paint_off + 8);
        uint32_t var_base = read_u32(d + paint_off + 10);
        angle_norm += vd(var_base, 0) / 16384.0f;
        cx += vd(var_base, 1); cy += vd(var_base, 2);
        float radians = angle_norm * 3.14159265358979f;
        float c = std::cos(radians), si = std::sin(radians);
        float m[6] = {c, si, -si, c,
                       cx - c * cx + si * cy,
                       cy - si * cx - c * cy};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 28: { // PaintSkew
        if (paint_off + 8 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float xs = detail::read_f2dot14(d + paint_off + 4);
        float ys = detail::read_f2dot14(d + paint_off + 6);
        // Per FreeType convention: M = [[1, tan(ys)], [-tan(xs), 1]] applied as
        // row-major to (x, y). In column-major affine [a b c d tx ty]:
        //   a = 1, b = -tan(xs), c = tan(ys), d = 1
        float tx = std::tan(xs * 3.14159265358979f);
        float ty = std::tan(ys * 3.14159265358979f);
        float m[6] = {1, -tx, ty, 1, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 29: { // PaintVarSkew
        if (paint_off + 12 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float xs = detail::read_f2dot14(d + paint_off + 4);
        float ys = detail::read_f2dot14(d + paint_off + 6);
        uint32_t var_base = read_u32(d + paint_off + 8);
        xs += vd(var_base, 0) / 16384.0f;
        ys += vd(var_base, 1) / 16384.0f;
        float tx = std::tan(xs * 3.14159265358979f);
        float ty = std::tan(ys * 3.14159265358979f);
        float m[6] = {1, -tx, ty, 1, 0, 0};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 30: { // PaintSkewAroundCenter
        if (paint_off + 12 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float xs = detail::read_f2dot14(d + paint_off + 4);
        float ys = detail::read_f2dot14(d + paint_off + 6);
        float cx = read_i16(d + paint_off + 8);
        float cy = read_i16(d + paint_off + 10);
        float tx = std::tan(xs * 3.14159265358979f);
        float ty = std::tan(ys * 3.14159265358979f);
        // M = T(cx,cy) * Skew * T(-cx,-cy)
        // a=1, b=-tx, c=ty, d=1; tx_off=cx*(1-a) - c*cy = -ty*cy
        //                       ty_off=cy*(1-d) - b*cx =  tx*cx
        float m[6] = {1, -tx, ty, 1, -ty * cy, tx * cx};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 31: { // PaintVarSkewAroundCenter
        if (paint_off + 16 > dsz) return false;
        uint32_t sub_off = paint_off + read_offset24(d + paint_off + 1);
        float xs = detail::read_f2dot14(d + paint_off + 4);
        float ys = detail::read_f2dot14(d + paint_off + 6);
        float cx = read_i16(d + paint_off + 8);
        float cy = read_i16(d + paint_off + 10);
        uint32_t var_base = read_u32(d + paint_off + 12);
        xs += vd(var_base, 0) / 16384.0f;
        ys += vd(var_base, 1) / 16384.0f;
        cx += vd(var_base, 2); cy += vd(var_base, 3);
        float tx = std::tan(xs * 3.14159265358979f);
        float ty = std::tan(ys * 3.14159265358979f);
        float m[6] = {1, -tx, ty, 1, -ty * cy, tx * cx};
        if (sink.push_transform) sink.push_transform(sink.ctx, m);
        walk_paint_v1(sub_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_transform) sink.pop_transform(sink.ctx);
        return true;
    }
    case 32: { // PaintComposite
        if (paint_off + 8 > dsz) return false;
        uint32_t src_off = paint_off + read_offset24(d + paint_off + 1);
        uint8_t mode_byte = d[paint_off + 4];
        uint32_t back_off = paint_off + read_offset24(d + paint_off + 5);
        CompositeMode mode = (mode_byte <= 27)
            ? static_cast<CompositeMode>(mode_byte) : CompositeMode::src_over;

        if (sink.push_layer) sink.push_layer(sink.ctx);
        walk_paint_v1(back_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.push_layer) sink.push_layer(sink.ctx);
        walk_paint_v1(src_off, palette_index, sink, foreground_color, var, depth + 1);
        if (sink.pop_layer) sink.pop_layer(sink.ctx, mode);
        if (sink.pop_layer) sink.pop_layer(sink.ctx, CompositeMode::src_over);
        return true;
    }
    default:
        // Unknown paint format. No-op rather than crash.
        return false;
    }
}

bool FontData::colr_v1_paint(uint16_t base_glyph, int palette_index,
                               const PaintSink& sink,
                               uint32_t foreground_color,
                               const VariationInstance* var) const {
    if (!has_colr_v1()) return false;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint32_t paint = find_v1_base_paint(d, dsz, m_colr_v1_base_list, base_glyph);
    if (!paint) return false;
    return walk_paint_v1(paint, palette_index, sink, foreground_color, var, 0);
}

} // namespace kcg::otf
