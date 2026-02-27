/*
 * KosmVG FreeType path stroker (C++20)
 *
 * Based on FreeType ftstroke.c
 * Copyright 2002-2006, 2008-2011, 2013
 * David Turner, Robert Wilhelm, Werner Lemberg.
 * FreeType project license, FTL.TXT.
 */

#include "kosmvg-ft.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <new>

namespace kvg::ft {

/* ---- bezier computation thresholds ---- */

static constexpr Angle kSmallConicThreshold = angle_pi / 6;
static constexpr Angle kSmallCubicThreshold = angle_pi / 8;

static constexpr Pos kEpsilon = 2;

static constexpr bool ft_is_small(Pos x)
{
    return x > -kEpsilon && x < kEpsilon;
}

static Pos ft_pos_abs(Pos x)
{
    return x >= 0 ? x : -x;
}

/* ---- bezier splitting ---- */

static void ft_conic_split(Vector* base)
{
    Pos a, b;

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

static Bool ft_conic_is_small_enough(Vector* base,
                                             Angle*  angle_in,
                                             Angle*  angle_out)
{
    Vector d1, d2;
    Angle  theta;
    Int    close1, close2;

    d1.x = base[1].x - base[2].x;
    d1.y = base[1].y - base[2].y;
    d2.x = base[0].x - base[1].x;
    d2.y = base[0].y - base[1].y;

    close1 = ft_is_small(d1.x) && ft_is_small(d1.y);
    close2 = ft_is_small(d2.x) && ft_is_small(d2.y);

    if (close1) {
        if (close2) {
            /* basically a point; do nothing to retain original direction */
        } else {
            *angle_in = *angle_out = ft_atan2(d2.x, d2.y);
        }
    } else {
        if (close2) {
            *angle_in = *angle_out = ft_atan2(d1.x, d1.y);
        } else {
            *angle_in  = ft_atan2(d1.x, d1.y);
            *angle_out = ft_atan2(d2.x, d2.y);
        }
    }

    theta = ft_pos_abs(angle_diff(*angle_in, *angle_out));

    return theta < kSmallConicThreshold;
}

static void ft_cubic_split(Vector* base)
{
    Pos a, b, c;

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

/* Return the average of angle1 and angle2. */
static Angle ft_angle_mean(Angle angle1, Angle angle2)
{
    return angle1 + angle_diff(angle1, angle2) / 2;
}

static Bool ft_cubic_is_small_enough(Vector* base,
                                             Angle*  angle_in,
                                             Angle*  angle_mid,
                                             Angle*  angle_out)
{
    Vector d1, d2, d3;
    Angle  theta1, theta2;
    Int    close1, close2, close3;

    d1.x = base[2].x - base[3].x;  d1.y = base[2].y - base[3].y;
    d2.x = base[1].x - base[2].x;  d2.y = base[1].y - base[2].y;
    d3.x = base[0].x - base[1].x;  d3.y = base[0].y - base[1].y;

    close1 = ft_is_small(d1.x) && ft_is_small(d1.y);
    close2 = ft_is_small(d2.x) && ft_is_small(d2.y);
    close3 = ft_is_small(d3.x) && ft_is_small(d3.y);

    if (close1) {
        if (close2) {
            if (close3) {
                /* basically a point */
            } else {
                *angle_in = *angle_mid = *angle_out = ft_atan2(d3.x, d3.y);
            }
        } else {
            if (close3) {
                *angle_in = *angle_mid = *angle_out = ft_atan2(d2.x, d2.y);
            } else {
                *angle_in = *angle_mid = ft_atan2(d2.x, d2.y);
                *angle_out = ft_atan2(d3.x, d3.y);
            }
        }
    } else {
        if (close2) {
            if (close3) {
                *angle_in = *angle_mid = *angle_out = ft_atan2(d1.x, d1.y);
            } else {
                *angle_in  = ft_atan2(d1.x, d1.y);
                *angle_out = ft_atan2(d3.x, d3.y);
                *angle_mid = ft_angle_mean(*angle_in, *angle_out);
            }
        } else {
            if (close3) {
                *angle_in  = ft_atan2(d1.x, d1.y);
                *angle_mid = *angle_out = ft_atan2(d2.x, d2.y);
            } else {
                *angle_in  = ft_atan2(d1.x, d1.y);
                *angle_mid = ft_atan2(d2.x, d2.y);
                *angle_out = ft_atan2(d3.x, d3.y);
            }
        }
    }

    theta1 = ft_pos_abs(angle_diff(*angle_in, *angle_mid));
    theta2 = ft_pos_abs(angle_diff(*angle_mid, *angle_out));

    return theta1 < kSmallCubicThreshold &&
           theta2 < kSmallCubicThreshold;
}

/* ==================================================================== */
/*  STROKE BORDERS                                                      */
/* ==================================================================== */

enum {
    kStrokeTagOn    = 1,  /* on-curve point  */
    kStrokeTagCubic = 2,  /* cubic off-point */
    kStrokeTagBegin = 4,  /* sub-path start  */
    kStrokeTagEnd   = 8   /* sub-path end    */
};

static constexpr Byte kStrokeTagBeginEnd =
    kStrokeTagBegin | kStrokeTagEnd;

struct StrokeBorderRec {
    std::vector<Vector> points;
    std::vector<Byte>   tags;
    bool       movable;
    int        start;
    bool       valid;
};

using StrokeBorder = StrokeBorderRec*;

/* ---- border operations ---- */

static void ft_stroke_border_close(StrokeBorder border, bool reverse)
{
    UInt start = static_cast<UInt>(border->start);
    UInt count = static_cast<UInt>(border->points.size());

    assert(border->start >= 0);

    /* don't record empty paths! */
    if (count <= start + 1U) {
        border->points.resize(start);
        border->tags.resize(start);
    } else {
        /* copy the last point to the start of this sub-path */
        border->points[start] = border->points.back();
        border->tags[start]   = border->tags.back();
        border->points.pop_back();
        border->tags.pop_back();
        count--;

        if (reverse) {
            std::reverse(border->points.begin() + start + 1,
                         border->points.end());
            std::reverse(border->tags.begin() + start + 1,
                         border->tags.end());
        }

        border->tags[start]     |= kStrokeTagBegin;
        border->tags[count - 1] |= kStrokeTagEnd;
    }

    border->start   = -1;
    border->movable = false;
}

static Error ft_stroke_border_lineto(StrokeBorder border,
                                             Vector* to, bool movable)
{
    assert(border->start >= 0);

    if (border->movable) {
        /* move last point */
        border->points.back() = *to;
    } else {
        /* don't add zero-length lineto, but always add moveto */
        if (border->points.size() > static_cast<size_t>(border->start) &&
            ft_is_small(border->points.back().x - to->x) &&
            ft_is_small(border->points.back().y - to->y))
            return 0;

        /* add one point */
        border->points.push_back(*to);
        border->tags.push_back(kStrokeTagOn);
    }
    border->movable = movable;
    return 0;
}

static Error ft_stroke_border_conicto(StrokeBorder border,
                                              Vector*      control,
                                              Vector*      to)
{
    assert(border->start >= 0);

    border->points.push_back(*control);
    border->points.push_back(*to);
    border->tags.push_back(0);
    border->tags.push_back(kStrokeTagOn);

    border->movable = false;
    return 0;
}

static Error ft_stroke_border_cubicto(StrokeBorder border,
                                              Vector*      control1,
                                              Vector*      control2,
                                              Vector*      to)
{
    assert(border->start >= 0);

    border->points.push_back(*control1);
    border->points.push_back(*control2);
    border->points.push_back(*to);
    border->tags.push_back(kStrokeTagCubic);
    border->tags.push_back(kStrokeTagCubic);
    border->tags.push_back(kStrokeTagOn);

    border->movable = false;
    return 0;
}

static constexpr Angle kArcCubicAngle = angle_pi / 2;

static Error
ft_stroke_border_arcto(StrokeBorder border,
                       Vector*      center,
                       Fixed        radius,
                       Angle        angle_start,
                       Angle        angle_diff)
{
    Fixed  coef;
    Vector a0, a1, a2, a3;
    Int    i, arcs = 1;
    Error  error = 0;

    /* number of cubic arcs to draw */
    while ( angle_diff > kArcCubicAngle * arcs ||
           -angle_diff > kArcCubicAngle * arcs)
        arcs++;

    /* control tangents */
    coef  = ft_tan(angle_diff / (4 * arcs));
    coef += coef / 3;

    /* compute start and first control point */
    vector_from_polar(&a0, radius, angle_start);
    a1.x = mul_fix(-a0.y, coef);
    a1.y = mul_fix( a0.x, coef);

    a0.x += center->x;
    a0.y += center->y;
    a1.x += a0.x;
    a1.y += a0.y;

    for (i = 1; i <= arcs; i++) {
        /* compute end and second control point */
        vector_from_polar(&a3, radius,
                                 angle_start + i * angle_diff / arcs);
        a2.x = mul_fix( a3.y, coef);
        a2.y = mul_fix(-a3.x, coef);

        a3.x += center->x;
        a3.y += center->y;
        a2.x += a3.x;
        a2.y += a3.y;

        /* add cubic arc */
        error = ft_stroke_border_cubicto(border, &a1, &a2, &a3);
        if (error) break;

        a1.x = a3.x - a2.x + a3.x;
        a1.y = a3.y - a2.y + a3.y;
    }

    return error;
}

static Error ft_stroke_border_moveto(StrokeBorder border,
                                             Vector*      to)
{
    /* close current open path if any */
    if (border->start >= 0)
        ft_stroke_border_close(border, false);

    border->start   = static_cast<int>(border->points.size());
    border->movable = false;

    return ft_stroke_border_lineto(border, to, false);
}

static void ft_stroke_border_init(StrokeBorder border)
{
    border->points.clear();
    border->tags.clear();
    border->start   = -1;
    border->movable = false;
    border->valid   = false;
}

static void ft_stroke_border_reset(StrokeBorder border)
{
    border->points.clear();
    border->tags.clear();
    border->start   = -1;
    border->valid   = false;
}

static Error ft_stroke_border_get_counts(StrokeBorder border,
                                                 UInt*        anum_points,
                                                 UInt*        anum_contours)
{
    Error error        = 0;
    UInt  num_points   = 0;
    UInt  num_contours = 0;

    UInt  count = static_cast<UInt>(border->points.size());
    Int   in_contour = 0;

    for (UInt i = 0; i < count; i++, num_points++) {
        Byte tag = border->tags[i];

        if (tag & kStrokeTagBegin) {
            if (in_contour != 0) goto Fail;
            in_contour = 1;
        } else if (in_contour == 0)
            goto Fail;

        if (tag & kStrokeTagEnd) {
            in_contour = 0;
            num_contours++;
        }
    }

    if (in_contour != 0) goto Fail;

    border->valid = true;

Exit:
    *anum_points   = num_points;
    *anum_contours = num_contours;
    return error;

Fail:
    num_points   = 0;
    num_contours = 0;
    goto Exit;
}

static void ft_stroke_border_export(StrokeBorder border,
                                    Outline*     outline)
{
    UInt count = static_cast<UInt>(border->points.size());

    /* copy point locations */
    std::memcpy(outline->points + outline->n_points,
                border->points.data(),
                count * sizeof(Vector));

    /* copy tags */
    {
        Byte* read  = border->tags.data();
        Byte* write = reinterpret_cast<Byte*>(
            outline->tags + outline->n_points);

        for (UInt i = 0; i < count; i++, read++, write++) {
            if (*read & kStrokeTagOn)
                *write = static_cast<char>(CurveTag::On);
            else if (*read & kStrokeTagCubic)
                *write = static_cast<char>(CurveTag::Cubic);
            else
                *write = static_cast<char>(CurveTag::Conic);
        }
    }

    /* copy contours */
    {
        Byte* tags_ptr = border->tags.data();
        int*         write    = outline->contours + outline->n_contours;
        Int   idx      = static_cast<Int>(outline->n_points);

        for (UInt i = 0; i < count; i++, tags_ptr++, idx++) {
            if (*tags_ptr & kStrokeTagEnd) {
                *write++ = idx;
                outline->n_contours++;
            }
        }
    }

    outline->n_points += static_cast<int>(count);

    assert(outline_check(outline) == 0);
}

/* ==================================================================== */
/*  STROKER                                                             */
/* ==================================================================== */

static constexpr Angle ft_side_to_rotate(Int s)
{
    return angle_pi2 - s * angle_pi;
}

struct StrokerRec {
    Angle  angle_in;
    Angle  angle_out;
    Vector center;
    Fixed  line_length;
    bool          first_point;
    bool          subpath_open;
    Angle  subpath_angle;
    Vector subpath_start;
    Fixed  subpath_line_length;
    bool          handle_wide_strokes;

    LineCap  line_cap;
    LineJoin line_join;
    LineJoin line_join_saved;
    Fixed            miter_limit;
    Fixed            radius;

    StrokeBorderRec borders[2];
};

/* ---- forward declarations ---- */
static Error ft_stroker_subpath_start(StrokerRec* stroker,
                                              Angle   start_angle,
                                              Fixed   line_length);
static void         stroker_rewind(StrokerRec* stroker);

/* ---- public API ---- */

Error stroker_new(StrokerRec** astroker)
{
    StrokerRec* stroker = new(std::nothrow) StrokerRec{};
    if (stroker) {
        ft_stroke_border_init(&stroker->borders[0]);
        ft_stroke_border_init(&stroker->borders[1]);
    }

    *astroker = stroker;
    return 0;
}

static void stroker_rewind(StrokerRec* stroker)
{
    if (stroker) {
        ft_stroke_border_reset(&stroker->borders[0]);
        ft_stroke_border_reset(&stroker->borders[1]);
    }
}

void stroker_set(StrokerRec*          stroker,
                         Fixed            radius,
                         LineCap  line_cap,
                         LineJoin line_join,
                         Fixed            miter_limit)
{
    stroker->radius      = radius;
    stroker->line_cap    = line_cap;
    stroker->line_join   = line_join;
    stroker->miter_limit = miter_limit;

    /* ensure miter limit has sensible value */
    if (stroker->miter_limit < 0x10000)
        stroker->miter_limit = 0x10000;

    stroker->line_join_saved = line_join;

    stroker_rewind(stroker);
}

void stroker_done(StrokerRec* stroker)
{
    delete stroker;
}

/* ---- arc at corner or cap ---- */

static Error ft_stroker_arcto(StrokerRec* stroker, Int side)
{
    Angle        total, rotate;
    Fixed        radius = stroker->radius;
    Error        error  = 0;
    StrokeBorder border = &stroker->borders[side];

    rotate = ft_side_to_rotate(side);

    total = angle_diff(stroker->angle_in, stroker->angle_out);
    if (total == angle_pi)
        total = -rotate * 2;

    error = ft_stroke_border_arcto(border, &stroker->center, radius,
                                   stroker->angle_in + rotate, total);
    border->movable = false;
    return error;
}

/* ---- cap at end of opened path ---- */

static Error
ft_stroker_cap(StrokerRec* stroker, Angle angle, Int side)
{
    Error error = 0;

    if (stroker->line_cap == LineCap::Round) {
        stroker->angle_in  = angle;
        stroker->angle_out = angle + angle_pi;
        error = ft_stroker_arcto(stroker, side);
    } else {
        Vector       middle, delta;
        Fixed        radius = stroker->radius;
        StrokeBorder border = &stroker->borders[side];

        vector_from_polar(&middle, radius, angle);
        delta.x = side ?  middle.y : -middle.y;
        delta.y = side ? -middle.x :  middle.x;

        if (stroker->line_cap == LineCap::Square) {
            middle.x += stroker->center.x;
            middle.y += stroker->center.y;
        } else {
            middle.x = stroker->center.x;
            middle.y = stroker->center.y;
        }

        delta.x += middle.x;
        delta.y += middle.y;

        error = ft_stroke_border_lineto(border, &delta, false);
        if (error) goto Exit;

        delta.x = middle.x - delta.x + middle.x;
        delta.y = middle.y - delta.y + middle.y;

        error = ft_stroke_border_lineto(border, &delta, false);
    }

Exit:
    return error;
}

/* ---- inside corner (intersection) ---- */

static Error ft_stroker_inside(StrokerRec* stroker, Int side,
                                       Fixed line_length)
{
    StrokeBorder border = &stroker->borders[side];
    Angle        phi, theta, rotate;
    Fixed        length;
    Vector       sigma = {0, 0};
    Vector       delta;
    Error        error = 0;
    bool                intersect;

    rotate = ft_side_to_rotate(side);
    theta  = angle_diff(stroker->angle_in, stroker->angle_out) / 2;

    if (!border->movable || line_length == 0 ||
        theta > 0x59C000 || theta < -0x59C000)
        intersect = false;
    else {
        Fixed min_length;

        vector_unit(&sigma, theta);
        min_length = ft_pos_abs(mul_div(stroker->radius, sigma.y, sigma.x));

        intersect = min_length &&
                    stroker->line_length >= min_length &&
                    line_length          >= min_length;
    }

    if (!intersect) {
        vector_from_polar(&delta, stroker->radius,
                                 stroker->angle_out + rotate);
        delta.x += stroker->center.x;
        delta.y += stroker->center.y;
        border->movable = false;
    } else {
        phi    = stroker->angle_in + theta + rotate;
        length = div_fix(stroker->radius, sigma.x);

        vector_from_polar(&delta, length, phi);
        delta.x += stroker->center.x;
        delta.y += stroker->center.y;
    }

    error = ft_stroke_border_lineto(border, &delta, false);
    return error;
}

/* ---- outside corner (bevel/miter/round) ---- */

static Error
ft_stroker_outside(StrokerRec* stroker, Int side,
                   Fixed line_length)
{
    StrokeBorder border = &stroker->borders[side];
    Error        error;
    Angle        rotate;

    if (stroker->line_join == LineJoin::Round) {
        error = ft_stroker_arcto(stroker, side);
    } else {
        Fixed  radius = stroker->radius;
        Vector sigma  = {0, 0};
        Angle  theta  = 0, phi = 0;
        bool          bevel, fixed_bevel;

        rotate = ft_side_to_rotate(side);

        bevel = (stroker->line_join == LineJoin::Bevel);
        fixed_bevel = (stroker->line_join != LineJoin::MiterVariable);

        if (!bevel) {
            theta = angle_diff(stroker->angle_in, stroker->angle_out) / 2;

            if (theta == angle_pi2)
                theta = -rotate;

            phi = stroker->angle_in + theta + rotate;

            vector_from_polar(&sigma, stroker->miter_limit, theta);

            if (sigma.x < 0x10000L) {
                if (fixed_bevel || ft_pos_abs(theta) > 57)
                    bevel = true;
            }
        }

        if (bevel) {
            if (fixed_bevel) {
                Vector delta;

                vector_from_polar(&delta, radius,
                                         stroker->angle_out + rotate);
                delta.x += stroker->center.x;
                delta.y += stroker->center.y;

                border->movable = false;
                error = ft_stroke_border_lineto(border, &delta, false);
            } else {
                Vector middle, delta;
                Fixed  coef;

                vector_from_polar(&middle,
                    mul_fix(radius, stroker->miter_limit), phi);

                coef    = div_fix(0x10000L - sigma.x, sigma.y);
                delta.x = mul_fix( middle.y, coef);
                delta.y = mul_fix(-middle.x, coef);

                middle.x += stroker->center.x;
                middle.y += stroker->center.y;
                delta.x  += middle.x;
                delta.y  += middle.y;

                error = ft_stroke_border_lineto(border, &delta, false);
                if (error) goto Exit;

                delta.x = middle.x - delta.x + middle.x;
                delta.y = middle.y - delta.y + middle.y;

                error = ft_stroke_border_lineto(border, &delta, false);
                if (error) goto Exit;

                if (line_length == 0) {
                    vector_from_polar(&delta, radius,
                                             stroker->angle_out + rotate);
                    delta.x += stroker->center.x;
                    delta.y += stroker->center.y;
                    error = ft_stroke_border_lineto(border, &delta, false);
                }
            }
        } else {
            Fixed  length;
            Vector delta;

            length = mul_div(stroker->radius, stroker->miter_limit, sigma.x);

            vector_from_polar(&delta, length, phi);
            delta.x += stroker->center.x;
            delta.y += stroker->center.y;

            error = ft_stroke_border_lineto(border, &delta, false);
            if (error) goto Exit;

            if (line_length == 0) {
                vector_from_polar(&delta, stroker->radius,
                                         stroker->angle_out + rotate);
                delta.x += stroker->center.x;
                delta.y += stroker->center.y;
                error = ft_stroke_border_lineto(border, &delta, false);
            }
        }
    }

Exit:
    return error;
}

static Error ft_stroker_process_corner(StrokerRec* stroker,
                                               Fixed   line_length)
{
    Error error = 0;
    Angle turn;
    Int   inside_side;

    turn = angle_diff(stroker->angle_in, stroker->angle_out);

    if (turn == 0) goto Exit;

    inside_side = 0;
    if (turn < 0) inside_side = 1;

    error = ft_stroker_inside(stroker, inside_side, line_length);
    if (error) goto Exit;

    error = ft_stroker_outside(stroker, 1 - inside_side, line_length);

Exit:
    return error;
}

static Error ft_stroker_subpath_start(StrokerRec* stroker,
                                              Angle   start_angle,
                                              Fixed   line_length)
{
    Vector       delta;
    Vector       point;
    Error        error;
    StrokeBorder border;

    vector_from_polar(&delta, stroker->radius,
                             start_angle + angle_pi2);

    point.x = stroker->center.x + delta.x;
    point.y = stroker->center.y + delta.y;

    border = stroker->borders;
    error  = ft_stroke_border_moveto(border, &point);
    if (error) goto Exit;

    point.x = stroker->center.x - delta.x;
    point.y = stroker->center.y - delta.y;

    border++;
    error = ft_stroke_border_moveto(border, &point);

    stroker->subpath_angle       = start_angle;
    stroker->first_point         = false;
    stroker->subpath_line_length = line_length;

Exit:
    return error;
}

/* ---- LineTo ---- */

Error stroker_line_to(StrokerRec* stroker, Vector* to)
{
    Error        error = 0;
    StrokeBorder border;
    Vector       delta;
    Angle        angle;
    Int          side;
    Fixed        line_length;

    delta.x = to->x - stroker->center.x;
    delta.y = to->y - stroker->center.y;

    if (delta.x == 0 && delta.y == 0) goto Exit;

    line_length = vector_length(&delta);

    angle = ft_atan2(delta.x, delta.y);
    vector_from_polar(&delta, stroker->radius, angle + angle_pi2);

    if (stroker->first_point) {
        error = ft_stroker_subpath_start(stroker, angle, line_length);
        if (error) goto Exit;
    } else {
        stroker->angle_out = angle;
        error = ft_stroker_process_corner(stroker, line_length);
        if (error) goto Exit;
    }

    for (border = stroker->borders, side = 1; side >= 0; side--, border++) {
        Vector point;
        point.x = to->x + delta.x;
        point.y = to->y + delta.y;

        error = ft_stroke_border_lineto(border, &point, true);
        if (error) goto Exit;

        delta.x = -delta.x;
        delta.y = -delta.y;
    }

    stroker->angle_in    = angle;
    stroker->center      = *to;
    stroker->line_length = line_length;

Exit:
    return error;
}

/* ---- ConicTo ---- */

Error stroker_conic_to(StrokerRec* stroker,
                                     Vector* control,
                                     Vector* to)
{
    Error   error = 0;
    Vector  bez_stack[34];
    Vector* arc;
    Vector* limit = bez_stack + 30;
    bool           first_arc = true;

    if (ft_is_small(stroker->center.x - control->x) &&
        ft_is_small(stroker->center.y - control->y) &&
        ft_is_small(control->x - to->x) &&
        ft_is_small(control->y - to->y)) {
        stroker->center = *to;
        goto Exit;
    }

    arc    = bez_stack;
    arc[0] = *to;
    arc[1] = *control;
    arc[2] = stroker->center;

    while (arc >= bez_stack) {
        Angle angle_in, angle_out;

        angle_in = angle_out = stroker->angle_in;

        if (arc < limit &&
            !ft_conic_is_small_enough(arc, &angle_in, &angle_out)) {
            if (stroker->first_point) stroker->angle_in = angle_in;
            ft_conic_split(arc);
            arc += 2;
            continue;
        }

        if (first_arc) {
            first_arc = false;
            if (stroker->first_point)
                error = ft_stroker_subpath_start(stroker, angle_in, 0);
            else {
                stroker->angle_out = angle_in;
                error = ft_stroker_process_corner(stroker, 0);
            }
        } else if (ft_pos_abs(angle_diff(stroker->angle_in, angle_in)) >
                   kSmallConicThreshold / 4) {
            stroker->center    = arc[2];
            stroker->angle_out = angle_in;
            stroker->line_join = LineJoin::Round;
            error = ft_stroker_process_corner(stroker, 0);
            stroker->line_join = stroker->line_join_saved;
        }

        if (error) goto Exit;

        {
            Vector       ctrl, end;
            Angle        theta, phi, rotate, alpha0 = 0;
            Fixed        length;
            StrokeBorder border;
            Int          side;

            theta  = angle_diff(angle_in, angle_out) / 2;
            phi    = angle_in + theta;
            length = div_fix(stroker->radius, ft_cos(theta));

            if (stroker->handle_wide_strokes)
                alpha0 = ft_atan2(arc[0].x - arc[2].x,
                                       arc[0].y - arc[2].y);

            for (border = stroker->borders, side = 0; side <= 1;
                 side++, border++) {
                rotate = ft_side_to_rotate(side);

                vector_from_polar(&ctrl, length, phi + rotate);
                ctrl.x += arc[1].x;
                ctrl.y += arc[1].y;

                vector_from_polar(&end, stroker->radius,
                                         angle_out + rotate);
                end.x += arc[0].x;
                end.y += arc[0].y;

                if (stroker->handle_wide_strokes) {
                    Vector start;
                    Angle  alpha1;

                    start = border->points.back();
                    alpha1 = ft_atan2(end.x - start.x, end.y - start.y);

                    if (ft_pos_abs(angle_diff(alpha0, alpha1)) >
                        angle_pi / 2) {
                        Angle  beta, gamma;
                        Vector bvec, delta;
                        Fixed  blen, sinA, sinB, alen;

                        beta  = ft_atan2(arc[2].x - start.x,
                                              arc[2].y - start.y);
                        gamma = ft_atan2(arc[0].x - end.x,
                                              arc[0].y - end.y);

                        bvec.x = end.x - start.x;
                        bvec.y = end.y - start.y;
                        blen   = vector_length(&bvec);

                        sinA = ft_pos_abs(ft_sin(alpha1 - gamma));
                        sinB = ft_pos_abs(ft_sin(beta - gamma));
                        alen = mul_div(blen, sinA, sinB);

                        vector_from_polar(&delta, alen, beta);
                        delta.x += start.x;
                        delta.y += start.y;

                        border->movable = false;
                        error = ft_stroke_border_lineto(border, &delta, false);
                        if (error) goto Exit;
                        error = ft_stroke_border_lineto(border, &end, false);
                        if (error) goto Exit;
                        error = ft_stroke_border_conicto(border, &ctrl, &start);
                        if (error) goto Exit;
                        error = ft_stroke_border_lineto(border, &end, false);
                        if (error) goto Exit;
                        continue;
                    }
                }

                error = ft_stroke_border_conicto(border, &ctrl, &end);
                if (error) goto Exit;
            }
        }

        arc -= 2;
        stroker->angle_in = angle_out;
    }

    stroker->center      = *to;
    stroker->line_length = 0;

Exit:
    return error;
}

/* ---- CubicTo ---- */

Error stroker_cubic_to(StrokerRec* stroker,
                                     Vector* control1,
                                     Vector* control2,
                                     Vector* to)
{
    Error   error = 0;
    Vector  bez_stack[37];
    Vector* arc;
    Vector* limit = bez_stack + 32;
    bool           first_arc = true;

    if (ft_is_small(stroker->center.x - control1->x) &&
        ft_is_small(stroker->center.y - control1->y) &&
        ft_is_small(control1->x - control2->x) &&
        ft_is_small(control1->y - control2->y) &&
        ft_is_small(control2->x - to->x) &&
        ft_is_small(control2->y - to->y)) {
        stroker->center = *to;
        goto Exit;
    }

    arc    = bez_stack;
    arc[0] = *to;
    arc[1] = *control2;
    arc[2] = *control1;
    arc[3] = stroker->center;

    while (arc >= bez_stack) {
        Angle angle_in, angle_mid, angle_out;

        angle_in = angle_out = angle_mid = stroker->angle_in;

        if (arc < limit &&
            !ft_cubic_is_small_enough(arc, &angle_in, &angle_mid, &angle_out)) {
            if (stroker->first_point) stroker->angle_in = angle_in;
            ft_cubic_split(arc);
            arc += 3;
            continue;
        }

        if (first_arc) {
            first_arc = false;
            if (stroker->first_point)
                error = ft_stroker_subpath_start(stroker, angle_in, 0);
            else {
                stroker->angle_out = angle_in;
                error = ft_stroker_process_corner(stroker, 0);
            }
        } else if (ft_pos_abs(angle_diff(stroker->angle_in, angle_in)) >
                   kSmallCubicThreshold / 4) {
            stroker->center    = arc[3];
            stroker->angle_out = angle_in;
            stroker->line_join = LineJoin::Round;
            error = ft_stroker_process_corner(stroker, 0);
            stroker->line_join = stroker->line_join_saved;
        }

        if (error) goto Exit;

        {
            Vector       ctrl1, ctrl2, end;
            Angle        theta1, phi1, theta2, phi2, rotate, alpha0 = 0;
            Fixed        length1, length2;
            StrokeBorder border;
            Int          side;

            theta1  = angle_diff(angle_in, angle_mid) / 2;
            theta2  = angle_diff(angle_mid, angle_out) / 2;
            phi1    = ft_angle_mean(angle_in, angle_mid);
            phi2    = ft_angle_mean(angle_mid, angle_out);
            length1 = div_fix(stroker->radius, ft_cos(theta1));
            length2 = div_fix(stroker->radius, ft_cos(theta2));

            if (stroker->handle_wide_strokes)
                alpha0 = ft_atan2(arc[0].x - arc[3].x,
                                       arc[0].y - arc[3].y);

            for (border = stroker->borders, side = 0; side <= 1;
                 side++, border++) {
                rotate = ft_side_to_rotate(side);

                vector_from_polar(&ctrl1, length1, phi1 + rotate);
                ctrl1.x += arc[2].x;
                ctrl1.y += arc[2].y;

                vector_from_polar(&ctrl2, length2, phi2 + rotate);
                ctrl2.x += arc[1].x;
                ctrl2.y += arc[1].y;

                vector_from_polar(&end, stroker->radius,
                                         angle_out + rotate);
                end.x += arc[0].x;
                end.y += arc[0].y;

                if (stroker->handle_wide_strokes) {
                    Vector start;
                    Angle  alpha1;

                    start  = border->points.back();
                    alpha1 = ft_atan2(end.x - start.x, end.y - start.y);

                    if (ft_pos_abs(angle_diff(alpha0, alpha1)) >
                        angle_pi / 2) {
                        Angle  beta, gamma;
                        Vector bvec, delta;
                        Fixed  blen, sinA, sinB, alen;

                        beta  = ft_atan2(arc[3].x - start.x,
                                              arc[3].y - start.y);
                        gamma = ft_atan2(arc[0].x - end.x,
                                              arc[0].y - end.y);

                        bvec.x = end.x - start.x;
                        bvec.y = end.y - start.y;
                        blen   = vector_length(&bvec);

                        sinA = ft_pos_abs(ft_sin(alpha1 - gamma));
                        sinB = ft_pos_abs(ft_sin(beta - gamma));
                        alen = mul_div(blen, sinA, sinB);

                        vector_from_polar(&delta, alen, beta);
                        delta.x += start.x;
                        delta.y += start.y;

                        border->movable = false;
                        error = ft_stroke_border_lineto(border, &delta, false);
                        if (error) goto Exit;
                        error = ft_stroke_border_lineto(border, &end, false);
                        if (error) goto Exit;
                        error = ft_stroke_border_cubicto(border, &ctrl2, &ctrl1,
                                                         &start);
                        if (error) goto Exit;
                        error = ft_stroke_border_lineto(border, &end, false);
                        if (error) goto Exit;
                        continue;
                    }
                }

                error = ft_stroke_border_cubicto(border, &ctrl1, &ctrl2, &end);
                if (error) goto Exit;
            }
        }

        arc -= 3;
        stroker->angle_in = angle_out;
    }

    stroker->center      = *to;
    stroker->line_length = 0;

Exit:
    return error;
}

/* ---- BeginSubPath ---- */

Error stroker_begin_subpath(StrokerRec* stroker,
                                          Vector* to,
                                          Bool    open)
{
    stroker->first_point  = true;
    stroker->center       = *to;
    stroker->subpath_open = open;

    stroker->handle_wide_strokes =
        stroker->line_join != LineJoin::Round ||
        (stroker->subpath_open &&
         stroker->line_cap == LineCap::Butt);

    stroker->subpath_start = *to;
    stroker->angle_in      = 0;

    return 0;
}

/* ---- add reverse left border ---- */

static Error ft_stroker_add_reverse_left(StrokerRec* stroker,
                                                 bool open)
{
    StrokeBorder right = &stroker->borders[0];
    StrokeBorder left  = &stroker->borders[1];
    Error        error = 0;

    assert(left->start >= 0);

    int new_points = static_cast<int>(left->points.size()) - left->start;
    if (new_points > 0) {

        /* reverse copy from left to right */
        auto src = left->points.size();
        auto start_idx = static_cast<size_t>(left->start);
        while (src > start_idx) {
            --src;
            Byte tag = left->tags[src];

            if (open) {
                tag &= static_cast<Byte>(~kStrokeTagBeginEnd);
            } else {
                Byte ttag = tag & kStrokeTagBeginEnd;
                if (ttag == kStrokeTagBegin ||
                    ttag == kStrokeTagEnd)
                    tag ^= kStrokeTagBeginEnd;
            }

            right->points.push_back(left->points[src]);
            right->tags.push_back(tag);
        }

        left->points.resize(static_cast<size_t>(left->start));
        left->tags.resize(static_cast<size_t>(left->start));

        right->movable = false;
        left->movable  = false;
    }

    return error;
}

/* ---- EndSubPath ---- */

Error stroker_end_subpath(StrokerRec* stroker)
{
    Error error = 0;

    if (stroker->subpath_open) {
        StrokeBorder right = &stroker->borders[0];

        error = ft_stroker_cap(stroker, stroker->angle_in, 0);
        if (error) goto Exit;

        error = ft_stroker_add_reverse_left(stroker, true);
        if (error) goto Exit;

        stroker->center = stroker->subpath_start;
        error = ft_stroker_cap(stroker,
                               stroker->subpath_angle + angle_pi, 0);
        if (error) goto Exit;

        ft_stroke_border_close(right, false);
    } else {
        Angle turn;
        Int   inside_side;

        if (stroker->center.x != stroker->subpath_start.x ||
            stroker->center.y != stroker->subpath_start.y) {
            error = stroker_line_to(stroker, &stroker->subpath_start);
            if (error) goto Exit;
        }

        stroker->angle_out = stroker->subpath_angle;
        turn = angle_diff(stroker->angle_in, stroker->angle_out);

        if (turn != 0) {
            inside_side = 0;
            if (turn < 0) inside_side = 1;

            error = ft_stroker_inside(stroker, inside_side,
                                      stroker->subpath_line_length);
            if (error) goto Exit;

            error = ft_stroker_outside(stroker, 1 - inside_side,
                                       stroker->subpath_line_length);
            if (error) goto Exit;
        }

        ft_stroke_border_close(&stroker->borders[0], false);
        ft_stroke_border_close(&stroker->borders[1], true);
    }

Exit:
    return error;
}

/* ---- GetBorderCounts ---- */

Error stroker_get_border_counts(StrokerRec*       stroker,
                                             StrokerBorder border,
                                             UInt*         anum_points,
                                             UInt*         anum_contours)
{
    UInt  num_points = 0, num_contours = 0;
    Error error;

    if (!stroker || static_cast<int>(border) > 1) {
        error = -1;
        goto Exit;
    }

    error = ft_stroke_border_get_counts(&stroker->borders[static_cast<int>(border)],
                                        &num_points, &num_contours);
Exit:
    if (anum_points)   *anum_points   = num_points;
    if (anum_contours) *anum_contours = num_contours;
    return error;
}

/* ---- GetCounts ---- */

Error stroker_get_counts(StrokerRec* stroker,
                                       UInt*  anum_points,
                                       UInt*  anum_contours)
{
    UInt  count1, count2, num_points   = 0;
    UInt  count3, count4, num_contours = 0;
    Error error;

    error = ft_stroke_border_get_counts(&stroker->borders[0], &count1, &count2);
    if (error) goto Exit;

    error = ft_stroke_border_get_counts(&stroker->borders[1], &count3, &count4);
    if (error) goto Exit;

    num_points   = count1 + count3;
    num_contours = count2 + count4;

Exit:
    *anum_points   = num_points;
    *anum_contours = num_contours;
    return error;
}

/* ---- ExportBorder ---- */

void stroker_export_border(StrokerRec*       stroker,
                                 StrokerBorder  border,
                                 Outline*       outline)
{
    if (border == StrokerBorder::Left ||
        border == StrokerBorder::Right) {
        StrokeBorder sborder = &stroker->borders[static_cast<int>(border)];
        if (sborder->valid)
            ft_stroke_border_export(sborder, outline);
    }
}

/* ---- Export ---- */

void stroker_export(StrokerRec* stroker, Outline* outline)
{
    stroker_export_border(stroker, StrokerBorder::Left, outline);
    stroker_export_border(stroker, StrokerBorder::Right, outline);
}

/* ---- ParseOutline ---- */

Error stroker_parse_outline(StrokerRec*         stroker,
                                          const Outline*  outline)
{
    Vector  v_last;
    Vector  v_control;
    Vector  v_start;

    Vector* point;
    Vector* limit;
    char*          tags;

    Error   error;

    Int     n;
    UInt    first;
    CurveTag tag;

    if (!outline || !stroker) return -1;

    stroker_rewind(stroker);

    first = 0;

    for (n = 0; n < outline->n_contours; n++) {
        UInt last = static_cast<UInt>(outline->contours[n]);

        limit = outline->points + last;

        if (last <= first) {
            first = last + 1;
            continue;
        }

        v_start   = outline->points[first];
        v_last    = outline->points[last];
        v_control = v_start;

        point = outline->points + first;
        tags  = outline->tags   + first;
        tag   = curve_tag(tags[0]);

        if (tag == CurveTag::Cubic) goto Invalid_Outline;

        if (tag == CurveTag::Conic) {
            if (curve_tag(outline->tags[last]) == CurveTag::On) {
                v_start = v_last;
                limit--;
            } else {
                v_start.x = (v_start.x + v_last.x) / 2;
                v_start.y = (v_start.y + v_last.y) / 2;
            }
            point--;
            tags--;
        }

        error = stroker_begin_subpath(stroker, &v_start,
                                             outline->contours_flag[n]);
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
                error = stroker_line_to(stroker, &vec);
                if (error) goto Exit;
                continue;
            }

            case CurveTag::Conic:
                v_control.x = point->x;
                v_control.y = point->y;

            Do_Conic:
                if (point < limit) {
                    Vector vec;
                    Vector v_middle;

                    point++;
                    tags++;
                    tag = curve_tag(tags[0]);
                    vec = point[0];

                    if (tag == CurveTag::On) {
                        error = stroker_conic_to(stroker, &v_control, &vec);
                        if (error) goto Exit;
                        continue;
                    }

                    if (tag != CurveTag::Conic)
                        goto Invalid_Outline;

                    v_middle.x = (v_control.x + vec.x) / 2;
                    v_middle.y = (v_control.y + vec.y) / 2;

                    error = stroker_conic_to(stroker, &v_control, &v_middle);
                    if (error) goto Exit;

                    v_control = vec;
                    goto Do_Conic;
                }

                error = stroker_conic_to(stroker, &v_control, &v_start);
                goto Close;

            default: {
                Vector vec1, vec2;

                if (point + 1 > limit ||
                    curve_tag(tags[1]) != CurveTag::Cubic)
                    goto Invalid_Outline;

                point += 2;
                tags  += 2;

                vec1 = point[-2];
                vec2 = point[-1];

                if (point <= limit) {
                    Vector vec;
                    vec = point[0];
                    error = stroker_cubic_to(stroker, &vec1, &vec2, &vec);
                    if (error) goto Exit;
                    continue;
                }

                error = stroker_cubic_to(stroker, &vec1, &vec2, &v_start);
                goto Close;
            }
            }
        }

    Close:
        if (error) goto Exit;

        if (stroker->first_point) {
            stroker->subpath_open = true;
            error = ft_stroker_subpath_start(stroker, 0, 0);
            if (error) goto Exit;
        }

        error = stroker_end_subpath(stroker);
        if (error) goto Exit;

        first = last + 1;
    }

    return 0;

Exit:
    return error;

Invalid_Outline:
    return -2;
}

// -- Stroker RAII class implementation --

Stroker::Stroker()
{
    StrokerRec* raw = new(std::nothrow) StrokerRec{};
    if (raw) {
        ft_stroke_border_init(&raw->borders[0]);
        ft_stroke_border_init(&raw->borders[1]);
    }
    impl_.reset(raw);
}

Stroker::~Stroker() = default;

Stroker::Stroker(Stroker&& other) noexcept = default;
Stroker& Stroker::operator=(Stroker&& other) noexcept = default;

void Stroker::set(Fixed radius, LineCap cap, LineJoin join, Fixed miter_limit)
{
    stroker_set(impl_.get(), radius, cap, join, miter_limit);
}

Error Stroker::parse_outline(const Outline* outline)
{
    return stroker_parse_outline(impl_.get(), outline);
}

Error Stroker::get_counts(UInt* num_points, UInt* num_contours)
{
    return stroker_get_counts(impl_.get(), num_points, num_contours);
}

void Stroker::export_outline(Outline* outline)
{
    stroker_export(impl_.get(), outline);
}

} // namespace kvg::ft
