#include "polyclip_internal.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <unordered_map>

// ===== Public API (Point, Rect, Contour, Polygon, compute) =================

namespace polyclip {

double Point::distance_sq(const Point& p) const noexcept
{
    double dx = x - p.x;
    double dy = y - p.y;
    return dx * dx + dy * dy;
}

bool Point::is_finite() const noexcept
{
    return std::isfinite(x) && std::isfinite(y);
}

bool Rect::overlaps(const Rect& r) const noexcept
{
    return !(x1 > r.x2 || r.x1 > x2 || y1 > r.y2 || r.y1 > y2);
}

bool Rect::contains(const Rect& r) const noexcept
{
    return x1 <= r.x1 && y1 <= r.y1 && x2 >= r.x2 && y2 >= r.y2;
}

Contour::Contour(std::vector<Point> points)
    : m_points(std::move(points)) {}

void Contour::clear()
{
    m_points.clear();
    m_holes.clear();
    m_hole = false;
    m_cc_cached = false;
}

Rect Contour::bbox() const noexcept
{
    if (m_points.empty())
        return {};
    Rect r{m_points[0].x, m_points[0].y, m_points[0].x, m_points[0].y};
    for (auto& p : m_points) {
        r.x1 = std::min(r.x1, p.x);
        r.y1 = std::min(r.y1, p.y);
        r.x2 = std::max(r.x2, p.x);
        r.y2 = std::max(r.y2, p.y);
    }
    return r;
}

double Contour::signed_area() const noexcept
{
    double area = 0.0;
    auto n = m_points.size();
    if (n < 3)
        return 0.0;
    // Shoelace without modulo: pair (n-1, 0) handled separately.
    for (std::size_t i = 0; i < n - 1; i++) {
        area += m_points[i].x * m_points[i + 1].y;
        area -= m_points[i + 1].x * m_points[i].y;
    }
    area += m_points[n - 1].x * m_points[0].y;
    area -= m_points[0].x * m_points[n - 1].y;
    return area * 0.5;
}

bool Contour::counter_clockwise() const
{
    if (m_cc_cached)
        return m_cc;
    m_cc_cached = true;
    // Strict > 0: degenerate (collinear) contours are not classified as CCW.
    m_cc = signed_area() > 0.0;
    return m_cc;
}

void Contour::set_clockwise()
{
    if (counter_clockwise())
        reverse();
}

void Contour::set_counter_clockwise()
{
    if (!counter_clockwise())
        reverse();
}

void Contour::reverse()
{
    std::ranges::reverse(m_points);
    if (m_cc_cached)
        m_cc = !m_cc;
}

std::size_t Contour::sanitize()
{
    if (m_points.size() < 2)
        return 0;

    std::size_t removed = 0;

    // Remove non-finite points
    auto it = std::remove_if(m_points.begin(), m_points.end(),
                             [](const Point& p) { return !p.is_finite(); });
    removed += static_cast<std::size_t>(m_points.end() - it);
    m_points.erase(it, m_points.end());

    // Remove consecutive duplicates (including wrap-around)
    if (m_points.size() >= 2) {
        std::vector<Point> clean;
        clean.reserve(m_points.size());
        clean.push_back(m_points[0]);
        for (std::size_t i = 1; i < m_points.size(); i++) {
            if (!(m_points[i] == m_points[i - 1]))
                clean.push_back(m_points[i]);
            else
                removed++;
        }
        // Check wrap-around duplicate
        if (clean.size() >= 2 && clean.front() == clean.back()) {
            clean.pop_back();
            removed++;
        }
        m_points = std::move(clean);
    }

    if (removed > 0)
        m_cc_cached = false;

    return removed;
}

std::size_t Polygon::vertex_count() const noexcept
{
    std::size_t n = 0;
    for (auto& c : m_contours)
        n += c.size();
    return n;
}

Rect Polygon::bbox() const noexcept
{
    if (m_contours.empty())
        return {};
    auto r = m_contours[0].bbox();
    for (std::size_t i = 1; i < m_contours.size(); i++) {
        auto b = m_contours[i].bbox();
        r.x1 = std::min(r.x1, b.x1);
        r.y1 = std::min(r.y1, b.y1);
        r.x2 = std::max(r.x2, b.x2);
        r.y2 = std::max(r.y2, b.y2);
    }
    return r;
}

double Polygon::area() const noexcept
{
    double a = 0.0;
    for (auto& c : m_contours)
        a += c.signed_area();
    return a;
}

std::size_t Polygon::sanitize()
{
    for (auto& c : m_contours)
        c.sanitize();

    auto it = std::remove_if(m_contours.begin(), m_contours.end(),
                             [](const Contour& c) { return c.size() < 3; });
    auto removed = static_cast<std::size_t>(m_contours.end() - it);
    m_contours.erase(it, m_contours.end());
    return removed;
}

Polygon compute(Operation op, const Polygon& subject, const Polygon& clip, FillRule rule)
{
    Polygon result;
    detail::SweepLine sweep(subject, clip, rule);
    sweep.compute(op, result);
    return result;
}

// -- validate() uses a dedicated sweep that does NOT need FillRule -----------

namespace {

struct VEvent;

struct VSegComp {
    bool operator()(const VEvent* e1, const VEvent* e2) const;
};

struct VEvent {
    Point p;
    bool left;
    std::size_t edge_idx;
    VEvent* other = nullptr;
    mutable std::set<const VEvent*, VSegComp>::iterator pos;
    mutable bool pos_valid = false;

    detail::Segment segment() const { return {p, other->p}; }
    bool below(const Point& x) const {
        return left ? detail::signed_area(p, other->p, x) > 0.0
                    : detail::signed_area(other->p, p, x) > 0.0;
    }
    bool above(const Point& x) const { return !below(x); }
};

struct VEventComp {
    bool operator()(const VEvent* a, const VEvent* b) const {
        if (a == b) return false;
        if (a->p.x < b->p.x) return true;
        if (b->p.x < a->p.x) return false;
        if (a->p != b->p) return a->p.y < b->p.y;
        if (a->left != b->left) return !a->left;
        return a < b;
    }
};

bool VSegComp::operator()(const VEvent* e1, const VEvent* e2) const
{
    return detail::segment_order<VEvent, VEventComp>(e1, e2);
}

} // anonymous namespace

bool Polygon::validate() const
{
    for (auto& contour : m_contours) {
        auto pts = contour.points();
        auto n = pts.size();
        if (n < 3)
            continue;

        std::deque<VEvent> pool;
        std::vector<VEvent*> eq;
        eq.reserve(n * 2);

        for (std::size_t i = 0; i < n; i++) {
            auto j = (i + 1) % n;
            if (pts[i] == pts[j])
                continue;
            if (pts[i].distance_sq(pts[j]) < detail::SnapDistSq)
                continue;
            VEvent ev1{}; ev1.p = pts[i]; ev1.left = true;  ev1.edge_idx = i;
            VEvent ev2{}; ev2.p = pts[j]; ev2.left = true;  ev2.edge_idx = i;
            pool.push_back(ev1);
            pool.push_back(ev2);
            auto* e1 = &pool[pool.size() - 2];
            auto* e2 = &pool[pool.size() - 1];
            e1->other = e2;
            e2->other = e1;
            if (e1->p.x < e2->p.x || (e1->p.x == e2->p.x && e1->p.y < e2->p.y)) {
                e2->left = false;
            } else {
                e1->left = false;
            }
            eq.push_back(e1);
            eq.push_back(e2);
        }

        std::sort(eq.begin(), eq.end(), VEventComp{});

        std::set<const VEvent*, VSegComp> S;

        auto adjacent = [n](std::size_t a, std::size_t b) -> bool {
            if (a > b) std::swap(a, b);
            return (b - a == 1) || (a == 0 && b == n - 1);
        };

        auto check_pair = [&](std::set<const VEvent*, VSegComp>::iterator it_a,
                              std::set<const VEvent*, VSegComp>::iterator it_b) -> bool {
            if (it_a == S.end() || it_b == S.end())
                return true;
            auto* ea = *it_a;
            auto* eb = *it_b;
            if (adjacent(ea->edge_idx, eb->edge_idx))
                return true;

            Point ip0, ip1;
            int ni = detail::find_intersection(ea->segment(), eb->segment(), ip0, ip1);
            return ni == 0;
        };

        bool valid = true;
        for (auto* e : eq) {
            if (!valid)
                break;
            if (e->left) {
                auto [it, _] = S.insert(e);
                e->pos = it;
                e->pos_valid = true;
                auto next = std::next(it);
                auto prev = (it != S.begin()) ? std::prev(it) : S.end();
                if (!check_pair(it, next)) { valid = false; break; }
                if (prev != S.end() && !check_pair(prev, it)) { valid = false; break; }
            } else {
                if (!e->other->pos_valid)
                    continue;
                auto sli = e->other->pos;
                e->other->pos_valid = false;

                auto next = std::next(sli);
                auto prev = (sli != S.begin()) ? std::prev(sli) : S.end();
                if (prev != S.end() && next != S.end()) {
                    if (!check_pair(prev, next)) { valid = false; break; }
                }
                S.erase(sli);
            }
        }

        if (!valid)
            return false;
    }
    return true;
}

// -- decompose() : split self-intersecting contours into simple contours ----

void Polygon::decompose(FillRule rule)
{
    std::vector<Contour> result;

    for (auto& contour : m_contours) {
        if (contour.size() < 4) {
            result.push_back(std::move(contour));
            continue;
        }

        auto pts = contour.points();
        auto n = pts.size();

        // Step 1: Find all self-intersections using sweep
        struct ISect {
            std::size_t edge_a, edge_b;
            Point pt;
        };
        std::vector<ISect> isects;

        std::deque<VEvent> pool;
        std::vector<VEvent*> eq;
        eq.reserve(n * 2);

        for (std::size_t i = 0; i < n; i++) {
            auto j = (i + 1) % n;
            if (pts[i] == pts[j])
                continue;
            if (pts[i].distance_sq(pts[j]) < detail::SnapDistSq)
                continue;
            VEvent ev1{}; ev1.p = pts[i]; ev1.left = true;  ev1.edge_idx = i;
            VEvent ev2{}; ev2.p = pts[j]; ev2.left = true;  ev2.edge_idx = i;
            pool.push_back(ev1);
            pool.push_back(ev2);
            auto* e1 = &pool[pool.size() - 2];
            auto* e2 = &pool[pool.size() - 1];
            e1->other = e2;
            e2->other = e1;
            if (e1->p.x < e2->p.x || (e1->p.x == e2->p.x && e1->p.y < e2->p.y)) {
                e2->left = false;
            } else {
                e1->left = false;
            }
            eq.push_back(e1);
            eq.push_back(e2);
        }

        std::sort(eq.begin(), eq.end(), VEventComp{});

        std::set<const VEvent*, VSegComp> S;

        auto adjacent_edges = [n](std::size_t a, std::size_t b) -> bool {
            if (a > b) std::swap(a, b);
            return (b - a == 1) || (a == 0 && b == n - 1);
        };

        auto check_and_collect = [&](std::set<const VEvent*, VSegComp>::iterator it_a,
                                     std::set<const VEvent*, VSegComp>::iterator it_b) {
            if (it_a == S.end() || it_b == S.end())
                return;
            auto* ea = *it_a;
            auto* eb = *it_b;
            if (adjacent_edges(ea->edge_idx, eb->edge_idx))
                return;

            Point ip0, ip1;
            int ni = detail::find_intersection(ea->segment(), eb->segment(), ip0, ip1);
            if (ni >= 1)
                isects.push_back({ea->edge_idx, eb->edge_idx, ip0});
            if (ni == 2)
                isects.push_back({ea->edge_idx, eb->edge_idx, ip1});
        };

        for (auto* e : eq) {
            if (e->left) {
                auto [it, _] = S.insert(e);
                e->pos = it;
                e->pos_valid = true;
                auto next_it = std::next(it);
                auto prev_it = (it != S.begin()) ? std::prev(it) : S.end();
                check_and_collect(it, next_it);
                if (prev_it != S.end())
                    check_and_collect(prev_it, it);
            } else {
                if (!e->other->pos_valid)
                    continue;
                auto sli = e->other->pos;
                e->other->pos_valid = false;
                auto next_it = std::next(sli);
                auto prev_it = (sli != S.begin()) ? std::prev(sli) : S.end();
                if (prev_it != S.end() && next_it != S.end())
                    check_and_collect(prev_it, next_it);
                S.erase(sli);
            }
        }

        if (isects.empty()) {
            result.push_back(std::move(contour));
            continue;
        }

        // Step 2: Insert intersection points into edges
        struct EdgeSplit {
            double t;
            Point pt;
        };
        std::vector<std::vector<EdgeSplit>> edge_splits(n);

        for (auto& isect : isects) {
            auto add_split = [&](std::size_t edge_idx, const Point& ip) {
                auto& p0 = pts[edge_idx];
                auto next = (edge_idx + 1) % n;
                auto& p1 = pts[next];
                double dx = p1.x - p0.x;
                double dy = p1.y - p0.y;
                double t;
                if (std::abs(dx) > std::abs(dy))
                    t = (ip.x - p0.x) / dx;
                else if (std::abs(dy) > 0)
                    t = (ip.y - p0.y) / dy;
                else
                    t = 0;
                t = std::clamp(t, 0.0, 1.0);
                if (t > 1e-9 && t < 1.0 - 1e-9)
                    edge_splits[edge_idx].push_back({t, ip});
            };
            add_split(isect.edge_a, isect.pt);
            add_split(isect.edge_b, isect.pt);
        }

        std::vector<Point> split_pts;
        split_pts.reserve(n + isects.size() * 2);

        for (std::size_t i = 0; i < n; i++) {
            split_pts.push_back(pts[i]);
            if (!edge_splits[i].empty()) {
                auto& splits = edge_splits[i];
                std::sort(splits.begin(), splits.end(),
                          [](const EdgeSplit& a, const EdgeSplit& b) { return a.t < b.t; });
                for (auto& sp : splits)
                    split_pts.push_back(sp.pt);
            }
        }

        // Step 3: Extract simple loops from the split vertex sequence.
        // At intersection nodes (vertices that appear more than once),
        // we can "short-circuit" the traversal to extract sub-loops.
        //
        // Algorithm: walk the vertex sequence; when we encounter a
        // vertex we've seen before, extract the loop between the two
        // occurrences and continue with the remaining vertices.

        // Build index of first occurrence for each snapped point
        auto snap_pt = [](const Point& p) -> Point {
            return detail::snap_to_grid(p);
        };

        // Use repeated traversals to extract all simple loops
        std::vector<std::vector<Point>> loops;
        std::vector<Point> current;
        // Map from snapped point to index in 'current'
        std::unordered_map<Point, std::size_t, PointHash> seen;

        for (std::size_t i = 0; i < split_pts.size(); i++) {
            Point sp = snap_pt(split_pts[i]);

            auto it2 = seen.find(sp);
            if (it2 != seen.end()) {
                // Found a repeated vertex: extract the loop from the
                // first occurrence to here.
                std::size_t loop_start = it2->second;
                std::vector<Point> loop(current.begin() + loop_start, current.end());

                if (loop.size() >= 3)
                    loops.push_back(std::move(loop));

                // Remove the loop from 'current', but keep the
                // intersection vertex so it can form further loops.
                current.resize(loop_start + 1);

                // Rebuild 'seen' for the remaining prefix
                seen.clear();
                for (std::size_t j = 0; j < current.size(); j++)
                    seen[snap_pt(current[j])] = j;
            } else {
                seen[sp] = current.size();
                current.push_back(split_pts[i]);
            }
        }

        // Check if the remaining path connects back to the start
        if (current.size() >= 3) {
            Point first = snap_pt(current.front());
            Point last = snap_pt(current.back());
            if (first == last) {
                current.pop_back();
                if (current.size() >= 3)
                    loops.push_back(std::move(current));
            } else {
                // Try to close: the original contour is closed, so the
                // remaining vertices should form a closed loop.
                if (current.size() >= 3)
                    loops.push_back(std::move(current));
            }
        }

        // Apply fill rule to determine which loops to keep
        if (rule == FillRule::EvenOdd) {
            // For even-odd, keep all simple loops
            for (auto& loop : loops)
                result.emplace_back(std::move(loop));
        } else {
            // For non-zero, keep loops whose winding direction matches
            // the original contour (they represent filled regions).
            // Opposite-winding loops represent cancellation regions
            // that should be discarded for non-zero fill.
            bool orig_ccw = contour.counter_clockwise();
            for (auto& loop : loops) {
                Contour c(std::move(loop));
                if (c.counter_clockwise() == orig_ccw) {
                    result.push_back(std::move(c));
                }
                // Opposite-winding loops are cancellation artifacts
                // under non-zero fill rule — drop them.
            }
        }

        // Fallback: if decomposition produced nothing, keep original
        if (loops.empty())
            result.push_back(std::move(contour));
    }

    m_contours = std::move(result);
}

} // namespace polyclip

// ===== Intersection & sweep event comparators ==============================

namespace polyclip::detail {

static int find_overlap(double u0, double u1, double v0, double v1, double w[2])
{
    if (u1 < v0 || u0 > v1)
        return 0;
    if (u1 > v0) {
        if (u0 < v1) {
            w[0] = std::max(u0, v0);
            w[1] = std::min(u1, v1);
            return 2;
        }
        w[0] = u0;
        return 1;
    }
    w[0] = u1;
    return 1;
}

static void snap_to_endpoint(Point& ip, const Segment& s0, const Segment& s1)
{
    if (ip.distance_sq(s0.begin()) < SnapDistSq) ip = s0.begin();
    else if (ip.distance_sq(s0.end()) < SnapDistSq) ip = s0.end();
    else if (ip.distance_sq(s1.begin()) < SnapDistSq) ip = s1.begin();
    else if (ip.distance_sq(s1.end()) < SnapDistSq) ip = s1.end();
    else ip = snap_to_grid(ip);
}

int find_intersection(const Segment& s0, const Segment& s1, Point& ip0, Point& ip1)
{
    Point d0{s0.end().x - s0.begin().x, s0.end().y - s0.begin().y};
    Point d1{s1.end().x - s1.begin().x, s1.end().y - s1.begin().y};
    Point E{s1.begin().x - s0.begin().x, s1.begin().y - s0.begin().y};

    double kross = d0.x * d1.y - d0.y * d1.x;
    double sqr_kross = kross * kross;
    double sqr_len0 = d0.x * d0.x + d0.y * d0.y;
    double sqr_len1 = d1.x * d1.x + d1.y * d1.y;

    double sqr_eps = GeomEpsilon * GeomEpsilon;

    if (sqr_kross > sqr_eps * sqr_len0 * sqr_len1) {
        double s = (E.x * d1.y - E.y * d1.x) / kross;
        if (s < 0.0 || s > 1.0)
            return 0;
        double t = (E.x * d0.y - E.y * d0.x) / kross;
        if (t < 0.0 || t > 1.0)
            return 0;
        ip0.x = s0.begin().x + s * d0.x;
        ip0.y = s0.begin().y + s * d0.y;
        snap_to_endpoint(ip0, s0, s1);
        return 1;
    }

    double sqr_len_e = E.x * E.x + E.y * E.y;
    kross = E.x * d0.y - E.y * d0.x;
    sqr_kross = kross * kross;
    if (sqr_kross > sqr_eps * sqr_len0 * sqr_len_e)
        return 0;

    double s0_param = (d0.x * E.x + d0.y * E.y) / sqr_len0;
    double s1_param = s0_param + (d0.x * d1.x + d0.y * d1.y) / sqr_len0;
    double smin = std::min(s0_param, s1_param);
    double smax = std::max(s0_param, s1_param);
    double w[2];
    int imax = find_overlap(0.0, 1.0, smin, smax, w);

    if (imax > 0) {
        ip0.x = s0.begin().x + w[0] * d0.x;
        ip0.y = s0.begin().y + w[0] * d0.y;
        snap_to_endpoint(ip0, s0, s1);
        if (imax > 1) {
            ip1.x = s0.begin().x + w[1] * d0.x;
            ip1.y = s0.begin().y + w[1] * d0.y;
            snap_to_endpoint(ip1, s0, s1);
            // Collapse degenerate overlap (touching at a single point) to imax=1.
            if (ip0 == ip1)
                imax = 1;
        }
    }
    return imax;
}

bool SweepEvent::below(const Point& x) const
{
    return left ? signed_area(p, other->p, x) > 0.0
                : signed_area(other->p, p, x) > 0.0;
}

bool SweepEventComp::operator()(const SweepEvent* e1, const SweepEvent* e2) const
{
    if (e1 == e2) return false;
    if (e1->p.x > e2->p.x) return true;
    if (e2->p.x > e1->p.x) return false;
    if (e1->p != e2->p) return e1->p.y > e2->p.y;
    if (e1->left != e2->left) return e1->left;
    if (e1->above(e2->other->p) != e2->above(e1->other->p))
        return e1->above(e2->other->p);
    return e1 > e2;
}

bool SegmentComp::operator()(const SweepEvent* e1, const SweepEvent* e2) const
{
    return segment_order<SweepEvent, SweepEventComp>(e1, e2);
}

// ===== Connector ===========================================================

void PointChain::init(const Segment& s)
{
    m_list.push_back(s.begin());
    m_list.push_back(s.end());
}

bool PointChain::link_segment(const Segment& s)
{
    if (point_near(s.begin(), m_list.front())) {
        if (point_near(s.end(), m_list.back()))
            m_closed = true;
        else
            m_list.push_front(s.end());
        return true;
    }
    if (point_near(s.end(), m_list.back())) {
        if (point_near(s.begin(), m_list.front()))
            m_closed = true;
        else
            m_list.push_back(s.begin());
        return true;
    }
    if (point_near(s.end(), m_list.front())) {
        if (point_near(s.begin(), m_list.back()))
            m_closed = true;
        else
            m_list.push_front(s.begin());
        return true;
    }
    if (point_near(s.begin(), m_list.back())) {
        if (point_near(s.end(), m_list.front()))
            m_closed = true;
        else
            m_list.push_back(s.end());
        return true;
    }
    return false;
}

bool PointChain::link_chain(PointChain& chain)
{
    if (point_near(chain.m_list.front(), m_list.back())) {
        chain.m_list.pop_front();
        m_list.splice(m_list.end(), chain.m_list);
        return true;
    }
    if (point_near(chain.m_list.back(), m_list.front())) {
        m_list.pop_front();
        m_list.splice(m_list.begin(), chain.m_list);
        return true;
    }
    if (point_near(chain.m_list.front(), m_list.front())) {
        m_list.pop_front();
        chain.m_list.reverse();
        m_list.splice(m_list.begin(), chain.m_list);
        return true;
    }
    if (point_near(chain.m_list.back(), m_list.back())) {
        m_list.pop_back();
        chain.m_list.reverse();
        m_list.splice(m_list.end(), chain.m_list);
        return true;
    }
    return false;
}

// -- Spatial endpoint map implementation ------------------------------------

Connector::FindResult Connector::find_endpoint(const Point& p) const
{
    auto c = to_cell(p);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            CellKey k{c.ix + dx, c.iy + dy};
            auto it = m_cells.find(k);
            if (it == m_cells.end())
                continue;
            for (auto& entry : it->second) {
                if (point_near(entry.pt, p))
                    return {true, entry.chain};
            }
        }
    }
    return {};
}

void Connector::insert_endpoint(const Point& p, ChainIter ci)
{
    m_cells[to_cell(p)].push_back({p, ci});
}

void Connector::erase_endpoint(const Point& p, ChainIter ci)
{
    auto c = to_cell(p);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            CellKey k{c.ix + dx, c.iy + dy};
            auto it = m_cells.find(k);
            if (it == m_cells.end())
                continue;
            auto& vec = it->second;
            for (auto vi = vec.begin(); vi != vec.end(); ++vi) {
                if (vi->chain == ci && point_near(vi->pt, p)) {
                    vec.erase(vi);
                    if (vec.empty())
                        m_cells.erase(it);
                    return;
                }
            }
        }
    }
}

void Connector::update_endpoints(ChainIter it)
{
    insert_endpoint(it->front(), it);
    insert_endpoint(it->back(), it);
}

void Connector::remove_endpoints(ChainIter it)
{
    erase_endpoint(it->front(), it);
    erase_endpoint(it->back(), it);
}

void Connector::add(const Segment& s)
{
    // Guard: ignore degenerate segments
    if (s.degenerate())
        return;

    auto res_a = find_endpoint(s.begin());
    auto res_b = find_endpoint(s.end());

    bool found_a = res_a.found;
    bool found_b = res_b.found;

    if (!found_a && !found_b) {
        m_open.emplace_back();
        auto it = std::prev(m_open.end());
        it->init(s);
        update_endpoints(it);
        return;
    }

    if (found_a && found_b && res_a.chain == res_b.chain) {
        auto j = res_a.chain;
        remove_endpoints(j);
        j->link_segment(s);
        m_closed.splice(m_closed.end(), m_open, j);
        return;
    }

    if (found_a && !found_b) {
        auto j = res_a.chain;
        remove_endpoints(j);
        j->link_segment(s);
        if (j->closed()) {
            m_closed.splice(m_closed.end(), m_open, j);
        } else {
            update_endpoints(j);
        }
        return;
    }

    if (!found_a && found_b) {
        auto j = res_b.chain;
        remove_endpoints(j);
        j->link_segment(s);
        if (j->closed()) {
            m_closed.splice(m_closed.end(), m_open, j);
        } else {
            update_endpoints(j);
        }
        return;
    }

    auto j = res_a.chain;
    auto k = res_b.chain;
    remove_endpoints(j);
    remove_endpoints(k);

    j->link_segment(s);
    if (j->closed()) {
        m_closed.splice(m_closed.end(), m_open, j);
        // k's endpoints were already removed but k still exists in m_open;
        // re-register them so k remains consistent.
        update_endpoints(k);
    } else if (j->link_chain(*k)) {
        m_open.erase(k);
        if (j->closed()) {
            m_closed.splice(m_closed.end(), m_open, j);
        } else {
            update_endpoints(j);
        }
    } else {
        update_endpoints(j);
        update_endpoints(k);
    }
}

void Connector::to_polygon(Polygon& p)
{
    m_open.clear();

    for (auto& chain : m_closed) {
        // Skip degenerate chains
        if (chain.size() < 3)
            continue;

        Contour c;
        Point prev{std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::quiet_NaN()};
        for (auto& pt : chain) {
            if (!point_near(pt, prev))
                c.add(pt);
            prev = pt;
        }
        // Remove wrap-around near-duplicate
        if (c.size() >= 2 && point_near(c.vertex(0), c.vertex(c.size() - 1)))
            c.pop_back();
        if (c.size() >= 3)
            p.add(std::move(c));
    }
}

// ===== Sweep line ==========================================================

// Winding delta is computed in process_segment() from the original
// edge direction and propagated through divide_segment().

bool SweepLine::contributes(const SweepEvent* e, Operation op) const noexcept
{
    bool is_inside = e->inside;

    switch (op) {
    case Operation::Intersection:
        return is_inside;
    case Operation::Union:
        return !is_inside;
    case Operation::Difference:
        return (e->pl == PolyLabel::Subject) ? !is_inside : is_inside;
    case Operation::Xor:
        return true;
    }
    return false;
}

void SweepLine::compute(Operation op, Polygon& result)
{
    if (m_subject.contour_count() * m_clipping.contour_count() == 0) {
        if (op == Operation::Difference)
            result = m_subject;
        if (op == Operation::Union || op == Operation::Xor)
            result = m_subject.empty() ? m_clipping : m_subject;
        return;
    }

    auto bbox_s = m_subject.bbox();
    auto bbox_c = m_clipping.bbox();
    if (!bbox_s.overlaps(bbox_c)) {
        if (op == Operation::Difference)
            result = m_subject;
        if (op == Operation::Union || op == Operation::Xor) {
            result = m_subject;
            for (auto& c : m_clipping)
                result.add(c);
        }
        return;
    }

    // Fast path: axis-aligned rectangle containment
    auto is_axis_rect = [](const Polygon& p) -> bool {
        if (p.contour_count() != 1 || p[0].size() != 4)
            return false;
        auto bb = p[0].bbox();
        bool corners[4]{};
        for (std::size_t i = 0; i < 4; i++) {
            auto& v = p[0].vertex(i);
            if      (v.x == bb.x1 && v.y == bb.y1) corners[0] = true;
            else if (v.x == bb.x2 && v.y == bb.y1) corners[1] = true;
            else if (v.x == bb.x2 && v.y == bb.y2) corners[2] = true;
            else if (v.x == bb.x1 && v.y == bb.y2) corners[3] = true;
            else return false;
        }
        return corners[0] && corners[1] && corners[2] && corners[3];
    };

    bool subj_is_rect = is_axis_rect(m_subject);
    bool clip_is_rect = is_axis_rect(m_clipping);

    if (op == Operation::Intersection) {
        if (subj_is_rect && bbox_s.contains(bbox_c)) { result = m_clipping; return; }
        if (clip_is_rect && bbox_c.contains(bbox_s)) { result = m_subject;  return; }
    }
    if (op == Operation::Union) {
        if (subj_is_rect && bbox_s.contains(bbox_c)) { result = m_subject;  return; }
        if (clip_is_rect && bbox_c.contains(bbox_s)) { result = m_clipping; return; }
    }
    if (op == Operation::Difference) {
        if (clip_is_rect && bbox_c.contains(bbox_s)) { return; }
    }

    for (auto& contour : m_subject) {
        auto pts = contour.points();
        for (std::size_t j = 0; j < pts.size(); j++) {
            auto k = (j + 1) % pts.size();
            process_segment({pts[j], pts[k]}, PolyLabel::Subject);
        }
    }
    for (auto& contour : m_clipping) {
        auto pts = contour.points();
        for (std::size_t j = 0; j < pts.size(); j++) {
            auto k = (j + 1) % pts.size();
            process_segment({pts[j], pts[k]}, PolyLabel::Clipping);
        }
    }

    Connector connector;
    auto snap_seg = [](const Segment& s) -> Segment {
        return {snap_to_grid(s.p1), snap_to_grid(s.p2)};
    };
    StatusLine S;
    StatusLine::iterator it, sli, prev, next;
    SweepEvent* e;
    double min_max_x = std::min(bbox_s.x2, bbox_c.x2);

    while (!m_eq.empty()) {
        if (!check_event_limit()) {
            // Safety valve: too many events, return what we have so far.
            // This prevents infinite loops on pathological input.
            assert(false && "polyclip: sweep event limit exceeded");
            break;
        }

        e = m_eq.top();
        m_eq.pop();

        if ((op == Operation::Intersection && e->p.x > min_max_x) ||
            (op == Operation::Difference && e->p.x > bbox_s.x2)) {
            connector.to_polygon(result);
            return;
        }

        if (op == Operation::Union && e->p.x > min_max_x) {
            if (!e->left)
                connector.add(snap_seg(e->segment()));
            while (!m_eq.empty()) {
                e = m_eq.top();
                m_eq.pop();
                if (!e->left)
                    connector.add(snap_seg(e->segment()));
            }
            connector.to_polygon(result);
            return;
        }

        if (e->left) {
            it = S.insert(e).first;
            e->pos = it;
            e->pos_valid = true;

            next = prev = it;
            (prev != S.begin()) ? --prev : prev = S.end();

            // -- Compute winding counts and inside/in_out --
            // winding_delta was set in process_segment() from the original
            // edge direction.  For edges created by divide_segment(), the
            // delta is inherited from the parent event.

            if (m_fill_rule == FillRule::NonZero) {
                if (prev == S.end()) {
                    e->winding_self = 0;
                    e->winding_other = 0;
                } else {
                    auto* p = *prev;
                    int prev_ws, prev_wo;
                    if (p->pl == e->pl) {
                        prev_ws = p->winding_self + p->winding_delta;
                        prev_wo = p->winding_other;
                    } else {
                        prev_ws = p->winding_other;
                        prev_wo = p->winding_self + p->winding_delta;
                    }
                    e->winding_self = prev_ws;
                    e->winding_other = prev_wo;
                }

                e->in_out = (e->winding_self != 0);
                e->inside = (e->winding_other != 0);
            } else {
                // EvenOdd fill rule
                if (prev == S.end()) {
                    e->inside = e->in_out = false;
                } else if ((*prev)->type != EdgeType::Normal) {
                    if (prev == S.begin()) {
                        e->inside = true;
                        e->in_out = false;
                    } else {
                        sli = prev;
                        --sli;
                        if ((*prev)->pl == e->pl) {
                            e->in_out = !(*prev)->in_out;
                            e->inside = !(*sli)->in_out;
                        } else {
                            e->in_out = !(*sli)->in_out;
                            e->inside = !(*prev)->in_out;
                        }
                    }
                } else if (e->pl == (*prev)->pl) {
                    e->inside = (*prev)->inside;
                    e->in_out = !(*prev)->in_out;
                } else {
                    e->inside = !(*prev)->in_out;
                    e->in_out = (*prev)->inside;
                }
            }

            if ((++next) != S.end())
                possible_intersection(e, *next);
            if (prev != S.end())
                possible_intersection(*prev, e);
        } else {
            // -- Right event: O(1) erasure using stored iterator --
            if (!e->other->pos_valid)
                continue;
            sli = e->other->pos;
            e->other->pos_valid = false;

            next = prev = sli;
            ++next;
            (prev != S.begin()) ? --prev : prev = S.end();

            switch (e->type) {
            case EdgeType::Normal:
                if (contributes(e->other, op))
                    connector.add(snap_seg(e->segment()));
                break;
            case EdgeType::SameTransition:
                if (op == Operation::Intersection || op == Operation::Union)
                    connector.add(snap_seg(e->segment()));
                break;
            case EdgeType::DifferentTransition:
                if (op == Operation::Difference)
                    connector.add(snap_seg(e->segment()));
                break;
            default:
                break;
            }

            S.erase(sli);
            if (next != S.end() && prev != S.end())
                possible_intersection(*prev, *next);
        }
    }
    connector.to_polygon(result);
}

void SweepLine::process_segment(const Segment& s, PolyLabel pl)
{
    Point p1 = snap_to_grid(s.begin());
    Point p2 = snap_to_grid(s.end());

    assert_coord_range(p1);
    assert_coord_range(p2);

    if (p1 == p2)
        return;

    // Compute winding delta from the ORIGINAL edge direction (before
    // reordering endpoints for the sweep).  An edge going upward
    // (y increases in polygon traversal order) contributes +1; downward
    // contributes -1.  Horizontal edges contribute 0 because they cannot
    // cross a horizontal ray used for winding-number computation.
    int original_delta;
    if (p1.y < p2.y)
        original_delta = 1;
    else if (p1.y > p2.y)
        original_delta = -1;
    else
        original_delta = 0;  // horizontal: no winding contribution

    auto* e1 = store(SweepEvent(p1, true, pl, nullptr));
    auto* e2 = store(SweepEvent(p2, true, pl, e1));
    e1->other = e2;

    if (e1->p.x < e2->p.x) {
        e2->left = false;
    } else if (e1->p.x > e2->p.x) {
        e1->left = false;
    } else if (e1->p.y < e2->p.y) {
        e2->left = false;
    } else {
        e1->left = false;
    }

    // Store the original winding delta on the LEFT event so
    // compute() can read it at insertion time.
    e1->winding_delta = original_delta;
    e2->winding_delta = original_delta;

    m_eq.push(e1);
    m_eq.push(e2);
}

void SweepLine::possible_intersection(SweepEvent* e1, SweepEvent* e2)
{
    Point ip1, ip2;
    int ni = find_intersection(e1->segment(), e2->segment(), ip1, ip2);
    if (!ni)
        return;

    if (ni == 1) {
        bool ip1_on_e1 = (e1->p == ip1 || e1->other->p == ip1);
        bool ip1_on_e2 = (e2->p == ip1 || e2->other->p == ip1);
        if (ip1_on_e1 && ip1_on_e2)
            return;
    }

    if (ni == 2 && e1->pl == e2->pl)
        return;

    if (ni == 1) {
        if (e1->p != ip1 && e1->other->p != ip1)
            divide_segment(e1, ip1);
        if (e2->p != ip1 && e2->other->p != ip1)
            divide_segment(e2, ip1);
        return;
    }

    // Overlapping segments (ni == 2).
    std::array<SweepEvent*, 4> sorted{};
    std::size_t n_sorted = 0;
    bool left_shared = (e1->p == e2->p);
    bool right_shared = (e1->other->p == e2->other->p);

    if (left_shared) {
        sorted[n_sorted++] = nullptr;
    } else if (m_sec(e1, e2)) {
        sorted[n_sorted++] = e2;
        sorted[n_sorted++] = e1;
    } else {
        sorted[n_sorted++] = e1;
        sorted[n_sorted++] = e2;
    }

    if (right_shared) {
        sorted[n_sorted++] = nullptr;
    } else if (m_sec(e1->other, e2->other)) {
        sorted[n_sorted++] = e2->other;
        sorted[n_sorted++] = e1->other;
    } else {
        sorted[n_sorted++] = e1->other;
        sorted[n_sorted++] = e2->other;
    }

    assert(n_sorted >= 2 && n_sorted <= 4);

    auto transition = [&]() {
        return (e1->in_out == e2->in_out) ? EdgeType::SameTransition
                                          : EdgeType::DifferentTransition;
    };

    if (n_sorted == 2) {
        e1->type = e1->other->type = EdgeType::NonContributing;
        e2->type = e2->other->type = transition();
        return;
    }

    if (n_sorted == 3) {
        sorted[1]->type = sorted[1]->other->type = EdgeType::NonContributing;
        if (!left_shared)
            sorted[0]->other->type = transition();
        else
            sorted[2]->other->type = transition();
        SweepEvent* to_divide = left_shared ? sorted[2]->other : sorted[0];
        divide_segment(to_divide, sorted[1]->p);
        return;
    }

    // n_sorted == 4: no shared endpoints.
    if (sorted[0] != sorted[3]->other) {
        sorted[1]->type = EdgeType::NonContributing;
        sorted[2]->type = transition();
        divide_segment(sorted[0], sorted[1]->p);
        divide_segment(sorted[1], sorted[2]->p);
        return;
    }

    sorted[1]->type = sorted[1]->other->type = EdgeType::NonContributing;
    divide_segment(sorted[0], sorted[1]->p);
    sorted[3]->other->type = transition();
    divide_segment(sorted[3]->other, sorted[2]->p);
}

void SweepLine::divide_segment(SweepEvent* e, const Point& p)
{
    auto* r = store(SweepEvent(p, false, e->pl, e, e->type));
    auto* l = store(SweepEvent(p, true, e->pl, e->other, e->other->type));

    // Propagate winding_delta to sub-segments: the direction of the
    // original polygon edge is unchanged by splitting.
    r->winding_delta = e->winding_delta;
    l->winding_delta = e->winding_delta;

    if (m_sec(l, e->other)) {
        e->other->left = true;
        l->left = false;
    }

    e->other->other = l;
    e->other = r;
    m_eq.push(l);
    m_eq.push(r);
}

} // namespace polyclip::detail

// ===== Hole classification =================================================

namespace polyclip {

namespace {

struct HoleEvent;

struct HoleSegComp {
    bool operator()(const HoleEvent* e1, const HoleEvent* e2) const;
};

struct HoleEvent {
    Point p;
    bool left;
    std::size_t contour_id;
    HoleEvent* other = nullptr;
    bool in_out = false;
    mutable std::set<HoleEvent*, HoleSegComp>::iterator pos;
    mutable bool pos_valid = false;

    detail::Segment segment() const { return {p, other->p}; }
    bool below(const Point& x) const {
        return left ? detail::signed_area(p, other->p, x) > 0.0
                    : detail::signed_area(other->p, p, x) > 0.0;
    }
    bool above(const Point& x) const { return !below(x); }
};

struct HoleEventComp {
    bool operator()(const HoleEvent* e1, const HoleEvent* e2) const {
        if (e1 == e2) return false;
        if (e1->p.x < e2->p.x) return true;
        if (e2->p.x < e1->p.x) return false;
        if (e1->p != e2->p) return e1->p.y < e2->p.y;
        if (e1->left != e2->left) return !e1->left;
        if (e1->below(e2->other->p) != e2->below(e1->other->p))
            return e1->below(e2->other->p);
        return e1 < e2;
    }
};

bool HoleSegComp::operator()(const HoleEvent* e1, const HoleEvent* e2) const
{
    return detail::segment_order<HoleEvent, HoleEventComp>(e1, e2);
}

} // anonymous namespace

void Polygon::compute_holes()
{
    auto nc = contour_count();
    if (nc < 2) {
        if (nc == 1) {
            m_contours[0].set_hole(false);
            if (!m_contours[0].counter_clockwise())
                m_contours[0].reverse();
        }
        return;
    }

    // Clear previous hole state to make compute_holes() idempotent
    for (auto& c : m_contours) {
        c.set_hole(false);
        c.clear_holes();
    }

    // Compute normalization factors.  compute_holes() only needs
    // topological containment, so we can rescale coordinates per-axis
    // to avoid extreme aspect ratios that break the sweep comparator.
    // Affine scaling preserves containment relationships.
    auto bb = bbox();
    double range_x = bb.x2 - bb.x1;
    double range_y = bb.y2 - bb.y1;
    double cx = (bb.x1 + bb.x2) * 0.5;
    double cy = (bb.y1 + bb.y2) * 0.5;
    double inv_sx = (range_x > 1e-15) ? (1.0 / range_x) : 1.0;
    double inv_sy = (range_y > 1e-15) ? (1.0 / range_y) : 1.0;

    auto normalize = [&](const Point& p) -> Point {
        return {(p.x - cx) * inv_sx, (p.y - cy) * inv_sy};
    };

    std::deque<HoleEvent> ev;
    std::vector<HoleEvent*> evp;
    auto nv = vertex_count();
    evp.reserve(nv * 2);

    for (std::size_t i = 0; i < nc; i++) {
        m_contours[i].set_counter_clockwise();
        auto pts = m_contours[i].points();
        for (std::size_t j = 0; j < pts.size(); j++) {
            auto k = (j + 1) % pts.size();
            Point pj = normalize(pts[j]);
            Point pk = normalize(pts[k]);
            if (pj == pk)
                continue;
            if (pj.x == pk.x)
                continue;
            // Skip near-degenerate edges (in normalized space).
            if (pj.distance_sq(pk) < detail::SnapDistSq)
                continue;

            HoleEvent e1, e2;
            e1.p = pj; e1.left = true; e1.contour_id = i;
            e2.p = pk; e2.left = true; e2.contour_id = i;
            ev.push_back(e1);
            ev.push_back(e2);

            auto* se1 = &ev[ev.size() - 2];
            auto* se2 = &ev[ev.size() - 1];
            se1->other = se2;
            se2->other = se1;

            if (se1->p.x < se2->p.x) {
                se2->left = false;
                se1->in_out = false;
            } else {
                se1->left = false;
                se2->in_out = true;
            }
            evp.push_back(se1);
            evp.push_back(se2);
        }
    }

    std::sort(evp.begin(), evp.end(), HoleEventComp{});

    std::set<HoleEvent*, HoleSegComp> S;
    std::vector<bool> processed(nc, false);
    std::vector<int> depth(nc, 0);
    std::vector<int> parent_outer(nc, -1);
    // Track hole children externally so we don't double-add
    std::vector<std::vector<std::size_t>> hole_children(nc);
    std::size_t nprocessed = 0;

    for (std::size_t i = 0; i < evp.size() && nprocessed < nc; i++) {
        auto* e = evp[i];

        if (e->left) {
            e->pos = S.insert(e).first;
            e->pos_valid = true;

            if (!processed[e->contour_id]) {
                processed[e->contour_id] = true;
                nprocessed++;
                auto cid = e->contour_id;

                auto prev_it = e->pos;
                if (prev_it == S.begin()) {
                    depth[cid] = 0;
                    parent_outer[cid] = -1;
                    m_contours[cid].set_counter_clockwise();
                } else {
                    --prev_it;
                    auto pcid = (*prev_it)->contour_id;
                    if (!(*prev_it)->in_out) {
                        depth[cid] = depth[pcid] + 1;
                        if (depth[cid] % 2 == 1) {
                            parent_outer[cid] = static_cast<int>(pcid);
                        } else {
                            parent_outer[cid] = -1;
                        }
                    } else {
                        depth[cid] = depth[pcid];
                        parent_outer[cid] = parent_outer[pcid];
                    }

                    bool is_hole = (depth[cid] % 2 == 1);
                    if (is_hole) {
                        // Validate parent before accessing
                        if (parent_outer[cid] >= 0
                            && static_cast<std::size_t>(parent_outer[cid]) < nc)
                        {
                            m_contours[cid].set_hole(true);
                            auto pid = static_cast<std::size_t>(parent_outer[cid]);
                            hole_children[pid].push_back(cid);
                            if (m_contours[pid].counter_clockwise())
                                m_contours[cid].set_clockwise();
                            else
                                m_contours[cid].set_counter_clockwise();
                        } else {
                            // Orphan hole: no valid parent found.
                            // Treat as outer contour (defensive).
                            depth[cid] = 0;
                            m_contours[cid].set_counter_clockwise();
                        }
                    } else {
                        m_contours[cid].set_counter_clockwise();
                    }
                }
            }
        } else {
            if (e->other->pos_valid) {
                S.erase(e->other->pos);
                e->other->pos_valid = false;
            }
        }
    }

    // Apply hole registrations
    for (std::size_t i = 0; i < nc; i++) {
        for (auto child : hole_children[i])
            m_contours[i].add_hole(child);
    }
}

} // namespace polyclip
