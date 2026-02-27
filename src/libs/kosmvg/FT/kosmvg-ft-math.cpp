/*
 * KosmVG FreeType trigonometric functions (C++20)
 *
 * Based on FreeType fttrigon.c
 * Copyright 2001-2005, 2012-2013
 * David Turner, Robert Wilhelm, Werner Lemberg.
 * FreeType project license, FTL.TXT.
 */

#include "kosmvg-ft.hpp"

#include <bit>

namespace kvg::ft {

// -- bit-scan helper --

static inline int ft_msb(unsigned int x)
{
    return 31 - std::countl_zero(x);
}

// -- rounding helpers --

static constexpr Fixed pad_floor(Fixed x, Fixed n)
{
    return x & ~(n - 1);
}

static constexpr Fixed pad_round(Fixed x, Fixed n)
{
    return pad_floor(x + (n / 2), n);
}

// -- sign transfer: make x positive, flip s if x was negative --

static inline void move_sign(Long& x, Int& s)
{
    if (x < 0) {
        x = -x;
        s = -s;
    }
}

// -- public fixed-point arithmetic --

Long mul_fix(Long a, Long b)
{
    Int s = 1;
    move_sign(a, s);
    move_sign(b, s);

    Long c = static_cast<Long>(
        (static_cast<Int64>(a) * b + 0x8000L) >> 16);

    return (s > 0) ? c : -c;
}

Long mul_div(Long a, Long b, Long c)
{
    Int s = 1;
    move_sign(a, s);
    move_sign(b, s);
    move_sign(c, s);

    Long d = static_cast<Long>(
        c > 0 ? (static_cast<Int64>(a) * b + (c >> 1)) / c
              : 0x7FFFFFFFL);

    return (s > 0) ? d : -d;
}

Long div_fix(Long a, Long b)
{
    Int s = 1;
    move_sign(a, s);
    move_sign(b, s);

    Long q = static_cast<Long>(
        b > 0 ? ((static_cast<UInt64>(a) << 16) + static_cast<UInt64>(b >> 1))
                / static_cast<UInt64>(b)
              : 0x7FFFFFFFL);

    return (s < 0) ? -q : q;
}

// -- CORDIC-based trigonometry --
//
// Angles are 16.16 fixed-point degrees. Angular resolution is 2^-16
// degrees. Only vectors longer than 2^16*180/pi (~22 bits) on a
// discrete Cartesian grid can match this precision.

static constexpr uint32_t trig_scale = 0xDBD95B16UL;  // 0.858785336480436 * 2^32
static constexpr int trig_safe_msb = 29;
static constexpr int trig_max_iters = 23;

static constexpr Fixed trig_arctan_table[] = {
    1740967L, 919879L, 466945L, 234379L, 117304L, 58666L, 29335L, 14668L,
    7334L,    3667L,   1833L,   917L,    458L,    229L,   115L,   57L,
    29L,      14L,     7L,      4L,      2L,      1L
};

static Fixed trig_downscale(Fixed val)
{
    Fixed s = val;
    val = ft_abs(val);

    Int64 v = static_cast<Int64>(val) *
              static_cast<Int64>(trig_scale) +
              static_cast<Int64>(0x100000000LL);
    val = static_cast<Fixed>(v >> 32);

    return (s >= 0) ? val : -val;
}

// undefined and never called for zero vector
static Int trig_prenorm(Vector* vec)
{
    Pos x = vec->x;
    Pos y = vec->y;

    int shift = ft_msb(static_cast<unsigned int>(ft_abs(x) | ft_abs(y)));

    if (shift <= trig_safe_msb) {
        shift = trig_safe_msb - shift;
        vec->x = static_cast<Pos>(static_cast<ULong>(x) << shift);
        vec->y = static_cast<Pos>(static_cast<ULong>(y) << shift);
    } else {
        shift -= trig_safe_msb;
        vec->x = x >> shift;
        vec->y = y >> shift;
        shift = -shift;
    }

    return shift;
}

static void trig_pseudo_rotate(Vector* vec, Angle theta)
{
    Fixed x = vec->x;
    Fixed y = vec->y;
    Fixed xtemp;
    const Fixed* arctanptr;

    while (theta < -angle_pi4) {
        xtemp = y;
        y = -x;
        x = xtemp;
        theta += angle_pi2;
    }

    while (theta > angle_pi4) {
        xtemp = -y;
        y = x;
        x = xtemp;
        theta -= angle_pi2;
    }

    arctanptr = trig_arctan_table;

    for (int i = 1, b = 1; i < trig_max_iters; b <<= 1, i++) {
        Fixed v1 = ((y + b) >> i);
        Fixed v2 = ((x + b) >> i);
        if (theta < 0) {
            xtemp = x + v1;
            y = y - v2;
            x = xtemp;
            theta += *arctanptr++;
        } else {
            xtemp = x - v1;
            y = y + v2;
            x = xtemp;
            theta -= *arctanptr++;
        }
    }

    vec->x = x;
    vec->y = y;
}

static void trig_pseudo_polarize(Vector* vec)
{
    Angle  theta;
    Fixed  x = vec->x;
    Fixed  y = vec->y;
    Fixed  xtemp;
    const Fixed* arctanptr;

    if (y > x) {
        if (y > -x) {
            theta = angle_pi2;
            xtemp = y;
            y = -x;
            x = xtemp;
        } else {
            theta = y > 0 ? angle_pi : -angle_pi;
            x = -x;
            y = -y;
        }
    } else {
        if (y < -x) {
            theta = -angle_pi2;
            xtemp = -y;
            y = x;
            x = xtemp;
        } else {
            theta = 0;
        }
    }

    arctanptr = trig_arctan_table;

    for (int i = 1, b = 1; i < trig_max_iters; b <<= 1, i++) {
        Fixed v1 = ((y + b) >> i);
        Fixed v2 = ((x + b) >> i);
        if (y > 0) {
            xtemp = x + v1;
            y = y - v2;
            x = xtemp;
            theta += *arctanptr++;
        } else {
            xtemp = x - v1;
            y = y + v2;
            x = xtemp;
            theta -= *arctanptr++;
        }
    }

    if (theta >= 0)
        theta = pad_round(theta, 32);
    else
        theta = -pad_round(-theta, 32);

    vec->x = x;
    vec->y = theta;
}

// -- public trigonometric functions --

Fixed ft_cos(Angle angle)
{
    Vector v;
    v.x = trig_scale >> 8;
    v.y = 0;
    trig_pseudo_rotate(&v, angle);
    return static_cast<Fixed>((v.x + 0x80L) >> 8);
}

Fixed ft_sin(Angle angle)
{
    return ft_cos(angle_pi2 - angle);
}

Fixed ft_tan(Angle angle)
{
    Vector v;
    v.x = trig_scale >> 8;
    v.y = 0;
    trig_pseudo_rotate(&v, angle);
    return div_fix(v.y, v.x);
}

Angle ft_atan2(Fixed dx, Fixed dy)
{
    if (dx == 0 && dy == 0) return 0;

    Vector v;
    v.x = dx;
    v.y = dy;
    trig_prenorm(&v);
    trig_pseudo_polarize(&v);
    return v.y;
}

Angle angle_diff(Angle angle1, Angle angle2)
{
    Angle delta = angle2 - angle1;

    while (delta <= -angle_pi)
        delta += angle_2pi;
    while (delta > angle_pi)
        delta -= angle_2pi;

    return delta;
}

// -- public vector operations --

void vector_unit(Vector* vec, Angle angle)
{
    vec->x = trig_scale >> 8;
    vec->y = 0;
    trig_pseudo_rotate(vec, angle);
    vec->x = static_cast<Pos>((vec->x + 0x80L) >> 8);
    vec->y = static_cast<Pos>((vec->y + 0x80L) >> 8);
}

void vector_rotate(Vector* vec, Angle angle)
{
    Vector v = *vec;
    if (v.x == 0 && v.y == 0) return;

    Int shift = trig_prenorm(&v);
    trig_pseudo_rotate(&v, angle);
    v.x = trig_downscale(v.x);
    v.y = trig_downscale(v.y);

    if (shift > 0) {
        Int32 half = static_cast<Int32>(1L << (shift - 1));
        vec->x = (v.x + half - (v.x < 0)) >> shift;
        vec->y = (v.y + half - (v.y < 0)) >> shift;
    } else {
        shift = -shift;
        vec->x = static_cast<Pos>(static_cast<ULong>(v.x) << shift);
        vec->y = static_cast<Pos>(static_cast<ULong>(v.y) << shift);
    }
}

Fixed vector_length(Vector* vec)
{
    Vector v = *vec;

    if (v.x == 0) return ft_abs(v.y);
    if (v.y == 0) return ft_abs(v.x);

    Int shift = trig_prenorm(&v);
    trig_pseudo_polarize(&v);
    v.x = trig_downscale(v.x);

    if (shift > 0)
        return (v.x + (1 << (shift - 1))) >> shift;

    return static_cast<Fixed>(static_cast<UInt32>(v.x) << -shift);
}

void vector_polarize(Vector* vec, Fixed* length, Angle* angle)
{
    Vector v = *vec;
    if (v.x == 0 && v.y == 0) return;

    Int shift = trig_prenorm(&v);
    trig_pseudo_polarize(&v);
    v.x = trig_downscale(v.x);

    *length = (shift >= 0)
        ? (v.x >> shift)
        : static_cast<Fixed>(static_cast<UInt32>(v.x) << -shift);
    *angle = v.y;
}

void vector_from_polar(Vector* vec, Fixed length, Angle angle)
{
    vec->x = length;
    vec->y = 0;
    vector_rotate(vec, angle);
}

} // namespace kvg::ft
