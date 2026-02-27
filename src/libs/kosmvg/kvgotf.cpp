// kvgotf.cpp — OpenType/TrueType font data parser implementation
// C++20

#include "kvgotf.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>

namespace kvg::otf {

// GlyphSink

void GlyphSink::move_to(float x, float y) const {
    OutlinePoint p{x, y};
    func(ctx, OutlineCmd::move_to, &p, 1);
}

void GlyphSink::line_to(float x, float y) const {
    OutlinePoint p{x, y};
    func(ctx, OutlineCmd::line_to, &p, 1);
}

void GlyphSink::cubic_to(float x1, float y1, float x2, float y2,
                          float x3, float y3) const {
    OutlinePoint pts[3] = {{x1, y1}, {x2, y2}, {x3, y3}};
    func(ctx, OutlineCmd::cubic_to, pts, 3);
}

void GlyphSink::close() const {
    func(ctx, OutlineCmd::close, nullptr, 0);
}

// CFF buffer helpers

namespace cff {

CFFBuffer get_index(CFFBuffer& b) {
    int start = b.cursor;
    int count = buf_get16(b);
    if (count) {
        int offsize = buf_get8(b);
        if (offsize < 1 || offsize > 4)
            return buf_range(b, start, b.cursor - start);
        buf_skip(b, offsize * count);
        buf_skip(b, static_cast<int>(buf_get(b, offsize)) - 1);
    }
    return buf_range(b, start, b.cursor - start);
}

int index_count(CFFBuffer& b) {
    buf_seek(b, 0);
    return buf_get16(b);
}

CFFBuffer index_get(CFFBuffer b, int i) {
    buf_seek(b, 0);
    int count = buf_get16(b);
    int offsize = buf_get8(b);
    if (i < 0 || i >= count) return {};
    if (offsize < 1 || offsize > 4) return {};
    buf_skip(b, i * offsize);
    int start = static_cast<int>(buf_get(b, offsize));
    int end = static_cast<int>(buf_get(b, offsize));
    return buf_range(b, 2 + (count + 1) * offsize + start, end - start);
}

uint32_t dict_int(CFFBuffer& b) {
    int b0 = buf_get8(b);
    if (b0 >= 32 && b0 <= 246)       return b0 - 139;
    else if (b0 >= 247 && b0 <= 250) return (b0 - 247) * 256 + buf_get8(b) + 108;
    else if (b0 >= 251 && b0 <= 254) return -(b0 - 251) * 256 - buf_get8(b) - 108;
    else if (b0 == 28)               return buf_get16(b);
    else if (b0 == 29)               return buf_get32(b);
    return 0;
}

void dict_skip_operand(CFFBuffer& b) {
    int b0 = buf_peek8(b);
    if (b0 < 28) { buf_skip(b, 1); return; }
    if (b0 == 30) {
        buf_skip(b, 1);
        while (b.cursor < b.size) {
            int v = buf_get8(b);
            if ((v & 0xF) == 0xF || (v >> 4) == 0xF) break;
        }
    } else {
        dict_int(b);
    }
}

CFFBuffer dict_get(CFFBuffer& b, int key) {
    buf_seek(b, 0);
    while (b.cursor < b.size) {
        int start = b.cursor;
        while (buf_peek8(b) >= 28)
            dict_skip_operand(b);
        int end = b.cursor;
        int op = buf_get8(b);
        if (op == 12) op = buf_get8(b) | 0x100;
        if (op == key) return buf_range(b, start, end - start);
    }
    return buf_range(b, 0, 0);
}

void dict_get_ints(CFFBuffer& b, int key, int outcount, uint32_t* out) {
    CFFBuffer operands = dict_get(b, key);
    for (int i = 0; i < outcount && operands.cursor < operands.size; i++)
        out[i] = dict_int(operands);
}

CFFBuffer get_subrs(const CFFBuffer& cff_buf, CFFBuffer fontdict) {
    uint32_t subrsoff = 0, private_loc[2] = {0, 0};
    dict_get_ints(fontdict, 18, 2, private_loc);
    if (!private_loc[1] || !private_loc[0]) return {};
    CFFBuffer pdict = buf_range(cff_buf, private_loc[1], private_loc[0]);
    dict_get_ints(pdict, 19, 1, &subrsoff);
    if (!subrsoff) return {};
    CFFBuffer cff_copy = cff_buf;
    buf_seek(cff_copy, private_loc[1] + subrsoff);
    return get_index(cff_copy);
}

} // namespace cff

// FontData - static helpers

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

// FontData::face_count / open

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

    // Resolve font offset
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

    // Locate required tables
    f.m_tables.cmap = f.find_table("cmap");
    f.m_tables.head = f.find_table("head");
    f.m_tables.hhea = f.find_table("hhea");
    f.m_tables.hmtx = f.find_table("hmtx");
    f.m_tables.glyf = f.find_table("glyf");
    f.m_tables.loca = f.find_table("loca");
    f.m_tables.maxp = f.find_table("maxp");
    f.m_tables.name = f.find_table("name");
    f.m_tables.os2  = f.find_table("OS/2");
    f.m_tables.kern = f.find_table("kern");
    f.m_tables.gpos = f.find_table("GPOS");
    f.m_tables.fvar = f.find_table("fvar");
    f.m_tables.colr = f.find_table("COLR");
    f.m_tables.cpal = f.find_table("CPAL");
    f.m_tables.sbix = f.find_table("sbix");
    f.m_tables.cbdt = f.find_table("CBDT");
    f.m_tables.cblc = f.find_table("CBLC");
    f.m_tables.svg  = f.find_table("SVG ");

    if (!f.m_tables.cmap || !f.m_tables.head || !f.m_tables.hhea || !f.m_tables.hmtx)
        return std::nullopt;

    // TrueType vs CFF
    if (f.m_tables.glyf) {
        if (!f.m_tables.loca) return std::nullopt;
        f.m_is_cff = false;
    } else {
        f.m_is_cff = true;
        if (!f.init_cff()) return std::nullopt;
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

    // cmap: find best Unicode subtable
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

        // Format 14 — Unicode Variation Selectors (platform 0, encoding 5)
        if (platform == 0 && encoding == 5) {
            uint32_t subtable = cmap + read_u32(d + rec + 4);
            if (subtable + 2 <= dsz && read_u16(d + subtable) == 14)
                f.m_vs_map = subtable;
            continue;
        }

        if (platform == 0 ||  // Unicode
            (platform == 3 && (encoding == 1 || encoding == 10))) {
            f.m_index_map = cmap + read_u32(d + rec + 4);
            // Prefer format 12 (full Unicode) over format 4 (BMP only)
            if (f.m_index_map + 2 <= dsz && read_u16(d + f.m_index_map) == 12)
                break;
        }
    }
    if (f.m_index_map == 0) return std::nullopt;

    return f;
}

// CFF initialization

bool FontData::init_cff() {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint32_t cff_off = find_table("CFF ");
    if (!cff_off) return false;

    m_cff.cff = cff::make_buf(d + cff_off,
        std::min<int>(static_cast<int>(dsz - cff_off), 512 * 1024 * 1024));

    CFFBuffer b = m_cff.cff;
    cff::buf_skip(b, 2);
    cff::buf_seek(b, cff::buf_get8(b)); // skip header

    cff::get_index(b);                              // name INDEX
    CFFBuffer topdictidx = cff::get_index(b);       // top DICT INDEX
    CFFBuffer topdict = cff::index_get(topdictidx, 0);
    cff::get_index(b);                              // string INDEX
    m_cff.global_subrs = cff::get_index(b);

    uint32_t charstrings = 0, cstype = 2;
    uint32_t fdarrayoff = 0, fdselectoff = 0;
    cff::dict_get_ints(topdict, 17, 1, &charstrings);            // CharStrings
    cff::dict_get_ints(topdict, 0x100 | 6, 1, &cstype);         // CharstringType
    cff::dict_get_ints(topdict, 0x100 | 36, 1, &fdarrayoff);    // FDArray
    cff::dict_get_ints(topdict, 0x100 | 37, 1, &fdselectoff);   // FDSelect
    m_cff.local_subrs = cff::get_subrs(m_cff.cff, topdict);

    if (cstype != 2 || charstrings == 0) return false;

    if (fdarrayoff) {
        if (!fdselectoff) return false;
        CFFBuffer b2 = m_cff.cff;
        cff::buf_seek(b2, fdarrayoff);
        m_cff.font_dicts = cff::get_index(b2);
        m_cff.fd_select = cff::buf_range(m_cff.cff, fdselectoff,
                                          m_cff.cff.size - fdselectoff);
    }

    CFFBuffer b3 = m_cff.cff;
    cff::buf_seek(b3, charstrings);
    m_cff.char_strings = cff::get_index(b3);

    return true;
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
    size_t dsz = m_data.size();
    uint32_t hhea = m_tables.hhea;
    if (static_cast<size_t>(hhea) + 10 > dsz) return {};
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

// Glyph-level metrics

HMetrics FontData::hmetrics(uint16_t glyph) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    int nhm = m_num_hmetrics;
    if (nhm == 0) return {};

    if (glyph < nhm) {
        size_t off = m_tables.hmtx + static_cast<size_t>(glyph) * 4;
        if (off + 4 > dsz) return {};
        return {static_cast<int>(read_u16(d + off)), read_i16(d + off + 2)};
    }
    // Past the long metrics — advance is from last entry, lsb from short array
    size_t aw_off = m_tables.hmtx + static_cast<size_t>(nhm - 1) * 4;
    size_t lsb_off = m_tables.hmtx + static_cast<size_t>(nhm) * 4
                   + static_cast<size_t>(glyph - nhm) * 2;
    if (aw_off + 2 > dsz || lsb_off + 2 > dsz) return {};
    return {static_cast<int>(read_u16(d + aw_off)), read_i16(d + lsb_off)};
}

bool FontData::glyph_box(uint16_t glyph, BBox& box) const {
    if (m_is_cff) return cff_glyph_box(glyph, box);
    int g = locate_glyph(glyph);
    if (g < 0) return false;
    auto* d = m_data.data();
    if (static_cast<size_t>(g) + 10 > m_data.size()) return false;
    box = {read_i16(d + g + 2), read_i16(d + g + 4),
           read_i16(d + g + 6), read_i16(d + g + 8)};
    return true;
}

bool FontData::is_glyph_empty(uint16_t glyph) const {
    if (m_is_cff) return cff_is_empty(glyph);
    int g = locate_glyph(glyph);
    if (g < 0) return true;
    if (static_cast<size_t>(g) + 2 > m_data.size()) return true;
    return read_i16(m_data.data() + g) == 0;
}

int FontData::locate_glyph(uint16_t glyph) const {
    if (m_is_cff || glyph >= m_num_glyphs) return -1;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint32_t loca = m_tables.loca;
    uint32_t glyf = m_tables.glyf;

    int g1, g2;
    if (m_loca_short) {
        size_t off = loca + static_cast<size_t>(glyph) * 2;
        if (off + 4 > dsz) return -1;
        g1 = glyf + read_u16(d + off) * 2;
        g2 = glyf + read_u16(d + off + 2) * 2;
    } else {
        size_t off = loca + static_cast<size_t>(glyph) * 4;
        if (off + 8 > dsz) return -1;
        g1 = glyf + read_u32(d + off);
        g2 = glyf + read_u32(d + off + 4);
    }
    if (g1 < 0 || static_cast<size_t>(g1) + 10 > dsz) return -1;
    return g1 == g2 ? -1 : g1;
}

// CMap

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

// Variation Selectors (cmap format 14)

uint16_t FontData::glyph_index_vs(uint32_t codepoint, uint32_t selector) const {
    uint32_t vs = m_vs_map;
    if (!vs) return glyph_index(codepoint);

    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (vs + 10 > dsz) return glyph_index(codepoint);

    uint32_t num_records = read_u32(d + vs + 6);
    // Each VariationSelector record: varSelector(3) + defaultUVSOffset(4) + nonDefaultUVSOffset(4) = 11
    if (vs + 10 + static_cast<size_t>(num_records) * 11 > dsz)
        return glyph_index(codepoint);

    // Binary search for the selector in the records
    int lo = 0, hi = static_cast<int>(num_records) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        size_t rec = vs + 10 + static_cast<size_t>(mid) * 11;
        uint32_t var_sel = (static_cast<uint32_t>(d[rec]) << 16)
                         | (static_cast<uint32_t>(d[rec + 1]) << 8)
                         | d[rec + 2];
        if (selector < var_sel) { hi = mid - 1; continue; }
        if (selector > var_sel) { lo = mid + 1; continue; }

        // Found the selector — check non-default UVS first
        uint32_t non_default_off = read_u32(d + rec + 7);
        if (non_default_off) {
            uint32_t nd = vs + non_default_off;
            if (nd + 4 <= dsz) {
                uint32_t num_mappings = read_u32(d + nd);
                // Each UVSMapping: unicodeValue(3) + glyphID(2) = 5
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

        // Check default UVS — if codepoint is in default ranges, use base mapping
        uint32_t default_off = read_u32(d + rec + 3);
        if (default_off) {
            uint32_t df = vs + default_off;
            if (df + 4 <= dsz) {
                uint32_t num_ranges = read_u32(d + df);
                // Each UnicodeRange: startUnicodeValue(3) + additionalCount(1) = 4
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

        // Selector found but codepoint not mapped — fall through
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
    // SVG table: version(2) + svgDocumentListOffset(4) + reserved(4)
    if (svg + 10 > dsz) return {};
    uint32_t doc_list = svg + read_u32(d + svg + 2);
    if (doc_list + 2 > dsz) return {};

    uint16_t num_entries = read_u16(d + doc_list);
    // Each SVGDocumentRecord: startGlyphID(2) + endGlyphID(2) + svgDocOffset(4) + svgDocLength(4) = 12
    if (doc_list + 2 + static_cast<size_t>(num_entries) * 12 > dsz)
        return {};

    // Binary search for glyph in SVG document index
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

// TrueType glyph outline

static constexpr int kMaxCompositeDepth = 32;

int FontData::glyph_outline(uint16_t glyph, const GlyphSink& sink) const {
    if (m_is_cff)
        return parse_cff_glyph(glyph, sink);
    return parse_tt_glyph(glyph, sink, 0);
}

int FontData::parse_tt_glyph(uint16_t glyph, const GlyphSink& sink,
                               int depth) const {
    if (depth > kMaxCompositeDepth) return hmetrics(glyph).advance_width;
    int g = locate_glyph(glyph);
    if (g < 0)
        return hmetrics(glyph).advance_width; // empty glyph

    auto* d = m_data.data();
    if (static_cast<size_t>(g) + 10 > m_data.size())
        return hmetrics(glyph).advance_width;

    int16_t num_contours = read_i16(d + g);
    if (num_contours > 0)
        parse_tt_simple(d + g, num_contours, sink);
    else if (num_contours == -1)
        parse_tt_composite(d + g, sink, depth);

    return hmetrics(glyph).advance_width;
}

// Emit a quadratic bezier as a cubic into the sink.
static void emit_quad_as_cubic(const GlyphSink& sink,
                                float ox, float oy,    // on-curve start
                                float cx, float cy,    // off-curve control
                                float ex, float ey) {  // on-curve end
    // Exact degree elevation: quad(P0,P1,P2) → cubic(P0, P0+2/3(P1-P0), P2+2/3(P1-P2), P2)
    float c1x = ox + (2.0f / 3.0f) * (cx - ox);
    float c1y = oy + (2.0f / 3.0f) * (cy - oy);
    float c2x = ex + (2.0f / 3.0f) * (cx - ex);
    float c2y = ey + (2.0f / 3.0f) * (cy - ey);
    sink.cubic_to(c1x, c1y, c2x, c2y, ex, ey);
}

int FontData::parse_tt_simple(const uint8_t* glyph_ptr, int num_contours,
                               const GlyphSink& sink) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    size_t base = glyph_ptr - d;

    size_t endPts_off = base + 10;
    if (endPts_off + num_contours * 2 + 2 > dsz) return 0;
    const uint8_t* endPts = d + endPts_off;

    int ins = read_u16(d + endPts_off + num_contours * 2);
    const uint8_t* pts = d + endPts_off + num_contours * 2 + 2 + ins;
    const uint8_t* data_end = d + dsz;
    if (pts >= data_end) return 0;

    int n = 1 + read_u16(endPts + num_contours * 2 - 2);

    // Decode flags
    struct RawPt { uint8_t flags; int16_t x, y; };
    // Use stack for small glyphs, heap for large
    constexpr int kStackLimit = 512;
    RawPt stack_buf[kStackLimit];
    std::unique_ptr<RawPt[]> heap_buf;
    RawPt* raw = stack_buf;
    if (n > kStackLimit) {
        heap_buf = std::make_unique<RawPt[]>(n);
        raw = heap_buf.get();
    }

    const uint8_t* p = pts;
    uint8_t flagcount = 0, flags = 0;
    for (int i = 0; i < n; ++i) {
        if (flagcount == 0) {
            if (p >= data_end) return 0;
            flags = *p++;
            if (flags & 8) {
                if (p >= data_end) return 0;
                flagcount = *p++;
            }
        } else --flagcount;
        raw[i].flags = flags;
    }

    // Decode X coordinates
    int x = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t fl = raw[i].flags;
        if (fl & 2) {
            if (p >= data_end) return 0;
            int16_t dx = *p++;
            x += (fl & 16) ? dx : -dx;
        } else if (!(fl & 16)) {
            if (p + 1 >= data_end) return 0;
            x += static_cast<int16_t>(p[0] * 256 + p[1]);
            p += 2;
        }
        raw[i].x = static_cast<int16_t>(x);
    }

    // Decode Y coordinates
    int y = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t fl = raw[i].flags;
        if (fl & 4) {
            if (p >= data_end) return 0;
            int16_t dy = *p++;
            y += (fl & 32) ? dy : -dy;
        } else if (!(fl & 32)) {
            if (p + 1 >= data_end) return 0;
            y += static_cast<int16_t>(p[0] * 256 + p[1]);
            p += 2;
        }
        raw[i].y = static_cast<int16_t>(y);
    }

    // Emit path commands contour by contour
    int pt_idx = 0;
    for (int c = 0; c < num_contours; ++c) {
        int end_pt = 1 + read_u16(endPts + c * 2);
        int count = end_pt - pt_idx;
        if (count < 1) { pt_idx = end_pt; continue; }

        // Find start point. TrueType contours can start with off-curve points.
        int first = pt_idx;
        int last = end_pt - 1;

        bool first_on = (raw[first].flags & 1);
        bool last_on  = (raw[last].flags & 1);

        float sx, sy;     // start point (on-curve)
        float scx, scy;   // start control (if contour starts off-curve)
        bool start_off = false;

        if (first_on) {
            sx = raw[first].x;
            sy = raw[first].y;
        } else if (last_on) {
            sx = raw[last].x;
            sy = raw[last].y;
            // We'll process last point as the start, skip it in the loop
            --last;
        } else {
            // Both off-curve: implied midpoint
            sx = (raw[first].x + raw[last].x) * 0.5f;
            sy = (raw[first].y + raw[last].y) * 0.5f;
            start_off = true;
            scx = static_cast<float>(raw[first].x);
            scy = static_cast<float>(raw[first].y);
        }

        sink.move_to(sx, sy);

        float last_on_x = sx, last_on_y = sy;
        float cx = 0, cy2 = 0;
        bool was_off = false;

        // If start was on first point, begin iteration from first+1.
        // If start was off (implied mid), first point is already used as scx/scy.
        int begin = first_on ? first + 1 : first + (start_off ? 1 : 0);

        for (int i = begin; i <= last; ++i) {
            float px = static_cast<float>(raw[i].x);
            float py = static_cast<float>(raw[i].y);
            bool on = (raw[i].flags & 1);

            if (on) {
                if (was_off) {
                    emit_quad_as_cubic(sink, last_on_x, last_on_y, cx, cy2, px, py);
                } else {
                    sink.line_to(px, py);
                }
                last_on_x = px;
                last_on_y = py;
                was_off = false;
            } else {
                if (was_off) {
                    // Implied on-curve midpoint
                    float mx = (cx + px) * 0.5f;
                    float my = (cy2 + py) * 0.5f;
                    emit_quad_as_cubic(sink, last_on_x, last_on_y, cx, cy2, mx, my);
                    last_on_x = mx;
                    last_on_y = my;
                }
                cx = px;
                cy2 = py;
                was_off = true;
            }
        }

        // Close contour
        if (start_off) {
            // Close back through the start control point
            if (was_off) {
                float mx = (cx + scx) * 0.5f;
                float my = (cy2 + scy) * 0.5f;
                emit_quad_as_cubic(sink, last_on_x, last_on_y, cx, cy2, mx, my);
                last_on_x = mx;
                last_on_y = my;
            }
            emit_quad_as_cubic(sink, last_on_x, last_on_y, scx, scy, sx, sy);
        } else {
            if (was_off) {
                emit_quad_as_cubic(sink, last_on_x, last_on_y, cx, cy2, sx, sy);
            } else if (last_on_x != sx || last_on_y != sy) {
                sink.line_to(sx, sy);
            }
        }
        sink.close();
        pt_idx = end_pt;
    }

    return 0; // advance handled by caller
}

int FontData::parse_tt_composite(const uint8_t* glyph_ptr, const GlyphSink& sink,
                                  int depth) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    const uint8_t* comp = glyph_ptr + 10;

    bool more = true;
    while (more) {
        if (comp + 4 > d + dsz) break;
        uint16_t cflags = read_u16(comp); comp += 2;
        uint16_t gidx   = read_u16(comp); comp += 2;

        float mtx[6] = {1, 0, 0, 1, 0, 0};

        if (cflags & 2) { // ARGS_ARE_XY_VALUES
            if (cflags & 1) { // ARG_1_AND_2_ARE_WORDS
                if (comp + 4 > d + dsz) break;
                mtx[4] = read_i16(comp); comp += 2;
                mtx[5] = read_i16(comp); comp += 2;
            } else {
                if (comp + 2 > d + dsz) break;
                mtx[4] = read_i8(comp); comp += 1;
                mtx[5] = read_i8(comp); comp += 1;
            }
        } else {
            break; // Unsupported: point-number positioning
        }

        if (cflags & (1 << 3)) { // WE_HAVE_A_SCALE
            if (comp + 2 > d + dsz) break;
            mtx[0] = mtx[3] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[1] = mtx[2] = 0;
        } else if (cflags & (1 << 6)) { // WE_HAVE_AN_X_AND_Y_SCALE
            if (comp + 4 > d + dsz) break;
            mtx[0] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[1] = mtx[2] = 0;
            mtx[3] = read_i16(comp) / 16384.0f; comp += 2;
        } else if (cflags & (1 << 7)) { // WE_HAVE_A_TWO_BY_TWO
            if (comp + 8 > d + dsz) break;
            mtx[0] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[1] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[2] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[3] = read_i16(comp) / 16384.0f; comp += 2;
        }

        // Create a transforming sink wrapper
        struct XformSink {
            const GlyphSink* parent;
            float m[6];
            static void callback(void* ctx, OutlineCmd cmd, const OutlinePoint* pts, int n) {
                auto& self = *static_cast<XformSink*>(ctx);
                if (n == 0) {
                    self.parent->func(self.parent->ctx, cmd, nullptr, 0);
                    return;
                }
                OutlinePoint tmp[3];
                for (int i = 0; i < n && i < 3; ++i) {
                    tmp[i].x = self.m[0] * pts[i].x + self.m[2] * pts[i].y + self.m[4];
                    tmp[i].y = self.m[1] * pts[i].x + self.m[3] * pts[i].y + self.m[5];
                }
                self.parent->func(self.parent->ctx, cmd, tmp, n);
            }
        };

        XformSink xsink;
        xsink.parent = &sink;
        std::memcpy(xsink.m, mtx, sizeof(mtx));

        GlyphSink child_sink{XformSink::callback, &xsink};
        parse_tt_glyph(gidx, child_sink, depth + 1);

        more = (cflags & (1 << 5)); // MORE_COMPONENTS
    }

    return 0;
}

// CFF charstring interpreter

struct FontData::CsCtx {
    const GlyphSink* sink = nullptr; // null = bounds-only pass
    bool started = false;
    float first_x = 0, first_y = 0;
    float x = 0, y = 0;
    int32_t min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    int num_vertices = 0;

    void track(float px, float py) {
        int ix = static_cast<int>(px), iy = static_cast<int>(py);
        if (ix > max_x || !started) max_x = ix;
        if (iy > max_y || !started) max_y = iy;
        if (ix < min_x || !started) min_x = ix;
        if (iy < min_y || !started) min_y = iy;
        started = true;
    }

    void cs_move_to(float dx, float dy) {
        cs_close_shape();
        first_x = x = x + dx;
        first_y = y = y + dy;
        track(x, y);
        num_vertices++;
        if (sink) sink->move_to(x, y);
    }

    void cs_line_to(float dx, float dy) {
        x += dx;
        y += dy;
        track(x, y);
        num_vertices++;
        if (sink) sink->line_to(x, y);
    }

    void cs_curve_to(float dx1, float dy1, float dx2, float dy2,
                     float dx3, float dy3) {
        float cx1 = x + dx1, cy1 = y + dy1;
        float cx2 = cx1 + dx2, cy2 = cy1 + dy2;
        x = cx2 + dx3;
        y = cy2 + dy3;
        track(cx1, cy1);
        track(cx2, cy2);
        track(x, y);
        num_vertices++;
        if (sink) sink->cubic_to(cx1, cy1, cx2, cy2, x, y);
    }

    void cs_close_shape() {
        if (first_x != x || first_y != y) {
            num_vertices++;
            if (sink) sink->line_to(first_x, first_y);
        }
        if (sink && started) sink->close();
    }
};

CFFBuffer FontData::get_subr(const CFFBuffer& idx, int n) const {
    int count = cff::index_count(const_cast<CFFBuffer&>(idx));
    int bias = 107;
    if (count >= 33900) bias = 32768;
    else if (count >= 1240) bias = 1131;
    n += bias;
    if (n < 0 || n >= count) return {};
    return cff::index_get(idx, n);
}

CFFBuffer FontData::cid_glyph_subrs(uint16_t glyph) const {
    CFFBuffer fds = m_cff.fd_select;
    int fdselector = -1;
    cff::buf_seek(fds, 0);
    int fmt = cff::buf_get8(fds);
    if (fmt == 0) {
        cff::buf_skip(fds, glyph);
        fdselector = cff::buf_get8(fds);
    } else if (fmt == 3) {
        int nranges = cff::buf_get16(fds);
        int start = cff::buf_get16(fds);
        for (int i = 0; i < nranges; ++i) {
            int v = cff::buf_get8(fds);
            int end = cff::buf_get16(fds);
            if (glyph >= start && glyph < end) {
                fdselector = v;
                break;
            }
            start = end;
        }
    }
    if (fdselector == -1) return {};
    return cff::get_subrs(m_cff.cff, cff::index_get(m_cff.font_dicts, fdselector));
}

bool FontData::run_charstring(uint16_t glyph, CsCtx& ctx) const {
    int in_header = 1, maskbits = 0, subr_stack_height = 0;
    int sp = 0, v, i, b0;
    int has_subrs = 0, clear_stack;
    float s[48];
    CFFBuffer subr_stack[10];
    CFFBuffer subrs = m_cff.local_subrs;
    float f;

    CFFBuffer b = cff::index_get(m_cff.char_strings, glyph);
    while (b.cursor < b.size) {
        i = 0;
        clear_stack = 1;
        b0 = cff::buf_get8(b);
        switch (b0) {
        case 0x13: // hintmask
        case 0x14: // cntrmask
            if (in_header) maskbits += (sp / 2);
            in_header = 0;
            cff::buf_skip(b, (maskbits + 7) / 8);
            break;

        case 0x01: // hstem
        case 0x03: // vstem
        case 0x12: // hstemhm
        case 0x17: // vstemhm
            maskbits += (sp / 2);
            break;

        case 0x15: // rmoveto
            in_header = 0;
            if (sp < 2) return false;
            ctx.cs_move_to(s[sp - 2], s[sp - 1]);
            break;
        case 0x04: // vmoveto
            in_header = 0;
            if (sp < 1) return false;
            ctx.cs_move_to(0, s[sp - 1]);
            break;
        case 0x16: // hmoveto
            in_header = 0;
            if (sp < 1) return false;
            ctx.cs_move_to(s[sp - 1], 0);
            break;

        case 0x05: // rlineto
            if (sp < 2) return false;
            for (; i + 1 < sp; i += 2)
                ctx.cs_line_to(s[i], s[i + 1]);
            break;

        case 0x07: // vlineto
            if (sp < 1) return false;
            goto vlineto;
        case 0x06: // hlineto
            if (sp < 1) return false;
            for (;;) {
                if (i >= sp) break;
                ctx.cs_line_to(s[i], 0);
                i++;
            vlineto:
                if (i >= sp) break;
                ctx.cs_line_to(0, s[i]);
                i++;
            }
            break;

        case 0x1F: // hvcurveto
            if (sp < 4) return false;
            goto hvcurveto;
        case 0x1E: // vhcurveto
            if (sp < 4) return false;
            for (;;) {
                if (i + 3 >= sp) break;
                ctx.cs_curve_to(0, s[i], s[i+1], s[i+2], s[i+3],
                                (sp - i == 5) ? s[i+4] : 0.0f);
                i += 4;
            hvcurveto:
                if (i + 3 >= sp) break;
                ctx.cs_curve_to(s[i], 0, s[i+1], s[i+2],
                                (sp - i == 5) ? s[i+4] : 0.0f, s[i+3]);
                i += 4;
            }
            break;

        case 0x08: // rrcurveto
            if (sp < 6) return false;
            for (; i + 5 < sp; i += 6)
                ctx.cs_curve_to(s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
            break;

        case 0x18: // rcurveline
            if (sp < 8) return false;
            for (; i + 5 < sp - 2; i += 6)
                ctx.cs_curve_to(s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
            if (i + 1 >= sp) return false;
            ctx.cs_line_to(s[i], s[i + 1]);
            break;

        case 0x19: // rlinecurve
            if (sp < 8) return false;
            for (; i + 1 < sp - 6; i += 2)
                ctx.cs_line_to(s[i], s[i + 1]);
            if (i + 5 >= sp) return false;
            ctx.cs_curve_to(s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
            break;

        case 0x1A: // vvcurveto
        case 0x1B: // hhcurveto
            if (sp < 4) return false;
            f = 0.0;
            if (sp & 1) { f = s[i]; i++; }
            for (; i + 3 < sp; i += 4) {
                if (b0 == 0x1B)
                    ctx.cs_curve_to(s[i], f, s[i+1], s[i+2], s[i+3], 0.0);
                else
                    ctx.cs_curve_to(f, s[i], s[i+1], s[i+2], 0.0, s[i+3]);
                f = 0.0;
            }
            break;

        case 0x0A: // callsubr
            if (!has_subrs) {
                if (m_cff.fd_select.size)
                    subrs = cid_glyph_subrs(glyph);
                has_subrs = 1;
            }
            [[fallthrough]];
        case 0x1D: // callgsubr
            if (sp < 1) return false;
            v = static_cast<int>(s[--sp]);
            if (subr_stack_height >= 10) return false;
            subr_stack[subr_stack_height++] = b;
            b = get_subr(b0 == 0x0A ? subrs : m_cff.global_subrs, v);
            if (b.size == 0) return false;
            b.cursor = 0;
            clear_stack = 0;
            break;

        case 0x0B: // return
            if (subr_stack_height <= 0) return false;
            b = subr_stack[--subr_stack_height];
            clear_stack = 0;
            break;

        case 0x0E: // endchar
            ctx.cs_close_shape();
            return true;

        case 0x0C: { // two-byte escape
            float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
            float dx, dy;
            int b1 = cff::buf_get8(b);
            switch (b1) {
            case 0x22: // hflex
                if (sp < 7) return false;
                dx1 = s[0]; dx2 = s[1]; dy2 = s[2]; dx3 = s[3];
                dx4 = s[4]; dx5 = s[5]; dx6 = s[6];
                ctx.cs_curve_to(dx1, 0, dx2, dy2, dx3, 0);
                ctx.cs_curve_to(dx4, 0, dx5, -dy2, dx6, 0);
                break;
            case 0x23: // flex
                if (sp < 13) return false;
                dx1=s[0]; dy1=s[1]; dx2=s[2]; dy2=s[3]; dx3=s[4]; dy3=s[5];
                dx4=s[6]; dy4=s[7]; dx5=s[8]; dy5=s[9]; dx6=s[10]; dy6=s[11];
                ctx.cs_curve_to(dx1, dy1, dx2, dy2, dx3, dy3);
                ctx.cs_curve_to(dx4, dy4, dx5, dy5, dx6, dy6);
                break;
            case 0x24: // hflex1
                if (sp < 9) return false;
                dx1=s[0]; dy1=s[1]; dx2=s[2]; dy2=s[3]; dx3=s[4];
                dx4=s[5]; dx5=s[6]; dy5=s[7]; dx6=s[8];
                ctx.cs_curve_to(dx1, dy1, dx2, dy2, dx3, 0);
                ctx.cs_curve_to(dx4, 0, dx5, dy5, dx6, -(dy1+dy2+dy5));
                break;
            case 0x25: // flex1
                if (sp < 11) return false;
                dx1=s[0]; dy1=s[1]; dx2=s[2]; dy2=s[3]; dx3=s[4]; dy3=s[5];
                dx4=s[6]; dy4=s[7]; dx5=s[8]; dy5=s[9];
                dx6 = dy6 = s[10];
                dx = dx1+dx2+dx3+dx4+dx5;
                dy = dy1+dy2+dy3+dy4+dy5;
                if (std::fabs(dx) > std::fabs(dy)) dy6 = -dy;
                else dx6 = -dx;
                ctx.cs_curve_to(dx1, dy1, dx2, dy2, dx3, dy3);
                ctx.cs_curve_to(dx4, dy4, dx5, dy5, dx6, dy6);
                break;
            default: return false;
            }
        } break;

        default:
            if (b0 != 255 && b0 != 28 && b0 < 32) return false;
            if (b0 == 255) {
                f = static_cast<float>(static_cast<int32_t>(cff::buf_get32(b))) / 0x10000;
            } else {
                cff::buf_skip(b, -1);
                f = static_cast<float>(static_cast<int16_t>(cff::dict_int(b)));
            }
            if (sp >= 48) return false;
            s[sp++] = f;
            clear_stack = 0;
            break;
        }
        if (clear_stack) sp = 0;
    }
    return false;
}

int FontData::parse_cff_glyph(uint16_t glyph, const GlyphSink& sink) const {
    CsCtx ctx;
    ctx.sink = &sink;
    run_charstring(glyph, ctx);
    return hmetrics(glyph).advance_width;
}

bool FontData::cff_glyph_box(uint16_t glyph, BBox& box) const {
    CsCtx ctx;
    ctx.sink = nullptr; // bounds-only
    if (!run_charstring(glyph, ctx)) return false;
    if (!ctx.started) return false;
    box = {ctx.min_x, ctx.min_y, ctx.max_x, ctx.max_y};
    return true;
}

bool FontData::cff_is_empty(uint16_t glyph) const {
    CsCtx ctx;
    ctx.sink = nullptr;
    if (!run_charstring(glyph, ctx)) return true;
    return ctx.num_vertices == 0;
}

// Kerning

int FontData::kern_advance(uint16_t g1, uint16_t g2) const {
    // GPOS first (higher priority per spec), then legacy kern
    int val = kern_gpos(g1, g2);
    if (val != 0) return val;
    return kern_legacy(g1, g2);
}

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

    if (read_u16(d + kern + 2) < 1) return 0;          // nTables
    if (read_u16(d + kern + 8) != 1) return 0;          // format + coverage check

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

// -- GPOS helpers --

/// Look up glyph in a Coverage table. Returns coverage index or -1.
static int coverage_index(const uint8_t* d, size_t dsz, uint32_t cov_off, uint16_t glyph) {
    if (cov_off + 4 > dsz) return -1;
    uint16_t fmt = read_u16(d + cov_off);

    if (fmt == 1) {
        // Format 1: sorted glyph array
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
        // Format 2: range array
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

/// Look up glyph class from a ClassDef table. Returns class value (0 = default).
static uint16_t classdef_lookup(const uint8_t* d, size_t dsz, uint32_t cd_off, uint16_t glyph) {
    if (cd_off + 4 > dsz) return 0;
    uint16_t fmt = read_u16(d + cd_off);

    if (fmt == 1) {
        // Format 1: array indexed by glyph - startGlyph
        uint16_t start = read_u16(d + cd_off + 2);
        uint16_t count = read_u16(d + cd_off + 4);
        if (glyph < start || glyph >= start + count) return 0;
        size_t off = cd_off + 6 + static_cast<size_t>(glyph - start) * 2;
        if (off + 2 > dsz) return 0;
        return read_u16(d + off);
    } else if (fmt == 2) {
        // Format 2: sorted range array — binary search
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

/// Count set bits in lower 8 bits — gives ValueRecord size in bytes.
static int value_record_size(uint16_t fmt) {
    int sz = 0;
    for (int bit = 0; bit < 8; ++bit)
        if (fmt & (1 << bit)) sz += 2;
    return sz;
}

/// Extract XAdvance from a ValueRecord given the value format and record start.
/// Returns 0 if XAdvance bit (0x04) is not set.
static int extract_xadvance(const uint8_t* d, size_t dsz, size_t rec_off, uint16_t vfmt) {
    if (!(vfmt & 0x04)) return 0;
    int byte_off = 0;
    if (vfmt & 0x01) byte_off += 2; // XPlacement
    if (vfmt & 0x02) byte_off += 2; // YPlacement
    size_t off = rec_off + byte_off;
    if (off + 2 > dsz) return 0;
    return read_i16(d + off);
}

int FontData::kern_gpos(uint16_t g1, uint16_t g2) const {
    uint32_t gpos = m_tables.gpos;
    if (!gpos) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();

    // GPOS header: version(4) + scriptListOff(2) + featureListOff(2) + lookupListOff(2)
    if (gpos + 10 > dsz) return 0;
    uint32_t feature_list = gpos + read_u16(d + gpos + 6);
    uint32_t lookup_list  = gpos + read_u16(d + gpos + 8);

    // Find 'kern' feature in feature list
    if (feature_list + 2 > dsz) return 0;
    int feature_count = read_u16(d + feature_list);

    for (int fi = 0; fi < feature_count; ++fi) {
        uint32_t frec = feature_list + 2 + static_cast<uint32_t>(fi) * 6;
        if (frec + 6 > dsz) break;
        if (!tag_eq(d + frec, "kern")) continue;

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
            // Extension lookup (type 9) — resolve to the real type
            bool is_extension = (lookup_type == 9);
            if (lookup_type != 2 && lookup_type != 9) continue;

            int subtable_count = read_u16(d + lookup + 4);
            for (int si = 0; si < subtable_count; ++si) {
                if (lookup + 6 + (si + 1) * 2 > dsz) break;
                uint32_t subtable = lookup + read_u16(d + lookup + 6 + si * 2);
                if (subtable + 4 > dsz) continue;

                // Resolve Extension subtable
                if (is_extension) {
                    if (subtable + 8 > dsz) continue;
                    uint16_t ext_fmt = read_u16(d + subtable);
                    if (ext_fmt != 1) continue;
                    uint16_t ext_type = read_u16(d + subtable + 2);
                    if (ext_type != 2) continue; // only PairAdjustment
                    subtable = subtable + read_u32(d + subtable + 4);
                    if (subtable + 4 > dsz) continue;
                }

                uint16_t pos_fmt = read_u16(d + subtable);

                if (pos_fmt == 1) {
                    // Format 1: Individual pairs
                    if (subtable + 10 > dsz) continue;
                    uint16_t vfmt1 = read_u16(d + subtable + 4);
                    uint16_t vfmt2 = read_u16(d + subtable + 6);
                    if (!(vfmt1 & 0x04)) continue; // need XAdvance

                    int vr1_sz = value_record_size(vfmt1);
                    int vr2_sz = value_record_size(vfmt2);

                    uint32_t cov_off = subtable + read_u16(d + subtable + 2);
                    int cov_idx = coverage_index(d, dsz, cov_off, g1);
                    if (cov_idx < 0) continue;

                    uint16_t pair_set_count = read_u16(d + subtable + 8);
                    if (cov_idx >= pair_set_count) continue;
                    uint32_t pairset = subtable + read_u16(d + subtable + 10 + cov_idx * 2);
                    if (pairset + 2 > dsz) continue;
                    int pvr_count = read_u16(d + pairset);
                    int pair_size = 2 + vr1_sz + vr2_sz;

                    // Binary search for g2 in PairSet
                    int lo = 0, hi = pvr_count - 1;
                    while (lo <= hi) {
                        int mid = (lo + hi) >> 1;
                        size_t poff = pairset + 2 + static_cast<size_t>(mid) * pair_size;
                        if (poff + 2 > dsz) break;
                        uint16_t second = read_u16(d + poff);
                        if (g2 < second) hi = mid - 1;
                        else if (g2 > second) lo = mid + 1;
                        else return extract_xadvance(d, dsz, poff + 2, vfmt1);
                    }

                } else if (pos_fmt == 2) {
                    // Format 2: Class-based pairs
                    // Layout: posFormat(2) coverageOff(2) vfmt1(2) vfmt2(2)
                    //         classDef1Off(2) classDef2Off(2) class1Count(2) class2Count(2)
                    //         class1Records[c1count * c2count * (vr1_sz + vr2_sz)]
                    if (subtable + 16 > dsz) continue;
                    uint16_t vfmt1 = read_u16(d + subtable + 4);
                    uint16_t vfmt2 = read_u16(d + subtable + 6);
                    if (!(vfmt1 & 0x04)) continue; // need XAdvance

                    // Coverage check: g1 must be covered
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

                    int vr1_sz = value_record_size(vfmt1);
                    int vr2_sz = value_record_size(vfmt2);
                    int rec_sz = vr1_sz + vr2_sz;
                    size_t rec_off = subtable + 16
                        + static_cast<size_t>(cls1) * c2count * rec_sz
                        + static_cast<size_t>(cls2) * rec_sz;

                    int val = extract_xadvance(d, dsz, rec_off, vfmt1);
                    if (val != 0) return val;
                }
            }
        }
    }
    return 0;
}

// Name table

// Decode big-endian UTF-16 → UTF-8
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

// Decode Mac Roman → UTF-8
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

    // Prefer Unicode/Microsoft, fallback to Mac Roman
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
            break; // best match
        }
    }

    if (unicode_ptr)
        return decode_utf16be(unicode_ptr, unicode_len);
    if (roman_ptr)
        return decode_mac_roman(roman_ptr, roman_len);
    return std::nullopt;
}

std::optional<std::string> FontData::family_name() const { return name_entry(1); }
std::optional<std::string> FontData::style_name() const  { return name_entry(2); }

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

// OS/2 extended metrics

int FontData::x_height() const {
    uint32_t os2 = m_tables.os2;
    if (!os2) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    // sxHeight is at offset 86, requires OS/2 version >= 2 (version at offset 0)
    if (static_cast<size_t>(os2) + 88 > dsz) return 0;
    uint16_t version = read_u16(d + os2);
    if (version < 2) return 0;
    return read_i16(d + os2 + 86);
}

int FontData::cap_height() const {
    uint32_t os2 = m_tables.os2;
    if (!os2) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    // sCapHeight is at offset 88, requires OS/2 version >= 2
    if (static_cast<size_t>(os2) + 90 > dsz) return 0;
    uint16_t version = read_u16(d + os2);
    if (version < 2) return 0;
    return read_i16(d + os2 + 88);
}

// Variable fonts (fvar)

bool FontData::has_fvar() const {
    return m_tables.fvar != 0;
}

int FontData::fvar_axis_count() const {
    uint32_t fvar = m_tables.fvar;
    if (!fvar) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    // fvar header: majorVersion(2) minorVersion(2) axesArrayOffset(2) reserved(2) axisCount(2)
    if (static_cast<size_t>(fvar) + 10 > dsz) return 0;
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

    // VariationAxisRecord: tag(4) minValue(Fixed) defaultValue(Fixed)
    //                      maxValue(Fixed) flags(2) axisNameID(2)
    FvarAxis ax;
    ax.tag = read_u32(d + rec);
    ax.min_value = read_fixed(d + rec + 4);
    ax.default_value = read_fixed(d + rec + 8);
    ax.max_value = read_fixed(d + rec + 12);
    return ax;
}

// CPAL color palettes

int FontData::cpal_palette_count() const {
    uint32_t cpal = m_tables.cpal;
    if (!cpal) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    // CPAL header: version(2) numPaletteEntries(2) numPalettes(2) numColorRecords(2)
    //              offsetFirstColorRecord(4) colorRecordIndices[numPalettes]
    if (static_cast<size_t>(cpal) + 8 > dsz) return 0;
    return read_u16(d + cpal + 4);
}

int FontData::cpal_palette_size() const {
    uint32_t cpal = m_tables.cpal;
    if (!cpal) return 0;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (static_cast<size_t>(cpal) + 4 > dsz) return 0;
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

    // colorRecordIndices array starts at offset 12
    size_t idx_off = static_cast<size_t>(cpal) + 12
                   + static_cast<size_t>(palette_index) * 2;
    if (idx_off + 2 > dsz) return 0xFF000000;
    int first_color = read_u16(d + idx_off);

    // Color record: {blue(1), green(1), red(1), alpha(1)}
    size_t rec = static_cast<size_t>(cpal) + color_record_off
               + static_cast<size_t>(first_color + color_index) * 4;
    if (rec + 4 > dsz) return 0xFF000000;

    uint8_t b = d[rec];
    uint8_t g = d[rec + 1];
    uint8_t r = d[rec + 2];
    uint8_t a = d[rec + 3];
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

// Raw table access

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
        if (t == tag) {
            return {read_u32(d + loc + 8), read_u32(d + loc + 12)};
        }
    }
    return {};
}

} // namespace kvg::otf
