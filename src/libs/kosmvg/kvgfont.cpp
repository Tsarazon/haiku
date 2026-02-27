// kvgfont.cpp — Font and FontCollection implementation
// C++20

#include "kvgprivate.hpp"
#include "kvgotf.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace kvg {

// -- Font data ownership --

struct DataRelease {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;

    void operator()(const std::byte*) const noexcept {
        if (fn) fn(ctx);
    }
};

// -- Font::Impl --

struct Font::Impl : RefCounted {
    otf::FontData otf;
    const std::byte* data = nullptr;
    size_t data_length = 0;
    DataRelease release;
    float size_ = 12.0f;

    std::string cached_family;
    std::string cached_style;

    struct GlyphPathEntry {
        Path path;
    };
    mutable std::shared_mutex cache_mutex;
    mutable std::unordered_map<uint16_t, GlyphPathEntry> path_cache;

    GlyphPathEntry cached_glyph_path(uint16_t glyph) const;

    ~Impl();
};

// -- FontCollection::Impl --

struct FontCollection::Impl : RefCounted {
    struct Entry {
        std::string family;
        FontCollection::Weight weight = FontCollection::Weight::Regular;
        FontCollection::Style  style  = FontCollection::Style::Normal;
        Font face;
    };

    std::vector<Entry> entries;
};


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
    otf.glyph_outline(glyph, gs);
    entry.path = builder.build();

    std::unique_lock lock(cache_mutex);
    // Double-check: another thread may have inserted while we parsed
    auto it = path_cache.find(glyph);
    if (it != path_cache.end())
        return it->second;

    auto [inserted, _] = path_cache.emplace(glyph, std::move(entry));
    return inserted->second;
}

Font::Impl::~Impl() {
    release(data);
}


// Font: COW ref-counting


Font::Font() : m_impl(nullptr) {}

Font::~Font() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

Font::Font(const Font& other) : m_impl(other.m_impl) {
    if (m_impl)
        m_impl->ref();
}

Font::Font(Font&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

Font& Font::operator=(const Font& other) {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        if (m_impl)
            m_impl->ref();
    }
    return *this;
}

Font& Font::operator=(Font&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

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

Font Font::with_variations(std::span<const FontVariation> /*axes*/) const {
    if (!m_impl || !m_impl->otf.has_fvar())
        return {};
    // Variable font instance creation requires a gvar interpolation engine.
    // The gvar table parser is not yet implemented, so we cannot produce
    // variation instances at this time. Return a clone at default values
    // with the same size.
    return with_size(m_impl->size_);
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


ColorFontFormat Font::color_format(GlyphID /*glyph*/) const {
    if (!m_impl) return ColorFontFormat::None;
    auto& otf = m_impl->otf;

    // Detection order: COLR v1 > COLR v0 > sbix > CBDT
    if (otf.has_colr()) {
        auto loc = otf.locate_table(make_otf_tag('C','O','L','R'));
        auto* d = otf.data().data();
        if (loc.length >= 4) {
            uint16_t version = otf::read_u16(d + loc.offset);
            return (version >= 1) ? ColorFontFormat::COLR_v1
                                  : ColorFontFormat::COLR_v0;
        }
    }
    if (otf.has_sbix()) return ColorFontFormat::Sbix;
    if (otf.has_cbdt()) return ColorFontFormat::CBDT;
    return ColorFontFormat::None;
}

bool Font::has_color_glyphs() const {
    if (!m_impl) return false;
    auto& otf = m_impl->otf;
    return otf.has_colr() || otf.has_sbix() || otf.has_cbdt();
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
    float a = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
    float r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    float g = static_cast<float>((argb >>  8) & 0xFF) / 255.0f;
    float b = static_cast<float>(argb & 0xFF) / 255.0f;
    return Color{r, g, b, a};
}


// Font: size and metrics


float Font::size() const {
    return m_impl ? m_impl->size_ : 0.0f;
}

FontMetrics Font::metrics() const {
    if (!m_impl) return {};
    auto& otf = m_impl->otf;
    float scale = otf.scale_for_em(m_impl->size_);

    auto vm = otf.vmetrics();
    auto bb = otf.bounding_box();

    FontMetrics m;
    m.ascent = vm.ascent * scale;
    m.descent = vm.descent * scale;
    m.leading = vm.line_gap * scale;
    m.x_height = otf.x_height() * scale;
    m.cap_height = otf.cap_height() * scale;
    m.bounding_box = Rect{
        bb.x0 * scale,
        -bb.y1 * scale,
        (bb.x1 - bb.x0) * scale,
        (bb.y1 - bb.y0) * scale
    };
    m.units_per_em = static_cast<float>(otf.units_per_em());
    return m;
}

GlyphMetrics Font::glyph_metrics(GlyphID glyph) const {
    if (!m_impl) return {};
    auto& otf = m_impl->otf;
    float scale = otf.scale_for_em(m_impl->size_);

    auto hm = otf.hmetrics(glyph);
    otf::BBox box;
    bool has_box = otf.glyph_box(glyph, box);

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


// Font: character mapping


GlyphID Font::glyph_for_codepoint(Codepoint cp) const {
    if (!m_impl) return 0;
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
    auto& otf = m_impl->otf;
    float scale = otf.scale_for_em(m_impl->size_);
    float total = 0.0f;
    uint16_t prev = 0;

    auto* p = reinterpret_cast<const uint8_t*>(utf8.data());
    auto* end = p + utf8.size();
    while (p < end) {
        uint32_t cp = decode_utf8(p, end);
        uint16_t glyph = otf.glyph_index(cp);
        if (prev)
            total += otf.kern_advance(prev, glyph) * scale;
        total += otf.hmetrics(glyph).advance_width * scale;
        prev = glyph;
    }
    return total;
}

Rect Font::text_bounds(std::string_view utf8, Point origin) const {
    if (!m_impl) return {};
    auto& otf = m_impl->otf;
    float scale = otf.scale_for_em(m_impl->size_);
    bool has_bounds = false;
    float total_advance = 0.0f;
    uint16_t prev = 0;
    Rect result;

    auto* p = reinterpret_cast<const uint8_t*>(utf8.data());
    auto* end = p + utf8.size();
    while (p < end) {
        uint32_t cp = decode_utf8(p, end);
        uint16_t glyph = otf.glyph_index(cp);

        if (prev)
            total_advance += otf.kern_advance(prev, glyph) * scale;

        otf::BBox box;
        if (otf.glyph_box(glyph, box)) {
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

        total_advance += otf.hmetrics(glyph).advance_width * scale;
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

FontCollection::~FontCollection() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

FontCollection::FontCollection(const FontCollection& other) : m_impl(other.m_impl) {
    if (m_impl)
        m_impl->ref();
}

FontCollection::FontCollection(FontCollection&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

FontCollection& FontCollection::operator=(const FontCollection& other) {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        if (m_impl)
            m_impl->ref();
    }
    return *this;
}

FontCollection& FontCollection::operator=(FontCollection&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

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


// FontCollection: bulk loading


#ifndef KVG_DISABLE_FONT_COLLECTION_LOAD

} // namespace kvg (close before platform headers)

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

namespace kvg { // reopen

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
#elif defined(__linux__) || defined(__HAIKU__)
    loaded += load_dir("/usr/share/fonts", default_size);
    loaded += load_dir("/usr/local/share/fonts", default_size);
#endif
    return loaded;
}

#else // KVG_DISABLE_FONT_COLLECTION_LOAD

int FontCollection::load_file(const char*, float) { return -1; }
int FontCollection::load_dir(const char*, float) { return -1; }
int FontCollection::load_system(float) { return -1; }

#endif // KVG_DISABLE_FONT_COLLECTION_LOAD

} // namespace kvg
