#ifndef POLYCLIP_INTERNAL_HPP
#define POLYCLIP_INTERNAL_HPP

#include "polyclip.hpp"
#include <set>
#include <deque>
#include <list>
#include <queue>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <cassert>

namespace polyclip::detail {

// -- Tolerance constants ----------------------------------------------------
//
// The tolerance model uses three layers:
//
//   Layer 1: GeomEpsilon (1e-10) -- relative threshold for collinearity
//            tests (signed_area ~ 0).  Scale-independent via near_zero().
//
//   Layer 2: SnapGrid (1e-7) -- coordinate quantization grid.  All computed
//            intersection points are rounded to this grid, ensuring
//            bit-identical doubles for geometrically coincident points.
//            This eliminates the need for fuzzy hashing in most code paths.
//
//   Layer 3: ConnectorTolSq (1e-12) -- squared distance for fuzzy endpoint
//            matching in the Connector.  Covers +/-1 grid-cell mismatch from
//            cascaded divide_segment() calls.
//
// Safe coordinate range: |x|, |y| < 1e6.
// At 1e7 the snap grid is ~10x ULP; at 1e8 it equals ULP and fails.
// The MaxCoord constant gates debug assertions.

constexpr double GeomEpsilon  = 1e-10;
constexpr double SnapDistSq   = 1e-12;
constexpr double SnapGrid     = 1e-7;
constexpr double ConnectorTolSq = SnapDistSq;
constexpr double MaxCoord     = 1e7;

// Maximum number of events the sweep line will process before aborting.
// Guards against infinite loops from degenerate / adversarial input.
// 50x the theoretical maximum for well-behaved input (2*n + 4*k where
// k = number of intersections ~ O(n^2) worst case).
constexpr std::size_t MaxSweepEvents = 50'000'000;

// Maximum recursion depth for decompose() -> compute() calls.
constexpr int MaxDecomposeDepth = 2;

inline bool point_near(const Point& a, const Point& b) noexcept
{
    return a.distance_sq(b) <= ConnectorTolSq;
}

inline bool near_zero(double area, double len_sq) noexcept
{
    return std::abs(area) <= GeomEpsilon * len_sq + GeomEpsilon;
}

inline double snap_coord(double v) noexcept
{
    return std::round(v / SnapGrid) * SnapGrid;
}

inline Point snap_to_grid(const Point& p) noexcept
{
    return {snap_coord(p.x), snap_coord(p.y)};
}

inline void assert_coord_range([[maybe_unused]] const Point& p) noexcept
{
    assert(std::isfinite(p.x) && std::isfinite(p.y)
           && "polyclip: non-finite coordinate");
    assert(std::abs(p.x) < MaxCoord && std::abs(p.y) < MaxCoord
           && "polyclip: coordinate exceeds safe range (1e7)");
}

// -- Geometry primitives ----------------------------------------------------

struct Segment {
    Point p1, p2;

    const Point& begin() const noexcept { return p1; }
    const Point& end() const noexcept { return p2; }
    bool degenerate() const noexcept { return p1 == p2; }
};

inline double signed_area(const Point& p0, const Point& p1, const Point& p2) noexcept
{
    return (p0.x - p2.x) * (p1.y - p2.y) - (p1.x - p2.x) * (p0.y - p2.y);
}

inline double seg_len_sq(const Point& p0, const Point& p1) noexcept
{
    double dx = p1.x - p0.x;
    double dy = p1.y - p0.y;
    return dx * dx + dy * dy;
}

inline bool collinear(const Point& p0, const Point& p1, const Point& p2) noexcept
{
    return near_zero(signed_area(p0, p1, p2), seg_len_sq(p0, p1));
}

int find_intersection(const Segment& s0, const Segment& s1, Point& ip0, Point& ip1);

// -- Sweep event types ------------------------------------------------------

enum class EdgeType : uint8_t {
    Normal,
    NonContributing,
    SameTransition,
    DifferentTransition
};

enum class PolyLabel : uint8_t { Subject, Clipping };

struct SweepEvent;

struct SegmentComp {
    bool operator()(const SweepEvent* e1, const SweepEvent* e2) const;
};

using StatusLine = std::set<SweepEvent*, SegmentComp>;

struct SweepEvent {
    Point p;
    bool left = true;
    PolyLabel pl = PolyLabel::Subject;
    SweepEvent* other = nullptr;
    bool in_out = false;
    EdgeType type = EdgeType::Normal;
    bool inside = false;

    // -- Winding number tracking (for FillRule::NonZero) --------------------
    // winding_self:  winding count of this event's own polygon on the
    //                left side of the edge (looking from left to right).
    // winding_other: winding count of the OTHER polygon on the left side.
    // winding_delta: +1 for bottom-to-top edges, -1 for top-to-bottom.
    //                Computed from vertex order at insertion time.
    int winding_self  = 0;
    int winding_other = 0;
    int winding_delta = 0;

    // -- Stored status-line iterator for O(1) erasure ----------------------
    // Set when the left event is inserted into the status line.
    // Used to erase in O(1) when the corresponding right event is processed.
    // Valid from insert until erase; never dereferenced, only passed to
    // std::set::erase(iterator).
    mutable StatusLine::iterator pos{};
    mutable bool pos_valid = false;

    SweepEvent() = default;
    SweepEvent(const Point& pp, bool l, PolyLabel label, SweepEvent* o, EdgeType t = EdgeType::Normal)
        : p(pp), left(l), pl(label), other(o), type(t) {}

    Segment segment() const { return {p, other->p}; }
    bool below(const Point& x) const;
    bool above(const Point& x) const { return !below(x); }
};

struct SweepEventComp {
    bool operator()(const SweepEvent* e1, const SweepEvent* e2) const;
};

template<typename Event, typename EventComp>
bool segment_order(const Event* e1, const Event* e2)
{
    if (e1 == e2)
        return false;

    // Check collinearity from BOTH directions for symmetry.
    // e2's endpoints w.r.t. e1's segment:
    bool col_e2p_in_e1  = collinear(e1->p, e1->other->p, e2->p);
    bool col_e2op_in_e1 = collinear(e1->p, e1->other->p, e2->other->p);
    // e1's endpoints w.r.t. e2's segment:
    bool col_e1p_in_e2  = collinear(e2->p, e2->other->p, e1->p);
    bool col_e1op_in_e2 = collinear(e2->p, e2->other->p, e1->other->p);

    // Segments are truly collinear only if ALL four checks agree.
    bool fully_collinear = col_e2p_in_e1 && col_e2op_in_e1
                        && col_e1p_in_e2 && col_e1op_in_e2;

    if (!fully_collinear) {
        // Use the non-collinear signed-area test, which is well-defined
        // when at least one endpoint is clearly off the other's line.
        if (e1->p == e2->p)
            return e1->below(e2->other->p);
        EventComp comp;
        if (comp(e1, e2))
            return e2->above(e1->p);
        return e1->below(e2->p);
    }

    // Fully collinear: order by sweep position, pointer as tie-breaker.
    if (e1->p == e2->p) {
        EventComp comp;
        if (e1->other->p != e2->other->p)
            return comp(e1->other, e2->other);
        return e1 < e2;
    }
    EventComp comp;
    return comp(e1, e2);
}

// -- Connector --------------------------------------------------------------

class PointChain {
public:
    void init(const Segment& s);
    bool link_segment(const Segment& s);
    bool link_chain(PointChain& chain);
    bool closed() const noexcept { return m_closed; }
    std::size_t size() const noexcept { return m_list.size(); }
    auto begin() { return m_list.begin(); }
    auto end() { return m_list.end(); }

    const Point& front() const { return m_list.front(); }
    const Point& back() const { return m_list.back(); }

private:
    std::list<Point> m_list;
    bool m_closed = false;
};

class Connector {
public:
    void add(const Segment& s);
    void to_polygon(Polygon& p);

private:
    using ChainIter = std::list<PointChain>::iterator;

    static constexpr double CellSize = SnapGrid * 4;

    struct CellKey {
        int64_t ix, iy;
        bool operator==(const CellKey&) const = default;
    };

    struct CellHash {
        std::size_t operator()(const CellKey& k) const noexcept {
            auto h1 = std::hash<int64_t>{}(k.ix);
            auto h2 = std::hash<int64_t>{}(k.iy);
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9
                         + (h1 << 6) + (h1 >> 2));
        }
    };

    struct EPEntry {
        Point   pt;
        ChainIter chain;
    };

    std::unordered_map<CellKey, std::vector<EPEntry>, CellHash> m_cells;

    static CellKey to_cell(const Point& p) noexcept {
        return {static_cast<int64_t>(std::floor(p.x / CellSize)),
                static_cast<int64_t>(std::floor(p.y / CellSize))};
    }

    struct FindResult {
        bool      found = false;
        ChainIter chain{};
    };

    FindResult find_endpoint(const Point& p) const;
    void       insert_endpoint(const Point& p, ChainIter ci);
    void       erase_endpoint(const Point& p, ChainIter ci);

    std::list<PointChain> m_open;
    std::list<PointChain> m_closed;

    void update_endpoints(ChainIter it);
    void remove_endpoints(ChainIter it);
};

// -- Sweep line -------------------------------------------------------------

class SweepLine {
public:
    SweepLine(const Polygon& subj, const Polygon& clip, FillRule rule,
              int decompose_depth = 0)
        : m_subject(subj), m_clipping(clip), m_fill_rule(rule)
        , m_decompose_depth(decompose_depth) {}

    void compute(Operation op, Polygon& result);

private:
    const Polygon& m_subject;
    const Polygon& m_clipping;
    FillRule m_fill_rule;
    int m_decompose_depth;
    std::priority_queue<SweepEvent*, std::vector<SweepEvent*>, SweepEventComp> m_eq;
    std::deque<SweepEvent> m_events;
    SweepEventComp m_sec;
    std::size_t m_events_processed = 0;

    void process_segment(const Segment& s, PolyLabel pl);
    void possible_intersection(SweepEvent* e1, SweepEvent* e2);
    void divide_segment(SweepEvent* e, const Point& p);

    /// Determine whether a Normal edge contributes to the result,
    /// taking FillRule into account.
    bool contributes(const SweepEvent* e, Operation op) const noexcept;

    SweepEvent* store(const SweepEvent& e) {
        m_events.push_back(e);
        return &m_events.back();
    }

    /// Check and enforce the event processing limit.
    /// Returns false if the limit has been exceeded.
    bool check_event_limit() noexcept {
        return ++m_events_processed <= MaxSweepEvents;
    }
};

} // namespace polyclip::detail

#endif
