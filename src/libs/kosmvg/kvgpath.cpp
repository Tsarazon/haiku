// kvgpath.cpp — Path, Path::Builder, Path::Iterator implementation
// C++20

#include "kvgprivate.hpp"
#include "polyclip.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace kvg {

// -- Helpers --

namespace {

Path::Impl* alloc_impl() {
    auto* p = new (std::nothrow) Path::Impl;
    if (!p)
        detail::set_last_error(Error::OutOfMemory);
    return p;
}

void retain(Path::Impl* p) {
    if (p) p->ref();
}

void release(Path::Impl* p) {
    if (p && p->deref())
        delete p;
}

// COW: ensure the impl is uniquely owned before mutation.
Path::Impl* cow(Path::Impl*& p) {
    if (!p) {
        p = alloc_impl();
        return p;
    }
    if (p->count() == 1) {
        // Materialize borrowed data before mutation.
        if (p->is_borrowed())
            p->copy_from(p->commands, p->points);
        return p;
    }
    auto* copy = alloc_impl();
    if (!copy) return nullptr;
    copy->copy_from(p->commands, p->points);
    copy->num_contours = p->num_contours;
    copy->num_curves   = p->num_curves;
    copy->start_point  = p->start_point;
    release(p);
    p = copy;
    return copy;
}

// Bounding-box expansion.
struct BBoxAccum {
    float x0 =  std::numeric_limits<float>::max();
    float y0 =  std::numeric_limits<float>::max();
    float x1 = -std::numeric_limits<float>::max();
    float y1 = -std::numeric_limits<float>::max();

    void add(Point p) {
        if (p.x < x0) x0 = p.x;
        if (p.y < y0) y0 = p.y;
        if (p.x > x1) x1 = p.x;
        if (p.y > y1) y1 = p.y;
    }

    void add_cubic(Point p0, Point p1, Point p2, Point p3) {
        add(p0);
        add(p3);
        // Extremes at t where derivative = 0 for each axis.
        auto solve = [&](float a, float b, float c, float d, bool is_x) {
            // Bezier derivative: 3(b-a)t^2 + 6(c-2b+a)t + 3(b-a)... simplify:
            float A = -a + 3*b - 3*c + d;
            float B = 2*a - 4*b + 2*c;
            float C = b - a;
            if (std::abs(A) < 1e-12f) {
                if (std::abs(B) > 1e-12f) {
                    float t = -C / B;
                    if (t > 0 && t < 1) {
                        float v = eval_cubic(a, b, c, d, t);
                        if (is_x) { x0 = std::min(x0, v); x1 = std::max(x1, v); }
                        else       { y0 = std::min(y0, v); y1 = std::max(y1, v); }
                    }
                }
                return;
            }
            float disc = B*B - 4*A*C;
            if (disc < 0) return;
            float sq = std::sqrt(disc);
            for (float t : {(-B + sq) / (2*A), (-B - sq) / (2*A)}) {
                if (t > 0 && t < 1) {
                    float v = eval_cubic(a, b, c, d, t);
                    if (is_x) { x0 = std::min(x0, v); x1 = std::max(x1, v); }
                    else       { y0 = std::min(y0, v); y1 = std::max(y1, v); }
                }
            }
        };
        solve(p0.x, p1.x, p2.x, p3.x, true);
        solve(p0.y, p1.y, p2.y, p3.y, false);
    }

    void add_quad(Point p0, Point p1, Point p2) {
        add(p0);
        add(p2);
        // Derivative zero: t = (p0 - p1) / (p0 - 2*p1 + p2)
        for (int i = 0; i < 2; i++) {
            float a = (i == 0) ? p0.x : p0.y;
            float b = (i == 0) ? p1.x : p1.y;
            float c = (i == 0) ? p2.x : p2.y;
            float denom = a - 2*b + c;
            if (std::abs(denom) < 1e-12f) continue;
            float t = (a - b) / denom;
            if (t > 0 && t < 1) {
                float v = (1-t)*(1-t)*a + 2*(1-t)*t*b + t*t*c;
                if (i == 0) { x0 = std::min(x0, v); x1 = std::max(x1, v); }
                else        { y0 = std::min(y0, v); y1 = std::max(y1, v); }
            }
        }
    }

    static float eval_cubic(float a, float b, float c, float d, float t) {
        float u = 1 - t;
        return u*u*u*a + 3*u*u*t*b + 3*u*t*t*c + t*t*t*d;
    }

    Rect to_rect() const {
        if (x0 > x1) return {};
        return {x0, y0, x1 - x0, y1 - y0};
    }
};

// Flatten a cubic Bezier segment into line segments.
void flatten_cubic(Point p0, Point p1, Point p2, Point p3,
                   float tolerance, std::vector<Point>& out) {
    // de Casteljau flatness test.
    float dx = p3.x - p0.x;
    float dy = p3.y - p0.y;
    float d_sq = dx * dx + dy * dy;

    if (d_sq < 1e-12f) {
        out.push_back(p3);
        return;
    }

    float d2 = std::abs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
    float d3 = std::abs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
    float err = (d2 + d3);

    if (err * err <= tolerance * tolerance * d_sq) {
        out.push_back(p3);
        return;
    }

    // Subdivide.
    Point m01  = (p0 + p1) * 0.5f;
    Point m12  = (p1 + p2) * 0.5f;
    Point m23  = (p2 + p3) * 0.5f;
    Point m012 = (m01 + m12) * 0.5f;
    Point m123 = (m12 + m23) * 0.5f;
    Point mid  = (m012 + m123) * 0.5f;

    flatten_cubic(p0, m01, m012, mid,  tolerance, out);
    flatten_cubic(mid, m123, m23, p3, tolerance, out);
}

// Flatten a quadratic Bezier.
void flatten_quad(Point p0, Point p1, Point p2,
                  float tolerance, std::vector<Point>& out) {
    // Convert to cubic and flatten.
    Point c1 = p0 + (p1 - p0) * (2.0f / 3.0f);
    Point c2 = p2 + (p1 - p2) * (2.0f / 3.0f);
    flatten_cubic(p0, c1, c2, p2, tolerance, out);
}

// Compute arc length of a cubic via recursive subdivision.
float cubic_length(Point p0, Point p1, Point p2, Point p3, int depth = 0) {
    float chord = p0.distance_to(p3);
    float net   = p0.distance_to(p1) + p1.distance_to(p2) + p2.distance_to(p3);
    if (depth > 16 || (net - chord) < 1e-4f)
        return (chord + net) * 0.5f;

    Point m01  = (p0 + p1) * 0.5f;
    Point m12  = (p1 + p2) * 0.5f;
    Point m23  = (p2 + p3) * 0.5f;
    Point m012 = (m01 + m12) * 0.5f;
    Point m123 = (m12 + m23) * 0.5f;
    Point mid  = (m012 + m123) * 0.5f;

    return cubic_length(p0, m01, m012, mid, depth + 1)
         + cubic_length(mid, m123, m23, p3, depth + 1);
}

float quad_length(Point p0, Point p1, Point p2) {
    Point c1 = p0 + (p1 - p0) * (2.0f / 3.0f);
    Point c2 = p2 + (p1 - p2) * (2.0f / 3.0f);
    return cubic_length(p0, c1, c2, p2);
}

// SVG arc to cubic Bezier conversion.
void arc_to_cubics(Point from, float rx, float ry, float x_rotation,
                   bool large_arc, bool sweep, Point to,
                   std::vector<PathElement>& cmds,
                   std::vector<Point>& pts) {
    if (rx == 0.0f || ry == 0.0f) {
        cmds.push_back(PathElement::AddLineToPoint);
        pts.push_back(to);
        return;
    }

    float dx2 = (from.x - to.x) * 0.5f;
    float dy2 = (from.y - to.y) * 0.5f;
    float cosA = std::cos(x_rotation);
    float sinA = std::sin(x_rotation);

    float x1p =  cosA * dx2 + sinA * dy2;
    float y1p = -sinA * dx2 + cosA * dy2;

    rx = std::abs(rx);
    ry = std::abs(ry);

    float x1p2 = x1p * x1p;
    float y1p2 = y1p * y1p;
    float rx2 = rx * rx;
    float ry2 = ry * ry;

    // Scale up radii if necessary.
    float lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0f) {
        float s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    float num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    float den = rx2 * y1p2 + ry2 * x1p2;
    float sq = (den > 0.0f) ? std::sqrt(std::max(0.0f, num / den)) : 0.0f;
    if (large_arc == sweep) sq = -sq;

    float cxp =  sq * rx * y1p / ry;
    float cyp = -sq * ry * x1p / rx;

    float cx = cosA * cxp - sinA * cyp + (from.x + to.x) * 0.5f;
    float cy = sinA * cxp + cosA * cyp + (from.y + to.y) * 0.5f;

    auto angle = [](float ux, float uy, float vx, float vy) -> float {
        float n = std::sqrt(ux*ux + uy*uy) * std::sqrt(vx*vx + vy*vy);
        if (n < 1e-12f) return 0.0f;
        float c = (ux*vx + uy*vy) / n;
        c = std::clamp(c, -1.0f, 1.0f);
        float a = std::acos(c);
        return (ux*vy - uy*vx < 0) ? -a : a;
    };

    float theta1 = angle(1.0f, 0.0f, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                         (-x1p - cxp) / rx, (-y1p - cyp) / ry);

    if (!sweep && dtheta > 0)  dtheta -= two_pi;
    if (sweep  && dtheta < 0)  dtheta += two_pi;

    int n_segs = static_cast<int>(std::ceil(std::abs(dtheta) / half_pi));
    if (n_segs < 1) n_segs = 1;
    float seg_angle = dtheta / n_segs;

    float t = std::tan(seg_angle * 0.5f);
    float alpha_f = std::sin(seg_angle) * (std::sqrt(4.0f + 3.0f * t * t) - 1.0f) / 3.0f;

    float theta = theta1;
    float prev_cos = std::cos(theta);
    float prev_sin = std::sin(theta);

    for (int i = 0; i < n_segs; i++) {
        float next_theta = theta + seg_angle;
        float next_cos = std::cos(next_theta);
        float next_sin = std::sin(next_theta);

        // Endpoint on the unit circle, then transform.
        auto ep = [&](float cs, float sn) -> Point {
            float px = cosA * rx * cs - sinA * ry * sn + cx;
            float py = sinA * rx * cs + cosA * ry * sn + cy;
            return {px, py};
        };

        // Derivative on unit circle.
        auto dp = [&](float cs, float sn) -> Point {
            float px = -cosA * rx * sn - sinA * ry * cs;
            float py = -sinA * rx * sn + cosA * ry * cs;
            return {px, py};
        };

        Point p_start = ep(prev_cos, prev_sin);
        Point p_end   = ep(next_cos, next_sin);
        Point d_start = dp(prev_cos, prev_sin);
        Point d_end   = dp(next_cos, next_sin);

        Point cp1 = {p_start.x + alpha_f * d_start.x,
                     p_start.y + alpha_f * d_start.y};
        Point cp2 = {p_end.x - alpha_f * d_end.x,
                     p_end.y - alpha_f * d_end.y};

        cmds.push_back(PathElement::AddCurve);
        pts.push_back(cp1);
        pts.push_back(cp2);
        pts.push_back(p_end);

        theta = next_theta;
        prev_cos = next_cos;
        prev_sin = next_sin;
    }
}

// Winding-number point-in-fill test on flattened contours.
int winding_number(const std::vector<std::vector<Point>>& contours, Point p) {
    int winding = 0;
    for (auto& contour : contours) {
        int n = static_cast<int>(contour.size());
        if (n < 2) continue;
        for (int i = 0; i < n; i++) {
            Point a = contour[i];
            Point b = contour[(i + 1) % n];
            if (a.y <= p.y) {
                if (b.y > p.y) {
                    float cross = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
                    if (cross > 0) ++winding;
                }
            } else {
                if (b.y <= p.y) {
                    float cross = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
                    if (cross < 0) --winding;
                }
            }
        }
    }
    return winding;
}

// Build flattened contours from a path impl.
std::vector<std::vector<Point>> build_flat_contours(const Path::Impl& impl, float tol = 0.25f) {
    std::vector<std::vector<Point>> contours;
    Point cur{};
    Point subpath_start{};
    int pt_idx = 0;

    for (auto cmd : impl.commands) {
        switch (cmd) {
        case PathElement::MoveToPoint: {
            Point p = impl.points[pt_idx++];
            contours.emplace_back();
            contours.back().push_back(p);
            cur = p;
            subpath_start = p;
            break;
        }
        case PathElement::AddLineToPoint: {
            Point p = impl.points[pt_idx++];
            if (contours.empty()) { contours.emplace_back(); contours.back().push_back(cur); }
            contours.back().push_back(p);
            cur = p;
            break;
        }
        case PathElement::AddQuadCurve: {
            Point cp = impl.points[pt_idx++];
            Point ep = impl.points[pt_idx++];
            if (contours.empty()) { contours.emplace_back(); contours.back().push_back(cur); }
            flatten_quad(cur, cp, ep, tol, contours.back());
            cur = ep;
            break;
        }
        case PathElement::AddCurve: {
            Point c1 = impl.points[pt_idx++];
            Point c2 = impl.points[pt_idx++];
            Point ep = impl.points[pt_idx++];
            if (contours.empty()) { contours.emplace_back(); contours.back().push_back(cur); }
            flatten_cubic(cur, c1, c2, ep, tol, contours.back());
            cur = ep;
            break;
        }
        case PathElement::CloseSubpath:
            if (!contours.empty() && !contours.back().empty()) {
                if (contours.back().front().x != cur.x || contours.back().front().y != cur.y)
                    contours.back().push_back(contours.back().front());
            }
            cur = subpath_start;
            break;
        }
    }
    return contours;
}

// Convert a Path to a polyclip::Polygon (flattened).
polyclip::Polygon path_to_polygon(const Path::Impl& impl, float tol = 0.25f) {
    auto contours = build_flat_contours(impl, tol);
    polyclip::Polygon poly;
    for (auto& pts : contours) {
        if (pts.size() < 3) continue;
        polyclip::Contour c;
        c.reserve(pts.size());
        for (auto& p : pts)
            c.add({static_cast<double>(p.x), static_cast<double>(p.y)});
        // Remove closing duplicate if present.
        if (c.size() > 1) {
            auto& first = c.vertex(0);
            auto& last  = c.vertex(c.size() - 1);
            if (first == last)
                c.pop_back();
        }
        if (c.size() >= 3)
            poly.add(std::move(c));
    }
    return poly;
}

// Convert polyclip::Polygon back to a Path.
Path polygon_to_path(const polyclip::Polygon& poly) {
    Path::Builder builder;
    for (auto& contour : poly) {
        auto pts = contour.points();
        if (pts.size() < 3) continue;
        builder.move_to(static_cast<float>(pts[0].x), static_cast<float>(pts[0].y));
        for (size_t i = 1; i < pts.size(); i++)
            builder.line_to(static_cast<float>(pts[i].x), static_cast<float>(pts[i].y));
        builder.close();
    }
    return builder.build();
}

polyclip::FillRule to_polyclip_fill(FillRule r) {
    return (r == FillRule::EvenOdd) ? polyclip::FillRule::EvenOdd : polyclip::FillRule::NonZero;
}

// Compute a sub-segment of the path for trimming.
struct SubpathRange {
    int cmd_start;
    int cmd_end;
    int pt_start;
};

struct SubpathInfo {
    int cmd_start, cmd_end;
    int pt_start, pt_end;
    float length;
};

std::vector<SubpathInfo> compute_subpath_info(const Path::Impl& impl) {
    std::vector<SubpathInfo> result;
    int pt_idx = 0;
    int cmd_start = 0;
    int pt_start = 0;
    Point cur{}, subpath_start{};
    float len = 0.0f;

    auto flush = [&](int i) {
        if (cmd_start < i)
            result.push_back({cmd_start, i, pt_start, pt_idx, len});
    };

    for (int i = 0; i < static_cast<int>(impl.commands.size()); i++) {
        auto cmd = impl.commands[i];
        switch (cmd) {
        case PathElement::MoveToPoint:
            if (i > 0) flush(i);
            cmd_start = i;
            pt_start = pt_idx;
            len = 0.0f;
            cur = impl.points[pt_idx++];
            subpath_start = cur;
            break;
        case PathElement::AddLineToPoint: {
            Point p = impl.points[pt_idx++];
            len += cur.distance_to(p);
            cur = p;
            break;
        }
        case PathElement::AddQuadCurve: {
            Point cp = impl.points[pt_idx++];
            Point ep = impl.points[pt_idx++];
            len += quad_length(cur, cp, ep);
            cur = ep;
            break;
        }
        case PathElement::AddCurve: {
            Point c1 = impl.points[pt_idx++];
            Point c2 = impl.points[pt_idx++];
            Point ep = impl.points[pt_idx++];
            len += cubic_length(cur, c1, c2, ep);
            cur = ep;
            break;
        }
        case PathElement::CloseSubpath:
            len += cur.distance_to(subpath_start);
            cur = subpath_start;
            break;
        }
    }
    flush(static_cast<int>(impl.commands.size()));
    return result;
}

// Split a cubic at parameter t.
struct CubicSplit {
    Point left[4];
    Point right[4];
};

CubicSplit split_cubic(Point p0, Point p1, Point p2, Point p3, float t) {
    Point m01 = p0 + (p1 - p0) * t;
    Point m12 = p1 + (p2 - p1) * t;
    Point m23 = p2 + (p3 - p2) * t;
    Point m012 = m01 + (m12 - m01) * t;
    Point m123 = m12 + (m23 - m12) * t;
    Point mid  = m012 + (m123 - m012) * t;
    return {{p0, m01, m012, mid}, {mid, m123, m23, p3}};
}

} // anonymous namespace

// -- Path lifecycle --

Path::Path() : m_impl(nullptr) {}

Path::~Path() {
    release(m_impl);
}

Path::Path(const Path& other) : m_impl(other.m_impl) {
    retain(m_impl);
}

Path::Path(Path&& other) noexcept : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

Path& Path::operator=(const Path& other) {
    if (this != &other) {
        retain(other.m_impl);
        release(m_impl);
        m_impl = other.m_impl;
    }
    return *this;
}

Path& Path::operator=(Path&& other) noexcept {
    if (this != &other) {
        release(m_impl);
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Path::operator bool() const {
    return m_impl && !m_impl->commands.empty();
}

Path Path::clone() const {
    Path p;
    if (!m_impl) return p;
    p.m_impl = alloc_impl();
    if (!p.m_impl) return p;
    p.m_impl->copy_from(m_impl->commands, m_impl->points);
    p.m_impl->num_contours = m_impl->num_contours;
    p.m_impl->num_curves   = m_impl->num_curves;
    p.m_impl->start_point  = m_impl->start_point;
    return p;
}

// -- Queries --

bool Path::is_empty() const {
    return !m_impl || m_impl->commands.empty();
}

Rect Path::bounding_box() const {
    if (!m_impl || m_impl->commands.empty())
        return {};

    BBoxAccum bb;
    Point cur{};
    int pt = 0;
    for (auto cmd : m_impl->commands) {
        switch (cmd) {
        case PathElement::MoveToPoint:
            cur = m_impl->points[pt++];
            bb.add(cur);
            break;
        case PathElement::AddLineToPoint: {
            Point p = m_impl->points[pt++];
            bb.add(p);
            cur = p;
            break;
        }
        case PathElement::AddQuadCurve: {
            Point cp = m_impl->points[pt++];
            Point ep = m_impl->points[pt++];
            bb.add_quad(cur, cp, ep);
            cur = ep;
            break;
        }
        case PathElement::AddCurve: {
            Point c1 = m_impl->points[pt++];
            Point c2 = m_impl->points[pt++];
            Point ep = m_impl->points[pt++];
            bb.add_cubic(cur, c1, c2, ep);
            cur = ep;
            break;
        }
        case PathElement::CloseSubpath:
            break;
        }
    }
    return bb.to_rect();
}

Rect Path::path_bounding_box() const {
    if (!m_impl || m_impl->points.empty())
        return {};

    BBoxAccum bb;
    for (auto& p : m_impl->points)
        bb.add(p);
    return bb.to_rect();
}

float Path::length() const {
    if (!m_impl || m_impl->commands.empty())
        return 0.0f;

    float total = 0.0f;
    Point cur{};
    Point subpath_start{};
    int pt = 0;

    for (auto cmd : m_impl->commands) {
        switch (cmd) {
        case PathElement::MoveToPoint:
            cur = m_impl->points[pt++];
            subpath_start = cur;
            break;
        case PathElement::AddLineToPoint: {
            Point p = m_impl->points[pt++];
            total += cur.distance_to(p);
            cur = p;
            break;
        }
        case PathElement::AddQuadCurve: {
            Point cp = m_impl->points[pt++];
            Point ep = m_impl->points[pt++];
            total += quad_length(cur, cp, ep);
            cur = ep;
            break;
        }
        case PathElement::AddCurve: {
            Point c1 = m_impl->points[pt++];
            Point c2 = m_impl->points[pt++];
            Point ep = m_impl->points[pt++];
            total += cubic_length(cur, c1, c2, ep);
            cur = ep;
            break;
        }
        case PathElement::CloseSubpath:
            total += cur.distance_to(subpath_start);
            cur = subpath_start;
            break;
        }
    }
    return total;
}

bool Path::contains(Point p, FillRule rule) const {
    if (!m_impl || m_impl->commands.empty())
        return false;

    auto contours = build_flat_contours(*m_impl);
    int w = winding_number(contours, p);

    if (rule == FillRule::EvenOdd)
        return (w & 1) != 0;
    return w != 0;
}

bool Path::stroke_contains(Point p, float line_width, LineCap cap,
                            LineJoin join, float miter_limit) const {
    if (!m_impl || m_impl->commands.empty() || line_width <= 0.0f)
        return false;

    Path stroked_path = stroked(line_width, cap, join, miter_limit);
    return stroked_path.contains(p, FillRule::Winding);
}

// -- Derived paths --

Path Path::transformed(const AffineTransform& t) const {
    if (!m_impl || m_impl->commands.empty() || t.is_identity())
        return *this;

    Path result;
    result.m_impl = alloc_impl();
    if (!result.m_impl) return {};
    result.m_impl->num_contours = m_impl->num_contours;
    result.m_impl->num_curves   = m_impl->num_curves;

    result.m_impl->cmd_storage.assign(m_impl->commands.begin(), m_impl->commands.end());
    result.m_impl->pt_storage.resize(m_impl->points.size());
    t.apply(m_impl->points.data(), result.m_impl->pt_storage.data(),
            static_cast<int>(m_impl->points.size()));
    result.m_impl->commands = result.m_impl->cmd_storage;
    result.m_impl->points   = result.m_impl->pt_storage;
    result.m_impl->start_point = t.apply(m_impl->start_point);
    return result;
}

Path Path::stroked(float width, LineCap cap, LineJoin join,
                   float miter_limit) const {
    if (!m_impl || m_impl->commands.empty() || width <= 0.0f)
        return {};

    StrokeData sd;
    sd.style.width = width;
    sd.style.cap = cap;
    sd.style.join = join;
    sd.style.miter_limit = miter_limit;

    return stroke_to_path(*this, AffineTransform::identity(), sd);
}

Path Path::dashed(float offset, std::span<const float> dashes) const {
    if (!m_impl || m_impl->commands.empty() || dashes.empty())
        return *this;

    // Compute dash pattern total length.
    float dash_total = 0.0f;
    for (float d : dashes)
        dash_total += d;
    if (dash_total <= 0.0f)
        return *this;

    // Flatten to work with line segments.
    Path flat = flattened(0.25f);
    const auto* fi = path_impl(flat);
    if (!fi || fi->commands.empty())
        return {};

    Builder builder;
    Point cur{};
    Point subpath_start{};
    bool drawing = true;
    float remaining = 0.0f;
    int dash_idx = 0;
    float dash_offset = std::fmod(offset, dash_total);
    if (dash_offset < 0) dash_offset += dash_total;

    // Find starting dash state from offset.
    drawing = true;
    dash_idx = 0;
    remaining = dashes[0];
    while (dash_offset > 0.0f) {
        if (dash_offset < remaining) {
            remaining -= dash_offset;
            dash_offset = 0.0f;
        } else {
            dash_offset -= remaining;
            drawing = !drawing;
            dash_idx = (dash_idx + 1) % static_cast<int>(dashes.size());
            remaining = dashes[dash_idx];
        }
    }

    bool need_move = true;
    int pt = 0;

    for (auto cmd : fi->commands) {
        switch (cmd) {
        case PathElement::MoveToPoint: {
            cur = fi->points[pt++];
            subpath_start = cur;
            need_move = true;
            // Reset dash state at each subpath.
            drawing = true;
            dash_idx = 0;
            remaining = dashes[0];
            float off = std::fmod(offset, dash_total);
            if (off < 0) off += dash_total;
            while (off > 0.0f) {
                if (off < remaining) { remaining -= off; off = 0.0f; }
                else { off -= remaining; drawing = !drawing; dash_idx = (dash_idx + 1) % static_cast<int>(dashes.size()); remaining = dashes[dash_idx]; }
            }
            break;
        }
        case PathElement::AddLineToPoint: {
            Point target = fi->points[pt++];
            float seg_len = cur.distance_to(target);
            if (seg_len < 1e-8f) { cur = target; break; }
            Point dir = (target - cur) / seg_len;
            float consumed = 0.0f;

            while (consumed < seg_len) {
                float step = std::min(remaining, seg_len - consumed);
                Point next_pt = cur + dir * step;

                if (drawing) {
                    if (need_move) { builder.move_to(cur.x, cur.y); need_move = false; }
                    builder.line_to(next_pt.x, next_pt.y);
                }

                consumed += step;
                remaining -= step;
                cur = next_pt;

                if (remaining <= 1e-8f) {
                    drawing = !drawing;
                    dash_idx = (dash_idx + 1) % static_cast<int>(dashes.size());
                    remaining = dashes[dash_idx];
                    need_move = true;
                }
            }
            break;
        }
        case PathElement::CloseSubpath: {
            float seg_len = cur.distance_to(subpath_start);
            if (seg_len > 1e-8f) {
                Point dir = (subpath_start - cur) / seg_len;
                float consumed = 0.0f;
                while (consumed < seg_len) {
                    float step = std::min(remaining, seg_len - consumed);
                    Point next_pt = cur + dir * step;
                    if (drawing) {
                        if (need_move) { builder.move_to(cur.x, cur.y); need_move = false; }
                        builder.line_to(next_pt.x, next_pt.y);
                    }
                    consumed += step;
                    remaining -= step;
                    cur = next_pt;
                    if (remaining <= 1e-8f) {
                        drawing = !drawing;
                        dash_idx = (dash_idx + 1) % static_cast<int>(dashes.size());
                        remaining = dashes[dash_idx];
                        need_move = true;
                    }
                }
            }
            cur = subpath_start;
            break;
        }
        default:
            // Flattened path should only have MoveToPoint, AddLineToPoint, CloseSubpath.
            pt += points_per_command(cmd);
            break;
        }
    }

    return builder.build();
}

Path Path::trimmed(float begin_frac, float end_frac, TrimMode mode) const {
    if (!m_impl || m_impl->commands.empty())
        return {};

    begin_frac = std::clamp(begin_frac, 0.0f, 1.0f);
    end_frac   = std::clamp(end_frac, 0.0f, 1.0f);
    if (begin_frac >= end_frac)
        return {};
    if (begin_frac == 0.0f && end_frac == 1.0f)
        return *this;

    // Flatten and work with line segments.
    Path flat = flattened(0.25f);
    const auto* fi = path_impl(flat);
    if (!fi) return {};

    auto subpaths = compute_subpath_info(*fi);
    if (subpaths.empty()) return {};

    Builder builder;

    if (mode == TrimMode::Sequential) {
        // Compute total length.
        float total = 0.0f;
        for (auto& sp : subpaths) total += sp.length;
        if (total <= 0.0f) return {};

        float start_dist = begin_frac * total;
        float end_dist   = end_frac * total;
        float accum = 0.0f;
        Point cur{};
        Point subpath_start{};
        int pt = 0;
        bool started = false;

        for (auto cmd : fi->commands) {
            switch (cmd) {
            case PathElement::MoveToPoint:
                cur = fi->points[pt++];
                subpath_start = cur;
                break;
            case PathElement::AddLineToPoint: {
                Point target = fi->points[pt++];
                float seg_len = cur.distance_to(target);
                float seg_start = accum;
                float seg_end = accum + seg_len;

                if (seg_end > start_dist && seg_start < end_dist) {
                    float t0 = 0.0f, t1 = 1.0f;
                    if (seg_start < start_dist)
                        t0 = (start_dist - seg_start) / seg_len;
                    if (seg_end > end_dist)
                        t1 = (end_dist - seg_start) / seg_len;

                    Point p0 = cur + (target - cur) * t0;
                    Point p1 = cur + (target - cur) * t1;

                    if (!started) {
                        builder.move_to(p0.x, p0.y);
                        started = true;
                    }
                    builder.line_to(p1.x, p1.y);
                }

                accum = seg_end;
                cur = target;
                break;
            }
            case PathElement::CloseSubpath: {
                float seg_len = cur.distance_to(subpath_start);
                float seg_start = accum;
                float seg_end = accum + seg_len;

                if (seg_end > start_dist && seg_start < end_dist && seg_len > 1e-8f) {
                    float t0 = 0.0f, t1 = 1.0f;
                    if (seg_start < start_dist)
                        t0 = (start_dist - seg_start) / seg_len;
                    if (seg_end > end_dist)
                        t1 = (end_dist - seg_start) / seg_len;

                    Point p0 = cur + (subpath_start - cur) * t0;
                    Point p1 = cur + (subpath_start - cur) * t1;

                    if (!started) {
                        builder.move_to(p0.x, p0.y);
                        started = true;
                    }
                    builder.line_to(p1.x, p1.y);
                }

                accum = seg_end;
                cur = subpath_start;
                break;
            }
            default:
                pt += points_per_command(cmd);
                break;
            }

            if (accum >= end_dist) break;
        }
    } else {
        // Simultaneous: trim each subpath individually.
        Point cur{};
        Point subpath_start{};
        int pt = 0;
        int sp_idx = 0;
        float sp_accum = 0.0f;
        bool started_sp = false;

        for (int i = 0; i < static_cast<int>(fi->commands.size()); i++) {
            auto cmd = fi->commands[i];
            if (cmd == PathElement::MoveToPoint) {
                cur = fi->points[pt++];
                subpath_start = cur;
                sp_accum = 0.0f;
                started_sp = false;
                continue;
            }

            if (sp_idx >= static_cast<int>(subpaths.size())) break;
            float sp_len = subpaths[sp_idx].length;
            float start_dist = begin_frac * sp_len;
            float end_dist   = end_frac * sp_len;

            auto process_line = [&](Point target) {
                float seg_len = cur.distance_to(target);
                float seg_start = sp_accum;
                float seg_end = sp_accum + seg_len;

                if (seg_end > start_dist && seg_start < end_dist && seg_len > 1e-8f) {
                    float t0 = 0.0f, t1 = 1.0f;
                    if (seg_start < start_dist)
                        t0 = (start_dist - seg_start) / seg_len;
                    if (seg_end > end_dist)
                        t1 = (end_dist - seg_start) / seg_len;

                    Point p0 = cur + (target - cur) * t0;
                    Point p1 = cur + (target - cur) * t1;

                    if (!started_sp) {
                        builder.move_to(p0.x, p0.y);
                        started_sp = true;
                    }
                    builder.line_to(p1.x, p1.y);
                }

                sp_accum = seg_end;
                cur = target;
            };

            switch (cmd) {
            case PathElement::AddLineToPoint:
                process_line(fi->points[pt++]);
                break;
            case PathElement::CloseSubpath:
                process_line(subpath_start);
                cur = subpath_start;
                sp_idx++;
                sp_accum = 0.0f;
                started_sp = false;
                break;
            default:
                pt += points_per_command(cmd);
                break;
            }
        }
    }

    return builder.build();
}

Path Path::flattened(float tolerance) const {
    if (!m_impl || m_impl->commands.empty())
        return {};

    if (m_impl->num_curves == 0)
        return *this;

    Builder builder;
    Point cur{};
    int pt = 0;

    for (auto cmd : m_impl->commands) {
        switch (cmd) {
        case PathElement::MoveToPoint: {
            Point p = m_impl->points[pt++];
            builder.move_to(p.x, p.y);
            cur = p;
            break;
        }
        case PathElement::AddLineToPoint: {
            Point p = m_impl->points[pt++];
            builder.line_to(p.x, p.y);
            cur = p;
            break;
        }
        case PathElement::AddQuadCurve: {
            Point cp = m_impl->points[pt++];
            Point ep = m_impl->points[pt++];
            std::vector<Point> flat_pts;
            flatten_quad(cur, cp, ep, tolerance, flat_pts);
            for (auto& fp : flat_pts)
                builder.line_to(fp.x, fp.y);
            cur = ep;
            break;
        }
        case PathElement::AddCurve: {
            Point c1 = m_impl->points[pt++];
            Point c2 = m_impl->points[pt++];
            Point ep = m_impl->points[pt++];
            std::vector<Point> flat_pts;
            flatten_cubic(cur, c1, c2, ep, tolerance, flat_pts);
            for (auto& fp : flat_pts)
                builder.line_to(fp.x, fp.y);
            cur = ep;
            break;
        }
        case PathElement::CloseSubpath:
            builder.close();
            break;
        }
    }

    return builder.build();
}

Path Path::reversed() const {
    if (!m_impl || m_impl->commands.empty())
        return {};

    // Collect subpaths.
    struct SubCmd {
        PathElement type;
        int pt_index;
    };
    struct Subpath {
        std::vector<SubCmd> cmds;
        bool closed = false;
    };

    std::vector<Subpath> subpaths;
    int pt = 0;

    for (auto cmd : m_impl->commands) {
        int n = points_per_command(cmd);
        if (cmd == PathElement::MoveToPoint) {
            subpaths.emplace_back();
        }
        if (!subpaths.empty()) {
            if (cmd == PathElement::CloseSubpath)
                subpaths.back().closed = true;
            else
                subpaths.back().cmds.push_back({cmd, pt});
        }
        pt += n;
    }

    Builder builder;

    for (int si = static_cast<int>(subpaths.size()) - 1; si >= 0; si--) {
        auto& sp = subpaths[si];
        if (sp.cmds.empty()) continue;

        // Determine the last point in this subpath.
        auto last_cmd = sp.cmds.back();
        int last_pt_idx = last_cmd.pt_index + points_per_command(last_cmd.type) - 1;
        Point last_pt = m_impl->points[last_pt_idx];
        builder.move_to(last_pt.x, last_pt.y);

        // Walk commands in reverse.
        for (int ci = static_cast<int>(sp.cmds.size()) - 1; ci >= 0; ci--) {
            auto& c = sp.cmds[ci];
            // The "from" point of this command is the endpoint of the previous command,
            // or the move-to point for the first command.
            Point from_pt;
            if (ci > 0) {
                auto& prev = sp.cmds[ci - 1];
                from_pt = m_impl->points[prev.pt_index + points_per_command(prev.type) - 1];
            } else {
                // ci==0 is always MoveToPoint (handled by break below).
                from_pt = m_impl->points[sp.cmds[0].pt_index];
            }

            switch (c.type) {
            case PathElement::MoveToPoint:
                // Already handled.
                break;
            case PathElement::AddLineToPoint:
                builder.line_to(from_pt.x, from_pt.y);
                break;
            case PathElement::AddQuadCurve: {
                Point cp = m_impl->points[c.pt_index];
                builder.quad_to(cp.x, cp.y, from_pt.x, from_pt.y);
                break;
            }
            case PathElement::AddCurve: {
                Point c2 = m_impl->points[c.pt_index + 1];
                Point c1 = m_impl->points[c.pt_index];
                builder.cubic_to(c2.x, c2.y, c1.x, c1.y, from_pt.x, from_pt.y);
                break;
            }
            case PathElement::CloseSubpath:
                break;
            }
        }

        if (sp.closed)
            builder.close();
    }

    return builder.build();
}

// -- Boolean ops --

Path Path::united(const Path& other, FillRule rule) const {
    if (is_empty()) return other;
    if (other.is_empty()) return *this;

    auto subject = path_to_polygon(*m_impl);
    auto clip    = path_to_polygon(*path_impl(other));
    auto fr = to_polyclip_fill(rule);
    subject.decompose(fr);
    clip.decompose(fr);
    auto result = polyclip::compute(polyclip::Operation::Union, subject, clip, fr);
    return polygon_to_path(result);
}

Path Path::intersected(const Path& other, FillRule rule) const {
    if (is_empty() || other.is_empty()) return {};

    auto subject = path_to_polygon(*m_impl);
    auto clip    = path_to_polygon(*path_impl(other));
    auto fr = to_polyclip_fill(rule);
    subject.decompose(fr);
    clip.decompose(fr);
    auto result = polyclip::compute(polyclip::Operation::Intersection, subject, clip, fr);
    return polygon_to_path(result);
}

Path Path::subtracted(const Path& other, FillRule rule) const {
    if (is_empty()) return {};
    if (other.is_empty()) return *this;

    auto subject = path_to_polygon(*m_impl);
    auto clip    = path_to_polygon(*path_impl(other));
    auto fr = to_polyclip_fill(rule);
    subject.decompose(fr);
    clip.decompose(fr);
    auto result = polyclip::compute(polyclip::Operation::Difference, subject, clip, fr);
    return polygon_to_path(result);
}

Path Path::xored(const Path& other, FillRule rule) const {
    if (is_empty()) return other;
    if (other.is_empty()) return *this;

    auto subject = path_to_polygon(*m_impl);
    auto clip    = path_to_polygon(*path_impl(other));
    auto fr = to_polyclip_fill(rule);
    subject.decompose(fr);
    clip.decompose(fr);
    auto result = polyclip::compute(polyclip::Operation::Xor, subject, clip, fr);
    return polygon_to_path(result);
}

// -- Traversal --

void Path::apply(ApplyFunc func, void* info) const {
    if (!m_impl || !func) return;

    int pt = 0;
    for (auto cmd : m_impl->commands) {
        int n = points_per_command(cmd);
        Element e{cmd, n > 0 ? &m_impl->points[pt] : nullptr};
        func(info, &e);
        pt += n;
    }
}

// -- Iterator --

Path::Element Path::Iterator::operator*() const {
    auto* impl = static_cast<const Path::Impl*>(m_data);
    if (!impl || m_index >= static_cast<int>(impl->commands.size()))
        return {PathElement::CloseSubpath, nullptr};

    PathElement cmd = impl->commands[m_index];
    int n = points_per_command(cmd);
    return {cmd, n > 0 ? &impl->points[m_pt_offset] : nullptr};
}

Path::Iterator& Path::Iterator::operator++() {
    auto* impl = static_cast<const Path::Impl*>(m_data);
    if (impl && m_index < static_cast<int>(impl->commands.size()))
        m_pt_offset += points_per_command(impl->commands[m_index]);
    ++m_index;
    return *this;
}

bool Path::Iterator::operator==(const Iterator& o) const {
    return m_data == o.m_data && m_index == o.m_index;
}

Path::Iterator Path::begin() const {
    Iterator it;
    it.m_data = m_impl;
    it.m_index = 0;
    return it;
}

Path::Iterator Path::end() const {
    Iterator it;
    it.m_data = m_impl;
    it.m_index = m_impl ? static_cast<int>(m_impl->commands.size()) : 0;
    return it;
}

// -- SVG path data parser --

std::optional<Path> Path::parse_svg(std::string_view data) {
    const char* it  = data.data();
    const char* end = it + data.size();

    Builder builder;
    Point cur{};
    Point subpath_start{};
    Point last_control{};
    char last_cmd = 0;

    auto skip = [&]() { skip_ws(it, end); };
    auto comma_ws = [&]() {
        skip_ws(it, end);
        if (it < end && *it == ',') { ++it; skip_ws(it, end); }
    };
    auto read_float = [&](float& v) -> bool {
        skip_ws(it, end);
        return parse_number(it, end, v);
    };
    auto read_flag = [&](bool& v) -> bool {
        skip_ws(it, end);
        if (it < end && *it == ',') { ++it; skip_ws(it, end); }
        if (it < end && (*it == '0' || *it == '1')) {
            v = (*it == '1');
            ++it;
            return true;
        }
        return false;
    };

    skip();

    while (it < end) {
        char c = *it;

        char prev_cmd = last_cmd;

        if (is_alpha(c) && c != 'e' && c != 'E') {
            last_cmd = c;
            ++it;
        } else if (last_cmd == 0) {
            detail::set_last_error(Error::PathParseError);
            return std::nullopt;
        }

        // The command to execute.
        char cmd = last_cmd;
        bool rel = (cmd >= 'a' && cmd <= 'z');
        char up  = rel ? static_cast<char>(cmd - 32) : cmd;

        switch (up) {
        case 'M': {
            float x, y;
            if (!read_float(x)) goto done;
            comma_ws();
            if (!read_float(y)) goto fail;
            if (rel) { x += cur.x; y += cur.y; }
            builder.move_to(x, y);
            cur = {x, y};
            subpath_start = cur;
            // Subsequent coords are implicit LineTo.
            last_cmd = rel ? 'l' : 'L';
            break;
        }
        case 'L': {
            float x, y;
            if (!read_float(x)) goto fail;
            comma_ws();
            if (!read_float(y)) goto fail;
            if (rel) { x += cur.x; y += cur.y; }
            builder.line_to(x, y);
            cur = {x, y};
            break;
        }
        case 'H': {
            float x;
            if (!read_float(x)) goto fail;
            if (rel) x += cur.x;
            builder.line_to(x, cur.y);
            cur.x = x;
            break;
        }
        case 'V': {
            float y;
            if (!read_float(y)) goto fail;
            if (rel) y += cur.y;
            builder.line_to(cur.x, y);
            cur.y = y;
            break;
        }
        case 'C': {
            float x1, y1, x2, y2, x, y;
            if (!read_float(x1)) goto fail; comma_ws();
            if (!read_float(y1)) goto fail; comma_ws();
            if (!read_float(x2)) goto fail; comma_ws();
            if (!read_float(y2)) goto fail; comma_ws();
            if (!read_float(x))  goto fail; comma_ws();
            if (!read_float(y))  goto fail;
            if (rel) { x1 += cur.x; y1 += cur.y; x2 += cur.x; y2 += cur.y; x += cur.x; y += cur.y; }
            builder.cubic_to(x1, y1, x2, y2, x, y);
            last_control = {x2, y2};
            cur = {x, y};
            break;
        }
        case 'S': {
            float x2, y2, x, y;
            if (!read_float(x2)) goto fail; comma_ws();
            if (!read_float(y2)) goto fail; comma_ws();
            if (!read_float(x))  goto fail; comma_ws();
            if (!read_float(y))  goto fail;
            if (rel) { x2 += cur.x; y2 += cur.y; x += cur.x; y += cur.y; }
            // Reflection of last control point.
            Point cp1 = cur;
            char prev_up = (prev_cmd >= 'a') ? static_cast<char>(prev_cmd - 32) : prev_cmd;
            if (prev_up == 'C' || prev_up == 'S')
                cp1 = cur * 2.0f - last_control;
            builder.cubic_to(cp1.x, cp1.y, x2, y2, x, y);
            last_control = {x2, y2};
            cur = {x, y};
            break;
        }
        case 'Q': {
            float x1, y1, x, y;
            if (!read_float(x1)) goto fail; comma_ws();
            if (!read_float(y1)) goto fail; comma_ws();
            if (!read_float(x))  goto fail; comma_ws();
            if (!read_float(y))  goto fail;
            if (rel) { x1 += cur.x; y1 += cur.y; x += cur.x; y += cur.y; }
            builder.quad_to(x1, y1, x, y);
            last_control = {x1, y1};
            cur = {x, y};
            break;
        }
        case 'T': {
            float x, y;
            if (!read_float(x)) goto fail; comma_ws();
            if (!read_float(y)) goto fail;
            if (rel) { x += cur.x; y += cur.y; }
            Point cp = cur;
            char prev_up2 = (prev_cmd >= 'a') ? static_cast<char>(prev_cmd - 32) : prev_cmd;
            if (prev_up2 == 'Q' || prev_up2 == 'T')
                cp = cur * 2.0f - last_control;
            builder.quad_to(cp.x, cp.y, x, y);
            last_control = cp;
            cur = {x, y};
            break;
        }
        case 'A': {
            float rx, ry, angle, x, y;
            bool large_arc, sweep;
            if (!read_float(rx))    goto fail; comma_ws();
            if (!read_float(ry))    goto fail; comma_ws();
            if (!read_float(angle)) goto fail; comma_ws();
            if (!read_flag(large_arc)) goto fail; comma_ws();
            if (!read_flag(sweep))     goto fail; comma_ws();
            if (!read_float(x))     goto fail; comma_ws();
            if (!read_float(y))     goto fail;
            if (rel) { x += cur.x; y += cur.y; }
            builder.arc_to(rx, ry, deg2rad(angle), large_arc, sweep, x, y);
            cur = {x, y};
            break;
        }
        case 'Z':
            builder.close();
            cur = subpath_start;
            break;
        default:
            goto fail;
        }

        // M/m handler already set last_cmd to L/l for implicit lineto.
        if (up != 'M')
            last_cmd = cmd;
        skip();
        continue;

    done:
        break;
    }

    {
        Path result = builder.build();
        if (!result)
            return std::nullopt;
        return result;
    }

fail:
    detail::set_last_error(Error::PathParseError);
    return std::nullopt;
}

// -- from_raw --

Path Path::from_raw(const PathElement* elements, int element_count,
                    const Point* points, int point_count) {
    if (!elements || element_count <= 0)
        return {};

    Path p;
    p.m_impl = alloc_impl();
    if (!p.m_impl) return {};

    p.m_impl->borrow(elements, element_count, points, point_count);

    // Compute metadata.
    for (auto cmd : p.m_impl->commands) {
        if (cmd == PathElement::MoveToPoint)
            p.m_impl->num_contours++;
        if (cmd == PathElement::AddQuadCurve || cmd == PathElement::AddCurve)
            p.m_impl->num_curves++;
    }
    if (!p.m_impl->points.empty())
        p.m_impl->start_point = p.m_impl->points[0];

    return p;
}



// Path::Builder


struct Path::Builder::Impl {
    std::vector<PathElement> commands;
    std::vector<Point>       points;
    int  num_contours = 0;
    int  num_curves   = 0;
    bool has_move     = false;
    Point current     = {};
    Point subpath_start = {};
};

Path::Builder::Builder() : m_impl(new (std::nothrow) Impl) {}

Path::Builder::~Builder() {
    delete m_impl;
}

Path::Builder::Builder(const Builder& o) : m_impl(nullptr) {
    if (o.m_impl)
        m_impl = new (std::nothrow) Impl(*o.m_impl);
}

Path::Builder::Builder(Builder&& o) noexcept : m_impl(o.m_impl) {
    o.m_impl = nullptr;
}

Path::Builder& Path::Builder::operator=(const Builder& o) {
    if (this != &o) {
        delete m_impl;
        m_impl = o.m_impl ? new (std::nothrow) Impl(*o.m_impl) : nullptr;
    }
    return *this;
}

Path::Builder& Path::Builder::operator=(Builder&& o) noexcept {
    if (this != &o) {
        delete m_impl;
        m_impl = o.m_impl;
        o.m_impl = nullptr;
    }
    return *this;
}

Path::Builder::Builder(const Path& source) : m_impl(new (std::nothrow) Impl) {
    if (!m_impl) return;
    auto* pi = path_impl(source);
    if (!pi) return;
    m_impl->commands.assign(pi->commands.begin(), pi->commands.end());
    m_impl->points.assign(pi->points.begin(), pi->points.end());
    m_impl->num_contours = pi->num_contours;
    m_impl->num_curves   = pi->num_curves;
    m_impl->has_move     = !pi->commands.empty();

    // Find current point.
    if (!pi->points.empty()) {
        m_impl->current = pi->points.back();
        // Find subpath start — walk back to last MoveToPoint.
        int pt = 0;
        Point last_move{};
        for (auto cmd : pi->commands) {
            if (cmd == PathElement::MoveToPoint)
                last_move = pi->points[pt];
            pt += points_per_command(cmd);
        }
        m_impl->subpath_start = last_move;
    }
}

// Ensure an implicit move-to at (0,0) if no move has been issued.
static void ensure_move(Path::Builder::Impl* m) {
    if (!m || m->has_move) return;
    m->commands.push_back(PathElement::MoveToPoint);
    m->points.push_back({0.0f, 0.0f});
    m->num_contours++;
    m->has_move = true;
    m->subpath_start = {0.0f, 0.0f};
}

Path::Builder& Path::Builder::move_to(float x, float y) {
    if (!m_impl) return *this;
    m_impl->commands.push_back(PathElement::MoveToPoint);
    m_impl->points.push_back({x, y});
    m_impl->current = {x, y};
    m_impl->subpath_start = {x, y};
    m_impl->has_move = true;
    m_impl->num_contours++;
    return *this;
}

Path::Builder& Path::Builder::line_to(float x, float y) {
    if (!m_impl) return *this;
    ensure_move(m_impl);
    m_impl->commands.push_back(PathElement::AddLineToPoint);
    m_impl->points.push_back({x, y});
    m_impl->current = {x, y};
    return *this;
}

Path::Builder& Path::Builder::quad_to(float cpx, float cpy, float x, float y) {
    if (!m_impl) return *this;
    ensure_move(m_impl);
    m_impl->commands.push_back(PathElement::AddQuadCurve);
    m_impl->points.push_back({cpx, cpy});
    m_impl->points.push_back({x, y});
    m_impl->current = {x, y};
    m_impl->num_curves++;
    return *this;
}

Path::Builder& Path::Builder::cubic_to(float cp1x, float cp1y,
                                        float cp2x, float cp2y,
                                        float x, float y) {
    if (!m_impl) return *this;
    ensure_move(m_impl);
    m_impl->commands.push_back(PathElement::AddCurve);
    m_impl->points.push_back({cp1x, cp1y});
    m_impl->points.push_back({cp2x, cp2y});
    m_impl->points.push_back({x, y});
    m_impl->current = {x, y};
    m_impl->num_curves++;
    return *this;
}

Path::Builder& Path::Builder::arc_to(float rx, float ry, float angle,
                                      bool large_arc, bool sweep,
                                      float x, float y) {
    if (!m_impl) return *this;
    ensure_move(m_impl);

    Point from = m_impl->current;
    Point to{x, y};

    if (from.x == to.x && from.y == to.y)
        return *this;

    size_t cmd_before = m_impl->commands.size();
    arc_to_cubics(from, rx, ry, angle, large_arc, sweep, to,
                  m_impl->commands, m_impl->points);

    m_impl->current = to;
    // Count only the curves added by this arc_to_cubics call.
    for (size_t i = cmd_before; i < m_impl->commands.size(); ++i) {
        if (m_impl->commands[i] == PathElement::AddCurve)
            m_impl->num_curves++;
    }
    return *this;
}

Path::Builder& Path::Builder::close() {
    if (!m_impl) return *this;
    m_impl->commands.push_back(PathElement::CloseSubpath);
    m_impl->current = m_impl->subpath_start;
    return *this;
}

Path::Builder& Path::Builder::add_rect(const Rect& r) {
    move_to(r.min_x(), r.min_y());
    line_to(r.max_x(), r.min_y());
    line_to(r.max_x(), r.max_y());
    line_to(r.min_x(), r.max_y());
    close();
    return *this;
}

Path::Builder& Path::Builder::add_round_rect(const Rect& r, float rx, float ry) {
    if (rx <= 0 && ry <= 0)
        return add_rect(r);

    // When both radii are equal, delegate to the uniform CornerRadii path.
    if (std::abs(rx - ry) < 1e-6f)
        return add_round_rect(r, CornerRadii{rx});

    // Clamp radii to half the rect dimensions (like SVG).
    float hw = r.width() * 0.5f;
    float hh = r.height() * 0.5f;
    rx = std::min(std::abs(rx), hw);
    ry = std::min(std::abs(ry), hh);

    if (rx <= 0 || ry <= 0)
        return add_rect(r);

    float x0 = r.min_x(), y0 = r.min_y();
    float x1 = r.max_x(), y1 = r.max_y();
    float kx = rx * kappa;
    float ky = ry * kappa;

    move_to(x0 + rx, y0);
    line_to(x1 - rx, y0);
    cubic_to(x1 - rx + kx, y0,       x1, y0 + ry - ky, x1, y0 + ry);
    line_to(x1, y1 - ry);
    cubic_to(x1, y1 - ry + ky,       x1 - rx + kx, y1,  x1 - rx, y1);
    line_to(x0 + rx, y1);
    cubic_to(x0 + rx - kx, y1,       x0, y1 - ry + ky, x0, y1 - ry);
    line_to(x0, y0 + ry);
    cubic_to(x0, y0 + ry - ky,       x0 + rx - kx, y0,  x0 + rx, y0);
    close();
    return *this;
}

Path::Builder& Path::Builder::add_round_rect(const Rect& r, const CornerRadii& radii) {
    float hw = r.width() * 0.5f;
    float hh = r.height() * 0.5f;

    float tl = std::min(radii.top_left,     std::min(hw, hh));
    float tr = std::min(radii.top_right,    std::min(hw, hh));
    float br = std::min(radii.bottom_right, std::min(hw, hh));
    float bl = std::min(radii.bottom_left,  std::min(hw, hh));

    if (tl <= 0 && tr <= 0 && br <= 0 && bl <= 0)
        return add_rect(r);

    float x0 = r.min_x(), y0 = r.min_y();
    float x1 = r.max_x(), y1 = r.max_y();

    move_to(x0 + tl, y0);
    line_to(x1 - tr, y0);
    if (tr > 0) {
        cubic_to(x1 - tr + tr * kappa, y0,
                 x1,                     y0 + tr - tr * kappa,
                 x1,                     y0 + tr);
    }
    line_to(x1, y1 - br);
    if (br > 0) {
        cubic_to(x1,                     y1 - br + br * kappa,
                 x1 - br + br * kappa,   y1,
                 x1 - br,               y1);
    }
    line_to(x0 + bl, y1);
    if (bl > 0) {
        cubic_to(x0 + bl - bl * kappa,  y1,
                 x0,                     y1 - bl + bl * kappa,
                 x0,                     y1 - bl);
    }
    line_to(x0, y0 + tl);
    if (tl > 0) {
        cubic_to(x0,                     y0 + tl - tl * kappa,
                 x0 + tl - tl * kappa,  y0,
                 x0 + tl,               y0);
    }
    close();
    return *this;
}

Path::Builder& Path::Builder::add_ellipse(const Rect& r) {
    float cx = r.mid_x();
    float cy = r.mid_y();
    float rx = r.width() * 0.5f;
    float ry = r.height() * 0.5f;
    float kx = rx * kappa;
    float ky = ry * kappa;

    move_to(cx + rx, cy);
    cubic_to(cx + rx, cy + ky, cx + kx, cy + ry, cx,      cy + ry);
    cubic_to(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    cubic_to(cx - rx, cy - ky, cx - kx, cy - ry, cx,      cy - ry);
    cubic_to(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    close();
    return *this;
}

Path::Builder& Path::Builder::add_circle(Point center, float radius) {
    return add_ellipse(Rect{center.x - radius, center.y - radius,
                            radius * 2.0f, radius * 2.0f});
}

Path::Builder& Path::Builder::add_arc(Point center, float radius,
                                       float start, float end, bool clockwise) {
    float sa = start;
    float ea = end;

    // Normalize arc direction.
    if (clockwise) {
        if (ea > sa) ea -= two_pi;
    } else {
        if (ea < sa) ea += two_pi;
    }

    float delta = ea - sa;
    int n_segs = static_cast<int>(std::ceil(std::abs(delta) / half_pi));
    if (n_segs < 1) n_segs = 1;
    float seg = delta / n_segs;

    float t = std::tan(seg * 0.5f);
    float alpha_f = std::sin(seg) * (std::sqrt(4.0f + 3.0f * t * t) - 1.0f) / 3.0f;

    float theta = sa;
    float prev_cos = std::cos(theta);
    float prev_sin = std::sin(theta);

    float px = center.x + radius * prev_cos;
    float py = center.y + radius * prev_sin;
    move_to(px, py);

    for (int i = 0; i < n_segs; i++) {
        float next_theta = theta + seg;
        float next_cos = std::cos(next_theta);
        float next_sin = std::sin(next_theta);

        float x0 = center.x + radius * prev_cos;
        float y0 = center.y + radius * prev_sin;
        float dx0 = -radius * prev_sin;
        float dy0 =  radius * prev_cos;

        float x3 = center.x + radius * next_cos;
        float y3 = center.y + radius * next_sin;
        float dx3 = -radius * next_sin;
        float dy3 =  radius * next_cos;

        cubic_to(x0 + alpha_f * dx0, y0 + alpha_f * dy0,
                 x3 - alpha_f * dx3, y3 - alpha_f * dy3,
                 x3, y3);

        theta = next_theta;
        prev_cos = next_cos;
        prev_sin = next_sin;
    }

    return *this;
}

Path::Builder& Path::Builder::add_path(const Path& path, const AffineTransform* t) {
    auto* pi = path_impl(path);
    if (!pi || !m_impl) return *this;

    if (!t || t->is_identity()) {
        m_impl->commands.insert(m_impl->commands.end(),
                                pi->commands.begin(), pi->commands.end());
        m_impl->points.insert(m_impl->points.end(),
                              pi->points.begin(), pi->points.end());
    } else {
        m_impl->commands.insert(m_impl->commands.end(),
                                pi->commands.begin(), pi->commands.end());
        size_t old_size = m_impl->points.size();
        m_impl->points.resize(old_size + pi->points.size());
        t->apply(pi->points.data(), m_impl->points.data() + old_size,
                 static_cast<int>(pi->points.size()));
    }

    m_impl->num_contours += pi->num_contours;
    m_impl->num_curves   += pi->num_curves;

    if (!pi->points.empty()) {
        m_impl->has_move = true;
        // Update current point.
        Point last = pi->points.back();
        if (t && !t->is_identity())
            last = t->apply(last);
        m_impl->current = last;
        // Find subpath start from added path.
        int pt = 0;
        Point last_move{};
        for (auto cmd : pi->commands) {
            if (cmd == PathElement::MoveToPoint)
                last_move = pi->points[pt];
            pt += points_per_command(cmd);
        }
        if (t && !t->is_identity())
            last_move = t->apply(last_move);
        m_impl->subpath_start = last_move;
    }

    return *this;
}

Path::Builder& Path::Builder::move_to_relative(float dx, float dy) {
    if (!m_impl) return *this;
    return move_to(m_impl->current.x + dx, m_impl->current.y + dy);
}

Path::Builder& Path::Builder::line_to_relative(float dx, float dy) {
    if (!m_impl) return *this;
    return line_to(m_impl->current.x + dx, m_impl->current.y + dy);
}

Path::Builder& Path::Builder::quad_to_relative(float dcpx, float dcpy,
                                                float dx, float dy) {
    if (!m_impl) return *this;
    float cx = m_impl->current.x;
    float cy = m_impl->current.y;
    return quad_to(cx + dcpx, cy + dcpy, cx + dx, cy + dy);
}

Path::Builder& Path::Builder::cubic_to_relative(float dcp1x, float dcp1y,
                                                  float dcp2x, float dcp2y,
                                                  float dx, float dy) {
    if (!m_impl) return *this;
    float cx = m_impl->current.x;
    float cy = m_impl->current.y;
    return cubic_to(cx + dcp1x, cy + dcp1y, cx + dcp2x, cy + dcp2y, cx + dx, cy + dy);
}

Path::Builder& Path::Builder::reserve(int elements) {
    if (!m_impl || elements <= 0) return *this;
    m_impl->commands.reserve(static_cast<size_t>(elements));
    m_impl->points.reserve(static_cast<size_t>(elements) * 3); // worst case: all cubics
    return *this;
}

Path::Builder& Path::Builder::transform(const AffineTransform& t) {
    if (!m_impl || t.is_identity()) return *this;
    for (auto& p : m_impl->points)
        p = t.apply(p);
    m_impl->current = t.apply(m_impl->current);
    m_impl->subpath_start = t.apply(m_impl->subpath_start);
    return *this;
}

Point Path::Builder::current_point() const {
    return m_impl ? m_impl->current : Point{};
}

bool Path::Builder::is_empty() const {
    return !m_impl || m_impl->commands.empty();
}

Path Path::Builder::build() {
    Path p;
    if (!m_impl || m_impl->commands.empty())
        return p;

    p.m_impl = alloc_impl();
    if (!p.m_impl) return p;

    p.m_impl->own(std::move(m_impl->commands), std::move(m_impl->points));
    p.m_impl->num_contours = m_impl->num_contours;
    p.m_impl->num_curves   = m_impl->num_curves;
    if (!p.m_impl->points.empty())
        p.m_impl->start_point = p.m_impl->points[0];

    // Reset builder.
    m_impl->commands.clear();
    m_impl->points.clear();
    m_impl->num_contours = 0;
    m_impl->num_curves   = 0;
    m_impl->has_move = false;
    m_impl->current = {};
    m_impl->subpath_start = {};

    return p;
}

} // namespace kvg
