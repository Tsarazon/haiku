/*
 * Copyright 2025, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Template-based drawing mode implementation.
 * Replaces 300 lines × 19 files with single template instantiation.
 *
 * This template implements all 6 blend functions required by AGG for any
 * BlendPolicy that defines Blend() and Assign() operations.
 */

#ifndef DRAWING_MODE_TEMPLATE_H
#define DRAWING_MODE_TEMPLATE_H

#include "DrawingMode.h"
#include "PatternHandler.h"
#include "PixelFormat.h"

// Template implementing all blend functions for simple drawing modes
// (Add, Subtract, Blend, Min, Max, AlphaCC, AlphaCO, AlphaPC, AlphaPO)
template<typename BlendPolicy>
struct DrawingModeImpl {

	// blend_pixel - blend a single pixel
	static void blend_pixel(int x, int y, const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = pattern->ColorAt(x, y);
		if (cover == 255) {
			BlendPolicy::Assign(p, color.red, color.green, color.blue, color.alpha);
		} else {
			BlendPolicy::Blend(p, color.red, color.green, color.blue, cover);
		}
	}

	// blend_hline - blend horizontal line
	static void blend_hline(int x, int y, unsigned len,
							const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		if (cover == 255) {
			do {
				rgb_color color = pattern->ColorAt(x, y);
				BlendPolicy::Assign(p, color.red, color.green, color.blue, color.alpha);
				p += 4;
				x++;
			} while(--len);
		} else {
			do {
				rgb_color color = pattern->ColorAt(x, y);
				BlendPolicy::Blend(p, color.red, color.green, color.blue, cover);
				x++;
				p += 4;
			} while(--len);
		}
	}

	// blend_solid_hspan - blend horizontal span with varying coverage
	static void blend_solid_hspan(int x, int y, unsigned len,
								   const color_type& c, const uint8* covers,
								   agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		do {
			rgb_color color = pattern->ColorAt(x, y);
			if (*covers) {
				if (*covers == 255) {
					BlendPolicy::Assign(p, color.red, color.green, color.blue, color.alpha);
				} else {
					BlendPolicy::Blend(p, color.red, color.green, color.blue, *covers);
				}
			}
			covers++;
			p += 4;
			x++;
		} while(--len);
	}

	// blend_solid_vspan - blend vertical span with varying coverage
	static void blend_solid_vspan(int x, int y, unsigned len,
								   const color_type& c, const uint8* covers,
								   agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		do {
			rgb_color color = pattern->ColorAt(x, y);
			if (*covers) {
				if (*covers == 255) {
					BlendPolicy::Assign(p, color.red, color.green, color.blue, color.alpha);
				} else {
					BlendPolicy::Blend(p, color.red, color.green, color.blue, *covers);
				}
			}
			covers++;
			p += buffer->stride();
			y++;
		} while(--len);
	}

	// blend_color_hspan - blend horizontal span with per-pixel colors and coverage
	static void blend_color_hspan(int x, int y, unsigned len,
								   const color_type* colors,
								   const uint8* covers, uint8 cover,
								   agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		if (covers) {
			// non-solid opacity
			do {
				if (*covers && colors->a > 0) {
					if (*covers == 255) {
						BlendPolicy::Assign(p, colors->r, colors->g, colors->b, colors->a);
					} else {
						BlendPolicy::Blend(p, colors->r, colors->g, colors->b, *covers);
					}
				}
				covers++;
				p += 4;
				++colors;
			} while(--len);
		} else {
			// solid full opacity
			if (cover == 255) {
				do {
					if (colors->a > 0) {
						BlendPolicy::Assign(p, colors->r, colors->g, colors->b, colors->a);
					}
					p += 4;
					++colors;
				} while(--len);
			// solid partial opacity
			} else if (cover) {
				do {
					if (colors->a > 0) {
						BlendPolicy::Blend(p, colors->r, colors->g, colors->b, cover);
					}
					p += 4;
					++colors;
				} while(--len);
			}
		}
	}

	// blend_solid_hspan_subpix - subpixel rendering for LCD displays
	static void blend_solid_hspan_subpix(int x, int y, unsigned len,
										  const color_type& c, const uint8* covers,
										  agg_buffer* buffer, const PatternHandler* pattern)
	{
		// Subpixel rendering uses separate coverage for R, G, B subpixels
		// Order depends on LCD subpixel layout (RGB or BGR)
		extern bool gSubpixelOrderingRGB;
		const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
		const int subpixelM = 1;
		const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;
		
		uint8* p = buffer->row_ptr(y) + (x << 2);
		do {
			rgb_color color = pattern->ColorAt(x, y);
			BlendPolicy::BlendSubpix(p, color.red, color.green, color.blue,
				covers[subpixelL], covers[subpixelM], covers[subpixelR]);
			covers += 3;
			p += 4;
			x++;
			len -= 3;
		} while (len);
	}
};

// Template for Alpha modes that use 16-bit alpha calculations
template<typename BlendPolicy>
struct AlphaModeImpl {

	// blend_pixel - blend a single pixel with alpha
	static void blend_pixel(int x, int y, const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = pattern->ColorAt(x, y);
		uint16 alpha = pattern->HighColor().alpha * cover;
		if (alpha == 255 * 255) {
			BlendPolicy::Assign(p, color.red, color.green, color.blue, color.alpha);
		} else {
			BlendPolicy::Blend(p, color.red, color.green, color.blue, alpha);
		}
	}

	// blend_hline - blend horizontal line with alpha
	static void blend_hline(int x, int y, unsigned len,
							const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		rgb_color color = pattern->HighColor();
		uint16 alpha = color.alpha * cover;
		if (alpha == 255 * 255) {
			// Cache the low and high color as 32bit values
			uint32 vh;
			uint8* p8 = (uint8*)&vh;
			p8[0] = color.blue;
			p8[1] = color.green;
			p8[2] = color.red;
			p8[3] = 255;
			
			color = pattern->LowColor();
			uint32 vl;
			p8 = (uint8*)&vl;
			p8[0] = color.blue;
			p8[1] = color.green;
			p8[2] = color.red;
			p8[3] = 255;
			
			uint32* p32 = (uint32*)(buffer->row_ptr(y)) + x;
			do {
				if (pattern->IsHighColor(x, y))
					*p32 = vh;
				else
					*p32 = vl;
				p32++;
				x++;
			} while(--len);
		} else {
			uint8* p = buffer->row_ptr(y) + (x << 2);
			do {
				rgb_color color = pattern->ColorAt(x, y);
				BlendPolicy::Blend(p, color.red, color.green, color.blue, alpha);
				x++;
				p += 4;
			} while(--len);
		}
	}

	// blend_solid_hspan - blend horizontal span with alpha
	static void blend_solid_hspan(int x, int y, unsigned len,
								   const color_type& c, const uint8* covers,
								   agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint8 hAlpha = pattern->HighColor().alpha;
		do {
			rgb_color color = pattern->ColorAt(x, y);
			uint16 alpha = hAlpha * *covers;
			if (alpha) {
				if (alpha == 255 * 255) {
					BlendPolicy::Assign(p, color.red, color.green, color.blue, color.alpha);
				} else {
					BlendPolicy::Blend(p, color.red, color.green, color.blue, alpha);
				}
			}
			covers++;
			p += 4;
			x++;
		} while(--len);
	}

	// blend_solid_vspan - blend vertical span with alpha
	static void blend_solid_vspan(int x, int y, unsigned len,
								   const color_type& c, const uint8* covers,
								   agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint8 hAlpha = pattern->HighColor().alpha;
		do {
			rgb_color color = pattern->ColorAt(x, y);
			uint16 alpha = hAlpha * *covers;
			if (alpha) {
				if (alpha == 255 * 255) {
					BlendPolicy::Assign(p, color.red, color.green, color.blue, color.alpha);
				} else {
					BlendPolicy::Blend(p, color.red, color.green, color.blue, alpha);
				}
			}
			covers++;
			p += buffer->stride();
			y++;
		} while(--len);
	}

	// blend_color_hspan - blend horizontal span with per-pixel colors and alpha
	static void blend_color_hspan(int x, int y, unsigned len,
								   const color_type* colors,
								   const uint8* covers, uint8 cover,
								   agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint8 hAlpha = pattern->HighColor().alpha;
		if (covers) {
			// non-solid opacity
			do {
				uint16 alpha = hAlpha * colors->a * *covers / 255;
				if (alpha) {
					if (alpha == 255 * 255) {
						BlendPolicy::Assign(p, colors->r, colors->g, colors->b, colors->a);
					} else {
						BlendPolicy::Blend(p, colors->r, colors->g, colors->b, alpha);
					}
				}
				covers++;
				p += 4;
				++colors;
			} while(--len);
		} else {
			// solid opacity
			uint16 alpha = hAlpha * colors->a * cover / 255;
			if (alpha == 255 * 255) {
				do {
					BlendPolicy::Assign(p, colors->r, colors->g, colors->b, colors->a);
					p += 4;
					++colors;
				} while(--len);
			} else if (alpha) {
				do {
					BlendPolicy::Blend(p, colors->r, colors->g, colors->b, alpha);
					p += 4;
					++colors;
				} while(--len);
			}
		}
	}

	// blend_solid_hspan_subpix - subpixel rendering for alpha modes
	static void blend_solid_hspan_subpix(int x, int y, unsigned len,
										  const color_type& c, const uint8* covers,
										  agg_buffer* buffer, const PatternHandler* pattern)
	{
		// Subpixel rendering for alpha modes with 16-bit alpha calculations
		extern bool gSubpixelOrderingRGB;
		const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
		const int subpixelM = 1;
		const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;
		
		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint8 hAlpha = pattern->HighColor().alpha;
		do {
			// Calculate 16-bit alpha for each RGB subpixel
			uint16 alphaRed = hAlpha * covers[subpixelL];
			uint16 alphaGreen = hAlpha * covers[subpixelM];
			uint16 alphaBlue = hAlpha * covers[subpixelR];
			
			rgb_color color = pattern->ColorAt(x, y);
			// Note: BlendSubpix expects alphaBlue, alphaGreen, alphaRed order
			BlendPolicy::BlendSubpix(p, color.red, color.green, color.blue,
				alphaBlue, alphaGreen, alphaRed);
			covers += 3;
			p += 4;
			x++;
			len -= 3;
		} while (len);
	}
};

// PatternFilteredModeImpl: For modes that check pattern->IsHighColor() before blending
// (B_OP_OVER, B_OP_ERASE, B_OP_INVERT)
template<typename BlendPolicy>
struct PatternFilteredModeImpl {
	// blend_pixel - pattern filtered
	static void blend_pixel(int x, int y, const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		if (pattern->IsHighColor(x, y)) {
			uint8* p = buffer->row_ptr(y) + (x << 2);
			rgb_color color = BlendPolicy::GetColor(pattern);
			if (cover == 255) {
				BlendPolicy::Assign(p, color.red, color.green, color.blue);
			} else {
				BlendPolicy::Blend(p, color.red, color.green, color.blue, cover);
			}
		}
	}

	// blend_hline - pattern filtered
	static void blend_hline(int x, int y, unsigned len,
							const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = BlendPolicy::GetColor(pattern);
		if (cover == 255) {
			do {
				if (pattern->IsHighColor(x, y)) {
					BlendPolicy::Assign(p, color.red, color.green, color.blue);
				}
				x++;
				p += 4;
			} while(--len);
		} else {
			do {
				if (pattern->IsHighColor(x, y)) {
					BlendPolicy::Blend(p, color.red, color.green, color.blue, cover);
				}
				x++;
				p += 4;
			} while(--len);
		}
	}

	// blend_solid_hspan - pattern filtered
	static void blend_solid_hspan(int x, int y, unsigned len,
								  const color_type& c, const uint8* covers,
								  agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = BlendPolicy::GetColor(pattern);
		do {
			if (pattern->IsHighColor(x, y)) {
				if (*covers) {
					if (*covers == 255) {
						BlendPolicy::Assign(p, color.red, color.green, color.blue);
					} else {
						BlendPolicy::Blend(p, color.red, color.green, color.blue, *covers);
					}
				}
			}
			covers++;
			p += 4;
			x++;
		} while(--len);
	}

	// blend_solid_vspan - pattern filtered
	static void blend_solid_vspan(int x, int y, unsigned len,
								  const color_type& c, const uint8* covers,
								  agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = BlendPolicy::GetColor(pattern);
		do {
			if (pattern->IsHighColor(x, y)) {
				if (*covers) {
					if (*covers == 255) {
						BlendPolicy::Assign(p, color.red, color.green, color.blue);
					} else {
						BlendPolicy::Blend(p, color.red, color.green, color.blue, *covers);
					}
				}
			}
			covers++;
			p += buffer->stride();
			y++;
		} while(--len);
	}

	// blend_solid_hspan_subpix - pattern filtered with subpixel rendering
	static void blend_solid_hspan_subpix(int x, int y, unsigned len,
										 const color_type& c, const uint8* covers,
										 agg_buffer* buffer, const PatternHandler* pattern)
	{
		extern bool gSubpixelOrderingRGB;
		const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
		const int subpixelM = 1;
		const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;
		
		uint8* p = buffer->row_ptr(y) + (x << 2);
		rgb_color color = BlendPolicy::GetColor(pattern);
		do {
			if (pattern->IsHighColor(x, y)) {
				BlendPolicy::BlendSubpix(p, color.red, color.green, color.blue,
					covers[subpixelL], covers[subpixelM], covers[subpixelR]);
			}
			covers += 3;
			p += 4;
			x++;
			len -= 3;
		} while (len);
	}
};

// Specialized blend_color_hspan for ErasePolicy (uses pattern->LowColor() instead of colors)
inline void
blend_color_hspan_erase(int x, int y, unsigned len,
						const color_type* colors,
						const uint8* covers, uint8 cover,
						agg_buffer* buffer, const PatternHandler* pattern)
{
	uint8* p = buffer->row_ptr(y) + (x << 2);
	rgb_color lowColor = pattern->LowColor();
	if (covers) {
		// non-solid opacity
		do {
			if (*covers && colors->a > 0) {
				if (*covers == 255) {
					ErasePolicy::Assign(p, lowColor.red, lowColor.green, lowColor.blue);
				} else {
					ErasePolicy::Blend(p, lowColor.red, lowColor.green, lowColor.blue, *covers);
				}
			}
			covers++;
			p += 4;
			++colors;
		} while(--len);
	} else {
		// solid full opacity
		if (cover == 255) {
			do {
				if (colors->a > 0) {
					ErasePolicy::Assign(p, lowColor.red, lowColor.green, lowColor.blue);
				}
				p += 4;
				++colors;
			} while(--len);
		// solid partial opacity
		} else if (cover) {
			do {
				if (colors->a > 0) {
					ErasePolicy::Blend(p, lowColor.red, lowColor.green, lowColor.blue, cover);
				}
				p += 4;
				++colors;
			} while(--len);
		}
	}
}

// SolidAlphaModeImpl: Optimized version for solid patterns with Alpha modes
// Uses 16-bit alpha calculations (pattern->HighColor().alpha * cover)
template<typename BlendPolicy>
struct SolidAlphaModeImpl {
	// blend_pixel - solid pattern with 16-bit alpha
	static void blend_pixel(int x, int y, const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint16 alpha = pattern->HighColor().alpha * cover;
		if (alpha == 255 * 255) {
			BlendPolicy::Assign(p, c.r, c.g, c.b, 255);
		} else {
			BlendPolicy::Blend(p, c.r, c.g, c.b, alpha);
		}
	}

	// blend_hline - solid pattern with 16-bit alpha and 32-bit write optimization
	static void blend_hline(int x, int y, unsigned len,
							const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint16 alpha = pattern->HighColor().alpha * cover;
		if (alpha == 255 * 255) {
			// Optimization: 32-bit write for full opacity
			uint32 v;
			uint8* p8 = (uint8*)&v;
			p8[0] = c.b;
			p8[1] = c.g;
			p8[2] = c.r;
			p8[3] = 255;
			uint32* p32 = (uint32*)(buffer->row_ptr(y)) + x;
			do {
				*p32 = v;
				p32++;
				x++;
			} while(--len);
		} else {
			uint8* p = buffer->row_ptr(y) + (x << 2);
			if (len < 4) {
				// Short line - use normal blending
				do {
					BlendPolicy::Blend(p, c.r, c.g, c.b, alpha);
					x++;
					p += 4;
				} while(--len);
			} else {
				// Long line - use optimized blend_line32
				uint8 alpha8 = alpha >> 8;
				blend_line32(p, len, c.r, c.g, c.b, alpha8);
			}
		}
	}

	// blend_solid_hspan - solid pattern with 16-bit alpha
	static void blend_solid_hspan(int x, int y, unsigned len,
								  const color_type& c, const uint8* covers,
								  agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint8 hAlpha = pattern->HighColor().alpha;
		do {
			uint16 alpha = hAlpha * *covers;
			if (alpha) {
				if (alpha == 255 * 255) {
					BlendPolicy::Assign(p, c.r, c.g, c.b, 255);
				} else {
					BlendPolicy::Blend(p, c.r, c.g, c.b, alpha);
				}
			}
			covers++;
			p += 4;
			x++;
		} while(--len);
	}

	// blend_solid_vspan - solid pattern with 16-bit alpha
	static void blend_solid_vspan(int x, int y, unsigned len,
								  const color_type& c, const uint8* covers,
								  agg_buffer* buffer, const PatternHandler* pattern)
	{
		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint8 hAlpha = pattern->HighColor().alpha;
		do {
			uint16 alpha = hAlpha * *covers;
			if (alpha) {
				if (alpha == 255 * 255) {
					BlendPolicy::Assign(p, c.r, c.g, c.b, 255);
				} else {
					BlendPolicy::Blend(p, c.r, c.g, c.b, alpha);
				}
			}
			covers++;
			p += buffer->stride();
			y++;
		} while(--len);
	}

	// blend_solid_hspan_subpix - solid pattern with 16-bit alpha and subpixel rendering
	static void blend_solid_hspan_subpix(int x, int y, unsigned len,
										 const color_type& c, const uint8* covers,
										 agg_buffer* buffer, const PatternHandler* pattern)
	{
		extern bool gSubpixelOrderingRGB;
		const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
		const int subpixelM = 1;
		const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;

		uint8* p = buffer->row_ptr(y) + (x << 2);
		uint8 hAlpha = pattern->HighColor().alpha;
		do {
			// Calculate 16-bit alpha for each RGB subpixel
			uint16 alphaRed = hAlpha * covers[subpixelL];
			uint16 alphaGreen = hAlpha * covers[subpixelM];
			uint16 alphaBlue = hAlpha * covers[subpixelR];

			// Note: BlendSubpix expects alphaBlue, alphaGreen, alphaRed order
			BlendPolicy::BlendSubpix(p, c.r, c.g, c.b,
				alphaBlue, alphaGreen, alphaRed);
			covers += 3;
			p += 4;
			x++;
			len -= 3;
		} while (len);
	}
};

// SolidPatternFilteredModeImpl: Optimized version for solid patterns
// Only for B_OP_OVER (checks IsSolidLow and uses color from AGG parameter)
template<typename BlendPolicy>
struct SolidPatternFilteredModeImpl {
	// blend_pixel - solid pattern optimized
	static void blend_pixel(int x, int y, const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		if (pattern->IsSolidLow())
			return;

		uint8* p = buffer->row_ptr(y) + (x << 2);
		if (cover == 255) {
			BlendPolicy::Assign(p, c.r, c.g, c.b);
		} else {
			BlendPolicy::Blend(p, c.r, c.g, c.b, cover);
		}
	}

	// blend_hline - solid pattern optimized with 32-bit write
	static void blend_hline(int x, int y, unsigned len,
							const color_type& c, uint8 cover,
							agg_buffer* buffer, const PatternHandler* pattern)
	{
		if (pattern->IsSolidLow())
			return;

		if (cover == 255) {
			// Optimization: 32-bit write for full opacity
			uint32 v;
			uint8* p8 = (uint8*)&v;
			p8[0] = (uint8)c.b;
			p8[1] = (uint8)c.g;
			p8[2] = (uint8)c.r;
			p8[3] = 255;
			uint32* p32 = (uint32*)(buffer->row_ptr(y)) + x;
			do {
				*p32 = v;
				p32++;
				x++;
			} while(--len);
		} else {
			uint8* p = buffer->row_ptr(y) + (x << 2);
			do {
				BlendPolicy::Blend(p, c.r, c.g, c.b, cover);
				x++;
				p += 4;
			} while(--len);
		}
	}

	// blend_solid_hspan - solid pattern optimized
	static void blend_solid_hspan(int x, int y, unsigned len,
								  const color_type& c, const uint8* covers,
								  agg_buffer* buffer, const PatternHandler* pattern)
	{
		if (pattern->IsSolidLow())
			return;

		uint8* p = buffer->row_ptr(y) + (x << 2);
		do {
			if (*covers) {
				if (*covers == 255) {
					BlendPolicy::Assign(p, c.r, c.g, c.b);
				} else {
					BlendPolicy::Blend(p, c.r, c.g, c.b, *covers);
				}
			}
			covers++;
			p += 4;
			x++;
		} while(--len);
	}

	// blend_solid_vspan - solid pattern optimized
	static void blend_solid_vspan(int x, int y, unsigned len,
								  const color_type& c, const uint8* covers,
								  agg_buffer* buffer, const PatternHandler* pattern)
	{
		if (pattern->IsSolidLow())
			return;

		uint8* p = buffer->row_ptr(y) + (x << 2);
		do {
			if (*covers) {
				if (*covers == 255) {
					BlendPolicy::Assign(p, c.r, c.g, c.b);
				} else {
					BlendPolicy::Blend(p, c.r, c.g, c.b, *covers);
				}
			}
			covers++;
			p += buffer->stride();
			y++;
		} while(--len);
	}

	// blend_solid_hspan_subpix - solid pattern optimized with subpixel rendering
	static void blend_solid_hspan_subpix(int x, int y, unsigned len,
										 const color_type& c, const uint8* covers,
										 agg_buffer* buffer, const PatternHandler* pattern)
	{
		if (pattern->IsSolidLow())
			return;

		extern bool gSubpixelOrderingRGB;
		const int subpixelL = gSubpixelOrderingRGB ? 2 : 0;
		const int subpixelM = 1;
		const int subpixelR = gSubpixelOrderingRGB ? 0 : 2;

		uint8* p = buffer->row_ptr(y) + (x << 2);
		do {
			BlendPolicy::BlendSubpix(p, c.r, c.g, c.b,
				covers[subpixelL], covers[subpixelM], covers[subpixelR]);
			covers += 3;
			p += 4;
			x++;
			len -= 3;
		} while (len);
	}
};

#endif // DRAWING_MODE_TEMPLATE_H
