/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * IRenderEngine - Production-ready interface for 2D rendering engines
 * This interface abstracts all rendering operations with complete compatibility
 * for AGG and extensibility for future backends (Skia, Blend2D, etc.)
 * 
 * Based on comprehensive analysis of AGG usage in Haiku's app_server
 */
#ifndef I_RENDER_ENGINE_H
#define I_RENDER_ENGINE_H

#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Point.h>
#include <Rect.h>
#include <Region.h>
#include <AffineTransform.h>
#include <Gradient.h>
#include <SupportDefs.h>
#include <OS.h>
#include <Font.h>
#include <Accelerant.h>

// Forward declarations
class BBitmap;
class BRegion;
class BView;
class BFont;
class BGradient;
class BGradientLinear;
class BGradientRadial;
class BGradientRadialFocus;
class BGradientDiamond;
class BGradientConic;
class DrawState;
class FontCacheEntry;
class FontCacheReference;
class PatternHandler;
class RenderingBuffer;
class ServerBitmap;
class ServerFont;
class ServerPicture;
class ServerCursor;

// Haiku-specific types for line arrays
struct ViewLineArrayInfo;

// Engine-specific opaque types
// Lifecycle: All opaque types are managed by the render engine.
// Create* methods allocate and return ownership to caller.
// Delete* methods must be called by owner to free resources.
// Failure to call matching Delete* will result in resource leaks.
// Base structures for render engine extensions - implementations will inherit
struct RenderPath {
	virtual ~RenderPath() {}
};
struct RenderGradient {
	virtual ~RenderGradient() {}
};
struct RenderCompoundShape {
	virtual ~RenderCompoundShape() {}
};
struct RenderOutlineRenderer {
	virtual ~RenderOutlineRenderer() {}
};
struct RenderBlurFilter {
	virtual ~RenderBlurFilter() {}
};
struct RenderPattern {
	virtual ~RenderPattern() {}
};

// Bitmap filtering modes
enum RenderBitmapFilter {
	RENDER_FILTER_NEAREST_NEIGHBOR,
	RENDER_FILTER_BILINEAR,
	RENDER_FILTER_BICUBIC
};

// Text rendering modes
enum RenderTextMode {
	RENDER_TEXT_NORMAL,
	RENDER_TEXT_SUBPIXEL,
	RENDER_TEXT_MONO,
	RENDER_TEXT_ALIASED
};

// Pattern rendering modes
enum RenderPatternMode {
	RENDER_PATTERN_SOLID,
	RENDER_PATTERN_TEXTURE,
	RENDER_PATTERN_GRADIENT
};

// Advanced gradient types
enum RenderGradientType {
	RENDER_GRADIENT_LINEAR,
	RENDER_GRADIENT_RADIAL,
	RENDER_GRADIENT_RADIAL_FOCUS,
	RENDER_GRADIENT_DIAMOND,
	RENDER_GRADIENT_CONIC,
	RENDER_GRADIENT_XY,
	RENDER_GRADIENT_SQRT_XY
};

// Outline rendering modes
enum RenderOutlineMode {
	RENDER_OUTLINE_AA,
	RENDER_OUTLINE_IMAGE,
	RENDER_OUTLINE_PRIMITIVE
};

// Blur types
enum RenderBlurType {
	RENDER_BLUR_STACK,
	RENDER_BLUR_GAUSSIAN,
	RENDER_BLUR_MOTION
};

// Interpolation modes for advanced transformations
enum RenderInterpolationMode {
	RENDER_INTERP_LINEAR,
	RENDER_INTERP_BILINEAR,
	RENDER_INTERP_PERSPECTIVE
};

// Renderer capability flags
enum RenderCapability {
	RENDER_CAP_SUBPIXEL_AA		= 1 << 0,
	RENDER_CAP_ALPHA_MASK		= 1 << 1,
	RENDER_CAP_HARDWARE_ACCEL	= 1 << 2,
	RENDER_CAP_GRADIENTS		= 1 << 3,
	RENDER_CAP_PATTERNS			= 1 << 4,
	RENDER_CAP_BEZIER_PATHS		= 1 << 5,
	RENDER_CAP_TEXT_RENDERING	= 1 << 6,
	RENDER_CAP_BITMAP_TRANSFORM	= 1 << 7,
	RENDER_CAP_COMPOUND_SHAPES	= 1 << 8,
	RENDER_CAP_OUTLINE_RENDERING= 1 << 9,
	RENDER_CAP_BLUR_EFFECTS		= 1 << 10,
	RENDER_CAP_PATH_CLIPPING	= 1 << 11,
	RENDER_CAP_PERSPECTIVE_TRANS= 1 << 12,
	RENDER_CAP_GOURAUD_SHADING	= 1 << 13,
	RENDER_CAP_ADVANCED_GRADIENTS= 1 << 14,
	RENDER_CAP_LINE_CLIPPING	= 1 << 15,
	RENDER_CAP_POLYGON_CLIPPING	= 1 << 16,
	RENDER_CAP_BITMAP_FONTS		= 1 << 17,
	RENDER_CAP_SCANLINE_EFFECTS	= 1 << 18,
	RENDER_CAP_REGION_RENDERING	= 1 << 19,
	RENDER_CAP_CURSOR			= 1 << 20,
	RENDER_CAP_VSYNC			= 1 << 21,
	RENDER_CAP_DISPLAY_INFO		= 1 << 22
};

// Abstract interface for 2D rendering engines
// Thread-safety: unless stated otherwise, instances are not thread-safe; 
// external synchronization is required
class IRenderEngine {
public:
	virtual ~IRenderEngine() = default;
	
	// Prevent copying and moving for safety
	IRenderEngine(const IRenderEngine&) = delete;
	IRenderEngine& operator=(const IRenderEngine&) = delete;
	IRenderEngine(IRenderEngine&&) = delete;
	IRenderEngine& operator=(IRenderEngine&&) = delete;

protected:
	IRenderEngine() = default;

public:
	// ========== Engine Information ==========
	virtual const char* GetRendererName() const = 0;
	virtual const char* GetRendererVersion() const = 0;
	virtual uint32 GetCapabilities() const = 0;
	virtual bool HasCapability(RenderCapability cap) const = 0;

	// ========== Buffer Management ==========
	virtual status_t AttachToBuffer(RenderingBuffer* buffer) = 0;
	virtual void DetachFromBuffer() = 0;
	virtual BRect GetBufferBounds() const = 0;
	
	// Buffer synchronization and double buffering (2. Buffering)
	virtual status_t SwapBuffers() = 0;
	virtual status_t CopyToFront(const BRegion* region) = 0;
	virtual status_t SetCopyToFrontEnabled(bool enable) = 0;
	virtual status_t Sync() = 0;
	virtual status_t Flush() = 0;
	
	// VSync/Retrace operations (capability-gated: requires RENDER_CAP_VSYNC)
	virtual status_t WaitForRetrace(bigtime_t timeout = B_INFINITE_TIMEOUT) = 0;
	virtual sem_id RetraceSemaphore() = 0;
	
	// Display/Monitor information (capability-gated: requires RENDER_CAP_DISPLAY_INFO)
	virtual status_t GetDisplayMode(display_mode* mode) const = 0;
	virtual status_t GetMonitorInfo(monitor_info* info) const = 0;
	virtual float GetDisplayDPI() const = 0;

	// ========== State Management ==========
	
	// Complete DrawState integration
	virtual status_t SetDrawState(const DrawState* state, 
		int32 xOffset = 0, int32 yOffset = 0) = 0;
	virtual status_t GetDrawState(DrawState* state) const = 0;
	
	// Individual state components (for fine-grained control)
	virtual status_t SetTransform(const BAffineTransform& transform,
		int32 xOffset = 0, int32 yOffset = 0) = 0;
	virtual BAffineTransform GetTransform() const = 0;
	virtual bool IsIdentityTransform() const = 0;
	
	// State stack management with depth tracking
	virtual status_t PushState() = 0;
	virtual status_t PopState() = 0;
	virtual int32 GetStateDepth() const = 0;

	// ========== Clipping ==========
	virtual status_t SetClippingRegion(const BRegion* region) = 0;
	virtual status_t ConstrainClipping(const BRegion* region) = 0;
	virtual const BRegion* GetClippingRegion() const = 0;
	
	// Multi-level clipping support
	virtual status_t AddClipRegion(const BRegion* region) = 0;
	virtual status_t RemoveClipRegion(const BRegion* region) = 0;
	virtual status_t ClearAllClipRegions() = 0;
	virtual int32 GetClipRegionCount() const = 0;
	
	// Extended clipping operations (from BView)
	virtual status_t ClipToRect(BRect rect, bool inverse = false) = 0;
	virtual status_t ClipToPath(RenderPath* path, bool inverse = false) = 0;

	// ========== Color and Pattern ==========
	
	// Basic colors
	virtual status_t SetHighColor(const rgb_color& color) = 0;
	virtual rgb_color GetHighColor() const = 0;
	virtual status_t SetLowColor(const rgb_color& color) = 0;
	virtual rgb_color GetLowColor() const = 0;
	
	// UI colors with tints (native Haiku support)
	virtual status_t SetHighUIColor(color_which which, float tint = B_NO_TINT) = 0;
	virtual status_t SetLowUIColor(color_which which, float tint = B_NO_TINT) = 0;
	
	// Pattern support
	virtual status_t SetPattern(const pattern& p) = 0;
	virtual pattern GetPattern() const = 0;

	// ========== Drawing Modes and Blending ==========
	
	// Native Haiku drawing modes
	virtual status_t SetDrawingMode(drawing_mode mode) = 0;
	virtual drawing_mode GetDrawingMode() const = 0;
	
	// Blending modes
	virtual status_t SetBlendingMode(source_alpha srcAlpha, alpha_function alphaFunc) = 0;
	virtual source_alpha GetSourceAlpha() const = 0;
	virtual alpha_function GetAlphaFunction() const = 0;
	
	// Subpixel precision
	virtual status_t SetSubpixelPrecise(bool precise) = 0;
	virtual bool IsSubpixelPrecise() const = 0;

	// ========== Stroke Settings ==========
	
	// Pen properties
	virtual status_t SetPenSize(float size) = 0;
	virtual float GetPenSize() const = 0;
	
	// Stroke mode (native Haiku types)
	virtual status_t SetStrokeMode(cap_mode lineCap, join_mode joinMode, 
		float miterLimit) = 0;
	virtual cap_mode GetLineCapMode() const = 0;
	virtual join_mode GetLineJoinMode() const = 0;
	virtual float GetMiterLimit() const = 0;
	
	// Fill rule
	virtual status_t SetFillRule(int32 fillRule) = 0;
	virtual int32 GetFillRule() const = 0;

	// ========== Basic Drawing Operations ==========
	
	// Lines  
	virtual BRect StrokeLine(BPoint a, BPoint b) = 0;
	// Optimized straight line rendering - may fallback to StrokeLine
	virtual bool StraightLine(BPoint a, BPoint b, const rgb_color& c) = 0;
	
	// Point drawing (single pixel)
	virtual status_t StrokePoint(const BPoint& point, const rgb_color& color) = 0;
	
	// Line arrays (efficient multi-line drawing with individual colors)
	virtual status_t StrokeLineArray(int32 numLines, 
		const ViewLineArrayInfo* lineData) = 0;
	
	// BView-compatible line array API  
	virtual status_t BeginLineArray(int32 count) = 0;
	virtual status_t AddLine(BPoint start, BPoint end, const rgb_color& color) = 0;
	virtual status_t EndLineArray() = 0;
	
	// Triangles
	virtual BRect StrokeTriangle(BPoint pt1, BPoint pt2, BPoint pt3) = 0;
	virtual BRect FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3) = 0;
	virtual BRect FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3,
		const BGradient& gradient) = 0;
	
	// Rectangles
	virtual BRect StrokeRect(const BRect& r) = 0;
	virtual status_t StrokeRect(const BRect& r, const rgb_color& c) = 0;
	virtual BRect FillRect(const BRect& r) = 0;
	virtual BRect FillRect(const BRect& r, const BGradient& gradient) = 0;
	virtual status_t FillRect(const BRect& r, const rgb_color& c) = 0;
	virtual status_t FillRectNoClipping(const clipping_rect& r, const rgb_color& c) = 0;
	virtual status_t FillRectVerticalGradient(BRect r, 
		const BGradientLinear& gradient) = 0;
	
	// Rounded rectangles
	virtual BRect StrokeRoundRect(const BRect& r, float xRadius, 
		float yRadius) = 0;
	virtual BRect FillRoundRect(const BRect& r, float xRadius, 
		float yRadius) = 0;
	virtual BRect FillRoundRect(const BRect& r, float xRadius, float yRadius,
		const BGradient& gradient) = 0;
	
	// Ellipses
	virtual void AlignEllipseRect(BRect* rect, bool filled) const = 0;
	virtual BRect DrawEllipse(BRect r, bool filled) = 0;
	virtual BRect FillEllipse(BRect r, const BGradient& gradient) = 0;
	
	// Arcs
	virtual BRect StrokeArc(BPoint center, float xRadius, float yRadius,
		float angle, float span) = 0;
	virtual BRect FillArc(BPoint center, float xRadius, float yRadius,
		float angle, float span) = 0;
	virtual BRect FillArc(BPoint center, float xRadius, float yRadius,
		float angle, float span, const BGradient& gradient) = 0;

	// ========== Complex Shapes ==========
	
	// Polygons
	virtual BRect DrawPolygon(BPoint* ptArray, int32 numPts, bool filled, 
		bool closed) = 0;
	virtual BRect FillPolygon(BPoint* ptArray, int32 numPts,
		const BGradient& gradient, bool closed) = 0;
	
	// Bezier curves
	virtual BRect DrawBezier(BPoint* controlPoints, bool filled) = 0;
	virtual BRect FillBezier(BPoint* controlPoints, const BGradient& gradient) = 0;
	
	// Arbitrary shapes (Haiku shape format)
	virtual BRect DrawShape(const int32& opCount, const uint32* opList,
		const int32& ptCount, const BPoint* ptList, bool filled,
		const BPoint& viewToScreenOffset, float viewScale) = 0;
	virtual BRect FillShape(const int32& opCount, const uint32* opList,
		const int32& ptCount, const BPoint* ptList, const BGradient& gradient,
		const BPoint& viewToScreenOffset, float viewScale) = 0;
	
	// Regions
	virtual BRect FillRegion(const BRegion* region) = 0;
	virtual BRect FillRegion(const BRegion* region, const BGradient& gradient) = 0;

	// ========== Path Operations ==========
	
	// Path lifecycle - caller owns returned path and must call DeletePath()
	virtual RenderPath* CreatePath() = 0;
	virtual status_t DeletePath(RenderPath* path) = 0;
	virtual status_t CopyPath(const RenderPath* source, RenderPath** destination) = 0;
	
	// Path building
	virtual status_t PathMoveTo(RenderPath* path, float x, float y) = 0;
	virtual status_t PathLineTo(RenderPath* path, float x, float y) = 0;
	
	// Bezier curves (both quadratic and cubic)
	virtual status_t PathQuadTo(RenderPath* path, float cx, float cy, 
		float x, float y) = 0;
	virtual status_t PathCurveTo(RenderPath* path, float cx1, float cy1,
		float cx2, float cy2, float x, float y) = 0;
	
	// Arc support (SVG-style)
	virtual status_t PathArcTo(RenderPath* path, float rx, float ry,
		float angle, bool largeArc, bool sweep, float x, float y) = 0;
	
	// Path control
	virtual status_t PathClosePath(RenderPath* path) = 0;
	virtual status_t PathClear(RenderPath* path) = 0;
	
	// Path utilities
	virtual status_t PathAddRect(RenderPath* path, const BRect& rect) = 0;
	virtual status_t PathAddEllipse(RenderPath* path, const BRect& rect) = 0;
	virtual status_t PathAddRoundRect(RenderPath* path, const BRect& rect, 
		float xRadius, float yRadius) = 0;
	
	// Path rendering
	virtual BRect StrokePath(RenderPath* path) = 0;
	virtual BRect FillPath(RenderPath* path) = 0;
	virtual BRect FillPath(RenderPath* path, const BGradient& gradient) = 0;
	
	// Path information
	virtual BRect GetPathBounds(RenderPath* path) const = 0;
	virtual bool IsPathEmpty(RenderPath* path) const = 0;
	virtual int32 GetPathPointCount(RenderPath* path) const = 0;
	// PathContainsPoint uses even-odd fill rule for complex shapes
	virtual bool PathContainsPoint(RenderPath* path, BPoint point) const = 0;

	// ========== Layer Operations ==========
	
	// Layer compositing for transparency effects
	virtual status_t BeginLayer(uint8 opacity) = 0;
	virtual status_t EndLayer() = 0;
	virtual int32 GetLayerDepth() const = 0;
	
	// ========== Picture Operations ==========
	
	// Picture recording (BView-compatible API)
	virtual status_t BeginPicture(ServerPicture* picture) = 0;
	virtual status_t EndPicture() = 0;
	virtual status_t AppendToPicture(ServerPicture* picture) = 0;
	virtual bool IsPictureRecording() const = 0;
	
	// Picture playback (5. Picture operations)
	virtual status_t DrawPicture(const ServerPicture* picture, BPoint origin) = 0;
	virtual status_t ClipToPicture(const ServerPicture* picture, BPoint origin, bool inverse = false) = 0;
	
	// ========== Cursor Operations ==========
	
	// Cursor rendering (capability-gated: requires RENDER_CAP_CURSOR)
	// Backend may fallback to software cursor if hardware cursor not available
	virtual status_t DrawCursor(const ServerCursor* cursor, BPoint position) = 0;
	virtual status_t SetCursorVisible(bool visible) = 0;
	virtual bool IsCursorVisible() const = 0;

	// ========== Text Rendering ==========
	
	// Font management
	virtual status_t SetFont(const ServerFont& font) = 0;
	virtual const ServerFont& GetFont() const = 0;
	
	// Text rendering
	virtual BRect DrawString(const char* utf8String, uint32 length,
		BPoint baseLine, const escapement_delta* delta = nullptr,
		FontCacheReference* cacheReference = nullptr) = 0;
	virtual BRect DrawString(const char* utf8String, uint32 length,
		const BPoint* offsets, FontCacheReference* cacheReference = nullptr) = 0;
	
	// Text metrics
	virtual BRect BoundingBox(const char* utf8String, uint32 length,
		BPoint baseLine, BPoint* penLocation = nullptr, 
		const escapement_delta* delta = nullptr,
		FontCacheReference* cacheReference = nullptr) const = 0;
	virtual BRect BoundingBox(const char* utf8String, uint32 length,
		const BPoint* offsets, BPoint* penLocation = nullptr,
		FontCacheReference* cacheReference = nullptr) const = 0;
	virtual float StringWidth(const char* utf8String, uint32 length,
		const escapement_delta* delta = nullptr) = 0;
	
	// Dry rendering - calculate without drawing (8. Dry rendering)
	virtual BPoint DrawStringDry(const char* utf8String, uint32 length,
		BPoint baseLine, const escapement_delta* delta = nullptr) = 0;
	virtual BPoint DrawStringDry(const char* utf8String, uint32 length,
		const BPoint* offsets) = 0;
	
	// Text rendering options
	virtual status_t SetTextRenderingMode(RenderTextMode mode) = 0;
	virtual RenderTextMode GetTextRenderingMode() const = 0;
	virtual status_t SetHinting(bool hinting) = 0;
	virtual bool GetHinting() const = 0;
	virtual status_t SetAntialiasing(bool antialiasing) = 0;
	virtual bool GetAntialiasing() const = 0;
	virtual status_t SetKerning(bool kerning) = 0;
	virtual bool GetKerning() const = 0;
	
	// Subpixel text quality settings
	virtual status_t SetSubpixelAverageWeight(uint8 weight) = 0;
	virtual uint8 GetSubpixelAverageWeight() const = 0;
	virtual status_t SetTextGamma(float gamma) = 0;
	virtual float GetTextGamma() const = 0;
	
	// Font metrics (critical for layout)
	virtual status_t GetFontHeight(font_height* height) const = 0;
	virtual float GetFontAscent() const = 0;
	virtual float GetFontDescent() const = 0; 
	virtual float GetFontLeading() const = 0;
	
	// Bitmap font support
	virtual status_t LoadBitmapFont(const char* fontPath) = 0;
	virtual status_t DrawBitmapGlyph(uint32 glyphCode, BPoint baseline, 
		const rgb_color& color) = 0;
	virtual bool IsBitmapFont(const ServerFont& font) const = 0;

	// ========== Bitmap/Image Operations ==========
	
	// Bitmap drawing with filtering options
	virtual BRect DrawBitmap(const ServerBitmap* bitmap, BRect bitmapRect,
		BRect viewRect, uint32 options, RenderBitmapFilter filter = RENDER_FILTER_BILINEAR) = 0;
	virtual status_t DrawTiledBitmap(const ServerBitmap* bitmap, BRect viewRect,
		BPoint phase) = 0;
	
	// Advanced bitmap operations
	virtual status_t DrawBitmapWithTransform(const ServerBitmap* bitmap,
		const BAffineTransform& transform, RenderBitmapFilter filter) = 0;
	virtual status_t DrawBitmapMask(const ServerBitmap* bitmap,
		const ServerBitmap* mask, BRect bitmapRect, BRect viewRect) = 0;
	virtual status_t DrawBitmapWithAlpha(const ServerBitmap* bitmap,
		BRect bitmapRect, BRect viewRect, float alpha) = 0;
	
	// Region copying (BitBlt) - matches DrawingEngine signature
	virtual status_t CopyRegion(const BRegion* region, int32 xOffset, int32 yOffset) = 0;
	virtual status_t CopyRegionWithTransform(const BRegion* region,
		const BAffineTransform& transform) = 0;
	
	// Optimized bit copying operations (1. CopyBits/ScrollRect)
	virtual status_t CopyBits(BRect src, BRect dst, const BRegion* clipRegion = nullptr) = 0;
	virtual status_t ScrollRect(BRect rect, int32 xOffset, int32 yOffset) = 0;
	
	// Screenshot operations (7. Screenshots)
	virtual status_t ReadBitmap(ServerBitmap* bitmap, BRect bounds, bool includeCursor = false) = 0;
	virtual ServerBitmap* DumpToBitmap(const BRect& bounds, bool includeCursor = false) = 0;
	
	// Bitmap scaling/resampling operations
	virtual status_t ScaleBitmap(const ServerBitmap* source, ServerBitmap* destination,
		RenderBitmapFilter filter = RENDER_FILTER_BILINEAR) = 0;
	virtual status_t ResampleBitmap(const ServerBitmap* source, ServerBitmap* destination, 
		BRect sourceRect, BRect destRect, RenderBitmapFilter filter = RENDER_FILTER_BILINEAR) = 0;
	
	// Color space conversion operations
	virtual status_t ConvertColorSpace(const ServerBitmap* source, ServerBitmap* destination,
		color_space targetSpace) = 0;
	virtual bool SupportsColorSpace(color_space space) const = 0;
	virtual color_space GetNativeColorSpace() const = 0;
	
	// Hardware overlay support (for video playback)
	virtual bool CheckOverlayRestrictions(int32 width, int32 height, color_space colorSpace) = 0;
	virtual status_t ConfigureOverlay(const BRect& sourceRect, const BRect& destinationRect, 
		const ServerBitmap* bitmap) = 0;
	virtual status_t HideOverlay() = 0;

	// ========== Gradient Support ==========
	
	// Standard gradient lifecycle - caller owns returned gradient and must call DeleteGradient()
	virtual RenderGradient* CreateLinearGradient(const BGradientLinear& gradient) = 0;
	virtual RenderGradient* CreateRadialGradient(const BGradientRadial& gradient) = 0;
	virtual RenderGradient* CreateRadialFocusGradient(
		const BGradientRadialFocus& gradient) = 0;
	virtual RenderGradient* CreateDiamondGradient(const BGradientDiamond& gradient) = 0;
	virtual RenderGradient* CreateConicGradient(const BGradientConic& gradient) = 0;
	
	// Advanced gradient types (AGG-specific)
	virtual RenderGradient* CreateXYGradient(BPoint start, BPoint end, 
		const rgb_color* colors, int32 count) = 0;
	virtual RenderGradient* CreateSqrtXYGradient(BPoint start, BPoint end,
		const rgb_color* colors, int32 count) = 0;
	virtual RenderGradient* CreateGouraudGradient(const BPoint* vertices, 
		const rgb_color* colors, int32 count) = 0;
	
	// Gradient management
	virtual status_t DeleteGradient(RenderGradient* gradient) = 0;
	virtual status_t UpdateGradient(RenderGradient* gradient, const BGradient& newGradient) = 0;
	virtual BRect GetGradientBounds(RenderGradient* gradient) const = 0;
	virtual status_t SetGradientTransform(RenderGradient* gradient, 
		const BAffineTransform& transform) = 0;

	// ========== Alpha Masking ==========
	virtual status_t SetAlphaMask(const ServerBitmap* mask) = 0;
	virtual status_t SetClippedAlphaMask(const ServerBitmap* mask, 
		const BRect& clipRect) = 0;
	virtual status_t ClearAlphaMask() = 0;
	virtual bool HasAlphaMask() const = 0;
	virtual status_t SetAlphaMaskTransform(const BAffineTransform& transform) = 0;
	virtual float GetAlphaMaskValue(BPoint point) const = 0;

	// ========== Special Operations ==========
	
	// Color operations
	virtual BRect InvertRect(const BRect& r) = 0;
	virtual status_t BlendRect(const BRect& r, const rgb_color& c) = 0;
	
	// ========== Compound Shape Operations ==========
	
	// Compound shape creation and management - caller owns returned shape
	virtual RenderCompoundShape* CreateCompoundShape() = 0;
	virtual status_t DeleteCompoundShape(RenderCompoundShape* compound) = 0;
	virtual status_t CompoundAddPath(RenderCompoundShape* compound, 
		RenderPath* path, uint32 styleId) = 0;
	virtual status_t CompoundSetStyle(RenderCompoundShape* compound,
		uint32 styleId, const rgb_color& color, float alpha = 1.0f) = 0;
	virtual BRect DrawCompoundShape(RenderCompoundShape* compound) = 0;
	
	// ========== Pattern Operations ==========
	
	// Pattern lifecycle - caller owns returned pattern and must call DeletePattern()
	virtual RenderPattern* CreateImagePattern(const ServerBitmap* bitmap,
		RenderPatternMode mode) = 0;
	virtual RenderPattern* CreateGradientPattern(RenderGradient* gradient) = 0;
	virtual status_t DeletePattern(RenderPattern* pattern) = 0;
	virtual status_t SetPatternTransform(RenderPattern* pattern,
		const BAffineTransform& transform) = 0;
	virtual status_t ApplyPattern(RenderPath* path, RenderPattern* pattern) = 0;
	
	// ========== Outline Rendering ==========
	
	// Outline renderer management - caller owns returned renderer
	virtual RenderOutlineRenderer* CreateOutlineRenderer(RenderOutlineMode mode) = 0;
	virtual status_t DeleteOutlineRenderer(RenderOutlineRenderer* renderer) = 0;
	virtual status_t SetOutlineWidth(RenderOutlineRenderer* renderer, float width) = 0;
	virtual status_t SetOutlineColor(RenderOutlineRenderer* renderer, 
		const rgb_color& color) = 0;
	virtual BRect RenderOutline(RenderOutlineRenderer* renderer, RenderPath* path) = 0;
	
	// Marker rendering
	virtual status_t RenderMarker(BPoint center, float size, 
		uint32 markerType, const rgb_color& color) = 0;
	
	// Advanced outline operations
	virtual status_t RenderOutlineImage(RenderOutlineRenderer* renderer,
		RenderPath* path, const ServerBitmap* image) = 0;
	virtual status_t SetOutlineAccuracy(RenderOutlineRenderer* renderer, 
		float accuracy) = 0;
	virtual status_t RenderOutlineDashed(RenderOutlineRenderer* renderer,
		RenderPath* path, const float* dashArray, int32 dashCount, float dashOffset) = 0;
	
	// ========== Path Clipping Operations ==========
	
	// Advanced path clipping
	virtual status_t ClipPathToPolygon(RenderPath* path, const BPoint* vertices, 
		int32 count) = 0;
	virtual status_t ClipPathToPolyline(RenderPath* path, const BPoint* vertices,
		int32 count, float width) = 0;
	virtual status_t ClipPathToPath(RenderPath* source, RenderPath* clipPath,
		RenderPath** result) = 0;
	
	// Line clipping algorithms
	
	// ========== Blur and Effects Operations ==========
	
	// Blur filter management - caller owns returned filter and must call DeleteBlurFilter()
	virtual RenderBlurFilter* CreateBlurFilter(RenderBlurType type, 
		float radius) = 0;
	virtual status_t DeleteBlurFilter(RenderBlurFilter* filter) = 0;
	virtual status_t SetBlurRadius(RenderBlurFilter* filter, float radius) = 0;
	virtual BRect ApplyBlur(RenderBlurFilter* filter, const BRect& rect) = 0;
	
	// Stack blur (optimized blur for UI elements)
	virtual status_t StackBlur(const BRect& rect, float radius) = 0;
	
	// Gaussian blur (general purpose)
	virtual status_t GaussianBlur(const BRect& rect, float radius) = 0;
	virtual status_t RecursiveBlur(const BRect& rect, float radius) = 0;
	
	// ========== Advanced Path Operations ==========
	
	// Path utilities and operations
	virtual status_t PathAddDash(RenderPath* path, const float* dashArray, 
		int32 count, float offset) = 0;
	virtual status_t PathSmooth(RenderPath* path, float factor) = 0;
	virtual status_t PathSimplify(RenderPath* path, float tolerance) = 0;
	virtual status_t PathContour(RenderPath* source, RenderPath** result,
		float width, bool inner) = 0;
	virtual status_t PathOffset(RenderPath* path, float dx, float dy) = 0;
	virtual BRect CalculateBoundingRect(RenderPath* path, 
		const BAffineTransform* transform = nullptr) = 0;
	
	// Path creation from arcs
	virtual RenderPath* CreateBezierArc(BPoint center, float rx, float ry,
		float startAngle, float spanAngle) = 0;
	
	// Coordinate transformations
	virtual BRect TransformAndClipRect(BRect rect) const = 0;
	virtual BRect ClipRect(BRect rect) const = 0;
	virtual BRect TransformAlignAndClipRect(BRect rect) const = 0;
	virtual BRect AlignAndClipRect(BRect rect) const = 0;
	virtual BRect AlignRect(BRect rect) const = 0;
	
	// Transform utilities  
	virtual BPoint TransformPoint(BPoint point) const = 0;
	virtual status_t TransformPoints(BPoint* points, int32 count) const = 0;
	virtual BRect TransformRect(BRect rect) const = 0;
	
	// Component transformation methods (from BView/Canvas)
	virtual status_t SetOrigin(BPoint origin) = 0;
	virtual BPoint GetOrigin() const = 0;
	virtual status_t SetScale(float scale) = 0;
	virtual status_t SetScale(float scaleX, float scaleY) = 0;
	virtual float GetScale() const = 0;
	virtual status_t SetRotation(float angle) = 0;
	virtual float GetRotation() const = 0;
	
	// Advanced transformation support
	virtual status_t SetPerspectiveTransform(float m00, float m01, float m02,
		float m10, float m11, float m12, float m20, float m21, float m22) = 0;
	virtual status_t SetBilinearTransform(BPoint quad[4]) = 0;
	virtual status_t SetViewportTransform(BRect viewport, BRect world) = 0;
	virtual status_t SetInterpolationMode(RenderInterpolationMode mode) = 0;
	
	// Lens transformation
	virtual status_t SetLensTransform(BPoint center, float radius, float power) = 0;
	
	// Renderer offset
	virtual status_t SetRendererOffset(int32 offsetX, int32 offsetY) = 0;
	
	// ========== Scanline Processing Operations ==========
	
	// Advanced scanline operations
	virtual status_t ProcessScanlines(const BRect& rect, 
		void (*processor)(int32 y, float* spans, int32 numSpans, void* userData),
		void* userData) = 0;
	virtual status_t RenderScanlinesAA(RenderPath* path, bool useSubpixel = false) = 0;
	virtual status_t RenderScanlinesBin(RenderPath* path) = 0;
	
	// Scanline boolean operations
	virtual status_t ScanlineUnion(RenderPath* path1, RenderPath* path2, 
		RenderPath** result) = 0;
	virtual status_t ScanlineIntersection(RenderPath* path1, RenderPath* path2,
		RenderPath** result) = 0;
	virtual status_t ScanlineXor(RenderPath* path1, RenderPath* path2,
		RenderPath** result) = 0;
	
	// ========== Geometric Utility Functions ==========
	
	// Geometric calculations
	virtual float CalculateDistance(BPoint p1, BPoint p2) const = 0;
	virtual float CalculateAngle(BPoint center, BPoint point) const = 0;
	virtual BPoint CalculateIntersection(BPoint line1Start, BPoint line1End,
		BPoint line2Start, BPoint line2End) const = 0;
	virtual bool LineIntersectsRect(BPoint lineStart, BPoint lineEnd, 
		const BRect& rect) const = 0;
	virtual BRect CalculateStrokeBounds(BPoint start, BPoint end, 
		float width, cap_mode cap, join_mode join) const = 0;
	
	// Polygon and curve utilities
	virtual bool PointInPolygon(BPoint point, const BPoint* vertices, int32 count) const = 0;
	virtual BRect CalculatePolygonBounds(const BPoint* vertices, int32 count) const = 0;
	virtual float CalculatePolygonArea(const BPoint* vertices, int32 count) const = 0;

	// ========== Performance and Quality ==========
	
	// Quality control
	virtual status_t SetQualityLevel(int32 level) = 0;  // 0=fast, 100=best quality
	virtual int32 GetQualityLevel() const = 0;
	
	// Memory management
	virtual size_t GetMemoryUsage() const = 0;
	virtual status_t TrimMemoryCache() = 0;
	
	// ========== Region-Based Rendering ==========
	
	// Direct region operations
	virtual status_t SetRenderingRegion(const BRegion* region) = 0;
	virtual status_t RenderToRegion(const BRegion* region, 
		void (*renderer)(const BRect& rect, void* userData), void* userData) = 0;
	virtual status_t FillRegionScanlines(const BRegion* region, 
		const rgb_color& color) = 0;
	
	// ========== Primitive Rendering Operations ==========
	
	// Direct primitive rendering (bypasses path system)
	virtual status_t RenderPixel(BPoint point, const rgb_color& color) = 0;
	virtual status_t RenderHorizontalLine(int32 y, int32 x1, int32 x2, 
		const rgb_color& color) = 0;
	virtual status_t RenderVerticalLine(int32 x, int32 y1, int32 y2,
		const rgb_color& color) = 0;
	virtual status_t RenderRectOutline(const BRect& rect, const rgb_color& color) = 0;

	// ========== Error Handling ==========
	
	// Error state management
	// GetLastErrorString() lifetime: string valid until next API call
	virtual status_t GetLastError() const = 0;
	virtual const char* GetLastErrorString() const = 0;
	virtual void ClearError() = 0;

	// ========== Debug and Profiling ==========
#if DEBUG
	// Performance profiling
	virtual void StartProfiling(const char* operationName) = 0;
	virtual void EndProfiling() = 0;
	virtual void DumpProfile() const = 0;
	
	// Debug instrumentation
	virtual void SetDebugMode(bool enable) = 0;
	virtual void DumpState() const = 0;
#endif
};

// Factory functions moved to separate factory header to avoid
// dependencies and improve compilation times

#endif // I_RENDER_ENGINE_H