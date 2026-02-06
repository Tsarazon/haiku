#ifndef PLUTOVG_FT_STROKER_HPP
#define PLUTOVG_FT_STROKER_HPP

#include "plutovg-ft-raster.hpp"

namespace plutovg::ft {

struct StrokerRec;
using Stroker = StrokerRec*;

enum class StrokerLineJoin : int {
    Round         = 0,
    Bevel         = 1,
    MiterVariable = 2,
    Miter         = MiterVariable,
    MiterFixed    = 3
};

enum class StrokerLineCap : int {
    Butt   = 0,
    Round  = 1,
    Square = 2
};

enum class StrokerBorder : int {
    Left  = 0,
    Right = 1
};

Error stroker_new(Stroker& stroker);

void stroker_set(Stroker stroker, Fixed radius,
    StrokerLineCap line_cap, StrokerLineJoin line_join,
    Fixed miter_limit);

Error stroker_parse_outline(Stroker stroker, const Outline& outline);

Error stroker_get_counts(Stroker stroker, UInt& num_points, UInt& num_contours);

void stroker_export(Stroker stroker, Outline& outline);

void stroker_done(Stroker stroker);

} // namespace plutovg::ft

#endif // PLUTOVG_FT_STROKER_HPP
