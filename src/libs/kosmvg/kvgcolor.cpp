// kvgcolor.cpp — Color, ColorSpace, ColorMatrix implementation
// C++20

#include "kvgprivate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kvg {

// -- ColorSpace lifecycle --

ColorSpace::~ColorSpace() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

ColorSpace::ColorSpace(const ColorSpace& o) : m_impl(o.m_impl) {
    if (m_impl) m_impl->ref();
}

ColorSpace::ColorSpace(ColorSpace&& o) noexcept : m_impl(o.m_impl) {
    o.m_impl = nullptr;
}

ColorSpace& ColorSpace::operator=(const ColorSpace& o) {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        if (m_impl) m_impl->ref();
    }
    return *this;
}

ColorSpace& ColorSpace::operator=(ColorSpace&& o) noexcept {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        o.m_impl = nullptr;
    }
    return *this;
}

ColorSpace::operator bool() const { return m_impl != nullptr; }

int ColorSpace::component_count() const {
    return m_impl ? m_impl->components : 0;
}

// Immortal singletons — zero heap allocations, thread-safe.
// RefCounted::Immortal prevents ref/deref from modifying the count,
// so these are never deleted regardless of copy/move traffic.

static ColorSpace::Impl g_cs_srgb       (RefCounted::Immortal, ColorSpace::Impl::Type::SRGB,          3);
static ColorSpace::Impl g_cs_linear_srgb(RefCounted::Immortal, ColorSpace::Impl::Type::LinearSRGB,    3);
static ColorSpace::Impl g_cs_display_p3 (RefCounted::Immortal, ColorSpace::Impl::Type::DisplayP3,     3);
static ColorSpace::Impl g_cs_ext_linear (RefCounted::Immortal, ColorSpace::Impl::Type::ExtLinearSRGB, 3);
static ColorSpace::Impl g_cs_device_gray(RefCounted::Immortal, ColorSpace::Impl::Type::DeviceGray,    1);

ColorSpace ColorSpace::srgb() {
    ColorSpace cs;
    cs.m_impl = &g_cs_srgb;
    return cs;
}

ColorSpace ColorSpace::linear_srgb() {
    ColorSpace cs;
    cs.m_impl = &g_cs_linear_srgb;
    return cs;
}

ColorSpace ColorSpace::display_p3() {
    ColorSpace cs;
    cs.m_impl = &g_cs_display_p3;
    return cs;
}

ColorSpace ColorSpace::extended_linear_srgb() {
    ColorSpace cs;
    cs.m_impl = &g_cs_ext_linear;
    return cs;
}

ColorSpace ColorSpace::device_gray() {
    ColorSpace cs;
    cs.m_impl = &g_cs_device_gray;
    return cs;
}

// -- Color::from_hsl --

Color Color::from_hsl(float h, float s, float l, float a) {
    // h in [0, 360], s and l in [0, 1]
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    h /= 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    l = std::clamp(l, 0.0f, 1.0f);

    auto hue_to_rgb = [](float p, float q, float t) -> float {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 0.5f)         return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };

    if (s == 0.0f)
        return {l, l, l, a};

    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    float r = hue_to_rgb(p, q, h + 1.0f / 3.0f);
    float g = hue_to_rgb(p, q, h);
    float b = hue_to_rgb(p, q, h - 1.0f / 3.0f);
    return {r, g, b, a};
}

// -- Color::parse (CSS color strings) --

namespace {

struct NamedColor { const char* name; uint32_t argb; };

// CSS named colors (subset — most common + all SVG basic colors)
constexpr NamedColor named_colors[] = {
    {"aliceblue",            0xFFF0F8FF}, {"antiquewhite",         0xFFFAEBD7},
    {"aqua",                 0xFF00FFFF}, {"aquamarine",           0xFF7FFFD4},
    {"azure",                0xFFF0FFFF}, {"beige",                0xFFF5F5DC},
    {"bisque",               0xFFFFE4C4}, {"black",                0xFF000000},
    {"blanchedalmond",       0xFFFFEBCD}, {"blue",                 0xFF0000FF},
    {"blueviolet",           0xFF8A2BE2}, {"brown",                0xFFA52A2A},
    {"burlywood",            0xFFDEB887}, {"cadetblue",            0xFF5F9EA0},
    {"chartreuse",           0xFF7FFF00}, {"chocolate",            0xFFD2691E},
    {"coral",                0xFFFF7F50}, {"cornflowerblue",       0xFF6495ED},
    {"cornsilk",             0xFFFFF8DC}, {"crimson",              0xFFDC143C},
    {"cyan",                 0xFF00FFFF}, {"darkblue",             0xFF00008B},
    {"darkcyan",             0xFF008B8B}, {"darkgoldenrod",        0xFFB8860B},
    {"darkgray",             0xFFA9A9A9}, {"darkgreen",            0xFF006400},
    {"darkgrey",             0xFFA9A9A9}, {"darkkhaki",            0xFFBDB76B},
    {"darkmagenta",          0xFF8B008B}, {"darkolivegreen",       0xFF556B2F},
    {"darkorange",           0xFFFF8C00}, {"darkorchid",           0xFF9932CC},
    {"darkred",              0xFF8B0000}, {"darksalmon",           0xFFE9967A},
    {"darkseagreen",         0xFF8FBC8F}, {"darkslateblue",        0xFF483D8B},
    {"darkslategray",        0xFF2F4F4F}, {"darkslategrey",        0xFF2F4F4F},
    {"darkturquoise",        0xFF00CED1}, {"darkviolet",           0xFF9400D3},
    {"deeppink",             0xFFFF1493}, {"deepskyblue",          0xFF00BFFF},
    {"dimgray",              0xFF696969}, {"dimgrey",              0xFF696969},
    {"dodgerblue",           0xFF1E90FF}, {"firebrick",            0xFFB22222},
    {"floralwhite",          0xFFFFFAF0}, {"forestgreen",          0xFF228B22},
    {"fuchsia",              0xFFFF00FF}, {"gainsboro",            0xFFDCDCDC},
    {"ghostwhite",           0xFFF8F8FF}, {"gold",                 0xFFFFD700},
    {"goldenrod",            0xFFDAA520}, {"gray",                 0xFF808080},
    {"green",                0xFF008000}, {"greenyellow",          0xFFADFF2F},
    {"grey",                 0xFF808080}, {"honeydew",             0xFFF0FFF0},
    {"hotpink",              0xFFFF69B4}, {"indianred",            0xFFCD5C5C},
    {"indigo",               0xFF4B0082}, {"ivory",                0xFFFFFFF0},
    {"khaki",                0xFFF0E68C}, {"lavender",             0xFFE6E6FA},
    {"lavenderblush",        0xFFFFF0F5}, {"lawngreen",            0xFF7CFC00},
    {"lemonchiffon",         0xFFFFFACD}, {"lightblue",            0xFFADD8E6},
    {"lightcoral",           0xFFF08080}, {"lightcyan",            0xFFE0FFFF},
    {"lightgoldenrodyellow", 0xFFFAFAD2}, {"lightgray",            0xFFD3D3D3},
    {"lightgreen",           0xFF90EE90}, {"lightgrey",            0xFFD3D3D3},
    {"lightpink",            0xFFFFB6C1}, {"lightsalmon",          0xFFFFA07A},
    {"lightseagreen",        0xFF20B2AA}, {"lightskyblue",         0xFF87CEFA},
    {"lightslategray",       0xFF778899}, {"lightslategrey",       0xFF778899},
    {"lightsteelblue",       0xFFB0C4DE}, {"lightyellow",          0xFFFFFFE0},
    {"lime",                 0xFF00FF00}, {"limegreen",            0xFF32CD32},
    {"linen",                0xFFFAF0E6}, {"magenta",              0xFFFF00FF},
    {"maroon",               0xFF800000}, {"mediumaquamarine",     0xFF66CDAA},
    {"mediumblue",           0xFF0000CD}, {"mediumorchid",         0xFFBA55D3},
    {"mediumpurple",         0xFF9370DB}, {"mediumseagreen",       0xFF3CB371},
    {"mediumslateblue",      0xFF7B68EE}, {"mediumspringgreen",    0xFF00FA9A},
    {"mediumturquoise",      0xFF48D1CC}, {"mediumvioletred",      0xFFC71585},
    {"midnightblue",         0xFF191970}, {"mintcream",            0xFFF5FFFA},
    {"mistyrose",            0xFFFFE4E1}, {"moccasin",             0xFFFFE4B5},
    {"navajowhite",          0xFFFFDEAD}, {"navy",                 0xFF000080},
    {"oldlace",              0xFFFDF5E6}, {"olive",                0xFF808000},
    {"olivedrab",            0xFF6B8E23}, {"orange",               0xFFFFA500},
    {"orangered",            0xFFFF4500}, {"orchid",               0xFFDA70D6},
    {"palegoldenrod",        0xFFEEE8AA}, {"palegreen",            0xFF98FB98},
    {"paleturquoise",        0xFFAFEEEE}, {"palevioletred",        0xFFDB7093},
    {"papayawhip",           0xFFFFEFD5}, {"peachpuff",            0xFFFFDAB9},
    {"peru",                 0xFFCD853F}, {"pink",                 0xFFFFC0CB},
    {"plum",                 0xFFDDA0DD}, {"powderblue",           0xFFB0E0E6},
    {"purple",               0xFF800080}, {"rebeccapurple",        0xFF663399},
    {"red",                  0xFFFF0000}, {"rosybrown",            0xFFBC8F8F},
    {"royalblue",            0xFF4169E1}, {"saddlebrown",          0xFF8B4513},
    {"salmon",               0xFFFA8072}, {"sandybrown",           0xFFF4A460},
    {"seagreen",             0xFF2E8B57}, {"seashell",             0xFFFFF5EE},
    {"sienna",               0xFFA0522D}, {"silver",               0xFFC0C0C0},
    {"skyblue",              0xFF87CEEB}, {"slateblue",            0xFF6A5ACD},
    {"slategray",            0xFF708090}, {"slategrey",            0xFF708090},
    {"snow",                 0xFFFFFAFA}, {"springgreen",          0xFF00FF7F},
    {"steelblue",            0xFF4682B4}, {"tan",                  0xFFD2B48C},
    {"teal",                 0xFF008080}, {"thistle",              0xFFD8BFD8},
    {"tomato",               0xFFFF6347}, {"turquoise",            0xFF40E0D0},
    {"violet",               0xFFEE82EE}, {"wheat",                0xFFF5DEB3},
    {"white",                0xFFFFFFFF}, {"whitesmoke",           0xFFF5F5F5},
    {"yellow",               0xFFFFFF00}, {"yellowgreen",          0xFF9ACD32},
    {"transparent",          0x00000000},
};

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_hex_color(const char* s, int len, Color& out) {
    auto h = [&](int i) -> int { return hex_val(s[i]); };

    if (len == 3) {
        int r = h(0), g = h(1), b = h(2);
        if (r < 0 || g < 0 || b < 0) return false;
        out = Color::from_rgb8(r * 17, g * 17, b * 17);
        return true;
    }
    if (len == 4) {
        int r = h(0), g = h(1), b = h(2), a = h(3);
        if (r < 0 || g < 0 || b < 0 || a < 0) return false;
        out = Color::from_rgba8(r * 17, g * 17, b * 17, a * 17);
        return true;
    }
    if (len == 6) {
        int r1 = h(0), r2 = h(1), g1 = h(2), g2 = h(3), b1 = h(4), b2 = h(5);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
        out = Color::from_rgb8(r1 * 16 + r2, g1 * 16 + g2, b1 * 16 + b2);
        return true;
    }
    if (len == 8) {
        int r1 = h(0), r2 = h(1), g1 = h(2), g2 = h(3);
        int b1 = h(4), b2 = h(5), a1 = h(6), a2 = h(7);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 ||
            b1 < 0 || b2 < 0 || a1 < 0 || a2 < 0) return false;
        out = Color::from_rgba8(r1 * 16 + r2, g1 * 16 + g2,
                                b1 * 16 + b2, a1 * 16 + a2);
        return true;
    }
    return false;
}

// Parse a CSS numeric value, optionally followed by '%'.
// If percent, value is divided by 100. If for_byte, non-percent value is in [0..255].
bool parse_css_num(const char*& it, const char* end, float& out,
                   bool allow_percent, bool for_byte) {
    skip_ws(it, end);
    float v;
    if (!parse_number(it, end, v)) return false;
    skip_ws(it, end);
    if (allow_percent && it < end && *it == '%') {
        ++it;
        out = v / 100.0f;
    } else if (for_byte) {
        out = v / 255.0f;
    } else {
        out = v;
    }
    return true;
}

bool parse_css_alpha(const char*& it, const char* end, float& out) {
    skip_ws(it, end);
    float v;
    if (!parse_number(it, end, v)) return false;
    skip_ws(it, end);
    if (it < end && *it == '%') {
        ++it;
        out = std::clamp(v / 100.0f, 0.0f, 1.0f);
    } else {
        out = std::clamp(v, 0.0f, 1.0f);
    }
    return true;
}

} // anonymous namespace

std::optional<Color> Color::parse(std::string_view css) {
    const char* it = css.data();
    const char* end = it + css.size();
    skip_ws(it, end);
    if (it >= end) return std::nullopt;

    // #hex
    if (*it == '#') {
        ++it;
        int len = static_cast<int>(end - it);
        while (len > 0 && is_ws(it[len - 1])) --len;
        Color c;
        if (parse_hex_color(it, len, c))
            return c;
        return std::nullopt;
    }

    // Detect function name
    enum class Func { None, RGB, HSL };
    Func fn = Func::None;
    const char* saved = it;
    if (skip_string(it, end, "rgba")) {
        fn = Func::RGB;
    } else {
        it = saved;
        if (skip_string(it, end, "rgb")) {
            fn = Func::RGB;
        }
    }
    if (fn == Func::None) {
        it = saved;
        if (skip_string(it, end, "hsla")) {
            fn = Func::HSL;
        } else {
            it = saved;
            if (skip_string(it, end, "hsl")) {
                fn = Func::HSL;
            }
        }
    }

    if (fn == Func::RGB) {
        skip_ws(it, end);
        if (it >= end || *it != '(') return std::nullopt;
        ++it;
        float r, g, b, a = 1.0f;
        if (!parse_css_num(it, end, r, true, true)) return std::nullopt;
        skip_ws(it, end);
        bool comma = (it < end && *it == ',');
        if (comma) ++it;
        if (!parse_css_num(it, end, g, true, true)) return std::nullopt;
        skip_ws(it, end);
        if (comma && it < end && *it == ',') ++it;
        if (!parse_css_num(it, end, b, true, true)) return std::nullopt;
        skip_ws(it, end);
        if (comma) {
            if (it < end && *it == ',') {
                ++it;
                if (!parse_css_alpha(it, end, a)) return std::nullopt;
            }
        } else if (it < end && *it == '/') {
            ++it;
            if (!parse_css_alpha(it, end, a)) return std::nullopt;
        }
        skip_ws(it, end);
        if (it >= end || *it != ')') return std::nullopt;
        return Color{std::clamp(r, 0.0f, 1.0f),
                     std::clamp(g, 0.0f, 1.0f),
                     std::clamp(b, 0.0f, 1.0f), a};
    }

    if (fn == Func::HSL) {
        skip_ws(it, end);
        if (it >= end || *it != '(') return std::nullopt;
        ++it;
        float h, s, l, a = 1.0f;
        skip_ws(it, end);
        if (!parse_number(it, end, h)) return std::nullopt;
        skip_ws(it, end);
        if (skip_string(it, end, "deg")) { /* already degrees */ }
        else if (skip_string(it, end, "rad")) { h = rad2deg(h); }
        else if (skip_string(it, end, "grad")) { h = h * 0.9f; }
        else if (skip_string(it, end, "turn")) { h = h * 360.0f; }
        skip_ws(it, end);
        bool comma = (it < end && *it == ',');
        if (comma) ++it;
        skip_ws(it, end);
        if (!parse_number(it, end, s)) return std::nullopt;
        skip_ws(it, end);
        if (it < end && *it == '%') { ++it; s /= 100.0f; }
        skip_ws(it, end);
        if (comma && it < end && *it == ',') ++it;
        skip_ws(it, end);
        if (!parse_number(it, end, l)) return std::nullopt;
        skip_ws(it, end);
        if (it < end && *it == '%') { ++it; l /= 100.0f; }
        skip_ws(it, end);
        if (comma) {
            if (it < end && *it == ',') {
                ++it;
                if (!parse_css_alpha(it, end, a)) return std::nullopt;
            }
        } else if (it < end && *it == '/') {
            ++it;
            if (!parse_css_alpha(it, end, a)) return std::nullopt;
        }
        skip_ws(it, end);
        if (it >= end || *it != ')') return std::nullopt;
        return from_hsl(h, std::clamp(s, 0.0f, 1.0f),
                           std::clamp(l, 0.0f, 1.0f), a);
    }

    // Named color (case-insensitive)
    it = css.data();
    skip_ws(it, end);
    while (end > it && is_ws(*(end - 1))) --end;

    auto len = static_cast<size_t>(end - it);
    if (len == 0 || len > 24) return std::nullopt;

    char lower[25];
    for (size_t i = 0; i < len; ++i)
        lower[i] = (it[i] >= 'A' && it[i] <= 'Z') ? static_cast<char>(it[i] + 32) : it[i];
    lower[len] = '\0';

    for (const auto& nc : named_colors) {
        if (std::strcmp(lower, nc.name) == 0)
            return Color::from_argb32(nc.argb);
    }

    return std::nullopt;
}

// -- Color::converted --

Color Color::converted(const ColorSpace& target) const {
    using Type = ColorSpace::Impl::Type;

    const auto* src_impl = color_space_impl(space);
    const auto* dst_impl = color_space_impl(target);

    if (!src_impl || !dst_impl) return *this;
    if (src_impl->type == dst_impl->type)
        return Color(target, components[0], components[1], components[2], components[3]);

    // sRGB gamma decode/encode for float inputs (no uint8 quantization)
    auto gamma_decode = [](float s) -> float {
        if (s <= 0.0f) return 0.0f;
        if (s >= 1.0f) return 1.0f;
        return (s <= 0.04045f) ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    };
    auto gamma_encode = [](float v) -> float {
        if (v <= 0.0f) return 0.0f;
        if (v >= 1.0f) return 1.0f;
        return (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    };

    // Convert source to linear sRGB as intermediate
    float lr, lg, lb;
    switch (src_impl->type) {
    case Type::LinearSRGB:
    case Type::ExtLinearSRGB:
        lr = components[0];
        lg = components[1];
        lb = components[2];
        break;
    case Type::SRGB:
    default:
        lr = gamma_decode(components[0]);
        lg = gamma_decode(components[1]);
        lb = gamma_decode(components[2]);
        break;
    case Type::DisplayP3: {
        float p3r = gamma_decode(components[0]);
        float p3g = gamma_decode(components[1]);
        float p3b = gamma_decode(components[2]);
        // Linear P3 → linear sRGB
        lr =  1.2249401f * p3r - 0.2249402f * p3g + 0.0000000f * p3b;
        lg = -0.0420569f * p3r + 1.0420571f * p3g - 0.0000000f * p3b;
        lb = -0.0196376f * p3r - 0.0786361f * p3g + 1.0982735f * p3b;
        break;
    }
    case Type::DeviceGray:
        lr = lg = lb = components[0];
        break;
    }

    // Convert from linear sRGB to target
    float dr, dg, db;
    switch (dst_impl->type) {
    case Type::LinearSRGB:
    case Type::ExtLinearSRGB:
        dr = lr; dg = lg; db = lb;
        break;
    case Type::SRGB:
    default:
        dr = gamma_encode(lr);
        dg = gamma_encode(lg);
        db = gamma_encode(lb);
        break;
    case Type::DisplayP3: {
        // Linear sRGB → linear P3
        float p3r =  0.8225469f * lr + 0.1774531f * lg + 0.0000000f * lb;
        float p3g =  0.0331942f * lr + 0.9668058f * lg + 0.0000000f * lb;
        float p3b =  0.0170826f * lr + 0.0723974f * lg + 0.9105199f * lb;
        dr = gamma_encode(p3r);
        dg = gamma_encode(p3g);
        db = gamma_encode(p3b);
        break;
    }
    case Type::DeviceGray:
        dr = dg = db = 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
        break;
    }

    return Color(target, dr, dg, db, components[3]);
}

// -- ColorMatrix factories --

ColorMatrix ColorMatrix::grayscale() {
    return saturate(0.0f);
}

ColorMatrix ColorMatrix::sepia() {
    ColorMatrix cm;
    cm.m[ 0] = 0.393f; cm.m[ 1] = 0.769f; cm.m[ 2] = 0.189f; cm.m[ 3] = 0; cm.m[ 4] = 0;
    cm.m[ 5] = 0.349f; cm.m[ 6] = 0.686f; cm.m[ 7] = 0.168f; cm.m[ 8] = 0; cm.m[ 9] = 0;
    cm.m[10] = 0.272f; cm.m[11] = 0.534f; cm.m[12] = 0.131f; cm.m[13] = 0; cm.m[14] = 0;
    cm.m[15] = 0;      cm.m[16] = 0;      cm.m[17] = 0;      cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::saturate(float s) {
    // SVG feColorMatrix saturate
    float sr = 0.2126f;
    float sg = 0.7152f;
    float sb = 0.0722f;

    ColorMatrix cm;
    cm.m[ 0] = sr + (1.0f - sr) * s;
    cm.m[ 1] = sg - sg * s;
    cm.m[ 2] = sb - sb * s;
    cm.m[ 3] = 0; cm.m[ 4] = 0;

    cm.m[ 5] = sr - sr * s;
    cm.m[ 6] = sg + (1.0f - sg) * s;
    cm.m[ 7] = sb - sb * s;
    cm.m[ 8] = 0; cm.m[ 9] = 0;

    cm.m[10] = sr - sr * s;
    cm.m[11] = sg - sg * s;
    cm.m[12] = sb + (1.0f - sb) * s;
    cm.m[13] = 0; cm.m[14] = 0;

    cm.m[15] = 0; cm.m[16] = 0; cm.m[17] = 0; cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::brightness(float amount) {
    // Multiply RGB by amount. amount=1 is identity.
    ColorMatrix cm;
    cm.m[ 0] = amount; cm.m[ 1] = 0;      cm.m[ 2] = 0;      cm.m[ 3] = 0; cm.m[ 4] = 0;
    cm.m[ 5] = 0;      cm.m[ 6] = amount; cm.m[ 7] = 0;      cm.m[ 8] = 0; cm.m[ 9] = 0;
    cm.m[10] = 0;      cm.m[11] = 0;      cm.m[12] = amount; cm.m[13] = 0; cm.m[14] = 0;
    cm.m[15] = 0;      cm.m[16] = 0;      cm.m[17] = 0;      cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::contrast(float amount) {
    // Scale around 0.5. amount=1 is identity.
    float off = 0.5f * (1.0f - amount);
    ColorMatrix cm;
    cm.m[ 0] = amount; cm.m[ 1] = 0;      cm.m[ 2] = 0;      cm.m[ 3] = 0; cm.m[ 4] = off;
    cm.m[ 5] = 0;      cm.m[ 6] = amount; cm.m[ 7] = 0;      cm.m[ 8] = 0; cm.m[ 9] = off;
    cm.m[10] = 0;      cm.m[11] = 0;      cm.m[12] = amount; cm.m[13] = 0; cm.m[14] = off;
    cm.m[15] = 0;      cm.m[16] = 0;      cm.m[17] = 0;      cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::hue_rotate(float radians) {
    // SVG feColorMatrix hueRotate
    float cs = std::cos(radians);
    float sn = std::sin(radians);

    float sr = 0.2126f;
    float sg = 0.7152f;
    float sb = 0.0722f;

    ColorMatrix cm;
    cm.m[ 0] = sr + cs * (1.0f - sr) + sn * (-sr);
    cm.m[ 1] = sg + cs * (-sg)       + sn * (-sg);
    cm.m[ 2] = sb + cs * (-sb)       + sn * (1.0f - sb);
    cm.m[ 3] = 0; cm.m[ 4] = 0;

    cm.m[ 5] = sr + cs * (-sr)       + sn * 0.143f;
    cm.m[ 6] = sg + cs * (1.0f - sg) + sn * 0.140f;
    cm.m[ 7] = sb + cs * (-sb)       + sn * (-0.283f);
    cm.m[ 8] = 0; cm.m[ 9] = 0;

    cm.m[10] = sr + cs * (-sr)       + sn * (-(1.0f - sr));
    cm.m[11] = sg + cs * (-sg)       + sn * sg;
    cm.m[12] = sb + cs * (1.0f - sb) + sn * sb;
    cm.m[13] = 0; cm.m[14] = 0;

    cm.m[15] = 0; cm.m[16] = 0; cm.m[17] = 0; cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

ColorMatrix ColorMatrix::invert() {
    ColorMatrix cm;
    cm.m[ 0] = -1; cm.m[ 1] =  0; cm.m[ 2] =  0; cm.m[ 3] = 0; cm.m[ 4] = 1;
    cm.m[ 5] =  0; cm.m[ 6] = -1; cm.m[ 7] =  0; cm.m[ 8] = 0; cm.m[ 9] = 1;
    cm.m[10] =  0; cm.m[11] =  0; cm.m[12] = -1; cm.m[13] = 0; cm.m[14] = 1;
    cm.m[15] =  0; cm.m[16] =  0; cm.m[17] =  0; cm.m[18] = 1; cm.m[19] = 0;
    return cm;
}

} // namespace kvg
