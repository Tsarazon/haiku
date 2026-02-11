/*
 * Copyright 2026 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef POLYCLIP_HPP
#define POLYCLIP_HPP

/// @file polyclip.hpp
/// @brief Martinez-Rueda polygon clipping library.
///
/// Computes boolean operations (intersection, union, difference, xor)
/// on two polygons represented as sets of contours.  Each contour is a
/// simple closed polyline; a Polygon may contain multiple contours
/// including holes.
///
/// Usage:
/// @code
///   polyclip::Polygon subject, clip;
///   // ... populate contours ...
///   auto result = polyclip::compute(polyclip::Operation::Union, subject, clip);
///   result.compute_holes();   // optional: classify inner contours as holes
/// @endcode
///
/// Self-intersecting contours: call decompose() before compute() to split
/// self-intersecting contours into simple ones.  Without decomposition,
/// self-intersecting input may silently lose geometry.
///
/// @par Coordinate contract
/// Input coordinates must be in **device space** (post-transform).
/// The library internally snaps computed intersection points to a grid
/// of spacing 1e-7, which provides ~3 decimal digits of headroom for
/// typical screen coordinates (0-4096).
///
/// @par Coordinate range
/// Safe range: |x|, |y| < 1e6.  At 1e7+ the snap grid approaches
/// IEEE-754 ULP and numerical coincidence detection degrades.
/// Debug builds assert coordinates are within range.

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
///
/// EvenOdd: a point is inside if a ray from it crosses an odd number
///     of edges.  SVG/PDF default.  Original Martinez-Rueda behavior.
///
/// NonZero: a point is inside if the net signed crossing count (winding
///     number) is non-zero.  Required for TrueType glyphs, many icon
///     formats, and Core Graphics kCGPathFillStrokeWinding.
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

    /// Total signed area (sum of all contour signed areas).
    /// Holes contribute negative area.
    [[nodiscard]] double area() const noexcept;

    Contour& operator[](std::size_t i) { return m_contours[i]; }
    const Contour& operator[](std::size_t i) const { return m_contours[i]; }
    Contour& back() { return m_contours.back(); }

    auto begin() { return m_contours.begin(); }
    auto end() { return m_contours.end(); }
    auto begin() const { return m_contours.begin(); }
    auto end() const { return m_contours.end(); }

    /// Classify contours as outer boundaries or holes using a sweep line.
    void compute_holes();

    /// Check all contours for self-intersections.  O(n log n) per contour.
    [[nodiscard]] bool validate() const;

    /// Decompose self-intersecting contours into simple contours.
    /// Call before compute() if input may contain self-intersections.
    void decompose(FillRule rule = FillRule::EvenOdd);

    /// Remove contours with fewer than 3 vertices and sanitize all
    /// remaining contours (remove consecutive duplicates).
    /// Returns the number of contours removed.
    std::size_t sanitize();

private:
    std::vector<Contour> m_contours;
};

/// Perform a boolean operation on two polygons.
///
/// Both @p subject and @p clip should consist of simple contours.
/// Call decompose() first if needed.
[[nodiscard]] Polygon compute(Operation op, const Polygon& subject, const Polygon& clip,
                              FillRule rule = FillRule::EvenOdd);

} // namespace polyclip

#endif
