// kcgotf_core.cpp - setup, font-level metadata, cmap, SVG, name, fvar, CPAL
// C++20, userspace

#include "kcgotf.hpp"
#include "kcgotf_internal.hpp"

#include <algorithm>

namespace kcg::otf {

// GlyphSink

void GlyphSink::move_to(float x, float y) const {
    OutlinePoint p{x, y};
    func(ctx, OutlineCmd::move_to, &p, 1);
}

void GlyphSink::line_to(float x, float y) const {
    OutlinePoint p{x, y};
    func(ctx, OutlineCmd::line_to, &p, 1);
}

void GlyphSink::quad_to(float cx, float cy, float x, float y) const {
    OutlinePoint pts[2] = {{cx, cy}, {x, y}};
    func(ctx, OutlineCmd::quad_to, pts, 2);
}

void GlyphSink::cubic_to(float x1, float y1, float x2, float y2,
                          float x3, float y3) const {
    OutlinePoint pts[3] = {{x1, y1}, {x2, y2}, {x3, y3}};
    func(ctx, OutlineCmd::cubic_to, pts, 3);
}

void GlyphSink::close() const {
    func(ctx, OutlineCmd::close, nullptr, 0);
}

// VariationInstance

VariationInstance::VariationInstance(const FontData& fd) : m_fd(&fd) {
    int n = fd.fvar_axis_count();
    if (n > kMaxAxes) n = kMaxAxes;
    m_count = n;
}

bool VariationInstance::set_axis(uint32_t tag, float user_value) {
    if (!m_fd) return false;
    int count = m_fd->fvar_axis_count();
    for (int i = 0; i < count && i < kMaxAxes; ++i) {
        FvarAxis ax = m_fd->fvar_axis(i);
        if (ax.tag == tag) {
            m_coords[i] = m_fd->normalize_axis(i, user_value);
            return true;
        }
    }
    return false;
}

void VariationInstance::set_normalized(int axis_index, float value) {
    if (axis_index < 0 || axis_index >= kMaxAxes) return;
    if (value < -1.0f) value = -1.0f;
    if (value >  1.0f) value =  1.0f;
    m_coords[axis_index] = value;
    if (axis_index >= m_count) m_count = axis_index + 1;
}

void VariationInstance::reset() {
    for (int i = 0; i < kMaxAxes; ++i) m_coords[i] = 0.0f;
}

bool VariationInstance::empty() const {
    for (int i = 0; i < m_count; ++i)
        if (m_coords[i] != 0.0f) return false;
    return true;
}

// Static helpers

static bool is_font(const uint8_t* p, size_t avail) {
    if (avail < 4) return false;
    if (p[0] == '1' && p[1] == 0 && p[2] == 0 && p[3] == 0) return true;
    if (tag_eq(p, "typ1")) return true;
    if (tag_eq(p, "OTTO")) return true;
    if (p[0] == 0 && p[1] == 1 && p[2] == 0 && p[3] == 0) return true;
    if (tag_eq(p, "true")) return true;
    return false;
}

static uint32_t find_table_raw(const uint8_t* data, size_t dsz,
                                uint32_t fontstart, const char* t) {
    if (fontstart + 6 > dsz) return 0;
    int num_tables = read_u16(data + fontstart + 4);
    uint32_t tabledir = fontstart + 12;
    for (int i = 0; i < num_tables; ++i) {
        uint32_t loc = tabledir + 16 * static_cast<uint32_t>(i);
        if (loc + 16 > dsz) return 0;
        if (tag_eq(data + loc, t))
            return read_u32(data + loc + 8);
    }
    return 0;
}

uint32_t FontData::find_table(const char* tag) const {
    return find_table_raw(m_data.data(), m_data.size(), m_font_start, tag);
}

TableLoc FontData::locate_table(uint32_t tag) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (static_cast<size_t>(m_font_start) + 6 > dsz) return {};

    int num_tables = read_u16(d + m_font_start + 4);
    uint32_t tabledir = m_font_start + 12;

    for (int i = 0; i < num_tables; ++i) {
        uint32_t loc = tabledir + 16 * static_cast<uint32_t>(i);
        if (static_cast<size_t>(loc) + 16 > dsz) return {};
        uint32_t t = read_u32(d + loc);
        if (t == tag) return {read_u32(d + loc + 8), read_u32(d + loc + 12)};
    }
    return {};
}

// face_count / open

int FontData::face_count(std::span<const uint8_t> data) {
    if (data.size() < 12) return 0;
    auto* d = data.data();
    if (is_font(d, data.size())) return 1;
    if (tag_eq(d, "ttcf")) {
        uint32_t ver = read_u32(d + 4);
        if (ver == 0x00010000 || ver == 0x00020000)
            return read_i32(d + 8);
    }
    return 0;
}

std::optional<FontData> FontData::open(std::span<const uint8_t> data,
                                        int face_index) {
    if (data.size() < 12 || face_index < 0) return std::nullopt;
    auto* d = data.data();
    size_t dsz = data.size();

    int offset = -1;
    if (is_font(d, dsz)) {
        offset = (face_index == 0) ? 0 : -1;
    } else if (tag_eq(d, "ttcf")) {
        uint32_t ver = read_u32(d + 4);
        if (ver == 0x00010000 || ver == 0x00020000) {
            int n = read_i32(d + 8);
            if (face_index >= n) return std::nullopt;
            size_t off = 12 + static_cast<size_t>(face_index) * 4;
            if (off + 4 > dsz) return std::nullopt;
            offset = static_cast<int>(read_u32(d + off));
        }
    }
    if (offset < 0) return std::nullopt;

    FontData f;
    f.m_data = data;
    f.m_font_start = offset;

    // Required tables
    f.m_tables.cmap = f.find_table("cmap");
    f.m_tables.head = f.find_table("head");
    f.m_tables.hhea = f.find_table("hhea");
    f.m_tables.hmtx = f.find_table("hmtx");

    // TT / CFF outline tables
    f.m_tables.glyf = f.find_table("glyf");
    f.m_tables.loca = f.find_table("loca");
    f.m_tables.maxp = f.find_table("maxp");

    // Metadata
    f.m_tables.name = f.find_table("name");
    f.m_tables.os2  = f.find_table("OS/2");

    // Positioning / substitution
    f.m_tables.kern = f.find_table("kern");
    f.m_tables.gpos = f.find_table("GPOS");
    f.m_tables.gsub = f.find_table("GSUB");

    // Variations
    f.m_tables.fvar = f.find_table("fvar");
    f.m_tables.gvar = f.find_table("gvar");
    f.m_tables.hvar = f.find_table("HVAR");
    f.m_tables.mvar = f.find_table("MVAR");
    f.m_tables.avar = f.find_table("avar");

    // Color
    f.m_tables.colr = f.find_table("COLR");
    f.m_tables.cpal = f.find_table("CPAL");
    f.m_tables.sbix = f.find_table("sbix");
    f.m_tables.cbdt = f.find_table("CBDT");
    f.m_tables.cblc = f.find_table("CBLC");
    f.m_tables.svg  = f.find_table("SVG ");

    if (!f.m_tables.cmap || !f.m_tables.head || !f.m_tables.hhea || !f.m_tables.hmtx)
        return std::nullopt;

    // TrueType vs CFF vs bitmap-only.
    if (f.m_tables.glyf) {
        if (!f.m_tables.loca) return std::nullopt;
        f.m_is_cff = false;
    } else if (f.find_table("CFF ") || f.find_table("CFF2")) {
        f.m_is_cff = true;
        if (!f.init_cff()) return std::nullopt;
    } else if (f.m_tables.cbdt || f.m_tables.sbix) {
        // Bitmap-only font (e.g. classic Noto Color Emoji): no outlines.
        f.m_is_cff = false;
    } else {
        return std::nullopt;
    }

    // head: unitsPerEm, indexToLocFormat
    if (static_cast<size_t>(f.m_tables.head) + 54 > dsz) return std::nullopt;
    f.m_units_per_em = read_u16(d + f.m_tables.head + 18);
    if (f.m_units_per_em == 0) return std::nullopt;
    f.m_loca_short = (read_u16(d + f.m_tables.head + 50) == 0);

    // hhea: numOfLongHorMetrics
    if (static_cast<size_t>(f.m_tables.hhea) + 36 > dsz) return std::nullopt;
    f.m_num_hmetrics = read_u16(d + f.m_tables.hhea + 34);

    // maxp: numGlyphs
    if (f.m_tables.maxp && static_cast<size_t>(f.m_tables.maxp) + 6 <= dsz)
        f.m_num_glyphs = read_u16(d + f.m_tables.maxp + 4);
    else
        f.m_num_glyphs = 0xFFFF;

    // cmap selection: prefer format 12 (Unicode full), then format 4 (BMP)
    uint32_t cmap = f.m_tables.cmap;
    if (cmap + 4 > dsz) return std::nullopt;
    int num_subtables = read_u16(d + cmap + 2);
    f.m_index_map = 0;
    f.m_vs_map = 0;
    for (int i = 0; i < num_subtables; ++i) {
        uint32_t rec = cmap + 4 + 8 * static_cast<uint32_t>(i);
        if (rec + 8 > dsz) break;
        uint16_t platform = read_u16(d + rec);
        uint16_t encoding = read_u16(d + rec + 2);

        if (platform == 0 && encoding == 5) {
            uint32_t subtable = cmap + read_u32(d + rec + 4);
            if (subtable + 2 <= dsz && read_u16(d + subtable) == 14)
                f.m_vs_map = subtable;
            continue;
        }

        if (platform == 0 ||
            (platform == 3 && (encoding == 1 || encoding == 10))) {
            f.m_index_map = cmap + read_u32(d + rec + 4);
            if (f.m_index_map + 2 <= dsz && read_u16(d + f.m_index_map) == 12)
                break;
        }
    }
    if (f.m_index_map == 0) return std::nullopt;

    f.resolve_kern_subtables();
    f.resolve_liga_subtables();
    f.resolve_colr();

    return f;
}

// Scaling

float FontData::scale_for_em(float pixels) const {
    return pixels / static_cast<float>(m_units_per_em);
}

float FontData::scale_for_height(float pixels) const {
    auto vm = vmetrics();
    int fheight = vm.ascent - vm.descent;
    if (fheight == 0) return 0.0f;
    return pixels / static_cast<float>(fheight);
}

// Font-level metrics

VMetrics FontData::vmetrics() const {
    auto* d = m_data.data();
    uint32_t hhea = m_tables.hhea;
    if (static_cast<size_t>(hhea) + 10 > m_data.size()) return {};
    return {read_i16(d + hhea + 4), read_i16(d + hhea + 6), read_i16(d + hhea + 8)};
}

std::optional<VMetrics> FontData::vmetrics_os2() const {
    uint32_t os2 = m_tables.os2;
    if (!os2) return std::nullopt;
    auto* d = m_data.data();
    if (static_cast<size_t>(os2) + 74 > m_data.size()) return std::nullopt;
    return VMetrics{read_i16(d + os2 + 68), read_i16(d + os2 + 70), read_i16(d + os2 + 72)};
}

BBox FontData::bounding_box() const {
    auto* d = m_data.data();
    uint32_t head = m_tables.head;
    if (static_cast<size_t>(head) + 44 > m_data.size()) return {};
    return {read_i16(d + head + 36), read_i16(d + head + 38),
            read_i16(d + head + 40), read_i16(d + head + 42)};
}

int FontData::x_height() const {
    uint32_t os2 = m_tables.os2;
    if (!os2) return 0;
    auto* d = m_data.data();
    if (static_cast<size_t>(os2) + 88 > m_data.size()) return 0;
    uint16_t version = read_u16(d + os2);
    if (version < 2) return 0;
    return read_i16(d + os2 + 86);
}

int FontData::cap_height() const {
    uint32_t os2 = m_tables.os2;
    if (!os2) return 0;
    auto* d = m_data.data();
    if (static_cast<size_t>(os2) + 90 > m_data.size()) return 0;
    uint16_t version = read_u16(d + os2);
    if (version < 2) return 0;
    return read_i16(d + os2 + 88);
}

// Glyph-level metrics (HVAR adjustment applied if var given and HVAR present)

HMetrics FontData::hmetrics(uint16_t glyph, const VariationInstance* var) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    int nhm = m_num_hmetrics;
    if (nhm == 0) return {};

    HMetrics m;
    if (glyph < nhm) {
        size_t off = m_tables.hmtx + static_cast<size_t>(glyph) * 4;
        if (off + 4 > dsz) return {};
        m = {static_cast<int>(read_u16(d + off)), read_i16(d + off + 2)};
    } else {
        size_t aw_off = m_tables.hmtx + static_cast<size_t>(nhm - 1) * 4;
        size_t lsb_off = m_tables.hmtx + static_cast<size_t>(nhm) * 4
                       + static_cast<size_t>(glyph - nhm) * 2;
        if (aw_off + 2 > dsz || lsb_off + 2 > dsz) return {};
        m = {static_cast<int>(read_u16(d + aw_off)), read_i16(d + lsb_off)};
    }

    if (var && has_hvar() && !var->empty()) {
        float aw_delta = hvar_advance_delta(glyph, *var);
        float lsb_delta = hvar_lsb_delta(glyph, *var);
        m.advance_width      += static_cast<int>(std::lround(aw_delta));
        m.left_side_bearing  += static_cast<int>(std::lround(lsb_delta));
    }

    return m;
}

// HVAR / MVAR - per-glyph and per-font metric deltas. Lookup is via
// detail::delta_set_index + detail::ivs_delta (defined in kcgotf_layout.cpp
// alongside other binary-table helpers).

float FontData::hvar_advance_delta(uint16_t glyph,
                                     const VariationInstance& var) const {
    uint32_t hvar = m_tables.hvar;
    if (!hvar || var.empty()) return 0.0f;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (hvar + 20 > dsz) return 0.0f;

    uint32_t ivs_off = hvar + read_u32(d + hvar + 4);
    uint32_t adv_map_off_raw = read_u32(d + hvar + 8);
    uint32_t adv_map_off = adv_map_off_raw ? hvar + adv_map_off_raw : 0;

    detail::IvsIndex idx;
    if (adv_map_off)
        idx = detail::delta_set_index(d, dsz, adv_map_off, glyph);
    else
        idx = {0, glyph};
    return detail::ivs_delta(d, dsz, ivs_off, idx.outer, idx.inner, var.normalized());
}

float FontData::hvar_lsb_delta(uint16_t glyph,
                                 const VariationInstance& var) const {
    uint32_t hvar = m_tables.hvar;
    if (!hvar || var.empty()) return 0.0f;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (hvar + 20 > dsz) return 0.0f;

    uint32_t lsb_map_off_raw = read_u32(d + hvar + 12);
    if (!lsb_map_off_raw) return 0.0f;
    uint32_t ivs_off = hvar + read_u32(d + hvar + 4);
    uint32_t lsb_map_off = hvar + lsb_map_off_raw;

    auto idx = detail::delta_set_index(d, dsz, lsb_map_off, glyph);
    return detail::ivs_delta(d, dsz, ivs_off, idx.outer, idx.inner, var.normalized());
}

float FontData::mvar_delta(uint32_t tag, const VariationInstance& var) const {
    uint32_t mvar = m_tables.mvar;
    if (!mvar || var.empty()) return 0.0f;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (mvar + 12 > dsz) return 0.0f;

    uint16_t value_record_size = read_u16(d + mvar + 6);
    uint16_t value_record_count = read_u16(d + mvar + 8);
    uint16_t ivs_off_rel = read_u16(d + mvar + 10);
    if (value_record_size < 8 || !ivs_off_rel) return 0.0f;
    uint32_t ivs_off = mvar + ivs_off_rel;

    uint32_t records_off = mvar + 12;
    if (records_off + size_t(value_record_count) * value_record_size > dsz) return 0.0f;

    for (int i = 0; i < value_record_count; ++i) {
        const uint8_t* rec = d + records_off + size_t(i) * value_record_size;
        uint32_t rec_tag = read_u32(rec);
        if (rec_tag != tag) continue;
        uint16_t outer = read_u16(rec + 4);
        uint16_t inner = read_u16(rec + 6);
        return detail::ivs_delta(d, dsz, ivs_off, outer, inner, var.normalized());
    }
    return 0.0f;
}

// Character mapping

uint16_t FontData::glyph_index(uint32_t codepoint) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint32_t im = m_index_map;
    if (im + 2 > dsz) return 0;

    uint16_t format = read_u16(d + im);

    if (format == 0) {
        if (im + 6 > dsz) return 0;
        int bytes = read_u16(d + im + 2);
        if (codepoint >= static_cast<uint32_t>(bytes - 6)) return 0;
        size_t off = im + 6 + codepoint;
        if (off >= dsz) return 0;
        return d[off];
    }

    if (format == 6) {
        if (im + 10 > dsz) return 0;
        uint32_t first = read_u16(d + im + 6);
        uint32_t count = read_u16(d + im + 8);
        if (codepoint < first || codepoint >= first + count) return 0;
        size_t off = im + 10 + (codepoint - first) * 2;
        if (off + 2 > dsz) return 0;
        return read_u16(d + off);
    }

    if (format == 10) {
        if (im + 20 > dsz) return 0;
        uint32_t first = read_u32(d + im + 12);
        uint32_t count = read_u32(d + im + 16);
        if (codepoint < first || codepoint >= first + count) return 0;
        size_t off = im + 20 + (codepoint - first) * 2;
        if (off + 2 > dsz) return 0;
        return read_u16(d + off);
    }

    if (format == 2) {
        if (im + 6 + 512 > dsz) return 0;
        uint32_t sub_base = im + 6 + 512;
        uint8_t high = static_cast<uint8_t>((codepoint >> 8) & 0xFF);
        uint8_t low  = static_cast<uint8_t>(codepoint & 0xFF);
        uint16_t sub_key = read_u16(d + im + 6 + high * 2);
        uint32_t sub = sub_base + sub_key;
        if (sub + 8 > dsz) return 0;
        uint16_t first = read_u16(d + sub);
        uint16_t count = read_u16(d + sub + 2);
        int16_t  delta = read_i16(d + sub + 4);
        uint16_t range = read_u16(d + sub + 6);
        uint8_t code = (sub_key == 0) ? static_cast<uint8_t>(codepoint) : low;
        if (code < first || code >= first + count) return 0;
        size_t goff = static_cast<size_t>(sub + 6) + range + (code - first) * 2;
        if (goff + 2 > dsz) return 0;
        uint16_t gi = read_u16(d + goff);
        if (gi == 0) return 0;
        return static_cast<uint16_t>((gi + delta) & 0xFFFF);
    }

    if (format == 4) {
        if (im + 14 > dsz) return 0;
        if (codepoint > 0xFFFF) return 0;
        uint16_t segcount = read_u16(d + im + 6) >> 1;
        if (segcount == 0) return 0;
        size_t table_end = im + 14 + static_cast<size_t>(segcount) * 8 + 2;
        if (table_end > dsz) return 0;

        uint16_t searchRange = read_u16(d + im + 8) >> 1;
        uint16_t entrySelector = read_u16(d + im + 10);
        uint16_t rangeShift = read_u16(d + im + 12) >> 1;

        uint32_t endCount = im + 14;
        uint32_t search = endCount;

        if (codepoint >= read_u16(d + search + rangeShift * 2))
            search += rangeShift * 2;

        search -= 2;
        while (entrySelector) {
            searchRange >>= 1;
            uint16_t end = read_u16(d + search + searchRange * 2);
            if (codepoint > end)
                search += searchRange * 2;
            --entrySelector;
        }
        search += 2;

        uint16_t item = static_cast<uint16_t>((search - endCount) >> 1);
        uint16_t start = read_u16(d + im + 14 + segcount * 2 + 2 + 2 * item);
        uint16_t last  = read_u16(d + endCount + 2 * item);
        if (codepoint < start || codepoint > last) return 0;
        uint16_t offset = read_u16(d + im + 14 + segcount * 6 + 2 + 2 * item);
        if (offset == 0)
            return static_cast<uint16_t>(codepoint + read_i16(d + im + 14 + segcount * 4 + 2 + 2 * item));
        return read_u16(d + offset + (codepoint - start) * 2 + im + 14 + segcount * 6 + 2 + 2 * item);
    }

    if (format == 12 || format == 13) {
        if (im + 16 > dsz) return 0;
        uint32_t ngroups = read_u32(d + im + 12);
        if (im + 16 + static_cast<size_t>(ngroups) * 12 > dsz) return 0;
        int lo = 0, hi = static_cast<int>(ngroups);
        while (lo < hi) {
            int mid = lo + ((hi - lo) >> 1);
            uint32_t start = read_u32(d + im + 16 + mid * 12);
            uint32_t end   = read_u32(d + im + 16 + mid * 12 + 4);
            if (codepoint < start) hi = mid;
            else if (codepoint > end) lo = mid + 1;
            else {
                uint32_t sg = read_u32(d + im + 16 + mid * 12 + 8);
                return static_cast<uint16_t>(
                    format == 12 ? sg + codepoint - start : sg);
            }
        }
        return 0;
    }

    return 0;
}

uint16_t FontData::glyph_index_vs(uint32_t codepoint, uint32_t selector) const {
    uint32_t vs = m_vs_map;
    if (!vs) return glyph_index(codepoint);

    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (vs + 10 > dsz) return glyph_index(codepoint);

    uint32_t num_records = read_u32(d + vs + 6);
    if (vs + 10 + static_cast<size_t>(num_records) * 11 > dsz)
        return glyph_index(codepoint);

    int lo = 0, hi = static_cast<int>(num_records) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        size_t rec = vs + 10 + static_cast<size_t>(mid) * 11;
        uint32_t var_sel = (static_cast<uint32_t>(d[rec]) << 16)
                         | (static_cast<uint32_t>(d[rec + 1]) << 8)
                         | d[rec + 2];
        if (selector < var_sel) { hi = mid - 1; continue; }
        if (selector > var_sel) { lo = mid + 1; continue; }

        uint32_t non_default_off = read_u32(d + rec + 7);
        if (non_default_off) {
            uint32_t nd = vs + non_default_off;
            if (nd + 4 <= dsz) {
                uint32_t num_mappings = read_u32(d + nd);
                int nlo = 0, nhi = static_cast<int>(num_mappings) - 1;
                while (nlo <= nhi) {
                    int nmid = (nlo + nhi) >> 1;
                    size_t moff = nd + 4 + static_cast<size_t>(nmid) * 5;
                    if (moff + 5 > dsz) break;
                    uint32_t uv = (static_cast<uint32_t>(d[moff]) << 16)
                                | (static_cast<uint32_t>(d[moff + 1]) << 8)
                                | d[moff + 2];
                    if (codepoint < uv) nhi = nmid - 1;
                    else if (codepoint > uv) nlo = nmid + 1;
                    else return read_u16(d + moff + 3);
                }
            }
        }

        uint32_t default_off = read_u32(d + rec + 3);
        if (default_off) {
            uint32_t df = vs + default_off;
            if (df + 4 <= dsz) {
                uint32_t num_ranges = read_u32(d + df);
                int dlo = 0, dhi = static_cast<int>(num_ranges) - 1;
                while (dlo <= dhi) {
                    int dmid = (dlo + dhi) >> 1;
                    size_t roff = df + 4 + static_cast<size_t>(dmid) * 4;
                    if (roff + 4 > dsz) break;
                    uint32_t start = (static_cast<uint32_t>(d[roff]) << 16)
                                   | (static_cast<uint32_t>(d[roff + 1]) << 8)
                                   | d[roff + 2];
                    uint32_t end = start + d[roff + 3];
                    if (codepoint < start) dhi = dmid - 1;
                    else if (codepoint > end) dlo = dmid + 1;
                    else return glyph_index(codepoint);
                }
            }
        }

        break;
    }
    return glyph_index(codepoint);
}

// SVG glyph data

std::span<const uint8_t> FontData::glyph_svg(uint16_t glyph) const {
    uint32_t svg = m_tables.svg;
    if (!svg) return {};

    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (svg + 10 > dsz) return {};
    uint32_t doc_list = svg + read_u32(d + svg + 2);
    if (doc_list + 2 > dsz) return {};

    uint16_t num_entries = read_u16(d + doc_list);
    if (doc_list + 2 + static_cast<size_t>(num_entries) * 12 > dsz)
        return {};

    int lo = 0, hi = num_entries - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        size_t rec = doc_list + 2 + static_cast<size_t>(mid) * 12;
        uint16_t start_glyph = read_u16(d + rec);
        uint16_t end_glyph   = read_u16(d + rec + 2);
        if (glyph < start_glyph) hi = mid - 1;
        else if (glyph > end_glyph) lo = mid + 1;
        else {
            uint32_t doc_off = read_u32(d + rec + 4);
            uint32_t doc_len = read_u32(d + rec + 8);
            size_t abs_off = doc_list + doc_off;
            if (abs_off + doc_len > dsz) return {};
            return {d + abs_off, doc_len};
        }
    }
    return {};
}

// Name table

static std::string decode_utf16be(const uint8_t* p, size_t len) {
    std::string result;
    result.reserve(len);
    size_t remaining = len;
    while (remaining >= 2) {
        uint16_t ch = static_cast<uint16_t>(p[0] * 256 + p[1]);
        if (ch >= 0xD800 && ch < 0xDC00 && remaining >= 4) {
            uint16_t ch2 = static_cast<uint16_t>(p[2] * 256 + p[3]);
            uint32_t c = ((ch - 0xD800u) << 10) + (ch2 - 0xDC00u) + 0x10000u;
            result += static_cast<char>(0xF0 + (c >> 18));
            result += static_cast<char>(0x80 + ((c >> 12) & 0x3F));
            result += static_cast<char>(0x80 + ((c >> 6) & 0x3F));
            result += static_cast<char>(0x80 + (c & 0x3F));
            p += 4; remaining -= 4;
        } else if (ch < 0x80) {
            result += static_cast<char>(ch);
            p += 2; remaining -= 2;
        } else if (ch < 0x800) {
            result += static_cast<char>(0xC0 + (ch >> 6));
            result += static_cast<char>(0x80 + (ch & 0x3F));
            p += 2; remaining -= 2;
        } else {
            result += static_cast<char>(0xE0 + (ch >> 12));
            result += static_cast<char>(0x80 + ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 + (ch & 0x3F));
            p += 2; remaining -= 2;
        }
    }
    return result;
}

static std::string decode_mac_roman(const uint8_t* p, size_t len) {
    static constexpr uint16_t table[128] = {
        0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
        0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
        0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
        0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
        0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
        0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
        0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
        0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
        0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
        0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
        0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
        0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
        0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
        0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
        0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
        0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7,
    };

    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        uint16_t ch = (p[i] < 0x80) ? p[i] : table[p[i] - 0x80];
        if (ch < 0x80) {
            result += static_cast<char>(ch);
        } else if (ch < 0x800) {
            result += static_cast<char>(0xC0 + (ch >> 6));
            result += static_cast<char>(0x80 + (ch & 0x3F));
        } else {
            result += static_cast<char>(0xE0 + (ch >> 12));
            result += static_cast<char>(0x80 + ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 + (ch & 0x3F));
        }
    }
    return result;
}

std::optional<std::string> FontData::name_entry(int name_id) const {
    uint32_t nm = m_tables.name;
    if (!nm) return std::nullopt;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (nm + 6 > dsz) return std::nullopt;

    int count = read_u16(d + nm + 2);
    uint32_t string_off = nm + read_u16(d + nm + 4);

    const uint8_t* unicode_ptr = nullptr;
    size_t unicode_len = 0;
    const uint8_t* roman_ptr = nullptr;
    size_t roman_len = 0;

    for (int i = 0; i < count; ++i) {
        uint32_t rec = nm + 6 + 12 * static_cast<uint32_t>(i);
        if (rec + 12 > dsz) break;
        if (read_u16(d + rec + 6) != name_id) continue;

        uint16_t platform = read_u16(d + rec);
        uint16_t encoding = read_u16(d + rec + 2);
        uint16_t nlen = read_u16(d + rec + 8);
        uint32_t noff = string_off + read_u16(d + rec + 10);
        if (noff + nlen > dsz) continue;

        if (platform == 1 && encoding == 0) {
            roman_ptr = d + noff;
            roman_len = nlen;
            continue;
        }
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) {
            unicode_ptr = d + noff;
            unicode_len = nlen;
            break;
        }
    }

    if (unicode_ptr) return decode_utf16be(unicode_ptr, unicode_len);
    if (roman_ptr)   return decode_mac_roman(roman_ptr, roman_len);
    return std::nullopt;
}

std::optional<std::string> FontData::family_name() const { return name_entry(1); }
std::optional<std::string> FontData::style_name()  const { return name_entry(2); }

// Style detection

uint16_t FontData::weight_class() const {
    uint32_t os2 = m_tables.os2;
    if (!os2) return 0;
    auto* d = m_data.data();
    if (os2 + 6 > m_data.size()) return 0;
    return read_u16(d + os2 + 4);
}

uint16_t FontData::mac_style() const {
    auto* d = m_data.data();
    uint32_t head = m_tables.head;
    if (static_cast<size_t>(head) + 46 > m_data.size()) return 0;
    return read_u16(d + head + 44);
}

// fvar

bool FontData::has_fvar() const { return m_tables.fvar != 0; }

int FontData::fvar_axis_count() const {
    uint32_t fvar = m_tables.fvar;
    if (!fvar) return 0;
    auto* d = m_data.data();
    if (static_cast<size_t>(fvar) + 10 > m_data.size()) return 0;
    return read_u16(d + fvar + 8);
}

FvarAxis FontData::fvar_axis(int index) const {
    uint32_t fvar = m_tables.fvar;
    if (!fvar || index < 0) return {};
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (static_cast<size_t>(fvar) + 12 > dsz) return {};

    uint16_t axes_offset = read_u16(d + fvar + 4);
    uint16_t axis_count = read_u16(d + fvar + 8);
    uint16_t axis_size = read_u16(d + fvar + 10);
    if (index >= axis_count || axis_size < 20) return {};

    size_t rec = static_cast<size_t>(fvar) + axes_offset
               + static_cast<size_t>(index) * axis_size;
    if (rec + 20 > dsz) return {};

    FvarAxis ax;
    ax.tag = read_u32(d + rec);
    ax.min_value = read_fixed(d + rec + 4);
    ax.default_value = read_fixed(d + rec + 8);
    ax.max_value = read_fixed(d + rec + 12);
    return ax;
}

float FontData::normalize_axis(int axis_index, float user_value) const {
    FvarAxis ax = fvar_axis(axis_index);
    float norm;
    if (user_value <= ax.default_value) {
        if (ax.default_value == ax.min_value) norm = 0.0f;
        else norm = (user_value - ax.default_value) / (ax.default_value - ax.min_value);
    } else {
        if (ax.max_value == ax.default_value) norm = 0.0f;
        else norm = (user_value - ax.default_value) / (ax.max_value - ax.default_value);
    }
    if (norm < -1.0f) norm = -1.0f;
    if (norm >  1.0f) norm =  1.0f;

    // avar: apply piecewise-linear segment map if present.
    uint32_t avar = m_tables.avar;
    if (avar) {
        auto* d = m_data.data();
        size_t dsz = m_data.size();
        if (avar + 8 > dsz) return norm;
        uint16_t avar_axes = read_u16(d + avar + 6);
        if (axis_index >= avar_axes) return norm;

        // Walk segment maps in order to reach axis_index
        size_t off = avar + 8;
        for (int i = 0; i <= axis_index; ++i) {
            if (off + 2 > dsz) return norm;
            uint16_t count = read_u16(d + off);
            off += 2;
            if (i == axis_index) {
                // Find pair around norm and lerp
                if (off + count * 4 > dsz) return norm;
                float prev_from = -1.0f, prev_to = -1.0f;
                for (int k = 0; k < count; ++k) {
                    float from = detail::read_f2dot14(d + off + k * 4);
                    float to   = detail::read_f2dot14(d + off + k * 4 + 2);
                    if (norm <= from) {
                        if (k == 0) return to;
                        if (from == prev_from) return to;
                        float t = (norm - prev_from) / (from - prev_from);
                        return prev_to + t * (to - prev_to);
                    }
                    prev_from = from;
                    prev_to = to;
                }
                return prev_to;
            }
            off += count * 4;
        }
    }
    return norm;
}

// CPAL

int FontData::cpal_palette_count() const {
    uint32_t cpal = m_tables.cpal;
    if (!cpal) return 0;
    auto* d = m_data.data();
    if (static_cast<size_t>(cpal) + 8 > m_data.size()) return 0;
    return read_u16(d + cpal + 4);
}

int FontData::cpal_palette_size() const {
    uint32_t cpal = m_tables.cpal;
    if (!cpal) return 0;
    auto* d = m_data.data();
    if (static_cast<size_t>(cpal) + 4 > m_data.size()) return 0;
    return read_u16(d + cpal + 2);
}

uint32_t FontData::cpal_color(int palette_index, int color_index) const {
    uint32_t cpal = m_tables.cpal;
    if (!cpal) return 0xFF000000;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (static_cast<size_t>(cpal) + 12 > dsz) return 0xFF000000;

    int num_entries = read_u16(d + cpal + 2);
    int num_palettes = read_u16(d + cpal + 4);
    uint32_t color_record_off = read_u32(d + cpal + 8);

    if (palette_index < 0 || palette_index >= num_palettes) return 0xFF000000;
    if (color_index < 0 || color_index >= num_entries) return 0xFF000000;

    size_t idx_off = static_cast<size_t>(cpal) + 12
                   + static_cast<size_t>(palette_index) * 2;
    if (idx_off + 2 > dsz) return 0xFF000000;
    int first_color = read_u16(d + idx_off);

    size_t rec = static_cast<size_t>(cpal) + color_record_off
               + static_cast<size_t>(first_color + color_index) * 4;
    if (rec + 4 > dsz) return 0xFF000000;

    uint8_t b = d[rec];
    uint8_t g = d[rec + 1];
    uint8_t r = d[rec + 2];
    uint8_t a = d[rec + 3];
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

} // namespace kcg::otf
