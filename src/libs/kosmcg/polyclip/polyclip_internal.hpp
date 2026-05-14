#ifndef POLYCLIP_INTERNAL_HPP
#define POLYCLIP_INTERNAL_HPP

#include "polyclip.hpp"
#include <cmath>
#include <cassert>

namespace polyclip::detail {

// Tolerance model, three layers:
//   GeomEpsilon  relative threshold for collinearity tests (signed_area ~ 0),
//                made scale-independent via near_zero().
//   SnapGrid     coordinate quantization grid. Computed intersection points
//                are rounded to it, so geometrically coincident points become
//                bit-identical doubles and can be compared/hashed exactly.
//   SnapDistSq   squared distance below which two points are treated as one.
//
// Coordinate limit:
//   HardMaxCoord  absolute upper bound; beyond it the snap grid approaches
//                 ULP and coincidence detection breaks. Asserted in debug.
//                 The recommended safe operating range is 1e6, which keeps
//                 ~860x ULP of headroom under the snap grid.
constexpr double HardMaxCoord = 1e7;

constexpr double GeomEpsilon = 1e-10;
constexpr double SnapDistSq  = 1e-12;
constexpr double SnapGrid    = 1e-7;

// Upper bound on sweep events processed before aborting; guards against
// pathological input. 50x the theoretical maximum (2n + 4k) for well-behaved
// input with k intersections.
constexpr std::size_t MaxSweepEvents = 50'000'000;

// Maximum recursion depth for decompose(): nested self-intersections produced
// as a side effect of loop extraction may need more than one pass.
constexpr int MaxDecomposeDepth = 2;

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
    assert(std::abs(p.x) < HardMaxCoord && std::abs(p.y) < HardMaxCoord
           && "polyclip: coordinate beyond hard limit (1e7); "
              "recommended safe range is 1e6");
}

struct Segment {
    Point p1, p2;

    const Point& begin() const noexcept { return p1; }
    const Point& end() const noexcept { return p2; }
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

// Segment/segment intersection. Returns 0 (none), 1 (single point in ip0),
// or 2 (collinear overlap from ip0 to ip1).
int find_intersection(const Segment& s0, const Segment& s1, Point& ip0, Point& ip1);

// Strict weak ordering for the sweep status structure: "is segment e1 below
// segment e2 at the current sweep position". Shared by every Bentley-Ottmann
// sweep in the library (validate(), decompose(), boolean_op_dcel(),
// compute_holes()), each parameterizing it with its own event/comparator type.
//
// e1 and e2 are always LEFT events held in the status, so e1->p and e2->p are
// their left endpoints. The comparison is done at the x where both segments
// are active, i.e. the rightmost of the two left endpoints, which is
// guaranteed to lie within the other segment's x-range. Testing against the
// earlier endpoint would extrapolate a segment's line backwards and can
// invert the order for segments of differing slope.
template<typename Event, typename EventComp>
bool segment_order(const Event* e1, const Event* e2)
{
    if (e1 == e2)
        return false;

    // Truly collinear only if all four endpoint/line checks agree.
    bool fully_collinear =
        collinear(e1->p, e1->other->p, e2->p) &&
        collinear(e1->p, e1->other->p, e2->other->p) &&
        collinear(e2->p, e2->other->p, e1->p) &&
        collinear(e2->p, e2->other->p, e1->other->p);

    if (!fully_collinear) {
        if (e1->p == e2->p)
            return e1->below(e2->other->p);
        // Compare e1->p.x / e2->p.x directly rather than via EventComp:
        // the library's EventComp types sort in opposite directions
        // (ascending for std::sort-based sweeps, descending for the
        // priority_queue-based one), so comp(e1,e2) would be ambiguous here.
        if (e1->p.x < e2->p.x)
            return e1->below(e2->p);   // e1 starts first
        if (e2->p.x < e1->p.x)
            return e2->above(e1->p);   // e2 starts first
        return e1->p.y < e2->p.y;      // same left x: lower y is "below"
    }

    // Fully collinear: order by sweep position, pointer as tie-breaker.
    EventComp comp;
    if (e1->p == e2->p) {
        if (e1->other->p != e2->other->p)
            return comp(e1->other, e2->other);
        return e1 < e2;
    }
    return comp(e1, e2);
}

// DCEL-based boolean operations.
//
// Pipeline: collect the edges of both polygons, find all pairwise
// intersections with a Bentley-Ottmann sweep, refine edges at the
// intersection points, snap-dedupe vertices, and build a half-edge graph.
// Walk its faces with the leftmost-turn rule, then classify each face by a
// winding-number test against both polygons combined with the fill rule and
// the boolean operation. A half-edge lies on the result boundary when its
// face is kept and its twin's face is not; walking those half-edges yields
// the output contours.
//
// Classification is per-face and winding-based, so it is global and immune
// to the event-ordering pathologies of local in/out state propagation.
void boolean_op_dcel(Operation op,
                     const Polygon& subject,
                     const Polygon& clipping,
                     FillRule rule,
                     Polygon& result);

} // namespace polyclip::detail

#endif
