// plutovg-path.cpp - Path implementation
// C++20

#include "plutovg-private.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

namespace plutovg {

// -- COW helpers --

static Path::Impl* make_impl() {
    auto* impl = new Path::Impl;
    return impl;
}

static Path::Impl* detach(Path::Impl*& impl) {
    if (!impl) {
        impl = make_impl();
        return impl;
    }
    if (impl->count() == 1)
        return impl;

    auto* copy = make_impl();
    copy->elements = impl->elements;
    copy->start_point = impl->start_point;
    copy->num_points = impl->num_points;
    copy->num_contours = impl->num_contours;
    copy->num_curves = impl->num_curves;

    if (impl->deref())
        delete impl;
    impl = copy;
    return impl;
}

// -- Constructors / destructor / copy / move --

Path::Path() : m_impl(make_impl()) {}

Path::~Path() {
    if (m_impl && m_impl->deref())
        delete m_impl;
}

Path::Path(const Path& other) : m_impl(other.m_impl) {
    if (m_impl)
        m_impl->ref();
}

Path::Path(Path&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

Path& Path::operator=(const Path& other) {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        if (m_impl)
            m_impl->ref();
    }
    return *this;
}

Path& Path::operator=(Path&& other) noexcept {
    if (this != &other) {
        if (m_impl && m_impl->deref())
            delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Path::operator bool() const {
    return m_impl != nullptr;
}

// -- Internal: add command --

static PathElement* add_command(Path::Impl* impl, PathCommand command, int npoints) {
    const int length = npoints + 1;
    auto& elems = impl->elements;
    elems.resize(elems.size() + length);
    auto* base = elems.data() + elems.size() - length;
    base->header.command = command;
    base->header.length = length;
    impl->num_points += npoints;
    return base + 1;
}

// -- Building --

void Path::move_to(float x, float y) {
    auto* impl = detach(m_impl);
    auto* e = add_command(impl, PathCommand::MoveTo, 1);
    e[0].point = {x, y};
    impl->start_point = {x, y};
    impl->num_contours += 1;
}

void Path::line_to(float x, float y) {
    auto* impl = detach(m_impl);
    if (impl->elements.empty())
        move_to(0, 0);
    auto* e = add_command(impl, PathCommand::LineTo, 1);
    e[0].point = {x, y};
}

void Path::quad_to(float x1, float y1, float x2, float y2) {
    auto [cx, cy] = current_point();
    float cp1x = 2.0f / 3.0f * x1 + 1.0f / 3.0f * cx;
    float cp1y = 2.0f / 3.0f * y1 + 1.0f / 3.0f * cy;
    float cp2x = 2.0f / 3.0f * x1 + 1.0f / 3.0f * x2;
    float cp2y = 2.0f / 3.0f * y1 + 1.0f / 3.0f * y2;
    cubic_to(cp1x, cp1y, cp2x, cp2y, x2, y2);
}

void Path::cubic_to(float x1, float y1, float x2, float y2, float x3, float y3) {
    auto* impl = detach(m_impl);
    if (impl->elements.empty())
        move_to(0, 0);
    auto* e = add_command(impl, PathCommand::CubicTo, 3);
    e[0].point = {x1, y1};
    e[1].point = {x2, y2};
    e[2].point = {x3, y3};
    impl->num_curves += 1;
}

void Path::arc_to(float rx, float ry, float angle, bool large_arc_flag, bool sweep_flag, float x, float y) {
    auto [cur_x, cur_y] = current_point();
    if (rx == 0.0f || ry == 0.0f || (cur_x == x && cur_y == y)) {
        line_to(x, y);
        return;
    }

    if (rx < 0.0f) rx = -rx;
    if (ry < 0.0f) ry = -ry;

    float dx = (cur_x - x) * 0.5f;
    float dy = (cur_y - y) * 0.5f;

    Matrix m = Matrix::rotated(-angle);
    m.map(dx, dy, dx, dy);

    float rxrx = rx * rx;
    float ryry = ry * ry;
    float dxdx = dx * dx;
    float dydy = dy * dy;
    float radius = dxdx / rxrx + dydy / ryry;
    if (radius > 1.0f) {
        rx *= std::sqrt(radius);
        ry *= std::sqrt(radius);
    }

    m = Matrix::scaled(1.0f / rx, 1.0f / ry);
    m.rotate(-angle);

    float x1, y1, x2_, y2_;
    m.map(cur_x, cur_y, x1, y1);
    m.map(x, y, x2_, y2_);

    float dx1 = x2_ - x1;
    float dy1 = y2_ - y1;
    float d = dx1 * dx1 + dy1 * dy1;
    float scale_sq = 1.0f / d - 0.25f;
    if (scale_sq < 0.0f) scale_sq = 0.0f;
    float scale = std::sqrt(scale_sq);
    if (sweep_flag == large_arc_flag)
        scale = -scale;
    dx1 *= scale;
    dy1 *= scale;

    float cx1 = 0.5f * (x1 + x2_) - dy1;
    float cy1 = 0.5f * (y1 + y2_) + dx1;

    float th1 = std::atan2(y1 - cy1, x1 - cx1);
    float th2 = std::atan2(y2_ - cy1, x2_ - cx1);
    float th_arc = th2 - th1;
    if (th_arc < 0.0f && sweep_flag)
        th_arc += two_pi;
    else if (th_arc > 0.0f && !sweep_flag)
        th_arc -= two_pi;

    m = Matrix::rotated(angle);
    m.scale(rx, ry);

    int segments = static_cast<int>(std::ceil(std::abs(th_arc / (half_pi + 0.001f))));
    for (int i = 0; i < segments; ++i) {
        float th_start = th1 + i * th_arc / segments;
        float th_end = th1 + (i + 1) * th_arc / segments;
        float t = (8.0f / 6.0f) * std::tan(0.25f * (th_end - th_start));

        float x3 = std::cos(th_end) + cx1;
        float y3 = std::sin(th_end) + cy1;

        float cp2x = x3 + t * std::sin(th_end);
        float cp2y = y3 - t * std::cos(th_end);

        float cp1x = std::cos(th_start) - t * std::sin(th_start);
        float cp1y = std::sin(th_start) + t * std::cos(th_start);

        cp1x += cx1;
        cp1y += cy1;

        m.map(cp1x, cp1y, cp1x, cp1y);
        m.map(cp2x, cp2y, cp2x, cp2y);
        m.map(x3, y3, x3, y3);

        cubic_to(cp1x, cp1y, cp2x, cp2y, x3, y3);
    }
}

void Path::close() {
    if (!m_impl || m_impl->elements.empty())
        return;
    auto* impl = detach(m_impl);
    auto* e = add_command(impl, PathCommand::Close, 1);
    e[0].point = impl->start_point;
}

// -- Shapes --

void Path::add_rect(float x, float y, float w, float h) {
    reserve(6 * 2);
    move_to(x, y);
    line_to(x + w, y);
    line_to(x + w, y + h);
    line_to(x, y + h);
    line_to(x, y);
    close();
}

void Path::add_round_rect(float x, float y, float w, float h, float rx, float ry) {
    rx = std::min(rx, w * 0.5f);
    ry = std::min(ry, h * 0.5f);
    if (rx == 0.0f && ry == 0.0f) {
        add_rect(x, y, w, h);
        return;
    }

    float right = x + w;
    float bottom = y + h;

    float cpx = rx * kappa;
    float cpy = ry * kappa;

    reserve(6 * 2 + 4 * 4);
    move_to(x, y + ry);
    cubic_to(x, y + ry - cpy, x + rx - cpx, y, x + rx, y);
    line_to(right - rx, y);
    cubic_to(right - rx + cpx, y, right, y + ry - cpy, right, y + ry);
    line_to(right, bottom - ry);
    cubic_to(right, bottom - ry + cpy, right - rx + cpx, bottom, right - rx, bottom);
    line_to(x + rx, bottom);
    cubic_to(x + rx - cpx, bottom, x, bottom - ry + cpy, x, bottom - ry);
    line_to(x, y + ry);
    close();
}

void Path::add_round_rect(float x, float y, float w, float h, const CornerRadii& radii) {
    if (radii.is_zero()) {
        add_rect(x, y, w, h);
        return;
    }
    if (radii.is_uniform()) {
        add_round_rect(x, y, w, h, radii.top_left, radii.top_left);
        return;
    }

    float half_w = w * 0.5f;
    float half_h = h * 0.5f;
    float tl = std::min(radii.top_left, std::min(half_w, half_h));
    float tr = std::min(radii.top_right, std::min(half_w, half_h));
    float br = std::min(radii.bottom_right, std::min(half_w, half_h));
    float bl = std::min(radii.bottom_left, std::min(half_w, half_h));

    float right = x + w;
    float bottom = y + h;

    reserve(6 * 2 + 4 * 4);
    move_to(x, y + tl);
    if (tl > 0) cubic_to(x, y + tl - tl * kappa, x + tl - tl * kappa, y, x + tl, y);
    line_to(right - tr, y);
    if (tr > 0) cubic_to(right - tr + tr * kappa, y, right, y + tr - tr * kappa, right, y + tr);
    line_to(right, bottom - br);
    if (br > 0) cubic_to(right, bottom - br + br * kappa, right - br + br * kappa, bottom, right - br, bottom);
    line_to(x + bl, bottom);
    if (bl > 0) cubic_to(x + bl - bl * kappa, bottom, x, bottom - bl + bl * kappa, x, bottom - bl);
    line_to(x, y + tl);
    close();
}

void Path::add_ellipse(float cx, float cy, float rx, float ry) {
    float left = cx - rx;
    float top = cy - ry;
    float right = cx + rx;
    float bottom = cy + ry;

    float cpx = rx * kappa;
    float cpy = ry * kappa;

    reserve(2 * 2 + 4 * 4);
    move_to(cx, top);
    cubic_to(cx + cpx, top, right, cy - cpy, right, cy);
    cubic_to(right, cy + cpy, cx + cpx, bottom, cx, bottom);
    cubic_to(cx - cpx, bottom, left, cy + cpy, left, cy);
    cubic_to(left, cy - cpy, cx - cpx, top, cx, top);
    close();
}

void Path::add_circle(float cx, float cy, float r) {
    add_ellipse(cx, cy, r, r);
}

void Path::add_arc(float cx, float cy, float r, float a0, float a1, bool ccw) {
    float da = a1 - a0;
    if (std::abs(da) > two_pi) {
        da = two_pi;
    } else if (da != 0.0f && ccw != (da < 0.0f)) {
        da += two_pi * (ccw ? -1 : 1);
    }

    int seg_n = static_cast<int>(std::ceil(std::abs(da) / half_pi));
    if (seg_n == 0)
        return;

    float a = a0;
    float ax = cx + std::cos(a) * r;
    float ay = cy + std::sin(a) * r;

    float seg_a = da / seg_n;
    float d = (seg_a / half_pi) * kappa * r;
    float ddx = -std::sin(a) * d;
    float ddy = std::cos(a) * d;

    reserve(2 + 4 * seg_n);
    if (!m_impl || m_impl->elements.empty()) {
        move_to(ax, ay);
    } else {
        line_to(ax, ay);
    }

    for (int i = 0; i < seg_n; ++i) {
        float cp1x = ax + ddx;
        float cp1y = ay + ddy;

        a += seg_a;
        ax = cx + std::cos(a) * r;
        ay = cy + std::sin(a) * r;

        ddx = -std::sin(a) * d;
        ddy = std::cos(a) * d;

        float cp2x = ax - ddx;
        float cp2y = ay - ddy;

        cubic_to(cp1x, cp1y, cp2x, cp2y, ax, ay);
    }
}

void Path::add_path(const Path& source, const Matrix* matrix) {
    if (!source.m_impl)
        return;
    auto* impl = detach(m_impl);

    if (!matrix) {
        impl->elements.insert(impl->elements.end(),
            source.m_impl->elements.begin(), source.m_impl->elements.end());
        impl->start_point = source.m_impl->start_point;
        impl->num_points += source.m_impl->num_points;
        impl->num_contours += source.m_impl->num_contours;
        impl->num_curves += source.m_impl->num_curves;
        return;
    }

    reserve(static_cast<int>(source.m_impl->elements.size()));
    for (const auto& seg : source) {
        Point pts[3];
        switch (seg.command) {
        case PathCommand::MoveTo:
            pts[0] = matrix->map(seg.points[0]);
            move_to(pts[0].x, pts[0].y);
            break;
        case PathCommand::LineTo:
            pts[0] = matrix->map(seg.points[0]);
            line_to(pts[0].x, pts[0].y);
            break;
        case PathCommand::CubicTo:
            matrix->map_points(seg.points, pts, 3);
            cubic_to(pts[0].x, pts[0].y, pts[1].x, pts[1].y, pts[2].x, pts[2].y);
            break;
        case PathCommand::Close:
            close();
            break;
        }
    }
}

// -- Transformation --

void Path::transform(const Matrix& matrix) {
    auto* impl = detach(m_impl);
    auto* elems = impl->elements.data();
    int size = static_cast<int>(impl->elements.size());
    for (int i = 0; i < size; i += elems[i].header.length) {
        int npts = elems[i].header.length - 1;
        for (int j = 1; j <= npts; ++j) {
            elems[i + j].point = matrix.map(elems[i + j].point);
        }
    }
}

// -- Queries --

Point Path::current_point() const {
    if (!m_impl || m_impl->num_points == 0)
        return {};
    return m_impl->elements.back().point;
}

int Path::element_count() const {
    return m_impl ? static_cast<int>(m_impl->elements.size()) : 0;
}

const PathElement* Path::elements() const {
    return m_impl ? m_impl->elements.data() : nullptr;
}

void Path::reserve(int count) {
    auto* impl = detach(m_impl);
    impl->elements.reserve(impl->elements.size() + count);
}

void Path::reset() {
    auto* impl = detach(m_impl);
    impl->elements.clear();
    impl->start_point = {};
    impl->num_points = 0;
    impl->num_contours = 0;
    impl->num_curves = 0;
}

// -- Clone --

Path Path::clone() const {
    Path result;
    if (m_impl) {
        result.m_impl->elements = m_impl->elements;
        result.m_impl->start_point = m_impl->start_point;
        result.m_impl->num_points = m_impl->num_points;
        result.m_impl->num_contours = m_impl->num_contours;
        result.m_impl->num_curves = m_impl->num_curves;
    }
    return result;
}

Path Path::clone_flatten() const {
    Path result;
    if (!m_impl)
        return result;
    result.reserve(static_cast<int>(m_impl->elements.size()) + m_impl->num_curves * 32);
    traverse_flatten([&](PathCommand cmd, const Point* pts, [[maybe_unused]] int npts) {
        switch (cmd) {
        case PathCommand::MoveTo:  result.move_to(pts[0].x, pts[0].y); break;
        case PathCommand::LineTo:  result.line_to(pts[0].x, pts[0].y); break;
        case PathCommand::CubicTo: result.cubic_to(pts[0].x, pts[0].y, pts[1].x, pts[1].y, pts[2].x, pts[2].y); break;
        case PathCommand::Close:   result.close(); break;
        }
    });
    return result;
}

Path Path::clone_dashed(float offset, std::span<const float> dashes) const {
    Path result;
    if (!m_impl)
        return result;
    result.reserve(static_cast<int>(m_impl->elements.size()) + m_impl->num_curves * 32);
    traverse_dashed(offset, dashes, [&](PathCommand cmd, const Point* pts, [[maybe_unused]] int npts) {
        switch (cmd) {
        case PathCommand::MoveTo:  result.move_to(pts[0].x, pts[0].y); break;
        case PathCommand::LineTo:  result.line_to(pts[0].x, pts[0].y); break;
        case PathCommand::CubicTo: result.cubic_to(pts[0].x, pts[0].y, pts[1].x, pts[1].y, pts[2].x, pts[2].y); break;
        case PathCommand::Close:   result.close(); break;
        }
    });
    return result;
}

// -- from_raw --

Path Path::from_raw(const PathCommand* cmds, int cmd_count,
                    const Point* pts, int pt_count) {
    Path result;
    int pt_idx = 0;
    for (int i = 0; i < cmd_count; ++i) {
        switch (cmds[i]) {
        case PathCommand::MoveTo:
            assert(pt_idx < pt_count);
            result.move_to(pts[pt_idx].x, pts[pt_idx].y);
            pt_idx += 1;
            break;
        case PathCommand::LineTo:
            assert(pt_idx < pt_count);
            result.line_to(pts[pt_idx].x, pts[pt_idx].y);
            pt_idx += 1;
            break;
        case PathCommand::CubicTo:
            assert(pt_idx + 2 < pt_count);
            result.cubic_to(pts[pt_idx].x, pts[pt_idx].y,
                            pts[pt_idx + 1].x, pts[pt_idx + 1].y,
                            pts[pt_idx + 2].x, pts[pt_idx + 2].y);
            pt_idx += 3;
            break;
        case PathCommand::Close:
            result.close();
            break;
        }
    }
    return result;
}

// -- Traversal --

void Path::traverse_raw(TraverseFunc func, void* closure) const {
    if (!m_impl)
        return;
    const auto* elems = m_impl->elements.data();
    int size = static_cast<int>(m_impl->elements.size());
    int index = 0;

    while (index < size) {
        auto cmd = elems[index].header.command;
        int len = elems[index].header.length;
        const Point* pts = &elems[index + 1].point;
        int npts = len - 1;
        func(closure, cmd, pts, npts);
        index += len;
    }
}

// -- Bezier splitting for flattening --

namespace {

struct Bezier {
    float x1, y1, x2, y2, x3, y3, x4, y4;
};

inline void split_bezier(const Bezier& b, Bezier& first, Bezier& second) {
    float c = (b.x2 + b.x3) * 0.5f;
    first.x2 = (b.x1 + b.x2) * 0.5f;
    second.x3 = (b.x3 + b.x4) * 0.5f;
    first.x1 = b.x1;
    second.x4 = b.x4;
    first.x3 = (first.x2 + c) * 0.5f;
    second.x2 = (second.x3 + c) * 0.5f;
    first.x4 = second.x1 = (first.x3 + second.x2) * 0.5f;

    c = (b.y2 + b.y3) * 0.5f;
    first.y2 = (b.y1 + b.y2) * 0.5f;
    second.y3 = (b.y3 + b.y4) * 0.5f;
    first.y1 = b.y1;
    second.y4 = b.y4;
    first.y3 = (first.y2 + c) * 0.5f;
    second.y2 = (second.y3 + c) * 0.5f;
    first.y4 = second.y1 = (first.y3 + second.y2) * 0.5f;
}

} // namespace

void Path::traverse_flatten_raw(TraverseFunc func, void* closure) const {
    if (!m_impl)
        return;
    if (m_impl->num_curves == 0) {
        traverse_raw(func, closure);
        return;
    }

    constexpr float threshold = 0.25f;

    const auto* elems = m_impl->elements.data();
    int size = static_cast<int>(m_impl->elements.size());
    int index = 0;

    Bezier beziers[32];
    Point current_point = {};

    while (index < size) {
        auto cmd = elems[index].header.command;
        int len = elems[index].header.length;
        const Point* pts = &elems[index + 1].point;

        switch (cmd) {
        case PathCommand::MoveTo:
        case PathCommand::LineTo:
        case PathCommand::Close:
            func(closure, cmd, pts, 1);
            current_point = pts[0];
            break;
        case PathCommand::CubicTo: {
            beziers[0] = {
                current_point.x, current_point.y,
                pts[0].x, pts[0].y,
                pts[1].x, pts[1].y,
                pts[2].x, pts[2].y
            };
            Bezier* b = beziers;
            while (b >= beziers) {
                float y4y1 = b->y4 - b->y1;
                float x4x1 = b->x4 - b->x1;
                float l = std::abs(x4x1) + std::abs(y4y1);
                float d;
                if (l > 1.0f) {
                    d = std::abs(x4x1 * (b->y1 - b->y2) - y4y1 * (b->x1 - b->x2))
                      + std::abs(x4x1 * (b->y1 - b->y3) - y4y1 * (b->x1 - b->x3));
                } else {
                    d = std::abs(b->x1 - b->x2) + std::abs(b->y1 - b->y2)
                      + std::abs(b->x1 - b->x3) + std::abs(b->y1 - b->y3);
                    l = 1.0f;
                }

                if (d < threshold * l || b == beziers + 31) {
                    Point p = {b->x4, b->y4};
                    func(closure, PathCommand::LineTo, &p, 1);
                    --b;
                } else {
                    split_bezier(*b, b[1], *b);
                    ++b;
                }
            }
            current_point = pts[2];
            break;
        }
        }
        index += len;
    }
}

// -- Dashed traversal --

namespace {

struct Dasher {
    const float* dashes;
    int ndashes;
    float start_phase;
    float phase;
    int start_index;
    int index;
    bool start_toggle;
    bool toggle;
    Point current_point;
    Path::TraverseFunc func;
    void* closure;
};

void dash_traverse_func(void* ctx, PathCommand command, const Point* points, int npoints) {
    auto* dasher = static_cast<Dasher*>(ctx);
    if (command == PathCommand::MoveTo) {
        if (dasher->start_toggle)
            dasher->func(dasher->closure, PathCommand::MoveTo, points, npoints);
        dasher->current_point = points[0];
        dasher->phase = dasher->start_phase;
        dasher->index = dasher->start_index;
        dasher->toggle = dasher->start_toggle;
        return;
    }

    assert(command == PathCommand::LineTo || command == PathCommand::Close);
    Point p0 = dasher->current_point;
    Point p1 = points[0];
    float dx = p1.x - p0.x;
    float dy = p1.y - p0.y;
    float dist0 = std::sqrt(dx * dx + dy * dy);
    float dist1 = 0.0f;

    while (dist0 - dist1 > dasher->dashes[dasher->index % dasher->ndashes] - dasher->phase) {
        dist1 += dasher->dashes[dasher->index % dasher->ndashes] - dasher->phase;
        float a = dist1 / dist0;
        Point p = {p0.x + a * dx, p0.y + a * dy};
        if (dasher->toggle) {
            dasher->func(dasher->closure, PathCommand::LineTo, &p, 1);
        } else {
            dasher->func(dasher->closure, PathCommand::MoveTo, &p, 1);
        }
        dasher->phase = 0.0f;
        dasher->toggle = !dasher->toggle;
        dasher->index++;
    }

    if (dasher->toggle) {
        dasher->func(dasher->closure, PathCommand::LineTo, &p1, 1);
    }

    dasher->phase += dist0 - dist1;
    dasher->current_point = p1;
}

} // namespace

void Path::traverse_dashed_raw(float offset, const float* dashes, int ndashes,
                               TraverseFunc func, void* closure) const {
    float dash_sum = 0.0f;
    for (int i = 0; i < ndashes; ++i)
        dash_sum += dashes[i];
    if (ndashes % 2 == 1)
        dash_sum *= 2.0f;
    if (dash_sum <= 0.0f) {
        traverse_raw(func, closure);
        return;
    }

    Dasher dasher{};
    dasher.dashes = dashes;
    dasher.ndashes = ndashes;
    dasher.start_phase = std::fmod(offset, dash_sum);
    if (dasher.start_phase < 0.0f)
        dasher.start_phase += dash_sum;
    dasher.start_index = 0;
    dasher.start_toggle = true;

    while (dasher.start_phase > 0.0f
           && dasher.start_phase >= dashes[dasher.start_index % ndashes]) {
        dasher.start_phase -= dashes[dasher.start_index % ndashes];
        dasher.start_toggle = !dasher.start_toggle;
        dasher.start_index++;
    }

    dasher.phase = dasher.start_phase;
    dasher.index = dasher.start_index;
    dasher.toggle = dasher.start_toggle;
    dasher.current_point = {};
    dasher.func = func;
    dasher.closure = closure;
    traverse_flatten_raw(dash_traverse_func, &dasher);
}

// -- Extents --

namespace {

struct ExtentsCalculator {
    Point current_point;
    bool is_first_point = true;
    float length = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
};

void extents_traverse_func(void* ctx, PathCommand command, const Point* points, int npoints) {
    auto* calc = static_cast<ExtentsCalculator*>(ctx);
    if (calc->is_first_point) {
        assert(command == PathCommand::MoveTo);
        calc->is_first_point = false;
        calc->current_point = points[0];
        calc->x1 = calc->x2 = points[0].x;
        calc->y1 = calc->y2 = points[0].y;
        calc->length = 0.0f;
        return;
    }

    for (int i = 0; i < npoints; ++i) {
        calc->x1 = std::min(calc->x1, points[i].x);
        calc->y1 = std::min(calc->y1, points[i].y);
        calc->x2 = std::max(calc->x2, points[i].x);
        calc->y2 = std::max(calc->y2, points[i].y);
        if (command != PathCommand::MoveTo)
            calc->length += std::hypot(points[i].x - calc->current_point.x,
                                       points[i].y - calc->current_point.y);
        calc->current_point = points[i];
    }
}

} // namespace

float Path::extents(Rect& rect, bool tight) const {
    ExtentsCalculator calc;
    if (tight) {
        traverse_flatten_raw(extents_traverse_func, &calc);
    } else {
        traverse_raw(extents_traverse_func, &calc);
    }

    rect.x = calc.x1;
    rect.y = calc.y1;
    rect.w = calc.x2 - calc.x1;
    rect.h = calc.y2 - calc.y1;
    return calc.length;
}

float Path::length() const {
    Rect r;
    return extents(r, true);
}

// -- trimmed --

Path Path::trimmed(float begin_t, float end_t) const {
    if (!m_impl || begin_t >= end_t)
        return {};

    begin_t = std::clamp(begin_t, 0.0f, 1.0f);
    end_t = std::clamp(end_t, 0.0f, 1.0f);

    float total_len = length();
    if (total_len <= 0.0f)
        return {};

    float start_dist = begin_t * total_len;
    float end_dist = end_t * total_len;

    Path result;
    float accumulated = 0.0f;
    bool started = false;
    Point prev = {};

    traverse_flatten([&](PathCommand cmd, const Point* pts, [[maybe_unused]] int npts) {
        if (cmd == PathCommand::MoveTo) {
            prev = pts[0];
            return;
        }

        Point p = pts[0];
        float seg_len = std::hypot(p.x - prev.x, p.y - prev.y);
        float seg_start = accumulated;
        float seg_end = accumulated + seg_len;

        if (seg_end > start_dist && seg_start < end_dist) {
            float t0 = (seg_len > 0.0f) ? std::clamp((start_dist - seg_start) / seg_len, 0.0f, 1.0f) : 0.0f;
            float t1 = (seg_len > 0.0f) ? std::clamp((end_dist - seg_start) / seg_len, 0.0f, 1.0f) : 1.0f;

            float x0 = prev.x + t0 * (p.x - prev.x);
            float y0 = prev.y + t0 * (p.y - prev.y);
            float x1 = prev.x + t1 * (p.x - prev.x);
            float y1 = prev.y + t1 * (p.y - prev.y);

            if (!started) {
                result.move_to(x0, y0);
                started = true;
            }
            result.line_to(x1, y1);
        }

        accumulated = seg_end;
        prev = p;
    });

    return result;
}

// -- Parse SVG path data --

namespace {

inline bool parse_arc_flag(const char*& it, const char* end, bool& flag) {
    if (skip_delim(it, end, '0'))
        flag = false;
    else if (skip_delim(it, end, '1'))
        flag = true;
    else
        return false;
    skip_ws_or_comma(it, end);
    return true;
}

inline bool parse_path_coordinates(const char*& it, const char* end,
                                   float* values, int offset, int count) {
    for (int i = 0; i < count; ++i) {
        if (!parse_number(it, end, values[offset + i]))
            return false;
        skip_ws_or_comma(it, end);
    }
    return true;
}

} // namespace

bool Path::parse(const char* data, int length) {
    if (length == -1)
        length = static_cast<int>(std::strlen(data));
    const char* it = data;
    const char* end = it + length;

    float values[6];
    bool flags[2];

    float start_x = 0, start_y = 0;
    float current_x = 0, current_y = 0;
    float last_control_x = 0, last_control_y = 0;

    char command = 0;
    char last_command = 0;

    skip_ws(it, end);
    while (it < end) {
        if (is_alpha(*it)) {
            command = *it++;
            skip_ws(it, end);
        }

        if (!last_command && !(command == 'M' || command == 'm'))
            return false;

        if (command == 'M' || command == 'm') {
            if (!parse_path_coordinates(it, end, values, 0, 2))
                return false;
            if (command == 'm') {
                values[0] += current_x;
                values[1] += current_y;
            }
            move_to(values[0], values[1]);
            current_x = start_x = values[0];
            current_y = start_y = values[1];
            command = (command == 'm') ? 'l' : 'L';
        } else if (command == 'L' || command == 'l') {
            if (!parse_path_coordinates(it, end, values, 0, 2))
                return false;
            if (command == 'l') {
                values[0] += current_x;
                values[1] += current_y;
            }
            line_to(values[0], values[1]);
            current_x = values[0];
            current_y = values[1];
        } else if (command == 'H' || command == 'h') {
            if (!parse_path_coordinates(it, end, values, 0, 1))
                return false;
            if (command == 'h')
                values[0] += current_x;
            line_to(values[0], current_y);
            current_x = values[0];
        } else if (command == 'V' || command == 'v') {
            if (!parse_path_coordinates(it, end, values, 1, 1))
                return false;
            if (command == 'v')
                values[1] += current_y;
            line_to(current_x, values[1]);
            current_y = values[1];
        } else if (command == 'Q' || command == 'q') {
            if (!parse_path_coordinates(it, end, values, 0, 4))
                return false;
            if (command == 'q') {
                values[0] += current_x;
                values[1] += current_y;
                values[2] += current_x;
                values[3] += current_y;
            }
            quad_to(values[0], values[1], values[2], values[3]);
            last_control_x = values[0];
            last_control_y = values[1];
            current_x = values[2];
            current_y = values[3];
        } else if (command == 'C' || command == 'c') {
            if (!parse_path_coordinates(it, end, values, 0, 6))
                return false;
            if (command == 'c') {
                values[0] += current_x; values[1] += current_y;
                values[2] += current_x; values[3] += current_y;
                values[4] += current_x; values[5] += current_y;
            }
            cubic_to(values[0], values[1], values[2], values[3], values[4], values[5]);
            last_control_x = values[2];
            last_control_y = values[3];
            current_x = values[4];
            current_y = values[5];
        } else if (command == 'T' || command == 't') {
            if (last_command != 'Q' && last_command != 'q'
                && last_command != 'T' && last_command != 't') {
                values[0] = current_x;
                values[1] = current_y;
            } else {
                values[0] = 2 * current_x - last_control_x;
                values[1] = 2 * current_y - last_control_y;
            }
            if (!parse_path_coordinates(it, end, values, 2, 2))
                return false;
            if (command == 't') {
                values[2] += current_x;
                values[3] += current_y;
            }
            quad_to(values[0], values[1], values[2], values[3]);
            last_control_x = values[0];
            last_control_y = values[1];
            current_x = values[2];
            current_y = values[3];
        } else if (command == 'S' || command == 's') {
            if (last_command != 'C' && last_command != 'c'
                && last_command != 'S' && last_command != 's') {
                values[0] = current_x;
                values[1] = current_y;
            } else {
                values[0] = 2 * current_x - last_control_x;
                values[1] = 2 * current_y - last_control_y;
            }
            if (!parse_path_coordinates(it, end, values, 2, 4))
                return false;
            if (command == 's') {
                values[2] += current_x; values[3] += current_y;
                values[4] += current_x; values[5] += current_y;
            }
            cubic_to(values[0], values[1], values[2], values[3], values[4], values[5]);
            last_control_x = values[2];
            last_control_y = values[3];
            current_x = values[4];
            current_y = values[5];
        } else if (command == 'A' || command == 'a') {
            if (!parse_path_coordinates(it, end, values, 0, 3)
                || !parse_arc_flag(it, end, flags[0])
                || !parse_arc_flag(it, end, flags[1])
                || !parse_path_coordinates(it, end, values, 3, 2)) {
                return false;
            }
            if (command == 'a') {
                values[3] += current_x;
                values[4] += current_y;
            }
            arc_to(values[0], values[1], deg2rad(values[2]), flags[0], flags[1], values[3], values[4]);
            current_x = values[3];
            current_y = values[4];
        } else if (command == 'Z' || command == 'z') {
            if (last_command == 'Z' || last_command == 'z')
                return false;
            close();
            current_x = start_x;
            current_y = start_y;
        } else {
            return false;
        }

        last_command = command;
    }

    return true;
}

// -- Iterator --

PathSegment Path::Iterator::operator*() const {
    const auto& hdr = m_elements[m_index].header;
    return {
        hdr.command,
        &m_elements[m_index + 1].point,
        hdr.length - 1
    };
}

Path::Iterator& Path::Iterator::operator++() {
    m_index += m_elements[m_index].header.length;
    return *this;
}

Path::Iterator Path::begin() const {
    if (!m_impl || m_impl->elements.empty())
        return {};
    return {m_impl->elements.data(), static_cast<int>(m_impl->elements.size()), 0};
}

Path::Iterator Path::end() const {
    if (!m_impl || m_impl->elements.empty())
        return {};
    int sz = static_cast<int>(m_impl->elements.size());
    return {m_impl->elements.data(), sz, sz};
}

} // namespace plutovg
