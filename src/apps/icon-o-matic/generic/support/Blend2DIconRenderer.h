/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D-based icon renderer for Icon-O-Matic
 */
#ifndef BLEND2D_ICON_RENDERER_H
#define BLEND2D_ICON_RENDERER_H

#include "IconRenderer.h"
#include <blend2d.h>

class Blend2DIconRenderer : public IconRenderer {
public:
    Blend2DIconRenderer(BBitmap* bitmap);
    virtual ~Blend2DIconRenderer();

    // Override rendering methods for Blend2D
    virtual void Render() override;
    virtual void SetScale(double scale) override;
    virtual void SetIcon(Icon* icon) override;

protected:
    // Blend2D rendering objects
    BLImage         fImage;
    BLContext       fContext;
    
    // Context management
    bool            fContextInitialized;
    
    // Initialize Blend2D context
    bool _InitContext();
    void _CleanupContext();
    
    // Render icon elements with Blend2D
    void _RenderShape(VectorPath* path, Style* style);
    void _RenderPath(const BLPath& path, const rgb_color& color);
    void _RenderGradient(const BLPath& path, const BGradient& gradient);
};

#endif // BLEND2D_ICON_RENDERER_H