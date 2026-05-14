#ifndef POLYCLIP_HPP
#define POLYCLIP_HPP

/// @file polyclip.hpp
/// @brief Polygon boolean operations (intersection, union, difference, xor).
///
/// Operates on polygons represented as sets of contours; each contour is a
/// simple closed polyline, and a Polygon may hold several contours including
/// holes. The implementation builds a planar subdivision (DCEL) of the two
/// inputs and classifies its faces by winding number.
///
/// @code
///   polyclip::Polygon subject, clip;
///   // ... populate contours ...
///   auto result = polyclip::compute(polyclip::Operation::Union, subject, clip);
///   double filled = result.area();   // correct even with holes
/// @endcode
///
/// @par Coordinates
/// Input must be in device space (post-transform). Computed intersection
/// points are snapped to a 1e-7 grid. Keep |x|, |y| < 1e6 for comfortable
/// numerical headroom; 1e7 is the hard bound (asserted in debug builds),
/// beyond which coincidence detection breaks down.
///
/// See compute(), decompose() and compute_holes() for the input contracts
/// around self-intersecting contours and hole orientation.

#include <vector>
#include <span>
#include <cstdint>
#include <cstddef>
#include <functional>

namespace polyclip {

struct Point {
    double x = 0;
    double y = 0;

    constexpr bool operator==(const Point&) const = default;
    [[nodiscard]] double distance_sq(const Point& p) const noexcept;
    [[nodiscard]] bool is_finite() const noexcept;
};

struct PointHash {
    std::size_t operator()(const Point& p) const noexcept {
        auto h1 = std::hash<double>{}(p.x);
        auto h2 = std::hash<double>{}(p.y);
        return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

struct Rect {
    double x1 = 0, y1 = 0;
    double x2 = 0, y2 = 0;

    [[nodiscard]] bool overlaps(const Rect& r) const noexcept;
    [[nodiscard]] bool contains(const Rect& r) const noexcept;
};

enum class Operation : uint8_t {
    Intersection,
    Union,
    Difference,
    Xor
};

/// Fill rule for determining polygon interior.
///   EvenOdd: inside if a ray crosses an odd number of edges (SVG/PDF default).
///   NonZero: inside if the winding number is non-zero (TrueType glyphs,
///            many icon formats, Core Graphics winding fill).
enum class FillRule : uint8_t {
    EvenOdd,
    NonZero
};

class Contour {
public:
    Contour() = default;
    explicit Contour(std::vector<Point> points);

    [[nodiscard]] std::span<const Point> points() const noexcept { return m_points; }
    [[nodiscard]] std::size_t size() const noexcept { return m_points.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_points.empty(); }

    void add(Point p) {
        m_points.push_back(p);
        m_cc_cached = false;
    }

    void reserve(std::size_t n) { m_points.reserve(n); }
    void clear();

    [[nodiscard]] bool is_hole() const noexcept { return m_hole; }
    void set_hole(bool h) noexcept { m_hole = h; }

    [[nodiscard]] Rect bbox() const noexcept;
    [[nodiscard]] bool counter_clockwise() const;
    void set_clockwise();
    void set_counter_clockwise();
    void reverse();

    /// Signed area: positive = CCW, negative = CW.
    [[nodiscard]] double signed_area() const noexcept;

    [[nodiscard]] const Point& vertex(std::size_t i) const { return m_points[i]; }
    Point& vertex(std::size_t i) { return m_points[i]; }

    void add_hole(std::size_t index) { m_holes.push_back(index); }
    void clear_holes() noexcept { m_holes.clear(); }
    [[nodiscard]] std::span<const std::size_t> holes() const noexcept { return m_holes; }

    /// Remove the last vertex.
    void pop_back() {
        if (!m_points.empty()) {
            m_points.pop_back();
            m_cc_cached = false;
        }
    }

    /// Remove consecutive duplicate vertices and degenerate edges.
    /// Returns the number of vertices removed.
    std::size_t sanitize();

private:
    std::vector<Point> m_points;
    std::vector<std::size_t> m_holes;
    bool m_hole = false;
    mutable bool m_cc_cached = false;
    mutable bool m_cc = false;
};

class Polygon {
public:
    Polygon() = default;

    void add(Contour c) { m_contours.push_back(std::move(c)); }
    void clear() { m_contours.clear(); }
    void pop_back() { m_contours.pop_back(); }

    [[nodiscard]] std::span<const Contour> contours() const noexcept { return m_contours; }
    [[nodiscard]] std::size_t contour_count() const noexcept { return m_contours.size(); }
    [[nodiscard]] std::size_t vertex_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return m_contours.empty(); }

    [[nodiscard]] Rect bbox() const noexcept;

    /// Total filled area: the sum of contour areas with holes subtracted.
    /// Polygons from compute() are ready to query directly; hand-built
    /// polygons need compute_holes() first to orient their holes.
    [[nodiscard]] double area() const noexcept;

    Contour& operator[](std::size_t i) { return m_contours[i]; }
    const Contour& operator[](std::size_t i) const { return m_contours[i]; }
    Contour& back() { return m_contours.back(); }

    auto begin() { return m_contours.begin(); }
    auto end() { return m_contours.end(); }
    auto begin() const { return m_contours.begin(); }
    auto end() const { return m_contours.end(); }

    /// Classify contours as outer boundaries or holes by containment, and
    /// normalize orientation (outer CCW, holes CW) so area() reports the
    /// filled region. compute() does this automatically; call it yourself
    /// on hand-built polygons. O(n^2) in the number of contours.
    void compute_holes();

    /// Check every contour for self-intersection. O(n log n) per contour.
    [[nodiscard]] bool validate() const;

    /// Split self-intersecting contours into simple ones. Call before
    /// compute() if the input may self-intersect. Runs a bounded number of
    /// passes, since deeply nested self-intersections can need more than one.
    void decompose(FillRule rule = FillRule::EvenOdd);

    /// Remove contours with fewer than 3 vertices and sanitize all
    /// remaining contours (remove consecutive duplicates).
    /// Returns the number of contours removed.
    std::size_t sanitize();

private:
    std::vector<Contour> m_contours;

    /// One decomposition pass. Returns true if any contour was split.
    bool decompose_one_pass(FillRule rule);
};

/// Perform a boolean operation on two polygons. Both inputs should consist
/// of simple contours; call decompose() first if they might not.
[[nodiscard]] Polygon compute(Operation op, const Polygon& subject, const Polygon& clip,
                              FillRule rule = FillRule::EvenOdd);

} // namespace polyclip

#endif
