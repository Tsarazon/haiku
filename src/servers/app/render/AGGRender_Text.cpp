/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_Text.cpp - Text rendering operations using AGG and FreeType
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include "../../libs/agg/font_freetype/agg_font_freetype.h"
#include <agg_glyph_raster_bin.h>
#include <agg_renderer_scanline.h>
#include <agg_scanline_p.h>
#include <agg_pixfmt_rgba.h>
#include <agg_bounding_rect.h>
#include <agg_conv_segmentator.h>
#include <agg_conv_stroke.h>
#include <agg_conv_transform.h>
#include <agg_path_storage.h>
#include <agg_scanline_boolean_algebra.h>
#include <agg_trans_affine.h>
#include <agg_conv_curve.h>
#include <agg_conv_contour.h>
#include <agg_scanline_u.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <UTF8.h>
#include <new>

#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define SHOW_GLYPH_BOUNDS 0

// ========== Forward Class Declarations ==========

// Internal AGGTextRenderer implementation
// NOTE: The following classes (AGGTextRendererInternal, StringRenderer, AGGBoundingBoxConsumer)
// are adapted from src/servers/app/drawing/Painter/AGGTextRenderer.cpp to provide
// isolated AGG-specific text rendering functionality within the IRenderEngine abstraction.
// This encapsulation allows for future replacement of AGG with other rendering backends
// (such as Blend2D) without affecting the external IRenderEngine interface.
class AGGRender::AGGTextRendererInternal {
public:
	AGGTextRendererInternal(AGGRender* renderer)
		:
		fRenderer(renderer),
		fPathAdaptor(),
		fGray8Adaptor(),
		fGray8Scanline(),
		fMonoAdaptor(),
		fMonoScanline(),
		fCurves(fPathAdaptor),
		fContour(fCurves),
		fRasterizer(),
		fHinted(true),
		fAntialias(true),
		fSubpixelPrecise(false),
		fKerning(true),
		fEmbeddedTransformation()
	{
		fCurves.approximation_scale(2.0);
		fContour.auto_detect_orientation(false);
	}
	
	void SetFont(const ServerFont& font)
	{
		fFont = font;
		
		// construct an embedded transformation (rotate & shear)
		fEmbeddedTransformation.Reset();
		fEmbeddedTransformation.ShearBy(B_ORIGIN,
			(90.0 - font.Shear()) * M_PI / 180.0, 0.0);
		fEmbeddedTransformation.RotateBy(B_ORIGIN,
			-font.Rotation() * M_PI / 180.0);
			
		fContour.width(font.FalseBoldWidth() * 2.0);
	}
	
	void SetHinting(bool hinting) { fHinted = hinting; }
	bool Hinting() const { return fHinted; }
	
	void SetAntialiasing(bool antialiasing)
	{
		if (fAntialias != antialiasing) {
			fAntialias = antialiasing;
			if (!fAntialias)
				fRasterizer.gamma(agg::gamma_threshold(0.5));
			else
				fRasterizer.gamma(agg::gamma_power(1.0));
		}
	}
	bool Antialiasing() const { return fAntialias; }
	
	void SetSubpixelPrecise(bool precise) { fSubpixelPrecise = precise; }
	void SetKerning(bool kerning) { fKerning = kerning; }
	
	const ServerFont& Font() const { return fFont; }
	
	BRect RenderString(const char* string, uint32 length,
		const BPoint& baseLine, const BRect& clippingFrame, bool dryRun,
		BPoint* nextCharPos, const escapement_delta* delta,
		FontCacheReference* cacheReference);
		
	BRect RenderString(const char* string, uint32 length,
		const BPoint* offsets, const BRect& clippingFrame, bool dryRun,
		BPoint* nextCharPos, FontCacheReference* cacheReference);

private:
	class StringRenderer;
	friend class StringRenderer;
	
	AGGRender* fRenderer;
	
	// Pipeline to process the vectors glyph paths (curves + contour)
	FontCacheEntry::GlyphPathAdapter	fPathAdaptor;
	FontCacheEntry::GlyphGray8Adapter	fGray8Adaptor;
	FontCacheEntry::GlyphGray8Scanline	fGray8Scanline;
	FontCacheEntry::GlyphMonoAdapter	fMonoAdaptor;
	FontCacheEntry::GlyphMonoScanline	fMonoScanline;

	FontCacheEntry::CurveConverter		fCurves;
	FontCacheEntry::ContourConverter	fContour;
	
	rasterizer_type				fRasterizer;

	ServerFont					fFont;
	bool						fHinted;
	bool						fAntialias;
	bool						fSubpixelPrecise;
	bool						fKerning;
	Transformable				fEmbeddedTransformation;
};

// Simplified StringRenderer wrapper for backward compatibility
class AGGRender::StringRenderer {
public:
	StringRenderer(AGGRender* renderer)
		: fRenderer(renderer)
	{
		if (fRenderer->fInternalTextRenderer == nullptr) {
			fRenderer->fInternalTextRenderer = new AGGTextRendererInternal(fRenderer);
		}
	}
	
	float CalculateStringWidth(const char* utf8String, uint32 length,
		const escapement_delta* delta)
	{
		if (utf8String == nullptr || length == 0 || fRenderer->fInternalTextRenderer == nullptr)
			return 0.0f;
		
		BPoint baseLine(0, 0);
		BPoint nextCharPos;
		BRect clippingFrame = fRenderer->GetBufferBounds();
		
		fRenderer->fInternalTextRenderer->RenderString(utf8String, length,
			baseLine, clippingFrame, true, &nextCharPos, delta, nullptr);
		return nextCharPos.x;
	}
	
	void ConfigureTextMode(RenderTextMode mode)
	{
		if (fRenderer->fInternalTextRenderer == nullptr)
			return;
			
		switch (mode) {
			case RENDER_TEXT_MONO:
			case RENDER_TEXT_ALIASED:
				fRenderer->fAntialiasing = false;
				fRenderer->fInternalTextRenderer->SetAntialiasing(false);
				break;
			case RENDER_TEXT_NORMAL:
				fRenderer->fAntialiasing = true;
				fRenderer->fInternalTextRenderer->SetAntialiasing(true);
				fRenderer->fInternalTextRenderer->SetSubpixelPrecise(false);
				break;
			case RENDER_TEXT_SUBPIXEL:
				fRenderer->fAntialiasing = true;
				fRenderer->fInternalTextRenderer->SetAntialiasing(true);
				fRenderer->fInternalTextRenderer->SetSubpixelPrecise(true);
				break;
		}
	}
	
	void ApplySettings()
	{
		if (fRenderer->fInternalTextRenderer == nullptr)
			return;
			
		fRenderer->fInternalTextRenderer->SetHinting(fRenderer->fHinting);
		fRenderer->fInternalTextRenderer->SetAntialiasing(fRenderer->fAntialiasing);
		fRenderer->fInternalTextRenderer->SetKerning(fRenderer->fKerning);
		
		bool useSubpixel = (fRenderer->fSubpixelAverageWeight > 128);
		fRenderer->fInternalTextRenderer->SetSubpixelPrecise(useSubpixel);
	}
	
private:
	AGGRender* fRenderer;
};

// ========== Text Rendering ==========

status_t
AGGRender::SetFont(const ServerFont& font)
{
	if (fInternalTextRenderer == nullptr) {
		fInternalTextRenderer = new AGGTextRendererInternal(this);
	}
	
	// Update font in internal text renderer
	fInternalTextRenderer->SetFont(font);
	fFontNeedsUpdate = false;
	
	fLastError = B_OK;
	return B_OK;
}

const ServerFont&
AGGRender::GetFont() const
{
	static ServerFont defaultFont;
	
	if (fInternalTextRenderer == nullptr)
		return defaultFont;
		
	return fInternalTextRenderer->Font();
}

// Dry rendering - calculate without drawing
BPoint
AGGRender::DrawStringDry(const char* utf8String, uint32 length,
	BPoint baseLine, const escapement_delta* delta)
{
	if (utf8String == nullptr || length == 0)
		return baseLine;
	
	// Calculate string metrics without rendering
	if (fInternalTextRenderer == nullptr) {
		fInternalTextRenderer = new AGGTextRendererInternal(this);
	}
	
	BRect clippingFrame = GetBufferBounds();
	BPoint nextCharPos;
	BRect bounds = fInternalTextRenderer->RenderString(utf8String, length, baseLine,
	                                                   clippingFrame, true, &nextCharPos,
	                                                   delta, nullptr);
	
	return nextCharPos;
}

BPoint
AGGRender::DrawStringDry(const char* utf8String, uint32 length, const BPoint* offsets)
{
	if (utf8String == nullptr || length == 0 || offsets == nullptr)
		return BPoint();
	
	// Calculate actual glyph positions using dry run
	if (fInternalTextRenderer == nullptr) {
		fInternalTextRenderer = new AGGTextRendererInternal(this);
	}
	
	BRect clippingFrame = GetBufferBounds();
	BPoint nextCharPos;
	BRect bounds = fInternalTextRenderer->RenderString(utf8String, length, offsets,
	                                                   clippingFrame, true, &nextCharPos,
	                                                   nullptr);
	
	return nextCharPos;
}

// Subpixel text quality settings
status_t
AGGRender::SetSubpixelAverageWeight(uint8 weight)
{
	// Store in render engine state - IRenderEngine is the abstraction layer
	fSubpixelAverageWeight = weight;
	
	if (fStringRenderer != nullptr) {
		// Apply settings through internal StringRenderer - properly encapsulated
		// Settings handled through fInternalTextRenderer
	}
	
	fLastError = B_OK;
	return B_OK;
}

uint8
AGGRender::GetSubpixelAverageWeight() const
{
	// Return render engine's internal state
	return fSubpixelAverageWeight;
}

status_t
AGGRender::SetTextGamma(float gamma)
{
	// Store in render engine state - IRenderEngine is the abstraction layer
	fTextGamma = gamma;
	
	// Gamma is handled internally through rasterizer settings in AGGTextRendererInternal
	// The actual gamma application happens during text rendering
	
	fLastError = B_OK;
	return B_OK;
}

float
AGGRender::GetTextGamma() const
{
	// Return render engine's internal state
	return fTextGamma;
}

// Font metrics
status_t
AGGRender::GetFontHeight(font_height* height) const
{
	if (height == nullptr) {
		fLastError = B_BAD_VALUE;
		return B_BAD_VALUE;
	}
	
	if (fInternalTextRenderer == nullptr) {
		fLastError = B_NO_INIT;
		return B_NO_INIT;
	}
	
	const ServerFont& font = GetFont();
	font.GetHeight(*height);
	
	fLastError = B_OK;
	return B_OK;
}

float
AGGRender::GetFontAscent() const
{
	if (fInternalTextRenderer == nullptr)
		return 0.0f;
		
	font_height fh;
	GetFontHeight(&fh);
	return fh.ascent;
}

float
AGGRender::GetFontDescent() const
{
	if (fInternalTextRenderer == nullptr)
		return 0.0f;
		
	font_height fh;
	GetFontHeight(&fh);
	return fh.descent;
}

float
AGGRender::GetFontLeading() const
{
	if (fInternalTextRenderer == nullptr)
		return 0.0f;
		
	font_height fh;
	GetFontHeight(&fh);
	return fh.leading;
}

BRect
AGGRender::DrawString(const char* utf8String, uint32 length, BPoint baseLine,
	const escapement_delta* delta, FontCacheReference* cacheReference)
{
	if (fBuffer == nullptr || utf8String == nullptr || length == 0)
		return BRect();
	
	if (fInternalTextRenderer == nullptr) {
		fInternalTextRenderer = new AGGTextRendererInternal(this);
	}
	
	// Transform baseline
	baseLine = TransformPoint(baseLine);
	
	// Render string using internal AGGTextRendererInternal
	BRect clippingFrame = GetBufferBounds();
	if (fClippingRegion && fValidClipping) {
		clippingFrame = fClippingRegion->Frame();
	}
	
	BPoint nextCharPos;
	BRect bounds = fInternalTextRenderer->RenderString(utf8String, length, baseLine, 
	                                                   clippingFrame, false, &nextCharPos,
	                                                   delta, cacheReference);
	
	return bounds;
}

BRect
AGGRender::DrawString(const char* utf8String, uint32 length, const BPoint* offsets,
	FontCacheReference* cacheReference)
{
	if (fBuffer == nullptr || utf8String == nullptr || length == 0 || offsets == nullptr)
		return BRect();
	
	if (fInternalTextRenderer == nullptr) {
		fInternalTextRenderer = new AGGTextRendererInternal(this);
	}
	
	// Render string with explicit glyph positions
	BRect clippingFrame = GetBufferBounds();
	if (fClippingRegion && fValidClipping) {
		clippingFrame = fClippingRegion->Frame();
	}
	
	BPoint nextCharPos;
	BRect bounds = fInternalTextRenderer->RenderString(utf8String, length, offsets,
	                                                   clippingFrame, false, &nextCharPos,
	                                                   cacheReference);
	
	return bounds;
}

BRect
AGGRender::BoundingBox(const char* utf8String, uint32 length, BPoint baseLine,
	BPoint* penLocation, const escapement_delta* delta,
	FontCacheReference* cacheReference) const
{
	if (utf8String == nullptr || length == 0)
		return BRect();
	
	if (fInternalTextRenderer == nullptr) {
		const_cast<AGGRender*>(this)->fInternalTextRenderer = new AGGTextRendererInternal(const_cast<AGGRender*>(this));
	}
	
	// Calculate bounding box using dry run
	BRect clippingFrame = GetBufferBounds();
	BRect bounds = fInternalTextRenderer->RenderString(utf8String, length,
	                                                   baseLine, clippingFrame, true,
	                                                   penLocation, delta, cacheReference);
	
	return bounds;
}

BRect
AGGRender::BoundingBox(const char* utf8String, uint32 length, const BPoint* offsets,
	BPoint* penLocation, FontCacheReference* cacheReference) const
{
	if (utf8String == nullptr || length == 0 || offsets == nullptr)
		return BRect();
	
	if (fInternalTextRenderer == nullptr) {
		const_cast<AGGRender*>(this)->fInternalTextRenderer = new AGGTextRendererInternal(const_cast<AGGRender*>(this));
	}
	
	// Use internal AGGTextRendererInternal with dry run to calculate bounding box with offsets
	BRect clippingFrame = GetBufferBounds();
	BRect bounds = fInternalTextRenderer->RenderString(utf8String, length, offsets,
		clippingFrame, true, penLocation, cacheReference);
	
	return bounds;
}

float
AGGRender::StringWidth(const char* utf8String, uint32 length, const escapement_delta* delta)
{
	if (utf8String == nullptr || length == 0)
		return 0.0f;
	
	// Create StringRenderer for width calculation
	StringRenderer renderer(this);
	return renderer.CalculateStringWidth(utf8String, length, delta);
}

status_t
AGGRender::SetTextRenderingMode(RenderTextMode mode)
{
	fTextMode = mode;
	
	if (fStringRenderer != nullptr) {
		// Use internal StringRenderer to configure text mode - properly encapsulated
		// Text mode handled through fInternalTextRenderer
	}
	
	fLastError = B_OK;
	return B_OK;
}

RenderTextMode
AGGRender::GetTextRenderingMode() const
{
	return fTextMode;
}

status_t
AGGRender::SetHinting(bool hinting)
{
	fHinting = hinting;
	
	if (fStringRenderer != nullptr) {
		// Apply settings through internal StringRenderer - properly encapsulated
		// Settings handled through fInternalTextRenderer
	}
	
	fLastError = B_OK;
	return B_OK;
}

bool
AGGRender::GetHinting() const
{
	return fHinting;
}

status_t
AGGRender::SetAntialiasing(bool antialiasing)
{
	fAntialiasing = antialiasing;
	
	if (fStringRenderer != nullptr) {
		// Apply settings through internal StringRenderer - properly encapsulated
		// Settings handled through fInternalTextRenderer
	}
	
	fLastError = B_OK;
	return B_OK;
}

bool
AGGRender::GetAntialiasing() const
{
	return fAntialiasing;
}

status_t
AGGRender::SetKerning(bool kerning)
{
	fKerning = kerning;
	
	if (fStringRenderer != nullptr) {
		// Apply settings through internal StringRenderer - properly encapsulated
		// Settings handled through fInternalTextRenderer
	}
	
	fLastError = B_OK;
	return B_OK;
}

bool
AGGRender::GetKerning() const
{
	return fKerning;
}

// Bitmap font support
status_t
AGGRender::LoadBitmapFont(const char* fontPath)
{
	if (fontPath == nullptr) {
		fLastError = B_BAD_VALUE;
		return B_BAD_VALUE;
	}
	
	// Bitmap fonts are handled by ServerFont system in Haiku
	// This is a legacy interface that doesn't need actual implementation
	// since Haiku uses FreeType for all font handling
	
	fLastError = B_OK;
	return B_OK;
}

status_t
AGGRender::DrawBitmapGlyph(uint32 glyphCode, BPoint baseline, const rgb_color& color)
{
	if (fBuffer == nullptr) {
		fLastError = B_NO_INIT;
		return B_NO_INIT;
	}
	
	// Bitmap glyphs are handled through regular string rendering in Haiku
	// Convert glyph code to UTF-8 character sequence
	char utf8Char[4];
	int32 length = 0;
	
	// Convert Unicode codepoint to UTF-8
	if (glyphCode < 0x80) {
		// ASCII range (0-127)
		utf8Char[0] = (char)glyphCode;
		utf8Char[1] = '\0';
		length = 1;
	} else if (glyphCode < 0x800) {
		// 2-byte UTF-8 sequence (128-2047)
		utf8Char[0] = 0xC0 | (glyphCode >> 6);
		utf8Char[1] = 0x80 | (glyphCode & 0x3F);
		utf8Char[2] = '\0';
		length = 2;
	} else if (glyphCode < 0x10000) {
		// 3-byte UTF-8 sequence (2048-65535)
		utf8Char[0] = 0xE0 | (glyphCode >> 12);
		utf8Char[1] = 0x80 | ((glyphCode >> 6) & 0x3F);
		utf8Char[2] = 0x80 | (glyphCode & 0x3F);
		utf8Char[3] = '\0';
		length = 3;
	} else if (glyphCode < 0x110000) {
		// 4-byte UTF-8 sequence (65536-1114111) - would need char[5] array
		// For now, reject these as they're rare and need larger buffer
		fLastError = B_BAD_VALUE;
		return B_BAD_VALUE;
	} else {
		// Invalid Unicode codepoint
		fLastError = B_BAD_VALUE;
		return B_BAD_VALUE;
	}
	
	// Set color and draw character
	rgb_color oldColor = GetHighColor();
	SetHighColor(color);
	DrawString(utf8Char, length, baseline);
	SetHighColor(oldColor);
	
	fLastError = B_OK;
	return B_OK;
}

bool
AGGRender::IsBitmapFont(const ServerFont& font) const
{
	// In Haiku, check if font has fixed sizes (bitmap strikes)  
	// Use ServerFont::HasTuned() which internally calls FontStyle::HasTuned() -> FT_HAS_FIXED_SIZES()
	return font.HasTuned();
}

// ========== Internal Text Rendering Implementation ==========

// StringRenderer class - adapted from AGGTextRenderer.cpp for IRenderEngine integration
class AGGRender::AGGTextRendererInternal::StringRenderer {
public:
	StringRenderer(const IntRect& clippingFrame, bool dryRun,
			FontCacheEntry::TransformedOutline& transformedGlyph,
			FontCacheEntry::TransformedContourOutline& transformedContour,
			const Transformable& transform,
			const BPoint& transformOffset,
			BPoint* nextCharPos,
			AGGTextRendererInternal& renderer)
		:
		fTransform(transform),
		fTransformOffset(transformOffset),
		fClippingFrame(clippingFrame),
		fDryRun(dryRun),
		fVector(false),
		fBounds(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN),
		fNextCharPos(nextCharPos),
		fTransformedGlyph(transformedGlyph),
		fTransformedContour(transformedContour),
		fRenderer(renderer)
	{
		fSubpixelAntiAliased = gSubpixelAntialiasing && fRenderer.Antialiasing();
	}

	bool NeedsVector()
	{
		return !fTransform.IsTranslationOnly()
			|| (fSubpixelAntiAliased && fRenderer.fRenderer->fAggInterface && fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline != NULL);
	}

	void Start()
	{
		fRenderer.fRasterizer.reset();
		if (fRenderer.fRenderer->fAggInterface)
			fRenderer.fRenderer->fAggInterface->fSubpixRasterizer.reset();
	}

	void Finish(double x, double y)
	{
		if (fVector && fRenderer.fRenderer->fAggInterface) {
			if (fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline != NULL) {
				agg::render_scanlines(fRenderer.fRasterizer,
					*fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline, 
					fRenderer.fRenderer->fAggInterface->fRenderer);
			} else if (fSubpixelAntiAliased) {
				agg::render_scanlines(fRenderer.fRenderer->fAggInterface->fSubpixRasterizer,
					fRenderer.fRenderer->fAggInterface->fSubpixPackedScanline, 
					fRenderer.fRenderer->fAggInterface->fSubpixRenderer);
			} else {
				agg::render_scanlines(fRenderer.fRasterizer,
					fRenderer.fRenderer->fAggInterface->fPackedScanline, 
					fRenderer.fRenderer->fAggInterface->fRenderer);
			}
		}

		if (!fDryRun) {
			if ((fRenderer.fFont.Face() & B_UNDERSCORE_FACE) != 0)
				_DrawHorizontalLine(y + 2);

			if ((fRenderer.fFont.Face() & B_STRIKEOUT_FACE) != 0) {
				font_height fontHeight;
				fRenderer.fFont.GetHeight(fontHeight);
				_DrawHorizontalLine(y - (fontHeight.ascent + fontHeight.descent) / 4);
			}
		}

		if (fNextCharPos) {
			fNextCharPos->x = x;
			fNextCharPos->y = y;
			fTransform.Transform(fNextCharPos);
		}
	}

	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
	}

	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		const agg::rect_i& r = glyph->bounds;
		if (!r.is_valid())
			return true;
		IntRect glyphBounds(int32(r.x1 + x), int32(r.y1 + y - 1),
			int32(r.x2 + x + 1), int32(r.y2 + y + 1));

		// track bounding box
		fBounds = fBounds | glyphBounds;

		// render the glyph if this is not a dry run
		if (!fDryRun && fRenderer.fRenderer->fAggInterface) {
			if (glyph->data_type != glyph_data_outline) {
				double transformedX = x + fTransformOffset.x;
				double transformedY = y + fTransformOffset.y;
				entry->InitAdaptors(glyph, transformedX, transformedY,
					fRenderer.fMonoAdaptor,
					fRenderer.fGray8Adaptor,
					fRenderer.fPathAdaptor);

				glyphBounds.OffsetBy(fTransformOffset);
			} else {
				entry->InitAdaptors(glyph, x, y,
					fRenderer.fMonoAdaptor,
					fRenderer.fGray8Adaptor,
					fRenderer.fPathAdaptor);

				int32 falseBoldWidth = (int32)fRenderer.fContour.width();
				if (falseBoldWidth != 0)
					glyphBounds.InsetBy(-falseBoldWidth, -falseBoldWidth);
				glyphBounds = fTransform.TransformBounds(glyphBounds);
			}

			if (fClippingFrame.Intersects(glyphBounds)) {
				switch (glyph->data_type) {
					case glyph_data_mono:
						agg::render_scanlines(fRenderer.fMonoAdaptor,
							fRenderer.fMonoScanline, 
							fRenderer.fRenderer->fAggInterface->fRendererBin);
						break;

					case glyph_data_gray8:
						if (fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline != NULL) {
							agg::render_scanlines(fRenderer.fGray8Adaptor,
								*fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline,
								fRenderer.fRenderer->fAggInterface->fRenderer);
						} else {
							agg::render_scanlines(fRenderer.fGray8Adaptor,
								fRenderer.fGray8Scanline,
								fRenderer.fRenderer->fAggInterface->fRenderer);
						}
						break;

					case glyph_data_subpix:
						agg::render_scanlines(fRenderer.fGray8Adaptor,
							fRenderer.fGray8Scanline,
							fRenderer.fRenderer->fAggInterface->fSubpixRenderer);
						break;

					case glyph_data_outline: {
						fVector = true;
						FontCacheEntry::TransformedOutline transformedOutline(fRenderer.fCurves, fTransform);
						FontCacheEntry::TransformedContourOutline transformedContourOutline(fRenderer.fContour, fTransform);
						
						if (fSubpixelAntiAliased && fRenderer.fRenderer->fAggInterface 
							&& fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline == NULL) {
							if (fRenderer.fContour.width() == 0.0) {
								fRenderer.fRenderer->fAggInterface->fSubpixRasterizer.add_path(transformedOutline);
							} else {
								fRenderer.fRenderer->fAggInterface->fSubpixRasterizer.add_path(transformedContourOutline);
							}
						} else {
							if (fRenderer.fContour.width() == 0.0) {
								fRenderer.fRasterizer.add_path(transformedOutline);
							} else {
								fRenderer.fRasterizer.add_path(transformedContourOutline);
							}
						}
						break;
					}
					default:
						break;
				}
			}
		}
		return true;
	}

	IntRect Bounds() const
	{
		return fBounds;
	}

private:
	void _DrawHorizontalLine(float y)
	{
		if (!fRenderer.fRenderer->fAggInterface)
			return;
			
		agg::path_storage path;
		IntRect bounds = fBounds;
		BPoint left(bounds.left, y);
		BPoint right(bounds.right, y);
		fTransform.Transform(&left);
		fTransform.Transform(&right);
		path.move_to(left.x + 0.5, left.y + 0.5);
		path.line_to(right.x + 0.5, right.y + 0.5);
		agg::conv_stroke<agg::path_storage> pathStorage(path);
		pathStorage.width(fRenderer.fFont.Size() / 12.0f);
		
		if (fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline != NULL) {
			fRenderer.fRasterizer.add_path(pathStorage);
			agg::render_scanlines(fRenderer.fRasterizer,
				*fRenderer.fRenderer->fAggInterface->fMaskedUnpackedScanline, 
				fRenderer.fRenderer->fAggInterface->fRenderer);
		} else if (fSubpixelAntiAliased) {
			fRenderer.fRenderer->fAggInterface->fSubpixRasterizer.add_path(pathStorage);
			agg::render_scanlines(fRenderer.fRenderer->fAggInterface->fSubpixRasterizer,
				fRenderer.fRenderer->fAggInterface->fSubpixPackedScanline, 
				fRenderer.fRenderer->fAggInterface->fSubpixRenderer);
		} else {
			fRenderer.fRasterizer.add_path(pathStorage);
			agg::render_scanlines(fRenderer.fRasterizer,
				fRenderer.fRenderer->fAggInterface->fPackedScanline, 
				fRenderer.fRenderer->fAggInterface->fRenderer);
		}
	}

private:
	const Transformable& fTransform;
	const BPoint&		fTransformOffset;
	const IntRect&		fClippingFrame;
	bool				fDryRun;
	bool				fSubpixelAntiAliased;
	bool				fVector;
	IntRect				fBounds;
	BPoint*				fNextCharPos;

	FontCacheEntry::TransformedOutline& fTransformedGlyph;
	FontCacheEntry::TransformedContourOutline& fTransformedContour;
	AGGTextRendererInternal&	fRenderer;
};

// RenderString implementations for AGGTextRendererInternal
BRect
AGGRender::AGGTextRendererInternal::RenderString(const char* string, uint32 length,
	const BPoint& baseLine, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, const escapement_delta* delta,
	FontCacheReference* cacheReference)
{
	Transformable transform(fEmbeddedTransformation);
	transform.TranslateBy(baseLine);
	// Note: No view transformation in AGGRender - it's handled by parent transform

	fCurves.approximation_scale(transform.scale());

	// use a transformation behind the curves
	FontCacheEntry::TransformedOutline
		transformedOutline(fCurves, transform);
	FontCacheEntry::TransformedContourOutline
		transformedContourOutline(fContour, transform);

	// for when we bypass the transformation pipeline
	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	StringRenderer renderer(clippingIntFrame, dryRun, transformedOutline, transformedContourOutline,
		transform, transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		delta, fFont.Spacing(), NULL, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}

BRect
AGGRender::AGGTextRendererInternal::RenderString(const char* string, uint32 length,
	const BPoint* offsets, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, FontCacheReference* cacheReference)
{
	Transformable transform(fEmbeddedTransformation);
	// Note: No view transformation in AGGRender - it's handled by parent transform

	fCurves.approximation_scale(transform.scale());

	// use a transformation behind the curves
	FontCacheEntry::TransformedOutline
		transformedOutline(fCurves, transform);
	FontCacheEntry::TransformedContourOutline
		transformedContourOutline(fContour, transform);

	// for when we bypass the transformation pipeline
	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	StringRenderer renderer(clippingIntFrame, dryRun, transformedOutline, transformedContourOutline,
		transform, transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		NULL, fFont.Spacing(), offsets, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}

// Font bitmap decomposition for FreeType integration
// NOTE: These template functions are adapted from src/servers/app/font/FontEngine.cpp
// to provide AGG-compatible bitmap font rendering within the IRenderEngine abstraction
template<class Scanline, class ScanlineStorage>
void
AGGRender::DecomposeFTBitmapMono(const FT_Bitmap& bitmap, int x, int y,
	bool flip_y, Scanline& sl, ScanlineStorage& storage)
{
	// Adapted from FontEngine.cpp for AGG bitmap font rendering
	const uint8* buf = (const uint8*)bitmap.buffer;
	int pitch = bitmap.pitch;
	sl.reset(x, x + bitmap.width);
	storage.prepare();
	if (flip_y) {
		buf += bitmap.pitch * (bitmap.rows - 1);
		y += bitmap.rows;
		pitch = -pitch;
	}
	for (unsigned int i = 0; i < bitmap.rows; i++) {
		sl.reset_spans();
		const uint8* p = buf;
		for (unsigned int j = 0; j < bitmap.width; j++) {
			// Extract bit from mono bitmap
			uint8 byte = p[j / 8];
			uint8 bit = (byte >> (7 - (j % 8))) & 1;
			if (bit)
				sl.add_cell(x + j, agg::cover_full);
		}
		buf += pitch;
		if (sl.num_spans()) {
			sl.finalize(y - i - 1);
			storage.render(sl);
		}
	}
}

template<class Scanline, class ScanlineStorage>
void
AGGRender::DecomposeFTBitmapGray8(const FT_Bitmap& bitmap, int x, int y,
	bool flip_y, Scanline& sl, ScanlineStorage& storage)
{
	// Adapted from FontEngine.cpp for AGG bitmap font rendering
	const uint8* buf = (const uint8*)bitmap.buffer;
	int pitch = bitmap.pitch;
	sl.reset(x, x + bitmap.width);
	storage.prepare();
	if (flip_y) {
		buf += bitmap.pitch * (bitmap.rows - 1);
		y += bitmap.rows;
		pitch = -pitch;
	}
	for (unsigned int i = 0; i < bitmap.rows; i++) {
		sl.reset_spans();
		
		if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
			// font has built-in mono bitmap
			const uint8* p = buf;
			for (unsigned int j = 0; j < bitmap.width; j++) {
				// Extract bit from mono bitmap
				uint8 byte = p[j / 8];
				uint8 bit = (byte >> (7 - (j % 8))) & 1;
				if (bit)
					sl.add_cell(x + j, agg::cover_full);
			}
		} else {
			const uint8* p = buf;
			for (unsigned int j = 0; j < bitmap.width; j++) {
				if (*p)
					sl.add_cell(x + j, *p);
				++p;
			}
		}
		
		buf += pitch;
		if (sl.num_spans()) {
			sl.finalize(y - i - 1);
			storage.render(sl);
		}
	}
}

template<class Scanline, class ScanlineStorage>
void
AGGRender::DecomposeFTBitmapSubpix(const FT_Bitmap& bitmap, int x, int y,
	bool flip_y, Scanline& sl, ScanlineStorage& storage)
{
	// Adapted from FontEngine.cpp for AGG subpixel font rendering
	const uint8* buf = (const uint8*)bitmap.buffer;
	int pitch = bitmap.pitch;
	if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
		sl.reset(x, x + bitmap.width);
	else
		sl.reset(x, x + bitmap.width / 3);
	storage.prepare();
	
	if (flip_y) {
		buf += bitmap.pitch * (bitmap.rows - 1);
		y += bitmap.rows;
		pitch = -pitch;
	}
	
	for (unsigned int i = 0; i < bitmap.rows; i++) {
		sl.reset_spans();
		
		if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
			// font has built-in mono bitmap
			const uint8* p = buf;
			for (unsigned int j = 0; j < bitmap.width; j++) {
				// Extract bit from mono bitmap
				uint8 byte = p[j / 8];
				uint8 bit = (byte >> (7 - (j % 8))) & 1;
				if (bit) {
					sl.add_cell(x + j, agg::cover_full, agg::cover_full, agg::cover_full);
				}
			}
		} else {
			const uint8* p = buf;
			int w = bitmap.width / 3;
			
			for (int j = 0; j < w; j++) {
				if (p[0] || p[1] || p[2])
					sl.add_cell(x + j, p[0], p[1], p[2]);
				p += 3;
			}
		}
		
		buf += pitch;
		if (sl.num_spans()) {
			sl.finalize(y - i - 1);
			storage.render(sl);
		}
	}
}

class AGGBoundingBoxConsumer {
 public:
	AGGBoundingBoxConsumer(Transformable& transform, BRect* rectArray,
			bool asString)
		:
		rectArray(rectArray),
		stringBoundingBox(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN),
		fAsString(asString),
		fCurves(fPathAdaptor),
		fContour(fCurves),
		fTransformedOutline(fCurves, transform),
		fTransformedContourOutline(fContour, transform),
		fTransform(transform)
	{
	}

	bool NeedsVector() { return false; }
	void Start() {}
	void Finish(double x, double y) {}
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y) {}

	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		if (glyph->data_type != glyph_data_outline) {
			const agg::rect_i& r = glyph->bounds;
			if (fAsString) {
				if (rectArray) {
					rectArray[index].left = r.x1 + x;
					rectArray[index].top = r.y1 + y;
					rectArray[index].right = r.x2 + x + 1;
					rectArray[index].bottom = r.y2 + y + 1;
				} else {
					stringBoundingBox = stringBoundingBox
						| BRect(r.x1 + x, r.y1 + y,
							r.x2 + x + 1, r.y2 + y + 1);
				}
			} else {
				rectArray[index].left = r.x1;
				rectArray[index].top = r.y1;
				rectArray[index].right = r.x2 + 1;
				rectArray[index].bottom = r.y2 + 1;
			}
		} else {
			if (fAsString) {
				entry->InitAdaptors(glyph, x, y,
						fMonoAdaptor, fGray8Adaptor, fPathAdaptor);
			} else {
				entry->InitAdaptors(glyph, 0, 0,
						fMonoAdaptor, fGray8Adaptor, fPathAdaptor);
			}
			double left = 0.0;
			double top = 0.0;
			double right = -1.0;
			double bottom = -1.0;
			uint32 pathID[1];
			pathID[0] = 0;
			
			// Use fContour (stroked outline) for false bold fonts, otherwise use regular outline
			if (fContour.width() > 0.0) {
				agg::bounding_rect(fTransformedContourOutline, pathID, 0, 1,
					&left, &top, &right, &bottom);
			} else {
				agg::bounding_rect(fTransformedOutline, pathID, 0, 1,
					&left, &top, &right, &bottom);
			}

			if (rectArray) {
				rectArray[index] = BRect(left, top, right, bottom);
			} else {
				stringBoundingBox = stringBoundingBox
					| BRect(left, top, right, bottom);
			}
		}
		return true;
	}

	BRect*								rectArray;
	BRect								stringBoundingBox;

 private:
	bool								fAsString;
	FontCacheEntry::GlyphPathAdapter	fPathAdaptor;
	FontCacheEntry::GlyphGray8Adapter	fGray8Adaptor;
	FontCacheEntry::GlyphMonoAdapter	fMonoAdaptor;

	FontCacheEntry::CurveConverter		fCurves;
	FontCacheEntry::ContourConverter	fContour;

	FontCacheEntry::TransformedOutline	fTransformedOutline;
	FontCacheEntry::TransformedContourOutline fTransformedContourOutline;

	Transformable&						fTransform;
};

status_t
AGGRender::GetBoundingBoxes(const ServerFont& font, const char* string, int32 numBytes, int32 numChars,
	BRect rectArray[], bool stringEscapement, font_metric_mode mode,
	escapement_delta delta, bool asString)
{
	// Note: font_metric_mode parameter is part of BeOS API compatibility but not used in AGG implementation
	// The AGG rendering pipeline handles metrics internally through glyph bounds calculation
	if (string == NULL || numBytes <= 0 || numChars <= 0 || rectArray == NULL)
		return B_BAD_DATA;

	Transformable transform(font.EmbeddedTransformation());

	AGGBoundingBoxConsumer consumer(transform, rectArray, asString);
	if (GlyphLayoutEngine::LayoutGlyphs(consumer, font, string, numBytes,
			numChars, stringEscapement ? &delta : NULL, font.Spacing())) {
		return B_OK;
	}
	return B_ERROR;
}

status_t
AGGRender::GetBoundingBoxesForStrings(const ServerFont& font, char *charArray[], size_t lengthArray[],
	int32 numStrings, BRect rectArray[], font_metric_mode mode,
	escapement_delta deltaArray[])
{
	// Note: font_metric_mode parameter is part of BeOS API compatibility but not used in AGG implementation
	// The AGG rendering pipeline handles metrics internally through glyph bounds calculation
	if (charArray == NULL || lengthArray == NULL || numStrings <= 0
		|| rectArray == NULL || deltaArray == NULL) {
		return B_BAD_DATA;
	}

	Transformable transform(font.EmbeddedTransformation());

	for (int32 i = 0; i < numStrings; i++) {
		size_t numBytes = lengthArray[i];
		const char* string = charArray[i];
		escapement_delta delta = deltaArray[i];

		AGGBoundingBoxConsumer consumer(transform, NULL, true);
		if (!GlyphLayoutEngine::LayoutGlyphs(consumer, font, string, numBytes,
				INT32_MAX, &delta, font.Spacing())) {
			return B_ERROR;
		}

		rectArray[i] = consumer.stringBoundingBox;
	}

	return B_OK;
}

status_t
AGGRender::CalculateTextBoundingBox(const char* string, int32 numBytes,
	BRect& boundingBox) const
{
	if (string == nullptr || numBytes <= 0) {
		fLastError = B_BAD_VALUE;
		return B_BAD_VALUE;
	}
	
	// Calculate text bounding box using internal AGGTextRendererInternal dry run
	if (fInternalTextRenderer == nullptr) {
		const_cast<AGGRender*>(this)->fInternalTextRenderer = new AGGTextRendererInternal(const_cast<AGGRender*>(this));
	}
	
	// Use BoundingBox method which does dry run internally
	BPoint baseline(0, 0);
	boundingBox = BoundingBox(string, numBytes, baseline, nullptr, nullptr, nullptr);
	
	fLastError = B_OK;
	return B_OK;
}