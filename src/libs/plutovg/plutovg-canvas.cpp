#include "plutovg-private.hpp"
#include "plutovg-utils.hpp"

namespace plutovg {

int get_version()
{
    return version;
}

const char* get_version_string()
{
    return version_string;
}

static constexpr StrokeStyle default_stroke_style = { 1.f, LineCap::Butt, LineJoin::Miter, 10.f };

static inline CanvasImpl* impl(Canvas* c) { return reinterpret_cast<CanvasImpl*>(c); }
static inline const CanvasImpl* impl(const Canvas* c) { return reinterpret_cast<const CanvasImpl*>(c); }
static inline SurfaceImpl* impl(Surface* s) { return reinterpret_cast<SurfaceImpl*>(s); }
static inline const SurfaceImpl* impl(const Surface* s) { return reinterpret_cast<const SurfaceImpl*>(s); }
static inline PaintImpl* impl(Paint* p) { return reinterpret_cast<PaintImpl*>(p); }

static State* state_create()
{
    auto* state = static_cast<State*>(std::malloc(sizeof(State)));
    state->paint = nullptr;
    state->font_face = nullptr;
    state->color = Color::black();
    state->matrix = Matrix::identity();
    state->stroke.style = default_stroke_style;
    state->stroke.dash.offset = 0.f;
    state->stroke.dash.array.init();
    span_buffer_init(state->clip_spans);
    state->winding = FillRule::NonZero;
    state->op = Operator::SrcOver;
    state->font_size = 12.f;
    state->opacity = 1.f;
    state->clipping = false;
    state->next = nullptr;
    return state;
}

static void state_reset(State* state)
{
    if(state->paint)
        reinterpret_cast<Paint*>(state->paint)->destroy();
    if(state->font_face)
        state->font_face->destroy();
    state->paint = nullptr;
    state->font_face = nullptr;
    state->color = Color::black();
    state->matrix = Matrix::identity();
    state->stroke.style = default_stroke_style;
    state->stroke.dash.offset = 0.f;
    state->stroke.dash.array.clear();
    span_buffer_reset(state->clip_spans);
    state->winding = FillRule::NonZero;
    state->op = Operator::SrcOver;
    state->font_size = 12.f;
    state->opacity = 1.f;
    state->clipping = false;
}

static void state_copy(State* state, const State* source)
{
    state->paint = source->paint
        ? impl(reinterpret_cast<Paint*>(source->paint)->reference())
        : nullptr;
    state->font_face = source->font_face
        ? source->font_face->reference()
        : nullptr;
    state->color = source->color;
    state->matrix = source->matrix;
    state->stroke.style = source->stroke.style;
    state->stroke.dash.offset = source->stroke.dash.offset;
    state->stroke.dash.array.clear();
    state->stroke.dash.array.append(source->stroke.dash.array);
    span_buffer_copy(state->clip_spans, source->clip_spans);
    state->winding = source->winding;
    state->op = source->op;
    state->font_size = source->font_size;
    state->opacity = source->opacity;
    state->clipping = source->clipping;
}

static void state_destroy(State* state)
{
    if(state->paint)
        reinterpret_cast<Paint*>(state->paint)->destroy();
    if(state->font_face)
        state->font_face->destroy();
    state->stroke.dash.array.destroy();
    span_buffer_destroy(state->clip_spans);
    std::free(state);
}

Canvas* Canvas::create(Surface* surface)
{
    auto* ci = static_cast<CanvasImpl*>(std::malloc(sizeof(CanvasImpl)));
    plutovg_init_reference(ci);
    ci->surface = surface->reference();
    ci->path = Path::create();
    ci->state = state_create();
    ci->freed_state = nullptr;
    ci->face_cache = nullptr;
    ci->clip_rect = Rect(0, 0, static_cast<float>(impl(surface)->width),
                                static_cast<float>(impl(surface)->height));
    span_buffer_init(ci->clip_spans);
    span_buffer_init(ci->fill_spans);
    return reinterpret_cast<Canvas*>(ci);
}

Canvas* Canvas::reference()
{
    plutovg_increment_reference(impl(this));
    return this;
}

void Canvas::destroy()
{
    auto* ci = impl(this);
    if(plutovg_destroy_reference(ci)) {
        while(ci->state) {
            auto* state = ci->state;
            ci->state = state->next;
            state_destroy(state);
        }
        while(ci->freed_state) {
            auto* state = ci->freed_state;
            ci->freed_state = state->next;
            state_destroy(state);
        }
        if(ci->face_cache)
            ci->face_cache->destroy();
        span_buffer_destroy(ci->fill_spans);
        span_buffer_destroy(ci->clip_spans);
        ci->surface->destroy();
        ci->path->destroy();
        std::free(ci);
    }
}

int Canvas::get_reference_count() const
{
    return plutovg_get_reference_count(impl(this));
}

Surface* Canvas::get_surface() const
{
    return impl(this)->surface;
}

void Canvas::save()
{
    auto* ci = impl(this);
    auto* new_state = ci->freed_state;
    if(new_state == nullptr)
        new_state = state_create();
    else
        ci->freed_state = new_state->next;
    state_copy(new_state, ci->state);
    new_state->next = ci->state;
    ci->state = new_state;
}

void Canvas::restore()
{
    auto* ci = impl(this);
    if(ci->state->next == nullptr)
        return;
    auto* old_state = ci->state;
    ci->state = old_state->next;
    state_reset(old_state);
    old_state->next = ci->freed_state;
    ci->freed_state = old_state;
}

void Canvas::set_rgb(float r, float g, float b)
{
    set_rgba(r, g, b, 1.f);
}

void Canvas::set_rgba(float r, float g, float b, float a)
{
    impl(this)->state->color.init_rgba(r, g, b, a);
    set_paint(nullptr);
}

void Canvas::set_color(const Color& color)
{
    set_rgba(color.r, color.g, color.b, color.a);
}

void Canvas::set_linear_gradient(float x1, float y1, float x2, float y2,
    SpreadMethod spread, const GradientStop* stops, int nstops, const Matrix* matrix)
{
    auto* paint = Paint::create_linear_gradient(x1, y1, x2, y2, spread, stops, nstops, matrix);
    set_paint(paint);
    paint->destroy();
}

void Canvas::set_radial_gradient(float cx, float cy, float cr, float fx, float fy, float fr,
    SpreadMethod spread, const GradientStop* stops, int nstops, const Matrix* matrix)
{
    auto* paint = Paint::create_radial_gradient(cx, cy, cr, fx, fy, fr, spread, stops, nstops, matrix);
    set_paint(paint);
    paint->destroy();
}

void Canvas::set_texture(Surface* surface, TextureType type, float opacity, const Matrix* matrix)
{
    auto* paint = Paint::create_texture(surface, type, opacity, matrix);
    set_paint(paint);
    paint->destroy();
}

void Canvas::set_paint(Paint* paint)
{
    auto* ci = impl(this);
    auto* new_paint = paint ? impl(paint->reference()) : nullptr;
    if(ci->state->paint)
        reinterpret_cast<Paint*>(ci->state->paint)->destroy();
    ci->state->paint = new_paint;
}

Paint* Canvas::get_paint(Color* color) const
{
    if(color)
        *color = impl(this)->state->color;
    return reinterpret_cast<Paint*>(impl(this)->state->paint);
}

void Canvas::set_font_face_cache(FontFaceCache* cache)
{
    auto* ci = impl(this);
    auto* new_cache = cache ? cache->reference() : nullptr;
    if(ci->face_cache)
        ci->face_cache->destroy();
    ci->face_cache = new_cache;
}

FontFaceCache* Canvas::get_font_face_cache() const
{
    return impl(this)->face_cache;
}

void Canvas::add_font_face(const char* family, bool bold, bool italic, FontFace* face)
{
    auto* ci = impl(this);
    if(ci->face_cache == nullptr)
        ci->face_cache = FontFaceCache::create();
    ci->face_cache->add(family, bold, italic, face);
}

bool Canvas::add_font_file(const char* family, bool bold, bool italic, const char* filename, int ttcindex)
{
    auto* ci = impl(this);
    if(ci->face_cache == nullptr)
        ci->face_cache = FontFaceCache::create();
    return ci->face_cache->add_file(family, bold, italic, filename, ttcindex);
}

bool Canvas::select_font_face(const char* family, bool bold, bool italic)
{
    auto* ci = impl(this);
    if(ci->face_cache == nullptr)
        return false;
    auto* face = ci->face_cache->get(family, bold, italic);
    if(face == nullptr)
        return false;
    set_font_face(face);
    return true;
}

void Canvas::set_font(FontFace* face, float size)
{
    set_font_face(face);
    set_font_size(size);
}

void Canvas::set_font_face(FontFace* face)
{
    auto* new_face = face ? face->reference() : nullptr;
    if(impl(this)->state->font_face)
        impl(this)->state->font_face->destroy();
    impl(this)->state->font_face = new_face;
}

FontFace* Canvas::get_font_face() const
{
    return impl(this)->state->font_face;
}

void Canvas::set_font_size(float size)
{
    impl(this)->state->font_size = size;
}

float Canvas::get_font_size() const
{
    return impl(this)->state->font_size;
}

void Canvas::set_fill_rule(FillRule rule)
{
    impl(this)->state->winding = rule;
}

FillRule Canvas::get_fill_rule() const
{
    return impl(this)->state->winding;
}

void Canvas::set_operator(Operator op)
{
    impl(this)->state->op = op;
}

Operator Canvas::get_operator() const
{
    return impl(this)->state->op;
}

void Canvas::set_opacity(float opacity)
{
    impl(this)->state->opacity = clamp(opacity, 0.f, 1.f);
}

float Canvas::get_opacity() const
{
    return impl(this)->state->opacity;
}

void Canvas::set_line_width(float width)
{
    impl(this)->state->stroke.style.width = width;
}

float Canvas::get_line_width() const
{
    return impl(this)->state->stroke.style.width;
}

void Canvas::set_line_cap(LineCap cap)
{
    impl(this)->state->stroke.style.cap = cap;
}

LineCap Canvas::get_line_cap() const
{
    return impl(this)->state->stroke.style.cap;
}

void Canvas::set_line_join(LineJoin join)
{
    impl(this)->state->stroke.style.join = join;
}

LineJoin Canvas::get_line_join() const
{
    return impl(this)->state->stroke.style.join;
}

void Canvas::set_miter_limit(float limit)
{
    impl(this)->state->stroke.style.miter_limit = limit;
}

float Canvas::get_miter_limit() const
{
    return impl(this)->state->stroke.style.miter_limit;
}

void Canvas::set_dash(float offset, const float* dashes, int ndashes)
{
    set_dash_offset(offset);
    set_dash_array(dashes, ndashes);
}

void Canvas::set_dash_offset(float offset)
{
    impl(this)->state->stroke.dash.offset = offset;
}

float Canvas::get_dash_offset() const
{
    return impl(this)->state->stroke.dash.offset;
}

void Canvas::set_dash_array(const float* dashes, int ndashes)
{
    auto& arr = impl(this)->state->stroke.dash.array;
    arr.clear();
    arr.append(dashes, ndashes);
}

int Canvas::get_dash_array(const float** dashes) const
{
    auto& arr = impl(this)->state->stroke.dash.array;
    if(dashes)
        *dashes = arr.data;
    return arr.size;
}

void Canvas::apply_translate(float tx, float ty)
{
    impl(this)->state->matrix.apply_translate(tx, ty);
}

void Canvas::apply_scale(float sx, float sy)
{
    impl(this)->state->matrix.apply_scale(sx, sy);
}

void Canvas::apply_shear(float shx, float shy)
{
    impl(this)->state->matrix.apply_shear(shx, shy);
}

void Canvas::apply_rotate(float angle)
{
    impl(this)->state->matrix.apply_rotate(angle);
}

void Canvas::apply_transform(const Matrix& matrix)
{
    Matrix::multiply(impl(this)->state->matrix, matrix, impl(this)->state->matrix);
}

void Canvas::reset_matrix()
{
    impl(this)->state->matrix.init_identity();
}

void Canvas::set_matrix(const Matrix& matrix)
{
    impl(this)->state->matrix = matrix;
}

void Canvas::get_matrix(Matrix& matrix) const
{
    matrix = impl(this)->state->matrix;
}

void Canvas::map(float x, float y, float& xx, float& yy) const
{
    impl(this)->state->matrix.map(x, y, xx, yy);
}

void Canvas::map_point(const Point& src, Point& dst) const
{
    impl(this)->state->matrix.map_point(src, dst);
}

void Canvas::map_rect(const Rect& src, Rect& dst) const
{
    impl(this)->state->matrix.map_rect(src, dst);
}

void Canvas::move_to(float x, float y)
{
    impl(this)->path->move_to(x, y);
}

void Canvas::line_to(float x, float y)
{
    impl(this)->path->line_to(x, y);
}

void Canvas::quad_to(float x1, float y1, float x2, float y2)
{
    impl(this)->path->quad_to(x1, y1, x2, y2);
}

void Canvas::cubic_to(float x1, float y1, float x2, float y2, float x3, float y3)
{
    impl(this)->path->cubic_to(x1, y1, x2, y2, x3, y3);
}

void Canvas::arc_to(float rx, float ry, float angle, bool large_arc_flag, bool sweep_flag, float x, float y)
{
    impl(this)->path->arc_to(rx, ry, angle, large_arc_flag, sweep_flag, x, y);
}

void Canvas::rect(float x, float y, float w, float h)
{
    impl(this)->path->add_rect(x, y, w, h);
}

void Canvas::round_rect(float x, float y, float w, float h, float rx, float ry)
{
    impl(this)->path->add_round_rect(x, y, w, h, rx, ry);
}

void Canvas::ellipse(float cx, float cy, float rx, float ry)
{
    impl(this)->path->add_ellipse(cx, cy, rx, ry);
}

void Canvas::circle(float cx, float cy, float r)
{
    impl(this)->path->add_circle(cx, cy, r);
}

void Canvas::arc(float cx, float cy, float r, float a0, float a1, bool ccw)
{
    impl(this)->path->add_arc(cx, cy, r, a0, a1, ccw);
}

void Canvas::add_path(const Path* path)
{
    impl(this)->path->add_path(path, nullptr);
}

void Canvas::new_path()
{
    impl(this)->path->reset();
}

void Canvas::close_path()
{
    impl(this)->path->close();
}

void Canvas::get_current_point(float& x, float& y) const
{
    impl(this)->path->get_current_point(x, y);
}

Path* Canvas::get_path() const
{
    return impl(this)->path;
}

bool Canvas::fill_contains(float x, float y)
{
    auto* ci = impl(this);
    rasterize(ci->fill_spans, ci->path, &ci->state->matrix, nullptr, nullptr, ci->state->winding);
    return span_buffer_contains(ci->fill_spans, x, y);
}

bool Canvas::stroke_contains(float x, float y)
{
    auto* ci = impl(this);
    rasterize(ci->fill_spans, ci->path, &ci->state->matrix, nullptr, &ci->state->stroke, FillRule::NonZero);
    return span_buffer_contains(ci->fill_spans, x, y);
}

bool Canvas::clip_contains(float x, float y)
{
    auto* ci = impl(this);
    if(ci->state->clipping)
        return span_buffer_contains(ci->state->clip_spans, x, y);

    float l = ci->clip_rect.x;
    float t = ci->clip_rect.y;
    float r = ci->clip_rect.x + ci->clip_rect.w;
    float b = ci->clip_rect.y + ci->clip_rect.h;
    return x >= l && x <= r && y >= t && y <= b;
}

void Canvas::fill_extents(Rect& extents)
{
    auto* ci = impl(this);
    rasterize(ci->fill_spans, ci->path, &ci->state->matrix, nullptr, nullptr, ci->state->winding);
    span_buffer_extents(ci->fill_spans, extents);
}

void Canvas::stroke_extents(Rect& extents)
{
    auto* ci = impl(this);
    rasterize(ci->fill_spans, ci->path, &ci->state->matrix, nullptr, &ci->state->stroke, FillRule::NonZero);
    span_buffer_extents(ci->fill_spans, extents);
}

void Canvas::clip_extents(Rect& extents)
{
    auto* ci = impl(this);
    if(ci->state->clipping) {
        span_buffer_extents(ci->state->clip_spans, extents);
    } else {
        extents = ci->clip_rect;
    }
}

void Canvas::fill()
{
    fill_preserve();
    new_path();
}

void Canvas::stroke()
{
    stroke_preserve();
    new_path();
}

void Canvas::clip()
{
    clip_preserve();
    new_path();
}

void Canvas::paint()
{
    auto* ci = impl(this);
    if(ci->state->clipping) {
        blend(this, ci->state->clip_spans);
    } else {
        span_buffer_init_rect(ci->clip_spans, 0, 0, impl(ci->surface)->width, impl(ci->surface)->height);
        blend(this, ci->clip_spans);
    }
}

void Canvas::fill_preserve()
{
    auto* ci = impl(this);
    rasterize(ci->fill_spans, ci->path, &ci->state->matrix, &ci->clip_rect, nullptr, ci->state->winding);
    if(ci->state->clipping) {
        span_buffer_intersect(ci->clip_spans, ci->fill_spans, ci->state->clip_spans);
        blend(this, ci->clip_spans);
    } else {
        blend(this, ci->fill_spans);
    }
}

void Canvas::stroke_preserve()
{
    auto* ci = impl(this);
    rasterize(ci->fill_spans, ci->path, &ci->state->matrix, &ci->clip_rect, &ci->state->stroke, FillRule::NonZero);
    if(ci->state->clipping) {
        span_buffer_intersect(ci->clip_spans, ci->fill_spans, ci->state->clip_spans);
        blend(this, ci->clip_spans);
    } else {
        blend(this, ci->fill_spans);
    }
}

void Canvas::clip_preserve()
{
    auto* ci = impl(this);
    if(ci->state->clipping) {
        rasterize(ci->fill_spans, ci->path, &ci->state->matrix, &ci->clip_rect, nullptr, ci->state->winding);
        span_buffer_intersect(ci->clip_spans, ci->fill_spans, ci->state->clip_spans);
        span_buffer_copy(ci->state->clip_spans, ci->clip_spans);
    } else {
        rasterize(ci->state->clip_spans, ci->path, &ci->state->matrix, &ci->clip_rect, nullptr, ci->state->winding);
        ci->state->clipping = true;
    }
}

void Canvas::fill_rect(float x, float y, float w, float h)
{
    new_path();
    rect(x, y, w, h);
    fill();
}

void Canvas::fill_path(const Path* path)
{
    new_path();
    add_path(path);
    fill();
}

void Canvas::stroke_rect(float x, float y, float w, float h)
{
    new_path();
    rect(x, y, w, h);
    stroke();
}

void Canvas::stroke_path(const Path* path)
{
    new_path();
    add_path(path);
    stroke();
}

void Canvas::clip_rect(float x, float y, float w, float h)
{
    new_path();
    rect(x, y, w, h);
    clip();
}

void Canvas::clip_path(const Path* path)
{
    new_path();
    add_path(path);
    clip();
}

float Canvas::add_glyph(Codepoint codepoint, float x, float y)
{
    auto* state = impl(this)->state;
    if(state->font_face && state->font_size > 0.f)
        return state->font_face->get_glyph_path(state->font_size, x, y, codepoint, impl(this)->path);
    return 0.f;
}

float Canvas::add_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    auto* state = impl(this)->state;
    if(state->font_face == nullptr || state->font_size <= 0.f)
        return 0.f;
    TextIterator it;
    text_iterator_init(it, text, length, encoding);
    float advance_width = 0.f;
    while(text_iterator_has_next(it)) {
        Codepoint codepoint = text_iterator_next(it);
        advance_width += state->font_face->get_glyph_path(state->font_size, x + advance_width, y, codepoint, impl(this)->path);
    }
    return advance_width;
}

float Canvas::fill_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    new_path();
    float advance_width = add_text(text, length, encoding, x, y);
    fill();
    return advance_width;
}

float Canvas::stroke_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    new_path();
    float advance_width = add_text(text, length, encoding, x, y);
    stroke();
    return advance_width;
}

float Canvas::clip_text(const void* text, int length, TextEncoding encoding, float x, float y)
{
    new_path();
    float advance_width = add_text(text, length, encoding, x, y);
    clip();
    return advance_width;
}

void Canvas::font_metrics(float* ascent, float* descent, float* line_gap, Rect* extents) const
{
    auto* state = impl(this)->state;
    if(state->font_face && state->font_size > 0.f) {
        state->font_face->get_metrics(state->font_size, ascent, descent, line_gap, extents);
        return;
    }
    if(ascent) *ascent = 0.f;
    if(descent) *descent = 0.f;
    if(line_gap) *line_gap = 0.f;
    if(extents) *extents = Rect{};
}

void Canvas::glyph_metrics(Codepoint codepoint, float* advance_width, float* left_side_bearing, Rect* extents)
{
    auto* state = impl(this)->state;
    if(state->font_face && state->font_size > 0.f) {
        state->font_face->get_glyph_metrics(state->font_size, codepoint, advance_width, left_side_bearing, extents);
        return;
    }
    if(advance_width) *advance_width = 0.f;
    if(left_side_bearing) *left_side_bearing = 0.f;
    if(extents) *extents = Rect{};
}

float Canvas::text_extents(const void* text, int length, TextEncoding encoding, Rect* extents)
{
    auto* state = impl(this)->state;
    if(state->font_face && state->font_size > 0.f)
        return state->font_face->text_extents(state->font_size, text, length, encoding, extents);
    if(extents) *extents = Rect{};
    return 0.f;
}

} // namespace plutovg
