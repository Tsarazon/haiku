/*
 * KosmVG FreeType anti-aliasing raster (C++20)
 *
 * Based on FreeType ftgrays.c
 * Copyright 2000-2003, 2005-2014
 * David Turner, Robert Wilhelm, Werner Lemberg.
 * FreeType project license, FTL.TXT.
 *
 * Exact-coverage anti-aliased scan-converter based on ideas from
 * Raph Levien's LibArt (see http://www.levien.com/libart).
 */

#include "kosmvg-ft.hpp"

#include <cstddef>
#include <cstdlib>
#include <climits>
#include <cstring>

namespace kvg::ft {

/* ---- error codes ---- */

static constexpr int ErrRaster_Invalid_Mode     = -2;
static constexpr int ErrRaster_Invalid_Outline  = -1;
static constexpr int ErrRaster_Invalid_Argument = -3;
static constexpr int ErrRaster_Memory_Overflow  = -4;
static constexpr int ErrRaster_OutOfMemory      = -6;

static constexpr int kMinPoolSize   = 8192;

/* ---- pixel configuration ---- */

static constexpr int PIXEL_BITS = 8;
static constexpr int64_t ONE_PIXEL = 1L << PIXEL_BITS;

static constexpr int32_t trunc_pixel(int64_t x) { return static_cast<int32_t>(x >> PIXEL_BITS); }
static constexpr int32_t fract_pixel(int64_t x) { return static_cast<int32_t>(x & (ONE_PIXEL - 1)); }

static constexpr int64_t upscale(int64_t x)
{
    if constexpr (PIXEL_BITS >= 6)
        return x * (ONE_PIXEL >> 6);
    else
        return x >> (6 - PIXEL_BITS);
}

static constexpr int64_t downscale(int64_t x)
{
    if constexpr (PIXEL_BITS >= 6)
        return x >> (PIXEL_BITS - 6);
    else
        return x * (64 >> PIXEL_BITS);
}

/* Compute dividend/divisor with corrected positive remainder. */
static inline void ft_div_mod(int32_t dividend, int64_t divisor,
                                  int32_t& quotient, int32_t& remainder)
{
    quotient  = static_cast<int32_t>(dividend / divisor);
    remainder = static_cast<int32_t>(dividend % divisor);
    if (remainder < 0) {
        quotient--;
        remainder += static_cast<int32_t>(divisor);
    }
}

/* ---- exception for memory overflow (replaces setjmp/longjmp) ---- */

struct RasterMemoryOverflow {};

/* ---- internal types ---- */

using TCoord = int32_t;   /* integer scanline/pixel coordinate */
using TPos   = int64_t;   /* sub-pixel coordinate              */
using TArea  = int64_t;   /* cell areas, coordinate products   */

static constexpr int kMaxGraySpans = 256;

struct TCell {
    int    x;
    int    cover;
    TArea  area;
    TCell* next;
};

using PCell = TCell*;

struct TWorker {
    TCoord  ex, ey;
    TPos    min_ex, max_ex;
    TPos    min_ey, max_ey;
    TPos    count_ex, count_ey;

    TArea   area;
    int     cover;
    int     invalid;

    PCell   cells;
    ptrdiff_t max_cells;
    ptrdiff_t num_cells;

    TPos    x, y;

    const Outline* outline;
    BBox     clip_box;

    int clip_flags;
    int clipping;

    Span     gray_spans[kMaxGraySpans];
    int         num_gray_spans;
    int         skip_spans;

    SpanFunc render_span;
    void*                    render_span_data;

    int  band_size;
    int  band_shoot;

    void*       buffer;
    long        buffer_size;

    PCell*     ycells;
    TPos       ycount;
};

/* ---- cell management ---- */

static void
gray_init_cells(TWorker& ras, void* buffer, long byte_size)
{
    ras.buffer      = buffer;
    ras.buffer_size = byte_size;

    ras.ycells      = reinterpret_cast<PCell*>(buffer);
    ras.cells       = nullptr;
    ras.max_cells   = 0;
    ras.num_cells   = 0;
    ras.area        = 0;
    ras.cover       = 0;
    ras.invalid     = 1;
}

/* ---- bounding box computation ---- */

static void
gray_compute_cbox(TWorker& ras)
{
    const Outline* outline = ras.outline;
    Vector*  vec     = outline->points;
    Vector*  limit   = vec + outline->n_points;

    if (outline->n_points <= 0) {
        ras.min_ex = ras.max_ex = 0;
        ras.min_ey = ras.max_ey = 0;
        return;
    }

    ras.min_ex = ras.max_ex = vec->x;
    ras.min_ey = ras.max_ey = vec->y;

    vec++;

    for (; vec < limit; vec++) {
        TPos x = vec->x;
        TPos y = vec->y;

        if (x < ras.min_ex) ras.min_ex = x;
        if (x > ras.max_ex) ras.max_ex = x;
        if (y < ras.min_ey) ras.min_ey = y;
        if (y > ras.max_ey) ras.max_ey = y;
    }

    /* truncate the bounding box to integer pixels */
    ras.min_ex = ras.min_ex >> 6;
    ras.min_ey = ras.min_ey >> 6;
    ras.max_ex = (ras.max_ex + 63) >> 6;
    ras.max_ey = (ras.max_ey + 63) >> 6;
}

/* ---- cell lookup ---- */

static PCell
gray_find_cell(TWorker& ras)
{
    PCell* pcell;
    PCell  cell;
    TPos   x = ras.ex;

    if (x > ras.count_ex)
        x = ras.count_ex;

    pcell = &ras.ycells[ras.ey];
    for (;;) {
        cell = *pcell;
        if (cell == nullptr || cell->x > x)
            break;

        if (cell->x == x)
            return cell;

        pcell = &cell->next;
    }

    if (ras.num_cells >= ras.max_cells)
        throw RasterMemoryOverflow{};

    cell        = ras.cells + ras.num_cells++;
    cell->x     = static_cast<int>(x);
    cell->area  = 0;
    cell->cover = 0;

    cell->next  = *pcell;
    *pcell      = cell;

    return cell;
}

static void
gray_record_cell(TWorker& ras)
{
    if (ras.area | ras.cover) {
        PCell cell = gray_find_cell(ras);

        cell->area  += ras.area;
        cell->cover += ras.cover;
    }
}

/* ---- cell positioning ---- */

static void
gray_set_cell(TWorker& ras, TCoord ex, TCoord ey)
{
    ey -= static_cast<TCoord>(ras.min_ey);

    if (ex > ras.max_ex)
        ex = static_cast<TCoord>(ras.max_ex);

    ex -= static_cast<TCoord>(ras.min_ex);
    if (ex < 0)
        ex = -1;

    /* are we moving to a different cell? */
    if (ex != ras.ex || ey != ras.ey) {
        /* record the current one if it is valid */
        if (!ras.invalid)
            gray_record_cell(ras);

        ras.area  = 0;
        ras.cover = 0;
        ras.ex    = ex;
        ras.ey    = ey;
    }

    ras.invalid = (static_cast<unsigned int>(ey) >= static_cast<unsigned int>(ras.count_ey) ||
                   ex >= ras.count_ex);
}

static void
gray_start_cell(TWorker& ras, TCoord ex, TCoord ey)
{
    if (ex > ras.max_ex)
        ex = static_cast<TCoord>(ras.max_ex);

    if (ex < ras.min_ex)
        ex = static_cast<TCoord>(ras.min_ex - 1);

    ras.area    = 0;
    ras.cover   = 0;
    ras.ex      = ex - static_cast<TCoord>(ras.min_ex);
    ras.ey      = ey - static_cast<TCoord>(ras.min_ey);
    ras.invalid = 0;

    gray_set_cell(ras, ex, ey);
}

/* ---- scanline rendering ---- */

#if 1

static void
gray_render_scanline(TWorker& ras, TCoord ey,
                     TPos x1, TCoord y1,
                     TPos x2, TCoord y2)
{
    TCoord ex1, ex2, fx1, fx2, first, dy, delta, mod;
    TPos   p, dx;
    int    incr;

    ex1 = trunc_pixel(x1);
    ex2 = trunc_pixel(x2);

    /* trivial case. Happens often */
    if (y1 == y2) {
        gray_set_cell(ras, ex2, ey);
        return;
    }

    fx1 = fract_pixel(x1);
    fx2 = fract_pixel(x2);

    /* everything is in a single cell. Easy! */
    if (ex1 == ex2)
        goto End;

    /* render a run of adjacent cells on the same scanline */
    dx = x2 - x1;
    dy = y2 - y1;

    if (dx > 0) {
        p     = (ONE_PIXEL - fx1) * dy;
        first = static_cast<TCoord>(ONE_PIXEL);
        incr  = 1;
    } else {
        p     = fx1 * dy;
        first = 0;
        incr  = -1;
        dx    = -dx;
    }

    ft_div_mod(static_cast<int32_t>(p), dx, delta, mod);

    ras.area  += static_cast<TArea>(fx1 + first) * delta;
    ras.cover += delta;
    y1        += delta;
    ex1       += incr;
    gray_set_cell(ras, ex1, ey);

    if (ex1 != ex2) {
        TCoord lift, rem;

        p = ONE_PIXEL * dy;
        ft_div_mod(static_cast<int32_t>(p), dx, lift, rem);

        do {
            delta = lift;
            mod  += rem;
            if (mod >= static_cast<TCoord>(dx)) {
                mod -= static_cast<TCoord>(dx);
                delta++;
            }

            ras.area  += static_cast<TArea>(ONE_PIXEL * delta);
            ras.cover += delta;
            y1        += delta;
            ex1       += incr;
            gray_set_cell(ras, ex1, ey);
        } while (ex1 != ex2);
    }
    fx1 = static_cast<TCoord>(ONE_PIXEL) - first;

End:
    dy = y2 - y1;

    ras.area  += static_cast<TArea>((fx1 + fx2) * dy);
    ras.cover += dy;
}

/* ---- line rendering across multiple scanlines ---- */

static void
gray_render_line(TWorker& ras, TPos from_x, TPos from_y, TPos to_x, TPos to_y)
{
    TCoord ey1, ey2, fy1, fy2, first, delta, mod;
    TPos   p, dx, dy, x, x2;
    int    incr;

    ey1 = trunc_pixel(from_y);
    ey2 = trunc_pixel(to_y);

    /* perform vertical clipping */
    if ((ey1 >= ras.max_ey && ey2 >= ras.max_ey) ||
        (ey1 <  ras.min_ey && ey2 <  ras.min_ey))
        return;

    fy1 = fract_pixel(from_y);
    fy2 = fract_pixel(to_y);

    /* everything is on a single scanline */
    if (ey1 == ey2) {
        gray_render_scanline(ras, ey1, from_x, fy1, to_x, fy2);
        return;
    }

    dx = to_x - from_x;
    dy = to_y - from_y;

    /* vertical line - avoid calling gray_render_scanline */
    if (dx == 0) {
        TCoord ex     = trunc_pixel(from_x);
        TCoord two_fx = static_cast<TCoord>(fract_pixel(from_x) << 1);
        TPos   area;
        TPos   max_ey1;

        if (dy > 0) {
            first = static_cast<TCoord>(ONE_PIXEL);
        } else {
            first = 0;
        }

        delta      = first - fy1;
        ras.area  += static_cast<TArea>(two_fx) * delta;
        ras.cover += delta;

        delta = first + first - static_cast<TCoord>(ONE_PIXEL);
        area  = static_cast<TArea>(two_fx) * delta;
        max_ey1 = ras.count_ey + ras.min_ey;
        if (dy < 0) {
            if (ey1 > max_ey1) {
                ey1 = static_cast<TCoord>((max_ey1 > ey2) ? max_ey1 : ey2);
                gray_set_cell(ras, ex, ey1);
            } else {
                ey1--;
                gray_set_cell(ras, ex, ey1);
            }
            while (ey1 > ey2 && ey1 >= ras.min_ey) {
                ras.area  += area;
                ras.cover += delta;
                ey1--;
                gray_set_cell(ras, ex, ey1);
            }
            if (ey1 != ey2) {
                ey1 = ey2;
                gray_set_cell(ras, ex, ey1);
            }
        } else {
            if (ey1 < ras.min_ey) {
                ey1 = static_cast<TCoord>((ras.min_ey < ey2) ? ras.min_ey : ey2);
                gray_set_cell(ras, ex, ey1);
            } else {
                ey1++;
                gray_set_cell(ras, ex, ey1);
            }
            while (ey1 < ey2 && ey1 < max_ey1) {
                ras.area  += area;
                ras.cover += delta;
                ey1++;
                gray_set_cell(ras, ex, ey1);
            }
            if (ey1 != ey2) {
                ey1 = ey2;
                gray_set_cell(ras, ex, ey1);
            }
        }

        delta      = static_cast<int>(fy2 - ONE_PIXEL + first);
        ras.area  += static_cast<TArea>(two_fx) * delta;
        ras.cover += delta;

        return;
    }

    /* ok, we have to render several scanlines */
    if (dy > 0) {
        p     = (ONE_PIXEL - fy1) * dx;
        first = static_cast<TCoord>(ONE_PIXEL);
        incr  = 1;
    } else {
        p     = fy1 * dx;
        first = 0;
        incr  = -1;
        dy    = -dy;
    }

    /* the fractional part of x-delta is mod/dy. */
    ft_div_mod(static_cast<int32_t>(p), dy, delta, mod);

    x = from_x + delta;
    gray_render_scanline(ras, ey1, from_x, fy1, x, first);

    ey1 += incr;
    gray_set_cell(ras, trunc_pixel(x), ey1);

    if (ey1 != ey2) {
        TCoord lift, rem;

        p = ONE_PIXEL * dx;
        ft_div_mod(static_cast<int32_t>(p), dy, lift, rem);

        do {
            delta = lift;
            mod  += rem;
            if (mod >= static_cast<TCoord>(dy)) {
                mod -= static_cast<TCoord>(dy);
                delta++;
            }

            x2 = x + delta;
            gray_render_scanline(ras, ey1,
                                 x, static_cast<TCoord>(ONE_PIXEL) - first,
                                 x2, first);
            x = x2;

            ey1 += incr;
            gray_set_cell(ras, trunc_pixel(x), ey1);
        } while (ey1 != ey2);
    }

    gray_render_scanline(ras, ey1,
                         x, static_cast<TCoord>(ONE_PIXEL) - first,
                         to_x, fy2);
}

#else

/* ---- alternative line renderer (retained for reference) ---- */

/* speed-up macros for repetitive divisions */
#define FT_UDIVPREP( b )                                       \
    long  b ## _r = (long)( ULONG_MAX >> PIXEL_BITS ) / ( b )
#define FT_UDIV( a, b )                                        \
    ( ( (unsigned long)( a ) * (unsigned long)( b ## _r ) ) >>   \
      ( sizeof( long ) * CHAR_BIT - PIXEL_BITS ) )

static void
gray_render_line(TWorker& ras, TPos from_x, TPos from_y, TPos to_x, TPos to_y)
{
    TPos    dx, dy, fx1, fy1, fx2, fy2;
    TCoord  ex1, ex2, ey1, ey2;

    ex1 = trunc_pixel(from_x);
    ex2 = trunc_pixel(to_x);
    ey1 = trunc_pixel(from_y);
    ey2 = trunc_pixel(to_y);

    /* perform vertical clipping */
    if ((ey1 >= ras.max_ey && ey2 >= ras.max_ey) ||
        (ey1 <  ras.min_ey && ey2 <  ras.min_ey))
        return;

    dx = to_x - from_x;
    dy = to_y - from_y;

    fx1 = fract_pixel(from_x);
    fy1 = fract_pixel(from_y);

    if (ex1 == ex2 && ey1 == ey2)       /* inside one cell */
        ;
    else if (dy == 0)                    /* any horizontal line */
    {
        ex1 = ex2;
        gray_set_cell(ras, ex1, ey1);
    }
    else if (dx == 0)
    {
        if (dy > 0)                       /* vertical line up */
            do {
                fy2 = ONE_PIXEL;
                ras.cover += (fy2 - fy1);
                ras.area  += (fy2 - fy1) * fx1 * 2;
                fy1 = 0;
                ey1++;
                gray_set_cell(ras, ex1, ey1);
            } while (ey1 != ey2);
        else                              /* vertical line down */
            do {
                fy2 = 0;
                ras.cover += (fy2 - fy1);
                ras.area  += (fy2 - fy1) * fx1 * 2;
                fy1 = ONE_PIXEL;
                ey1--;
                gray_set_cell(ras, ex1, ey1);
            } while (ey1 != ey2);
    }
    else                                  /* any other line */
    {
        TArea prod = dx * fy1 - dy * fx1;
        FT_UDIVPREP(dx);
        FT_UDIVPREP(dy);

        do {
            if      (prod                                  <= 0 &&
                     prod - dx * ONE_PIXEL                 >  0)
            {
                fx2 = 0;
                fy2 = (TPos)FT_UDIV(-prod, -dx);
                prod -= dy * ONE_PIXEL;
                ras.cover += (fy2 - fy1);
                ras.area  += (fy2 - fy1) * (fx1 + fx2);
                fx1 = ONE_PIXEL;
                fy1 = fy2;
                ex1--;
            }
            else if (prod - dx * ONE_PIXEL                 <= 0 &&
                     prod - dx * ONE_PIXEL + dy * ONE_PIXEL > 0)
            {
                prod -= dx * ONE_PIXEL;
                fx2 = (TPos)FT_UDIV(-prod, dy);
                fy2 = ONE_PIXEL;
                ras.cover += (fy2 - fy1);
                ras.area  += (fy2 - fy1) * (fx1 + fx2);
                fx1 = fx2;
                fy1 = 0;
                ey1++;
            }
            else if (prod - dx * ONE_PIXEL + dy * ONE_PIXEL <= 0 &&
                     prod                  + dy * ONE_PIXEL >= 0)
            {
                prod += dy * ONE_PIXEL;
                fx2 = ONE_PIXEL;
                fy2 = (TPos)FT_UDIV(prod, dx);
                ras.cover += (fy2 - fy1);
                ras.area  += (fy2 - fy1) * (fx1 + fx2);
                fx1 = 0;
                fy1 = fy2;
                ex1++;
            }
            else
            {
                fx2 = (TPos)FT_UDIV(prod, -dy);
                fy2 = 0;
                prod += dx * ONE_PIXEL;
                ras.cover += (fy2 - fy1);
                ras.area  += (fy2 - fy1) * (fx1 + fx2);
                fx1 = fx2;
                fy1 = ONE_PIXEL;
                ey1--;
            }

            gray_set_cell(ras, ex1, ey1);
        } while (ex1 != ex2 || ey1 != ey2);
    }

    fx2 = fract_pixel(to_x);
    fy2 = fract_pixel(to_y);

    ras.cover += (fy2 - fy1);
    ras.area  += (fy2 - fy1) * (fx1 + fx2);
}

#undef FT_UDIVPREP
#undef FT_UDIV

#endif

/* ---- clipping helpers ---- */

static int
gray_clip_flags(TWorker& ras, TPos x, TPos y)
{
    return ((x > ras.clip_box.xMax) << 0) | ((y > ras.clip_box.yMax) << 1) |
           ((x < ras.clip_box.xMin) << 2) | ((y < ras.clip_box.yMin) << 3);
}

static int
gray_clip_vflags(TWorker& ras, TPos y)
{
    return ((y > ras.clip_box.yMax) << 1) | ((y < ras.clip_box.yMin) << 3);
}

static void
gray_vline(TWorker& ras, TPos x1, TPos y1, TPos x2, TPos y2, int f1, int f2)
{
    f1 &= 10;
    f2 &= 10;
    if ((f1 | f2) == 0) {
        /* fully visible */
        gray_render_line(ras, x1, y1, x2, y2);
    } else if (f1 == f2) {
        /* invisible by Y */
        return;
    } else {
        TPos tx1 = x1, ty1 = y1, tx2 = x2, ty2 = y2;
        TPos clip_y1 = ras.clip_box.yMin;
        TPos clip_y2 = ras.clip_box.yMax;

        if (f1 & 8) /* y1 < clip_y1 */ {
            tx1 = x1 + mul_div(static_cast<Long>(clip_y1 - y1),
                                      static_cast<Long>(x2 - x1),
                                      static_cast<Long>(y2 - y1));
            ty1 = clip_y1;
        }
        if (f1 & 2) /* y1 > clip_y2 */ {
            tx1 = x1 + mul_div(static_cast<Long>(clip_y2 - y1),
                                      static_cast<Long>(x2 - x1),
                                      static_cast<Long>(y2 - y1));
            ty1 = clip_y2;
        }
        if (f2 & 8) /* y2 < clip_y1 */ {
            tx2 = x1 + mul_div(static_cast<Long>(clip_y1 - y1),
                                      static_cast<Long>(x2 - x1),
                                      static_cast<Long>(y2 - y1));
            ty2 = clip_y1;
        }
        if (f2 & 2) /* y2 > clip_y2 */ {
            tx2 = x1 + mul_div(static_cast<Long>(clip_y2 - y1),
                                      static_cast<Long>(x2 - x1),
                                      static_cast<Long>(y2 - y1));
            ty2 = clip_y2;
        }

        gray_render_line(ras, tx1, ty1, tx2, ty2);
    }
}

static void
gray_line_to(TWorker& ras, TPos x2, TPos y2)
{
    if (!ras.clipping) {
        gray_render_line(ras, ras.x, ras.y, x2, y2);
    } else {
        TPos x1, y1, y3, y4;
        TPos clip_x1, clip_x2;
        int  f1, f2, f3, f4;

        f1 = ras.clip_flags;
        f2 = gray_clip_flags(ras, x2, y2);

        if ((f1 & 10) == (f2 & 10) && (f1 & 10) != 0) {
            /* invisible by Y */
            ras.clip_flags = f2;
            goto End;
        }

        x1 = ras.x;
        y1 = ras.y;

        clip_x1 = ras.clip_box.xMin;
        clip_x2 = ras.clip_box.xMax;

        switch (((f1 & 5) << 1) | (f2 & 5)) {
        case 0: /* visible by X */
            gray_vline(ras, x1, y1, x2, y2, f1, f2);
            break;

        case 1: /* x2 > clip_x2 */
            y3 = y1 + mul_div(static_cast<Long>(clip_x2 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            f3 = gray_clip_vflags(ras, y3);
            gray_vline(ras, x1, y1, clip_x2, y3, f1, f3);
            gray_vline(ras, clip_x2, y3, clip_x2, y2, f3, f2);
            break;

        case 2: /* x1 > clip_x2 */
            y3 = y1 + mul_div(static_cast<Long>(clip_x2 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            f3 = gray_clip_vflags(ras, y3);
            gray_vline(ras, clip_x2, y1, clip_x2, y3, f1, f3);
            gray_vline(ras, clip_x2, y3, x2, y2, f3, f2);
            break;

        case 3: /* x1 > clip_x2 && x2 > clip_x2 */
            gray_vline(ras, clip_x2, y1, clip_x2, y2, f1, f2);
            break;

        case 4: /* x2 < clip_x1 */
            y3 = y1 + mul_div(static_cast<Long>(clip_x1 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            f3 = gray_clip_vflags(ras, y3);
            gray_vline(ras, x1, y1, clip_x1, y3, f1, f3);
            gray_vline(ras, clip_x1, y3, clip_x1, y2, f3, f2);
            break;

        case 6: /* x1 > clip_x2 && x2 < clip_x1 */
            y3 = y1 + mul_div(static_cast<Long>(clip_x2 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            y4 = y1 + mul_div(static_cast<Long>(clip_x1 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            f3 = gray_clip_vflags(ras, y3);
            f4 = gray_clip_vflags(ras, y4);
            gray_vline(ras, clip_x2, y1, clip_x2, y3, f1, f3);
            gray_vline(ras, clip_x2, y3, clip_x1, y4, f3, f4);
            gray_vline(ras, clip_x1, y4, clip_x1, y2, f4, f2);
            break;

        case 8: /* x1 < clip_x1 */
            y3 = y1 + mul_div(static_cast<Long>(clip_x1 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            f3 = gray_clip_vflags(ras, y3);
            gray_vline(ras, clip_x1, y1, clip_x1, y3, f1, f3);
            gray_vline(ras, clip_x1, y3, x2, y2, f3, f2);
            break;

        case 9: /* x1 < clip_x1 && x2 > clip_x2 */
            y3 = y1 + mul_div(static_cast<Long>(clip_x1 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            y4 = y1 + mul_div(static_cast<Long>(clip_x2 - x1),
                                     static_cast<Long>(y2 - y1),
                                     static_cast<Long>(x2 - x1));
            f3 = gray_clip_vflags(ras, y3);
            f4 = gray_clip_vflags(ras, y4);
            gray_vline(ras, clip_x1, y1, clip_x1, y3, f1, f3);
            gray_vline(ras, clip_x1, y3, clip_x2, y4, f3, f4);
            gray_vline(ras, clip_x2, y4, clip_x2, y2, f4, f2);
            break;

        case 12: /* x1 < clip_x1 && x2 < clip_x1 */
            gray_vline(ras, clip_x1, y1, clip_x1, y2, f1, f2);
            break;
        }

        ras.clip_flags = f2;
    }

End:
    ras.x = x2;
    ras.y = y2;
}

/* ---- conic / cubic bezier splitting ---- */

static void
gray_split_conic(Vector* base)
{
    TPos a, b;

    base[4].x = base[2].x;
    b = base[1].x;
    a = (base[2].x + b) / 2; base[3].x = static_cast<Pos>(a);
    b = (base[0].x + b) / 2; base[1].x = static_cast<Pos>(b);
    base[2].x = static_cast<Pos>((a + b) / 2);

    base[4].y = base[2].y;
    b = base[1].y;
    a = (base[2].y + b) / 2; base[3].y = static_cast<Pos>(a);
    b = (base[0].y + b) / 2; base[1].y = static_cast<Pos>(b);
    base[2].y = static_cast<Pos>((a + b) / 2);
}

static void
gray_render_conic(TWorker& ras, const Vector* control,
                  const Vector* to)
{
    Vector  bez_stack[16 * 2 + 1];
    Vector* arc = bez_stack;
    TPos      dx, dy;
    int       draw, split;

    arc[0].x = static_cast<Pos>(upscale(to->x));
    arc[0].y = static_cast<Pos>(upscale(to->y));
    arc[1].x = static_cast<Pos>(upscale(control->x));
    arc[1].y = static_cast<Pos>(upscale(control->y));
    arc[2].x = static_cast<Pos>(ras.x);
    arc[2].y = static_cast<Pos>(ras.y);

    /* short-cut the arc that crosses the current band */
    if ((trunc_pixel(arc[0].y) >= ras.max_ey &&
         trunc_pixel(arc[1].y) >= ras.max_ey &&
         trunc_pixel(arc[2].y) >= ras.max_ey) ||
        (trunc_pixel(arc[0].y) <  ras.min_ey &&
         trunc_pixel(arc[1].y) <  ras.min_ey &&
         trunc_pixel(arc[2].y) <  ras.min_ey))
    {
        if (ras.clipping)
            ras.clip_flags = gray_clip_flags(ras, arc[0].x, arc[0].y);
        ras.x = arc[0].x;
        ras.y = arc[0].y;
        return;
    }

    dx = ft_abs(arc[2].x + arc[0].x - 2 * arc[1].x);
    dy = ft_abs(arc[2].y + arc[0].y - 2 * arc[1].y);
    if (dx < dy)
        dx = dy;

    draw = 1;
    while (dx > ONE_PIXEL / 4) {
        dx >>= 2;
        draw <<= 1;
    }

    do {
        split = 1;
        while ((draw & split) == 0) {
            gray_split_conic(arc);
            arc += 2;
            split <<= 1;
        }

        gray_line_to(ras, arc[0].x, arc[0].y);
        arc -= 2;

    } while (--draw);
}

static void
gray_split_cubic(Vector* base)
{
    TPos a, b, c, d;

    base[6].x = base[3].x;
    c = base[1].x;
    d = base[2].x;
    a = (base[0].x + c) / 2; base[1].x = static_cast<Pos>(a);
    b = (base[3].x + d) / 2; base[5].x = static_cast<Pos>(b);
    c = (c + d) / 2;
    a = (a + c) / 2; base[2].x = static_cast<Pos>(a);
    b = (b + c) / 2; base[4].x = static_cast<Pos>(b);
    base[3].x = static_cast<Pos>((a + b) / 2);

    base[6].y = base[3].y;
    c = base[1].y;
    d = base[2].y;
    a = (base[0].y + c) / 2; base[1].y = static_cast<Pos>(a);
    b = (base[3].y + d) / 2; base[5].y = static_cast<Pos>(b);
    c = (c + d) / 2;
    a = (a + c) / 2; base[2].y = static_cast<Pos>(a);
    b = (b + c) / 2; base[4].y = static_cast<Pos>(b);
    base[3].y = static_cast<Pos>((a + b) / 2);
}

static void
gray_render_cubic(TWorker& ras, const Vector* control1,
                  const Vector* control2,
                  const Vector* to)
{
    Vector  bez_stack[16 * 3 + 1];
    Vector* arc   = bez_stack;
    Vector* limit = bez_stack + 45;
    TPos      dx, dy, dx_, dy_;
    TPos      dx1, dy1, dx2, dy2;
    TPos      L, s, s_limit;

    arc[0].x = static_cast<Pos>(upscale(to->x));
    arc[0].y = static_cast<Pos>(upscale(to->y));
    arc[1].x = static_cast<Pos>(upscale(control2->x));
    arc[1].y = static_cast<Pos>(upscale(control2->y));
    arc[2].x = static_cast<Pos>(upscale(control1->x));
    arc[2].y = static_cast<Pos>(upscale(control1->y));
    arc[3].x = static_cast<Pos>(ras.x);
    arc[3].y = static_cast<Pos>(ras.y);

    /* short-cut arc crossing current band */
    if ((trunc_pixel(arc[0].y) >= ras.max_ey &&
         trunc_pixel(arc[1].y) >= ras.max_ey &&
         trunc_pixel(arc[2].y) >= ras.max_ey &&
         trunc_pixel(arc[3].y) >= ras.max_ey) ||
        (trunc_pixel(arc[0].y) <  ras.min_ey &&
         trunc_pixel(arc[1].y) <  ras.min_ey &&
         trunc_pixel(arc[2].y) <  ras.min_ey &&
         trunc_pixel(arc[3].y) <  ras.min_ey))
    {
        if (ras.clipping)
            ras.clip_flags = gray_clip_flags(ras, arc[0].x, arc[0].y);
        ras.x = arc[0].x;
        ras.y = arc[0].y;
        return;
    }

    for (;;) {
        dx = dx_ = arc[3].x - arc[0].x;
        dy = dy_ = arc[3].y - arc[0].y;

        L = ft_hypot(static_cast<Pos>(dx_), static_cast<Pos>(dy_));

        /* avoid possible arithmetic overflow below by splitting */
        if (L >= (1 << 23))
            goto Split;

        s_limit = L * static_cast<TPos>(ONE_PIXEL / 6);

        dx1 = arc[1].x - arc[0].x;
        dy1 = arc[1].y - arc[0].y;
        s = ft_abs(static_cast<Pos>(dy * dx1 - dx * dy1));

        if (s > s_limit)
            goto Split;

        dx2 = arc[2].x - arc[0].x;
        dy2 = arc[2].y - arc[0].y;
        s = ft_abs(static_cast<Pos>(dy * dx2 - dx * dy2));

        if (s > s_limit)
            goto Split;

        if (dx1 * (dx1 - dx) + dy1 * (dy1 - dy) > 0 ||
            dx2 * (dx2 - dx) + dy2 * (dy2 - dy) > 0)
            goto Split;

        gray_line_to(ras, arc[0].x, arc[0].y);

        if (arc == bez_stack)
            return;

        arc -= 3;
        continue;

    Split:
        if (arc == limit)
            return;
        gray_split_cubic(arc);
        arc += 3;
    }
}

/* ---- move_to callback ---- */

static int
gray_move_to(const Vector* to, TWorker& ras)
{
    TPos x, y;

    /* record current cell, if any */
    if (!ras.invalid)
        gray_record_cell(ras);

    /* start to a new position */
    x = upscale(to->x);
    y = upscale(to->y);

    gray_start_cell(ras, trunc_pixel(x), trunc_pixel(y));

    if (ras.clipping)
        ras.clip_flags = gray_clip_flags(ras, x, y);
    ras.x = x;
    ras.y = y;
    return 0;
}

/* ---- horizontal line span output ---- */

static void
gray_hline(TWorker& ras, TCoord x, TCoord y, TPos area, int acount)
{
    int coverage;

    coverage = static_cast<int>(area >> (PIXEL_BITS * 2 + 1 - 8));
    if (coverage < 0)
        coverage = -coverage;

    if (ras.outline->flags & static_cast<int>(OutlineFlags::EvenOddFill)) {
        coverage &= 511;

        if (coverage > 256)
            coverage = 512 - coverage;
        else if (coverage == 256)
            coverage = 255;
    } else {
        /* normal non-zero winding rule */
        if (coverage >= 256)
            coverage = 255;
    }

    y += static_cast<TCoord>(ras.min_ey);
    x += static_cast<TCoord>(ras.min_ex);

    if (x >= (1 << 23))
        x = (1 << 23) - 1;

    if (y >= (1 << 23))
        y = (1 << 23) - 1;

    if (coverage) {
        Span* span;
        int      count;
        int      skip;

        count = ras.num_gray_spans;
        span  = ras.gray_spans + count - 1;
        if (count > 0                          &&
            span->y == y                       &&
            span->x + span->len == x           &&
            span->coverage == coverage)
        {
            span->len = span->len + acount;
            return;
        }

        if (count >= kMaxGraySpans) {
            if (ras.render_span && count > ras.skip_spans) {
                skip = ras.skip_spans > 0 ? ras.skip_spans : 0;
                ras.render_span(ras.num_gray_spans - skip,
                                ras.gray_spans + skip,
                                ras.render_span_data);
            }

            ras.skip_spans -= ras.num_gray_spans;
            ras.num_gray_spans = 0;
            span = ras.gray_spans;
        } else {
            span++;
        }

        span->x        = x;
        span->len      = acount;
        span->y        = y;
        span->coverage = static_cast<unsigned char>(coverage);

        ras.num_gray_spans++;
    }
}

/* ---- sweep: iterate cells and produce spans ---- */

static void
gray_sweep(TWorker& ras)
{
    if (ras.num_cells == 0)
        return;

    for (int yindex = 0; yindex < ras.ycount; yindex++) {
        PCell  cell  = ras.ycells[yindex];
        TCoord cover = 0;
        TCoord x     = 0;

        for (; cell != nullptr; cell = cell->next) {
            TArea area;

            if (cell->x > x && cover != 0)
                gray_hline(ras, x, yindex, cover * (ONE_PIXEL * 2),
                           cell->x - x);

            cover += cell->cover;
            area   = cover * (ONE_PIXEL * 2) - cell->area;

            if (area != 0 && cell->x >= 0)
                gray_hline(ras, cell->x, yindex, area, 1);

            x = cell->x + 1;
        }

        if (ras.count_ex > x && cover != 0)
            gray_hline(ras, x, yindex, cover * (ONE_PIXEL * 2),
                       static_cast<int>(ras.count_ex - x));
    }
}

/* ---- outline validation & cbox ---- */

Error outline_check(Outline* outline)
{
    if (outline) {
        Int n_points   = outline->n_points;
        Int n_contours = outline->n_contours;
        Int end0, end;
        Int n;

        if (n_points == 0 && n_contours == 0) return 0;
        if (n_points <= 0 || n_contours <= 0) goto Bad;

        end0 = end = -1;
        for (n = 0; n < n_contours; n++) {
            end = outline->contours[n];
            if (end <= end0 || end >= n_points) goto Bad;
            end0 = end;
        }

        if (end != n_points - 1) goto Bad;
        return 0;
    }

Bad:
    return ErrRaster_Invalid_Outline;
}

void outline_get_cbox(const Outline* outline, BBox* acbox)
{
    Pos xMin, yMin, xMax, yMax;

    if (outline && acbox) {
        if (outline->n_points == 0) {
            xMin = 0; yMin = 0; xMax = 0; yMax = 0;
        } else {
            Vector* vec   = outline->points;
            Vector* limit = vec + outline->n_points;

            xMin = xMax = vec->x;
            yMin = yMax = vec->y;
            vec++;

            for (; vec < limit; vec++) {
                Pos x = vec->x, y = vec->y;
                if (x < xMin) xMin = x;
                if (x > xMax) xMax = x;
                if (y < yMin) yMin = y;
                if (y > yMax) yMax = y;
            }
        }
        acbox->xMin = xMin;
        acbox->xMax = xMax;
        acbox->yMin = yMin;
        acbox->yMax = yMax;
    }
}

/* ---- outline decomposition ---- */

static int
Outline_Decompose(const Outline* outline, TWorker& ras)
{
    Vector  v_last;
    Vector  v_control;
    Vector  v_start;

    Vector* point;
    Vector* limit;
    char*      tags;

    int   n;
    int   first;
    int   error;
    CurveTag tag;

    if (!outline)
        return ErrRaster_Invalid_Outline;

    first = 0;

    for (n = 0; n < outline->n_contours; n++) {
        int last = outline->contours[n];
        if (last < 0)
            goto Invalid_Outline;
        limit = outline->points + last;

        v_start   = outline->points[first];
        v_last    = outline->points[last];

        v_control = v_start;

        point = outline->points + first;
        tags  = outline->tags  + first;
        tag   = curve_tag(tags[0]);

        /* A contour cannot start with a cubic control point! */
        if (tag == CurveTag::Cubic)
            goto Invalid_Outline;

        /* check first point to determine origin */
        if (tag == CurveTag::Conic) {
            if (curve_tag(outline->tags[last]) == CurveTag::On) {
                v_start = v_last;
                limit--;
            } else {
                v_start.x = (v_start.x + v_last.x) / 2;
                v_start.y = (v_start.y + v_last.y) / 2;
                v_last = v_start;
            }
            point--;
            tags--;
        }

        error = gray_move_to(&v_start, ras);
        if (error) goto Exit;

        while (point < limit) {
            point++;
            tags++;

            tag = curve_tag(tags[0]);
            switch (tag) {
            case CurveTag::On: {
                Vector vec;
                vec.x = point->x;
                vec.y = point->y;
                gray_line_to(ras, upscale(vec.x), upscale(vec.y));
                continue;
            }

            case CurveTag::Conic: {
                v_control.x = point->x;
                v_control.y = point->y;

            Do_Conic:
                if (point < limit) {
                    Vector vec;
                    Vector v_middle;

                    point++;
                    tags++;
                    tag = curve_tag(tags[0]);

                    vec.x = point->x;
                    vec.y = point->y;

                    if (tag == CurveTag::On) {
                        gray_render_conic(ras, &v_control, &vec);
                        continue;
                    }

                    if (tag != CurveTag::Conic)
                        goto Invalid_Outline;

                    v_middle.x = (v_control.x + vec.x) / 2;
                    v_middle.y = (v_control.y + vec.y) / 2;

                    gray_render_conic(ras, &v_control, &v_middle);

                    v_control = vec;
                    goto Do_Conic;
                }

                gray_render_conic(ras, &v_control, &v_start);
                goto Close;
            }

            default: /* CurveTag::Cubic */ {
                Vector vec1, vec2;

                if (point + 1 > limit ||
                    curve_tag(tags[1]) != CurveTag::Cubic)
                    goto Invalid_Outline;

                point += 2;
                tags  += 2;

                vec1.x = point[-2].x;
                vec1.y = point[-2].y;

                vec2.x = point[-1].x;
                vec2.y = point[-1].y;

                if (point <= limit) {
                    Vector vec;
                    vec.x = point->x;
                    vec.y = point->y;
                    gray_render_cubic(ras, &vec1, &vec2, &vec);
                    continue;
                }

                gray_render_cubic(ras, &vec1, &vec2, &v_start);
                goto Close;
            }
            }
        }

        /* close the contour with a line segment */
        gray_line_to(ras, upscale(v_start.x), upscale(v_start.y));

    Close:
        first = last + 1;
    }

    return 0;

Exit:
    return error;

Invalid_Outline:
    return ErrRaster_Invalid_Outline;
}

/* ---- band structure ---- */

struct TBand {
    TPos min, max;
};

/* ---- glyph conversion (inner - catches memory overflow) ---- */

static int
gray_convert_glyph_inner(TWorker& ras)
{
    try {
        int error = Outline_Decompose(ras.outline, ras);
        if (!ras.invalid)
            gray_record_cell(ras);
        return error;
    } catch (const RasterMemoryOverflow&) {
        return ErrRaster_Memory_Overflow;
    }
}

/* ---- glyph conversion (outer - banding) ---- */

static int
gray_convert_glyph(TWorker& ras)
{
    TBand            bands[40];
    TBand*           band;
    int              n, num_bands;
    TPos             min, max, max_y;
    BBox*     clip;
    int              skip;

    ras.num_gray_spans = 0;

    /* set up state in the raster object */
    gray_compute_cbox(ras);

    /* clip to target bitmap, exit if nothing to do */
    clip = &ras.clip_box;

    if (ras.max_ex <= clip->xMin || ras.min_ex >= clip->xMax ||
        ras.max_ey <= clip->yMin || ras.min_ey >= clip->yMax)
        return 0;

    ras.clip_flags = ras.clipping = 0;

    if (ras.min_ex < clip->xMin) { ras.min_ex = clip->xMin; ras.clipping = 1; }
    if (ras.min_ey < clip->yMin) { ras.min_ey = clip->yMin; ras.clipping = 1; }
    if (ras.max_ex > clip->xMax) { ras.max_ex = clip->xMax; ras.clipping = 1; }
    if (ras.max_ey > clip->yMax) { ras.max_ey = clip->yMax; ras.clipping = 1; }

    clip->xMin = static_cast<Pos>((ras.min_ex - 1) * ONE_PIXEL);
    clip->yMin = static_cast<Pos>((ras.min_ey - 1) * ONE_PIXEL);
    clip->xMax = static_cast<Pos>((ras.max_ex + 1) * ONE_PIXEL);
    clip->yMax = static_cast<Pos>((ras.max_ey + 1) * ONE_PIXEL);

    ras.count_ex = ras.max_ex - ras.min_ex;
    ras.count_ey = ras.max_ey - ras.min_ey;

    /* set up vertical bands */
    num_bands = static_cast<int>((ras.max_ey - ras.min_ey) / ras.band_size);
    if (num_bands == 0) num_bands = 1;
    if (num_bands >= 39) num_bands = 39;

    ras.band_shoot = 0;

    min   = ras.min_ey;
    max_y = ras.max_ey;

    for (n = 0; n < num_bands; n++, min = max) {
        max = min + ras.band_size;
        if (n == num_bands - 1 || max > max_y)
            max = max_y;

        bands[0].min = min;
        bands[0].max = max;
        band         = bands;

        while (band >= bands) {
            TPos bottom, top, middle;
            int  error;

            {
                PCell cells_max;
                int   yindex;
                int   cell_start, cell_end, cell_mod;

                ras.ycells = reinterpret_cast<PCell*>(ras.buffer);
                ras.ycount = band->max - band->min;

                cell_start = static_cast<int>(sizeof(PCell) * static_cast<size_t>(ras.ycount));
                cell_mod   = cell_start % static_cast<int>(sizeof(TCell));
                if (cell_mod > 0)
                    cell_start += static_cast<int>(sizeof(TCell)) - cell_mod;

                cell_end  = static_cast<int>(ras.buffer_size);
                cell_end -= cell_end % static_cast<int>(sizeof(TCell));

                cells_max = reinterpret_cast<PCell>(
                    static_cast<char*>(ras.buffer) + cell_end);
                ras.cells = reinterpret_cast<PCell>(
                    static_cast<char*>(ras.buffer) + cell_start);
                if (ras.cells >= cells_max)
                    goto ReduceBands;

                ras.max_cells = static_cast<int>(cells_max - ras.cells);
                if (ras.max_cells < 2)
                    goto ReduceBands;

                for (yindex = 0; yindex < ras.ycount; yindex++)
                    ras.ycells[yindex] = nullptr;
            }

            ras.num_cells = 0;
            ras.invalid   = 1;
            ras.min_ey    = band->min;
            ras.max_ey    = band->max;
            ras.count_ey  = band->max - band->min;

            error = gray_convert_glyph_inner(ras);

            if (!error) {
                gray_sweep(ras);
                band--;
                continue;
            } else if (error != ErrRaster_Memory_Overflow) {
                return 1;
            }

        ReduceBands:
            /* render pool overflow; reduce the render band by half */
            bottom = band->min;
            top    = band->max;
            middle = bottom + ((top - bottom) >> 1);

            if (middle == bottom) {
                return ErrRaster_OutOfMemory;
            }

            if (bottom - top >= ras.band_size)
                ras.band_shoot++;

            band[1].min = bottom;
            band[1].max = middle;
            band[0].min = middle;
            band[0].max = top;
            band++;
        }
    }

    if (ras.render_span && ras.num_gray_spans > ras.skip_spans) {
        skip = ras.skip_spans > 0 ? ras.skip_spans : 0;
        ras.render_span(ras.num_gray_spans - skip,
                        ras.gray_spans + skip,
                        ras.render_span_data);
    }

    ras.skip_spans -= ras.num_gray_spans;

    if (ras.band_shoot > 8 && ras.band_size > 16)
        ras.band_size = ras.band_size / 2;

    return 0;
}

/* ---- top-level raster render ---- */

static int
gray_raster_render(TWorker& ras, void* buffer, long buffer_size,
                   const RasterParams* params)
{
    const Outline* outline = params->source;
    if (outline == nullptr)
        return ErrRaster_Invalid_Outline;

    /* return immediately if the outline is empty */
    if (outline->n_points == 0 || outline->n_contours <= 0)
        return 0;

    if (!outline->contours || !outline->points)
        return ErrRaster_Invalid_Outline;

    if (outline->n_points != outline->contours[outline->n_contours - 1] + 1)
        return ErrRaster_Invalid_Outline;

    /* this version does not support monochrome rendering */
    if (!(params->flags & RasterFlags::AA))
        return ErrRaster_Invalid_Mode;

    if (!(params->flags & RasterFlags::Direct))
        return ErrRaster_Invalid_Mode;

    /* compute clipping box */
    if (params->flags & RasterFlags::Clip) {
        ras.clip_box = params->clip_box;
    } else {
        ras.clip_box.xMin = -(1 << 23);
        ras.clip_box.yMin = -(1 << 23);
        ras.clip_box.xMax =  (1 << 23) - 1;
        ras.clip_box.yMax =  (1 << 23) - 1;
    }

    gray_init_cells(ras, buffer, buffer_size);

    ras.outline   = outline;
    ras.num_cells = 0;
    ras.invalid   = 1;
    ras.band_size = static_cast<int>(buffer_size / static_cast<long>(sizeof(TCell) * 8));

    ras.render_span      = params->gray_spans;
    ras.render_span_data = params->user;

    return gray_convert_glyph(ras);
}

void
raster_render(const RasterParams* params)
{
    char   stack[kMinPoolSize];
    size_t length = kMinPoolSize;

    TWorker worker{};
    worker.skip_spans = 0;
    int rendered_spans = 0;
    int error = gray_raster_render(worker, stack, static_cast<long>(length), params);
    while (error == ErrRaster_OutOfMemory) {
        if (worker.skip_spans < 0)
            rendered_spans += -worker.skip_spans;
        worker.skip_spans = rendered_spans;
        length *= 2;
        void* heap = std::malloc(length);
        error = gray_raster_render(worker, heap, static_cast<long>(length), params);
        std::free(heap);
    }
}

} // namespace kvg::ft
