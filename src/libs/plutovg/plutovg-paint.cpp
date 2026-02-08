// plutovg-paint.cpp - Color and Paint implementation
// C++20

#include "plutovg-private.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace plutovg {

// -- Color: HSL --

static inline float hsl_component(float h, float s, float l, float n) {
    float k = std::fmod(n + h / 30.0f, 12.0f);
    float a = s * std::min(l, 1.0f - l);
    return l - a * std::max(-1.0f, std::min(1.0f, std::min(k - 3.0f, 9.0f - k)));
}

Color Color::from_hsl(float h, float s, float l) {
    return from_hsla(h, s, l, 1.0f);
}

Color Color::from_hsla(float h, float s, float l, float a) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;

    float r = hsl_component(h, s, l, 0);
    float g = hsl_component(h, s, l, 8);
    float b = hsl_component(h, s, l, 4);
    return {
        std::clamp(r, 0.0f, 1.0f),
        std::clamp(g, 0.0f, 1.0f),
        std::clamp(b, 0.0f, 1.0f),
        std::clamp(a, 0.0f, 1.0f)
    };
}

// -- Color parsing --

static inline uint8_t hex_digit(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
    return static_cast<uint8_t>(10 + c - 'A');
}

static inline uint8_t hex_byte(char c1, char c2) {
    return static_cast<uint8_t>((hex_digit(c1) << 4) | hex_digit(c2));
}

static inline bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static bool parse_rgb_component(const char*& it, const char* end, float& component) {
    float value = 0;
    if (!parse_number(it, end, value))
        return false;
    if (skip_delim(it, end, '%'))
        value *= 2.55f;
    component = std::clamp(value, 0.0f, 255.0f) / 255.0f;
    return true;
}

static bool parse_alpha_component(const char*& it, const char* end, float& component) {
    float value = 0;
    if (!parse_number(it, end, value))
        return false;
    if (skip_delim(it, end, '%'))
        value /= 100.0f;
    component = std::clamp(value, 0.0f, 1.0f);
    return true;
}

struct ColorEntry {
    const char* name;
    uint32_t value;
};

static int color_entry_compare(const void* a, const void* b) {
    return std::strcmp(static_cast<const char*>(a),
                      static_cast<const ColorEntry*>(b)->name);
}

static constexpr ColorEntry colormap[] = {
    {"aliceblue", 0xF0F8FF},
    {"antiquewhite", 0xFAEBD7},
    {"aqua", 0x00FFFF},
    {"aquamarine", 0x7FFFD4},
    {"azure", 0xF0FFFF},
    {"beige", 0xF5F5DC},
    {"bisque", 0xFFE4C4},
    {"black", 0x000000},
    {"blanchedalmond", 0xFFEBCD},
    {"blue", 0x0000FF},
    {"blueviolet", 0x8A2BE2},
    {"brown", 0xA52A2A},
    {"burlywood", 0xDEB887},
    {"cadetblue", 0x5F9EA0},
    {"chartreuse", 0x7FFF00},
    {"chocolate", 0xD2691E},
    {"coral", 0xFF7F50},
    {"cornflowerblue", 0x6495ED},
    {"cornsilk", 0xFFF8DC},
    {"crimson", 0xDC143C},
    {"cyan", 0x00FFFF},
    {"darkblue", 0x00008B},
    {"darkcyan", 0x008B8B},
    {"darkgoldenrod", 0xB8860B},
    {"darkgray", 0xA9A9A9},
    {"darkgreen", 0x006400},
    {"darkgrey", 0xA9A9A9},
    {"darkkhaki", 0xBDB76B},
    {"darkmagenta", 0x8B008B},
    {"darkolivegreen", 0x556B2F},
    {"darkorange", 0xFF8C00},
    {"darkorchid", 0x9932CC},
    {"darkred", 0x8B0000},
    {"darksalmon", 0xE9967A},
    {"darkseagreen", 0x8FBC8F},
    {"darkslateblue", 0x483D8B},
    {"darkslategray", 0x2F4F4F},
    {"darkslategrey", 0x2F4F4F},
    {"darkturquoise", 0x00CED1},
    {"darkviolet", 0x9400D3},
    {"deeppink", 0xFF1493},
    {"deepskyblue", 0x00BFFF},
    {"dimgray", 0x696969},
    {"dimgrey", 0x696969},
    {"dodgerblue", 0x1E90FF},
    {"firebrick", 0xB22222},
    {"floralwhite", 0xFFFAF0},
    {"forestgreen", 0x228B22},
    {"fuchsia", 0xFF00FF},
    {"gainsboro", 0xDCDCDC},
    {"ghostwhite", 0xF8F8FF},
    {"gold", 0xFFD700},
    {"goldenrod", 0xDAA520},
    {"gray", 0x808080},
    {"green", 0x008000},
    {"greenyellow", 0xADFF2F},
    {"grey", 0x808080},
    {"honeydew", 0xF0FFF0},
    {"hotpink", 0xFF69B4},
    {"indianred", 0xCD5C5C},
    {"indigo", 0x4B0082},
    {"ivory", 0xFFFFF0},
    {"khaki", 0xF0E68C},
    {"lavender", 0xE6E6FA},
    {"lavenderblush", 0xFFF0F5},
    {"lawngreen", 0x7CFC00},
    {"lemonchiffon", 0xFFFACD},
    {"lightblue", 0xADD8E6},
    {"lightcoral", 0xF08080},
    {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2},
    {"lightgray", 0xD3D3D3},
    {"lightgreen", 0x90EE90},
    {"lightgrey", 0xD3D3D3},
    {"lightpink", 0xFFB6C1},
    {"lightsalmon", 0xFFA07A},
    {"lightseagreen", 0x20B2AA},
    {"lightskyblue", 0x87CEFA},
    {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xB0C4DE},
    {"lightyellow", 0xFFFFE0},
    {"lime", 0x00FF00},
    {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6},
    {"magenta", 0xFF00FF},
    {"maroon", 0x800000},
    {"mediumaquamarine", 0x66CDAA},
    {"mediumblue", 0x0000CD},
    {"mediumorchid", 0xBA55D3},
    {"mediumpurple", 0x9370DB},
    {"mediumseagreen", 0x3CB371},
    {"mediumslateblue", 0x7B68EE},
    {"mediumspringgreen", 0x00FA9A},
    {"mediumturquoise", 0x48D1CC},
    {"mediumvioletred", 0xC71585},
    {"midnightblue", 0x191970},
    {"mintcream", 0xF5FFFA},
    {"mistyrose", 0xFFE4E1},
    {"moccasin", 0xFFE4B5},
    {"navajowhite", 0xFFDEAD},
    {"navy", 0x000080},
    {"oldlace", 0xFDF5E6},
    {"olive", 0x808000},
    {"olivedrab", 0x6B8E23},
    {"orange", 0xFFA500},
    {"orangered", 0xFF4500},
    {"orchid", 0xDA70D6},
    {"palegoldenrod", 0xEEE8AA},
    {"palegreen", 0x98FB98},
    {"paleturquoise", 0xAFEEEE},
    {"palevioletred", 0xDB7093},
    {"papayawhip", 0xFFEFD5},
    {"peachpuff", 0xFFDAB9},
    {"peru", 0xCD853F},
    {"pink", 0xFFC0CB},
    {"plum", 0xDDA0DD},
    {"powderblue", 0xB0E0E6},
    {"purple", 0x800080},
    {"rebeccapurple", 0x663399},
    {"red", 0xFF0000},
    {"rosybrown", 0xBC8F8F},
    {"royalblue", 0x4169E1},
    {"saddlebrown", 0x8B4513},
    {"salmon", 0xFA8072},
    {"sandybrown", 0xF4A460},
    {"seagreen", 0x2E8B57},
    {"seashell", 0xFFF5EE},
    {"sienna", 0xA0522D},
    {"silver", 0xC0C0C0},
    {"skyblue", 0x87CEEB},
    {"slateblue", 0x6A5ACD},
    {"slategray", 0x708090},
    {"slategrey", 0x708090},
    {"snow", 0xFFFAFA},
    {"springgreen", 0x00FF7F},
    {"steelblue", 0x4682B4},
    {"tan", 0xD2B48C},
    {"teal", 0x008080},
    {"thistle", 0xD8BFD8},
    {"tomato", 0xFF6347},
    {"turquoise", 0x40E0D0},
    {"violet", 0xEE82EE},
    {"wheat", 0xF5DEB3},
    {"white", 0xFFFFFF},
    {"whitesmoke", 0xF5F5F5},
    {"yellow", 0xFFFF00},
    {"yellowgreen", 0x9ACD32},
};

static constexpr int colormap_size = sizeof(colormap) / sizeof(colormap[0]);
static constexpr int max_name_length = 20;

std::optional<std::pair<Color, int>> Color::parse(const char* data, int length) {
    if (length == -1)
        length = static_cast<int>(std::strlen(data));

    const char* it = data;
    const char* end = it + length;
    skip_ws(it, end);

    Color color;

    if (skip_delim(it, end, '#')) {
        int r, g, b, a = 255;
        const char* begin = it;
        while (it < end && is_hex(*it))
            ++it;
        int count = static_cast<int>(it - begin);

        if (count == 3 || count == 4) {
            r = hex_byte(begin[0], begin[0]);
            g = hex_byte(begin[1], begin[1]);
            b = hex_byte(begin[2], begin[2]);
            if (count == 4)
                a = hex_byte(begin[3], begin[3]);
        } else if (count == 6 || count == 8) {
            r = hex_byte(begin[0], begin[1]);
            g = hex_byte(begin[2], begin[3]);
            b = hex_byte(begin[4], begin[5]);
            if (count == 8)
                a = hex_byte(begin[6], begin[7]);
        } else {
            return std::nullopt;
        }

        color = Color::from_rgba8(r, g, b, a);
    } else {
        int name_length = 0;
        char name[max_name_length + 1];
        while (it < end && name_length < max_name_length && is_alpha(*it))
            name[name_length++] = to_lower(*it++);
        name[name_length] = '\0';

        if (std::strcmp(name, "transparent") == 0) {
            color = Color::transparent();
        } else if (std::strcmp(name, "rgb") == 0 || std::strcmp(name, "rgba") == 0) {
            if (!skip_ws_and_delim(it, end, '('))
                return std::nullopt;
            float r, g, b, a = 1.0f;
            if (!parse_rgb_component(it, end, r)
                || !skip_ws_and_comma(it, end)
                || !parse_rgb_component(it, end, g)
                || !skip_ws_and_comma(it, end)
                || !parse_rgb_component(it, end, b)) {
                return std::nullopt;
            }

            if (skip_ws_and_comma(it, end)
                && !parse_alpha_component(it, end, a)) {
                return std::nullopt;
            }

            skip_ws(it, end);
            if (!skip_delim(it, end, ')'))
                return std::nullopt;
            color = {
                std::clamp(r, 0.0f, 1.0f),
                std::clamp(g, 0.0f, 1.0f),
                std::clamp(b, 0.0f, 1.0f),
                std::clamp(a, 0.0f, 1.0f)
            };
        } else if (std::strcmp(name, "hsl") == 0 || std::strcmp(name, "hsla") == 0) {
            if (!skip_ws_and_delim(it, end, '('))
                return std::nullopt;
            float h, s, l, a = 1.0f;
            if (!parse_number(it, end, h)
                || !skip_ws_and_comma(it, end)
                || !parse_alpha_component(it, end, s)
                || !skip_ws_and_comma(it, end)
                || !parse_alpha_component(it, end, l)) {
                return std::nullopt;
            }

            if (skip_ws_and_comma(it, end)
                && !parse_alpha_component(it, end, a)) {
                return std::nullopt;
            }

            skip_ws(it, end);
            if (!skip_delim(it, end, ')'))
                return std::nullopt;
            color = Color::from_hsla(h, s, l, a);
        } else {
            const auto* entry = static_cast<const ColorEntry*>(
                std::bsearch(name, colormap, colormap_size, sizeof(ColorEntry), color_entry_compare));
            if (!entry)
                return std::nullopt;
            color = Color::from_argb32(0xFF000000 | entry->value);
        }
    }

    skip_ws(it, end);
    return std::pair{color, static_cast<int>(it - data)};
}

// -- Paint: COW ref-counting boilerplate --

static Paint::Impl* make_impl() {
    auto* p = new Paint::Impl;
    return p;
}

Paint::Paint() : m_impl(nullptr) {}

Paint::~Paint() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

Paint::Paint(const Paint& other) : m_impl(other.m_impl) {
    if (m_impl)
        m_impl->ref();
}

Paint::Paint(Paint&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

Paint& Paint::operator=(const Paint& other) {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        if (m_impl)
            m_impl->ref();
    }
    return *this;
}

Paint& Paint::operator=(Paint&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Paint::operator bool() const {
    return m_impl != nullptr;
}

// -- Paint factories --

Paint Paint::color(const Color& c) {
    Paint p;
    p.m_impl = make_impl();
    p.m_impl->data = SolidPaintData{
        {std::clamp(c.r, 0.0f, 1.0f),
         std::clamp(c.g, 0.0f, 1.0f),
         std::clamp(c.b, 0.0f, 1.0f),
         std::clamp(c.a, 0.0f, 1.0f)}
    };
    return p;
}

Paint Paint::color(float r, float g, float b, float a) {
    return Paint::color(Color{r, g, b, a});
}

static GradientPaintData make_gradient_data(GradientType type,
                                            SpreadMethod spread,
                                            std::span<const GradientStop> stops,
                                            const Matrix* matrix) {
    GradientPaintData data;
    data.type = type;
    data.spread = spread;
    data.matrix = matrix ? *matrix : Matrix::identity();
    data.stops.resize(stops.size());

    float prev_offset = 0.0f;
    for (size_t i = 0; i < stops.size(); ++i) {
        float offset = std::clamp(stops[i].offset, 0.0f, 1.0f);
        offset = std::max(prev_offset, offset);
        data.stops[i].offset = offset;
        data.stops[i].color = {
            std::clamp(stops[i].color.r, 0.0f, 1.0f),
            std::clamp(stops[i].color.g, 0.0f, 1.0f),
            std::clamp(stops[i].color.b, 0.0f, 1.0f),
            std::clamp(stops[i].color.a, 0.0f, 1.0f)
        };
        prev_offset = offset;
    }

    return data;
}

Paint Paint::linear_gradient(float x1, float y1, float x2, float y2,
                             SpreadMethod spread,
                             std::span<const GradientStop> stops,
                             const Matrix* matrix) {
    Paint p;
    p.m_impl = make_impl();
    auto data = make_gradient_data(GradientType::Linear, spread, stops, matrix);
    data.values[0] = x1;
    data.values[1] = y1;
    data.values[2] = x2;
    data.values[3] = y2;
    p.m_impl->data = std::move(data);
    return p;
}

Paint Paint::radial_gradient(float cx, float cy, float cr,
                             float fx, float fy, float fr,
                             SpreadMethod spread,
                             std::span<const GradientStop> stops,
                             const Matrix* matrix) {
    Paint p;
    p.m_impl = make_impl();
    auto data = make_gradient_data(GradientType::Radial, spread, stops, matrix);
    data.values[0] = cx;
    data.values[1] = cy;
    data.values[2] = cr;
    data.values[3] = fx;
    data.values[4] = fy;
    data.values[5] = fr;
    p.m_impl->data = std::move(data);
    return p;
}

Paint Paint::conic_gradient(float cx, float cy, float start_angle,
                            SpreadMethod spread,
                            std::span<const GradientStop> stops,
                            const Matrix* matrix) {
    Paint p;
    p.m_impl = make_impl();
    auto data = make_gradient_data(GradientType::Conic, spread, stops, matrix);
    data.values[0] = cx;
    data.values[1] = cy;
    data.values[2] = start_angle;
    p.m_impl->data = std::move(data);
    return p;
}

Paint Paint::texture(const Surface& surface, TextureType type,
                     float opacity, const Matrix* matrix) {
    Paint p;
    p.m_impl = make_impl();
    p.m_impl->data = TexturePaintData{
        type,
        std::clamp(opacity, 0.0f, 1.0f),
        matrix ? *matrix : Matrix::identity(),
        surface
    };
    return p;
}

} // namespace plutovg
