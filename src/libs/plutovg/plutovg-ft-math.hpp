#ifndef PLUTOVG_FT_MATH_HPP
#define PLUTOVG_FT_MATH_HPP

#include "plutovg-ft-types.hpp"

#include <algorithm>
#include <cstdlib>

namespace plutovg::ft {

template<typename T>
constexpr T ft_min(T a, T b) { return std::min(a, b); }

template<typename T>
constexpr T ft_max(T a, T b) { return std::max(a, b); }

template<typename T>
constexpr T ft_abs(T a) { return a < 0 ? -a : a; }

inline Pos ft_hypot(Pos x, Pos y) {
    x = ft_abs(x);
    y = ft_abs(y);
    return x > y ? x + (3 * y >> 3) : y + (3 * x >> 3);
}

Long mul_fix(Long a, Long b);
Long mul_div(Long a, Long b, Long c);
Long div_fix(Long a, Long b);

using Angle = Fixed; // 16.16 fixed-point degrees

inline constexpr Angle angle_pi  = 180L << 16;
inline constexpr Angle angle_2pi = angle_pi * 2;
inline constexpr Angle angle_pi2 = angle_pi / 2;
inline constexpr Angle angle_pi4 = angle_pi / 4;

Fixed sin(Angle angle);
Fixed cos(Angle angle);
Fixed tan(Angle angle);
Angle atan2(Fixed x, Fixed y);
Angle angle_diff(Angle angle1, Angle angle2);

void vector_unit(Vector& vec, Angle angle);
void vector_rotate(Vector& vec, Angle angle);
Fixed vector_length(Vector& vec);
void vector_polarize(Vector& vec, Fixed& length, Angle& angle);
void vector_from_polar(Vector& vec, Fixed length, Angle angle);

} // namespace plutovg::ft

#endif // PLUTOVG_FT_MATH_HPP
