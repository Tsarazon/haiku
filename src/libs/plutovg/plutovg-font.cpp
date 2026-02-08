// plutovg-font.cpp - Text iterator, FontFace and FontFaceCache implementation
// C++20

#include "plutovg-private.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "plutovg-stb-truetype.h"

namespace plutovg {

// -- StbttFontInfo (defined here, forward-declared in private header) --

struct GlyphData {
    Codepoint codepoint = 0;
    stbtt_vertex* vertices = nullptr;
    int nvertices = 0;
    int index = 0;
    int advance_width = 0;
    int left_side_bearing = 0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

struct StbttFontInfo {
    stbtt_fontinfo info = {};
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    mutable std::mutex cache_mutex;
    mutable std::unordered_map<Codepoint, GlyphData> glyph_cache;

    ~StbttFontInfo() {
        for (auto& [cp, glyph] : glyph_cache) {
            if (glyph.vertices)
                stbtt_FreeShape(&info, glyph.vertices);
        }
    }

    float scale_for_size(float size) const {
        return stbtt_ScaleForMappingEmToPixels(&info, size);
    }

    const GlyphData& get_glyph(Codepoint codepoint) const {
        std::lock_guard lock(cache_mutex);
        auto it = glyph_cache.find(codepoint);
        if (it != glyph_cache.end())
            return it->second;

        GlyphData glyph;
        glyph.codepoint = codepoint;
        glyph.index = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
        glyph.nvertices = stbtt_GetGlyphShape(&info, glyph.index, &glyph.vertices);
        stbtt_GetGlyphHMetrics(&info, glyph.index, &glyph.advance_width, &glyph.left_side_bearing);
        if (!stbtt_GetGlyphBox(&info, glyph.index, &glyph.x1, &glyph.y1, &glyph.x2, &glyph.y2))
            glyph.x1 = glyph.y1 = glyph.x2 = glyph.y2 = 0;

        auto [inserted, _] = glyph_cache.emplace(codepoint, glyph);
        return inserted->second;
    }
};

// FontFace::Impl destructor (StbttFontInfo is now complete)
FontFace::Impl::~Impl() { release(data); }

// -- TextIterator --

static int text_length(const void* data, TextEncoding encoding) {
    int length = 0;
    switch (encoding) {
    case TextEncoding::UTF8: {
        auto* text = static_cast<const uint8_t*>(data);
        while (*text++) ++length;
        break;
    }
    case TextEncoding::UTF16: {
        auto* text = static_cast<const uint16_t*>(data);
        while (*text++) ++length;
        break;
    }
    case TextEncoding::UTF32: {
        auto* text = static_cast<const uint32_t*>(data);
        while (*text++) ++length;
        break;
    }
    }
    return length;
}

TextIterator::TextIterator(const void* text, int length, TextEncoding encoding)
    : m_text(text)
    , m_length(length == -1 ? text_length(text, encoding) : length)
    , m_encoding(encoding)
{}

bool TextIterator::has_next() const {
    return m_index < m_length;
}

Codepoint TextIterator::next() {
    Codepoint codepoint = 0;
    switch (m_encoding) {
    case TextEncoding::UTF8: {
        static constexpr uint8_t trailing[256] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
            2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5,
        };

        static constexpr uint32_t offsets[6] = {
            0x00000000, 0x00003080, 0x000E2080, 0x03C82080, 0xFA082080, 0x82082080,
        };

        auto* text = static_cast<const uint8_t*>(m_text);
        uint8_t trail = trailing[text[m_index]];
        uint32_t offset = offsets[trail];
        while (trail > 0 && m_index < m_length - 1) {
            codepoint += text[m_index++];
            codepoint <<= 6;
            --trail;
        }
        codepoint += text[m_index++];
        codepoint -= offset;
        break;
    }
    case TextEncoding::UTF16: {
        auto* text = static_cast<const uint16_t*>(m_text);
        codepoint = text[m_index++];
        if ((codepoint & 0xFFFFFC00) == 0xD800) {
            if (m_index < m_length) {
                uint16_t trail = text[m_index];
                if ((trail & 0xFFFFFC00) == 0xDC00) {
                    ++m_index;
                    codepoint = (codepoint << 10) + trail - ((0xD800u << 10) - 0x10000u + 0xDC00u);
                }
            }
        }
        break;
    }
    case TextEncoding::UTF32: {
        auto* text = static_cast<const uint32_t*>(m_text);
        codepoint = text[m_index++];
        break;
    }
    }
    return codepoint;
}

// -- FontFace: COW ref-counting --

FontFace::FontFace() : m_impl(nullptr) {}

FontFace::~FontFace() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

FontFace::FontFace(const FontFace& other) : m_impl(other.m_impl) {
    if (m_impl)
        m_impl->ref();
}

FontFace::FontFace(FontFace&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

FontFace& FontFace::operator=(const FontFace& other) {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        if (m_impl)
            m_impl->ref();
    }
    return *this;
}

FontFace& FontFace::operator=(FontFace&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

FontFace::operator bool() const {
    return m_impl != nullptr;
}

// -- FontFace: internal init --

static FontFace make_font_face(const void* data, unsigned int length, int ttcindex,
                               DataRelease release) {
    auto font_info = std::make_unique<StbttFontInfo>();
    int offset = stbtt_GetFontOffsetForIndex(static_cast<const unsigned char*>(data), ttcindex);
    if (offset == -1 || !stbtt_InitFont(&font_info->info, static_cast<const unsigned char*>(data), offset)) {
        if (release.fn)
            release.fn(release.ctx);
        return {};
    }

    stbtt_GetFontVMetrics(&font_info->info,
                          &font_info->ascent, &font_info->descent, &font_info->line_gap);
    stbtt_GetFontBoundingBox(&font_info->info,
                             &font_info->x1, &font_info->y1, &font_info->x2, &font_info->y2);

    auto* impl = new FontFace::Impl;
    impl->font_info = std::move(font_info);
    impl->data = static_cast<const std::byte*>(data);
    impl->data_length = length;
    impl->release = release;

    // Access private m_impl via layout (same pattern as canvas.cpp path_impl/paint_impl).
    FontFace face;
    static_assert(sizeof(FontFace) == sizeof(void*));
    *reinterpret_cast<FontFace::Impl**>(&face) = impl;
    return face;
}

// -- FontFace: factories --

FontFace FontFace::load_from_file(const char* filename, int ttcindex) {
    std::FILE* fp = std::fopen(filename, "rb");
    if (!fp)
        return {};

    std::fseek(fp, 0, SEEK_END);
    long length = std::ftell(fp);
    if (length == -1L) {
        std::fclose(fp);
        return {};
    }

    auto* data = new std::byte[static_cast<size_t>(length)];
    std::fseek(fp, 0, SEEK_SET);
    size_t nread = std::fread(data, 1, static_cast<size_t>(length), fp);
    std::fclose(fp);

    if (nread != static_cast<size_t>(length)) {
        delete[] data;
        return {};
    }

    DataRelease release;
    release.fn = [](void* ctx) { delete[] static_cast<std::byte*>(ctx); };
    release.ctx = data;
    return make_font_face(data, static_cast<unsigned int>(length), ttcindex, release);
}

FontFace FontFace::load_from_data(std::span<const std::byte> data, int ttcindex) {
    auto* copy = new std::byte[data.size()];
    std::memcpy(copy, data.data(), data.size());

    DataRelease release;
    release.fn = [](void* ctx) { delete[] static_cast<std::byte*>(ctx); };
    release.ctx = copy;
    return make_font_face(copy, static_cast<unsigned int>(data.size()), ttcindex, release);
}

FontFace FontFace::load_from_data(std::unique_ptr<std::byte[]> data, size_t length, int ttcindex) {
    auto* raw = data.release();
    DataRelease release;
    release.fn = [](void* ctx) { delete[] static_cast<std::byte*>(ctx); };
    release.ctx = raw;
    return make_font_face(raw, static_cast<unsigned int>(length), ttcindex, release);
}

FontFace FontFace::load_from_data(const void* data, size_t length, int ttcindex,
                                  void (*release_fn)(void*), void* context) {
    DataRelease release;
    release.fn = release_fn;
    release.ctx = context;
    return make_font_face(data, static_cast<unsigned int>(length), ttcindex, release);
}

// -- FontFace: metrics --

FontMetrics FontFace::metrics(float size) const {
    if (!m_impl)
        return {};
    auto& fi = *m_impl->font_info;
    float scale = fi.scale_for_size(size);
    FontMetrics m;
    m.ascent = fi.ascent * scale;
    m.descent = fi.descent * scale;
    m.line_gap = fi.line_gap * scale;
    m.extents = {
        fi.x1 * scale,
        fi.y2 * -scale,
        (fi.x2 - fi.x1) * scale,
        (fi.y1 - fi.y2) * -scale,
    };
    return m;
}

GlyphMetrics FontFace::glyph_metrics(float size, Codepoint codepoint) const {
    if (!m_impl)
        return {};
    auto& fi = *m_impl->font_info;
    float scale = fi.scale_for_size(size);
    auto& glyph = fi.get_glyph(codepoint);
    GlyphMetrics m;
    m.advance_width = glyph.advance_width * scale;
    m.left_side_bearing = glyph.left_side_bearing * scale;
    m.extents = {
        glyph.x1 * scale,
        glyph.y2 * -scale,
        (glyph.x2 - glyph.x1) * scale,
        (glyph.y1 - glyph.y2) * -scale,
    };
    return m;
}

// -- FontFace: glyph path --

static void glyph_to_path_func(void* closure, PathCommand command,
                                const Point* points, [[maybe_unused]] int npoints) {
    auto& path = *static_cast<Path*>(closure);
    switch (command) {
    case PathCommand::MoveTo:
        path.move_to(points[0].x, points[0].y);
        break;
    case PathCommand::LineTo:
        path.line_to(points[0].x, points[0].y);
        break;
    case PathCommand::CubicTo:
        path.cubic_to(points[0].x, points[0].y,
                      points[1].x, points[1].y,
                      points[2].x, points[2].y);
        break;
    case PathCommand::Close:
        assert(false);
        break;
    }
}

float FontFace::get_glyph_path(float size, float x, float y,
                                Codepoint codepoint, Path& path) const {
    return traverse_glyph_path_raw(size, x, y, codepoint, glyph_to_path_func, &path);
}

float FontFace::traverse_glyph_path_raw(float size, float x, float y,
                                         Codepoint codepoint,
                                         Path::TraverseFunc traverse_func,
                                         void* closure) const {
    if (!m_impl)
        return 0.0f;
    auto& fi = *m_impl->font_info;
    float scale = fi.scale_for_size(size);

    Matrix matrix{scale, 0, 0, -scale, x, y};

    Point points[3];
    Point current_point{0, 0};
    auto& glyph = fi.get_glyph(codepoint);

    for (int i = 0; i < glyph.nvertices; ++i) {
        auto& v = glyph.vertices[i];
        switch (v.type) {
        case STBTT_vmove:
            points[0] = {static_cast<float>(v.x), static_cast<float>(v.y)};
            current_point = points[0];
            matrix.map_points(points, points, 1);
            traverse_func(closure, PathCommand::MoveTo, points, 1);
            break;
        case STBTT_vline:
            points[0] = {static_cast<float>(v.x), static_cast<float>(v.y)};
            current_point = points[0];
            matrix.map_points(points, points, 1);
            traverse_func(closure, PathCommand::LineTo, points, 1);
            break;
        case STBTT_vcurve: {
            float cx = static_cast<float>(v.cx);
            float cy = static_cast<float>(v.cy);
            float vx = static_cast<float>(v.x);
            float vy = static_cast<float>(v.y);
            points[0] = {2.0f / 3.0f * cx + 1.0f / 3.0f * current_point.x,
                         2.0f / 3.0f * cy + 1.0f / 3.0f * current_point.y};
            points[1] = {2.0f / 3.0f * cx + 1.0f / 3.0f * vx,
                         2.0f / 3.0f * cy + 1.0f / 3.0f * vy};
            points[2] = {vx, vy};
            current_point = points[2];
            matrix.map_points(points, points, 3);
            traverse_func(closure, PathCommand::CubicTo, points, 3);
            break;
        }
        case STBTT_vcubic:
            points[0] = {static_cast<float>(v.cx), static_cast<float>(v.cy)};
            points[1] = {static_cast<float>(v.cx1), static_cast<float>(v.cy1)};
            points[2] = {static_cast<float>(v.x), static_cast<float>(v.y)};
            current_point = points[2];
            matrix.map_points(points, points, 3);
            traverse_func(closure, PathCommand::CubicTo, points, 3);
            break;
        default:
            assert(false);
        }
    }

    return glyph.advance_width * scale;
}

// -- FontFace: text extents --

float FontFace::text_extents(float size, const void* text, int length,
                              TextEncoding encoding, Rect* extents) const {
    TextIterator it(text, length, encoding);
    bool has_extents = false;
    float total_advance = 0.0f;

    while (it.has_next()) {
        Codepoint cp = it.next();
        auto gm = glyph_metrics(size, cp);

        if (!extents) {
            total_advance += gm.advance_width;
            continue;
        }

        Rect ge = gm.extents;
        ge.x += total_advance;
        total_advance += gm.advance_width;

        if (!has_extents) {
            *extents = ge;
            has_extents = true;
            continue;
        }

        float x1 = std::min(extents->x, ge.x);
        float y1 = std::min(extents->y, ge.y);
        float x2 = std::max(extents->right(), ge.right());
        float y2 = std::max(extents->bottom(), ge.bottom());
        *extents = {x1, y1, x2 - x1, y2 - y1};
    }

    if (extents && !has_extents)
        *extents = {};
    return total_advance;
}

// -- FontFaceCache: COW ref-counting --

FontFaceCache::FontFaceCache() : m_impl(new Impl) {}

FontFaceCache::~FontFaceCache() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

FontFaceCache::FontFaceCache(const FontFaceCache& other) : m_impl(other.m_impl) {
    if (m_impl)
        m_impl->ref();
}

FontFaceCache::FontFaceCache(FontFaceCache&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

FontFaceCache& FontFaceCache::operator=(const FontFaceCache& other) {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        if (m_impl)
            m_impl->ref();
    }
    return *this;
}

FontFaceCache& FontFaceCache::operator=(FontFaceCache&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

FontFaceCache::operator bool() const {
    return m_impl != nullptr;
}

// -- FontFaceCache: COW detach helper --

static FontFaceCache::Impl* detach(FontFaceCache::Impl*& impl) {
    if (impl->count() > 1) {
        auto* copy = new FontFaceCache::Impl;
        copy->entries = impl->entries;
        impl->deref();
        impl = copy;
    }
    return impl;
}

// -- FontFaceCache: mutators --

void FontFaceCache::reset() {
    auto* d = detach(m_impl);
    d->entries.clear();
}

void FontFaceCache::add(std::string_view family, bool bold, bool italic, const FontFace& face) {
    auto* d = detach(m_impl);
    d->entries.push_back({std::string(family), bold, italic, face});
}

bool FontFaceCache::add_file(std::string_view family, bool bold, bool italic,
                             const char* filename, int ttcindex) {
    FontFace face = FontFace::load_from_file(filename, ttcindex);
    if (!face)
        return false;
    add(family, bold, italic, face);
    return true;
}

// -- FontFaceCache: lookup --

FontFace FontFaceCache::get(std::string_view family, bool bold, bool italic) const {
    if (!m_impl)
        return {};

    const Impl::Entry* best = nullptr;
    int best_score = -1;

    for (auto& entry : m_impl->entries) {
        if (entry.family != family)
            continue;
        int score = (bold == entry.bold ? 1 : 0) + (italic == entry.italic ? 1 : 0);
        if (score > best_score) {
            best_score = score;
            best = &entry;
        }
    }

    return best ? best->face : FontFace{};
}

// -- FontFaceCache: bulk loading --

#ifndef PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD

} // namespace plutovg (close before platform headers)

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

namespace plutovg { // reopen

// -- Memory-mapped file --

struct MappedFile {
    void* data = nullptr;
    long length = 0;

    MappedFile() = default;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    ~MappedFile() { unmap(); }

    bool map(const char* filename);
    void unmap();
};

bool MappedFile::map(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return false;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size == 0) {
        close(fd);
        return false;
    }
    data = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) {
        data = nullptr;
        return false;
    }
    length = static_cast<long>(st.st_size);
    return true;
}

void MappedFile::unmap() {
    if (data) {
        ::munmap(data, static_cast<size_t>(length));
        data = nullptr;
        length = 0;
    }
}

// -- Font name table parsing --

static std::string decode_unicode_family(const uint8_t* name_data, size_t byte_length) {
    std::string result;
    result.reserve(byte_length);
    const uint8_t* p = name_data;
    size_t remaining = byte_length;

    while (remaining >= 2) {
        uint16_t ch = static_cast<uint16_t>(p[0] * 256 + p[1]);

        if (ch >= 0xD800 && ch < 0xDC00 && remaining >= 4) {
            uint16_t ch2 = static_cast<uint16_t>(p[2] * 256 + p[3]);
            uint32_t c = ((ch - 0xD800u) << 10) + (ch2 - 0xDC00u) + 0x10000u;
            result += static_cast<char>(0xF0 + (c >> 18));
            result += static_cast<char>(0x80 + ((c >> 12) & 0x3F));
            result += static_cast<char>(0x80 + ((c >> 6) & 0x3F));
            result += static_cast<char>(0x80 + (c & 0x3F));
            p += 4;
            remaining -= 4;
        } else if (ch < 0x80) {
            result += static_cast<char>(ch);
            p += 2;
            remaining -= 2;
        } else if (ch < 0x800) {
            result += static_cast<char>(0xC0 + (ch >> 6));
            result += static_cast<char>(0x80 + (ch & 0x3F));
            p += 2;
            remaining -= 2;
        } else {
            result += static_cast<char>(0xE0 + (ch >> 12));
            result += static_cast<char>(0x80 + ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 + (ch & 0x3F));
            p += 2;
            remaining -= 2;
        }
    }
    return result;
}

static std::string decode_roman_family(const uint8_t* name_data, size_t byte_length) {
    static constexpr uint16_t mac_roman_table[256] = {
        0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,
        0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x000F,
        0x0010,0x2318,0x21E7,0x2325,0x2303,0x0015,0x0016,0x0017,
        0x0018,0x0019,0x001A,0x001B,0x001C,0x001D,0x001E,0x001F,
        0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,
        0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F,
        0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,
        0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F,
        0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,
        0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F,
        0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,
        0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F,
        0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,
        0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F,
        0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,
        0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x007F,
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
    result.reserve(byte_length * 2);
    for (size_t i = 0; i < byte_length; ++i) {
        uint16_t ch = mac_roman_table[name_data[i]];
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

// -- Font file extension check --

static bool is_font_file(const char* filename) {
    const char* ext = std::strrchr(filename, '.');
    if (!ext || std::strlen(ext) != 4)
        return false;
    char lower[5];
    for (int i = 0; i < 4; ++i)
        lower[i] = static_cast<char>(ext[i] >= 'A' && ext[i] <= 'Z' ? ext[i] + 32 : ext[i]);
    lower[4] = '\0';
    return std::strcmp(lower, ".ttf") == 0
        || std::strcmp(lower, ".otf") == 0
        || std::strcmp(lower, ".ttc") == 0
        || std::strcmp(lower, ".otc") == 0;
}

// -- FontFaceCache::load_file --

int FontFaceCache::load_file(const char* filename) {
    MappedFile mf;
    if (!mf.map(filename))
        return 0;

    auto* data = static_cast<const stbtt_uint8*>(mf.data);
    int num_fonts = stbtt_GetNumberOfFonts(data);
    int num_faces = 0;

    for (int index = 0; index < num_fonts; ++index) {
        int offset = stbtt_GetFontOffsetForIndex(data, index);
        if (offset == -1 || !stbtt__isfont(data + offset))
            continue;

        stbtt_uint32 nm = stbtt__find_table(data, offset, "name");
        stbtt_uint16 nm_count = ttUSHORT(data + nm + 2);

        const uint8_t* unicode_family_name = nullptr;
        const uint8_t* roman_family_name = nullptr;
        size_t family_byte_length = 0;

        for (stbtt_int32 i = 0; i < nm_count; ++i) {
            stbtt_uint32 loc = nm + 6 + 12 * i;
            stbtt_uint16 nm_id = ttUSHORT(data + loc + 6);
            if (nm_id != 1)
                continue;

            stbtt_uint16 platform = ttUSHORT(data + loc + 0);
            stbtt_uint16 encoding = ttUSHORT(data + loc + 2);
            const uint8_t* name_ptr = data + nm + ttUSHORT(data + nm + 4) + ttUSHORT(data + loc + 10);
            stbtt_uint16 name_len = ttUSHORT(data + loc + 8);

            if (platform == 1 && encoding == 0) {
                family_byte_length = name_len;
                roman_family_name = name_ptr;
                continue;
            }
            if (platform == 0 || (platform == 3 && encoding == 1) || (platform == 3 && encoding == 10)) {
                family_byte_length = name_len;
                unicode_family_name = name_ptr;
                break;
            }
        }

        if (!unicode_family_name && !roman_family_name)
            continue;

        std::string family;
        if (unicode_family_name)
            family = decode_unicode_family(unicode_family_name, family_byte_length);
        else
            family = decode_roman_family(roman_family_name, family_byte_length);

        bool bold = false;
        bool italic = false;
        stbtt_uint32 hd = stbtt__find_table(data, offset, "head");
        stbtt_uint16 style = ttUSHORT(data + hd + 44);
        if (style & 0x1) bold = true;
        if (style & 0x2) italic = true;

        FontFace face = FontFace::load_from_file(filename, index);
        if (!face)
            continue;

        add(family, bold, italic, face);
        ++num_faces;
    }

    return num_faces;
}

// -- FontFaceCache::load_dir --

int FontFaceCache::load_dir(const char* dirname) {
    DIR* dir = opendir(dirname);
    if (!dir)
        return 0;

    int num_faces = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        char path[PATH_MAX];
        std::snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name);
        struct stat st;
        if (stat(path, &st) == -1)
            continue;
        if (S_ISDIR(st.st_mode)) {
            num_faces += load_dir(path);
        } else if (S_ISREG(st.st_mode) && is_font_file(path)) {
            num_faces += load_file(path);
        }
    }

    closedir(dir);
    return num_faces;
}

// -- FontFaceCache::load_sys --

int FontFaceCache::load_sys() {
    int num_faces = 0;
#if defined(__APPLE__)
    num_faces += load_dir("/Library/Fonts");
    num_faces += load_dir("/System/Library/Fonts");
#elif defined(__linux__)
    num_faces += load_dir("/usr/share/fonts");
    num_faces += load_dir("/usr/local/share/fonts");
#endif
    return num_faces;
}

#else // PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD

int FontFaceCache::load_file(const char*) { return -1; }
int FontFaceCache::load_dir(const char*) { return -1; }
int FontFaceCache::load_sys() { return -1; }

#endif // PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD

} // namespace plutovg
