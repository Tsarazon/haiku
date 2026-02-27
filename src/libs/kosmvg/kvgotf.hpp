// kvgotf.hpp - Internal OpenType/TrueType font data parser
// C++20 — read-only, zero-allocation font metrics & outline extraction

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <optional>
#include <span>
#include <string>

namespace kvg::otf {

// Self-contained outline types — no dependency on public API
struct OutlinePoint { float x = 0.0f, y = 0.0f; };

enum class OutlineCmd : uint8_t {
    move_to,
    line_to,
    quad_to,
    cubic_to,
    close
};

// -- Endian helpers (big-endian font data) --

inline constexpr uint8_t  read_u8(const uint8_t* p) { return *p; }
inline constexpr int8_t   read_i8(const uint8_t* p) { return static_cast<int8_t>(*p); }
inline constexpr uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] << 8 | p[1]);
}
inline constexpr int16_t read_i16(const uint8_t* p) {
    return static_cast<int16_t>(read_u16(p));
}
inline constexpr uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24
         | static_cast<uint32_t>(p[1]) << 16
         | static_cast<uint32_t>(p[2]) << 8
         | p[3];
}
inline constexpr int32_t read_i32(const uint8_t* p) {
    return static_cast<int32_t>(read_u32(p));
}

inline bool tag_eq(const uint8_t* p, const char* t) {
    return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3];
}

/// Convert a uint32_t tag to 4 chars for table lookup.
inline void tag_to_chars(uint32_t tag, char out[4]) {
    out[0] = static_cast<char>((tag >> 24) & 0xFF);
    out[1] = static_cast<char>((tag >> 16) & 0xFF);
    out[2] = static_cast<char>((tag >>  8) & 0xFF);
    out[3] = static_cast<char>(tag & 0xFF);
}

/// Read a 16.16 fixed-point number as float.
inline constexpr float read_fixed(const uint8_t* p) {
    return static_cast<float>(read_i32(p)) / 65536.0f;
}

// -- Structured return types --

struct VMetrics { int ascent = 0, descent = 0, line_gap = 0; };
struct HMetrics { int advance_width = 0, left_side_bearing = 0; };
struct BBox     { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

/// Location and size of a table within the font data.
struct TableLoc { uint32_t offset = 0; uint32_t length = 0; };

/// Variation axis descriptor (fvar table).
struct FvarAxis {
    uint32_t tag = 0;
    float min_value = 0.0f;
    float default_value = 0.0f;
    float max_value = 0.0f;
};

/// Kerning pair for bulk export.
struct KernPair {
    uint16_t g1 = 0, g2 = 0;
    int16_t value = 0;
};

// -- GlyphSink: type-erased callback for outline data --
// Contract: sink receives only MoveToPoint, AddLineToPoint, AddCurve, CloseSubpath.
// No AddQuadCurve — TrueType quadratics are converted to cubics by the parser.
// CloseSubpath receives n=0, pts=nullptr.

struct GlyphSink {
    using Func = void(*)(void* ctx, OutlineCmd cmd, const OutlinePoint* pts, int n);
    Func func = nullptr;
    void* ctx = nullptr;

    void move_to(float x, float y) const;
    void line_to(float x, float y) const;
    void cubic_to(float x1, float y1, float x2, float y2, float x3, float y3) const;
    void close() const;
};

// -- CFF internal data (parsed at open time) --

struct CFFBuffer {
    const uint8_t* data = nullptr;
    int cursor = 0;
    int size = 0;
};

struct CFFData {
    CFFBuffer cff;              // entire CFF table
    CFFBuffer char_strings;
    CFFBuffer global_subrs;
    CFFBuffer local_subrs;      // from Private DICT
    CFFBuffer font_dicts;       // CIDFont FDArray
    CFFBuffer fd_select;        // CIDFont FDSelect
};

// -- Table offsets --

struct Tables {
    uint32_t cmap = 0;
    uint32_t glyf = 0;
    uint32_t loca = 0;
    uint32_t head = 0;
    uint32_t hhea = 0;
    uint32_t hmtx = 0;
    uint32_t maxp = 0;
    uint32_t name = 0;
    uint32_t os2  = 0;
    uint32_t kern = 0;
    uint32_t gpos = 0;
    uint32_t fvar = 0;
    uint32_t colr = 0;
    uint32_t cpal = 0;
    uint32_t sbix = 0;
    uint32_t cbdt = 0;
    uint32_t cblc = 0;
    uint32_t svg  = 0;
};

// -- FontData: read-only view into font binary --

class FontData {
public:
    FontData() = default;

    /// Open a single face from font data. face_index selects within TTC.
    /// Returns nullopt if data is invalid or face_index is out of range.
    [[nodiscard]] static std::optional<FontData> open(
        std::span<const uint8_t> data, int face_index = 0);

    /// Number of faces in the font data (1 for single fonts, N for TTC).
    [[nodiscard]] static int face_count(std::span<const uint8_t> data);

    // -- Scaling --

    [[nodiscard]] float scale_for_em(float pixels) const;
    [[nodiscard]] float scale_for_height(float pixels) const;

    // -- Font-level metrics (in font units) --

    [[nodiscard]] VMetrics vmetrics() const;
    [[nodiscard]] std::optional<VMetrics> vmetrics_os2() const;
    [[nodiscard]] BBox bounding_box() const;
    [[nodiscard]] int units_per_em() const { return m_units_per_em; }
    [[nodiscard]] int num_glyphs() const { return m_num_glyphs; }

    /// OS/2 sxHeight (v2+). Returns 0 if unavailable.
    [[nodiscard]] int x_height() const;

    /// OS/2 sCapHeight (v2+). Returns 0 if unavailable.
    [[nodiscard]] int cap_height() const;

    // -- Glyph-level metrics --

    [[nodiscard]] HMetrics hmetrics(uint16_t glyph) const;
    [[nodiscard]] bool glyph_box(uint16_t glyph, BBox& box) const;
    [[nodiscard]] bool is_glyph_empty(uint16_t glyph) const;

    // -- Character mapping --

    [[nodiscard]] uint16_t glyph_index(uint32_t codepoint) const;

    /// Resolve codepoint with a Unicode Variation Selector (cmap format 14).
    /// Returns the variant glyph if defined, otherwise falls back to glyph_index().
    [[nodiscard]] uint16_t glyph_index_vs(uint32_t codepoint, uint32_t selector) const;

    // -- Kerning --

    [[nodiscard]] int kern_advance(uint16_t g1, uint16_t g2) const;

    /// Number of kerning pairs in the legacy kern table (format 0).
    [[nodiscard]] int kern_pair_count() const;

    /// Export all legacy kern pairs. Returns number written.
    int kern_pairs(std::span<KernPair> out) const;

    // -- Outline extraction --

    int glyph_outline(uint16_t glyph, const GlyphSink& sink) const;

    // -- SVG glyph data --

    /// Returns the raw SVG document bytes for a glyph, or empty span if none.
    [[nodiscard]] std::span<const uint8_t> glyph_svg(uint16_t glyph) const;

    // -- Name table --

    [[nodiscard]] std::optional<std::string> family_name() const;
    [[nodiscard]] std::optional<std::string> style_name() const;

    // -- Style detection --

    [[nodiscard]] uint16_t weight_class() const;
    [[nodiscard]] uint16_t mac_style() const;

    // -- Variable fonts (fvar) --

    [[nodiscard]] bool has_fvar() const;
    [[nodiscard]] int fvar_axis_count() const;
    [[nodiscard]] FvarAxis fvar_axis(int index) const;

    // -- Color font tables (presence detection) --

    [[nodiscard]] bool has_colr() const { return m_tables.colr != 0; }
    [[nodiscard]] bool has_cpal() const { return m_tables.cpal != 0; }
    [[nodiscard]] bool has_sbix() const { return m_tables.sbix != 0; }
    [[nodiscard]] bool has_cbdt() const { return m_tables.cbdt != 0 && m_tables.cblc != 0; }
    [[nodiscard]] bool has_svg()  const { return m_tables.svg != 0; }

    // -- CPAL palette access --

    [[nodiscard]] int cpal_palette_count() const;
    [[nodiscard]] int cpal_palette_size() const;
    /// Returns color as 0xAARRGGBB.
    [[nodiscard]] uint32_t cpal_color(int palette_index, int color_index) const;

    // -- Raw table access --

    /// Locate a table by its 4-byte tag string (e.g. "GPOS").
    [[nodiscard]] uint32_t find_table(const char* tag) const;

    /// Locate a table by uint32_t tag, returning both offset and length.
    [[nodiscard]] TableLoc locate_table(uint32_t tag) const;

    // -- Access --

    [[nodiscard]] bool valid() const { return m_data.data() != nullptr; }
    [[nodiscard]] std::span<const uint8_t> data() const { return m_data; }
    [[nodiscard]] int font_start() const { return m_font_start; }

private:
    std::span<const uint8_t> m_data;
    int m_font_start = 0;
    Tables m_tables;

    int m_units_per_em = 0;
    int m_num_glyphs = 0;
    int m_num_hmetrics = 0;
    bool m_loca_short = false;
    uint32_t m_index_map = 0;
    uint32_t m_vs_map = 0;   // cmap format 14 (Variation Selectors)

    bool m_is_cff = false;
    CFFData m_cff;

    int locate_glyph(uint16_t glyph) const;
    int parse_tt_glyph(uint16_t glyph, const GlyphSink& sink, int depth) const;
    int parse_tt_simple(const uint8_t* p, int num_contours, const GlyphSink& sink) const;
    int parse_tt_composite(const uint8_t* p, const GlyphSink& sink, int depth) const;
    int parse_cff_glyph(uint16_t glyph, const GlyphSink& sink) const;

    bool init_cff();
    std::optional<std::string> name_entry(int name_id) const;
    int kern_legacy(uint16_t g1, uint16_t g2) const;
    int kern_gpos(uint16_t g1, uint16_t g2) const;

    struct CsCtx;
    bool run_charstring(uint16_t glyph, CsCtx& ctx) const;
    CFFBuffer get_subr(const CFFBuffer& idx, int n) const;
    CFFBuffer cid_glyph_subrs(uint16_t glyph) const;

    bool cff_glyph_box(uint16_t glyph, BBox& box) const;
    bool cff_is_empty(uint16_t glyph) const;
};

// -- CFF buffer helpers (used internally in .cpp) --

namespace cff {

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

} // namespace cff

} // namespace kvg::otf
