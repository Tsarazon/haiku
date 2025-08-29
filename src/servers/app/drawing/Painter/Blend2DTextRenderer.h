/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D text renderer replacing AGGTextRenderer
 */
#ifndef BLEND2D_TEXT_RENDERER_H
#define BLEND2D_TEXT_RENDERER_H

#include "defines.h"

#include "FontCacheEntry.h"
#include "ServerFont.h"
#include "Transformable.h"

#include <blend2d.h>

class FontCacheReference;

class Blend2DTextRenderer {
public:
    Blend2DTextRenderer(BLContext& context, BLMatrix2D& viewTransformation);
    virtual ~Blend2DTextRenderer();

    void SetFont(const ServerFont &font);
    inline const ServerFont& Font() const { return fFont; }

    void SetHinting(bool hinting);
    bool Hinting() const { return fHinted; }

    void SetAntialiasing(bool antialiasing);
    bool Antialiasing() const { return fAntialias; }

    BRect RenderString(const char* utf8String,
                      uint32 length, const BPoint& baseLine,
                      const BRect& clippingFrame, bool dryRun,
                      BPoint* nextCharPos,
                      const escapement_delta* delta,
                      FontCacheReference* cacheReference);

    BRect RenderString(const char* utf8String,
                      uint32 length, const BPoint* offsets,
                      const BRect& clippingFrame, bool dryRun,
                      BPoint* nextCharPos,
                      FontCacheReference* cacheReference);

private:
    class StringRenderer;
    friend class StringRenderer;

    // Blend2D text rendering objects
    BLFont                  fBlendFont;       // Replaces AGG font handling
    BLGlyphBuffer           fGlyphBuffer;     // Replaces AGG glyph pipeline
    BLFontFace              fFontFace;        // Font face for glyph access

    BLContext&              fContext;         // Main rendering context
    BLMatrix2D&             fViewTransformation;

    ServerFont              fFont;
    bool                    fHinted;
    bool                    fAntialias;
    Transformable           fEmbeddedTransformation;
    
    // Font loading and caching
    bool LoadFont(const ServerFont& serverFont);
    void UpdateFontSettings();
    
    // Text measurement and rendering
    BRect MeasureString(const char* utf8String, uint32 length,
                       const BPoint& baseLine,
                       const escapement_delta* delta) const;
                       
    BRect RenderGlyphRun(const BLGlyphRun& glyphRun,
                        const BPoint& origin,
                        const BRect& clippingFrame,
                        bool dryRun);
};

#endif // BLEND2D_TEXT_RENDERER_H