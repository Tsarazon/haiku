// kcgfont.cpp — Font, FontCollection, and color glyph rendering
// C++20

#include "kcgprivate.hpp"
#include "kcgotf.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kcg {


// Font data ownership (binary memory release callback)


struct DataRelease {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;

    void operator()(const std::byte*) const noexcept {
        if (fn) fn(ctx);
    }
};


// Font::Impl


struct Font::Impl : RefCounted {
    otf::FontData otf;
    const std::byte* data        = nullptr;
    size_t           data_length = 0;
    DataRelease      release;
    float            size_       = 12.0f;

    std::string cached_family;
    std::string cached_style;

    // Variation state (nullopt = base font, no variations applied).
    // VariationInstance holds a pointer to `otf` above. When the Impl
    // is copied (with_size, with_variations), the instance must be
    // reconstructed so its pointer matches the *new* otf.
    std::optional<otf::VariationInstance> var;

    // Outline-path cache (raw font-unit paths, before scale/Y-flip).
    // Keyed by glyph ID. Per-Impl, so a different variation instance
    // automatically gets its own cache.
    struct GlyphPathEntry {
        Path     path;
        uint32_t generation = 0;
    };
    static constexpr int kMaxCacheSize = 4096;
    mutable std::shared_mutex                       cache_mutex;
    mutable std::unordered_map<uint16_t, GlyphPathEntry> path_cache;
    mutable uint32_t cache_generation = 0;

    GlyphPathEntry cached_glyph_path(uint16_t glyph) const;

    // Convenience: pointer-or-null for backend calls that accept
    // `const VariationInstance*`.
    const otf::VariationInstance* var_ptr() const {
        return var ? &*var : nullptr;
    }

    ~Impl();
};


// FontCollection::Impl


struct FontCollection::Impl : RefCounted {
    struct Entry {
        std::string                family;
        FontCollection::Weight     weight = FontCollection::Weight::Regular;
        FontCollection::Style      style  = FontCollection::Style::Normal;
        Font                       face;
    };

    std::vector<Entry> entries;
};


// Internal accessor and enum bridges


/// Reach the Impl behind a public Font handle. Font is layout-compatible
/// with a single pointer; this is the zero-cost reinterpret at the boundary.
static Font::Impl* font_impl(const Font& f) {
    static_assert(sizeof(Font) == sizeof(void*));
    return *reinterpret_cast<Font::Impl* const*>(&f);
}

static ColorFontFormat otf_format_to_kcg(otf::ColorFontFormat f) {
    switch (f) {
    case otf::ColorFontFormat::sbix:    return ColorFontFormat::Sbix;
    case otf::ColorFontFormat::cbdt:    return ColorFontFormat::CBDT;
    case otf::ColorFontFormat::colr_v1: return ColorFontFormat::COLR_v1;
    case otf::ColorFontFormat::colr_v0: return ColorFontFormat::COLR_v0;
    case otf::ColorFontFormat::svg:     return ColorFontFormat::SVG;
    case otf::ColorFontFormat::none:    return ColorFontFormat::None;
    }
    return ColorFontFormat::None;
}

static otf::ColorFormatPreference kcg_pref_to_otf(ColorFormatPreference p) {
    switch (p) {
    case ColorFormatPreference::BitmapFirst: return otf::ColorFormatPreference::bitmap_first;
    case ColorFormatPreference::VectorFirst: return otf::ColorFormatPreference::vector_first;
    case ColorFormatPreference::AutoBySize:  return otf::ColorFormatPreference::auto_by_size;
    }
    return otf::ColorFormatPreference::auto_by_size;
}


// UTF-8 decoder (internal helper)


/// Decode a single UTF-8 codepoint, advancing `p`. Returns 0xFFFD on error.
static uint32_t decode_utf8(const uint8_t*& p, const uint8_t* end) {
    if (p >= end) return 0xFFFD;
    uint8_t c = *p++;
    if (c < 0x80) return c;

    int extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else return 0xFFFD;

    for (int i = 0; i < extra; ++i) {
        if (p >= end || (*p & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (*p++ & 0x3F);
    }
    return cp;
}


// Font::Impl — glyph path cache


Font::Impl::GlyphPathEntry
Font::Impl::cached_glyph_path(uint16_t glyph) const {
    // Fast path: read lock
    {
        std::shared_lock lock(cache_mutex);
        auto it = path_cache.find(glyph);
        if (it != path_cache.end())
            return it->second;
    }

    // Slow path: parse outline and insert under write lock
    GlyphPathEntry entry;

    Path::Builder builder;

    // Layout compatibility: zero-cost reinterpret at the boundary
    static_assert(sizeof(otf::OutlinePoint) == sizeof(Point));
    static_assert(alignof(otf::OutlinePoint) == alignof(Point));
    static_assert(offsetof(otf::OutlinePoint, x) == offsetof(Point, x));
    static_assert(offsetof(otf::OutlinePoint, y) == offsetof(Point, y));

    struct PathSink {
        Path::Builder* b;
        static void callback(void* ctx, otf::OutlineCmd cmd,
                             const otf::OutlinePoint* pts, int /*npoints*/) {
            auto& bld = *static_cast<PathSink*>(ctx)->b;
            switch (cmd) {
            case otf::OutlineCmd::move_to:
                bld.move_to(pts[0].x, pts[0].y);
                break;
            case otf::OutlineCmd::line_to:
                bld.line_to(pts[0].x, pts[0].y);
                break;
            case otf::OutlineCmd::quad_to:
                bld.quad_to(pts[0].x, pts[0].y,
                            pts[1].x, pts[1].y);
                break;
            case otf::OutlineCmd::cubic_to:
                bld.cubic_to(pts[0].x, pts[0].y,
                             pts[1].x, pts[1].y,
                             pts[2].x, pts[2].y);
                break;
            case otf::OutlineCmd::close:
                bld.close();
                break;
            default:
                break;
            }
        }
    };

    PathSink sink{&builder};
    otf::GlyphSink gs{PathSink::callback, &sink};
    otf.glyph_outline(glyph, gs, var_ptr());
    entry.path = builder.build();

    std::unique_lock lock(cache_mutex);
    // Double-check: another thread may have inserted while we parsed
    auto it = path_cache.find(glyph);
    if (it != path_cache.end())
        return it->second;

    // Evict oldest entries when cache is full.
    if (static_cast<int>(path_cache.size()) >= kMaxCacheSize) {
        uint32_t min_gen = cache_generation;
        for (auto& [k, v] : path_cache)
            if (v.generation < min_gen) min_gen = v.generation;
        uint32_t threshold = min_gen + (cache_generation - min_gen) / 2;
        for (auto ci = path_cache.begin(); ci != path_cache.end(); ) {
            if (ci->second.generation <= threshold)
                ci = path_cache.erase(ci);
            else
                ++ci;
        }
    }

    entry.generation = ++cache_generation;
    auto [inserted, _] = path_cache.emplace(glyph, std::move(entry));
    return inserted->second;
}

Font::Impl::~Impl() {
    release(data);
}


// Font: COW ref-counting


Font::Font() : m_impl(nullptr) {}
Font::~Font()                          { impl_release(m_impl); }
Font::Font(const Font& other)          : m_impl(other.m_impl) { impl_retain(m_impl); }
Font::Font(Font&& other) noexcept      : m_impl(other.m_impl) { other.m_impl = nullptr; }
Font& Font::operator=(const Font& other)   { impl_assign(m_impl, other.m_impl); return *this; }
Font& Font::operator=(Font&& other) noexcept { impl_move(m_impl, other.m_impl); return *this; }

Font::operator bool() const {
    return m_impl != nullptr;
}


// Font: internal factory helper


/// Set m_impl on a default-constructed Font.
/// Font is layout-compatible with a single pointer.
static void set_impl(Font& font, Font::Impl* impl) {
    static_assert(sizeof(Font) == sizeof(void*));
    std::memcpy(&font, &impl, sizeof(impl));
}

/// Rebuild a VariationInstance bound to a new FontData. Used after copying
/// the otf FontData in with_size / with_variations — the source instance
/// holds a raw pointer to the *old* FontData, which would dangle.
static otf::VariationInstance rebind_variations(const otf::VariationInstance& src,
                                                 const otf::FontData& new_fd) {
    otf::VariationInstance vi(new_fd);
    auto coords = src.normalized();
    for (size_t i = 0; i < coords.size(); ++i)
        vi.set_normalized(static_cast<int>(i), coords[i]);
    return vi;
}

static Font make_font(const void* raw_data, size_t length, float size,
                      int face_index, DataRelease release) {
    auto u8 = std::span<const uint8_t>(
        static_cast<const uint8_t*>(raw_data), length);

    auto font_data = otf::FontData::open(u8, face_index);
    if (!font_data) {
        if (release.fn) release.fn(release.ctx);
        return {};
    }

    auto* impl = new Font::Impl;
    impl->otf = std::move(*font_data);
    impl->data = static_cast<const std::byte*>(raw_data);
    impl->data_length = length;
    impl->release = release;
    impl->size_ = size;

    if (auto name = impl->otf.family_name())
        impl->cached_family = std::move(*name);
    if (auto name = impl->otf.style_name())
        impl->cached_style = std::move(*name);

    Font f;
    set_impl(f, impl);
    return f;
}


// Font: factories


Font Font::load(const char* filename, float size, int face_index) {
    std::FILE* fp = std::fopen(filename, "rb");
    if (!fp) return {};

    std::fseek(fp, 0, SEEK_END);
    long length = std::ftell(fp);
    if (length <= 0) {
        std::fclose(fp);
        return {};
    }

    auto* buf = new std::byte[static_cast<size_t>(length)];
    std::fseek(fp, 0, SEEK_SET);
    size_t nread = std::fread(buf, 1, static_cast<size_t>(length), fp);
    std::fclose(fp);

    if (nread != static_cast<size_t>(length)) {
        delete[] buf;
        return {};
    }

    DataRelease rel;
    rel.fn = [](void* ctx) { delete[] static_cast<std::byte*>(ctx); };
    rel.ctx = buf;
    return make_font(buf, static_cast<size_t>(length), size, face_index, rel);
}

Font Font::load(std::span<const std::byte> data, float size, int face_index) {
    auto* copy = new std::byte[data.size()];
    std::memcpy(copy, data.data(), data.size());

    DataRelease rel;
    rel.fn = [](void* ctx) { delete[] static_cast<std::byte*>(ctx); };
    rel.ctx = copy;
    return make_font(copy, data.size(), size, face_index, rel);
}

Font Font::load(const void* data, size_t length, float size,
                int face_index,
                void (*release_fn)(void*), void* context) {
    DataRelease rel;
    rel.fn = release_fn;
    rel.ctx = context;
    return make_font(data, length, size, face_index, rel);
}


// Font: with_size / with_variations


Font Font::with_size(float new_size) const {
    if (!m_impl) return {};

    auto* impl = new Impl;
    impl->otf = m_impl->otf;            // FontData is a view — cheap copy
    impl->data = m_impl->data;
    impl->data_length = m_impl->data_length;
    impl->size_ = new_size;
    impl->cached_family = m_impl->cached_family;
    impl->cached_style = m_impl->cached_style;

    // Preserve variation state across resizing. The old VariationInstance
    // points to the old otf; rebind to the new copy.
    if (m_impl->var)
        impl->var = rebind_variations(*m_impl->var, impl->otf);

    // The new Impl does not own the binary data. Keep the original alive
    // by capturing a ref to it as the release callback.
    Impl* original = m_impl;
    original->ref();
    impl->release.fn = [](void* ctx) {
        auto* orig = static_cast<Impl*>(ctx);
        if (orig->deref()) delete orig;
    };
    impl->release.ctx = original;

    Font f;
    set_impl(f, impl);
    return f;
}

Font Font::with_variations(std::span<const FontVariation> axes) const {
    if (!m_impl || !m_impl->otf.has_fvar())
        return {};

    auto* impl = new Impl;
    impl->otf = m_impl->otf;
    impl->data = m_impl->data;
    impl->data_length = m_impl->data_length;
    impl->size_ = m_impl->size_;
    impl->cached_family = m_impl->cached_family;
    impl->cached_style = m_impl->cached_style;

    // Build variation instance starting from current state (if any) and
    // overlay the new axis values.
    otf::VariationInstance vi(impl->otf);
    if (m_impl->var) {
        auto src = m_impl->var->normalized();
        for (size_t i = 0; i < src.size(); ++i)
            vi.set_normalized(static_cast<int>(i), src[i]);
    }
    for (auto& a : axes)
        vi.set_axis(a.tag, a.value);
    impl->var = std::move(vi);

    // Keep the underlying binary alive via the original Impl, just like
    // with_size does.
    Impl* original = m_impl;
    original->ref();
    impl->release.fn = [](void* ctx) {
        auto* orig = static_cast<Impl*>(ctx);
        if (orig->deref()) delete orig;
    };
    impl->release.ctx = original;

    Font f;
    set_impl(f, impl);
    return f;
}


// Font: variable font queries


bool Font::is_variable() const {
    return m_impl && m_impl->otf.has_fvar();
}

int Font::variation_axis_count() const {
    if (!m_impl) return 0;
    return m_impl->otf.fvar_axis_count();
}

Font::VariationAxis Font::variation_axis(int index) const {
    if (!m_impl) return {};
    auto ax = m_impl->otf.fvar_axis(index);
    return {ax.tag, ax.min_value, ax.default_value, ax.max_value};
}


// Font: color font queries


ColorFontFormat Font::color_format(GlyphID glyph, ColorFormatPreference pref) const {
    if (!m_impl) return ColorFontFormat::None;
    uint16_t target_ppem = static_cast<uint16_t>(
        std::clamp(m_impl->size_, 1.0f, 65535.0f));
    return otf_format_to_kcg(
        m_impl->otf.color_format(glyph, kcg_pref_to_otf(pref), target_ppem));
}

bool Font::has_color_glyphs() const {
    if (!m_impl) return false;
    auto& o = m_impl->otf;
    return o.has_colr_v0() || o.has_colr_v1()
        || o.has_sbix() || o.has_cbdt() || o.has_svg();
}

int Font::palette_count() const {
    if (!m_impl) return 0;
    return m_impl->otf.cpal_palette_count();
}

int Font::palette_size() const {
    if (!m_impl) return 0;
    return m_impl->otf.cpal_palette_size();
}

Color Font::palette_color(int palette_index, int color_index) const {
    if (!m_impl) return Color::black();
    uint32_t argb = m_impl->otf.cpal_color(palette_index, color_index);
    return Color::from_argb32(argb);
}

std::span<const std::byte> Font::glyph_svg_data(GlyphID glyph) const {
    if (!m_impl) return {};
    auto bytes = m_impl->otf.glyph_svg(glyph);
    return {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}


// Font: size and metrics


float Font::size() const {
    return m_impl ? m_impl->size_ : 0.0f;
}

FontMetrics Font::metrics() const {
    if (!m_impl) return {};
    auto& o = m_impl->otf;
    float scale = o.scale_for_em(m_impl->size_);

    // Prefer OS/2 typo metrics when present; they are more reliable on
    // mixed-script and symbol fonts than hhea ascent/descent.
    auto vm = o.vmetrics_os2().value_or(o.vmetrics());
    auto bb = o.bounding_box();

    FontMetrics m;
    m.ascent = vm.ascent * scale;
    m.descent = vm.descent * scale;
    m.leading = vm.line_gap * scale;
    m.x_height = o.x_height() * scale;
    m.cap_height = o.cap_height() * scale;
    m.bounding_box = Rect{
        bb.x0 * scale,
        -bb.y1 * scale,
        (bb.x1 - bb.x0) * scale,
        (bb.y1 - bb.y0) * scale
    };
    m.units_per_em = static_cast<float>(o.units_per_em());
    return m;
}

GlyphMetrics Font::glyph_metrics(GlyphID glyph) const {
    if (!m_impl) return {};
    auto& o = m_impl->otf;
    float scale = o.scale_for_em(m_impl->size_);

    // hmetrics() applies HVAR delta automatically when var is set.
    auto hm = o.hmetrics(glyph, m_impl->var_ptr());
    otf::BBox box;
    bool has_box = o.glyph_box(glyph, box);

    GlyphMetrics gm;
    gm.advance = hm.advance_width * scale;
    if (has_box) {
        gm.bounds = Rect{
            box.x0 * scale,
            -box.y1 * scale,
            (box.x1 - box.x0) * scale,
            (box.y1 - box.y0) * scale
        };
    }
    return gm;
}


// Font: name table


std::optional<std::string> Font::family_name() const {
    if (!m_impl) return std::nullopt;
    if (!m_impl->cached_family.empty())
        return m_impl->cached_family;
    return m_impl->otf.family_name();
}

std::optional<std::string> Font::style_name() const {
    if (!m_impl) return std::nullopt;
    if (!m_impl->cached_style.empty())
        return m_impl->cached_style;
    return m_impl->otf.style_name();
}

int Font::weight_class() const {
    if (!m_impl) return 0;
    return static_cast<int>(m_impl->otf.weight_class());
}


// Font: character mapping


GlyphID Font::glyph_for_codepoint(Codepoint cp) const {
    if (!m_impl) return 0;
    return m_impl->otf.glyph_index(cp);
}

GlyphID Font::glyph_for_codepoint(Codepoint cp, Codepoint variation_selector) const {
    if (!m_impl) return 0;
    GlyphID g = m_impl->otf.glyph_index_vs(cp, variation_selector);
    if (g != 0) return g;
    // Fall back to the plain cmap mapping when the VS pair isn't covered.
    return m_impl->otf.glyph_index(cp);
}

bool Font::has_glyph(Codepoint cp) const {
    if (!m_impl) return false;
    return m_impl->otf.glyph_index(cp) != 0;
}


// Font: glyph path


Path Font::glyph_path(GlyphID glyph) const {
    if (!m_impl) return {};
    float scale = m_impl->otf.scale_for_em(m_impl->size_);

    auto entry = m_impl->cached_glyph_path(glyph);
    if (!entry.path)
        return {};

    // Scale and flip Y: font units → pixel units with top-left origin
    return entry.path.transformed(AffineTransform{scale, 0, 0, -scale, 0, 0});
}

Path Font::glyph_path(GlyphID glyph, float x, float y) const {
    if (!m_impl) return {};
    float scale = m_impl->otf.scale_for_em(m_impl->size_);

    auto entry = m_impl->cached_glyph_path(glyph);
    if (!entry.path)
        return {};

    return entry.path.transformed(AffineTransform{scale, 0, 0, -scale, x, y});
}


// Font: glyph run path


Path Font::glyph_run_path(std::span<const GlyphID> glyphs,
                          std::span<const Point> positions) const {
    if (glyphs.empty() || glyphs.size() != positions.size())
        return {};
    if (!m_impl || !m_impl->otf.valid())
        return {};

    float scale = m_impl->otf.scale_for_em(m_impl->size_);
    Path::Builder builder;

    for (size_t i = 0; i < glyphs.size(); i++) {
        auto entry = m_impl->cached_glyph_path(glyphs[i]);
        if (!entry.path) continue;

        AffineTransform t = AffineTransform::scaled(scale, -scale);
        t.tx = positions[i].x;
        t.ty = positions[i].y;
        builder.add_path(entry.path, &t);
    }

    return builder.build();
}

Path Font::glyph_run_path(std::span<const GlyphPosition> run) const {
    if (run.empty()) return {};
    if (!m_impl || !m_impl->otf.valid())
        return {};

    float scale = m_impl->otf.scale_for_em(m_impl->size_);
    Path::Builder builder;

    for (auto& gp : run) {
        auto entry = m_impl->cached_glyph_path(gp.glyph);
        if (!entry.path) continue;

        AffineTransform t = AffineTransform::scaled(scale, -scale);
        t.tx = gp.position.x + gp.offset.x;
        t.ty = gp.position.y + gp.offset.y;
        builder.add_path(entry.path, &t);
    }

    return builder.build();
}


// Font: text measurement


float Font::measure(std::string_view utf8) const {
    if (!m_impl) return 0.0f;
    auto& o = m_impl->otf;
    float scale = o.scale_for_em(m_impl->size_);
    float total = 0.0f;
    uint16_t prev = 0;

    auto* p = reinterpret_cast<const uint8_t*>(utf8.data());
    auto* end = p + utf8.size();
    while (p < end) {
        uint32_t cp = decode_utf8(p, end);
        uint16_t glyph = o.glyph_index(cp);
        if (prev)
            total += o.kern_advance(prev, glyph) * scale;
        // hmetrics() applies HVAR delta if var is set.
        total += o.hmetrics(glyph, m_impl->var_ptr()).advance_width * scale;
        prev = glyph;
    }
    return total;
}

Rect Font::text_bounds(std::string_view utf8, Point origin) const {
    if (!m_impl) return {};
    auto& o = m_impl->otf;
    float scale = o.scale_for_em(m_impl->size_);
    bool has_bounds = false;
    float total_advance = 0.0f;
    uint16_t prev = 0;
    Rect result;

    auto* p = reinterpret_cast<const uint8_t*>(utf8.data());
    auto* end = p + utf8.size();
    while (p < end) {
        uint32_t cp = decode_utf8(p, end);
        uint16_t glyph = o.glyph_index(cp);

        if (prev)
            total_advance += o.kern_advance(prev, glyph) * scale;

        // Skip empty glyphs (space, control chars) for bounds purposes —
        // they contribute advance but no ink.
        if (!o.is_glyph_empty(glyph)) {
            otf::BBox box;
            if (o.glyph_box(glyph, box)) {
                Rect gb{
                    origin.x + total_advance + box.x0 * scale,
                    origin.y - box.y1 * scale,
                    (box.x1 - box.x0) * scale,
                    (box.y1 - box.y0) * scale
                };
                if (!has_bounds) {
                    result = gb;
                    has_bounds = true;
                } else {
                    result = result.united(gb);
                }
            }
        }

        total_advance += o.hmetrics(glyph, m_impl->var_ptr()).advance_width * scale;
        prev = glyph;
    }
    return result;
}


// Font: kerning


float Font::kerning(GlyphID left, GlyphID right) const {
    if (!m_impl) return 0.0f;
    float scale = m_impl->otf.scale_for_em(m_impl->size_);
    return m_impl->otf.kern_advance(left, right) * scale;
}


// Font: shaper helpers (ligatures, bulk kerning)


bool Font::has_ligatures() const {
    if (!m_impl) return false;
    return m_impl->otf.has_liga();
}

LigatureMatch Font::lookup_ligature(std::span<const GlyphID> glyphs) const {
    if (!m_impl || glyphs.empty()) return {};
    auto m = m_impl->otf.liga_lookup(glyphs);
    return LigatureMatch{m.glyph, m.consumed};
}

int Font::kern_pair_count() const {
    if (!m_impl) return 0;
    return m_impl->otf.kern_pair_count();
}

int Font::kern_pairs(std::span<KerningPair> out) const {
    if (!m_impl || out.empty()) return 0;

    // Backend writes KernPair (font units); we expose KerningPair (pixels).
    // Stage in a small heap buffer to avoid stack pressure on big arrays.
    std::vector<otf::KernPair> staging(out.size());
    int n = m_impl->otf.kern_pairs(staging);
    float scale = m_impl->otf.scale_for_em(m_impl->size_);

    for (int i = 0; i < n; ++i) {
        out[i].left  = staging[i].g1;
        out[i].right = staging[i].g2;
        out[i].value = staging[i].value * scale;
    }
    return n;
}


// Font: raw table access


const void* Font::table_data(uint32_t tag) const {
    if (!m_impl) return nullptr;
    auto loc = m_impl->otf.locate_table(tag);
    if (loc.offset == 0 && loc.length == 0) return nullptr;
    auto d = m_impl->otf.data();
    if (static_cast<size_t>(loc.offset) + loc.length > d.size()) return nullptr;
    return d.data() + loc.offset;
}

size_t Font::table_size(uint32_t tag) const {
    if (!m_impl) return 0;
    return m_impl->otf.locate_table(tag).length;
}


// FontCollection: COW ref-counting


FontCollection::FontCollection() : m_impl(new Impl) {}
FontCollection::~FontCollection()                                    { impl_release(m_impl); }
FontCollection::FontCollection(const FontCollection& other)          : m_impl(other.m_impl) { impl_retain(m_impl); }
FontCollection::FontCollection(FontCollection&& other) noexcept      : m_impl(other.m_impl) { other.m_impl = nullptr; }
FontCollection& FontCollection::operator=(const FontCollection& other)   { impl_assign(m_impl, other.m_impl); return *this; }
FontCollection& FontCollection::operator=(FontCollection&& other) noexcept { impl_move(m_impl, other.m_impl); return *this; }

FontCollection::operator bool() const {
    return m_impl != nullptr;
}

// -- COW detach --

static FontCollection::Impl* detach(FontCollection::Impl*& impl) {
    if (impl->count() > 1) {
        auto* copy = new FontCollection::Impl;
        copy->entries = impl->entries;
        if (impl->deref())
            delete impl;
        impl = copy;
    }
    return impl;
}


// FontCollection: mutators


void FontCollection::reset() {
    auto* d = detach(m_impl);
    d->entries.clear();
}

void FontCollection::add(std::string_view family, Weight weight, Style style,
                         const Font& font) {
    auto* d = detach(m_impl);
    d->entries.push_back({std::string(family), weight, style, font});
}

bool FontCollection::add_file(std::string_view family, Weight weight, Style style,
                              const char* filename, float default_size, int face_index) {
    Font face = Font::load(filename, default_size, face_index);
    if (!face)
        return false;
    add(family, weight, style, face);
    return true;
}


// FontCollection: lookup


Font FontCollection::get(std::string_view family, float size,
                         Weight weight, Style style) const {
    if (!m_impl)
        return {};

    const Impl::Entry* best = nullptr;
    int best_score = -1;

    for (auto& entry : m_impl->entries) {
        if (entry.family != family)
            continue;
        int score = (weight == entry.weight ? 2 : 0)
                  + (style == entry.style ? 1 : 0);
        if (score > best_score) {
            best_score = score;
            best = &entry;
        }
    }

    if (!best) return {};

    // Return a font at the requested size (cheap — shares parsed data)
    const Font& found = best->face;
    if (found.size() == size)
        return found;
    return found.with_size(size);
}


// Color glyph rendering (COLR v0/v1, sbix, CBDT)

namespace {


// CompositeMode mapping (OTF spec → kcg CompositeOp + BlendMode)


std::pair<CompositeOp, BlendMode> composite_mode_to_kcg(otf::CompositeMode m) {
    using CM = otf::CompositeMode;
    using CO = CompositeOp;
    using BM = BlendMode;
    switch (m) {
    // Porter-Duff: pure compositing, blend stays Normal.
    case CM::clear:     return {CO::Clear,           BM::Normal};
    case CM::src:       return {CO::Copy,            BM::Normal};
    case CM::dest:      return {CO::DestinationIn,   BM::Normal}; // closest "keep dest"
    case CM::src_over:  return {CO::SourceOver,      BM::Normal};
    case CM::dest_over: return {CO::DestinationOver, BM::Normal};
    case CM::src_in:    return {CO::SourceIn,        BM::Normal};
    case CM::dest_in:   return {CO::DestinationIn,   BM::Normal};
    case CM::src_out:   return {CO::SourceOut,       BM::Normal};
    case CM::dest_out:  return {CO::DestinationOut,  BM::Normal};
    case CM::src_atop:  return {CO::SourceAtop,      BM::Normal};
    case CM::dest_atop: return {CO::DestinationAtop, BM::Normal};
    case CM::xor_:      return {CO::Xor,             BM::Normal};
    // Separable blend modes: composite stays SourceOver.
    case CM::plus:        return {CO::SourceOver, BM::Add};
    case CM::screen:      return {CO::SourceOver, BM::Screen};
    case CM::overlay:     return {CO::SourceOver, BM::Overlay};
    case CM::darken:      return {CO::SourceOver, BM::Darken};
    case CM::lighten:     return {CO::SourceOver, BM::Lighten};
    case CM::color_dodge: return {CO::SourceOver, BM::ColorDodge};
    case CM::color_burn:  return {CO::SourceOver, BM::ColorBurn};
    case CM::hard_light:  return {CO::SourceOver, BM::HardLight};
    case CM::soft_light:  return {CO::SourceOver, BM::SoftLight};
    case CM::difference:  return {CO::SourceOver, BM::Difference};
    case CM::exclusion:   return {CO::SourceOver, BM::Exclusion};
    case CM::multiply:    return {CO::SourceOver, BM::Multiply};
    // Non-separable HSL.
    case CM::hsl_hue:        return {CO::SourceOver, BM::Hue};
    case CM::hsl_saturation: return {CO::SourceOver, BM::Saturation};
    case CM::hsl_color:      return {CO::SourceOver, BM::Color};
    case CM::hsl_luminosity: return {CO::SourceOver, BM::Luminosity};
    }
    return {CO::SourceOver, BM::Normal};
}

GradientSpread extend_to_spread(otf::GradientExtend e) {
    switch (e) {
    case otf::GradientExtend::pad:     return GradientSpread::Pad;
    case otf::GradientExtend::repeat:  return GradientSpread::Repeat;
    case otf::GradientExtend::reflect: return GradientSpread::Reflect;
    }
    return GradientSpread::Pad;
}


// COLR v1 linear gradient: three-point form -> two endpoints
//
// OTF gives (p0, p1, p2) where p2 is a rotation control. The actual
// gradient axis is perpendicular to (p2 - p0); we project p1 onto that
// perpendicular line to get the effective endpoint. If p2 is degenerate
// (= p0) or colinear with p0p1, fall back to a simple p0→p1 gradient.


std::pair<Point, Point> linear_gradient_endpoints(Point p0, Point p1, Point p2) {
    Point dp2{p2.x - p0.x, p2.y - p0.y};
    float len_sq_dp2 = dp2.x * dp2.x + dp2.y * dp2.y;
    if (len_sq_dp2 < 1e-6f)
        return {p0, p1};

    Point dp1{p1.x - p0.x, p1.y - p0.y};
    // Colinearity check via cross product magnitude.
    float cross = dp1.x * dp2.y - dp1.y * dp2.x;
    float len_dp1_sq = dp1.x * dp1.x + dp1.y * dp1.y;
    if (cross * cross < 1e-8f * len_sq_dp2 * len_dp1_sq)
        return {p0, p1};

    // Project dp1 onto perpendicular of dp2.
    // perp(dp2) = (-dp2.y, dp2.x); dp1 · perp(dp2) = -dp1.x*dp2.y + dp1.y*dp2.x.
    float dot_perp = -dp1.x * dp2.y + dp1.y * dp2.x;
    float t = dot_perp / len_sq_dp2;
    Point new_p1{p0.x + t * -dp2.y, p0.y + t * dp2.x};
    return {p0, new_p1};
}


// COLR v1 walker context


struct WalkerCtx {
    Context&        gc;
    Context::Impl&  impl;
    Font::Impl&     font_impl;
    uint32_t        foreground_argb;
    const otf::VariationInstance* var;
};

void copy_stops(int n, const otf::ColorStop* in,
                std::vector<Gradient::Stop>& out) {
    out.clear();
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        out.push_back({in[i].offset, Color::from_argb32(in[i].argb)});
}


// PaintSink C-style callbacks


extern "C" void cb_set_solid(void* ctx, uint32_t argb) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    w->gc.set_fill_color(Color::from_argb32(argb));
    w->gc.clear_fill_gradient();
}

extern "C" void cb_set_linear_gradient(void* ctx,
                                       float x0, float y0,
                                       float x1, float y1,
                                       float x2, float y2,
                                       otf::GradientExtend extend,
                                       int num_stops,
                                       const otf::ColorStop* stops) {
    auto* w = static_cast<WalkerCtx*>(ctx);

    std::vector<Gradient::Stop> kcg_stops;
    copy_stops(num_stops, stops, kcg_stops);
    auto grad = Gradient::create(ColorSpace::srgb(), kcg_stops,
                                 extend_to_spread(extend));

    auto [p_start, p_end] = linear_gradient_endpoints({x0, y0}, {x1, y1}, {x2, y2});
    w->gc.set_fill_linear_gradient(grad, p_start, p_end);
}

extern "C" void cb_set_radial_gradient(void* ctx,
                                       float cx0, float cy0, float r0,
                                       float cx1, float cy1, float r1,
                                       otf::GradientExtend extend,
                                       int num_stops,
                                       const otf::ColorStop* stops) {
    auto* w = static_cast<WalkerCtx*>(ctx);

    std::vector<Gradient::Stop> kcg_stops;
    copy_stops(num_stops, stops, kcg_stops);
    auto grad = Gradient::create(ColorSpace::srgb(), kcg_stops,
                                 extend_to_spread(extend));

    w->gc.set_fill_radial_gradient(grad,
                                   {cx0, cy0}, r0,
                                   {cx1, cy1}, r1);
}

extern "C" void cb_set_sweep_gradient(void* ctx,
                                      float cx, float cy,
                                      float start_angle_deg, float end_angle_deg,
                                      otf::GradientExtend extend,
                                      int num_stops,
                                      const otf::ColorStop* stops) {
    auto* w = static_cast<WalkerCtx*>(ctx);

    // Map OTF sweep [start_deg, end_deg] onto kcg conic (single start,
    // full 360°). Stop offsets get scaled by range/360 so that the
    // gradient occupies only the active angular range; outside that
    // range, the spread mode takes over.
    float range_deg = end_angle_deg - start_angle_deg;
    // Degenerate or pathological: paint first stop as solid.
    if (std::abs(range_deg) < 1e-3f) {
        uint32_t argb = (num_stops > 0) ? stops[0].argb : w->foreground_argb;
        w->gc.set_fill_color(Color::from_argb32(argb));
        w->gc.clear_fill_gradient();
        return;
    }
    bool reversed = range_deg < 0.0f;
    if (reversed) range_deg = -range_deg;

    float scale = range_deg / 360.0f;

    std::vector<Gradient::Stop> kcg_stops;
    kcg_stops.reserve(static_cast<size_t>(num_stops));
    for (int i = 0; i < num_stops; ++i) {
        float t = stops[i].offset * scale;
        if (reversed) t = scale - t;
        kcg_stops.push_back({
            std::clamp(t, 0.0f, 1.0f),
            Color::from_argb32(stops[i].argb)
        });
    }
    std::sort(kcg_stops.begin(), kcg_stops.end(),
              [](const Gradient::Stop& a, const Gradient::Stop& b) {
                  return a.offset < b.offset;
              });

    auto grad = Gradient::create(ColorSpace::srgb(), kcg_stops,
                                 extend_to_spread(extend));
    float kcg_start_rad = deg2rad(reversed ? end_angle_deg : start_angle_deg);
    w->gc.set_fill_conic_gradient(grad, {cx, cy}, kcg_start_rad);
}

extern "C" void cb_fill_glyph(void* ctx, uint16_t glyph) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    // The cached path is in raw font units, Y-up. The CTM set up at the
    // top of render_colr_v1 maps font→pixels with Y flip.
    auto entry = w->font_impl.cached_glyph_path(glyph);
    if (!entry.path) return;
    w->gc.fill_path(entry.path, FillRule::NonZero);
}

extern "C" void cb_push_transform(void* ctx, const float m[6]) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    // OTF column-major [a b c d tx ty] matches kcg AffineTransform exactly.
    AffineTransform t{m[0], m[1], m[2], m[3], m[4], m[5]};
    w->gc.save_state();
    w->gc.concat_ctm(t);
}

extern "C" void cb_pop_transform(void* ctx) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    w->gc.restore_state();
}

extern "C" void cb_push_layer(void* ctx) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    // Start neutral; the actual composite mode is supplied at pop time.
    w->gc.begin_transparency_layer(1.0f, BlendMode::Normal);
}

extern "C" void cb_pop_layer(void* ctx, otf::CompositeMode mode) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    // end_transparency_layer composites using the LayerInfo's stored
    // op + blend (captured at begin time). We mutate them here so the
    // pop-time mode is honored.
    auto [op, blend] = composite_mode_to_kcg(mode);
    if (w->impl.state().layer) {
        w->impl.state().layer->op = op;
        w->impl.state().layer->blend_mode = blend;
    }
    w->gc.end_transparency_layer();
}

extern "C" void cb_push_clip_glyph(void* ctx, uint16_t glyph) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    auto entry = w->font_impl.cached_glyph_path(glyph);
    w->gc.save_state();
    if (entry.path)
        w->gc.clip_to_path(entry.path, FillRule::NonZero);
}

extern "C" void cb_pop_clip(void* ctx) {
    auto* w = static_cast<WalkerCtx*>(ctx);
    w->gc.restore_state();
}


// CTM helpers: push the font-unit-to-pixel transform at the glyph origin


/// Push CTM that maps glyph outlines in font units (Y-up) to pixel
/// coordinates at the given baseline (Y-down). Pairs with restore_state.
void push_font_to_pixel_ctm(Context& gc, float scale, Point baseline) {
    gc.save_state();
    // Equivalent to: ctm * translate(baseline) * scale(s, -s)
    gc.translate_ctm(baseline.x, baseline.y);
    gc.scale_ctm(scale, -scale);
}


// COLR v0: layered colored outlines


bool render_colr_v0(Context& gc, Context::Impl& impl, Font::Impl& font_impl,
                    uint16_t base_glyph, int palette_index,
                    uint32_t foreground_argb, Point baseline) {
    auto& o = font_impl.otf;
    int n = o.colr_v0_layer_count(base_glyph);
    if (n <= 0) return false;

    std::vector<otf::ColrLayer> layers(static_cast<size_t>(n));
    int got = o.colr_v0_layers(base_glyph, layers);
    if (got <= 0) return false;

    float scale = o.scale_for_em(font_impl.size_);

    push_font_to_pixel_ctm(gc, scale, baseline);
    // Use a transparency layer so per-layer composites stack independently
    // of any clip-related artifacts on the parent surface.
    gc.begin_transparency_layer(1.0f, BlendMode::Normal);

    for (int i = 0; i < got; ++i) {
        const auto& layer = layers[i];
        uint32_t argb;
        if (layer.palette_index == 0xFFFF) {
            argb = foreground_argb;
        } else {
            argb = o.cpal_color(palette_index, layer.palette_index);
        }
        gc.set_fill_color(Color::from_argb32(argb));
        gc.clear_fill_gradient();

        auto entry = font_impl.cached_glyph_path(layer.glyph_id);
        if (!entry.path) continue;
        gc.fill_path(entry.path, FillRule::NonZero);
    }

    gc.end_transparency_layer();
    gc.restore_state();
    return true;
}


// COLR v1: paint tree walker


bool render_colr_v1(Context& gc, Context::Impl& impl, Font::Impl& font_impl,
                    uint16_t base_glyph, int palette_index,
                    uint32_t foreground_argb, Point baseline) {
    auto& o = font_impl.otf;
    float scale = o.scale_for_em(font_impl.size_);

    push_font_to_pixel_ctm(gc, scale, baseline);

    WalkerCtx wc{gc, impl, font_impl, foreground_argb, font_impl.var_ptr()};

    otf::PaintSink sink;
    sink.ctx                  = &wc;
    sink.set_solid            = &cb_set_solid;
    sink.set_linear_gradient  = &cb_set_linear_gradient;
    sink.set_radial_gradient  = &cb_set_radial_gradient;
    sink.set_sweep_gradient   = &cb_set_sweep_gradient;
    sink.fill_glyph           = &cb_fill_glyph;
    sink.push_transform       = &cb_push_transform;
    sink.pop_transform        = &cb_pop_transform;
    sink.push_layer           = &cb_push_layer;
    sink.pop_layer            = &cb_pop_layer;
    sink.push_clip_glyph      = &cb_push_clip_glyph;
    sink.pop_clip             = &cb_pop_clip;

    bool ok = o.colr_v1_paint(base_glyph, palette_index, sink,
                              foreground_argb, wc.var);

    gc.restore_state();
    return ok;
}


// sbix: per-strike PNG/JPG bitmap glyphs (Apple Color Emoji)


bool render_sbix(Context& gc, Font::Impl& font_impl,
                 uint16_t glyph, Point baseline) {
    auto& o = font_impl.otf;
    if (o.units_per_em() <= 0) return false;

    uint16_t target_ppem = static_cast<uint16_t>(
        std::clamp(font_impl.size_, 1.0f, 65535.0f));
    int strike = o.sbix_best_strike(target_ppem);
    if (strike < 0) return false;

    auto sg = o.sbix_glyph(strike, glyph);
    if (sg.data.empty() || sg.format == 0)
        return false;

    // TIFF (sbix supports tiff) is not handled by stbi.
    // Format codes: 'png ' = 0x706E6720, 'jpg ' = 0x6A706720.
    constexpr uint32_t PNG_TAG = (uint32_t('p') << 24) | (uint32_t('n') << 16)
                               | (uint32_t('g') <<  8) |  uint32_t(' ');
    constexpr uint32_t JPG_TAG = (uint32_t('j') << 24) | (uint32_t('p') << 16)
                               | (uint32_t('g') <<  8) |  uint32_t(' ');
    if (sg.format != PNG_TAG && sg.format != JPG_TAG)
        return false;

    auto bytes = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(sg.data.data()),
        sg.data.size()
    };
    Image img = Image::load(bytes);
    if (!img) return false;

    float em_to_px     = font_impl.size_ / static_cast<float>(o.units_per_em());
    float strike_to_px = font_impl.size_ / static_cast<float>(sg.ppem ? sg.ppem : target_ppem);

    float bw = static_cast<float>(img.width())  * strike_to_px;
    float bh = static_cast<float>(img.height()) * strike_to_px;

    // sbix originOffsetX/Y are in font design units, measured from the
    // graphic's bottom-left to the glyph origin. In screen Y-down:
    //   top-left.x = baseline.x - origin_x * em_to_px
    //   top-left.y = baseline.y + origin_y * em_to_px - bh
    Rect dest{
        baseline.x - sg.origin_x * em_to_px,
        baseline.y + sg.origin_y * em_to_px - bh,
        bw, bh
    };

    gc.draw_image(img, dest);
    return true;
}


// CBDT/CBLC: per-strike bitmap glyphs (Google Noto Color Emoji classic)


bool render_cbdt(Context& gc, Font::Impl& font_impl,
                 uint16_t glyph, Point baseline) {
    auto& o = font_impl.otf;
    if (o.units_per_em() <= 0) return false;

    uint16_t target_ppem = static_cast<uint16_t>(
        std::clamp(font_impl.size_, 1.0f, 65535.0f));
    int strike = o.cblc_best_strike(target_ppem);
    if (strike < 0) return false;

    auto cg = o.cbdt_glyph(strike, glyph);
    if (cg.data.empty()) return false;

    // CBDT formats 17/18/19 carry PNG image data. Other formats
    // (binary masks 1-9) are not handled here.
    if (cg.format != 17 && cg.format != 18 && cg.format != 19)
        return false;

    auto bytes = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(cg.data.data()),
        cg.data.size()
    };
    Image img = Image::load(bytes);
    if (!img) return false;

    // CBDT bearings are in pixels at the strike's PPEM (unlike sbix
    // which uses font units). Use ppem_y for vertical scaling, ppem_x
    // for horizontal — typically equal but spec allows asymmetric strikes.
    uint16_t ppx = cg.ppem_x ? cg.ppem_x : target_ppem;
    uint16_t ppy = cg.ppem_y ? cg.ppem_y : target_ppem;
    float sx = font_impl.size_ / static_cast<float>(ppx);
    float sy = font_impl.size_ / static_cast<float>(ppy);

    float bw = static_cast<float>(img.width())  * sx;
    float bh = static_cast<float>(img.height()) * sy;

    // bearing_x: offset to left of bitmap (LSB-equivalent, pixels at strike).
    // bearing_y: from baseline to top of bitmap (positive = above baseline,
    //   pixels at strike). Y-down: bitmap top = baseline - bearing_y_in_px.
    Rect dest{
        baseline.x + cg.bearing_x * sx,
        baseline.y - cg.bearing_y * sy,
        bw, bh
    };

    gc.draw_image(img, dest);
    return true;
}

} // anonymous namespace


// Dispatcher: try_render_color_glyph (declared in kcgprivate.hpp)


namespace detail {

bool try_render_color_glyph(Context::Impl& impl, const Font& font,
                             GlyphID glyph, Point baseline,
                             ColorFormatPreference pref,
                             int palette_index,
                             uint32_t foreground_argb) {
    auto* fi = font_impl(font);
    if (!fi) return false;
    if (!fi->otf.valid()) return false;

    uint16_t target_ppem = static_cast<uint16_t>(
        std::clamp(fi->size_, 1.0f, 65535.0f));

    auto fmt = fi->otf.color_format(glyph,
                                     kcg_pref_to_otf(pref),
                                     target_ppem);

    // We need a Context handle to call the public API. Reconstruct
    // a temporary Context view over impl. Context's protected ctor
    // (taking Impl*) is the legitimate path; we use a thin local
    // subclass to access it.
    struct ContextView : Context {
        explicit ContextView(Impl* i) : Context(i) {}
        ~ContextView() override { m_impl = nullptr; } // don't delete shared impl
    };
    ContextView gc(&impl);

    bool handled = false;
    switch (fmt) {
    case otf::ColorFontFormat::sbix:
        handled = render_sbix(gc, *fi, glyph, baseline);
        break;
    case otf::ColorFontFormat::cbdt:
        handled = render_cbdt(gc, *fi, glyph, baseline);
        break;
    case otf::ColorFontFormat::colr_v1:
        handled = render_colr_v1(gc, impl, *fi, glyph,
                                  palette_index, foreground_argb, baseline);
        // COLR v1 may legitimately return false for glyphs that have
        // no v1 record; let the caller fall back to v0 or outline.
        if (!handled && fi->otf.has_colr_v0()) {
            handled = render_colr_v0(gc, impl, *fi, glyph,
                                     palette_index, foreground_argb, baseline);
        }
        break;
    case otf::ColorFontFormat::colr_v0:
        handled = render_colr_v0(gc, impl, *fi, glyph,
                                  palette_index, foreground_argb, baseline);
        break;
    case otf::ColorFontFormat::svg:
        // kcg does not link SVG. The bytes are available via
        // Font::glyph_svg_data() for external rendering (kosmsvg).
        // We report "handled" to suppress monochrome fallback —
        // showing the .notdef outline of an emoji glyph is uglier
        // than a missing glyph slot.
        handled = true;
        break;
    case otf::ColorFontFormat::none:
        handled = false;
        break;
    }
    return handled;
}

} // namespace detail



// FontCollection: bulk loading


#ifndef KCG_DISABLE_FONT_COLLECTION_LOAD

} // namespace kcg (close before platform headers)

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#ifdef __linux__
#include <linux/limits.h>
#else
#include <limits.h>
#endif
#include <sys/mman.h>
#include <sys/stat.h>

namespace kcg { // reopen

// -- Memory-mapped file for probing font metadata --

struct MappedFile {
    void* data = nullptr;
    long length = 0;

    MappedFile() = default;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    ~MappedFile() { unmap(); }

    bool map(const char* filename) {
        int fd = ::open(filename, O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0 || st.st_size == 0) {
            ::close(fd);
            return false;
        }
        data = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                      PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (data == MAP_FAILED) {
            data = nullptr;
            return false;
        }
        length = static_cast<long>(st.st_size);
        return true;
    }

    void unmap() {
        if (data) {
            ::munmap(data, static_cast<size_t>(length));
            data = nullptr;
            length = 0;
        }
    }
};

// -- Font file extension check --

static bool is_font_file(const char* filename) {
    const char* ext = std::strrchr(filename, '.');
    if (!ext) return false;
    size_t len = std::strlen(ext);
    if (len != 4) return false;
    char lower[5];
    for (int i = 0; i < 4; ++i)
        lower[i] = static_cast<char>(
            (ext[i] >= 'A' && ext[i] <= 'Z') ? ext[i] + 32 : ext[i]);
    lower[4] = '\0';
    return std::strcmp(lower, ".ttf") == 0
        || std::strcmp(lower, ".otf") == 0
        || std::strcmp(lower, ".ttc") == 0
        || std::strcmp(lower, ".otc") == 0;
}

// -- Weight / style from font metadata --

static FontCollection::Weight weight_from_class(uint16_t wc) {
    if (wc == 0) return FontCollection::Weight::Regular;
    int rounded = ((wc + 50) / 100) * 100;
    rounded = std::clamp(rounded, 100, 900);
    switch (rounded) {
    case 100: return FontCollection::Weight::Thin;
    case 200:
    case 300: return FontCollection::Weight::Light;
    case 400: return FontCollection::Weight::Regular;
    case 500: return FontCollection::Weight::Medium;
    case 600: return FontCollection::Weight::Semibold;
    case 700: return FontCollection::Weight::Bold;
    case 800: return FontCollection::Weight::Heavy;
    case 900: return FontCollection::Weight::Black;
    default:  return FontCollection::Weight::Regular;
    }
}

// -- FontCollection::load_file --

int FontCollection::load_file(const char* filename, float default_size) {
    MappedFile mf;
    if (!mf.map(filename))
        return 0;

    auto data = std::span<const uint8_t>(
        static_cast<const uint8_t*>(mf.data),
        static_cast<size_t>(mf.length));

    int num_fonts = otf::FontData::face_count(data);
    int loaded = 0;

    for (int index = 0; index < num_fonts; ++index) {
        auto probe = otf::FontData::open(data, index);
        if (!probe) continue;

        auto family = probe->family_name();
        if (!family || family->empty()) continue;

        // Detect weight
        Weight weight = Weight::Regular;
        uint16_t wc = probe->weight_class();
        if (wc > 0) {
            weight = weight_from_class(wc);
        } else {
            uint16_t ms = probe->mac_style();
            if (ms & 0x1) weight = Weight::Bold;
        }

        // Detect style
        Style style = Style::Normal;
        uint16_t ms = probe->mac_style();
        if (ms & 0x2) style = Style::Italic;

        Font face = Font::load(filename, default_size, index);
        if (!face) continue;

        add(*family, weight, style, face);
        ++loaded;
    }

    return loaded;
}

// -- FontCollection::load_dir --

int FontCollection::load_dir(const char* dirname, float default_size) {
    DIR* dir = opendir(dirname);
    if (!dir) return 0;

    int loaded = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") == 0
         || std::strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        std::snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name);
        struct stat st;
        if (stat(path, &st) == -1) continue;
        if (S_ISDIR(st.st_mode)) {
            loaded += load_dir(path, default_size);
        } else if (S_ISREG(st.st_mode) && is_font_file(path)) {
            loaded += load_file(path, default_size);
        }
    }

    closedir(dir);
    return loaded;
}

// -- FontCollection::load_system --

int FontCollection::load_system(float default_size) {
    int loaded = 0;
#if defined(__APPLE__)
    loaded += load_dir("/Library/Fonts", default_size);
    loaded += load_dir("/System/Library/Fonts", default_size);
#elif defined(__HAIKU__)
    loaded += load_dir("/boot/system/data/fonts/otfonts", default_size);
    loaded += load_dir("/boot/system/data/fonts/ttfonts", default_size);
    loaded += load_dir("/boot/home/config/non-packaged/data/fonts", default_size);
#elif defined(__linux__)
    loaded += load_dir("/usr/share/fonts", default_size);
    loaded += load_dir("/usr/local/share/fonts", default_size);
#endif
    return loaded;
}

#else // KCG_DISABLE_FONT_COLLECTION_LOAD

int FontCollection::load_file(const char*, float) { return -1; }
int FontCollection::load_dir(const char*, float) { return -1; }
int FontCollection::load_system(float) { return -1; }

#endif // KCG_DISABLE_FONT_COLLECTION_LOAD

} // namespace kcg
