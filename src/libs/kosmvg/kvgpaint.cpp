// kvgpaint.cpp — Gradient, Pattern, Shading implementation
// C++20

#include "kvgprivate.hpp"

#include <algorithm>
#include <cmath>

namespace kvg {

// ============================================================
//  Gradient
// ============================================================

// -- lifecycle --

Gradient::Gradient() : m_impl(nullptr) {}

Gradient::~Gradient() {
    if (m_impl && m_impl->deref()) delete m_impl;
}

Gradient::Gradient(const Gradient& o) : m_impl(o.m_impl) {
    if (m_impl) m_impl->ref();
}

Gradient::Gradient(Gradient&& o) noexcept : m_impl(o.m_impl) {
    o.m_impl = nullptr;
}

Gradient& Gradient::operator=(const Gradient& o) {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        if (m_impl) m_impl->ref();
    }
    return *this;
}

Gradient& Gradient::operator=(Gradient&& o) noexcept {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        o.m_impl = nullptr;
    }
    return *this;
}

Gradient::operator bool() const { return m_impl != nullptr && !m_impl->stops.empty(); }

// -- factories --

Gradient Gradient::create(const ColorSpace& space,
                          std::span<const Stop> stops,
                          GradientSpread spread) {
    Gradient g;
    auto* impl = new Impl;
    impl->stops.assign(stops.begin(), stops.end());
    impl->spread = spread;
    impl->color_space_ = space;

    // Sort by offset and clamp to [0, 1]
    for (auto& s : impl->stops)
        s.offset = std::clamp(s.offset, 0.0f, 1.0f);
    std::sort(impl->stops.begin(), impl->stops.end(),
              [](const Stop& a, const Stop& b) { return a.offset < b.offset; });

    g.m_impl = impl;
    return g;
}

Gradient Gradient::create(std::span<const Stop> stops, GradientSpread spread) {
    return create(ColorSpace::srgb(), stops, spread);
}

Gradient Gradient::create(const Color& start, const Color& end, GradientSpread spread) {
    Stop stops[2] = {{0.0f, start}, {1.0f, end}};
    return create(std::span<const Stop>(stops, 2), spread);
}

// -- queries --

int Gradient::stop_count() const {
    return m_impl ? static_cast<int>(m_impl->stops.size()) : 0;
}

Gradient::Stop Gradient::stop_at(int index) const {
    if (!m_impl || index < 0 || index >= static_cast<int>(m_impl->stops.size()))
        return {0.0f, Color::black()};
    return m_impl->stops[static_cast<size_t>(index)];
}

GradientSpread Gradient::spread() const {
    return m_impl ? m_impl->spread : GradientSpread::Pad;
}

// -- sample --

Color Gradient::sample(float t) const {
    if (!m_impl || m_impl->stops.empty())
        return Color::black();

    // NaN/Inf guard — treat non-finite as clamped edge.
    if (t != t)  // NaN
        return m_impl->stops.front().color;
    if (!std::isfinite(t))
        t = (t > 0) ? 1.0f : 0.0f;

    const auto& stops = m_impl->stops;

    // Apply spread mode
    switch (m_impl->spread) {
    case GradientSpread::Pad:
        t = std::clamp(t, 0.0f, 1.0f);
        break;
    case GradientSpread::Reflect: {
        t = std::fmod(t, 2.0f);
        if (t < 0.0f) t += 2.0f;
        if (t > 1.0f) t = 2.0f - t;
        break;
    }
    case GradientSpread::Repeat:
        t = t - std::floor(t);
        break;
    }

    if (stops.size() == 1)
        return stops[0].color;

    if (t <= stops.front().offset)
        return stops.front().color;
    if (t >= stops.back().offset)
        return stops.back().color;

    // Binary search for the interval
    size_t lo = 0, hi = stops.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (stops[mid].offset <= t)
            lo = mid;
        else
            hi = mid;
    }

    float range = stops[hi].offset - stops[lo].offset;
    float frac = (range > 0.0f) ? (t - stops[lo].offset) / range : 0.0f;

    // Determine interpolation space from gradient's color_space_.
    auto* cs = color_space_impl(m_impl->color_space_);
    bool linear = cs && (cs->type == ColorSpace::Impl::Type::LinearSRGB ||
                         cs->type == ColorSpace::Impl::Type::ExtLinearSRGB);

    if (linear) {
        const Color& c0 = stops[lo].color;
        const Color& c1 = stops[hi].color;

        using color_space_util::srgb_to_linear_f;
        using color_space_util::linear_to_srgb_f;

        float r0 = srgb_to_linear_f(c0.r()), r1 = srgb_to_linear_f(c1.r());
        float g0 = srgb_to_linear_f(c0.g()), g1 = srgb_to_linear_f(c1.g());
        float b0 = srgb_to_linear_f(c0.b()), b1 = srgb_to_linear_f(c1.b());

        float r = r0 + (r1 - r0) * frac;
        float g = g0 + (g1 - g0) * frac;
        float b = b0 + (b1 - b0) * frac;
        float a = c0.a() + (c1.a() - c0.a()) * frac;

        return Color(linear_to_srgb_f(r), linear_to_srgb_f(g), linear_to_srgb_f(b), a);
    }

    return stops[lo].color.lerp(stops[hi].color, frac);
}

// ============================================================
//  Pattern
// ============================================================

// -- lifecycle --

Pattern::Pattern() : m_impl(nullptr) {}

Pattern::~Pattern() {
    if (m_impl && m_impl->deref()) delete m_impl;
}

Pattern::Pattern(const Pattern& o) : m_impl(o.m_impl) {
    if (m_impl) m_impl->ref();
}

Pattern::Pattern(Pattern&& o) noexcept : m_impl(o.m_impl) {
    o.m_impl = nullptr;
}

Pattern& Pattern::operator=(const Pattern& o) {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        if (m_impl) m_impl->ref();
    }
    return *this;
}

Pattern& Pattern::operator=(Pattern&& o) noexcept {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        o.m_impl = nullptr;
    }
    return *this;
}

Pattern::operator bool() const { return m_impl != nullptr; }

// -- factories --

Pattern Pattern::create_colored(
    Rect bounds, AffineTransform matrix,
    float x_step, float y_step,
    PatternTiling tiling,
    DrawFunc draw, void* info,
    void (*release)(void*)) {
    Pattern p;
    auto* impl = new Impl;
    impl->kind = Impl::Kind::Colored;
    impl->bounds = bounds;
    impl->matrix = matrix;
    impl->x_step = x_step;
    impl->y_step = y_step;
    impl->tiling = tiling;
    impl->draw_func = draw;
    impl->draw_info = info;
    impl->release = release;
    p.m_impl = impl;
    return p;
}

Pattern Pattern::create_from_image(const Image& image, AffineTransform matrix) {
    Pattern p;
    auto* impl = new Impl;
    impl->kind = Impl::Kind::ImageBased;
    impl->matrix = matrix;
    impl->source_image = image;
    impl->bounds = Rect(0, 0, static_cast<float>(image.width()),
                              static_cast<float>(image.height()));
    impl->x_step = impl->bounds.width();
    impl->y_step = impl->bounds.height();
    p.m_impl = impl;
    return p;
}

// ============================================================
//  Shading
// ============================================================

// -- lifecycle --

Shading::Shading() : m_impl(nullptr) {}

Shading::~Shading() {
    if (m_impl && m_impl->deref()) delete m_impl;
}

Shading::Shading(const Shading& o) : m_impl(o.m_impl) {
    if (m_impl) m_impl->ref();
}

Shading::Shading(Shading&& o) noexcept : m_impl(o.m_impl) {
    o.m_impl = nullptr;
}

Shading& Shading::operator=(const Shading& o) {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        if (m_impl) m_impl->ref();
    }
    return *this;
}

Shading& Shading::operator=(Shading&& o) noexcept {
    if (this != &o) {
        if (m_impl && m_impl->deref()) delete m_impl;
        m_impl = o.m_impl;
        o.m_impl = nullptr;
    }
    return *this;
}

Shading::operator bool() const { return m_impl != nullptr && m_impl->eval != nullptr; }

// -- factories --

Shading Shading::create_axial(
    Point start, Point end,
    const ColorSpace& space,
    EvalFunc eval, void* info,
    void (*release)(void*),
    bool extend_start, bool extend_end) {
    Shading s;
    auto* impl = new Impl;
    impl->kind = Impl::Kind::Axial;
    impl->start_point = start;
    impl->end_point = end;
    impl->color_space_ = space;
    impl->eval = eval;
    impl->eval_info = info;
    impl->release = release;
    impl->extend_start = extend_start;
    impl->extend_end = extend_end;
    s.m_impl = impl;
    return s;
}

Shading Shading::create_radial(
    Point start_center, float start_radius,
    Point end_center, float end_radius,
    const ColorSpace& space,
    EvalFunc eval, void* info,
    void (*release)(void*),
    bool extend_start, bool extend_end) {
    Shading s;
    auto* impl = new Impl;
    impl->kind = Impl::Kind::Radial;
    impl->start_point = start_center;
    impl->end_point = end_center;
    impl->start_radius = start_radius;
    impl->end_radius = end_radius;
    impl->color_space_ = space;
    impl->eval = eval;
    impl->eval_info = info;
    impl->release = release;
    impl->extend_start = extend_start;
    impl->extend_end = extend_end;
    s.m_impl = impl;
    return s;
}

} // namespace kvg
