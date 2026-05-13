// kcgotf.hpp - Internal OpenType/TrueType font data parser
// C++20, userspace

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <optional>
#include <span>
#include <string>

namespace kcg::otf {

// Outline types

struct OutlinePoint { float x = 0.0f, y = 0.0f; };

enum class OutlineCmd : uint8_t {
    move_to,
    line_to,
    quad_to,
    cubic_to,
    close
};

// Endian helpers (big-endian font data)

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

inline void tag_to_chars(uint32_t tag, char out[4]) {
    out[0] = static_cast<char>((tag >> 24) & 0xFF);
    out[1] = static_cast<char>((tag >> 16) & 0xFF);
    out[2] = static_cast<char>((tag >>  8) & 0xFF);
    out[3] = static_cast<char>(tag & 0xFF);
}

inline constexpr uint32_t make_tag(const char t[4]) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(t[0])) << 24)
         | (static_cast<uint32_t>(static_cast<uint8_t>(t[1])) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(t[2])) <<  8)
         |  static_cast<uint32_t>(static_cast<uint8_t>(t[3]));
}

/// Read a 16.16 fixed-point number as float.
inline constexpr float read_fixed(const uint8_t* p) {
    return static_cast<float>(read_i32(p)) / 65536.0f;
}

// Structured return types

struct VMetrics { int ascent = 0, descent = 0, line_gap = 0; };
struct HMetrics { int advance_width = 0, left_side_bearing = 0; };
struct BBox     { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct TableLoc { uint32_t offset = 0; uint32_t length = 0; };

struct FvarAxis {
    uint32_t tag = 0;
    float min_value = 0.0f;
    float default_value = 0.0f;
    float max_value = 0.0f;
};

struct KernPair {
    uint16_t g1 = 0, g2 = 0;
    int16_t value = 0;
};

// GlyphSink: type-erased callback for outline data

struct GlyphSink {
    using Func = void(*)(void* ctx, OutlineCmd cmd, const OutlinePoint* pts, int n);
    Func func = nullptr;
    void* ctx = nullptr;

    void move_to(float x, float y) const;
    void line_to(float x, float y) const;
    void quad_to(float cx, float cy, float x, float y) const;
    void cubic_to(float x1, float y1, float x2, float y2, float x3, float y3) const;
    void close() const;
};

// Color font formats

enum class ColorFontFormat : uint8_t {
    none,
    sbix,        // Apple Color Emoji (PNG/JPG bitmaps)
    cbdt,        // Noto Color Emoji classic (PNG bitmaps)
    colr_v1,     // Vector paint tree (Noto Color Emoji v2, EmojiOne, modern)
    colr_v0,     // Vector colored layers
    svg,         // Raw SVG documents per glyph
};

/// Strategy for picking among multiple color formats present in one font.
/// Affects `color_format()` resolution.
enum class ColorFormatPreference : uint8_t {
    /// sbix > cbdt > colr_v1 > colr_v0 > svg.
    /// Best for low-DPI displays and performance-constrained devices.
    bitmap_first,
    /// colr_v1 > colr_v0 > svg > sbix > cbdt.
    /// Best for HiDPI/4K displays where vector scales cleanly.
    vector_first,
    /// Bitmap when there's a strike within reasonable scaling distance of
    /// `target_ppem`; otherwise vector. Falls back to bitmap as last resort.
    auto_by_size,
};

// COLR v0: layered color glyph
struct ColrLayer {
    uint16_t glyph_id = 0;
    uint16_t palette_index = 0xFFFF;   // 0xFFFF = use foreground (text) color
};

// COLR v1: paint state callbacks

struct ColorStop {
    float offset = 0.0f;    // 0..1
    uint32_t argb = 0;
};

enum class GradientExtend : uint8_t {
    pad = 0,
    repeat = 1,
    reflect = 2
};

enum class CompositeMode : uint8_t {
    // Porter-Duff
    clear = 0,
    src,
    dest,
    src_over,
    dest_over,
    src_in,
    dest_in,
    src_out,
    dest_out,
    src_atop,
    dest_atop,
    xor_ = 11,
    // Separable blend
    plus = 12,
    screen,
    overlay,
    darken,
    lighten,
    color_dodge,
    color_burn,
    hard_light,
    soft_light,
    difference,
    exclusion,
    multiply,
    // Non-separable
    hsl_hue = 24,
    hsl_saturation,
    hsl_color,
    hsl_luminosity,
};

/// Callback interface for COLR v1 paint tree traversal.
/// Walker calls these in document order: set the paint source, then
/// fill the glyph; push/pop for transform/clip/layer scopes.
struct PaintSink {
    void* ctx = nullptr;

    // Color source (terminal paint types)
    void (*set_solid)(void* ctx, uint32_t argb) = nullptr;
    void (*set_linear_gradient)(void* ctx,
                                  float x0, float y0,
                                  float x1, float y1,
                                  float x2, float y2,
                                  GradientExtend extend,
                                  int num_stops,
                                  const ColorStop* stops) = nullptr;
    void (*set_radial_gradient)(void* ctx,
                                  float cx0, float cy0, float r0,
                                  float cx1, float cy1, float r1,
                                  GradientExtend extend,
                                  int num_stops,
                                  const ColorStop* stops) = nullptr;
    void (*set_sweep_gradient)(void* ctx,
                                 float cx, float cy,
                                 float start_angle, float end_angle,
                                 GradientExtend extend,
                                 int num_stops,
                                 const ColorStop* stops) = nullptr;

    // Fill glyph outline with current paint source
    void (*fill_glyph)(void* ctx, uint16_t glyph) = nullptr;

    // Transform stack (column-major 2x3: [a b c d tx ty] applies as
    //   x' = a*x + c*y + tx
    //   y' = b*x + d*y + ty)
    void (*push_transform)(void* ctx, const float m[6]) = nullptr;
    void (*pop_transform)(void* ctx) = nullptr;

    // Layer stack (for composite). push_layer begins an offscreen group;
    // pop_layer composites it back with the given mode.
    void (*push_layer)(void* ctx) = nullptr;
    void (*pop_layer)(void* ctx, CompositeMode mode) = nullptr;

    // Clip stack
    void (*push_clip_glyph)(void* ctx, uint16_t glyph) = nullptr;
    void (*pop_clip)(void* ctx) = nullptr;
};

// sbix glyph data
struct SbixGlyph {
    std::span<const uint8_t> data;
    uint32_t format = 0;        // 'png ', 'jpg ', 'tiff', or 0
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    uint16_t ppem = 0;
    uint16_t ppi = 0;
};

// CBDT glyph data
struct CbdtGlyph {
    std::span<const uint8_t> data;
    uint8_t format = 0;         // 17, 18, 19 typical for color emoji (PNG)
    int8_t bearing_x = 0;
    int8_t bearing_y = 0;
    uint8_t advance = 0;
    uint16_t ppem_x = 0;
    uint16_t ppem_y = 0;
};

// GSUB liga
struct LigatureMatch {
    uint16_t glyph = 0;     // 0 if no match
    int consumed = 0;       // number of input glyphs consumed (0 if no match)
};

// Variations

class FontData;

/// Per-instance variation coordinates. Decouples variation state from
/// the const FontData; safe for concurrent rendering with different
/// instances over the same FontData.
class VariationInstance {
public:
    static constexpr int kMaxAxes = 32;

    VariationInstance() = default;
    explicit VariationInstance(const FontData& fd);

    /// Set an axis by tag, using user-space value.
    /// Returns false if the axis isn't in the font.
    bool set_axis(uint32_t tag, float user_value);

    /// Set a normalized value directly by axis index [-1, 1].
    void set_normalized(int axis_index, float value);

    /// Clear all axes to default (zero deltas).
    void reset();

    [[nodiscard]] int axis_count() const { return m_count; }
    [[nodiscard]] std::span<const float> normalized() const {
        return {m_coords, static_cast<size_t>(m_count)};
    }
    [[nodiscard]] bool empty() const;

private:
    const FontData* m_fd = nullptr;
    float m_coords[kMaxAxes] = {};
    int m_count = 0;
};

// CFF internals — opaque structs needed for FontData private members.
// Functional CFF helpers live in kcgotf_internal.hpp.

struct CFFBuffer {
    const uint8_t* data = nullptr;
    int cursor = 0;
    int size = 0;
};

struct CFFData {
    CFFBuffer cff;
    CFFBuffer char_strings;
    CFFBuffer global_subrs;
    CFFBuffer local_subrs;
    CFFBuffer font_dicts;
    CFFBuffer fd_select;
};

// Table offsets

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
    uint32_t gsub = 0;
    uint32_t fvar = 0;
    uint32_t colr = 0;
    uint32_t cpal = 0;
    uint32_t sbix = 0;
    uint32_t cbdt = 0;
    uint32_t cblc = 0;
    uint32_t svg  = 0;
    uint32_t gvar = 0;
    uint32_t hvar = 0;
    uint32_t mvar = 0;
    uint32_t avar = 0;
};

// FontData

class FontData {
public:
    FontData() = default;

    [[nodiscard]] static std::optional<FontData> open(
        std::span<const uint8_t> data, int face_index = 0);

    [[nodiscard]] static int face_count(std::span<const uint8_t> data);

    // Scaling

    [[nodiscard]] float scale_for_em(float pixels) const;
    [[nodiscard]] float scale_for_height(float pixels) const;

    // Font-level metrics

    [[nodiscard]] VMetrics vmetrics() const;
    [[nodiscard]] std::optional<VMetrics> vmetrics_os2() const;
    [[nodiscard]] BBox bounding_box() const;
    [[nodiscard]] int units_per_em() const { return m_units_per_em; }
    [[nodiscard]] int num_glyphs() const { return m_num_glyphs; }
    [[nodiscard]] int x_height() const;
    [[nodiscard]] int cap_height() const;

    // Glyph-level metrics

    [[nodiscard]] HMetrics hmetrics(uint16_t glyph,
                                     const VariationInstance* var = nullptr) const;
    [[nodiscard]] bool glyph_box(uint16_t glyph, BBox& box) const;
    [[nodiscard]] bool is_glyph_empty(uint16_t glyph) const;

    // Character mapping

    [[nodiscard]] uint16_t glyph_index(uint32_t codepoint) const;
    [[nodiscard]] uint16_t glyph_index_vs(uint32_t codepoint, uint32_t selector) const;

    // Kerning

    [[nodiscard]] int kern_advance(uint16_t g1, uint16_t g2) const;
    [[nodiscard]] int kern_pair_count() const;
    int kern_pairs(std::span<KernPair> out) const;

    // GSUB: standard ligatures ('liga' feature, LookupType 4).
    // Given a sequence of glyphs, returns the longest matching ligature.
    [[nodiscard]] LigatureMatch liga_lookup(std::span<const uint16_t> glyphs) const;

    /// Returns true if the font's 'liga' feature has any active subtables.
    [[nodiscard]] bool has_liga() const { return m_liga_subtable_count > 0; }

    // Outline extraction

    int glyph_outline(uint16_t glyph, const GlyphSink& sink,
                       const VariationInstance* var = nullptr) const;

    // SVG glyph data

    [[nodiscard]] std::span<const uint8_t> glyph_svg(uint16_t glyph) const;

    // Name table

    [[nodiscard]] std::optional<std::string> family_name() const;
    [[nodiscard]] std::optional<std::string> style_name() const;

    // Style detection

    [[nodiscard]] uint16_t weight_class() const;
    [[nodiscard]] uint16_t mac_style() const;

    // Variable fonts (fvar)

    [[nodiscard]] bool has_fvar() const;
    [[nodiscard]] int fvar_axis_count() const;
    [[nodiscard]] FvarAxis fvar_axis(int index) const;

    /// Normalize a user-space axis value to [-1, 1] using fvar (+avar if present).
    [[nodiscard]] float normalize_axis(int axis_index, float user_value) const;

    // gvar: applies TupleVariation deltas to TrueType glyph points.
    // Called internally by glyph_outline when a VariationInstance is given.
    [[nodiscard]] bool has_gvar() const { return m_tables.gvar != 0; }

    // HVAR: variable horizontal advance/lsb deltas.
    [[nodiscard]] bool has_hvar() const { return m_tables.hvar != 0; }

    /// HVAR advance delta in font units. Returns 0 if no HVAR or zero delta.
    [[nodiscard]] float hvar_advance_delta(uint16_t glyph,
                                            const VariationInstance& var) const;
    [[nodiscard]] float hvar_lsb_delta(uint16_t glyph,
                                        const VariationInstance& var) const;

    // MVAR: variable font-level metrics.
    [[nodiscard]] bool has_mvar() const { return m_tables.mvar != 0; }

    /// MVAR delta for a metric tag (e.g. 'hasc' for ascent). Returns 0 if absent.
    [[nodiscard]] float mvar_delta(uint32_t tag, const VariationInstance& var) const;

    // Color font tables

    [[nodiscard]] bool has_colr_v0() const;
    [[nodiscard]] bool has_colr_v1() const;
    [[nodiscard]] bool has_sbix() const { return m_tables.sbix != 0; }
    [[nodiscard]] bool has_cbdt() const { return m_tables.cbdt != 0 && m_tables.cblc != 0; }
    [[nodiscard]] bool has_svg()  const { return m_tables.svg != 0; }
    [[nodiscard]] bool has_cpal() const { return m_tables.cpal != 0; }

    /// Determine the best color format for a glyph given a preference policy.
    /// `target_ppem` is the intended rendering size in pixels (used by
    /// `auto_by_size`; ignored by other policies). Pass 0 to fall back to
    /// font's units_per_em for bitmap strike selection.
    [[nodiscard]] ColorFontFormat color_format(
        uint16_t glyph,
        ColorFormatPreference pref = ColorFormatPreference::bitmap_first,
        uint16_t target_ppem = 0) const;

    // CPAL palette access

    [[nodiscard]] int cpal_palette_count() const;
    [[nodiscard]] int cpal_palette_size() const;
    [[nodiscard]] uint32_t cpal_color(int palette_index, int color_index) const;

    // COLR v0

    /// Number of layers for a base glyph in COLR v0, or 0.
    [[nodiscard]] int colr_v0_layer_count(uint16_t base_glyph) const;

    /// Fill `out` with layers for base_glyph. Returns count written.
    int colr_v0_layers(uint16_t base_glyph, std::span<ColrLayer> out) const;

    // COLR v1: walk the paint tree for a base glyph through `sink`.
    // Optional `var` enables variable-color paints (PaintVarSolid,
    // PaintVarLinearGradient, etc). `foreground_color` is used when a paint
    // references palette index 0xFFFF (the "current text color" indirection
    // in the COLR spec); defaults to opaque black.
    // Returns true on success, false if base_glyph has no COLR v1 paint.
    bool colr_v1_paint(uint16_t base_glyph, int palette_index,
                        const PaintSink& sink,
                        uint32_t foreground_color = 0xFF000000,
                        const VariationInstance* var = nullptr) const;

    // sbix

    [[nodiscard]] int sbix_strike_count() const;
    [[nodiscard]] uint16_t sbix_strike_ppem(int strike) const;
    [[nodiscard]] uint16_t sbix_strike_ppi(int strike) const;

    /// Find the strike with ppem closest to (and at least) target_ppem.
    /// Returns -1 if no strikes.
    [[nodiscard]] int sbix_best_strike(uint16_t target_ppem) const;

    [[nodiscard]] SbixGlyph sbix_glyph(int strike, uint16_t glyph) const;

    // CBDT/CBLC

    [[nodiscard]] int cblc_strike_count() const;
    [[nodiscard]] uint16_t cblc_strike_ppem(int strike) const;
    [[nodiscard]] int cblc_best_strike(uint16_t target_ppem) const;
    [[nodiscard]] CbdtGlyph cbdt_glyph(int strike, uint16_t glyph) const;

    // Raw table access

    [[nodiscard]] uint32_t find_table(const char* tag) const;
    [[nodiscard]] TableLoc locate_table(uint32_t tag) const;

    // Access

    [[nodiscard]] bool valid() const { return m_data.data() != nullptr; }
    [[nodiscard]] std::span<const uint8_t> data() const { return m_data; }
    [[nodiscard]] int font_start() const { return m_font_start; }
    [[nodiscard]] const Tables& tables() const { return m_tables; }

private:
    std::span<const uint8_t> m_data;
    int m_font_start = 0;
    Tables m_tables;

    int m_units_per_em = 0;
    int m_num_glyphs = 0;
    int m_num_hmetrics = 0;
    bool m_loca_short = false;
    uint32_t m_index_map = 0;
    uint32_t m_vs_map = 0;

    bool m_is_cff = false;
    CFFData m_cff;

    // Cached GPOS kern subtable offsets
    static constexpr int kMaxKernSubtables = 8;
    struct KernSubtable {
        uint32_t offset = 0;
        uint16_t format = 0;
        uint16_t vfmt1 = 0;
        uint16_t vfmt2 = 0;
    };
    KernSubtable m_kern_subtables[kMaxKernSubtables];
    int m_kern_subtable_count = 0;
    void resolve_kern_subtables();

    // Cached GSUB liga subtable offsets
    static constexpr int kMaxLigaSubtables = 8;
    uint32_t m_liga_subtables[kMaxLigaSubtables] = {};
    int m_liga_subtable_count = 0;
    void resolve_liga_subtables();

    // Cached COLR locations
    uint32_t m_colr_v0_base_records = 0;
    uint16_t m_colr_v0_num_base = 0;
    uint32_t m_colr_v0_layer_records = 0;
    uint16_t m_colr_v0_num_layers = 0;

    // COLR v1
    uint32_t m_colr_v1_base_list = 0;
    uint32_t m_colr_v1_layer_list = 0;
    uint32_t m_colr_v1_clip_list = 0;
    uint32_t m_colr_v1_var_index_map = 0;
    uint32_t m_colr_v1_var_store = 0;
    uint16_t m_colr_v1_num_base = 0;

    void resolve_colr();

    int locate_glyph(uint16_t glyph) const;
    int parse_tt_glyph(uint16_t glyph, const GlyphSink& sink,
                        const VariationInstance* var, int depth) const;
    int parse_tt_simple(const uint8_t* p, int num_contours, const GlyphSink& sink,
                         const VariationInstance* var, uint16_t glyph) const;
    int parse_tt_composite(const uint8_t* p, const GlyphSink& sink,
                            const VariationInstance* var, int depth) const;
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

    // gvar internal
    bool apply_gvar(uint16_t glyph, std::span<float> xs, std::span<float> ys,
                     std::span<const uint8_t> on_curve,
                     std::span<const uint16_t> end_pts,
                     const VariationInstance& var) const;

    // COLR v1 paint tree walker
    bool walk_paint_v1(uint32_t paint_off, int palette_index,
                        const PaintSink& sink,
                        uint32_t foreground_color,
                        const VariationInstance* var, int depth) const;
};

} // namespace kcg::otf
