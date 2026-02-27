/*
 * KosmVG FreeType monochrome rasterizer (C++20)
 *
 * Profile-based scan-converter for 1-bit (black/white) rendering.
 * Supports direct bitmap output and span callback.
 *
 * Based on FreeType ftraster.c
 * Copyright 1996-2025
 * David Turner, Robert Wilhelm, Werner Lemberg.
 * FreeType project license, FTL.TXT.
 *
 * Algorithm overview:
 *   1. Decompose outline into monotone "profiles" (arrays of
 *      scanline x-intersections stored in a render pool).
 *   2. Vertical sweep: walk scanlines bottom-to-top, pair
 *      ascending/descending profiles, draw filled spans.
 *   3. Horizontal sweep (dropout control): walk columns
 *      left-to-right to fix missing single-pixel features.
 */

#include "kosmvg-ft.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace kvg::ft {

// -- error codes --

static constexpr int kErrOk               =  0;
static constexpr int kErrInvalidOutline    = -1;
static constexpr int kErrCannotRender      = -2;
static constexpr int kErrInvalidArgument   = -3;
static constexpr int kErrRasterOverflow    = -4;
static constexpr int kErrNegativeHeight    = -6;

// -- constants --

static constexpr int kPixelBits   = 6;   // fractional bits of input (26.6)
static constexpr int kMaxBezier   = 32;
static constexpr int kPoolLongs   = 16384 / static_cast<int>(sizeof(Long));

// -- profile flags --

static constexpr unsigned short kFlowUp          = 0x08;
static constexpr unsigned short kOvershootTop    = 0x10;
static constexpr unsigned short kOvershootBottom = 0x20;
static constexpr unsigned short kDropout         = 0x40;

// -- profile states --

enum class PState : int {
    Unknown    = 0,
    Ascending  = 1,
    Descending = 2,
    Flat       = 3,
};

// -- internal point type --

struct TPoint {
    Long x;
    Long y;
};

// -- profile: variable-length, allocated from the pool --

struct Profile {
    Profile*       link   = nullptr;
    Profile*       next   = nullptr;
    int            offset = 0;
    int            height = 0;
    int            start  = 0;
    unsigned short flags  = 0;
    Long           X      = 0;
    Long           x[1];               // variable-length array
};

// -- sweep function types --

struct Worker;

using SweepInitFn = void (*)(Worker&, int min, int max);
using SweepSpanFn = void (*)(Worker&, int y, Long x1, Long x2);
using SweepStepFn = void (*)(Worker&);

// -- worker state --

struct Worker {
    // precision control
    int  precision_bits  = 0;
    int  precision       = 0;
    int  precision_half  = 0;
    int  precision_scale = 0;
    int  precision_step  = 0;

    // render pool
    Long*  buff     = nullptr;
    Long*  sizeBuff = nullptr;
    Long*  maxBuff  = nullptr;
    Long*  top      = nullptr;

    int    error = 0;
    Byte   dropOutControl = 0;

    Long   lastX = 0, lastY = 0;
    Long   minY  = 0, maxY  = 0;

    unsigned short num_Profs = 0;
    int            numTurns  = 0;

    Profile*  cProfile = nullptr;
    Profile*  fProfile = nullptr;
    Profile*  gProfile = nullptr;

    PState    state = PState::Unknown;

    // outline reference (shallow copy)
    const Vector* o_points   = nullptr;
    const char*   o_tags     = nullptr;
    const int*    o_contours = nullptr;
    int           o_n_contours = 0;
    int           o_n_points   = 0;

    // bitmap target
    int   bTop    = 0;
    int   bRight  = 0;
    int   bPitch  = 0;
    Byte* bOrigin = nullptr;
    Byte* bLine   = nullptr;

    // span callback (alternative to bitmap)
    SpanFunc spanFunc = nullptr;
    void*    spanUser = nullptr;

    // sweep dispatch
    SweepInitFn Proc_Sweep_Init = nullptr;
    SweepSpanFn Proc_Sweep_Span = nullptr;
    SweepSpanFn Proc_Sweep_Drop = nullptr;
    SweepStepFn Proc_Sweep_Step = nullptr;

    // span accumulation buffer (replaces former file-scope globals)
    static constexpr int kMaxSpans = 256;
    int  span_y     = 0;
    int  span_count = 0;
    Span span_buf[kMaxSpans];

    // clip box for span callback mode
    BBox span_clip    = {};
    bool span_clip_on = false;

    // fill rule
    bool even_odd = false;

    // pool storage (aligned for Profile pointers)
    alignas(alignof(Profile)) Long pool[kPoolLongs];

    // -- precision helpers (inline, replace macros) --

    Long floor_p(Long x) const   { return x & -precision; }
    Long ceiling_p(Long x) const { return (x + precision - 1) & -precision; }
    Long trunc_p(Long x) const   { return x >> precision_bits; }
    Long frac_p(Long x) const    { return x & (precision - 1); }
    Long scaled(Long x) const    { return x * precision_scale - precision_half; }

    bool is_bottom_overshoot(Long x) const {
        return (ceiling_p(x) - x) >= precision_half;
    }
    bool is_top_overshoot(Long x) const {
        return (x - floor_p(x)) >= precision_half;
    }
    Long smart_round(Long p, Long q) const {
        return floor_p((p + q + static_cast<Long>(precision) * 63 / 64) >> 1);
    }
};

// -- fixed-point multiply/divide used in line rasterization --

static inline Long smul_div(Long a, Long b, Long c)
{
    return static_cast<Long>(static_cast<Int64>(a) * b / c);
}

static inline Long fmul_div(Long a, Long b, Long c)
{
    return a * b / c;
}

// -- pool pointer alignment helper --
// Long = int32_t (4 bytes), but Profile contains pointers (8 bytes on 64-bit).
// Ensure pool pointer is aligned to Profile's alignment before casting.

static Long* pool_align(Long* ptr)
{
    constexpr auto align = alignof(Profile);
    auto p = reinterpret_cast<uintptr_t>(ptr);
    p = (p + align - 1) & ~(align - 1);
    return reinterpret_cast<Long*>(p);
}

// =====================================================================
// Profile computation
// =====================================================================

static void set_high_precision(Worker& w, bool high)
{
    if (high) {
        w.precision_bits = 12;
        w.precision_step = 256;
    } else {
        w.precision_bits = 6;
        w.precision_step = 32;
    }
    w.precision       = 1 << w.precision_bits;
    w.precision_half  = w.precision >> 1;
    w.precision_scale = w.precision >> kPixelBits;
}

static bool insert_y_turns(Worker& w, int y, int top_val)
{
    int   n       = w.numTurns;
    Long* y_turns = w.maxBuff;

    if (n == 0 || top_val > y_turns[n])
        y_turns[n] = top_val;

    while (n-- && y < y_turns[n])
        ;

    if (n < 0 || y > y_turns[n]) {
        w.maxBuff--;
        if (w.maxBuff <= w.top) {
            w.error = kErrRasterOverflow;
            return false;
        }
        do {
            int y2 = static_cast<int>(y_turns[n]);
            y_turns[n] = y;
            y = y2;
        } while (n-- >= 0);
        w.numTurns++;
    }
    return true;
}

static bool new_profile(Worker& w, PState aState)
{
    Long e;

    if (!w.cProfile || w.cProfile->height) {
        w.top = pool_align(w.top);
        w.cProfile = reinterpret_cast<Profile*>(w.top);
        w.top      = w.cProfile->x;
        if (w.top >= w.maxBuff) {
            w.error = kErrRasterOverflow;
            return false;
        }
        w.cProfile->height = 0;
    }

    w.cProfile->flags = w.dropOutControl;

    switch (aState) {
    case PState::Ascending:
        w.cProfile->flags |= kFlowUp;
        if (w.is_bottom_overshoot(w.lastY))
            w.cProfile->flags |= kOvershootBottom;
        e = w.ceiling_p(w.lastY);
        break;
    case PState::Descending:
        if (w.is_top_overshoot(w.lastY))
            w.cProfile->flags |= kOvershootTop;
        e = w.floor_p(w.lastY);
        break;
    default:
        w.error = kErrInvalidOutline;
        return false;
    }

    if (e > w.maxY) e = w.maxY;
    if (e < w.minY) e = w.minY;
    w.cProfile->start = static_cast<int>(w.trunc_p(e));

    if (w.lastY == e)
        *w.top++ = w.lastX;

    w.state = aState;
    return true;
}

static bool end_profile(Worker& w)
{
    Profile* p = w.cProfile;
    int h = static_cast<int>(w.top - p->x);

    if (h < 0) {
        w.error = kErrNegativeHeight;
        return false;
    }

    if (h > 0) {
        p->height = h;
        int bottom, top_val;

        if (p->flags & kFlowUp) {
            if (w.is_top_overshoot(w.lastY))
                p->flags |= kOvershootTop;
            bottom    = p->start;
            top_val   = bottom + h;
            p->offset = 0;
            p->X      = p->x[0];
        } else {
            if (w.is_bottom_overshoot(w.lastY))
                p->flags |= kOvershootBottom;
            top_val   = p->start + 1;
            bottom    = top_val - h;
            p->start  = bottom;
            p->offset = h - 1;
            p->X      = p->x[h - 1];
        }

        if (!insert_y_turns(w, bottom, top_val))
            return false;

        if (!w.gProfile)
            w.gProfile = p;

        p->next = w.gProfile;
        p->link = reinterpret_cast<Profile*>(pool_align(w.top));
        w.num_Profs++;
    }
    return true;
}

static void finalize_profile_table(Worker& w)
{
    unsigned short n = w.num_Profs;
    Profile* p = w.fProfile;
    Profile* q;

    while (--n) {
        q = p->link;
        if (q->next == p->next)
            p->next = q;
        p = q;
    }
    p->link = nullptr;
}

// =====================================================================
// Bezier splitting
// =====================================================================

static void split_conic(TPoint* base)
{
    Long a, b;

    base[4].x = base[2].x;
    a = base[0].x + base[1].x;
    b = base[1].x + base[2].x;
    base[3].x = b >> 1;
    base[2].x = (a + b) >> 2;
    base[1].x = a >> 1;

    base[4].y = base[2].y;
    a = base[0].y + base[1].y;
    b = base[1].y + base[2].y;
    base[3].y = b >> 1;
    base[2].y = (a + b) >> 2;
    base[1].y = a >> 1;
}

static void split_cubic(TPoint* base)
{
    Long a, b, c;

    base[6].x = base[3].x;
    a = base[0].x + base[1].x;
    b = base[1].x + base[2].x;
    c = base[2].x + base[3].x;
    base[5].x = c >> 1;
    c += b;
    base[4].x = c >> 2;
    base[1].x = a >> 1;
    a += b;
    base[2].x = a >> 2;
    base[3].x = (a + c) >> 3;

    base[6].y = base[3].y;
    a = base[0].y + base[1].y;
    b = base[1].y + base[2].y;
    c = base[2].y + base[3].y;
    base[5].y = c >> 1;
    c += b;
    base[4].y = c >> 2;
    base[1].y = a >> 1;
    a += b;
    base[2].y = a >> 2;
    base[3].y = (a + c) >> 3;
}

using Splitter = void (*)(TPoint*);

// =====================================================================
// Line and Bezier rasterization (ascending/descending)
// =====================================================================

static bool line_up(Worker& w, Long x1, Long y1, Long x2, Long y2,
                    Long miny, Long maxy)
{
    if (y2 < miny || y1 > maxy)
        return true;

    Long e2 = y2 > maxy ? maxy : w.floor_p(y2);
    Long e  = y1 < miny ? miny : w.ceiling_p(y1);

    if (y1 == e)
        e += w.precision;
    if (e2 < e)
        return true;

    int size = static_cast<int>(w.trunc_p(e2 - e)) + 1;
    Long* top = w.top;

    if (top + size >= w.maxBuff) {
        w.error = kErrRasterOverflow;
        return false;
    }

    Long Dx = x2 - x1;
    Long Dy = y2 - y1;

    if (Dx == 0) {
        for (int i = 0; i < size; i++)
            *top++ = x1;
        w.top = top;
        return true;
    }

    Long Ix = smul_div(e - y1, Dx, Dy);
    x1 += Ix;
    *top++ = x1;

    if (--size) {
        Long Ax = Dx * (e - y1) - Dy * Ix;
        Ix = smul_div(w.precision, Dx, Dy);
        Long Rx = Dx * w.precision - Dy * Ix;
        Long Dxi = 1;

        if (x2 < x1) {
            Ax  = -Ax;
            Rx  = -Rx;
            Dxi = -Dxi;
        }

        do {
            x1 += Ix;
            Ax += Rx;
            if (Ax >= Dy) {
                Ax -= Dy;
                x1 += Dxi;
            }
            *top++ = x1;
        } while (--size);
    }

    w.top = top;
    return true;
}

static bool line_down(Worker& w, Long x1, Long y1, Long x2, Long y2,
                      Long miny, Long maxy)
{
    return line_up(w, x1, -y1, x2, -y2, -maxy, -miny);
}

static bool bezier_up(Worker& w, int degree, TPoint* arc, Splitter splitter,
                      Long miny, Long maxy)
{
    Long y1 = arc[degree].y;
    Long y2 = arc[0].y;

    if (y2 < miny || y1 > maxy)
        return true;

    Long e2 = y2 > maxy ? maxy : w.floor_p(y2);
    Long e  = y1 < miny ? miny : w.ceiling_p(y1);

    if (y1 == e) e += w.precision;
    if (e2 < e)  return true;

    Long* top = w.top;

    if ((top + w.trunc_p(e2 - e) + 1) >= w.maxBuff) {
        w.error = kErrRasterOverflow;
        return false;
    }

    do {
        y2 = arc[0].y;
        Long x2 = arc[0].x;

        if (y2 > e) {
            Long dy = y2 - arc[degree].y;
            Long dx = x2 - arc[degree].x;

            if (dy > w.precision_step ||
                dx > w.precision_step ||
               -dx > w.precision_step) {
                splitter(arc);
                arc += degree;
            } else {
                *top++ = x2 - fmul_div(y2 - e, dx, dy);
                e += w.precision;
                arc -= degree;
            }
        } else {
            if (y2 == e) {
                *top++ = x2;
                e += w.precision;
            }
            arc -= degree;
        }
    } while (e <= e2);

    w.top = top;
    return true;
}

static bool bezier_down(Worker& w, int degree, TPoint* arc, Splitter splitter,
                        Long miny, Long maxy)
{
    arc[0].y = -arc[0].y;
    arc[1].y = -arc[1].y;
    arc[2].y = -arc[2].y;
    if (degree > 2) arc[3].y = -arc[3].y;

    bool ok = bezier_up(w, degree, arc, splitter, -maxy, -miny);

    arc[0].y = -arc[0].y;
    return ok;
}

// =====================================================================
// Outline decomposition (Line_To / Conic_To / Cubic_To / Decompose)
// =====================================================================

static bool mono_line_to(Worker& w, Long x, Long y)
{
    if (y == w.lastY) {
        w.lastX = x;
        w.lastY = y;
        return true;
    }

    PState st = w.lastY < y ? PState::Ascending : PState::Descending;

    if (w.state != st) {
        if (w.state != PState::Unknown && !end_profile(w))
            return false;
        if (!new_profile(w, st))
            return false;
    }

    bool ok;
    if (st == PState::Ascending)
        ok = line_up(w, w.lastX, w.lastY, x, y, w.minY, w.maxY);
    else
        ok = line_down(w, w.lastX, w.lastY, x, y, w.minY, w.maxY);

    if (!ok) return false;

    w.lastX = x;
    w.lastY = y;
    return true;
}

static bool mono_conic_to(Worker& w, Long cx, Long cy, Long x, Long y)
{
    TPoint arcs[2 * kMaxBezier + 1];
    TPoint* arc = arcs;

    arc[2].x = w.lastX; arc[2].y = w.lastY;
    arc[1].x = cx;      arc[1].y = cy;
    arc[0].x = x;       arc[0].y = y;

    do {
        Long y1 = arc[2].y, y2 = arc[1].y, y3 = arc[0].y;
        Long x3 = arc[0].x;
        Long ymin = y1 <= y3 ? y1 : y3;
        Long ymax = y1 <= y3 ? y3 : y1;

        if (y2 < w.floor_p(ymin) || y2 > w.ceiling_p(ymax)) {
            split_conic(arc);
            arc += 2;
        } else if (y1 == y3) {
            arc -= 2;
            w.lastX = x3;
            w.lastY = y3;
        } else {
            PState st = y1 < y3 ? PState::Ascending : PState::Descending;
            if (w.state != st) {
                if (w.state != PState::Unknown && !end_profile(w))
                    return false;
                if (!new_profile(w, st))
                    return false;
            }
            bool ok;
            if (st == PState::Ascending)
                ok = bezier_up(w, 2, arc, split_conic, w.minY, w.maxY);
            else
                ok = bezier_down(w, 2, arc, split_conic, w.minY, w.maxY);
            if (!ok) return false;
            arc -= 2;
            w.lastX = x3;
            w.lastY = y3;
        }
    } while (arc >= arcs);

    return true;
}

static bool mono_cubic_to(Worker& w, Long cx1, Long cy1,
                          Long cx2, Long cy2, Long x, Long y)
{
    TPoint arcs[3 * kMaxBezier + 1];
    TPoint* arc = arcs;

    arc[3].x = w.lastX; arc[3].y = w.lastY;
    arc[2].x = cx1;     arc[2].y = cy1;
    arc[1].x = cx2;     arc[1].y = cy2;
    arc[0].x = x;       arc[0].y = y;

    do {
        Long y1 = arc[3].y, y2 = arc[2].y, y3 = arc[1].y, y4 = arc[0].y;
        Long x4 = arc[0].x;

        Long ymin1, ymax1, ymin2, ymax2;
        if (y1 <= y4) { ymin1 = y1; ymax1 = y4; }
        else          { ymin1 = y4; ymax1 = y1; }
        if (y2 <= y3) { ymin2 = y2; ymax2 = y3; }
        else          { ymin2 = y3; ymax2 = y2; }

        if (ymin2 < w.floor_p(ymin1) || ymax2 > w.ceiling_p(ymax1)) {
            split_cubic(arc);
            arc += 3;
        } else if (y1 == y4) {
            arc -= 3;
            w.lastX = x4;
            w.lastY = y4;
        } else {
            PState st = y1 < y4 ? PState::Ascending : PState::Descending;
            if (w.state != st) {
                if (w.state != PState::Unknown && !end_profile(w))
                    return false;
                if (!new_profile(w, st))
                    return false;
            }
            bool ok;
            if (st == PState::Ascending)
                ok = bezier_up(w, 3, arc, split_cubic, w.minY, w.maxY);
            else
                ok = bezier_down(w, 3, arc, split_cubic, w.minY, w.maxY);
            if (!ok) return false;
            arc -= 3;
            w.lastX = x4;
            w.lastY = y4;
        }
    } while (arc >= arcs);

    return true;
}

static bool decompose_curve(Worker& w, int first, int last, bool flipped)
{
    const Vector* points = w.o_points;
    const char*   tags   = w.o_tags;

    auto make_scaled = [&](int idx) -> TPoint {
        Long sx = w.scaled(points[idx].x);
        Long sy = w.scaled(points[idx].y);
        if (flipped) std::swap(sx, sy);
        return {sx, sy};
    };

    TPoint v_start = make_scaled(first);
    TPoint v_last  = make_scaled(last);
    TPoint v_control = v_start;

    int point_idx = first;
    int tag = static_cast<int>(curve_tag(tags[first]));

    if (tag == static_cast<int>(CurveTag::Cubic)) {
        w.error = kErrInvalidOutline;
        return false;
    }

    if (tag == static_cast<int>(CurveTag::Conic)) {
        if (curve_tag(tags[last]) == CurveTag::On) {
            v_start = v_last;
            last--;
        } else {
            v_start.x = (v_start.x + v_last.x) / 2;
            v_start.y = (v_start.y + v_last.y) / 2;
        }
        point_idx--;
    }

    w.lastX = v_start.x;
    w.lastY = v_start.y;

    while (point_idx < last) {
        point_idx++;
        tag = static_cast<int>(curve_tag(tags[point_idx]));

        if (tag == static_cast<int>(CurveTag::On)) {
            TPoint p = make_scaled(point_idx);
            if (!mono_line_to(w, p.x, p.y))
                return false;
        }
        else if (tag == static_cast<int>(CurveTag::Conic)) {
            v_control = make_scaled(point_idx);

            while (true) {
                if (point_idx >= last) {
                    if (!mono_conic_to(w, v_control.x, v_control.y,
                                       v_start.x, v_start.y))
                        return false;
                    goto Close;
                }

                point_idx++;
                tag = static_cast<int>(curve_tag(tags[point_idx]));
                TPoint p = make_scaled(point_idx);

                if (tag == static_cast<int>(CurveTag::On)) {
                    if (!mono_conic_to(w, v_control.x, v_control.y, p.x, p.y))
                        return false;
                    break;
                }

                if (tag != static_cast<int>(CurveTag::Conic)) {
                    w.error = kErrInvalidOutline;
                    return false;
                }

                TPoint mid = {(v_control.x + p.x) / 2,
                              (v_control.y + p.y) / 2};
                if (!mono_conic_to(w, v_control.x, v_control.y, mid.x, mid.y))
                    return false;
                v_control = p;
            }
        }
        else { // Cubic
            if (point_idx + 1 > last ||
                curve_tag(tags[point_idx + 1]) != CurveTag::Cubic) {
                w.error = kErrInvalidOutline;
                return false;
            }

            TPoint cp1 = make_scaled(point_idx);
            TPoint cp2 = make_scaled(point_idx + 1);
            point_idx += 2;

            if (point_idx <= last) {
                TPoint p = make_scaled(point_idx);
                if (!mono_cubic_to(w, cp1.x, cp1.y, cp2.x, cp2.y, p.x, p.y))
                    return false;
            } else {
                if (!mono_cubic_to(w, cp1.x, cp1.y, cp2.x, cp2.y,
                                   v_start.x, v_start.y))
                    return false;
                goto Close;
            }
        }
    }

    if (!mono_line_to(w, v_start.x, v_start.y))
        return false;

Close:
    return true;
}

static bool convert_glyph(Worker& w, bool flipped)
{
    w.fProfile  = nullptr;
    w.cProfile  = nullptr;
    w.top       = w.buff;
    w.maxBuff   = w.sizeBuff - 1;
    w.numTurns  = 0;
    w.num_Profs = 0;

    int last_idx = -1;
    for (int i = 0; i < w.o_n_contours; i++) {
        w.state    = PState::Unknown;
        w.gProfile = nullptr;

        int first = last_idx + 1;
        last_idx  = w.o_contours[i];

        if (!decompose_curve(w, first, last_idx, flipped))
            return false;

        if (!w.gProfile)
            continue;

        if (w.frac_p(w.lastY) == 0 &&
            w.lastY >= w.minY && w.lastY <= w.maxY)
            if ((w.gProfile->flags & kFlowUp) ==
                (w.cProfile->flags & kFlowUp))
                w.top--;

        if (!end_profile(w))
            return false;

        if (!w.fProfile)
            w.fProfile = w.gProfile;
    }

    if (w.fProfile)
        finalize_profile_table(w);

    return true;
}

// =====================================================================
// Sweep: sorted profile list management
// =====================================================================

static void ins_new(Profile** list, Profile* profile)
{
    Profile** old = list;
    Profile*  cur = *old;
    Long x = profile->X;

    while (cur && cur->X < x) {
        old = &cur->link;
        cur = *old;
    }
    profile->link = cur;
    *old = profile;
}

static void increment(Profile** list, int flow)
{
    Profile** old = list;
    while (*old) {
        Profile* cur = *old;
        if (--cur->height) {
            cur->offset += flow;
            cur->X = cur->x[cur->offset];
            old = &cur->link;
        } else {
            *old = cur->link;
        }
    }

    // bubble-sort with restart
    old = list;
    Profile* cur = *old;
    if (!cur) return;

    while (cur->link) {
        Profile* next = cur->link;
        if (cur->X <= next->X) {
            old = &cur->link;
            cur = next;
        } else {
            *old = next;
            cur->link = next->link;
            next->link = cur;
            old = list;
            cur = *old;
        }
    }
}

// =====================================================================
// Vertical sweep (bitmap)
// =====================================================================

static void vert_sweep_init(Worker& w, int min, [[maybe_unused]] int max)
{
    w.bLine = w.bOrigin - min * w.bPitch;
}

static void vert_sweep_span(Worker& w, [[maybe_unused]] int y, Long x1, Long x2)
{
    int e1 = static_cast<int>(w.trunc_p(w.ceiling_p(x1)));
    int e2 = static_cast<int>(w.trunc_p(w.floor_p(x2)));

    if (e2 >= 0 && e1 <= w.bRight) {
        if (e1 < 0)        e1 = 0;
        if (e2 > w.bRight) e2 = w.bRight;

        int c1 = e1 >> 3;
        int c2 = e2 >> 3;
        int f1 = 0xFF >> (e1 & 7);
        int f2 = ~0x7F >> (e2 & 7);

        Byte* target = w.bLine + c1;
        int span = c2 - c1;

        if (span > 0) {
            target[0] |= static_cast<Byte>(f1);
            while (--span > 0)
                *(++target) = 0xFF;
            target[1] |= static_cast<Byte>(f2);
        } else {
            *target |= static_cast<Byte>(f1 & f2);
        }
    }
}

static void vert_sweep_drop(Worker& w, [[maybe_unused]] int y, Long x1, Long x2)
{
    int e1 = static_cast<int>(w.trunc_p(x1));
    int e2 = static_cast<int>(w.trunc_p(x2));

    if (e1 < 0 || e1 > w.bRight)
        e1 = e2;
    else if (e2 >= 0 && e2 <= w.bRight) {
        int c1 = e2 >> 3;
        int f1 = 0x80 >> (e2 & 7);
        if (w.bLine[c1] & f1)
            return;
    }

    if (e1 >= 0 && e1 <= w.bRight) {
        int c1 = e1 >> 3;
        int f1 = 0x80 >> (e1 & 7);
        w.bLine[c1] |= static_cast<Byte>(f1);
    }
}

static void vert_sweep_step(Worker& w)
{
    w.bLine -= w.bPitch;
}

// =====================================================================
// Vertical sweep (span callback)
// =====================================================================

static void span_sweep_init(Worker& w, int min, [[maybe_unused]] int max)
{
    w.span_y = min;
    w.span_count = 0;
}

static void span_sweep_span(Worker& w, [[maybe_unused]] int y, Long x1, Long x2)
{
    int e1 = static_cast<int>(w.trunc_p(w.ceiling_p(x1)));
    int e2 = static_cast<int>(w.trunc_p(w.floor_p(x2)));

    if (w.span_clip_on) {
        if (w.span_y < w.span_clip.yMin || w.span_y >= w.span_clip.yMax)
            return;
        if (e1 < w.span_clip.xMin) e1 = static_cast<int>(w.span_clip.xMin);
        if (e2 >= w.span_clip.xMax) e2 = static_cast<int>(w.span_clip.xMax) - 1;
    }

    if (e2 >= e1) {
        if (w.span_count >= Worker::kMaxSpans) {
            w.spanFunc(w.span_count, w.span_buf, w.spanUser);
            w.span_count = 0;
        }
        Span& s = w.span_buf[w.span_count++];
        s.x = e1;
        s.len = e2 - e1 + 1;
        s.y = w.span_y;
        s.coverage = 255;
    }
}

static void span_sweep_drop(Worker& w, [[maybe_unused]] int y, Long x1, Long x2)
{
    int e1 = static_cast<int>(w.trunc_p(x1));
    int e2 = static_cast<int>(w.trunc_p(x2));

    int px = (e1 >= 0) ? e1 : e2;
    if (px < 0) return;

    if (w.span_clip_on) {
        if (w.span_y < w.span_clip.yMin || w.span_y >= w.span_clip.yMax)
            return;
        if (px < w.span_clip.xMin || px >= w.span_clip.xMax)
            return;
    }

    if (w.span_count >= Worker::kMaxSpans) {
        w.spanFunc(w.span_count, w.span_buf, w.spanUser);
        w.span_count = 0;
    }
    Span& s = w.span_buf[w.span_count++];
    s.x = px;
    s.len = 1;
    s.y = w.span_y;
    s.coverage = 255;
}

static void span_sweep_step(Worker& w)
{
    if (w.span_count > 0 && w.spanFunc) {
        w.spanFunc(w.span_count, w.span_buf, w.spanUser);
        w.span_count = 0;
    }
    w.span_y++;
}

// =====================================================================
// Horizontal sweep (bitmap, for dropout control)
// =====================================================================

static void horiz_sweep_init([[maybe_unused]] Worker& w,
                             [[maybe_unused]] int min,
                             [[maybe_unused]] int max)
{
}

static void horiz_sweep_span(Worker& w, int y, Long x1, Long x2)
{
    Long e1 = w.ceiling_p(x1);
    Long e2 = w.floor_p(x2);

    if (x1 == e1) {
        e1 = w.trunc_p(e1);
        if (e1 >= 0 && e1 <= w.bTop) {
            Byte* bits = w.bOrigin + (y >> 3) - static_cast<int>(e1) * w.bPitch;
            int f1 = 0x80 >> (y & 7);
            bits[0] |= static_cast<Byte>(f1);
        }
    }

    if (x2 == e2) {
        e2 = w.trunc_p(e2);
        if (e2 >= 0 && e2 <= w.bTop) {
            Byte* bits = w.bOrigin + (y >> 3) - static_cast<int>(e2) * w.bPitch;
            int f1 = 0x80 >> (y & 7);
            bits[0] |= static_cast<Byte>(f1);
        }
    }
}

static void horiz_sweep_drop(Worker& w, int y, Long x1, Long x2)
{
    int e1 = static_cast<int>(w.trunc_p(x1));
    int e2 = static_cast<int>(w.trunc_p(x2));

    if (e1 < 0 || e1 > w.bTop)
        e1 = e2;
    else if (e2 >= 0 && e2 <= w.bTop) {
        Byte* bits = w.bOrigin + (y >> 3) - e2 * w.bPitch;
        int f1 = 0x80 >> (y & 7);
        if (*bits & f1) return;
    }

    if (e1 >= 0 && e1 <= w.bTop) {
        Byte* bits = w.bOrigin + (y >> 3) - e1 * w.bPitch;
        int f1 = 0x80 >> (y & 7);
        *bits |= static_cast<Byte>(f1);
    }
}

static void horiz_sweep_step([[maybe_unused]] Worker& w)
{
}

// =====================================================================
// Generic sweep (Draw_Sweep)
// =====================================================================

// Maximum number of simultaneously active profiles on one scanline.
// Bounded by num_Profs (unsigned short).  512 is generous for any
// practical outline; if exceeded we silently clamp.
static constexpr int kMaxActiveProfiles = 512;

static void draw_sweep(Worker& w)
{
    int min_Y = static_cast<int>(w.maxBuff[0]);
    int max_Y = static_cast<int>(w.maxBuff[w.numTurns]) - 1;

    w.Proc_Sweep_Init(w, min_Y, max_Y);

    Profile* waiting    = w.fProfile;
    Profile* draw_left  = nullptr;
    Profile* draw_right = nullptr;

    for (int y = min_Y; y <= max_Y; ) {
        // activate profiles starting at y
        Profile** Q = &waiting;
        while (*Q) {
            Profile* P = *Q;
            if (P->start == y) {
                *Q = P->link;
                if (P->flags & kFlowUp)
                    ins_new(&draw_left, P);
                else
                    ins_new(&draw_right, P);
            } else {
                Q = &P->link;
            }
        }

        int y_turn = static_cast<int>(*++w.maxBuff);

        do {
            if (w.even_odd) {
                // Even-odd: merge all X intersections, sort, pair sequentially
                Long xs[kMaxActiveProfiles];
                int n = 0;
                for (Profile* P = draw_left; P && n < kMaxActiveProfiles; P = P->link)
                    xs[n++] = P->X;
                for (Profile* P = draw_right; P && n < kMaxActiveProfiles; P = P->link)
                    xs[n++] = P->X;

                std::sort(xs, xs + n);

                for (int i = 0; i + 1 < n; i += 2) {
                    Long x1 = xs[i];
                    Long x2 = xs[i + 1];
                    if (w.ceiling_p(x1) <= w.floor_p(x2))
                        w.Proc_Sweep_Span(w, y, x1, x2);
                }
            } else {
                // Non-zero winding: pair left/right profiles
                int dropouts = 0;
                Profile* P_Left  = draw_left;
                Profile* P_Right = draw_right;

                while (P_Left && P_Right) {
                    Long x1 = P_Left->X;
                    Long x2 = P_Right->X;

                    if (x1 > x2) std::swap(x1, x2);

                    if (w.ceiling_p(x1) <= w.floor_p(x2)) {
                        w.Proc_Sweep_Span(w, y, x1, x2);
                    } else {
                        int doc = P_Left->flags & 7;

                        if (doc & 2)
                            goto Next_Pair;

                        if (doc & 1) {
                            if (P_Left->height == 1 &&
                                P_Left->next == P_Right &&
                                !(P_Left->flags & kOvershootTop &&
                                  x2 - x1 >= w.precision_half))
                                goto Next_Pair;

                            if (P_Left->offset == 0 &&
                                P_Right->next == P_Left &&
                                !(P_Left->flags & kOvershootBottom &&
                                  x2 - x1 >= w.precision_half))
                                goto Next_Pair;
                        }

                        if (doc & 4) {
                            x2 = w.smart_round(x1, x2);
                            x1 = x1 > x2 ? x2 + w.precision : x2 - w.precision;
                        } else {
                            x2 = w.floor_p(x2);
                            x1 = w.ceiling_p(x1);
                        }

                        P_Left->X  = x2;
                        P_Right->X = x1;
                        P_Left->flags |= kDropout;
                        dropouts++;
                    }

                Next_Pair:
                    P_Left  = P_Left->link;
                    P_Right = P_Right->link;
                }

                // handle dropouts
                P_Left  = draw_left;
                P_Right = draw_right;
                while (dropouts) {
                    if (P_Left->flags & kDropout) {
                        w.Proc_Sweep_Drop(w, y, P_Left->X, P_Right->X);
                        P_Left->flags &= ~kDropout;
                        dropouts--;
                    }
                    P_Left  = P_Left->link;
                    P_Right = P_Right->link;
                }
            }

            w.Proc_Sweep_Step(w);

            increment(&draw_left,  1);
            increment(&draw_right, -1);
        } while (++y < y_turn);
    }
}

// =====================================================================
// Single-pass with sub-banding
// =====================================================================

static int render_single_pass(Worker& w, bool flipped, int y_min, int y_max)
{
    int band_top = 0;
    int band_stack[32];

    while (true) {
        w.minY = static_cast<Long>(y_min) * w.precision;
        w.maxY = static_cast<Long>(y_max) * w.precision;
        w.error = kErrOk;

        if (!convert_glyph(w, flipped)) {
            if (w.error != kErrRasterOverflow)
                return w.error;

            if (y_min == y_max)
                return w.error;

            int y_mid = (y_min + y_max) >> 1;
            band_stack[band_top++] = y_min;
            y_min = y_mid + 1;
        } else {
            if (w.fProfile)
                draw_sweep(w);

            if (--band_top < 0)
                break;

            y_max = y_min - 1;
            y_min = band_stack[band_top];
        }
    }

    return kErrOk;
}

// =====================================================================
// Public entry point
// =====================================================================

Error raster_render_mono(const MonoRasterParams* params)
{
    if (!params || !params->source)
        return kErrInvalidArgument;

    const Outline* outline = params->source;

    if (outline->n_points == 0 || outline->n_contours == 0)
        return kErrOk;

    if (!outline->contours || !outline->points)
        return kErrInvalidOutline;

    if (outline->n_points != outline->contours[outline->n_contours - 1] + 1)
        return kErrInvalidOutline;

    bool has_bitmap = params->target &&
                      params->target->buffer &&
                      params->target->width > 0 &&
                      params->target->rows > 0;
    bool has_spans  = params->mono_spans != nullptr;

    if (!has_bitmap && !has_spans)
        return kErrInvalidArgument;

    // initialize worker
    Worker w{};

    w.buff     = w.pool;
    w.sizeBuff = w.pool + kPoolLongs;

    set_high_precision(w, params->high_precision);
    w.dropOutControl = 0;

    // outline reference
    w.o_points     = outline->points;
    w.o_tags       = outline->tags;
    w.o_contours   = outline->contours;
    w.o_n_contours = outline->n_contours;
    w.o_n_points   = outline->n_points;

    // fill rule
    w.even_odd = (outline->flags & static_cast<int>(OutlineFlags::EvenOddFill)) != 0;

    int err;

    if (has_bitmap) {
        MonoBitmap* bm = params->target;

        w.bTop    = bm->rows - 1;
        w.bRight  = bm->width - 1;
        w.bPitch  = bm->pitch;
        w.bOrigin = bm->buffer;

        if (w.bPitch > 0)
            w.bOrigin += w.bTop * w.bPitch;

        // vertical sweep
        w.Proc_Sweep_Init = vert_sweep_init;
        w.Proc_Sweep_Span = vert_sweep_span;
        w.Proc_Sweep_Drop = vert_sweep_drop;
        w.Proc_Sweep_Step = vert_sweep_step;

        err = render_single_pass(w, false, 0, w.bTop);
        if (err) return err;

        // horizontal sweep (dropout control)
        w.Proc_Sweep_Init = horiz_sweep_init;
        w.Proc_Sweep_Span = horiz_sweep_span;
        w.Proc_Sweep_Drop = horiz_sweep_drop;
        w.Proc_Sweep_Step = horiz_sweep_step;

        err = render_single_pass(w, true, 0, w.bRight);
        if (err) return err;
    }
    else {
        // span callback mode: compute bounding box
        BBox cbox;
        outline_get_cbox(outline, &cbox);

        int y_min = static_cast<int>(cbox.yMin >> 6);
        int y_max = static_cast<int>((cbox.yMax + 63) >> 6);

        // apply clip box
        if (params->clip) {
            w.span_clip    = params->clip_box;
            w.span_clip_on = true;
            if (y_min < params->clip_box.yMin)
                y_min = static_cast<int>(params->clip_box.yMin);
            if (y_max > params->clip_box.yMax)
                y_max = static_cast<int>(params->clip_box.yMax);
        }

        if (y_max <= y_min) return kErrOk;

        w.bTop   = y_max - 1;
        w.bRight = static_cast<int>((cbox.xMax + 63) >> 6) - 1;

        w.spanFunc = params->mono_spans;
        w.spanUser = params->user;

        w.Proc_Sweep_Init = span_sweep_init;
        w.Proc_Sweep_Span = span_sweep_span;
        w.Proc_Sweep_Drop = span_sweep_drop;
        w.Proc_Sweep_Step = span_sweep_step;

        err = render_single_pass(w, false, y_min, y_max - 1);
        if (err) return err;

        // flush any remaining spans
        if (w.span_count > 0 && w.spanFunc) {
            w.spanFunc(w.span_count, w.span_buf, w.spanUser);
            w.span_count = 0;
        }
    }

    return kErrOk;
}

} // namespace kvg::ft
