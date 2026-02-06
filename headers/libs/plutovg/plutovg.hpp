#ifndef PLUTOVG_HPP
#define PLUTOVG_HPP

#include <cstdint>
#include <functional>
#include <string_view>

#if defined(PLUTOVG_BUILD_STATIC)
#define PLUTOVG_EXPORT
#define PLUTOVG_IMPORT
#elif (defined(_WIN32) || defined(__CYGWIN__))
#define PLUTOVG_EXPORT __declspec(dllexport)
#define PLUTOVG_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) && (__GNUC__ >= 4)
#define PLUTOVG_EXPORT __attribute__((__visibility__("default")))
#define PLUTOVG_IMPORT
#else
#define PLUTOVG_EXPORT
#define PLUTOVG_IMPORT
#endif

#ifdef PLUTOVG_BUILD
#define PLUTOVG_API PLUTOVG_EXPORT
#else
#define PLUTOVG_API PLUTOVG_IMPORT
#endif

namespace plutovg {

inline constexpr int version_major = 1;
inline constexpr int version_minor = 3;
inline constexpr int version_micro = 2;

inline constexpr int version_encode(int major, int minor, int micro) {
    return major * 10000 + minor * 100 + micro;
}

inline constexpr int version = version_encode(version_major, version_minor, version_micro);
inline constexpr const char* version_string = "1.3.2";

/**
 * @brief Gets the version of the plutovg library.
 * @return An integer representing the version of the plutovg library.
 */
PLUTOVG_API int get_version();

/**
 * @brief Gets the version of the plutovg library as a string.
 * @return A string representing the version of the plutovg library.
 */
PLUTOVG_API const char* get_version_string();

/**
 * @brief A function type for a cleanup callback.
 * @param closure A pointer to the resource to be cleaned up.
 */
using destroy_func_t = std::function<void(void*)>;

/**
 * @brief A function type for a write callback.
 * @param closure A pointer to user-defined data or context.
 * @param data A pointer to the data to be written.
 * @param size The size of the data in bytes.
 */
using write_func_t = std::function<void(void*, void*, int)>;

inline constexpr float pi      = 3.14159265358979323846f;
inline constexpr float two_pi  = 6.28318530717958647693f;
inline constexpr float half_pi = 1.57079632679489661923f;
inline constexpr float sqrt2   = 1.41421356237309504880f;
inline constexpr float kappa   = 0.55228474983079339840f;

inline constexpr float deg2rad(float x) { return x * (pi / 180.0f); }
inline constexpr float rad2deg(float x) { return x * (180.0f / pi); }

/**
 * @brief A structure representing a point in 2D space.
 */
struct Point {
    float x = 0.0f; ///< The x-coordinate of the point.
    float y = 0.0f; ///< The y-coordinate of the point.

    constexpr Point() = default;
    constexpr Point(float x, float y) : x(x), y(y) {}
};

/**
 * @brief A structure representing a rectangle in 2D space.
 */
struct Rect {
    float x = 0.0f; ///< The x-coordinate of the top-left corner of the rectangle.
    float y = 0.0f; ///< The y-coordinate of the top-left corner of the rectangle.
    float w = 0.0f; ///< The width of the rectangle.
    float h = 0.0f; ///< The height of the rectangle.

    constexpr Rect() = default;
    constexpr Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}
};

/**
 * @brief A structure representing a 2D transformation matrix.
 */
struct Matrix {
    float a = 1.0f; ///< The horizontal scaling factor.
    float b = 0.0f; ///< The vertical shearing factor.
    float c = 0.0f; ///< The horizontal shearing factor.
    float d = 1.0f; ///< The vertical scaling factor.
    float e = 0.0f; ///< The horizontal translation offset.
    float f = 0.0f; ///< The vertical translation offset.

    constexpr Matrix() = default;
    constexpr Matrix(float a, float b, float c, float d, float e, float f)
        : a(a), b(b), c(c), d(d), e(e), f(f) {}

    static constexpr Matrix identity() { return {}; }
    static constexpr Matrix scale(float sx, float sy) { return {sx, 0, 0, sy, 0, 0}; }
    static constexpr Matrix translate(float tx, float ty) { return {1, 0, 0, 1, tx, ty}; }

    /**
     * @brief Initializes a 2D transformation matrix.
     * @param a The horizontal scaling factor.
     * @param b The vertical shearing factor.
     * @param c The horizontal shearing factor.
     * @param d The vertical scaling factor.
     * @param e The horizontal translation offset.
     * @param f The vertical translation offset.
     */
    PLUTOVG_API void init(float a, float b, float c, float d, float e, float f);

    /**
     * @brief Initializes a 2D transformation matrix to the identity matrix.
     */
    PLUTOVG_API void init_identity();

    /**
     * @brief Initializes a 2D transformation matrix for translation.
     * @param tx The translation offset in the x-direction.
     * @param ty The translation offset in the y-direction.
     */
    PLUTOVG_API void init_translate(float tx, float ty);

    /**
     * @brief Initializes a 2D transformation matrix for scaling.
     * @param sx The scaling factor in the x-direction.
     * @param sy The scaling factor in the y-direction.
     */
    PLUTOVG_API void init_scale(float sx, float sy);

    /**
     * @brief Initializes a 2D transformation matrix for rotation.
     * @param angle The rotation angle in radians.
     */
    PLUTOVG_API void init_rotate(float angle);

    /**
     * @brief Initializes a 2D transformation matrix for shearing.
     * @param shx The shearing factor in the x-direction.
     * @param shy The shearing factor in the y-direction.
     */
    PLUTOVG_API void init_shear(float shx, float shy);

    /**
     * @brief Adds a translation with offsets `tx` and `ty` to the matrix.
     * @param tx The translation offset in the x-direction.
     * @param ty The translation offset in the y-direction.
     */
    PLUTOVG_API void apply_translate(float tx, float ty);

    /**
     * @brief Scales the matrix by factors `sx` and `sy`.
     * @param sx The scaling factor in the x-direction.
     * @param sy The scaling factor in the y-direction.
     */
    PLUTOVG_API void apply_scale(float sx, float sy);

    /**
     * @brief Rotates the matrix by the specified angle (in radians).
     * @param angle The rotation angle in radians.
     */
    PLUTOVG_API void apply_rotate(float angle);

    /**
     * @brief Shears the matrix by factors `shx` and `shy`.
     * @param shx The shearing factor in the x-direction.
     * @param shy The shearing factor in the y-direction.
     */
    PLUTOVG_API void apply_shear(float shx, float shy);

    /**
     * @brief Multiplies `left` and `right` matrices and stores the result in `result`.
     * @note `result` can be identical to either `left` or `right`.
     * @param result A reference to the Matrix to store the result.
     * @param left The first Matrix.
     * @param right The second Matrix.
     */
    PLUTOVG_API static void multiply(Matrix& result, const Matrix& left, const Matrix& right);

    /**
     * @brief Calculates the inverse of this matrix and stores it in `inverse`.
     *
     * If `inverse` is `nullptr`, the function only checks if the matrix is invertible.
     *
     * @param inverse A pointer to the Matrix to store the result, or `nullptr`.
     * @return `true` if the matrix is invertible; `false` otherwise.
     */
    PLUTOVG_API bool invert(Matrix* inverse) const;

    /**
     * @brief Transforms the point `(x, y)` using this matrix and stores the result in `(xx, yy)`.
     * @param x The x-coordinate of the point to transform.
     * @param y The y-coordinate of the point to transform.
     * @param xx A reference to store the transformed x-coordinate.
     * @param yy A reference to store the transformed y-coordinate.
     */
    PLUTOVG_API void map(float x, float y, float& xx, float& yy) const;

    /**
     * @brief Transforms the `src` point using this matrix and stores the result in `dst`.
     * @note `src` and `dst` can be identical.
     * @param src The Point to transform.
     * @param dst The Point to store the transformed result.
     */
    PLUTOVG_API void map_point(const Point& src, Point& dst) const;

    /**
     * @brief Transforms an array of `src` points using this matrix and stores the results in `dst`.
     * @note `src` and `dst` can be identical.
     * @param src A pointer to the array of Points to transform.
     * @param dst A pointer to the array of Points to store the results.
     * @param count The number of points to transform.
     */
    PLUTOVG_API void map_points(const Point* src, Point* dst, int count) const;

    /**
     * @brief Transforms the `src` rectangle using this matrix and stores the result in `dst`.
     * @note `src` and `dst` can be identical.
     * @param src The Rect to transform.
     * @param dst The Rect to store the transformed result.
     */
    PLUTOVG_API void map_rect(const Rect& src, Rect& dst) const;

    /**
     * @brief Parses an SVG transform string into a matrix.
     * @param matrix A reference to the Matrix to store the result.
     * @param data Input SVG transform string.
     * @param length Length of the string, or `-1` if null-terminated.
     * @return `true` on success, `false` on failure.
     */
    PLUTOVG_API static bool parse(Matrix& matrix, const char* data, int length);
};

/**
 * @brief Enumeration defining path commands.
 */
enum class PathCommand {
    MoveTo,  ///< Moves the current point to a new position.
    LineTo,  ///< Draws a straight line to a new point.
    CubicTo, ///< Draws a cubic Bezier curve to a new point.
    Close    ///< Closes the current path by drawing a line to the starting point.
};

/**
 * @brief Union representing a path element.
 *
 * A path element can be a command with a length or a coordinate point.
 * Each command type in the path element array is followed by a specific number of points:
 * - `PathCommand::MoveTo`: 1 point
 * - `PathCommand::LineTo`: 1 point
 * - `PathCommand::CubicTo`: 3 points
 * - `PathCommand::Close`: 1 point
 */
union PathElement {
    struct {
        PathCommand command; ///< The path command.
        int length;          ///< Number of elements including the header.
    } header; ///< Header for path commands.
    Point point; ///< A coordinate point in the path.
};

/**
 * @brief Iterator for traversing path elements in a path.
 */
struct PathIterator {
    const PathElement* elements = nullptr; ///< Pointer to the array of path elements.
    int size = 0;                          ///< Total number of elements in the array.
    int index = 0;                         ///< Current position in the array.
};

class Path;

/**
 * @brief Initializes a path iterator for a given path.
 * @param it The path iterator to initialize.
 * @param path The path to iterate over.
 */
PLUTOVG_API void path_iterator_init(PathIterator& it, const Path* path);

/**
 * @brief Checks if there are more elements to iterate over.
 * @param it The path iterator.
 * @return `true` if there are more elements; otherwise, `false`.
 */
PLUTOVG_API bool path_iterator_has_next(const PathIterator& it);

/**
 * @brief Retrieves the current command and its associated points, then advances the iterator.
 * @param it The path iterator.
 * @param points An array to store the points for the current command.
 * @return The path command for the current element.
 */
PLUTOVG_API PathCommand path_iterator_next(PathIterator& it, Point points[3]);

/**
 * @brief Represents a 2D path for drawing operations.
 */
class PLUTOVG_API Path {
public:
    /**
     * @brief Creates a new path object.
     * @return A pointer to the newly created path object.
     */
    static Path* create();

    /**
     * @brief Increases the reference count of a path object.
     * @return A pointer to the same Path object.
     */
    Path* reference();

    /**
     * @brief Decreases the reference count of a path object.
     *
     * If the reference count reaches zero, the path object is destroyed
     * and its resources are freed.
     */
    void destroy();

    /**
     * @brief Retrieves the reference count of a path object.
     * @return The current reference count of the path object.
     */
    int get_reference_count() const;

    /**
     * @brief Retrieves the elements of a path.
     *
     * Provides access to the array of path elements.
     *
     * @param elements A pointer to a pointer that will be set to the array of path elements.
     * @return The number of elements in the path.
     */
    int get_elements(const PathElement** elements) const;

    /**
     * @brief Moves the current point to a new position.
     *
     * Equivalent to the `M` command in SVG path syntax.
     *
     * @param x The x-coordinate of the new position.
     * @param y The y-coordinate of the new position.
     */
    void move_to(float x, float y);

    /**
     * @brief Adds a straight line segment to the path.
     *
     * Equivalent to the `L` command in SVG path syntax.
     *
     * @param x The x-coordinate of the end point of the line segment.
     * @param y The y-coordinate of the end point of the line segment.
     */
    void line_to(float x, float y);

    /**
     * @brief Adds a quadratic Bezier curve to the path.
     *
     * Equivalent to the `Q` command in SVG path syntax.
     *
     * @param x1 The x-coordinate of the control point.
     * @param y1 The y-coordinate of the control point.
     * @param x2 The x-coordinate of the end point of the curve.
     * @param y2 The y-coordinate of the end point of the curve.
     */
    void quad_to(float x1, float y1, float x2, float y2);

    /**
     * @brief Adds a cubic Bezier curve to the path.
     *
     * Equivalent to the `C` command in SVG path syntax.
     *
     * @param x1 The x-coordinate of the first control point.
     * @param y1 The y-coordinate of the first control point.
     * @param x2 The x-coordinate of the second control point.
     * @param y2 The y-coordinate of the second control point.
     * @param x3 The x-coordinate of the end point of the curve.
     * @param y3 The y-coordinate of the end point of the curve.
     */
    void cubic_to(float x1, float y1, float x2, float y2, float x3, float y3);

    /**
     * @brief Adds an elliptical arc to the path.
     *
     * Equivalent to the `A` command in SVG path syntax.
     *
     * @param rx The x-radius of the ellipse.
     * @param ry The y-radius of the ellipse.
     * @param angle The rotation angle of the ellipse in radians.
     * @param large_arc_flag If true, draw the large arc; otherwise, draw the small arc.
     * @param sweep_flag If true, draw the arc in the positive-angle direction.
     * @param x The x-coordinate of the end point of the arc.
     * @param y The y-coordinate of the end point of the arc.
     */
    void arc_to(float rx, float ry, float angle, bool large_arc_flag, bool sweep_flag, float x, float y);

    /**
     * @brief Closes the current sub-path.
     *
     * Equivalent to the `Z` command in SVG path syntax.
     */
    void close();

    /**
     * @brief Retrieves the current point of the path.
     * @param x The x-coordinate of the current point.
     * @param y The y-coordinate of the current point.
     */
    void get_current_point(float& x, float& y) const;

    /**
     * @brief Reserves space for path elements.
     * @param count The number of path elements to reserve space for.
     */
    void reserve(int count);

    /**
     * @brief Resets the path, clearing all path data.
     */
    void reset();

    /**
     * @brief Adds a rectangle to the path.
     * @param x The x-coordinate of the rectangle's top-left corner.
     * @param y The y-coordinate of the rectangle's top-left corner.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     */
    void add_rect(float x, float y, float w, float h);

    /**
     * @brief Adds a rounded rectangle to the path.
     * @param x The x-coordinate of the rectangle's top-left corner.
     * @param y The y-coordinate of the rectangle's top-left corner.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     * @param rx The x-radius of the rectangle's corners.
     * @param ry The y-radius of the rectangle's corners.
     */
    void add_round_rect(float x, float y, float w, float h, float rx, float ry);

    /**
     * @brief Adds an ellipse to the path.
     * @param cx The x-coordinate of the ellipse's center.
     * @param cy The y-coordinate of the ellipse's center.
     * @param rx The x-radius of the ellipse.
     * @param ry The y-radius of the ellipse.
     */
    void add_ellipse(float cx, float cy, float rx, float ry);

    /**
     * @brief Adds a circle to the path.
     * @param cx The x-coordinate of the circle's center.
     * @param cy The y-coordinate of the circle's center.
     * @param r The radius of the circle.
     */
    void add_circle(float cx, float cy, float r);

    /**
     * @brief Adds an arc to the path.
     * @param cx The x-coordinate of the arc's center.
     * @param cy The y-coordinate of the arc's center.
     * @param r The radius of the arc.
     * @param a0 The start angle of the arc in radians.
     * @param a1 The end angle of the arc in radians.
     * @param ccw If true, the arc is drawn counter-clockwise; if false, clockwise.
     */
    void add_arc(float cx, float cy, float r, float a0, float a1, bool ccw);

    /**
     * @brief Adds a sub-path to the path.
     * @param source A pointer to the Path to copy elements from.
     * @param matrix A pointer to a Matrix, or `nullptr` for no transformation.
     */
    void add_path(const Path* source, const Matrix* matrix);

    /**
     * @brief Applies a transformation matrix to the path.
     * @param matrix A pointer to a Matrix.
     */
    void transform(const Matrix* matrix);

    /**
     * @brief Callback function type for traversing a path.
     * @param closure A pointer to user-defined data.
     * @param command The current path command.
     * @param points An array of points associated with the command.
     * @param npoints The number of points in the array.
     */
    using traverse_func_t = void(*)(void*, PathCommand, const Point*, int);

    /**
     * @brief Traverses the path and calls the callback for each element.
     * @param func The callback function.
     * @param closure User-defined data passed to the callback.
     */
    void traverse(traverse_func_t func, void* closure) const;

    /**
     * @brief Traverses the path with Bezier curves flattened to line segments.
     * @param func The callback function.
     * @param closure User-defined data passed to the callback.
     */
    void traverse_flatten(traverse_func_t func, void* closure) const;

    /**
     * @brief Traverses the path with a dashed pattern.
     * @param offset The starting offset into the dash pattern.
     * @param dashes An array of dash lengths.
     * @param ndashes The number of elements in the `dashes` array.
     * @param func The callback function.
     * @param closure User-defined data passed to the callback.
     */
    void traverse_dashed(float offset, const float* dashes, int ndashes, traverse_func_t func, void* closure) const;

    /**
     * @brief Creates a copy of the path.
     * @return A pointer to the newly created path clone.
     */
    Path* clone() const;

    /**
     * @brief Creates a copy of the path with Bezier curves flattened to line segments.
     * @return A pointer to the newly created path clone with flattened curves.
     */
    Path* clone_flatten() const;

    /**
     * @brief Creates a copy of the path with a dashed pattern applied.
     * @param offset The starting offset into the dash pattern.
     * @param dashes An array of dash lengths.
     * @param ndashes The number of elements in the `dashes` array.
     * @return A pointer to the newly created path clone with dashed pattern.
     */
    Path* clone_dashed(float offset, const float* dashes, int ndashes) const;

    /**
     * @brief Computes the bounding box and total length of the path.
     * @param rect A pointer to a Rect to store the bounding box.
     * @param tight If `true`, computes a precise bounding box; otherwise, aligns to control points.
     * @return The total length of the path.
     */
    float extents(Rect* rect, bool tight) const;

    /**
     * @brief Calculates the total length of the path.
     * @return The total length of the path.
     */
    float length() const;

    /**
     * @brief Parses SVG path data into this path object.
     * @param data The SVG path data string.
     * @param length The length of `data`, or `-1` for null-terminated data.
     * @return `true` if successful; `false` otherwise.
     */
    bool parse(const char* data, int length);
};

/**
 * @brief Text encodings used for converting text data to code points.
 */
enum class TextEncoding {
    Latin1, ///< Latin-1 encoding
    UTF8,   ///< UTF-8 encoding
    UTF16,  ///< UTF-16 encoding
    UTF32   ///< UTF-32 encoding
};

/**
 * @brief Iterator for traversing code points in text data.
 */
struct TextIterator {
    const void* text = nullptr;              ///< Pointer to the text data.
    int length = 0;                          ///< Length of the text data.
    TextEncoding encoding = TextEncoding::UTF8; ///< Encoding format of the text data.
    int index = 0;                           ///< Current position in the text data.
};

/**
 * @brief Represents a Unicode code point.
 */
using Codepoint = unsigned int;

/**
 * @brief Initializes a text iterator.
 * @param it Pointer to the text iterator.
 * @param text Pointer to the text data.
 * @param length Length of the text data, or -1 if the data is null-terminated.
 * @param encoding Encoding of the text data.
 */
PLUTOVG_API void text_iterator_init(TextIterator& it, const void* text, int length, TextEncoding encoding);

/**
 * @brief Checks if there are more code points to iterate.
 * @param it Pointer to the text iterator.
 * @return `true` if more code points are available; otherwise, `false`.
 */
PLUTOVG_API bool text_iterator_has_next(const TextIterator& it);

/**
 * @brief Retrieves the next code point and advances the iterator.
 * @param it Pointer to the text iterator.
 * @return The next code point.
 */
PLUTOVG_API Codepoint text_iterator_next(TextIterator& it);

/**
 * @brief Represents a font face.
 */
class PLUTOVG_API FontFace {
public:
    /**
     * @brief Loads a font face from a file.
     * @param filename Path to the font file.
     * @param ttcindex Index of the font face within a TrueType Collection (TTC).
     * @return A pointer to the loaded FontFace, or `nullptr` on failure.
     */
    static FontFace* load_from_file(const char* filename, int ttcindex);

    /**
     * @brief Loads a font face from memory.
     * @param data Pointer to the font data.
     * @param length Length of the font data.
     * @param ttcindex Index of the font face within a TrueType Collection (TTC).
     * @param destroy_func Function to free the font data when no longer needed.
     * @param closure User-defined data passed to `destroy_func`.
     * @return A pointer to the loaded FontFace, or `nullptr` on failure.
     */
    static FontFace* load_from_data(const void* data, unsigned int length, int ttcindex,
                                     void(*destroy_func)(void*), void* closure);

    /**
     * @brief Increments the reference count of a font face.
     * @return A pointer to the same FontFace with an incremented reference count.
     */
    FontFace* reference();

    /**
     * @brief Decrements the reference count and potentially destroys the font face.
     */
    void destroy();

    /**
     * @brief Retrieves the current reference count of a font face.
     * @return The reference count of the font face.
     */
    int get_reference_count() const;

    /**
     * @brief Retrieves metrics for a font face at a specified size.
     * @param size The font size in pixels.
     * @param ascent Pointer to store the ascent metric.
     * @param descent Pointer to store the descent metric.
     * @param line_gap Pointer to store the line gap metric.
     * @param extents Pointer to a Rect to store the font bounding box.
     */
    void get_metrics(float size, float* ascent, float* descent, float* line_gap, Rect* extents) const;

    /**
     * @brief Retrieves metrics for a specified glyph at a given size.
     * @param size The font size in pixels.
     * @param codepoint The Unicode code point of the glyph.
     * @param advance_width Pointer to store the advance width of the glyph.
     * @param left_side_bearing Pointer to store the left side bearing of the glyph.
     * @param extents Pointer to a Rect to store the glyph bounding box.
     */
    void get_glyph_metrics(float size, Codepoint codepoint, float* advance_width,
                           float* left_side_bearing, Rect* extents);

    /**
     * @brief Retrieves the path of a glyph and its advance width.
     * @param size The font size in pixels.
     * @param x The x-coordinate for positioning the glyph.
     * @param y The y-coordinate for positioning the glyph.
     * @param codepoint The Unicode code point of the glyph.
     * @param path Pointer to a Path to store the glyph path.
     * @return The advance width of the glyph.
     */
    float get_glyph_path(float size, float x, float y, Codepoint codepoint, Path* path);

    /**
     * @brief Traverses the path of a glyph and calls a callback for each path element.
     * @param size The font size in pixels.
     * @param x The x-coordinate for positioning the glyph.
     * @param y The y-coordinate for positioning the glyph.
     * @param codepoint The Unicode code point of the glyph.
     * @param func The callback function.
     * @param closure User-defined data passed to the callback function.
     * @return The advance width of the glyph.
     */
    float traverse_glyph_path(float size, float x, float y, Codepoint codepoint,
                              Path::traverse_func_t func, void* closure);

    /**
     * @brief Computes the bounding box of a text string and its advance width.
     * @param size The font size in pixels.
     * @param text Pointer to the text data.
     * @param length Length of the text data, or -1 if null-terminated.
     * @param encoding Encoding of the text data.
     * @param extents Pointer to a Rect to store the bounding box.
     * @return The total advance width of the text.
     */
    float text_extents(float size, const void* text, int length, TextEncoding encoding, Rect* extents);
};

/**
 * @brief Represents a cache of loaded font faces.
 */
class PLUTOVG_API FontFaceCache {
public:
    /**
     * @brief Create a new, empty font-face cache.
     * @return Pointer to a newly allocated FontFaceCache.
     */
    static FontFaceCache* create();

    /**
     * @brief Increments the reference count of a font-face cache.
     * @return A pointer to the same FontFaceCache with an incremented reference count.
     */
    FontFaceCache* reference();

    /**
     * @brief Decrement the reference count and destroy when it reaches zero.
     */
    void destroy();

    /**
     * @brief Retrieve the current reference count of a font-face cache.
     * @return The current reference count, or 0 if cache is nullptr.
     */
    int reference_count() const;

    /**
     * @brief Remove all entries from a font-face cache.
     */
    void reset();

    /**
     * @brief Add a font face to the cache with the specified family and style.
     * @param family The font family name.
     * @param bold Whether the font is bold.
     * @param italic Whether the font is italic.
     * @param face A pointer to the FontFace to add. The cache increments its reference count.
     */
    void add(const char* family, bool bold, bool italic, FontFace* face);

    /**
     * @brief Load a font face from a file and add it to the cache.
     * @param family The font family name to associate with the face.
     * @param bold Whether the font is bold.
     * @param italic Whether the font is italic.
     * @param filename Path to the font file.
     * @param ttcindex Index of the face in a TrueType collection (use 0 for non-TTC fonts).
     * @return `true` on success, `false` if the file could not be loaded.
     */
    bool add_file(const char* family, bool bold, bool italic, const char* filename, int ttcindex);

    /**
     * @brief Retrieve a font face from the cache by family and style.
     * @param family The font family name.
     * @param bold Whether the font is bold.
     * @param italic Whether the font is italic.
     * @return A pointer to the matching FontFace, or nullptr if not found.
     *         The returned face is owned by the cache and must not be destroyed by the caller.
     */
    FontFace* get(const char* family, bool bold, bool italic);

    /**
     * @brief Load all font faces from a file and add them to the cache.
     * @param filename Path to the font file (TrueType, OpenType, or font collection).
     * @return The number of faces successfully loaded, or `-1` if loading is disabled.
     */
    int load_file(const char* filename);

    /**
     * @brief Load all font faces from files in a directory recursively.
     * @param dirname Path to the directory containing font files.
     * @return The number of faces successfully loaded, or `-1` if loading is disabled.
     */
    int load_dir(const char* dirname);

    /**
     * @brief Load all available system font faces and add them to the cache.
     * @return The number of faces successfully loaded, or `-1` if loading is disabled.
     */
    int load_sys();
};

/**
 * @brief Represents a color with red, green, blue, and alpha components.
 */
struct Color {
    float r = 0.0f; ///< Red component (0 to 1).
    float g = 0.0f; ///< Green component (0 to 1).
    float b = 0.0f; ///< Blue component (0 to 1).
    float a = 1.0f; ///< Alpha (opacity) component (0 to 1).

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static constexpr Color black()   { return {0, 0, 0, 1}; }
    static constexpr Color white()   { return {1, 1, 1, 1}; }
    static constexpr Color red()     { return {1, 0, 0, 1}; }
    static constexpr Color green()   { return {0, 1, 0, 1}; }
    static constexpr Color blue()    { return {0, 0, 1, 1}; }
    static constexpr Color yellow()  { return {1, 1, 0, 1}; }
    static constexpr Color cyan()    { return {0, 1, 1, 1}; }
    static constexpr Color magenta() { return {1, 0, 1, 1}; }

    /**
     * @brief Initializes a color using RGB components in the 0-1 range.
     * @param r Red component (0 to 1).
     * @param g Green component (0 to 1).
     * @param b Blue component (0 to 1).
     */
    PLUTOVG_API void init_rgb(float r, float g, float b);

    /**
     * @brief Initializes a color using RGBA components in the 0-1 range.
     * @param r Red component (0 to 1).
     * @param g Green component (0 to 1).
     * @param b Blue component (0 to 1).
     * @param a Alpha component (0 to 1).
     */
    PLUTOVG_API void init_rgba(float r, float g, float b, float a);

    /**
     * @brief Initializes a color using RGB components in the 0-255 range.
     * @param r Red component (0 to 255).
     * @param g Green component (0 to 255).
     * @param b Blue component (0 to 255).
     */
    PLUTOVG_API void init_rgb8(int r, int g, int b);

    /**
     * @brief Initializes a color using RGBA components in the 0-255 range.
     * @param r Red component (0 to 255).
     * @param g Green component (0 to 255).
     * @param b Blue component (0 to 255).
     * @param a Alpha component (0 to 255).
     */
    PLUTOVG_API void init_rgba8(int r, int g, int b, int a);

    /**
     * @brief Initializes a color from a 32-bit unsigned RGBA value.
     * @param value 32-bit unsigned RGBA value.
     */
    PLUTOVG_API void init_rgba32(unsigned int value);

    /**
     * @brief Initializes a color from a 32-bit unsigned ARGB value.
     * @param value 32-bit unsigned ARGB value.
     */
    PLUTOVG_API void init_argb32(unsigned int value);

    /**
     * @brief Initializes a color with the specified HSL color values.
     * @param h Hue component in degrees (0 to 360).
     * @param s Saturation component (0 to 1).
     * @param l Lightness component (0 to 1).
     */
    PLUTOVG_API void init_hsl(float h, float s, float l);

    /**
     * @brief Initializes a color with the specified HSLA color values.
     * @param h Hue component in degrees (0 to 360).
     * @param s Saturation component (0 to 1).
     * @param l Lightness component (0 to 1).
     * @param a Alpha component (0 to 1).
     */
    PLUTOVG_API void init_hsla(float h, float s, float l, float a);

    /**
     * @brief Converts a color to a 32-bit unsigned RGBA value.
     * @return 32-bit unsigned RGBA value.
     */
    PLUTOVG_API unsigned int to_rgba32() const;

    /**
     * @brief Converts a color to a 32-bit unsigned ARGB value.
     * @return 32-bit unsigned ARGB value.
     */
    PLUTOVG_API unsigned int to_argb32() const;

    /**
     * @brief Parses a color from a string using CSS color syntax.
     * @param color A reference to the Color to store the parsed color.
     * @param data A pointer to the input string containing the color data.
     * @param length The length of the input string in bytes, or `-1` if null-terminated.
     * @return The number of characters consumed on success, or 0 on failure.
     */
    PLUTOVG_API static int parse(Color& color, const char* data, int length);
};

/**
 * @brief Represents an image surface for drawing operations.
 *
 * Stores pixel data in a 32-bit premultiplied ARGB format (0xAARRGGBB),
 * where red, green, and blue channels are multiplied by the alpha channel
 * and divided by 255.
 */
class PLUTOVG_API Surface {
public:
    /**
     * @brief Creates a new image surface with the specified dimensions.
     * @param width The width of the surface in pixels.
     * @param height The height of the surface in pixels.
     * @return A pointer to the newly created Surface.
     */
    static Surface* create(int width, int height);

    /**
     * @brief Creates an image surface using existing pixel data.
     * @param data Pointer to the pixel data.
     * @param width The width of the surface in pixels.
     * @param height The height of the surface in pixels.
     * @param stride The number of bytes per row in the pixel data.
     * @return A pointer to the newly created Surface.
     */
    static Surface* create_for_data(unsigned char* data, int width, int height, int stride);

    /**
     * @brief Loads an image surface from a file.
     * @param filename Path to the image file.
     * @return Pointer to the surface, or `nullptr` on failure.
     */
    static Surface* load_from_image_file(const char* filename);

    /**
     * @brief Loads an image surface from raw image data.
     * @param data Pointer to the image data.
     * @param length Length of the data in bytes.
     * @return Pointer to the surface, or `nullptr` on failure.
     */
    static Surface* load_from_image_data(const void* data, int length);

    /**
     * @brief Loads an image surface from base64-encoded data.
     * @param data Pointer to the base64-encoded image data.
     * @param length Length of the data in bytes, or `-1` if null-terminated.
     * @return Pointer to the surface, or `nullptr` on failure.
     */
    static Surface* load_from_image_base64(const char* data, int length);

    /**
     * @brief Increments the reference count for a surface.
     * @return Pointer to the Surface.
     */
    Surface* reference();

    /**
     * @brief Decrements the reference count and destroys the surface if it reaches zero.
     */
    void destroy();

    /**
     * @brief Gets the current reference count of a surface.
     * @return The reference count of the surface.
     */
    int get_reference_count() const;

    /**
     * @brief Gets the pixel data of the surface.
     * @return Pointer to the pixel data.
     */
    unsigned char* get_data() const;

    /**
     * @brief Gets the width of the surface.
     * @return Width of the surface in pixels.
     */
    int get_width() const;

    /**
     * @brief Gets the height of the surface.
     * @return Height of the surface in pixels.
     */
    int get_height() const;

    /**
     * @brief Gets the stride of the surface.
     * @return Number of bytes per row.
     */
    int get_stride() const;

    /**
     * @brief Clears the entire surface with the specified color.
     * @param color The color used for clearing.
     */
    void clear(const Color& color);

    /**
     * @brief Writes the surface to a PNG file.
     * @param filename Path to the output PNG file.
     * @return `true` if successful, `false` otherwise.
     */
    bool write_to_png(const char* filename) const;

    /**
     * @brief Writes the surface to a JPEG file.
     * @param filename Path to the output JPEG file.
     * @param quality JPEG quality (0 to 100).
     * @return `true` if successful, `false` otherwise.
     */
    bool write_to_jpg(const char* filename, int quality) const;

    /**
     * @brief Writes the surface to a PNG stream.
     * @param write_func Callback function for writing data.
     * @param closure User-defined data passed to the callback.
     * @return `true` if successful, `false` otherwise.
     */
    bool write_to_png_stream(void(*write_func)(void*, void*, int), void* closure) const;

    /**
     * @brief Writes the surface to a JPEG stream.
     * @param write_func Callback function for writing data.
     * @param closure User-defined data passed to the callback.
     * @param quality JPEG quality (0 to 100).
     * @return `true` if successful, `false` otherwise.
     */
    bool write_to_jpg_stream(void(*write_func)(void*, void*, int), void* closure, int quality) const;
};

/**
 * @brief Converts pixel data from premultiplied ARGB to RGBA format.
 * @param dst Pointer to the destination buffer (can overlap with `src`).
 * @param src Pointer to the source buffer in ARGB premultiplied format.
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param stride Number of bytes per image row in the buffers.
 */
PLUTOVG_API void convert_argb_to_rgba(unsigned char* dst, const unsigned char* src, int width, int height, int stride);

/**
 * @brief Converts pixel data from RGBA to premultiplied ARGB format.
 * @param dst Pointer to the destination buffer (can overlap with `src`).
 * @param src Pointer to the source buffer in RGBA format.
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param stride Number of bytes per image row in the buffers.
 */
PLUTOVG_API void convert_rgba_to_argb(unsigned char* dst, const unsigned char* src, int width, int height, int stride);

/**
 * @brief Defines the type of texture, either plain or tiled.
 */
enum class TextureType {
    Plain, ///< Plain texture.
    Tiled  ///< Tiled texture.
};

/**
 * @brief Defines the spread method for gradients.
 */
enum class SpreadMethod {
    Pad,     ///< Pad the gradient's edges.
    Reflect, ///< Reflect the gradient beyond its bounds.
    Repeat   ///< Repeat the gradient pattern.
};

/**
 * @brief Represents a gradient stop.
 */
struct GradientStop {
    float offset = 0.0f;  ///< The offset of the gradient stop, as a value between 0 and 1.
    Color color;           ///< The color of the gradient stop.

    constexpr GradientStop() = default;
    constexpr GradientStop(float offset, const Color& color) : offset(offset), color(color) {}
};

/**
 * @brief Represents a paint object used for drawing operations.
 */
class PLUTOVG_API Paint {
public:
    /**
     * @brief Creates a solid RGB paint.
     * @param r The red component (0 to 1).
     * @param g The green component (0 to 1).
     * @param b The blue component (0 to 1).
     * @return A pointer to the created Paint.
     */
    static Paint* create_rgb(float r, float g, float b);

    /**
     * @brief Creates a solid RGBA paint.
     * @param r The red component (0 to 1).
     * @param g The green component (0 to 1).
     * @param b The blue component (0 to 1).
     * @param a The alpha component (0 to 1).
     * @return A pointer to the created Paint.
     */
    static Paint* create_rgba(float r, float g, float b, float a);

    /**
     * @brief Creates a solid color paint.
     * @param color The Color.
     * @return A pointer to the created Paint.
     */
    static Paint* create_color(const Color& color);

    /**
     * @brief Creates a linear gradient paint.
     * @param x1 The x coordinate of the gradient start.
     * @param y1 The y coordinate of the gradient start.
     * @param x2 The x coordinate of the gradient end.
     * @param y2 The y coordinate of the gradient end.
     * @param spread The gradient spread method.
     * @param stops Array of gradient stops.
     * @param nstops Number of gradient stops.
     * @param matrix Optional transformation matrix.
     * @return A pointer to the created Paint.
     */
    static Paint* create_linear_gradient(float x1, float y1, float x2, float y2,
                                         SpreadMethod spread, const GradientStop* stops, int nstops,
                                         const Matrix* matrix);

    /**
     * @brief Creates a radial gradient paint.
     * @param cx The x coordinate of the gradient center.
     * @param cy The y coordinate of the gradient center.
     * @param cr The radius of the gradient.
     * @param fx The x coordinate of the focal point.
     * @param fy The y coordinate of the focal point.
     * @param fr The radius of the focal point.
     * @param spread The gradient spread method.
     * @param stops Array of gradient stops.
     * @param nstops Number of gradient stops.
     * @param matrix Optional transformation matrix.
     * @return A pointer to the created Paint.
     */
    static Paint* create_radial_gradient(float cx, float cy, float cr, float fx, float fy, float fr,
                                         SpreadMethod spread, const GradientStop* stops, int nstops,
                                         const Matrix* matrix);

    /**
     * @brief Creates a texture paint from a surface.
     * @param surface The texture surface.
     * @param type The texture type (plain or tiled).
     * @param opacity The opacity of the texture (0 to 1).
     * @param matrix Optional transformation matrix.
     * @return A pointer to the created Paint.
     */
    static Paint* create_texture(Surface* surface, TextureType type, float opacity, const Matrix* matrix);

    /**
     * @brief Increments the reference count of a paint object.
     * @return A pointer to the referenced Paint.
     */
    Paint* reference();

    /**
     * @brief Decrements the reference count and destroys the paint if it reaches zero.
     */
    void destroy();

    /**
     * @brief Retrieves the reference count of a paint object.
     * @return The reference count.
     */
    int get_reference_count() const;
};

/**
 * @brief Defines fill rule types for filling paths.
 */
enum class FillRule {
    NonZero, ///< Non-zero winding fill rule.
    EvenOdd  ///< Even-odd fill rule.
};

/**
 * @brief Defines compositing operations.
 */
enum class Operator {
    Clear,   ///< Clears the destination (resulting in a fully transparent image).
    Src,     ///< Source replaces destination.
    Dst,     ///< Destination is kept, source is ignored.
    SrcOver, ///< Source is composited over destination.
    DstOver, ///< Destination is composited over source.
    SrcIn,   ///< Source within destination.
    DstIn,   ///< Destination within source.
    SrcOut,  ///< Source outside destination.
    DstOut,  ///< Destination outside source.
    SrcAtop, ///< Source atop destination.
    DstAtop, ///< Destination atop source.
    Xor      ///< Source and destination combined, overlapping regions cleared.
};

/**
 * @brief Defines the shape used at the ends of open subpaths.
 */
enum class LineCap {
    Butt,   ///< Flat edge at the end of the stroke.
    Round,  ///< Rounded ends at the end of the stroke.
    Square  ///< Square ends at the end of the stroke.
};

/**
 * @brief Defines the shape used at the corners of paths.
 */
enum class LineJoin {
    Miter, ///< Miter join with sharp corners.
    Round, ///< Rounded join.
    Bevel  ///< Beveled join with a flattened corner.
};

/**
 * @brief Represents a drawing context.
 */
class PLUTOVG_API Canvas {
public:
    /**
     * @brief Creates a drawing context on a surface.
     * @param surface A pointer to a Surface.
     * @return A pointer to the newly created Canvas.
     */
    static Canvas* create(Surface* surface);

    /**
     * @brief Increases the reference count of the canvas.
     * @return The same pointer to the Canvas.
     */
    Canvas* reference();

    /**
     * @brief Decreases the reference count and destroys the canvas when it reaches zero.
     */
    void destroy();

    /**
     * @brief Retrieves the reference count of the canvas.
     * @return The current reference count.
     */
    int get_reference_count() const;

    /**
     * @brief Gets the surface associated with the canvas.
     * @return A pointer to the Surface.
     */
    Surface* get_surface() const;

    /**
     * @brief Saves the current state of the canvas.
     */
    void save();

    /**
     * @brief Restores the canvas to the most recently saved state.
     */
    void restore();

    /**
     * @brief Sets the current paint to a solid color (RGB).
     *
     * If not set, the default paint is opaque black color.
     *
     * @param r The red component (0 to 1).
     * @param g The green component (0 to 1).
     * @param b The blue component (0 to 1).
     */
    void set_rgb(float r, float g, float b);

    /**
     * @brief Sets the current paint to a solid color (RGBA).
     *
     * If not set, the default paint is opaque black color.
     *
     * @param r The red component (0 to 1).
     * @param g The green component (0 to 1).
     * @param b The blue component (0 to 1).
     * @param a The alpha component (0 to 1).
     */
    void set_rgba(float r, float g, float b, float a);

    /**
     * @brief Sets the current paint to a solid color.
     *
     * If not set, the default paint is opaque black color.
     *
     * @param color The Color.
     */
    void set_color(const Color& color);

    /**
     * @brief Sets the current paint to a linear gradient.
     *
     * If not set, the default paint is opaque black color.
     *
     * @param x1 The x coordinate of the start point.
     * @param y1 The y coordinate of the start point.
     * @param x2 The x coordinate of the end point.
     * @param y2 The y coordinate of the end point.
     * @param spread The gradient spread method.
     * @param stops Array of gradient stops.
     * @param nstops Number of gradient stops.
     * @param matrix Optional transformation matrix.
     */
    void set_linear_gradient(float x1, float y1, float x2, float y2,
                             SpreadMethod spread, const GradientStop* stops, int nstops,
                             const Matrix* matrix);

    /**
     * @brief Sets the current paint to a radial gradient.
     *
     * If not set, the default paint is opaque black color.
     *
     * @param cx The x coordinate of the center.
     * @param cy The y coordinate of the center.
     * @param cr The radius of the gradient.
     * @param fx The x coordinate of the focal point.
     * @param fy The y coordinate of the focal point.
     * @param fr The radius of the focal point.
     * @param spread The gradient spread method.
     * @param stops Array of gradient stops.
     * @param nstops Number of gradient stops.
     * @param matrix Optional transformation matrix.
     */
    void set_radial_gradient(float cx, float cy, float cr, float fx, float fy, float fr,
                             SpreadMethod spread, const GradientStop* stops, int nstops,
                             const Matrix* matrix);

    /**
     * @brief Sets the current paint to a texture.
     *
     * If not set, the default paint is opaque black color.
     *
     * @param surface The texture surface.
     * @param type The texture type (plain or tiled).
     * @param opacity The opacity of the texture (0 to 1).
     * @param matrix Optional transformation matrix.
     */
    void set_texture(Surface* surface, TextureType type, float opacity, const Matrix* matrix);

    /**
     * @brief Sets the current paint.
     *
     * If not set, the default paint is opaque black color.
     *
     * @param paint The paint to be used for subsequent drawing operations.
     */
    void set_paint(Paint* paint);

    /**
     * @brief Retrieves the current paint.
     *
     * If not set, the default paint is opaque black color.
     *
     * @param color A pointer to a Color where the current color will be stored.
     * @return The current Paint, or `nullptr` if no paint is set.
     */
    Paint* get_paint(Color* color) const;

    /**
     * @brief Assigns a font-face cache to the canvas for font management.
     * @param cache A pointer to a FontFaceCache, or nullptr to unset.
     */
    void set_font_face_cache(FontFaceCache* cache);

    /**
     * @brief Returns the font-face cache associated with the canvas.
     * @return A pointer to the FontFaceCache, or nullptr if none is set.
     */
    FontFaceCache* get_font_face_cache() const;

    /**
     * @brief Add a font face to the canvas using the specified family and style.
     * @param family The font family name.
     * @param bold Whether the font is bold.
     * @param italic Whether the font is italic.
     * @param face A pointer to the FontFace to add.
     */
    void add_font_face(const char* family, bool bold, bool italic, FontFace* face);

    /**
     * @brief Load a font face from a file and add it to the canvas.
     * @param family The font family name.
     * @param bold Whether the font is bold.
     * @param italic Whether the font is italic.
     * @param filename Path to the font file.
     * @param ttcindex Index within a TrueType Collection.
     * @return `true` on success, or `false` if the font could not be loaded.
     */
    bool add_font_file(const char* family, bool bold, bool italic, const char* filename, int ttcindex);

    /**
     * @brief Selects and sets the current font face on the canvas.
     * @param family The font family name to select.
     * @param bold Whether to match a bold variant.
     * @param italic Whether to match an italic variant.
     * @return `true` if a matching font was found and set, `false` otherwise.
     */
    bool select_font_face(const char* family, bool bold, bool italic);

    /**
     * @brief Sets the font face and size for text rendering on the canvas.
     *
     * Default font face is `nullptr`, default size is 12.
     *
     * @param face A pointer to a FontFace.
     * @param size The size of the font, in pixels.
     */
    void set_font(FontFace* face, float size);

    /**
     * @brief Sets the font face for text rendering on the canvas.
     *
     * Default font face is `nullptr`.
     *
     * @param face A pointer to a FontFace.
     */
    void set_font_face(FontFace* face);

    /**
     * @brief Retrieves the current font face.
     *
     * Default font face is `nullptr`.
     *
     * @return A pointer to the current FontFace.
     */
    FontFace* get_font_face() const;

    /**
     * @brief Sets the font size for text rendering.
     *
     * Default font size is 12.
     *
     * @param size The size of the font, in pixels.
     */
    void set_font_size(float size);

    /**
     * @brief Retrieves the current font size.
     *
     * Default font size is 12.
     *
     * @return The current font size, in pixels.
     */
    float get_font_size() const;

    /**
     * @brief Sets the fill rule.
     *
     * Default is `FillRule::NonZero`.
     *
     * @param rule The fill rule.
     */
    void set_fill_rule(FillRule rule);

    /**
     * @brief Retrieves the current fill rule.
     *
     * Default is `FillRule::NonZero`.
     *
     * @return The current fill rule.
     */
    FillRule get_fill_rule() const;

    /**
     * @brief Sets the compositing operator.
     *
     * Default is `Operator::SrcOver`.
     *
     * @param op The compositing operator.
     */
    void set_operator(Operator op);

    /**
     * @brief Retrieves the current compositing operator.
     *
     * Default is `Operator::SrcOver`.
     *
     * @return The current compositing operator.
     */
    Operator get_operator() const;

    /**
     * @brief Sets the global opacity.
     *
     * Default is 1.
     *
     * @param opacity The opacity value (0 to 1).
     */
    void set_opacity(float opacity);

    /**
     * @brief Retrieves the current global opacity.
     *
     * Default is 1.
     *
     * @return The current opacity value.
     */
    float get_opacity() const;

    /**
     * @brief Sets the line width.
     *
     * Default is 1.
     *
     * @param width The width of the stroke.
     */
    void set_line_width(float width);

    /**
     * @brief Retrieves the current line width.
     *
     * Default is 1.
     *
     * @return The current line width.
     */
    float get_line_width() const;

    /**
     * @brief Sets the line cap style.
     *
     * Default is `LineCap::Butt`.
     *
     * @param cap The line cap style.
     */
    void set_line_cap(LineCap cap);

    /**
     * @brief Retrieves the current line cap style.
     *
     * Default is `LineCap::Butt`.
     *
     * @return The current line cap style.
     */
    LineCap get_line_cap() const;

    /**
     * @brief Sets the line join style.
     *
     * Default is `LineJoin::Miter`.
     *
     * @param join The line join style.
     */
    void set_line_join(LineJoin join);

    /**
     * @brief Retrieves the current line join style.
     *
     * Default is `LineJoin::Miter`.
     *
     * @return The current line join style.
     */
    LineJoin get_line_join() const;

    /**
     * @brief Sets the miter limit.
     *
     * Default is 10.
     *
     * @param limit The miter limit value.
     */
    void set_miter_limit(float limit);

    /**
     * @brief Retrieves the current miter limit.
     *
     * Default is 10.
     *
     * @return The current miter limit value.
     */
    float get_miter_limit() const;

    /**
     * @brief Sets the dash pattern.
     *
     * Default dash offset is 0, default dash array is `nullptr`.
     *
     * @param offset The dash offset.
     * @param dashes Array of dash lengths.
     * @param ndashes Number of dash lengths.
     */
    void set_dash(float offset, const float* dashes, int ndashes);

    /**
     * @brief Sets the dash offset.
     *
     * Default is 0.
     *
     * @param offset The dash offset.
     */
    void set_dash_offset(float offset);

    /**
     * @brief Retrieves the current dash offset.
     *
     * Default is 0.
     *
     * @return The current dash offset.
     */
    float get_dash_offset() const;

    /**
     * @brief Sets the dash pattern.
     *
     * Default dash array is `nullptr`.
     *
     * @param dashes Array of dash lengths.
     * @param ndashes Number of dash lengths.
     */
    void set_dash_array(const float* dashes, int ndashes);

    /**
     * @brief Retrieves the current dash pattern.
     *
     * Default dash array is `nullptr`.
     *
     * @param dashes Pointer to store the dash array.
     * @return The number of dash lengths.
     */
    int get_dash_array(const float** dashes) const;

    /**
     * @brief Translates the current transformation matrix.
     * @param tx The translation offset in the x-direction.
     * @param ty The translation offset in the y-direction.
     */
    void apply_translate(float tx, float ty);

    /**
     * @brief Scales the current transformation matrix.
     * @param sx The scaling factor in the x-direction.
     * @param sy The scaling factor in the y-direction.
     */
    void apply_scale(float sx, float sy);

    /**
     * @brief Shears the current transformation matrix.
     * @param shx The shearing factor in the x-direction.
     * @param shy The shearing factor in the y-direction.
     */
    void apply_shear(float shx, float shy);

    /**
     * @brief Rotates the current transformation matrix.
     * @param angle The rotation angle in radians.
     */
    void apply_rotate(float angle);

    /**
     * @brief Multiplies the current transformation matrix with the specified matrix.
     * @param matrix The Matrix to multiply with.
     */
    void apply_transform(const Matrix& matrix);

    /**
     * @brief Resets the current transformation matrix to the identity matrix.
     */
    void reset_matrix();

    /**
     * @brief Resets the current transformation matrix to the specified matrix.
     * @param matrix The Matrix to set.
     */
    void set_matrix(const Matrix& matrix);

    /**
     * @brief Stores the current transformation matrix.
     * @param matrix A reference to the Matrix to store the result.
     */
    void get_matrix(Matrix& matrix) const;

    /**
     * @brief Transforms the point `(x, y)` using the current transformation matrix.
     * @param x The x-coordinate of the point to transform.
     * @param y The y-coordinate of the point to transform.
     * @param xx A reference to store the transformed x-coordinate.
     * @param yy A reference to store the transformed y-coordinate.
     */
    void map(float x, float y, float& xx, float& yy) const;

    /**
     * @brief Transforms the `src` point using the current transformation matrix.
     * @note `src` and `dst` can be identical.
     * @param src The Point to transform.
     * @param dst The Point to store the result.
     */
    void map_point(const Point& src, Point& dst) const;

    /**
     * @brief Transforms the `src` rectangle using the current transformation matrix.
     * @note `src` and `dst` can be identical.
     * @param src The Rect to transform.
     * @param dst The Rect to store the result.
     */
    void map_rect(const Rect& src, Rect& dst) const;

    /**
     * @brief Moves the current point to a new position. Equivalent to SVG `M`.
     * @param x The x-coordinate of the new position.
     * @param y The y-coordinate of the new position.
     */
    void move_to(float x, float y);

    /**
     * @brief Adds a straight line segment to the current path. Equivalent to SVG `L`.
     * @param x The x-coordinate of the end point.
     * @param y The y-coordinate of the end point.
     */
    void line_to(float x, float y);

    /**
     * @brief Adds a quadratic Bezier curve to the current path. Equivalent to SVG `Q`.
     * @param x1 The x-coordinate of the control point.
     * @param y1 The y-coordinate of the control point.
     * @param x2 The x-coordinate of the end point.
     * @param y2 The y-coordinate of the end point.
     */
    void quad_to(float x1, float y1, float x2, float y2);

    /**
     * @brief Adds a cubic Bezier curve to the current path. Equivalent to SVG `C`.
     * @param x1 The x-coordinate of the first control point.
     * @param y1 The y-coordinate of the first control point.
     * @param x2 The x-coordinate of the second control point.
     * @param y2 The y-coordinate of the second control point.
     * @param x3 The x-coordinate of the end point.
     * @param y3 The y-coordinate of the end point.
     */
    void cubic_to(float x1, float y1, float x2, float y2, float x3, float y3);

    /**
     * @brief Adds an elliptical arc to the current path. Equivalent to SVG `A`.
     * @param rx The x-radius of the ellipse.
     * @param ry The y-radius of the ellipse.
     * @param angle The rotation angle of the ellipse in degrees.
     * @param large_arc_flag If true, add the large arc.
     * @param sweep_flag If true, add the arc in the positive-angle direction.
     * @param x The x-coordinate of the end point.
     * @param y The y-coordinate of the end point.
     */
    void arc_to(float rx, float ry, float angle, bool large_arc_flag, bool sweep_flag, float x, float y);

    /**
     * @brief Adds a rectangle to the current path.
     * @param x The x-coordinate of the rectangle's origin.
     * @param y The y-coordinate of the rectangle's origin.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     */
    void rect(float x, float y, float w, float h);

    /**
     * @brief Adds a rounded rectangle to the current path.
     * @param x The x-coordinate of the rectangle's origin.
     * @param y The y-coordinate of the rectangle's origin.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     * @param rx The x-radius of the corners.
     * @param ry The y-radius of the corners.
     */
    void round_rect(float x, float y, float w, float h, float rx, float ry);

    /**
     * @brief Adds an ellipse to the current path.
     * @param cx The x-coordinate of the ellipse's center.
     * @param cy The y-coordinate of the ellipse's center.
     * @param rx The x-radius of the ellipse.
     * @param ry The y-radius of the ellipse.
     */
    void ellipse(float cx, float cy, float rx, float ry);

    /**
     * @brief Adds a circle to the current path.
     * @param cx The x-coordinate of the circle's center.
     * @param cy The y-coordinate of the circle's center.
     * @param r The radius of the circle.
     */
    void circle(float cx, float cy, float r);

    /**
     * @brief Adds an arc to the current path.
     * @param cx The x-coordinate of the arc's center.
     * @param cy The y-coordinate of the arc's center.
     * @param r The radius of the arc.
     * @param a0 The starting angle in radians.
     * @param a1 The ending angle in radians.
     * @param ccw If true, counter-clockwise; otherwise, clockwise.
     */
    void arc(float cx, float cy, float r, float a0, float a1, bool ccw);

    /**
     * @brief Adds a path to the current path.
     * @param path A pointer to the Path to be added.
     */
    void add_path(const Path* path);

    /**
     * @brief Starts a new path on the canvas, clearing any existing path data.
     */
    void new_path();

    /**
     * @brief Closes the current path by drawing a line to the starting point.
     */
    void close_path();

    /**
     * @brief Retrieves the current point of the canvas.
     * @param x The x-coordinate of the current point.
     * @param y The y-coordinate of the current point.
     */
    void get_current_point(float& x, float& y) const;

    /**
     * @brief Gets the current path from the canvas.
     * @return The current path.
     */
    Path* get_path() const;

    /**
     * @brief Tests whether a point lies within the current fill region.
     * @note Clipping and surface dimensions are not considered.
     * @param x The X coordinate of the point, in user space.
     * @param y The Y coordinate of the point, in user space.
     * @return `true` if the point is within the fill region, `false` otherwise.
     */
    bool fill_contains(float x, float y);

    /**
     * @brief Tests whether a point lies within the current stroke region.
     * @note Clipping and surface dimensions are not considered.
     * @param x The X coordinate of the point, in user space.
     * @param y The Y coordinate of the point, in user space.
     * @return `true` if the point is within the stroke region, `false` otherwise.
     */
    bool stroke_contains(float x, float y);

    /**
     * @brief Tests whether a point lies within the current clipping region.
     * @param x The X coordinate of the point, in user space.
     * @param y The Y coordinate of the point, in user space.
     * @return `true` if the point is within the clipping region, `false` otherwise.
     */
    bool clip_contains(float x, float y);

    /**
     * @brief Computes the bounding box of the area that would be affected by a fill operation.
     * @note Clipping and surface dimensions are not considered.
     * @param extents A Rect to receive the bounding box.
     */
    void fill_extents(Rect& extents);

    /**
     * @brief Computes the bounding box of the area that would be affected by a stroke operation.
     * @note Clipping and surface dimensions are not considered.
     * @param extents A Rect to receive the bounding box.
     */
    void stroke_extents(Rect& extents);

    /**
     * @brief Gets the bounding box of the current clipping region.
     * @param extents A Rect to receive the bounding box.
     */
    void clip_extents(Rect& extents);

    /**
     * @brief Fills the current path according to the current fill rule.
     *
     * The current path will be cleared after this operation.
     */
    void fill();

    /**
     * @brief Strokes the current path according to the current stroke settings.
     *
     * The current path will be cleared after this operation.
     */
    void stroke();

    /**
     * @brief Intersects the current clipping region with the current path.
     *
     * The current path will be cleared after this operation.
     */
    void clip();

    /**
     * @brief Paints the current clipping region using the current paint.
     *
     * @note The current path will not be affected by this operation.
     */
    void paint();

    /**
     * @brief Fills the current path, preserving the path.
     */
    void fill_preserve();

    /**
     * @brief Strokes the current path, preserving the path.
     */
    void stroke_preserve();

    /**
     * @brief Clips using the current path, preserving the path.
     */
    void clip_preserve();

    /**
     * @brief Fills a rectangle.
     * @note The current path will be cleared.
     * @param x The x-coordinate of the rectangle's origin.
     * @param y The y-coordinate of the rectangle's origin.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     */
    void fill_rect(float x, float y, float w, float h);

    /**
     * @brief Fills a path.
     * @note The current path will be cleared.
     * @param path The Path.
     */
    void fill_path(const Path* path);

    /**
     * @brief Strokes a rectangle.
     * @note The current path will be cleared.
     * @param x The x-coordinate of the rectangle's origin.
     * @param y The y-coordinate of the rectangle's origin.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     */
    void stroke_rect(float x, float y, float w, float h);

    /**
     * @brief Strokes a path.
     * @note The current path will be cleared.
     * @param path The Path.
     */
    void stroke_path(const Path* path);

    /**
     * @brief Intersects the current clipping region with a rectangle.
     * @note The current path will be cleared.
     * @param x The x-coordinate of the rectangle's origin.
     * @param y The y-coordinate of the rectangle's origin.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     */
    void clip_rect(float x, float y, float w, float h);

    /**
     * @brief Intersects the current clipping region with a path.
     * @note The current path will be cleared.
     * @param path The Path.
     */
    void clip_path(const Path* path);

    /**
     * @brief Adds a glyph to the current path at the specified origin.
     * @param codepoint The glyph codepoint.
     * @param x The x-coordinate of the origin.
     * @param y The y-coordinate of the origin.
     * @return The advance width of the glyph.
     */
    float add_glyph(Codepoint codepoint, float x, float y);

    /**
     * @brief Adds text to the current path at the specified origin.
     * @param text The text data.
     * @param length The length of the text data, or -1 if null-terminated.
     * @param encoding The encoding of the text data.
     * @param x The x-coordinate of the origin.
     * @param y The y-coordinate of the origin.
     * @return The total advance width of the text.
     */
    float add_text(const void* text, int length, TextEncoding encoding, float x, float y);

    /**
     * @brief Fills text at the specified origin.
     * @note The current path will be cleared.
     * @param text The text data.
     * @param length The length of the text data, or -1 if null-terminated.
     * @param encoding The encoding of the text data.
     * @param x The x-coordinate of the origin.
     * @param y The y-coordinate of the origin.
     * @return The total advance width of the text.
     */
    float fill_text(const void* text, int length, TextEncoding encoding, float x, float y);

    /**
     * @brief Strokes text at the specified origin.
     * @note The current path will be cleared.
     * @param text The text data.
     * @param length The length of the text data, or -1 if null-terminated.
     * @param encoding The encoding of the text data.
     * @param x The x-coordinate of the origin.
     * @param y The y-coordinate of the origin.
     * @return The total advance width of the text.
     */
    float stroke_text(const void* text, int length, TextEncoding encoding, float x, float y);

    /**
     * @brief Intersects the current clipping region with text at the specified origin.
     * @note The current path will be cleared.
     * @param text The text data.
     * @param length The length of the text data, or -1 if null-terminated.
     * @param encoding The encoding of the text data.
     * @param x The x-coordinate of the origin.
     * @param y The y-coordinate of the origin.
     * @return The total advance width of the text.
     */
    float clip_text(const void* text, int length, TextEncoding encoding, float x, float y);

    /**
     * @brief Retrieves font metrics for the current font.
     * @param ascent The ascent of the font.
     * @param descent The descent of the font.
     * @param line_gap The line gap of the font.
     * @param extents The bounding box of the font.
     */
    void font_metrics(float* ascent, float* descent, float* line_gap, Rect* extents) const;

    /**
     * @brief Retrieves metrics for a specific glyph.
     * @param codepoint The glyph codepoint.
     * @param advance_width The advance width of the glyph.
     * @param left_side_bearing The left side bearing of the glyph.
     * @param extents The bounding box of the glyph.
     */
    void glyph_metrics(Codepoint codepoint, float* advance_width, float* left_side_bearing, Rect* extents);

    /**
     * @brief Retrieves the extents of a text.
     * @param text The text data.
     * @param length The length of the text data, or -1 if null-terminated.
     * @param encoding The encoding of the text data.
     * @param extents The bounding box of the text.
     * @return The total advance width of the text.
     */
    float text_extents(const void* text, int length, TextEncoding encoding, Rect* extents);
};

} // namespace plutovg

#endif // PLUTOVG_HPP
