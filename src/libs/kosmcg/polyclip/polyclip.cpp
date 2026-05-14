#include "polyclip_internal.hpp"
#include <algorithm>
#include <deque>
#include <limits>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Public API: Point, Rect, Contour, Polygon, compute().

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
    detail::boolean_op_dcel(op, subject, clip, rule, result);
    // Establish the (outer CCW, holes CW) convention so area() reports the
    // filled region directly, without an explicit caller-side step.
    result.compute_holes();
    return result;
}

// validate() uses a dedicated Bentley-Ottmann sweep that needs no FillRule:
// it only reports whether any contour intersects itself.

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

// Bentley-Ottmann self-intersection sweep.
//
// Rather than swapping two crossing edges in the status structure (fragile
// with std::set + a y-at comparator, because of floating point on the
// intersection point), both edges are SPLIT at the intersection point in the
// Martinez-Rueda style: the originals leave the status via newly-scheduled
// right events at ip, and the right halves re-enter as fresh left events that
// sort into post-swap order through segment_order's cross-product comparator.
//
// BoEventComp uses the "right-before-left at the same point" convention,
// required so that divide_segment() cleans up an original edge before its
// right half is inserted. Complexity: O((n + k) log n) for k intersections.

struct BoIsect {
    std::size_t edge_a;
    std::size_t edge_b;
    Point pt;
};

// Forward-declare BoEvent so BoSegComp can be defined, instantiate the status
// set, then define BoEvent (which now sees a complete iterator type), and
// finally define BoSegComp::operator() out of line.
struct BoEvent;
struct BoSegComp {
    bool operator()(const BoEvent* a, const BoEvent* b) const;
};
using BoStatusSet = std::set<const BoEvent*, BoSegComp>;

struct BoEvent {
    Point p;
    bool left = true;
    std::size_t edge_idx = 0;
    BoEvent* other = nullptr;
    mutable BoStatusSet::iterator pos{};
    mutable bool pos_valid = false;

    detail::Segment segment() const { return {p, other->p}; }
    bool below(const Point& x) const {
        if (left) return detail::signed_area(p, other->p, x) > 0.0;
        return other->below(x);
    }
    bool above(const Point& x) const { return !below(x); }
};

struct BoEventComp {
    bool operator()(const BoEvent* a, const BoEvent* b) const {
        if (a == b) return false;
        if (a->p.x > b->p.x) return true;
        if (b->p.x > a->p.x) return false;
        if (a->p != b->p) return a->p.y > b->p.y;
        // Right before left at the same point, so divide_segment() cleans
        // up an original edge before its right half is inserted.
        if (a->left != b->left) return a->left;
        if (a->above(b->other->p) != b->above(a->other->p))
            return a->above(b->other->p);
        return a < b;
    }
};

inline bool BoSegComp::operator()(const BoEvent* e1, const BoEvent* e2) const
{
    return detail::segment_order<BoEvent, BoEventComp>(e1, e2);
}

void collect_self_intersections(const std::vector<Point>& pts,
                                std::vector<BoIsect>& out)
{
    auto n = pts.size();
    if (n < 4) return;

    // Event pool with stable pointers via deque.
    std::deque<BoEvent> pool;

    auto make_event = [&](const Point& p, bool left, std::size_t edge_idx) -> BoEvent* {
        BoEvent ev{};
        ev.p = p;
        ev.left = left;
        ev.edge_idx = edge_idx;
        pool.push_back(ev);
        return &pool.back();
    };

    // Build initial endpoint events.
    std::priority_queue<BoEvent*, std::vector<BoEvent*>, BoEventComp> eq;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t j = (i + 1) % n;
        if (pts[i] == pts[j]) continue;
        if (pts[i].distance_sq(pts[j]) < detail::SnapDistSq) continue;

        auto* e1 = make_event(pts[i], true, i);
        auto* e2 = make_event(pts[j], true, i);
        e1->other = e2;
        e2->other = e1;
        if (e1->p.x < e2->p.x ||
            (e1->p.x == e2->p.x && e1->p.y < e2->p.y)) {
            e2->left = false;
        } else {
            e1->left = false;
        }
        eq.push(e1);
        eq.push(e2);
    }

    BoStatusSet S;

    auto adjacent = [n](std::size_t a, std::size_t b) {
        if (a > b) std::swap(a, b);
        return (b - a == 1) || (a == 0 && b == n - 1);
    };

    // Split a left-event edge at point ip.  The original left event keeps
    // its position in the status; its segment shortens to end at ip.  Two
    // new events at ip are scheduled: a right event paired with the
    // original left, and a left event paired with the original right.
    auto divide_segment = [&](BoEvent* e, const Point& ip) {
        auto* r_for_left  = make_event(ip, false, e->edge_idx);
        auto* l_for_right = make_event(ip, true,  e->edge_idx);

        BoEvent* original_right = e->other;

        // Left half: e ⟷ r_for_left
        r_for_left->other = e;
        e->other = r_for_left;

        // Right half: l_for_right ⟷ original_right
        l_for_right->other = original_right;
        original_right->other = l_for_right;

        eq.push(r_for_left);
        eq.push(l_for_right);
    };

    auto check_pair = [&](BoEvent* a, BoEvent* b) {
        if (!a || !b || a == b) return;
        if (adjacent(a->edge_idx, b->edge_idx)) return;

        Point ip0, ip1;
        int ni = detail::find_intersection(a->segment(), b->segment(), ip0, ip1);
        if (ni == 0) return;

        auto interior = [](const Point& p, const detail::Segment& s) {
            return !(p == s.p1) && !(p == s.p2);
        };

        if (ni == 1) {
            if (interior(ip0, a->segment()) && interior(ip0, b->segment())) {
                out.push_back({a->edge_idx, b->edge_idx, ip0});
                divide_segment(a, ip0);
                divide_segment(b, ip0);
            }
        } else {
            // ni == 2: collinear overlap from ip0 to ip1 (in some order).
            // Geometrically rare for self-intersections of a single
            // simple polygon traversal but can occur when the path
            // back-tracks along its own edge.
            bool ip0_int = interior(ip0, a->segment()) &&
                           interior(ip0, b->segment());
            bool ip1_int = interior(ip1, a->segment()) &&
                           interior(ip1, b->segment());
            if (ip0_int && ip1_int) {
                // Split at the leftward point first; then split the
                // right halves at the second point.  Order by sweep key
                // (smaller x, then smaller y).
                Point lo = ip0, hi = ip1;
                if (ip1.x < ip0.x ||
                    (ip1.x == ip0.x && ip1.y < ip0.y)) {
                    std::swap(lo, hi);
                }
                BoEvent* orig_a_right = a->other;
                BoEvent* orig_b_right = b->other;
                out.push_back({a->edge_idx, b->edge_idx, lo});
                divide_segment(a, lo);
                divide_segment(b, lo);
                // After divide_segment, the new left event for the right
                // half is reachable via original_right->other.
                BoEvent* a_right_half = orig_a_right->other;
                BoEvent* b_right_half = orig_b_right->other;
                out.push_back({a->edge_idx, b->edge_idx, hi});
                divide_segment(a_right_half, hi);
                divide_segment(b_right_half, hi);
            } else if (ip0_int) {
                out.push_back({a->edge_idx, b->edge_idx, ip0});
                divide_segment(a, ip0);
                divide_segment(b, ip0);
            } else if (ip1_int) {
                out.push_back({a->edge_idx, b->edge_idx, ip1});
                divide_segment(a, ip1);
                divide_segment(b, ip1);
            }
        }
    };

    while (!eq.empty()) {
        BoEvent* e = eq.top(); eq.pop();

        if (e->left) {
            auto [it, ok] = S.insert(e);
            (void)ok;
            e->pos = it;
            e->pos_valid = true;

            BoEvent* prev = (it == S.begin())
                          ? nullptr
                          : const_cast<BoEvent*>(*std::prev(it));
            auto next_it = std::next(it);
            BoEvent* next = (next_it == S.end())
                          ? nullptr
                          : const_cast<BoEvent*>(*next_it);

            check_pair(e, prev);
            check_pair(e, next);
        } else {
            // Right event: erase the paired left event from status.
            BoEvent* left = e->other;
            if (!left->pos_valid) continue;

            auto sli = left->pos;
            left->pos_valid = false;

            BoEvent* prev = (sli == S.begin())
                          ? nullptr
                          : const_cast<BoEvent*>(*std::prev(sli));
            auto next_it = std::next(sli);
            BoEvent* next = (next_it == S.end())
                          ? nullptr
                          : const_cast<BoEvent*>(*next_it);

            S.erase(sli);
            check_pair(prev, next);
        }
    }
}

// DCEL planar subdivision used by decompose_one_pass().
//
// Given a refined vertex sequence with intersection points already inserted,
// build a half-edge graph, walk faces with the leftmost-turn rule (the CW
// predecessor of twin(e) in the CCW-sorted outgoing list at the target
// vertex), drop the outer face, and keep inner faces selected by fill rule
// via a winding-number test against the original contour. Walking with the
// interior on the left gives inner faces positive signed area and the outer
// face non-positive.

struct PlanarHalfEdge {
    std::size_t origin;
    std::size_t target;
    std::size_t twin;
    std::size_t next = 0;
    bool        visited = false;
};

inline double face_signed_area(const std::vector<Point>& verts,
                               const std::vector<std::size_t>& face)
{
    double a = 0;
    auto n = face.size();
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t j = (i + 1) % n;
        a += verts[face[i]].x * verts[face[j]].y
           - verts[face[j]].x * verts[face[i]].y;
    }
    return a * 0.5;
}

// Pick a representative interior point for a CCW-walked face: midpoint of
// the first edge offset perpendicular to the LEFT (face interior side
// under the leftmost-turn traversal convention).
inline Point face_interior_point(const std::vector<Point>& verts,
                                 const std::vector<std::size_t>& face)
{
    const Point& p0 = verts[face[0]];
    const Point& p1 = verts[face[1 % face.size()]];
    Point mid{(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};
    double dx = p1.x - p0.x;
    double dy = p1.y - p0.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-15) return mid;
    // Left-perpendicular to (dx,dy) is (-dy,dx). Offset by ~16x snap grid.
    double eps = detail::SnapGrid * 16.0;
    return {mid.x - dy / len * eps, mid.y + dx / len * eps};
}

// Standard right-going-ray winding number against a polygon boundary.
inline int planar_winding(const Point& test, const Contour& c)
{
    auto pts = c.points();
    auto n = pts.size();
    int wn = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& a = pts[i];
        const Point& b = pts[(i + 1) % n];
        if (a.y <= test.y) {
            if (b.y > test.y && detail::signed_area(a, b, test) > 0)
                ++wn;
        } else {
            if (b.y <= test.y && detail::signed_area(a, b, test) < 0)
                --wn;
        }
    }
    return wn;
}

// Build half-edge graph from refined vertex sequence and walk faces.
// Drops outer face, applies fill rule via winding number against the
// original contour, appends kept faces as Contours to `output`.
void decompose_planar(const Contour& original,
                      const std::vector<Point>& split_pts,
                      FillRule rule,
                      std::vector<Contour>& output)
{
    if (split_pts.size() < 3) return;

    // Snap-dedupe vertices.
    std::unordered_map<Point, std::size_t, PointHash> vmap;
    std::vector<Point> verts;
    verts.reserve(split_pts.size());
    auto get_vertex = [&](const Point& p) -> std::size_t {
        Point sp = detail::snap_to_grid(p);
        auto it = vmap.find(sp);
        if (it != vmap.end()) return it->second;
        std::size_t idx = verts.size();
        vmap[sp] = idx;
        verts.push_back(sp);
        return idx;
    };

    // Build half-edges (forward + twin) for each non-degenerate refined
    // segment.  Twins are stored adjacent: twin(2k) = 2k+1.
    std::vector<PlanarHalfEdge> edges;
    edges.reserve(split_pts.size() * 2);
    auto sn = split_pts.size();
    for (std::size_t i = 0; i < sn; ++i) {
        std::size_t a = get_vertex(split_pts[i]);
        std::size_t b = get_vertex(split_pts[(i + 1) % sn]);
        if (a == b) continue;  // collapsed by snap
        std::size_t fwd = edges.size();
        std::size_t bwd = fwd + 1;
        edges.push_back({a, b, bwd, 0, false});
        edges.push_back({b, a, fwd, 0, false});
    }
    if (edges.size() < 6) return;  // need >= 3 distinct edges for any face

    // Outgoing half-edges per vertex, sorted CCW by angle.
    std::vector<std::vector<std::size_t>> outgoing(verts.size());
    for (std::size_t e = 0; e < edges.size(); ++e)
        outgoing[edges[e].origin].push_back(e);

    auto edge_angle = [&](std::size_t e) {
        double dx = verts[edges[e].target].x - verts[edges[e].origin].x;
        double dy = verts[edges[e].target].y - verts[edges[e].origin].y;
        return std::atan2(dy, dx);
    };
    for (auto& list : outgoing) {
        std::sort(list.begin(), list.end(),
                  [&](std::size_t a, std::size_t b) {
                      return edge_angle(a) < edge_angle(b);
                  });
    }

    // For each half-edge, set face-traversal `next` =
    // CW predecessor of twin in target's outgoing list.
    for (std::size_t e = 0; e < edges.size(); ++e) {
        std::size_t b = edges[e].target;
        auto& list = outgoing[b];
        std::size_t twin_idx = edges[e].twin;
        std::size_t k = 0;
        for (; k < list.size(); ++k)
            if (list[k] == twin_idx) break;
        assert(k < list.size() && "twin not found in outgoing list");
        std::size_t pred = (k + list.size() - 1) % list.size();
        edges[e].next = list[pred];
    }

    // Walk faces.
    std::vector<std::vector<std::size_t>> faces;
    const std::size_t max_walk = edges.size() * 2;
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (edges[start].visited) continue;
        std::vector<std::size_t> face;
        std::size_t cur = start;
        std::size_t safety = 0;
        while (!edges[cur].visited) {
            edges[cur].visited = true;
            face.push_back(edges[cur].origin);
            cur = edges[cur].next;
            if (++safety > max_walk) break;  // bounded against malformed graph
        }
        if (face.size() >= 3)
            faces.push_back(std::move(face));
    }

    // Drop outer face (signed area <= 0 in CCW-walk convention) and filter
    // inner faces by fill rule via winding number against the original.
    for (auto& face : faces) {
        double sa = face_signed_area(verts, face);
        if (sa <= 0) continue;  // outer face (or degenerate zero-area face)
        Point ip = face_interior_point(verts, face);
        int wn = planar_winding(ip, original);
        bool keep = (rule == FillRule::EvenOdd)
                  ? (std::abs(wn) % 2 == 1)
                  : (wn != 0);
        if (!keep) continue;
        Contour c;
        c.reserve(face.size());
        for (auto vi : face) c.add(verts[vi]);
        output.push_back(std::move(c));
    }
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

// decompose(): split self-intersecting contours into simple contours.

void Polygon::decompose(FillRule rule)
{
    // Iterate decompose_one_pass until either nothing more can be split
    // (early-exit) or the depth budget is exhausted.  Each pass may
    // reveal nested self-intersections that a previous pass produced
    // as side effects: e.g. when a multi-self-crossing contour is
    // decomposed by stack-based loop extraction, some of the produced
    // sub-contours can themselves still be self-intersecting.
    for (int depth = 0; depth < detail::MaxDecomposeDepth; ++depth) {
        if (!decompose_one_pass(rule))
            break;
    }
}

bool Polygon::decompose_one_pass(FillRule rule)
{
    std::vector<Contour> result;
    bool any_split = false;

    for (auto& contour : m_contours) {
        if (contour.size() < 4) {
            result.push_back(std::move(contour));
            continue;
        }

        auto pts = contour.points();
        auto n = pts.size();

        // Step 1: Find all self-intersections via Bentley-Ottmann sweep
        // with Martinez-Rueda divide_segment splitting.  Numerically
        // robust through cross-product based segment ordering, and
        // O((n + k) log n) where k is the number of intersections.
        std::vector<BoIsect> isects;
        collect_self_intersections(std::vector<Point>(pts.begin(), pts.end()),
                                   isects);

        // Detect coincident vertices (self-touch without edge crossing).
        // If present, planar subdivision is needed even when isects is
        // empty.
        bool has_dup_vertices = false;
        {
            std::unordered_set<Point, PointHash> seen;
            seen.reserve(n);
            for (auto& p : pts) {
                Point sp = detail::snap_to_grid(p);
                if (!seen.insert(sp).second) { has_dup_vertices = true; break; }
            }
        }

        if (isects.empty() && !has_dup_vertices) {
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

        // Step 3: Build planar subdivision (DCEL), walk faces, drop outer
        // face, filter inner faces by fill rule via winding number against
        // the original contour.
        std::vector<Contour> sub;
        decompose_planar(contour, split_pts, rule, sub);

        if (sub.empty()) {
            // Fallback: subdivision produced nothing usable. Keep original
            // contour rather than silently losing geometry.
            result.push_back(std::move(contour));
        } else {
            for (auto& c : sub)
                result.push_back(std::move(c));
            any_split = true;
        }
    }

    m_contours = std::move(result);
    return any_split;
}

} // namespace polyclip

// Segment/segment intersection.

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

} // namespace polyclip::detail

// Hole classification: a sweep that assigns containment depth to each
// contour and orients holes (odd depth) opposite their parent.

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

// DCEL-based boolean operations; see the boolean_op_dcel() declaration in
// polyclip_internal.hpp for the pipeline overview.

namespace polyclip::detail {

namespace {

// An edge of one of the input polygons, tagged with its source. contour_id is
// unique across both polygons; vertex_in_contour and contour_size let the
// intersection sweep skip cyclically-adjacent edges of the same contour.
struct TaggedEdge {
    Point a, b;
    uint16_t label;             // 0 = subject, 1 = clipping
    uint32_t contour_id;
    uint32_t vertex_in_contour; // index of `a` within its contour
    uint32_t contour_size;
};

// Bentley-Ottmann intersection collection over tagged edges. Same sweep as
// collect_self_intersections() above, extended so that two edges
// count as "adjacent" (intersection skipped) only when they share a contour
// and are cyclically adjacent within it.

struct BoIsect {
    std::size_t edge_a;
    std::size_t edge_b;
    Point pt;
};

struct DEvent;

struct DSegComp {
    bool operator()(const DEvent* a, const DEvent* b) const;
};
using DStatusSet = std::set<const DEvent*, DSegComp>;

struct DEvent {
    Point p;
    bool left = true;
    std::size_t edge_idx = 0;
    DEvent* other = nullptr;
    mutable DStatusSet::iterator pos{};
    mutable bool pos_valid = false;

    Segment segment() const { return {p, other->p}; }
    bool below(const Point& x) const {
        if (left) return signed_area(p, other->p, x) > 0.0;
        return other->below(x);
    }
    bool above(const Point& x) const { return !below(x); }
};

struct DEventComp {
    bool operator()(const DEvent* a, const DEvent* b) const {
        if (a == b) return false;
        if (a->p.x > b->p.x) return true;
        if (b->p.x > a->p.x) return false;
        if (a->p != b->p) return a->p.y > b->p.y;
        // R-before-L convention: required for divide_segment to clean up
        // originals before inserting right halves.
        if (a->left != b->left) return a->left;
        if (a->above(b->other->p) != b->above(a->other->p))
            return a->above(b->other->p);
        return a < b;
    }
};

inline bool DSegComp::operator()(const DEvent* e1, const DEvent* e2) const
{
    return segment_order<DEvent, DEventComp>(e1, e2);
}

void find_all_intersections(const std::vector<TaggedEdge>& edges,
                            std::vector<BoIsect>& out)
{
    auto n = edges.size();
    if (n < 2) return;

    std::deque<DEvent> pool;
    auto make_event = [&](const Point& p, bool left, std::size_t edge_idx) -> DEvent* {
        DEvent ev{};
        ev.p = p;
        ev.left = left;
        ev.edge_idx = edge_idx;
        pool.push_back(ev);
        return &pool.back();
    };

    std::priority_queue<DEvent*, std::vector<DEvent*>, DEventComp> eq;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& e = edges[i];
        if (e.a == e.b) continue;
        if (e.a.distance_sq(e.b) < SnapDistSq) continue;

        auto* e1 = make_event(e.a, true, i);
        auto* e2 = make_event(e.b, true, i);
        e1->other = e2;
        e2->other = e1;
        if (e1->p.x < e2->p.x ||
            (e1->p.x == e2->p.x && e1->p.y < e2->p.y)) {
            e2->left = false;
        } else {
            e1->left = false;
        }
        eq.push(e1);
        eq.push(e2);
    }

    DStatusSet S;

    // Two edges are "adjacent" only if they share the same contour AND
    // are cyclically adjacent within that contour.
    auto adjacent = [&edges](std::size_t a, std::size_t b) -> bool {
        if (edges[a].contour_id != edges[b].contour_id) return false;
        auto sz = edges[a].contour_size;
        auto va = edges[a].vertex_in_contour;
        auto vb = edges[b].vertex_in_contour;
        if (va > vb) std::swap(va, vb);
        return (vb - va == 1) || (va == 0 && vb == sz - 1);
    };

    auto divide_segment = [&](DEvent* e, const Point& ip) {
        auto* r_for_left  = make_event(ip, false, e->edge_idx);
        auto* l_for_right = make_event(ip, true,  e->edge_idx);

        DEvent* original_right = e->other;

        r_for_left->other = e;
        e->other = r_for_left;

        l_for_right->other = original_right;
        original_right->other = l_for_right;

        eq.push(r_for_left);
        eq.push(l_for_right);
    };

    auto check_pair = [&](DEvent* a, DEvent* b) {
        if (!a || !b || a == b) return;
        if (adjacent(a->edge_idx, b->edge_idx)) return;

        Point ip0, ip1;
        int ni = find_intersection(a->segment(), b->segment(), ip0, ip1);
        if (ni == 0) return;

        auto interior = [](const Point& p, const Segment& s) {
            return !(p == s.p1) && !(p == s.p2);
        };

        // Record every intersection, including those at shared endpoints:
        // the DCEL build snap-dedupes vertices anyway, so coincident
        // endpoints collapse to one vertex automatically. Only DIVIDE at
        // points strictly interior to a segment, since splitting at an
        // endpoint would create a zero-length sub-segment.
        auto record = [&](const Point& ip) {
            out.push_back({a->edge_idx, b->edge_idx, ip});
        };

        if (ni == 1) {
            record(ip0);
            if (interior(ip0, a->segment())) divide_segment(a, ip0);
            if (interior(ip0, b->segment())) divide_segment(b, ip0);
        } else {
            // Collinear overlap.
            bool ip0_int_a = interior(ip0, a->segment());
            bool ip0_int_b = interior(ip0, b->segment());
            bool ip1_int_a = interior(ip1, a->segment());
            bool ip1_int_b = interior(ip1, b->segment());

            // Pick split points: only interior ones for each segment.
            // Order by sweep key (smaller x, then smaller y).
            Point lo = ip0, hi = ip1;
            if (ip1.x < ip0.x ||
                (ip1.x == ip0.x && ip1.y < ip0.y)) {
                std::swap(lo, hi);
                std::swap(ip0_int_a, ip1_int_a);
                std::swap(ip0_int_b, ip1_int_b);
            }
            record(lo);
            if (lo != hi) record(hi);

            // Split each segment at its interior points only.
            DEvent* a_cur = a;
            if (ip0_int_a) { divide_segment(a_cur, lo); a_cur = a_cur->other->other; }
            if (ip1_int_a) { divide_segment(a_cur, hi); }
            DEvent* b_cur = b;
            if (ip0_int_b) { divide_segment(b_cur, lo); b_cur = b_cur->other->other; }
            if (ip1_int_b) { divide_segment(b_cur, hi); }
        }
    };

    std::size_t budget = MaxSweepEvents;
    while (!eq.empty() && budget-- > 0) {
        DEvent* e = eq.top(); eq.pop();

        if (e->left) {
            auto [it, ok] = S.insert(e);
            (void)ok;
            e->pos = it;
            e->pos_valid = true;

            DEvent* prev = (it == S.begin()) ? nullptr
                : const_cast<DEvent*>(*std::prev(it));
            auto next_it = std::next(it);
            DEvent* next = (next_it == S.end()) ? nullptr
                : const_cast<DEvent*>(*next_it);

            check_pair(e, prev);
            check_pair(e, next);
        } else {
            DEvent* left = e->other;
            if (!left->pos_valid) continue;

            auto sli = left->pos;
            left->pos_valid = false;

            DEvent* prev = (sli == S.begin()) ? nullptr
                : const_cast<DEvent*>(*std::prev(sli));
            auto next_it = std::next(sli);
            DEvent* next = (next_it == S.end()) ? nullptr
                : const_cast<DEvent*>(*next_it);

            S.erase(sli);
            check_pair(prev, next);
        }
    }
}

// DCEL construction and face walking, generalized from decompose_planar()
// above to classify faces against both the subject and clipping
// polygons.

struct DCELHalfEdge {
    std::size_t origin;
    std::size_t target;
    std::size_t twin;
    std::size_t next = 0;
    std::size_t face = std::size_t(-1);  // face id (set after face walk)
    bool        visited = false;
};

inline double face_signed_area_dcel(const std::vector<Point>& verts,
                                    const std::vector<std::size_t>& face)
{
    double a = 0;
    auto n = face.size();
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t j = (i + 1) % n;
        a += verts[face[i]].x * verts[face[j]].y
           - verts[face[j]].x * verts[face[i]].y;
    }
    return a * 0.5;
}

// Representative interior point of a CCW-walked inner face: the midpoint of
// the face's longest edge, offset perpendicular-left (the face interior side)
// by a small epsilon. Using the longest edge keeps the offset small relative
// to the edge and leaves room to stay inside even thin slivers; the epsilon
// scales with the face's short bbox side but never drops below ~100*SnapGrid,
// so the point never coincides with a polygon edge by floating-point noise.
inline Point face_interior_point_dcel(const std::vector<Point>& verts,
                                      const std::vector<std::size_t>& face)
{
    auto n = face.size();
    if (n < 3) return verts[face[0]];

    double minx = std::numeric_limits<double>::infinity();
    double miny = std::numeric_limits<double>::infinity();
    double maxx = -std::numeric_limits<double>::infinity();
    double maxy = -std::numeric_limits<double>::infinity();
    for (auto vi : face) {
        minx = std::min(minx, verts[vi].x);
        miny = std::min(miny, verts[vi].y);
        maxx = std::max(maxx, verts[vi].x);
        maxy = std::max(maxy, verts[vi].y);
    }
    double bbox_short = std::min(maxx - minx, maxy - miny);
    double eps = std::max(bbox_short * 1e-3, SnapGrid * 100.0);

    double best_len2 = -1;
    std::size_t best_i = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& a = verts[face[i]];
        const Point& b = verts[face[(i + 1) % n]];
        double dx = b.x - a.x;
        double dy = b.y - a.y;
        double l2 = dx * dx + dy * dy;
        if (l2 > best_len2) {
            best_len2 = l2;
            best_i = i;
        }
    }
    const Point& a = verts[face[best_i]];
    const Point& b = verts[face[(best_i + 1) % n]];
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-15) return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5};
    // Perpendicular-left of (dx, dy) is (-dy, dx).
    double nx = -dy / len;
    double ny =  dx / len;
    return {(a.x + b.x) * 0.5 + nx * eps,
            (a.y + b.y) * 0.5 + ny * eps};
}

// Standard right-going-ray winding number against a polygon (all contours).
int polygon_winding(const Point& test, const Polygon& p)
{
    int wn = 0;
    for (const auto& c : p) {
        auto pts = c.points();
        auto n = pts.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Point& a = pts[i];
            const Point& b = pts[(i + 1) % n];
            if (a.y <= test.y) {
                if (b.y > test.y && signed_area(a, b, test) > 0)
                    ++wn;
            } else {
                if (b.y <= test.y && signed_area(a, b, test) < 0)
                    --wn;
            }
        }
    }
    return wn;
}

bool inside_by_rule(int winding, FillRule rule)
{
    if (rule == FillRule::EvenOdd) return (std::abs(winding) % 2) == 1;
    return winding != 0;
}

bool keep_face(Operation op, bool in_subject, bool in_clipping)
{
    switch (op) {
    case Operation::Intersection: return in_subject && in_clipping;
    case Operation::Union:        return in_subject || in_clipping;
    case Operation::Difference:   return in_subject && !in_clipping;
    case Operation::Xor:          return in_subject != in_clipping;
    }
    return false;
}

} // anonymous namespace

void boolean_op_dcel(Operation op,
                     const Polygon& subject,
                     const Polygon& clipping,
                     FillRule rule,
                     Polygon& result)
{
    // Handle trivial empty cases early.
    if (subject.empty() && clipping.empty()) return;

    // Bbox-disjoint shortcut.
    auto bbox_s = subject.bbox();
    auto bbox_c = clipping.bbox();
    bool any_subject = !subject.empty();
    bool any_clipping = !clipping.empty();

    if (!any_subject) {
        if (op == Operation::Union || op == Operation::Xor)
            result = clipping;
        return;
    }
    if (!any_clipping) {
        if (op == Operation::Union || op == Operation::Xor
            || op == Operation::Difference)
            result = subject;
        return;
    }
    if (!bbox_s.overlaps(bbox_c)) {
        if (op == Operation::Difference) {
            result = subject;
            return;
        }
        if (op == Operation::Union || op == Operation::Xor) {
            result = subject;
            for (const auto& c : clipping) result.add(c);
            return;
        }
        // Intersection: empty
        return;
    }

    // Step 1: collect tagged edges.
    std::vector<TaggedEdge> edges;
    uint32_t cid = 0;
    auto emit_polygon = [&](const Polygon& p, uint16_t label) {
        for (const auto& c : p) {
            auto pts = c.points();
            auto n = pts.size();
            if (n < 3) { cid++; continue; }
            for (std::size_t i = 0; i < n; ++i) {
                Point a = snap_to_grid(pts[i]);
                Point b = snap_to_grid(pts[(i + 1) % n]);
                assert_coord_range(a);
                assert_coord_range(b);
                if (a == b) continue;
                TaggedEdge e{};
                e.a = a;
                e.b = b;
                e.label = label;
                e.contour_id = cid;
                e.vertex_in_contour = static_cast<uint32_t>(i);
                e.contour_size = static_cast<uint32_t>(n);
                edges.push_back(e);
            }
            cid++;
        }
    };
    emit_polygon(subject, 0);
    emit_polygon(clipping, 1);

    if (edges.empty()) return;

    // Step 2: find all intersections.
    std::vector<BoIsect> isects;
    find_all_intersections(edges, isects);

    // Step 3: refine each edge by inserting interior intersection points.
    struct EdgeSplit {
        double t;
        Point pt;
    };
    std::vector<std::vector<EdgeSplit>> edge_splits(edges.size());

    auto param_of = [](const Point& a, const Point& b, const Point& p) -> double {
        double dx = b.x - a.x;
        double dy = b.y - a.y;
        double t;
        if (std::abs(dx) > std::abs(dy))
            t = (p.x - a.x) / dx;
        else if (std::abs(dy) > 0)
            t = (p.y - a.y) / dy;
        else
            t = 0;
        return std::clamp(t, 0.0, 1.0);
    };

    for (const auto& isect : isects) {
        auto add_split = [&](std::size_t edge_idx, const Point& ip) {
            const auto& e = edges[edge_idx];
            double t = param_of(e.a, e.b, ip);
            // Only record strictly interior splits; endpoint coincidences
            // are handled via vertex snapping in DCEL build.
            if (t > 1e-9 && t < 1.0 - 1e-9)
                edge_splits[edge_idx].push_back({t, ip});
        };
        add_split(isect.edge_a, isect.pt);
        add_split(isect.edge_b, isect.pt);
    }

    // Step 4: build refined segment list and snap-dedupe vertices.
    std::unordered_map<Point, std::size_t, PointHash> vmap;
    std::vector<Point> verts;
    auto get_vertex = [&](const Point& p) -> std::size_t {
        Point sp = snap_to_grid(p);
        auto it = vmap.find(sp);
        if (it != vmap.end()) return it->second;
        std::size_t idx = verts.size();
        vmap[sp] = idx;
        verts.push_back(sp);
        return idx;
    };

    // A refined sub-segment, stored a->b in the source polygon's vertex
    // order. `label` records which polygon it came from.
    struct RefSeg {
        std::size_t v_a, v_b;
        uint16_t label;
    };
    std::vector<RefSeg> refined;
    refined.reserve(edges.size() + isects.size() * 2);

    for (std::size_t ei = 0; ei < edges.size(); ++ei) {
        const auto& e = edges[ei];
        auto& splits = edge_splits[ei];
        std::sort(splits.begin(), splits.end(),
                  [](const EdgeSplit& a, const EdgeSplit& b) { return a.t < b.t; });

        std::vector<Point> chain;
        chain.reserve(splits.size() + 2);
        chain.push_back(e.a);
        for (const auto& sp : splits) chain.push_back(sp.pt);
        chain.push_back(e.b);

        for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
            std::size_t va = get_vertex(chain[i]);
            std::size_t vb = get_vertex(chain[i + 1]);
            if (va == vb) continue;  // collapsed by snap
            refined.push_back({va, vb, e.label});
        }
    }

    if (refined.empty()) return;

    // Step 5: deduplicate refined segments by unordered vertex pair. When
    // subject and clipping share an edge there would otherwise be two copies;
    // duplicate parallel half-edges between the same two vertices would break
    // the leftmost-turn face walk, so deduplication is required. The dropped
    // copies carry no information we need: face classification is done by
    // winding number, independent of how often an edge appears in the input.
    struct PairKey { std::size_t a, b; bool operator==(const PairKey&) const = default; };
    struct PairHash {
        std::size_t operator()(const PairKey& k) const noexcept {
            return std::hash<std::size_t>{}(k.a)
                 ^ (std::hash<std::size_t>{}(k.b) * 0x9e3779b97f4a7c15ULL);
        }
    };

    std::unordered_map<PairKey, std::size_t, PairHash> seg_set;
    std::vector<RefSeg> unique_segs;
    for (const auto& s : refined) {
        PairKey k{std::min(s.v_a, s.v_b), std::max(s.v_a, s.v_b)};
        auto [it, inserted] = seg_set.try_emplace(k, unique_segs.size());
        if (inserted)
            unique_segs.push_back(s);
    }

    if (unique_segs.size() < 3) return;

    // Step 6: build two directed half-edges per undirected segment. Twins are
    // stored adjacent: twin(2k) = 2k+1.
    std::vector<DCELHalfEdge> hedges;
    hedges.reserve(unique_segs.size() * 2);
    for (const auto& s : unique_segs) {
        std::size_t fwd = hedges.size();
        std::size_t bwd = fwd + 1;
        hedges.push_back({s.v_a, s.v_b, bwd, 0, std::size_t(-1), false});
        hedges.push_back({s.v_b, s.v_a, fwd, 0, std::size_t(-1), false});
    }

    // Step 7: per-vertex outgoing half-edges, sorted CCW by angle.
    std::vector<std::vector<std::size_t>> outgoing(verts.size());
    for (std::size_t e = 0; e < hedges.size(); ++e)
        outgoing[hedges[e].origin].push_back(e);

    auto edge_angle = [&](std::size_t e) {
        double dx = verts[hedges[e].target].x - verts[hedges[e].origin].x;
        double dy = verts[hedges[e].target].y - verts[hedges[e].origin].y;
        return std::atan2(dy, dx);
    };
    for (auto& list : outgoing) {
        std::sort(list.begin(), list.end(),
                  [&](std::size_t a, std::size_t b) {
                      return edge_angle(a) < edge_angle(b);
                  });
    }

    // Step 8: set next pointer = CW predecessor of twin in target's outgoing list.
    for (std::size_t e = 0; e < hedges.size(); ++e) {
        std::size_t b = hedges[e].target;
        auto& list = outgoing[b];
        std::size_t twin_idx = hedges[e].twin;
        std::size_t k = 0;
        for (; k < list.size(); ++k)
            if (list[k] == twin_idx) break;
        assert(k < list.size() && "twin not found in outgoing list");
        std::size_t pred = (k + list.size() - 1) % list.size();
        hedges[e].next = list[pred];
    }

    // Step 9: walk faces.
    std::vector<std::vector<std::size_t>> faces;  // each face = list of half-edge ids
    const std::size_t max_walk = hedges.size() * 2 + 16;
    for (std::size_t start = 0; start < hedges.size(); ++start) {
        if (hedges[start].visited) continue;
        std::vector<std::size_t> face_edges;
        std::size_t cur = start;
        std::size_t safety = 0;
        while (!hedges[cur].visited) {
            hedges[cur].visited = true;
            face_edges.push_back(cur);
            cur = hedges[cur].next;
            if (++safety > max_walk) break;
        }
        std::size_t fid = faces.size();
        for (auto e : face_edges) hedges[e].face = fid;
        faces.push_back(std::move(face_edges));
    }

    // Step 10: classify faces.
    //
    // CCW faces (signed area > 0) are inner faces; classify each by a
    // winding-number test at an interior point. CW faces (signed area < 0)
    // are either the single true outer face or a hole-ring of a disconnected
    // DCEL component. The latter arises when one polygon lies entirely inside
    // the other with no edge intersections: that polygon forms a separate
    // component whose CW cycle must inherit the keep state of the CCW face
    // containing it, rather than being treated as the outer face.
    struct FaceInfo {
        bool is_outer = false;
        bool keep = false;
        std::vector<std::size_t> verts_cycle;
        double sa = 0;
    };
    std::vector<FaceInfo> finfo(faces.size());

    for (std::size_t fid = 0; fid < faces.size(); ++fid) {
        auto& fe = faces[fid];
        if (fe.size() < 3) { finfo[fid].is_outer = true; continue; }
        std::vector<std::size_t> vc;
        vc.reserve(fe.size());
        for (auto he : fe) vc.push_back(hedges[he].origin);
        finfo[fid].sa = face_signed_area_dcel(verts, vc);
        finfo[fid].verts_cycle = std::move(vc);
    }

    for (std::size_t fid = 0; fid < faces.size(); ++fid) {
        if (finfo[fid].sa <= 0) continue;
        Point ip = face_interior_point_dcel(verts, finfo[fid].verts_cycle);
        bool in_s = inside_by_rule(polygon_winding(ip, subject), rule);
        bool in_c = inside_by_rule(polygon_winding(ip, clipping), rule);
        finfo[fid].keep = keep_face(op, in_s, in_c);
    }

    auto pip_against_cycle = [&](const Point& p, const std::vector<std::size_t>& cycle) -> bool {
        int wn = 0;
        auto n = cycle.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Point& a = verts[cycle[i]];
            const Point& b = verts[cycle[(i + 1) % n]];
            if (a.y <= p.y) {
                if (b.y > p.y && signed_area(a, b, p) > 0) ++wn;
            } else {
                if (b.y <= p.y && signed_area(a, b, p) < 0) --wn;
            }
        }
        return wn != 0;
    };

    // For each CW face, find the smallest CCW face containing it: if one
    // exists this CW cycle is a hole-ring and inherits that face's keep
    // state, otherwise it is the true outer face.
    for (std::size_t fid = 0; fid < faces.size(); ++fid) {
        if (finfo[fid].sa >= 0) continue;
        if (finfo[fid].verts_cycle.empty()) { finfo[fid].is_outer = true; continue; }

        // Test point strictly inside the CW face's region. A face always
        // lies to the LEFT of its half-edges, so the perpendicular-left
        // offset of any cycle edge enters the face interior and is never on
        // a cycle boundary, keeping the containment test unambiguous: for
        // the true outer face the point falls outside every CCW face, for a
        // hole-ring it falls inside exactly its parent.
        const auto& cycle = finfo[fid].verts_cycle;
        auto cn = cycle.size();
        double best_l2 = -1;
        std::size_t best_i = 0;
        for (std::size_t i = 0; i < cn; ++i) {
            const Point& a = verts[cycle[i]];
            const Point& b = verts[cycle[(i + 1) % cn]];
            double dx = b.x - a.x, dy = b.y - a.y;
            double l2 = dx * dx + dy * dy;
            if (l2 > best_l2) { best_l2 = l2; best_i = i; }
        }
        const Point& ea = verts[cycle[best_i]];
        const Point& eb = verts[cycle[(best_i + 1) % cn]];
        double dx = eb.x - ea.x, dy = eb.y - ea.y;
        double len = std::sqrt(dx * dx + dy * dy);
        Point test;
        if (len < 1e-15) {
            test = ea;
        } else {
            double nx = -dy / len;
            double ny =  dx / len;
            double eps = SnapGrid * 100.0;
            test = {(ea.x + eb.x) * 0.5 + nx * eps,
                    (ea.y + eb.y) * 0.5 + ny * eps};
        }

        std::size_t best = std::size_t(-1);
        double best_area = std::numeric_limits<double>::infinity();
        for (std::size_t g = 0; g < faces.size(); ++g) {
            if (finfo[g].sa <= 0) continue;
            if (g == fid) continue;
            if (pip_against_cycle(test, finfo[g].verts_cycle)) {
                if (finfo[g].sa < best_area) {
                    best = g;
                    best_area = finfo[g].sa;
                }
            }
        }
        if (best == std::size_t(-1)) {
            finfo[fid].is_outer = true;
        } else {
            finfo[fid].keep = finfo[best].keep;
        }
    }

    // Step 11: boundary extraction. A half-edge is OUTPUT when its face is a
    // kept inner face and its twin's face is not, i.e. it lies on the
    // boundary of the kept region. Following hedges[h].next directly would
    // wander into edges shared by two kept faces; next_out() instead walks
    // from one OUTPUT half-edge to the next, skipping such internal edges.
    std::vector<bool> is_output(hedges.size(), false);
    for (std::size_t h = 0; h < hedges.size(); ++h) {
        std::size_t f = hedges[h].face;
        std::size_t tf = hedges[hedges[h].twin].face;
        bool kept_h  = (f  != std::size_t(-1)) && !finfo[f].is_outer  && finfo[f].keep;
        bool kept_tw = (tf != std::size_t(-1)) && !finfo[tf].is_outer && finfo[tf].keep;
        if (kept_h && !kept_tw) is_output[h] = true;
    }

    // Position of each half-edge within its origin's CCW-sorted outgoing
    // list, for fast next_out lookup.
    std::vector<std::size_t> ccw_pos(hedges.size(), 0);
    for (std::size_t v = 0; v < outgoing.size(); ++v) {
        const auto& list = outgoing[v];
        for (std::size_t i = 0; i < list.size(); ++i)
            ccw_pos[list[i]] = i;
    }

    // From OUTPUT half-edge h, the next boundary edge is the first OUTPUT
    // outgoing edge found scanning CW from twin(h) at h's target vertex:
    // the leftmost-turn rule restricted to boundary edges.
    auto next_out = [&](std::size_t h) -> std::size_t {
        std::size_t v = hedges[h].target;
        const auto& list = outgoing[v];
        std::size_t k = ccw_pos[hedges[h].twin];
        std::size_t sz = list.size();
        for (std::size_t step = 1; step <= sz; ++step) {
            std::size_t cand = list[(k + sz - step) % sz];
            if (is_output[cand]) return cand;
        }
        return std::size_t(-1);
    };

    // Step 12: walk OUTPUT half-edges into closed contours.
    std::vector<bool> walked(hedges.size(), false);
    for (std::size_t start = 0; start < hedges.size(); ++start) {
        if (!is_output[start] || walked[start]) continue;

        std::vector<std::size_t> cycle;
        std::size_t cur = start;
        std::size_t safety = 0;
        while (!walked[cur]) {
            walked[cur] = true;
            cycle.push_back(cur);
            std::size_t nx = next_out(cur);
            if (nx == std::size_t(-1)) break;
            cur = nx;
            if (++safety > max_walk) break;
        }
        if (cycle.size() < 3) continue;

        Contour c;
        c.reserve(cycle.size());
        for (auto he : cycle) c.add(verts[hedges[he].origin]);
        if (c.size() >= 3) result.add(std::move(c));
    }
}

} // namespace polyclip::detail
