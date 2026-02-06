#ifndef PLUTOVG_PRIVATE_HPP
#define PLUTOVG_PRIVATE_HPP

#include "plutovg.hpp"

#if defined(_WIN32)
#include <windows.h>
using plutovg_ref_count_t = LONG;
#define plutovg_init_reference(ob)       ((ob)->ref_count = 1)
#define plutovg_increment_reference(ob)  (void)(ob && InterlockedIncrement(&(ob)->ref_count))
#define plutovg_destroy_reference(ob)    (ob && InterlockedDecrement(&(ob)->ref_count) == 0)
#define plutovg_get_reference_count(ob)  ((ob) ? InterlockedCompareExchange(reinterpret_cast<LONG*>(&(ob)->ref_count), 0, 0) : 0)
#elif __cpp_lib_atomic_ref >= 201806L || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__))
#include <atomic>
using plutovg_ref_count_t = std::atomic<int>;
#define plutovg_init_reference(ob)       ((ob)->ref_count.store(1))
#define plutovg_increment_reference(ob)  (void)(ob && (ob)->ref_count.fetch_add(1))
#define plutovg_destroy_reference(ob)    (ob && (ob)->ref_count.fetch_sub(1) == 1)
#define plutovg_get_reference_count(ob)  ((ob) ? (ob)->ref_count.load() : 0)
#else
using plutovg_ref_count_t = int;
#define plutovg_init_reference(ob)       ((ob)->ref_count = 1)
#define plutovg_increment_reference(ob)  (void)(ob && ++(ob)->ref_count)
#define plutovg_destroy_reference(ob)    (ob && --(ob)->ref_count == 0)
#define plutovg_get_reference_count(ob)  ((ob) ? (ob)->ref_count : 0)
#endif

namespace plutovg {

struct SurfaceImpl {
    plutovg_ref_count_t ref_count;
    int width;
    int height;
    int stride;
    unsigned char* data;
};

struct PathImpl {
    plutovg_ref_count_t ref_count;
    int num_points;
    int num_contours;
    int num_curves;
    Point start_point;
    Array<PathElement> elements;
};

enum class PaintType {
    Color,
    Gradient,
    Texture
};

struct PaintImpl {
    plutovg_ref_count_t ref_count;
    PaintType type;
};

struct SolidPaint {
    PaintImpl base;
    Color color;
};

enum class GradientType {
    Linear,
    Radial
};

struct GradientPaint {
    PaintImpl base;
    GradientType type;
    SpreadMethod spread;
    Matrix matrix;
    GradientStop* stops;
    int nstops;
    float values[6];
};

struct TexturePaint {
    PaintImpl base;
    TextureType type;
    float opacity;
    Matrix matrix;
    Surface* surface;
};

struct SpanData {
    int x;
    int len;
    int y;
    unsigned char coverage;
};

struct SpanBuffer {
    Array<SpanData> spans;
    int x;
    int y;
    int w;
    int h;
};

struct StrokeDash {
    float offset;
    Array<float> array;
};

struct StrokeStyle {
    float width;
    LineCap cap;
    LineJoin join;
    float miter_limit;
};

struct StrokeData {
    StrokeStyle style;
    StrokeDash dash;
};

struct State {
    PaintImpl* paint;
    FontFace* font_face;
    Color color;
    Matrix matrix;
    StrokeData stroke;
    SpanBuffer clip_spans;
    FillRule winding;
    Operator op;
    float font_size;
    float opacity;
    bool clipping;
    State* next;
};

struct CanvasImpl {
    plutovg_ref_count_t ref_count;
    Surface* surface;
    Path* path;
    State* state;
    State* freed_state;
    FontFaceCache* face_cache;
    Rect clip_rect;
    SpanBuffer clip_spans;
    SpanBuffer fill_spans;
};

void span_buffer_init(SpanBuffer& buf);
void span_buffer_init_rect(SpanBuffer& buf, int x, int y, int width, int height);
void span_buffer_reset(SpanBuffer& buf);
void span_buffer_destroy(SpanBuffer& buf);
void span_buffer_copy(SpanBuffer& buf, const SpanBuffer& source);
bool span_buffer_contains(const SpanBuffer& buf, float x, float y);
void span_buffer_extents(SpanBuffer& buf, Rect& extents);
void span_buffer_intersect(SpanBuffer& buf, const SpanBuffer& a, const SpanBuffer& b);

void rasterize(SpanBuffer& buf, const Path* path, const Matrix* matrix,
    const Rect* clip_rect, const StrokeData* stroke_data, FillRule winding);
void blend(Canvas* canvas, const SpanBuffer& buf);
void memfill32(unsigned int* dest, int length, unsigned int value);

} // namespace plutovg

#endif // PLUTOVG_PRIVATE_HPP
