// kcgotf_outlines.cpp - TrueType and CFF outline extraction
// C++20, userspace

#include "kcgotf.hpp"
#include "kcgotf_internal.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace kcg::otf {

// CFF helpers (internal namespace impl)

namespace cff {

CFFBuffer get_index(CFFBuffer& b) {
    int start = b.cursor;
    int count = buf_get16(b);
    if (count) {
        int offsize = buf_get8(b);
        if (offsize < 1 || offsize > 4)
            return buf_range(b, start, b.cursor - start);
        buf_skip(b, offsize * count);
        buf_skip(b, static_cast<int>(buf_get(b, offsize)) - 1);
    }
    return buf_range(b, start, b.cursor - start);
}

int index_count(CFFBuffer& b) {
    buf_seek(b, 0);
    return buf_get16(b);
}

CFFBuffer index_get(CFFBuffer b, int i) {
    buf_seek(b, 0);
    int count = buf_get16(b);
    int offsize = buf_get8(b);
    if (i < 0 || i >= count) return {};
    if (offsize < 1 || offsize > 4) return {};
    buf_skip(b, i * offsize);
    int start = static_cast<int>(buf_get(b, offsize));
    int end = static_cast<int>(buf_get(b, offsize));
    return buf_range(b, 2 + (count + 1) * offsize + start, end - start);
}

uint32_t dict_int(CFFBuffer& b) {
    int b0 = buf_get8(b);
    if (b0 >= 32 && b0 <= 246)       return b0 - 139;
    else if (b0 >= 247 && b0 <= 250) return (b0 - 247) * 256 + buf_get8(b) + 108;
    else if (b0 >= 251 && b0 <= 254) return -(b0 - 251) * 256 - buf_get8(b) - 108;
    else if (b0 == 28)               return buf_get16(b);
    else if (b0 == 29)               return buf_get32(b);
    return 0;
}

void dict_skip_operand(CFFBuffer& b) {
    int b0 = buf_peek8(b);
    if (b0 < 28) { buf_skip(b, 1); return; }
    if (b0 == 30) {
        buf_skip(b, 1);
        while (b.cursor < b.size) {
            int v = buf_get8(b);
            if ((v & 0xF) == 0xF || (v >> 4) == 0xF) break;
        }
    } else {
        dict_int(b);
    }
}

CFFBuffer dict_get(CFFBuffer& b, int key) {
    buf_seek(b, 0);
    while (b.cursor < b.size) {
        int start = b.cursor;
        while (buf_peek8(b) >= 28)
            dict_skip_operand(b);
        int end = b.cursor;
        int op = buf_get8(b);
        if (op == 12) op = buf_get8(b) | 0x100;
        if (op == key) return buf_range(b, start, end - start);
    }
    return buf_range(b, 0, 0);
}

void dict_get_ints(CFFBuffer& b, int key, int outcount, uint32_t* out) {
    CFFBuffer operands = dict_get(b, key);
    for (int i = 0; i < outcount && operands.cursor < operands.size; i++)
        out[i] = dict_int(operands);
}

CFFBuffer get_subrs(const CFFBuffer& cff_buf, CFFBuffer fontdict) {
    uint32_t subrsoff = 0, private_loc[2] = {0, 0};
    dict_get_ints(fontdict, 18, 2, private_loc);
    if (!private_loc[1] || !private_loc[0]) return {};
    CFFBuffer pdict = buf_range(cff_buf, private_loc[1], private_loc[0]);
    dict_get_ints(pdict, 19, 1, &subrsoff);
    if (!subrsoff) return {};
    CFFBuffer cff_copy = cff_buf;
    buf_seek(cff_copy, private_loc[1] + subrsoff);
    return get_index(cff_copy);
}

} // namespace cff

// CFF initialization (called from open() in core)

bool FontData::init_cff() {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint32_t cff_off = find_table("CFF ");
    if (!cff_off) return false;

    m_cff.cff = cff::make_buf(d + cff_off,
        std::min<int>(static_cast<int>(dsz - cff_off), 512 * 1024 * 1024));

    CFFBuffer b = m_cff.cff;
    cff::buf_skip(b, 2);
    cff::buf_seek(b, cff::buf_get8(b));

    cff::get_index(b);
    CFFBuffer topdictidx = cff::get_index(b);
    CFFBuffer topdict = cff::index_get(topdictidx, 0);
    cff::get_index(b);
    m_cff.global_subrs = cff::get_index(b);

    uint32_t charstrings = 0, cstype = 2;
    uint32_t fdarrayoff = 0, fdselectoff = 0;
    cff::dict_get_ints(topdict, 17, 1, &charstrings);
    cff::dict_get_ints(topdict, 0x100 | 6, 1, &cstype);
    cff::dict_get_ints(topdict, 0x100 | 36, 1, &fdarrayoff);
    cff::dict_get_ints(topdict, 0x100 | 37, 1, &fdselectoff);
    m_cff.local_subrs = cff::get_subrs(m_cff.cff, topdict);

    if (cstype != 2 || charstrings == 0) return false;

    if (fdarrayoff) {
        if (!fdselectoff) return false;
        CFFBuffer b2 = m_cff.cff;
        cff::buf_seek(b2, fdarrayoff);
        m_cff.font_dicts = cff::get_index(b2);
        m_cff.fd_select = cff::buf_range(m_cff.cff, fdselectoff,
                                          m_cff.cff.size - fdselectoff);
    }

    CFFBuffer b3 = m_cff.cff;
    cff::buf_seek(b3, charstrings);
    m_cff.char_strings = cff::get_index(b3);

    return true;
}

// Glyph location and box

int FontData::locate_glyph(uint16_t glyph) const {
    if (m_is_cff || glyph >= m_num_glyphs) return -1;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    uint32_t loca = m_tables.loca;
    uint32_t glyf = m_tables.glyf;

    int g1, g2;
    if (m_loca_short) {
        size_t off = loca + static_cast<size_t>(glyph) * 2;
        if (off + 4 > dsz) return -1;
        g1 = glyf + read_u16(d + off) * 2;
        g2 = glyf + read_u16(d + off + 2) * 2;
    } else {
        size_t off = loca + static_cast<size_t>(glyph) * 4;
        if (off + 8 > dsz) return -1;
        g1 = glyf + read_u32(d + off);
        g2 = glyf + read_u32(d + off + 4);
    }
    if (g1 < 0 || static_cast<size_t>(g1) + 10 > dsz) return -1;
    return g1 == g2 ? -1 : g1;
}

bool FontData::glyph_box(uint16_t glyph, BBox& box) const {
    if (m_is_cff) return cff_glyph_box(glyph, box);
    int g = locate_glyph(glyph);
    if (g < 0) return false;
    auto* d = m_data.data();
    if (static_cast<size_t>(g) + 10 > m_data.size()) return false;
    box = {read_i16(d + g + 2), read_i16(d + g + 4),
           read_i16(d + g + 6), read_i16(d + g + 8)};
    return true;
}

bool FontData::is_glyph_empty(uint16_t glyph) const {
    if (m_is_cff) return cff_is_empty(glyph);
    int g = locate_glyph(glyph);
    if (g < 0) return true;
    if (static_cast<size_t>(g) + 2 > m_data.size()) return true;
    return read_i16(m_data.data() + g) == 0;
}

// Public entry

int FontData::glyph_outline(uint16_t glyph, const GlyphSink& sink,
                              const VariationInstance* var) const {
    if (m_is_cff) {
        // CFF2 variation support not implemented; CFF1 ignores variations.
        return parse_cff_glyph(glyph, sink);
    }
    return parse_tt_glyph(glyph, sink, var, 0);
}

// gvar - TupleVariationStore deltas applied to TT outline points.
// Helpers in anonymous namespace; relies on detail::ivs_delta (layout.cpp).

namespace {

// Read a packed point-number list from `p` (advances `p`); returns number of points.
// `out_indices` is filled with point indices. If ALL_POINTS, out_indices remains
// empty and `all_points` is set true.
struct PackedPoints {
    bool all_points = false;
    std::vector<uint16_t> indices;
};

bool read_packed_points(const uint8_t*& p, const uint8_t* end, PackedPoints& out) {
    out.all_points = false;
    out.indices.clear();
    if (p >= end) return false;

    uint16_t count = *p++;
    if (count & 0x80) {
        if (p >= end) return false;
        count = ((count & 0x7F) << 8) | (*p++);
    }
    if (count == 0) {
        out.all_points = true;
        return true;
    }

    out.indices.reserve(count);
    int position = 0;
    int remaining = count;
    while (remaining > 0 && p < end) {
        uint8_t control = *p++;
        int run_count = (control & 0x7F) + 1;
        if (run_count > remaining) run_count = remaining;
        bool words = (control & 0x80) != 0;
        for (int i = 0; i < run_count; ++i) {
            int delta;
            if (words) {
                if (p + 2 > end) return false;
                delta = read_u16(p);
                p += 2;
            } else {
                if (p >= end) return false;
                delta = *p++;
            }
            position += delta;
            out.indices.push_back(static_cast<uint16_t>(position));
        }
        remaining -= run_count;
    }
    return remaining == 0;
}

// Read packed deltas. Fills `out` with `count` deltas.
bool read_packed_deltas(const uint8_t*& p, const uint8_t* end,
                         int count, std::vector<int>& out) {
    out.clear();
    out.reserve(count);
    int remaining = count;
    while (remaining > 0 && p < end) {
        uint8_t control = *p++;
        int run_count = (control & 0x3F) + 1;
        if (run_count > remaining) run_count = remaining;
        if (control & 0x80) {
            for (int i = 0; i < run_count; ++i) out.push_back(0);
        } else if (control & 0x40) {
            for (int i = 0; i < run_count; ++i) {
                if (p + 2 > end) return false;
                out.push_back(read_i16(p));
                p += 2;
            }
        } else {
            for (int i = 0; i < run_count; ++i) {
                if (p >= end) return false;
                out.push_back(static_cast<int8_t>(*p));
                p++;
            }
        }
        remaining -= run_count;
    }
    return remaining == 0;
}

// Per-tuple scalar from peak (and optional intermediate) and normalized coords.
float tuple_scalar(const float* peak, const float* start, const float* end,
                    int axis_count, std::span<const float> norm) {
    float scalar = 1.0f;
    for (int a = 0; a < axis_count; ++a) {
        float p = peak[a];
        float c = (a < int(norm.size())) ? norm[a] : 0.0f;
        if (p == 0.0f) continue;
        if (c == 0.0f) return 0.0f;
        if (c == p) continue;

        float s = start ? start[a] : std::min(p, 0.0f);
        float e = end   ? end[a]   : std::max(p, 0.0f);
        if (c < s || c > e) return 0.0f;
        if (c < p) {
            if (p == s) return 0.0f;
            scalar *= (c - s) / (p - s);
        } else {
            if (e == p) return 0.0f;
            scalar *= (e - c) / (e - p);
        }
    }
    return scalar;
}

// Interpolation of Untouched Points: per contour, per axis, fill deltas
// for points that have no explicit delta in this tuple.
void apply_iup(float* delta_axis, const float* coord_axis,
                const uint8_t* touched,
                const uint16_t* end_pts, int num_contours,
                int num_points) {
    int contour_start = 0;
    for (int c = 0; c < num_contours; ++c) {
        int contour_end = end_pts[c];
        if (contour_end >= num_points) contour_end = num_points - 1;
        if (contour_end < contour_start) {
            contour_start = contour_end + 1;
            continue;
        }

        // Count touched in this contour.
        int touched_count = 0;
        for (int i = contour_start; i <= contour_end; ++i)
            if (touched[i]) ++touched_count;
        if (touched_count == 0) {
            contour_start = contour_end + 1;
            continue;
        }

        // Find first touched index for wrap-around start.
        int first_touched = -1;
        for (int i = contour_start; i <= contour_end; ++i) {
            if (touched[i]) { first_touched = i; break; }
        }
        if (first_touched < 0) {
            contour_start = contour_end + 1;
            continue;
        }

        // Iterate around the contour starting at first_touched.
        int i = first_touched;
        int prev = first_touched;
        do {
            int next_idx = i + 1;
            if (next_idx > contour_end) next_idx = contour_start;

            if (touched[i]) {
                prev = i;
                i = next_idx;
                continue;
            }

            // Find next touched going forward.
            int next_touched = next_idx;
            while (!touched[next_touched]) {
                int adv = next_touched + 1;
                if (adv > contour_end) adv = contour_start;
                next_touched = adv;
                if (next_touched == i) break;
            }

            float dPrev = delta_axis[prev];
            float dNext = delta_axis[next_touched];
            if (prev == next_touched || dPrev == dNext) {
                delta_axis[i] = dPrev;
            } else {
                float cPrev = coord_axis[prev];
                float cNext = coord_axis[next_touched];
                float cCur  = coord_axis[i];

                // Order so that cLo <= cHi.
                float cLo, cHi, dLo, dHi;
                if (cPrev <= cNext) {
                    cLo = cPrev; cHi = cNext; dLo = dPrev; dHi = dNext;
                } else {
                    cLo = cNext; cHi = cPrev; dLo = dNext; dHi = dPrev;
                }
                if (cCur <= cLo)       delta_axis[i] = dLo;
                else if (cCur >= cHi)  delta_axis[i] = dHi;
                else {
                    float t = (cCur - cLo) / (cHi - cLo);
                    delta_axis[i] = dLo + t * (dHi - dLo);
                }
            }

            i = next_idx;
        } while (i != first_touched);

        contour_start = contour_end + 1;
    }
}

} // namespace

bool FontData::apply_gvar(uint16_t glyph,
                            std::span<float> xs, std::span<float> ys,
                            std::span<const uint8_t> on_curve,
                            std::span<const uint16_t> end_pts,
                            const VariationInstance& var) const {
    (void)on_curve;
    uint32_t gvar = m_tables.gvar;
    if (!gvar) return false;
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    if (gvar + 20 > dsz) return false;

    uint16_t axis_count = read_u16(d + gvar + 4);
    uint16_t shared_tuple_count = read_u16(d + gvar + 6);
    uint32_t shared_tuples_off = gvar + read_u32(d + gvar + 8);
    uint16_t glyph_count = read_u16(d + gvar + 12);
    uint16_t flags = read_u16(d + gvar + 14);
    uint32_t glyph_variation_data_array_off = gvar + read_u32(d + gvar + 16);

    if (glyph >= glyph_count) return false;
    bool long_offsets = (flags & 1) != 0;

    uint32_t offsets_off = gvar + 20;
    uint32_t glyph_var_start = 0, glyph_var_end = 0;
    if (long_offsets) {
        if (offsets_off + size_t(glyph + 2) * 4 > dsz) return false;
        glyph_var_start = read_u32(d + offsets_off + glyph * 4);
        glyph_var_end = read_u32(d + offsets_off + (glyph + 1) * 4);
    } else {
        if (offsets_off + size_t(glyph + 2) * 2 > dsz) return false;
        glyph_var_start = uint32_t(read_u16(d + offsets_off + glyph * 2)) * 2;
        glyph_var_end = uint32_t(read_u16(d + offsets_off + (glyph + 1) * 2)) * 2;
    }
    if (glyph_var_start == glyph_var_end) return false;
    if (glyph_var_end < glyph_var_start) return false;

    uint32_t gvd_off = glyph_variation_data_array_off + glyph_var_start;
    uint32_t gvd_size = glyph_var_end - glyph_var_start;
    if (gvd_off + gvd_size > dsz || gvd_size < 4) return false;

    const uint8_t* gvd = d + gvd_off;
    const uint8_t* gvd_end = gvd + gvd_size;

    uint16_t tvc_raw = read_u16(gvd);
    uint16_t tuple_count = tvc_raw & 0x0FFF;
    bool shared_points = (tvc_raw & 0x8000) != 0;
    uint16_t data_offset_rel = read_u16(gvd + 2);
    if (data_offset_rel > gvd_size) return false;

    const uint8_t* tuple_headers = gvd + 4;
    const uint8_t* data_area = gvd + data_offset_rel;
    if (data_area > gvd_end) return false;

    int num_points = static_cast<int>(xs.size());

    // Read shared point numbers if present.
    PackedPoints shared_pts;
    const uint8_t* cur_data = data_area;
    if (shared_points) {
        if (!read_packed_points(cur_data, gvd_end, shared_pts)) return false;
    } else {
        shared_pts.all_points = true;
    }

    // Heap buffers for accumulation.
    std::vector<float> accum_x(num_points, 0.0f);
    std::vector<float> accum_y(num_points, 0.0f);

    auto norm = var.normalized();
    const uint8_t* th = tuple_headers;

    for (int t = 0; t < tuple_count && th + 4 <= gvd_end; ++t) {
        uint16_t var_data_size = read_u16(th);
        uint16_t tuple_index = read_u16(th + 2);
        th += 4;

        bool embedded_peak = (tuple_index & 0x8000) != 0;
        bool intermediate  = (tuple_index & 0x4000) != 0;
        bool private_pts   = (tuple_index & 0x2000) != 0;
        uint16_t shared_idx = tuple_index & 0x0FFF;

        float peak[16] = {0};
        float start[16] = {0};
        float end[16] = {0};
        float* start_ptr = nullptr;
        float* end_ptr = nullptr;

        int eff_axes = std::min<int>(axis_count, 16);
        if (embedded_peak) {
            if (th + eff_axes * 2 > gvd_end) return false;
            for (int a = 0; a < eff_axes; ++a)
                peak[a] = detail::read_f2dot14(th + a * 2);
            th += axis_count * 2;
        } else {
            if (shared_idx >= shared_tuple_count) return false;
            size_t sp_off = shared_tuples_off + size_t(shared_idx) * axis_count * 2;
            if (sp_off + eff_axes * 2 > dsz) return false;
            for (int a = 0; a < eff_axes; ++a)
                peak[a] = detail::read_f2dot14(d + sp_off + a * 2);
        }
        if (intermediate) {
            if (th + eff_axes * 4 > gvd_end) return false;
            for (int a = 0; a < eff_axes; ++a) {
                start[a] = detail::read_f2dot14(th + a * 2);
                end[a]   = detail::read_f2dot14(th + axis_count * 2 + a * 2);
            }
            th += axis_count * 4;
            start_ptr = start;
            end_ptr = end;
        }

        float scalar = tuple_scalar(peak, start_ptr, end_ptr, eff_axes, norm);
        const uint8_t* td = cur_data;
        const uint8_t* tuple_data_end = std::min(td + var_data_size, gvd_end);
        cur_data += var_data_size;

        if (scalar == 0.0f) continue;

        // Point numbers for this tuple.
        PackedPoints private_packed;
        bool use_all_points;
        const std::vector<uint16_t>* indices_src;

        if (private_pts) {
            if (!read_packed_points(td, tuple_data_end, private_packed)) continue;
            use_all_points = private_packed.all_points;
            indices_src = &private_packed.indices;
        } else {
            use_all_points = shared_pts.all_points;
            indices_src = &shared_pts.indices;
        }

        int delta_count = use_all_points
            ? num_points + 4
            : static_cast<int>(indices_src->size());

        std::vector<int> dx_packed, dy_packed;
        if (!read_packed_deltas(td, tuple_data_end, delta_count, dx_packed)) continue;
        if (!read_packed_deltas(td, tuple_data_end, delta_count, dy_packed)) continue;

        std::vector<float> dx_per(num_points, 0.0f);
        std::vector<float> dy_per(num_points, 0.0f);
        std::vector<uint8_t> touched(num_points, 0);

        if (use_all_points) {
            int n = std::min(num_points, delta_count);
            for (int i = 0; i < n; ++i) {
                dx_per[i] = static_cast<float>(dx_packed[i]);
                dy_per[i] = static_cast<float>(dy_packed[i]);
                touched[i] = 1;
            }
            for (int i = 0; i < num_points; ++i) {
                accum_x[i] += scalar * dx_per[i];
                accum_y[i] += scalar * dy_per[i];
            }
        } else {
            for (size_t k = 0; k < indices_src->size() && k < dx_packed.size(); ++k) {
                int idx = (*indices_src)[k];
                if (idx >= num_points) continue;
                dx_per[idx] = static_cast<float>(dx_packed[k]);
                dy_per[idx] = static_cast<float>(dy_packed[k]);
                touched[idx] = 1;
            }
            std::vector<uint16_t> end_pts_local(end_pts.begin(), end_pts.end());
            apply_iup(dx_per.data(), xs.data(), touched.data(),
                       end_pts_local.data(), int(end_pts.size()), num_points);
            apply_iup(dy_per.data(), ys.data(), touched.data(),
                       end_pts_local.data(), int(end_pts.size()), num_points);
            for (int i = 0; i < num_points; ++i) {
                accum_x[i] += scalar * dx_per[i];
                accum_y[i] += scalar * dy_per[i];
            }
        }
    }

    for (int i = 0; i < num_points; ++i) {
        xs[i] += accum_x[i];
        ys[i] += accum_y[i];
    }
    return true;
}

// TrueType glyph parsing

static constexpr int kMaxCompositeDepth = 32;

int FontData::parse_tt_glyph(uint16_t glyph, const GlyphSink& sink,
                               const VariationInstance* var, int depth) const {
    if (depth > kMaxCompositeDepth) return hmetrics(glyph, var).advance_width;
    int g = locate_glyph(glyph);
    if (g < 0)
        return hmetrics(glyph, var).advance_width;

    auto* d = m_data.data();
    if (static_cast<size_t>(g) + 10 > m_data.size())
        return hmetrics(glyph, var).advance_width;

    int16_t num_contours = read_i16(d + g);
    if (num_contours > 0)
        parse_tt_simple(d + g, num_contours, sink, var, glyph);
    else if (num_contours == -1)
        parse_tt_composite(d + g, sink, var, depth);

    return hmetrics(glyph, var).advance_width;
}

// Decode TT simple glyph into float coordinate arrays, apply gvar (if any),
// then emit contours through the sink. Single-pass for non-var path.

int FontData::parse_tt_simple(const uint8_t* glyph_ptr, int num_contours,
                                const GlyphSink& sink,
                                const VariationInstance* var,
                                uint16_t glyph) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    size_t base = glyph_ptr - d;

    size_t endPts_off = base + 10;
    if (endPts_off + num_contours * 2 + 2 > dsz) return 0;
    const uint8_t* endPts = d + endPts_off;

    int ins = read_u16(d + endPts_off + num_contours * 2);
    const uint8_t* pts = d + endPts_off + num_contours * 2 + 2 + ins;
    const uint8_t* data_end = d + dsz;
    if (pts >= data_end) return 0;

    int n = 1 + read_u16(endPts + num_contours * 2 - 2);

    // Storage: small glyphs on stack, larger on heap.
    constexpr int kStackLimit = 512;
    float stack_x[kStackLimit];
    float stack_y[kStackLimit];
    uint8_t stack_on[kStackLimit];
    std::unique_ptr<float[]> heap_x, heap_y;
    std::unique_ptr<uint8_t[]> heap_on;
    float* xs = stack_x;
    float* ys = stack_y;
    uint8_t* on_curve = stack_on;
    if (n > kStackLimit) {
        heap_x = std::make_unique<float[]>(n);
        heap_y = std::make_unique<float[]>(n);
        heap_on = std::make_unique<uint8_t[]>(n);
        xs = heap_x.get();
        ys = heap_y.get();
        on_curve = heap_on.get();
    }

    // Decode flags
    std::vector<uint8_t> flags_buf;
    flags_buf.resize(n);
    const uint8_t* p = pts;
    uint8_t flagcount = 0, flags = 0;
    for (int i = 0; i < n; ++i) {
        if (flagcount == 0) {
            if (p >= data_end) return 0;
            flags = *p++;
            if (flags & 8) {
                if (p >= data_end) return 0;
                flagcount = *p++;
            }
        } else --flagcount;
        flags_buf[i] = flags;
        on_curve[i] = (flags & 1) ? 1 : 0;
    }

    // Decode X coordinates
    int x_int = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t fl = flags_buf[i];
        if (fl & 2) {
            if (p >= data_end) return 0;
            int16_t dx = *p++;
            x_int += (fl & 16) ? dx : -dx;
        } else if (!(fl & 16)) {
            if (p + 1 >= data_end) return 0;
            x_int += static_cast<int16_t>(p[0] * 256 + p[1]);
            p += 2;
        }
        xs[i] = static_cast<float>(x_int);
    }

    // Decode Y coordinates
    int y_int = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t fl = flags_buf[i];
        if (fl & 4) {
            if (p >= data_end) return 0;
            int16_t dy = *p++;
            y_int += (fl & 32) ? dy : -dy;
        } else if (!(fl & 32)) {
            if (p + 1 >= data_end) return 0;
            y_int += static_cast<int16_t>(p[0] * 256 + p[1]);
            p += 2;
        }
        ys[i] = static_cast<float>(y_int);
    }

    // gvar
    if (var && has_gvar() && !var->empty()) {
        // Copy end points to a typed span
        std::vector<uint16_t> end_pts_buf(num_contours);
        for (int i = 0; i < num_contours; ++i)
            end_pts_buf[i] = read_u16(endPts + i * 2);
        apply_gvar(glyph,
                    std::span<float>(xs, n),
                    std::span<float>(ys, n),
                    std::span<const uint8_t>(on_curve, n),
                    std::span<const uint16_t>(end_pts_buf.data(), num_contours),
                    *var);
    }

    // Emit contours
    int pt_idx = 0;
    for (int c = 0; c < num_contours; ++c) {
        int end_pt = 1 + read_u16(endPts + c * 2);
        int count = end_pt - pt_idx;
        if (count < 1) { pt_idx = end_pt; continue; }

        int first = pt_idx;
        int last = end_pt - 1;

        bool first_on = (on_curve[first] != 0);
        bool last_on  = (on_curve[last] != 0);

        float sx, sy;
        float scx = 0, scy = 0;
        bool start_off = false;

        if (first_on) {
            sx = xs[first];
            sy = ys[first];
        } else if (last_on) {
            sx = xs[last];
            sy = ys[last];
            --last;
        } else {
            sx = (xs[first] + xs[last]) * 0.5f;
            sy = (ys[first] + ys[last]) * 0.5f;
            start_off = true;
            scx = xs[first];
            scy = ys[first];
        }

        sink.move_to(sx, sy);

        float last_on_x = sx, last_on_y = sy;
        float cx = 0, cy = 0;
        bool was_off = false;

        int begin = first_on ? first + 1 : first + (start_off ? 1 : 0);

        for (int i = begin; i <= last; ++i) {
            float px = xs[i];
            float py = ys[i];
            bool on = (on_curve[i] != 0);

            if (on) {
                if (was_off) sink.quad_to(cx, cy, px, py);
                else         sink.line_to(px, py);
                last_on_x = px;
                last_on_y = py;
                was_off = false;
            } else {
                if (was_off) {
                    float mx = (cx + px) * 0.5f;
                    float my = (cy + py) * 0.5f;
                    sink.quad_to(cx, cy, mx, my);
                    last_on_x = mx;
                    last_on_y = my;
                }
                cx = px;
                cy = py;
                was_off = true;
            }
        }

        if (start_off) {
            if (was_off) {
                float mx = (cx + scx) * 0.5f;
                float my = (cy + scy) * 0.5f;
                sink.quad_to(cx, cy, mx, my);
            }
            sink.quad_to(scx, scy, sx, sy);
        } else {
            if (was_off) {
                sink.quad_to(cx, cy, sx, sy);
            } else if (last_on_x != sx || last_on_y != sy) {
                sink.line_to(sx, sy);
            }
        }
        sink.close();
        pt_idx = end_pt;
    }

    return 0;
}

int FontData::parse_tt_composite(const uint8_t* glyph_ptr, const GlyphSink& sink,
                                   const VariationInstance* var, int depth) const {
    auto* d = m_data.data();
    size_t dsz = m_data.size();
    const uint8_t* comp = glyph_ptr + 10;

    bool more = true;
    while (more) {
        if (comp + 4 > d + dsz) break;
        uint16_t cflags = read_u16(comp); comp += 2;
        uint16_t gidx   = read_u16(comp); comp += 2;

        float mtx[6] = {1, 0, 0, 1, 0, 0};

        if (cflags & 2) {
            if (cflags & 1) {
                if (comp + 4 > d + dsz) break;
                mtx[4] = read_i16(comp); comp += 2;
                mtx[5] = read_i16(comp); comp += 2;
            } else {
                if (comp + 2 > d + dsz) break;
                mtx[4] = read_i8(comp); comp += 1;
                mtx[5] = read_i8(comp); comp += 1;
            }
        } else {
            // Point-number positioning: not implemented (rare; older Apple fonts).
            break;
        }

        if (cflags & (1 << 3)) {
            if (comp + 2 > d + dsz) break;
            mtx[0] = mtx[3] = read_i16(comp) / 16384.0f; comp += 2;
        } else if (cflags & (1 << 6)) {
            if (comp + 4 > d + dsz) break;
            mtx[0] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[3] = read_i16(comp) / 16384.0f; comp += 2;
        } else if (cflags & (1 << 7)) {
            if (comp + 8 > d + dsz) break;
            mtx[0] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[1] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[2] = read_i16(comp) / 16384.0f; comp += 2;
            mtx[3] = read_i16(comp) / 16384.0f; comp += 2;
        }

        struct XformSink {
            const GlyphSink* parent;
            float m[6];
            static void callback(void* ctx, OutlineCmd cmd, const OutlinePoint* pts, int n) {
                auto& self = *static_cast<XformSink*>(ctx);
                if (n == 0) {
                    self.parent->func(self.parent->ctx, cmd, nullptr, 0);
                    return;
                }
                OutlinePoint tmp[3];
                for (int i = 0; i < n && i < 3; ++i) {
                    tmp[i].x = self.m[0] * pts[i].x + self.m[2] * pts[i].y + self.m[4];
                    tmp[i].y = self.m[1] * pts[i].x + self.m[3] * pts[i].y + self.m[5];
                }
                self.parent->func(self.parent->ctx, cmd, tmp, n);
            }
        };

        XformSink xsink;
        xsink.parent = &sink;
        std::memcpy(xsink.m, mtx, sizeof(mtx));

        GlyphSink child_sink{XformSink::callback, &xsink};
        // gvar deltas for composite component offsets are not applied here.
        // Sub-glyphs themselves get their own gvar through parse_tt_simple.
        parse_tt_glyph(gidx, child_sink, var, depth + 1);

        more = (cflags & (1 << 5));
    }

    return 0;
}

// CFF charstring interpreter

struct FontData::CsCtx {
    const GlyphSink* sink = nullptr;
    bool started = false;
    float first_x = 0, first_y = 0;
    float x = 0, y = 0;
    int32_t min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    int num_vertices = 0;

    void track(float px, float py) {
        int ix = static_cast<int>(px), iy = static_cast<int>(py);
        if (ix > max_x || !started) max_x = ix;
        if (iy > max_y || !started) max_y = iy;
        if (ix < min_x || !started) min_x = ix;
        if (iy < min_y || !started) min_y = iy;
        started = true;
    }

    void cs_move_to(float dx, float dy) {
        cs_close_shape();
        first_x = x = x + dx;
        first_y = y = y + dy;
        track(x, y);
        num_vertices++;
        if (sink) sink->move_to(x, y);
    }

    void cs_line_to(float dx, float dy) {
        x += dx;
        y += dy;
        track(x, y);
        num_vertices++;
        if (sink) sink->line_to(x, y);
    }

    void cs_curve_to(float dx1, float dy1, float dx2, float dy2,
                     float dx3, float dy3) {
        float cx1 = x + dx1, cy1 = y + dy1;
        float cx2 = cx1 + dx2, cy2 = cy1 + dy2;
        x = cx2 + dx3;
        y = cy2 + dy3;
        track(cx1, cy1);
        track(cx2, cy2);
        track(x, y);
        num_vertices++;
        if (sink) sink->cubic_to(cx1, cy1, cx2, cy2, x, y);
    }

    void cs_close_shape() {
        if (first_x != x || first_y != y) {
            num_vertices++;
            if (sink) sink->line_to(first_x, first_y);
        }
        if (sink && started) sink->close();
    }
};

CFFBuffer FontData::get_subr(const CFFBuffer& idx, int n) const {
    int count = cff::index_count(const_cast<CFFBuffer&>(idx));
    int bias = 107;
    if (count >= 33900) bias = 32768;
    else if (count >= 1240) bias = 1131;
    n += bias;
    if (n < 0 || n >= count) return {};
    return cff::index_get(idx, n);
}

CFFBuffer FontData::cid_glyph_subrs(uint16_t glyph) const {
    CFFBuffer fds = m_cff.fd_select;
    int fdselector = -1;
    cff::buf_seek(fds, 0);
    int fmt = cff::buf_get8(fds);
    if (fmt == 0) {
        cff::buf_skip(fds, glyph);
        fdselector = cff::buf_get8(fds);
    } else if (fmt == 3) {
        int nranges = cff::buf_get16(fds);
        int start = cff::buf_get16(fds);
        for (int i = 0; i < nranges; ++i) {
            int v = cff::buf_get8(fds);
            int end = cff::buf_get16(fds);
            if (glyph >= start && glyph < end) {
                fdselector = v;
                break;
            }
            start = end;
        }
    }
    if (fdselector == -1) return {};
    return cff::get_subrs(m_cff.cff, cff::index_get(m_cff.font_dicts, fdselector));
}

bool FontData::run_charstring(uint16_t glyph, CsCtx& ctx) const {
    int in_header = 1, maskbits = 0, subr_stack_height = 0;
    int sp = 0, v, i, b0;
    int has_subrs = 0, clear_stack;
    float s[48];
    CFFBuffer subr_stack[10];
    CFFBuffer subrs = m_cff.local_subrs;
    float f;

    CFFBuffer b = cff::index_get(m_cff.char_strings, glyph);
    while (b.cursor < b.size) {
        i = 0;
        clear_stack = 1;
        b0 = cff::buf_get8(b);
        switch (b0) {
        case 0x13: case 0x14:
            if (in_header) maskbits += (sp / 2);
            in_header = 0;
            cff::buf_skip(b, (maskbits + 7) / 8);
            break;

        case 0x01: case 0x03: case 0x12: case 0x17:
            maskbits += (sp / 2);
            break;

        case 0x15:
            in_header = 0;
            if (sp < 2) return false;
            ctx.cs_move_to(s[sp - 2], s[sp - 1]);
            break;
        case 0x04:
            in_header = 0;
            if (sp < 1) return false;
            ctx.cs_move_to(0, s[sp - 1]);
            break;
        case 0x16:
            in_header = 0;
            if (sp < 1) return false;
            ctx.cs_move_to(s[sp - 1], 0);
            break;

        case 0x05:
            if (sp < 2) return false;
            for (; i + 1 < sp; i += 2)
                ctx.cs_line_to(s[i], s[i + 1]);
            break;

        case 0x07:
            if (sp < 1) return false;
            goto vlineto;
        case 0x06:
            if (sp < 1) return false;
            for (;;) {
                if (i >= sp) break;
                ctx.cs_line_to(s[i], 0);
                i++;
            vlineto:
                if (i >= sp) break;
                ctx.cs_line_to(0, s[i]);
                i++;
            }
            break;

        case 0x1F:
            if (sp < 4) return false;
            goto hvcurveto;
        case 0x1E:
            if (sp < 4) return false;
            for (;;) {
                if (i + 3 >= sp) break;
                ctx.cs_curve_to(0, s[i], s[i+1], s[i+2], s[i+3],
                                (sp - i == 5) ? s[i+4] : 0.0f);
                i += 4;
            hvcurveto:
                if (i + 3 >= sp) break;
                ctx.cs_curve_to(s[i], 0, s[i+1], s[i+2],
                                (sp - i == 5) ? s[i+4] : 0.0f, s[i+3]);
                i += 4;
            }
            break;

        case 0x08:
            if (sp < 6) return false;
            for (; i + 5 < sp; i += 6)
                ctx.cs_curve_to(s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
            break;

        case 0x18:
            if (sp < 8) return false;
            for (; i + 5 < sp - 2; i += 6)
                ctx.cs_curve_to(s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
            if (i + 1 >= sp) return false;
            ctx.cs_line_to(s[i], s[i + 1]);
            break;

        case 0x19:
            if (sp < 8) return false;
            for (; i + 1 < sp - 6; i += 2)
                ctx.cs_line_to(s[i], s[i + 1]);
            if (i + 5 >= sp) return false;
            ctx.cs_curve_to(s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
            break;

        case 0x1A: case 0x1B:
            if (sp < 4) return false;
            f = 0.0;
            if (sp & 1) { f = s[i]; i++; }
            for (; i + 3 < sp; i += 4) {
                if (b0 == 0x1B)
                    ctx.cs_curve_to(s[i], f, s[i+1], s[i+2], s[i+3], 0.0);
                else
                    ctx.cs_curve_to(f, s[i], s[i+1], s[i+2], 0.0, s[i+3]);
                f = 0.0;
            }
            break;

        case 0x0A:
            if (!has_subrs) {
                if (m_cff.fd_select.size)
                    subrs = cid_glyph_subrs(glyph);
                has_subrs = 1;
            }
            [[fallthrough]];
        case 0x1D:
            if (sp < 1) return false;
            v = static_cast<int>(s[--sp]);
            if (subr_stack_height >= 10) return false;
            subr_stack[subr_stack_height++] = b;
            b = get_subr(b0 == 0x0A ? subrs : m_cff.global_subrs, v);
            if (b.size == 0) return false;
            b.cursor = 0;
            clear_stack = 0;
            break;

        case 0x0B:
            if (subr_stack_height <= 0) return false;
            b = subr_stack[--subr_stack_height];
            clear_stack = 0;
            break;

        case 0x0E:
            ctx.cs_close_shape();
            return true;

        case 0x0C: {
            float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
            float dx, dy;
            int b1 = cff::buf_get8(b);
            switch (b1) {
            case 0x22:
                if (sp < 7) return false;
                dx1 = s[0]; dx2 = s[1]; dy2 = s[2]; dx3 = s[3];
                dx4 = s[4]; dx5 = s[5]; dx6 = s[6];
                ctx.cs_curve_to(dx1, 0, dx2, dy2, dx3, 0);
                ctx.cs_curve_to(dx4, 0, dx5, -dy2, dx6, 0);
                break;
            case 0x23:
                if (sp < 13) return false;
                dx1=s[0]; dy1=s[1]; dx2=s[2]; dy2=s[3]; dx3=s[4]; dy3=s[5];
                dx4=s[6]; dy4=s[7]; dx5=s[8]; dy5=s[9]; dx6=s[10]; dy6=s[11];
                ctx.cs_curve_to(dx1, dy1, dx2, dy2, dx3, dy3);
                ctx.cs_curve_to(dx4, dy4, dx5, dy5, dx6, dy6);
                break;
            case 0x24:
                if (sp < 9) return false;
                dx1=s[0]; dy1=s[1]; dx2=s[2]; dy2=s[3]; dx3=s[4];
                dx4=s[5]; dx5=s[6]; dy5=s[7]; dx6=s[8];
                ctx.cs_curve_to(dx1, dy1, dx2, dy2, dx3, 0);
                ctx.cs_curve_to(dx4, 0, dx5, dy5, dx6, -(dy1+dy2+dy5));
                break;
            case 0x25:
                if (sp < 11) return false;
                dx1=s[0]; dy1=s[1]; dx2=s[2]; dy2=s[3]; dx3=s[4]; dy3=s[5];
                dx4=s[6]; dy4=s[7]; dx5=s[8]; dy5=s[9];
                dx6 = dy6 = s[10];
                dx = dx1+dx2+dx3+dx4+dx5;
                dy = dy1+dy2+dy3+dy4+dy5;
                if (std::fabs(dx) > std::fabs(dy)) dy6 = -dy;
                else dx6 = -dx;
                ctx.cs_curve_to(dx1, dy1, dx2, dy2, dx3, dy3);
                ctx.cs_curve_to(dx4, dy4, dx5, dy5, dx6, dy6);
                break;
            default: return false;
            }
        } break;

        default:
            if (b0 != 255 && b0 != 28 && b0 < 32) return false;
            if (b0 == 255) {
                f = static_cast<float>(static_cast<int32_t>(cff::buf_get32(b))) / 0x10000;
            } else {
                cff::buf_skip(b, -1);
                f = static_cast<float>(static_cast<int16_t>(cff::dict_int(b)));
            }
            if (sp >= 48) return false;
            s[sp++] = f;
            clear_stack = 0;
            break;
        }
        if (clear_stack) sp = 0;
    }
    return false;
}

int FontData::parse_cff_glyph(uint16_t glyph, const GlyphSink& sink) const {
    CsCtx ctx;
    ctx.sink = &sink;
    run_charstring(glyph, ctx);
    return hmetrics(glyph, nullptr).advance_width;
}

bool FontData::cff_glyph_box(uint16_t glyph, BBox& box) const {
    CsCtx ctx;
    ctx.sink = nullptr;
    if (!run_charstring(glyph, ctx)) return false;
    if (!ctx.started) return false;
    box = {ctx.min_x, ctx.min_y, ctx.max_x, ctx.max_y};
    return true;
}

bool FontData::cff_is_empty(uint16_t glyph) const {
    CsCtx ctx;
    ctx.sink = nullptr;
    if (!run_charstring(glyph, ctx)) return true;
    return ctx.num_vertices == 0;
}

} // namespace kcg::otf
