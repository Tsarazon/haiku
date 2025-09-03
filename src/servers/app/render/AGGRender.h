/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender - Anti-Grain Geometry implementation of IRenderEngine
 * This class wraps all AGG functionality behind the IRenderEngine interface.
 */
#ifndef AGG_RENDER_H
#define AGG_RENDER_H

#include "IRenderEngine.h"

// Forward declarations for Haiku internal classes (implementation in .cpp)
class IntRect;
class DrawState; 
class ServerFont;
struct FontCacheEntry;
class PatternHandler;
class GlobalFontManager;
class GlyphLayoutEngine;
class FontManager;

// Forward declaration for FreeType
struct FT_Bitmap_;
typedef struct FT_Bitmap_ FT_Bitmap;

#include <agg_conv_curve.h>
#include <agg_path_storage.h>
#include <agg_trans_affine.h>
#include <agg_rendering_buffer.h>
#include <agg_alpha_mask_u8.h>
#include <agg_rasterizer_compound_aa.h>
#include <agg_rasterizer_sl_clip.h>
#include <agg_renderer_base.h>
#include <agg_pixfmt_rgba.h>
#include <agg_renderer_outline_aa.h>

#include <ObjectList.h>
#include <String.h>

// Forward declarations for Interface Kit types
class BGradient;
class BGradientLinear;
class BGradientRadial;
class BGradientRadialFocus;
class BGradientDiamond;
class BGradientConic;
struct ViewLineArrayInfo;
struct FontCacheReference;
class ServerBitmap;
class ServerPicture;
class ServerCursor;
class RenderingBuffer;
// Forward declarations already defined in system headers
// display_mode, monitor_info, clipping_rect are typedefs, not structs
// Main AGGRender implementation class
class AGGRender : public IRenderEngine {
public:
	AGGRender();
	virtual ~AGGRender();

	// ========== Buffer Management ==========
	virtual status_t AttachToBuffer(RenderingBuffer* buffer);
	virtual void DetachFromBuffer();
	virtual BRect GetBufferBounds() const;
	
	// Buffer synchronization and double buffering
	virtual status_t SwapBuffers();
	virtual status_t CopyToFront(const BRegion* region);
	virtual status_t SetCopyToFrontEnabled(bool enable);
	virtual status_t Sync();
	virtual status_t Flush();
	
	// VSync/Retrace operations (capability-gated: requires RENDER_CAP_VSYNC)
	virtual status_t WaitForRetrace(bigtime_t timeout = B_INFINITE_TIMEOUT);
	virtual sem_id RetraceSemaphore();
	
	// Display/Monitor information (capability-gated: requires RENDER_CAP_DISPLAY_INFO)
	virtual status_t GetDisplayMode(display_mode* mode) const;
	virtual status_t GetMonitorInfo(monitor_info* info) const;
	virtual float GetDisplayDPI() const;

	// ========== Engine Information ==========
	virtual const char* GetRendererName() const;
	virtual const char* GetRendererVersion() const;
	virtual uint32 GetCapabilities() const;
	virtual bool HasCapability(RenderCapability cap) const;
	
	// ========== State Management ==========
	virtual status_t SetDrawState(const DrawState* state, 
		int32 xOffset = 0, int32 yOffset = 0);
	virtual status_t GetDrawState(DrawState* state) const;
	virtual status_t SetTransform(const BAffineTransform& transform,
		int32 xOffset = 0, int32 yOffset = 0);
	virtual BAffineTransform GetTransform() const;
	virtual bool IsIdentityTransform() const;
	virtual status_t PushState();
	virtual status_t PopState();
	virtual int32 GetStateDepth() const;
	
	// ========== Clipping ==========
	virtual status_t SetClippingRegion(const BRegion* region);
	virtual status_t ConstrainClipping(const BRegion* region);
	virtual const BRegion* GetClippingRegion() const;
	
	// Multi-level clipping support
	virtual status_t AddClipRegion(const BRegion* region);
	virtual status_t RemoveClipRegion(const BRegion* region);
	virtual status_t ClearAllClipRegions();
	virtual int32 GetClipRegionCount() const;
	
	// Extended clipping operations (from BView)
	virtual status_t ClipToRect(BRect rect, bool inverse = false);
	virtual status_t ClipToPath(RenderPath* path, bool inverse = false);
	
	// ========== Color and Pattern ==========
	virtual status_t SetHighColor(const rgb_color& color);
	virtual rgb_color GetHighColor() const;
	virtual status_t SetLowColor(const rgb_color& color);
	virtual rgb_color GetLowColor() const;
	virtual status_t SetHighUIColor(color_which which, float tint = B_NO_TINT);
	virtual status_t SetLowUIColor(color_which which, float tint = B_NO_TINT);
	virtual status_t SetPattern(const pattern& p);
	virtual pattern GetPattern() const;
	
	// ========== Drawing Modes and Blending ==========
	virtual status_t SetDrawingMode(drawing_mode mode);
	virtual drawing_mode GetDrawingMode() const;
	virtual status_t SetBlendingMode(source_alpha srcAlpha, alpha_function alphaFunc);
	virtual source_alpha GetSourceAlpha() const;
	virtual alpha_function GetAlphaFunction() const;
	virtual status_t SetSubpixelPrecise(bool precise);
	virtual bool IsSubpixelPrecise() const;
	
	// ========== Stroke Settings ==========
	virtual status_t SetPenSize(float size);
	virtual float GetPenSize() const;
	virtual status_t SetStrokeMode(cap_mode lineCap, join_mode joinMode,
		float miterLimit);
	virtual cap_mode GetLineCapMode() const;
	virtual join_mode GetLineJoinMode() const;
	virtual float GetMiterLimit() const;
	virtual status_t SetFillRule(int32 fillRule);
	virtual int32 GetFillRule() const;
	
	// ========== Basic Drawing Operations ==========
	virtual BRect StrokeLine(BPoint a, BPoint b);
	virtual bool StraightLine(BPoint a, BPoint b, const rgb_color& c);
	
	// Point and line array operations
	virtual status_t StrokePoint(const BPoint& point, const rgb_color& color);
	virtual status_t StrokeLineArray(int32 numLines,
		const ViewLineArrayInfo* lineData);
	
	// BView-compatible line array API  
	virtual status_t BeginLineArray(int32 count);
	virtual status_t AddLine(BPoint start, BPoint end, const rgb_color& color);
	virtual status_t EndLineArray();
	
	virtual BRect StrokeTriangle(BPoint pt1, BPoint pt2, BPoint pt3);
	virtual BRect FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3);
	virtual BRect FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3,
		const BGradient& gradient);
	
	virtual BRect StrokeRect(const BRect& r);
	virtual status_t StrokeRect(const BRect& r, const rgb_color& c);
	virtual BRect FillRect(const BRect& r);
	virtual BRect FillRect(const BRect& r, const BGradient& gradient);
	virtual status_t FillRect(const BRect& r, const rgb_color& c);
	virtual status_t FillRectNoClipping(const clipping_rect& r, const rgb_color& c);
	virtual status_t FillRectVerticalGradient(BRect r,
		const BGradientLinear& gradient);
	
	virtual BRect StrokeRoundRect(const BRect& r, float xRadius, float yRadius);
	virtual BRect FillRoundRect(const BRect& r, float xRadius, float yRadius);
	virtual BRect FillRoundRect(const BRect& r, float xRadius, float yRadius,
		const BGradient& gradient);
	
	virtual void AlignEllipseRect(BRect* rect, bool filled) const;
	virtual BRect DrawEllipse(BRect r, bool filled);
	virtual BRect FillEllipse(BRect r, const BGradient& gradient);
	
	virtual BRect StrokeArc(BPoint center, float xRadius, float yRadius,
		float angle, float span);
	virtual BRect FillArc(BPoint center, float xRadius, float yRadius,
		float angle, float span);
	virtual BRect FillArc(BPoint center, float xRadius, float yRadius,
		float angle, float span, const BGradient& gradient);
	
	// ========== Complex Shapes ==========
	virtual BRect DrawPolygon(BPoint* ptArray, int32 numPts, bool filled,
		bool closed);
	virtual BRect FillPolygon(BPoint* ptArray, int32 numPts,
		const BGradient& gradient, bool closed);
	
	virtual BRect DrawBezier(BPoint* controlPoints, bool filled);
	virtual BRect FillBezier(BPoint* controlPoints, const BGradient& gradient);
	
	virtual BRect DrawShape(const int32& opCount, const uint32* opList,
		const int32& ptCount, const BPoint* ptList, bool filled,
		const BPoint& viewToScreenOffset, float viewScale);
	virtual BRect FillShape(const int32& opCount, const uint32* opList,
		const int32& ptCount, const BPoint* ptList, const BGradient& gradient,
		const BPoint& viewToScreenOffset, float viewScale);
	
	virtual BRect FillRegion(const BRegion* region);
	virtual BRect FillRegion(const BRegion* region, const BGradient& gradient);
	
	// ========== Layer Operations ==========
	
	// Layer compositing for transparency effects
	virtual status_t BeginLayer(uint8 opacity);
	virtual status_t EndLayer();
	virtual int32 GetLayerDepth() const;
	
	// ========== Picture Operations ==========
	
	// Picture recording (BView-compatible API)
	virtual status_t BeginPicture(ServerPicture* picture);
	virtual status_t EndPicture();
	virtual status_t AppendToPicture(ServerPicture* picture);
	virtual bool IsPictureRecording() const;
	
	// Picture playback
	virtual status_t DrawPicture(const ServerPicture* picture, BPoint origin);
	virtual status_t ClipToPicture(const ServerPicture* picture, BPoint origin, bool inverse = false);
	
	// ========== Cursor Operations ==========
	
	// Cursor rendering (capability-gated: requires RENDER_CAP_CURSOR)
	// Backend may fallback to software cursor if hardware cursor not available
	virtual status_t DrawCursor(const ServerCursor* cursor, BPoint position);
	virtual status_t SetCursorVisible(bool visible);
	virtual bool IsCursorVisible() const;
	
	// ========== Path Operations ==========
	virtual RenderPath* CreatePath();
	virtual status_t DeletePath(RenderPath* path);
	virtual status_t CopyPath(const RenderPath* source, RenderPath** destination);
	virtual status_t PathMoveTo(RenderPath* path, float x, float y);
	virtual status_t PathLineTo(RenderPath* path, float x, float y);
	virtual status_t PathQuadTo(RenderPath* path, float cx, float cy, 
		float x, float y);
	virtual status_t PathCurveTo(RenderPath* path, float cx1, float cy1,
		float cx2, float cy2, float x, float y);
	virtual status_t PathArcTo(RenderPath* path, float rx, float ry,
		float angle, bool largeArc, bool sweep, float x, float y);
	virtual status_t PathClosePath(RenderPath* path);
	virtual status_t PathClear(RenderPath* path);
	virtual status_t PathAddRect(RenderPath* path, const BRect& rect);
	virtual status_t PathAddEllipse(RenderPath* path, const BRect& rect);
	virtual status_t PathAddRoundRect(RenderPath* path, const BRect& rect, 
		float xRadius, float yRadius);
	virtual BRect StrokePath(RenderPath* path);
	virtual BRect FillPath(RenderPath* path);
	virtual BRect FillPath(RenderPath* path, const BGradient& gradient);
	virtual BRect GetPathBounds(RenderPath* path) const;
	virtual bool IsPathEmpty(RenderPath* path) const;
	virtual int32 GetPathPointCount(RenderPath* path) const;
	virtual bool PathContainsPoint(RenderPath* path, BPoint point) const;
	
	// ========== Text Rendering ==========
	virtual status_t SetFont(const ServerFont& font);
	virtual const ServerFont& GetFont() const;
	
	// Dry rendering - calculate without drawing
	virtual BPoint DrawStringDry(const char* utf8String, uint32 length,
		BPoint baseLine, const escapement_delta* delta = nullptr);
	virtual BPoint DrawStringDry(const char* utf8String, uint32 length,
		const BPoint* offsets);
	
	// Subpixel text quality settings
	virtual status_t SetSubpixelAverageWeight(uint8 weight);
	virtual uint8 GetSubpixelAverageWeight() const;
	virtual status_t SetTextGamma(float gamma);
	virtual float GetTextGamma() const;
	
	// Font metrics (critical for layout)
	virtual status_t GetFontHeight(font_height* height) const;
	virtual float GetFontAscent() const;
	virtual float GetFontDescent() const; 
	virtual float GetFontLeading() const;
	virtual BRect DrawString(const char* utf8String, uint32 length,
		BPoint baseLine, const escapement_delta* delta = nullptr,
		FontCacheReference* cacheReference = nullptr);
	virtual BRect DrawString(const char* utf8String, uint32 length,
		const BPoint* offsets, FontCacheReference* cacheReference = nullptr);
	virtual BRect BoundingBox(const char* utf8String, uint32 length,
		BPoint baseLine, BPoint* penLocation = nullptr, 
		const escapement_delta* delta = nullptr,
		FontCacheReference* cacheReference = nullptr) const;
	virtual BRect BoundingBox(const char* utf8String, uint32 length,
		const BPoint* offsets, BPoint* penLocation = nullptr,
		FontCacheReference* cacheReference = nullptr) const;
	virtual float StringWidth(const char* utf8String, uint32 length,
		const escapement_delta* delta = nullptr);
	virtual status_t SetTextRenderingMode(RenderTextMode mode);
	virtual RenderTextMode GetTextRenderingMode() const;
	virtual status_t SetHinting(bool hinting);
	virtual bool GetHinting() const;
	virtual status_t SetAntialiasing(bool antialiasing);
	virtual bool GetAntialiasing() const;
	virtual status_t SetKerning(bool kerning);
	virtual bool GetKerning() const;
	
	// Bitmap font support
	virtual status_t LoadBitmapFont(const char* fontPath);
	virtual status_t DrawBitmapGlyph(uint32 glyphCode, BPoint baseline, 
		const rgb_color& color);
	virtual bool IsBitmapFont(const ServerFont& font) const;
	
	// ========== Bitmap/Image Operations ==========
	virtual BRect DrawBitmap(const ServerBitmap* bitmap, BRect bitmapRect,
		BRect viewRect, uint32 options, RenderBitmapFilter filter = RENDER_FILTER_BILINEAR);
	virtual status_t DrawTiledBitmap(const ServerBitmap* bitmap, BRect viewRect,
		BPoint phase);
	virtual status_t DrawBitmapWithTransform(const ServerBitmap* bitmap,
		const BAffineTransform& transform, RenderBitmapFilter filter);
	virtual status_t DrawBitmapMask(const ServerBitmap* bitmap,
		const ServerBitmap* mask, BRect bitmapRect, BRect viewRect);
	virtual status_t DrawBitmapWithAlpha(const ServerBitmap* bitmap,
		BRect bitmapRect, BRect viewRect, float alpha);
	virtual status_t CopyRegion(const BRegion* region, int32 xOffset, int32 yOffset);
	virtual status_t CopyRegionWithTransform(const BRegion* region,
		const BAffineTransform& transform);
	
	// Optimized bit copying operations
	virtual status_t CopyBits(BRect src, BRect dst, const BRegion* clipRegion = nullptr);
	virtual status_t ScrollRect(BRect rect, int32 xOffset, int32 yOffset);
	
	// Screenshot operations
	virtual status_t ReadBitmap(ServerBitmap* bitmap, BRect bounds, bool includeCursor = false);
	virtual ServerBitmap* DumpToBitmap(const BRect& bounds, bool includeCursor = false);
	
	// Bitmap scaling/resampling operations
	virtual status_t ScaleBitmap(const ServerBitmap* source, ServerBitmap* destination,
		RenderBitmapFilter filter = RENDER_FILTER_BILINEAR);
	virtual status_t ResampleBitmap(const ServerBitmap* source, ServerBitmap* destination, 
		BRect sourceRect, BRect destRect, RenderBitmapFilter filter = RENDER_FILTER_BILINEAR);
	
	// Color space conversion operations
	virtual status_t ConvertColorSpace(const ServerBitmap* source, ServerBitmap* destination,
		color_space targetSpace);
	virtual bool SupportsColorSpace(color_space space) const;
	virtual color_space GetNativeColorSpace() const;
	
	// Hardware overlay support (for video playback)
	virtual bool CheckOverlayRestrictions(int32 width, int32 height, color_space colorSpace);
	virtual status_t ConfigureOverlay(const BRect& sourceRect, const BRect& destinationRect, 
		const ServerBitmap* bitmap);
	virtual status_t HideOverlay();
	
	// ========== Gradient Support ==========
	
	// Standard gradient lifecycle
	virtual RenderGradient* CreateLinearGradient(const BGradientLinear& gradient);
	virtual RenderGradient* CreateRadialGradient(const BGradientRadial& gradient);
	virtual RenderGradient* CreateRadialFocusGradient(
		const BGradientRadialFocus& gradient);
	virtual RenderGradient* CreateDiamondGradient(const BGradientDiamond& gradient);
	virtual RenderGradient* CreateConicGradient(const BGradientConic& gradient);
	
	// Advanced gradient types (AGG-specific)
	virtual RenderGradient* CreateXYGradient(BPoint start, BPoint end,
		const rgb_color* colors, int32 count);
	virtual RenderGradient* CreateSqrtXYGradient(BPoint start, BPoint end,
		const rgb_color* colors, int32 count);
	virtual RenderGradient* CreateGouraudGradient(const BPoint* vertices,
		const rgb_color* colors, int32 count);
	
	// Gradient management
	virtual status_t DeleteGradient(RenderGradient* gradient);
	virtual status_t UpdateGradient(RenderGradient* gradient, const BGradient& newGradient);
	virtual BRect GetGradientBounds(RenderGradient* gradient) const;
	virtual status_t SetGradientTransform(RenderGradient* gradient,
		const BAffineTransform& transform);
	
	// ========== Alpha Masking ==========
	virtual status_t SetAlphaMask(const ServerBitmap* mask);
	virtual status_t SetClippedAlphaMask(const ServerBitmap* mask,
		const BRect& clipRect);
	virtual status_t ClearAlphaMask();
	virtual bool HasAlphaMask() const;
	virtual status_t SetAlphaMaskTransform(const BAffineTransform& transform);
	virtual float GetAlphaMaskValue(BPoint point) const;
	
	// ========== Special Operations ==========
	virtual BRect InvertRect(const BRect& r);
	virtual status_t BlendRect(const BRect& r, const rgb_color& c);
	
	// ========== Compound Shape Operations ==========
	virtual RenderCompoundShape* CreateCompoundShape();
	virtual status_t DeleteCompoundShape(RenderCompoundShape* compound);
	virtual status_t CompoundAddPath(RenderCompoundShape* compound,
		RenderPath* path, uint32 styleId);
	virtual status_t CompoundSetStyle(RenderCompoundShape* compound,
		uint32 styleId, const rgb_color& color, float alpha = 1.0f);
	virtual BRect DrawCompoundShape(RenderCompoundShape* compound);
	
	// ========== Pattern Operations ==========
	virtual RenderPattern* CreateImagePattern(const ServerBitmap* bitmap,
		RenderPatternMode mode);
	virtual RenderPattern* CreateGradientPattern(RenderGradient* gradient);
	virtual status_t DeletePattern(RenderPattern* pattern);
	virtual status_t SetPatternTransform(RenderPattern* pattern,
		const BAffineTransform& transform);
	virtual status_t ApplyPattern(RenderPath* path, RenderPattern* pattern);
	
	// ========== Outline Rendering ==========
	virtual RenderOutlineRenderer* CreateOutlineRenderer(RenderOutlineMode mode);
	virtual status_t DeleteOutlineRenderer(RenderOutlineRenderer* renderer);
	virtual status_t SetOutlineWidth(RenderOutlineRenderer* renderer, float width);
	virtual status_t SetOutlineColor(RenderOutlineRenderer* renderer,
		const rgb_color& color);
	virtual BRect RenderOutline(RenderOutlineRenderer* renderer, RenderPath* path);
	virtual status_t RenderMarker(BPoint center, float size,
		uint32 markerType, const rgb_color& color);
	
	// Advanced outline operations
	virtual status_t RenderOutlineImage(RenderOutlineRenderer* renderer,
		RenderPath* path, const ServerBitmap* image);
	virtual status_t SetOutlineAccuracy(RenderOutlineRenderer* renderer, 
		float accuracy);
	virtual status_t RenderOutlineDashed(RenderOutlineRenderer* renderer,
		RenderPath* path, const float* dashArray, int32 dashCount, float dashOffset);
	
	// ========== Path Clipping Operations ==========
	virtual status_t ClipPathToPolygon(RenderPath* path, const BPoint* vertices,
		int32 count);
	virtual status_t ClipPathToPolyline(RenderPath* path, const BPoint* vertices,
		int32 count, float width);
	virtual status_t ClipPathToPath(RenderPath* source, RenderPath* clipPath,
		RenderPath** result);
	
	// Line clipping algorithms
	
	// ========== Blur and Effects Operations ==========
	virtual RenderBlurFilter* CreateBlurFilter(RenderBlurType type,
		float radius);
	virtual status_t DeleteBlurFilter(RenderBlurFilter* filter);
	virtual status_t SetBlurRadius(RenderBlurFilter* filter, float radius);
	virtual BRect ApplyBlur(RenderBlurFilter* filter, const BRect& rect);
	virtual status_t StackBlur(const BRect& rect, float radius);
	
	// Gaussian blur (general purpose)
	virtual status_t GaussianBlur(const BRect& rect, float radius);
	virtual status_t RecursiveBlur(const BRect& rect, float radius);
	
	// ========== Advanced Path Operations ==========
	virtual status_t PathAddDash(RenderPath* path, const float* dashArray,
		int32 count, float offset);
	virtual status_t PathSmooth(RenderPath* path, float factor);
	virtual status_t PathSimplify(RenderPath* path, float tolerance);
	virtual status_t PathContour(RenderPath* source, RenderPath** result,
		float width, bool inner);
	virtual status_t PathOffset(RenderPath* path, float dx, float dy);
	virtual BRect CalculateBoundingRect(RenderPath* path,
		const BAffineTransform* transform = nullptr);
	
	// Path creation from arcs
	virtual RenderPath* CreateBezierArc(BPoint center, float rx, float ry,
		float startAngle, float spanAngle);
	
	// Component transformation methods (from BView/Canvas)
	virtual status_t SetOrigin(BPoint origin);
	virtual BPoint GetOrigin() const;
	virtual status_t SetScale(float scale);
	virtual status_t SetScale(float scaleX, float scaleY);
	virtual float GetScale() const;
	virtual status_t SetRotation(float angle);
	virtual float GetRotation() const;
	
	// Lens transformation
	virtual status_t SetLensTransform(BPoint center, float radius, float power);
	
	// ========== Performance and Quality ==========
	virtual status_t SetQualityLevel(int32 level);
	virtual int32 GetQualityLevel() const;
	virtual size_t GetMemoryUsage() const;
	virtual status_t TrimMemoryCache();

	// ========== Error Handling ==========
	virtual status_t GetLastError() const;
	virtual const char* GetLastErrorString() const;
	virtual void ClearError();

private:
	// ========== AGG-specific Implementation Details ==========
	// Wrap mode types for image accessors
	enum wrap_mode_e {
		WRAP_MODE_REPEAT,
		WRAP_MODE_REFLECT,
		WRAP_MODE_REFLECT_AUTOSCALE
	};

	// Image filter types
	enum image_filter_type_e {
		IMAGE_FILTER_BILINEAR,
		IMAGE_FILTER_NEAREST_NEIGHBOR
	};

	// Pixel format types
	enum pixfmt_type_e {
		PIXFMT_BGRA32 = 0,
		PIXFMT_BGRA32_PRE = 1,
		PIXFMT_RGBA32 = 2,
		PIXFMT_RGBA32_PRE = 3
	};

	// Opaque handles for image processing objects
	struct ImageAccessorHandle {
		void* accessor;
		void* pixfmt;
		void* renderBuffer;
	};

	struct SpanImageFilterHandle {
		void* filter;
		void* interpolator;
	};

	struct SpanInterpolatorHandle {
		void* interpolator;
	};

	struct SpanAllocatorHandle {
		void* allocator;
	};

	struct PixelFormatHandle {
		void* pixfmt;
		void* renderBuffer;
		pixfmt_type_e format;
	};

	struct ImageFilterHandle {
		void* filter;
		image_filter_type_e filterType;
	};

	// Image accessor management
	ImageAccessorHandle* CreateImageAccessorClone(uint8* buffer, int width, int height, int stride);
	ImageAccessorHandle* CreateImageAccessorWrap(uint8* buffer, int width, int height, int stride, wrap_mode_e wrapMode);
	void DeleteImageAccessor(ImageAccessorHandle* handle);

	// Span image filter management
	SpanImageFilterHandle* CreateSpanImageFilterRGBA32Bilinear(const ImageAccessorHandle* accessor);
	SpanImageFilterHandle* CreateSpanImageFilterRGBA32NearestNeighbor(const ImageAccessorHandle* accessor);
	void DeleteSpanImageFilter(SpanImageFilterHandle* handle);

	// Span interpolator management
	SpanInterpolatorHandle* CreateSpanInterpolatorLinear();
	void SetSpanInterpolatorTransform(SpanInterpolatorHandle* handle, const AffineTransformHandle* transform);
	void DeleteSpanInterpolator(SpanInterpolatorHandle* handle);

	// Span allocator management
	SpanAllocatorHandle* CreateSpanAllocator();
	void DeleteSpanAllocator(SpanAllocatorHandle* handle);

	// Pixel format management
	PixelFormatHandle* CreatePixelFormatBGRA32(uint8* buffer, int width, int height, int stride);
	PixelFormatHandle* CreatePixelFormatBGRA32Premultiplied(uint8* buffer, int width, int height, int stride);
	PixelFormatHandle* CreatePixelFormatRGBA32(uint8* buffer, int width, int height, int stride);
	void DeletePixelFormat(PixelFormatHandle* handle);

	// Blur operations
	status_t ApplyRecursiveBlur(uint8* buffer, int width, int height, int stride, double radius);
	status_t ApplyRecursiveBlurRGBA(uint8* buffer, int width, int height, int stride, double radius);

	// Image filter operations
	ImageFilterHandle* CreateImageFilterBilinear();
	void DeleteImageFilter(ImageFilterHandle* handle);

	// Utility functions
	void GetImageDimensions(const ImageAccessorHandle* handle, int* width, int* height);
	uint8* GetImageBuffer(const ImageAccessorHandle* handle);

	// ========== Strokes & Line Styles ==========
	// Line cap types
	enum line_cap_e {
		LINE_CAP_BUTT,
		LINE_CAP_ROUND,
		LINE_CAP_SQUARE
	};

	// Line join types
	enum line_join_e {
		LINE_JOIN_MITER,
		LINE_JOIN_ROUND,
		LINE_JOIN_BEVEL
	};

	// Inner join types
	enum inner_join_e {
		INNER_JOIN_BEVEL,
		INNER_JOIN_MITER,
		INNER_JOIN_JAG,
		INNER_JOIN_ROUND
	};

	// Filling rule types
	enum filling_rule_e {
		FILLING_RULE_NON_ZERO,
		FILLING_RULE_EVEN_ODD
	};

	// Alpha mask types
	enum alpha_mask_type_e {
		ALPHA_MASK_GRAY8
	};

	// Pixel order types
	enum pixel_order_e {
		PIXEL_ORDER_BGRA,
		PIXEL_ORDER_RGBA
	};

	// Opaque handles for stroke and line style objects
	struct StrokeStyleHandle {
		void* stroke;
		double width;
		line_cap_e lineCap;
		line_join_e lineJoin;
		double miterLimit;
	};

	struct LineProfileHandle {
		void* profile;
	};

	struct AlphaMaskHandle {
		void* mask;
		alpha_mask_type_e maskType;
	};

	struct ClippedAlphaMaskHandle {
		void* clippedMask;
	};

	// Line cap and join conversion functions
	line_cap_e ConvertToAGGLineCap(uint32 haikuLineCap);
	uint32 ConvertFromAGGLineCap(line_cap_e aggLineCap);
	line_join_e ConvertToAGGLineJoin(uint32 haikuLineJoin);
	uint32 ConvertFromAGGLineJoin(line_join_e aggLineJoin);
	inner_join_e ConvertToAGGInnerJoin(uint32 haikuInnerJoin);

	// Stroke style management
	StrokeStyleHandle* CreateStrokeStyle();
	void DeleteStrokeStyle(StrokeStyleHandle* handle);
	status_t SetStrokeWidth(StrokeStyleHandle* handle, double width);
	status_t SetStrokeLineCap(StrokeStyleHandle* handle, line_cap_e cap);
	status_t SetStrokeLineJoin(StrokeStyleHandle* handle, line_join_e join);
	status_t SetStrokeMiterLimit(StrokeStyleHandle* handle, double miterLimit);

	// Line profile management
	LineProfileHandle* CreateLineProfileAA();
	void DeleteLineProfile(LineProfileHandle* handle);
	status_t SetLineProfileWidth(LineProfileHandle* handle, double width);

	// Filling rule operations
	filling_rule_e ConvertToAGGFillingRule(uint32 haikuFillingRule);
	uint32 ConvertFromAGGFillingRule(filling_rule_e aggFillingRule);

	// Alpha mask management
	AlphaMaskHandle* CreateAlphaMaskGray8(uint8* buffer, int width, int height, int stride);
	ClippedAlphaMaskHandle* CreateClippedAlphaMask(const AlphaMaskHandle* baseMask, int x1, int y1, int x2, int y2);
	void DeleteAlphaMask(AlphaMaskHandle* handle);
	void DeleteClippedAlphaMask(ClippedAlphaMaskHandle* handle);

	// Pixel order operations
	pixel_order_e ConvertToAGGPixelOrder(uint32 haikuPixelOrder);
	uint32 ConvertFromAGGPixelOrder(pixel_order_e aggPixelOrder);

	// Advanced stroke operations
	status_t ApplyStrokeToPath(RenderPath* path, const StrokeStyleHandle* style);
	status_t CreateStrokedPath(const RenderPath* sourcePath, RenderPath** resultPath, const StrokeStyleHandle* style);

	// Stroke measurement
	double CalculateStrokeLength(const RenderPath* path, const StrokeStyleHandle* style);
	BRect CalculateStrokeBounds(const RenderPath* path, const StrokeStyleHandle* style);

	// Dash pattern support
	status_t SetStrokeDashPattern(StrokeStyleHandle* handle, const float* dashArray, int32 dashCount, float dashOffset);

	// ========== Coordinate Transformation Helpers ==========
	virtual BRect TransformAndClipRect(BRect rect) const;
	virtual BRect ClipRect(BRect rect) const;
	virtual BRect TransformAlignAndClipRect(BRect rect) const;
	virtual BRect AlignAndClipRect(BRect rect) const;
	virtual BRect AlignRect(BRect rect) const;
	virtual BPoint TransformPoint(BPoint point) const;
	virtual status_t TransformPoints(BPoint* points, int32 count) const;
	virtual BRect TransformRect(BRect rect) const;
	
	// Advanced transformation support
	virtual status_t SetPerspectiveTransform(float m00, float m01, float m02,
		float m10, float m11, float m12, float m20, float m21, float m22);
	virtual status_t SetBilinearTransform(BPoint quad[4]);
	virtual status_t SetViewportTransform(BRect viewport, BRect world);
	virtual status_t SetInterpolationMode(RenderInterpolationMode mode);
	
	virtual status_t SetRendererOffset(int32 offsetX, int32 offsetY);
	
	// ========== Scanline Processing Operations ==========
	
	// Advanced scanline operations
	virtual status_t ProcessScanlines(const BRect& rect, 
		void (*processor)(int32 y, float* spans, int32 numSpans, void* userData),
		void* userData);
	virtual status_t RenderScanlinesAA(RenderPath* path, bool useSubpixel = false);
	virtual status_t RenderScanlinesBin(RenderPath* path);
	
	// Scanline boolean operations
	virtual status_t ScanlineUnion(RenderPath* path1, RenderPath* path2, 
		RenderPath** result);
	virtual status_t ScanlineIntersection(RenderPath* path1, RenderPath* path2,
		RenderPath** result);
	virtual status_t ScanlineXor(RenderPath* path1, RenderPath* path2,
		RenderPath** result);
	
	// ========== Region-Based Rendering ==========
	
	// Direct region operations
	virtual status_t SetRenderingRegion(const BRegion* region);
	virtual status_t RenderToRegion(const BRegion* region, 
		void (*renderer)(const BRect& rect, void* userData), void* userData);
	virtual status_t FillRegionScanlines(const BRegion* region, 
		const rgb_color& color);
	
	// ========== Primitive Rendering Operations ==========
	
	// Direct primitive rendering (bypasses path system)
	virtual status_t RenderPixel(BPoint point, const rgb_color& color);
	virtual status_t RenderHorizontalLine(int32 y, int32 x1, int32 x2, 
		const rgb_color& color);
	virtual status_t RenderVerticalLine(int32 x, int32 y1, int32 y2,
		const rgb_color& color);
	virtual status_t RenderRectOutline(const BRect& rect, const rgb_color& color);
	
	// ========== Composition Operations ==========
	// Composition operation types (without template parameters)
	enum comp_op_e {
		COMP_OP_CLEAR = 0,
		COMP_OP_SRC = 1,
		COMP_OP_SRC_OVER = 2,
		COMP_OP_SRC_IN = 3,
		COMP_OP_SRC_OUT = 4,
		COMP_OP_SRC_ATOP = 5,
		COMP_OP_DST = 6,
		COMP_OP_DST_OVER = 7,
		COMP_OP_DST_IN = 8,
		COMP_OP_DST_OUT = 9,
		COMP_OP_DST_ATOP = 10,
		COMP_OP_XOR = 11,
		COMP_OP_PLUS = 12,
		COMP_OP_MINUS = 13,
		COMP_OP_MULTIPLY = 14,
		COMP_OP_SCREEN = 15,
		COMP_OP_OVERLAY = 16,
		COMP_OP_DARKEN = 17,
		COMP_OP_LIGHTEN = 18,
		COMP_OP_COLOR_DODGE = 19,
		COMP_OP_COLOR_BURN = 20,
		COMP_OP_HARD_LIGHT = 21,
		COMP_OP_SOFT_LIGHT = 22,
		COMP_OP_DIFFERENCE = 23,
		COMP_OP_EXCLUSION = 24
	};

	// Initialize composition operations
	status_t InitializeComposition();

	// Set composition operation mode
	void SetCompositionOperation(comp_op_e op);
	comp_op_e GetCompositionOperation() const;

	// Blend color values using specified operation
	void BlendPixel(uint8* dest, const uint8* src, comp_op_e op);
	void BlendPixelAlpha(uint8* dest, const uint8* src, uint8 alpha, comp_op_e op);

	// Perform composition operation on pixel buffers
	void CompositeBuffer(uint8* dest, const uint8* src, 
								int width, int height, 
								int destStride, int srcStride,
								comp_op_e op);

	// Composition with alpha mask
	void CompositeBufferMasked(uint8* dest, const uint8* src, const uint8* mask,
									  int width, int height,
									  int destStride, int srcStride, int maskStride,
									  comp_op_e op);

	// Porter-Duff composition operations
	void Clear(uint8* dest, int width, int height, int stride);
	void Copy(uint8* dest, const uint8* src, int width, int height, 
					 int destStride, int srcStride);
	void SourceOver(uint8* dest, const uint8* src, int width, int height,
						   int destStride, int srcStride);
	void SourceAtop(uint8* dest, const uint8* src, int width, int height,
						   int destStride, int srcStride);
	void DestinationOver(uint8* dest, const uint8* src, int width, int height,
								int destStride, int srcStride);
	void DestinationIn(uint8* dest, const uint8* src, int width, int height,
							  int destStride, int srcStride);
	void DestinationOut(uint8* dest, const uint8* src, int width, int height,
							   int destStride, int srcStride);
	void DestinationAtop(uint8* dest, const uint8* src, int width, int height,
								int destStride, int srcStride);
	void Xor(uint8* dest, const uint8* src, int width, int height,
					int destStride, int srcStride);

	// Blend mode operations
	void Darken(uint8* dest, const uint8* src, int width, int height,
					   int destStride, int srcStride);
	void Lighten(uint8* dest, const uint8* src, int width, int height,
						int destStride, int srcStride);
	void Difference(uint8* dest, const uint8* src, int width, int height,
						   int destStride, int srcStride);

	// ========== Path Geometry Operations ==========
	// Path command types
	enum path_cmd_e {
		PATH_CMD_MOVE_TO,
		PATH_CMD_LINE_TO,
		PATH_CMD_CURVE4,
		PATH_CMD_CLOSE,
		PATH_CMD_STOP,
		PATH_CMD_VERTEX
	};

	// Path command creation and validation
	uint32 CreatePathCommand(path_cmd_e cmd, float x, float y);
	bool IsPathCommandType(uint32 cmd, path_cmd_e type);
	bool ValidatePathCommand(uint32 cmd);
	bool IsPathCommandMoveTo(uint32 cmd);
	bool IsPathCommandLineTo(uint32 cmd);
	bool IsPathCommandCurve(uint32 cmd);
	bool IsPathCommandClose(uint32 cmd);
	bool IsPathCommandStop(uint32 cmd);
	bool IsPathCommandVertex(uint32 cmd);

	// Arc creation methods
	status_t CreateArc(RenderPath* path, float cx, float cy, float rx, float ry,
					   float startAngle, float endAngle);
	status_t CreateBezierArc(RenderPath* path, float cx, float cy, float rx, float ry,
							 float startAngle, float sweepAngle);
	status_t CreateSVGArc(RenderPath* path, float rx, float ry, float angle,
						  bool largeArcFlag, bool sweepFlag, float x, float y);

	// Shape creation
	status_t CreateEllipse(RenderPath* path, float cx, float cy, float rx, float ry);
	status_t CreateRoundedRect(RenderPath* path, float x1, float y1, float x2, float y2,
							   float r);
	status_t CreateRoundedRectVarying(RenderPath* path, float x1, float y1, float x2, float y2,
									  float rx1, float ry1, float rx2, float ry2,
									  float rx3, float ry3, float rx4, float ry4);

	// Path converters and transformations
	status_t ConvertPathToCurves(RenderPath* source, RenderPath** result);
	status_t TransformPath(RenderPath* path, const BAffineTransform& transform);
	status_t AddDashToPath(RenderPath* source, RenderPath** result,
						   const float* dashArray, int32 dashCount, float dashStart);
	status_t SmoothPath(RenderPath* source, RenderPath** result, float smoothValue);
	status_t CreateContourPath(RenderPath* source, RenderPath** result,
							   float width, bool counterClockwise = false);

	// Path clipping operations
	status_t ClipPathToPolygon(RenderPath* source, RenderPath** result,
							   const BPoint* vertices, int32 count);
	status_t ClipPathToPolyline(RenderPath* source, RenderPath** result,
								const BPoint* vertices, int32 count);

	// Path geometry utilities
	float CalculatePathLength(RenderPath* path);
	BRect CalculatePathBoundingRect(RenderPath* path);

	// ========== Rasterization & Scanlines ==========
	// Rasterizer types
	enum rasterizer_type_e {
		RASTERIZER_SCANLINE_AA,
		RASTERIZER_COMPOUND_AA,
		RASTERIZER_OUTLINE,
		RASTERIZER_OUTLINE_AA
	};

	// Scanline types
	enum scanline_type_e {
		SCANLINE_P8,
		SCANLINE_U8,
		SCANLINE_BIN,
		SCANLINE_U8_AM,
		SCANLINE_P8_SUBPIX,
		SCANLINE_U8_SUBPIX
	};

	// Scanline storage types
	enum scanline_storage_type_e {
		SCANLINE_STORAGE_AA8,
		SCANLINE_STORAGE_BIN,
		SCANLINE_STORAGE_SUBPIX8
	};

	// Renderer types
	enum renderer_type_e {
		RENDERER_SCANLINE_AA,
		RENDERER_SCANLINE_AA_SOLID,
		RENDERER_SCANLINE_BIN_SOLID,
		RENDERER_SCANLINE_SUBPIX_SOLID,
		RENDERER_OUTLINE_AA,
		RENDERER_PRIMITIVES,
		RENDERER_REGION
	};

	// Opaque handles for AGG objects
	struct RasterizerHandle {
		rasterizer_type_e type;
		void* rasterizer;
	};

	struct ScanlineHandle {
		scanline_type_e type;
		void* scanline;
	};

	struct ScanlineStorageHandle {
		scanline_storage_type_e type;
		void* storage;
	};

	struct RenderingBufferHandle {
		agg::rendering_buffer* buffer;
	};

	// Rasterizer management
	RasterizerHandle* CreateRasterizer(rasterizer_type_e type);
	status_t DeleteRasterizer(RasterizerHandle* rasterizer);
	status_t ResetRasterizer(RasterizerHandle* rasterizer);

	// Scanline management
	ScanlineHandle* CreateScanline(scanline_type_e type);
	status_t DeleteScanline(ScanlineHandle* scanline);

	// Scanline storage management
	ScanlineStorageHandle* CreateScanlineStorage(scanline_storage_type_e type);
	status_t DeleteScanlineStorage(ScanlineStorageHandle* storage);

	// Rendering buffer management
	RenderingBufferHandle* CreateRenderingBuffer(uint8* buffer, int32 width, int32 height, int32 stride);
	status_t DeleteRenderingBuffer(RenderingBufferHandle* buffer);
	status_t AttachBufferToRenderingBuffer(RenderingBufferHandle* handle, uint8* buffer,
										   int32 width, int32 height, int32 stride);

	// Rasterization operations
	status_t AddPathToRasterizer(RasterizerHandle* rasterizer, RenderPath* path);
	status_t RenderScanlines(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
							 renderer_type_e rendererType, const rgb_color& color);
	status_t RenderScanlinesCompound(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
									 const rgb_color* colors, int32 styleCount);

	// Scanline boolean operations
	status_t ScanlineUnion(ScanlineHandle* sl1, ScanlineHandle* sl2, ScanlineHandle* result);
	status_t ScanlineIntersection(ScanlineHandle* sl1, ScanlineHandle* sl2, ScanlineHandle* result);
	status_t ScanlineXor(ScanlineHandle* sl1, ScanlineHandle* sl2, ScanlineHandle* result);

	// Subpixel rendering support
	status_t SetSubpixelAccuracy(RasterizerHandle* rasterizer, float accuracy);
	bool IsRasterizerReady(RasterizerHandle* rasterizer);

	// ========== Rendering & Output ==========
	// Pixel format types (already defined earlier as pixfmt_type_e)

	// Scanline renderer types
	enum scanline_renderer_type_e {
		SCANLINE_RENDERER_AA_SOLID,
		SCANLINE_RENDERER_BIN_SOLID,
		SCANLINE_RENDERER_SUBPIX_SOLID
	};

	// Renderer handles
	struct RendererBaseHandle {
		pixfmt_type_e format;
		void* renderer;
		void* pixfmt;
	};

	struct ScanlineRendererHandle {
		scanline_renderer_type_e type;
		pixfmt_type_e baseFormat;
		void* renderer;
	};

	struct PrimitiveRendererHandle {
		pixfmt_type_e baseFormat;
		void* renderer;
	};

	struct RegionRendererHandle {
		pixfmt_type_e baseFormat;
		void* renderer;
	};

	// Base renderer management
	RendererBaseHandle* CreateRendererBase(RenderingBufferHandle* buffer, pixfmt_type_e format);
	status_t DeleteRendererBase(RendererBaseHandle* renderer);

	// Scanline renderers
	ScanlineRendererHandle* CreateScanlineRenderer(RendererBaseHandle* base, scanline_renderer_type_e type);
	status_t DeleteScanlineRenderer(ScanlineRendererHandle* renderer);

	// Primitive renderer
	PrimitiveRendererHandle* CreatePrimitiveRenderer(RendererBaseHandle* base);
	status_t DeletePrimitiveRenderer(PrimitiveRendererHandle* renderer);

	// Region renderer
	RegionRendererHandle* CreateRegionRenderer(RendererBaseHandle* base);
	status_t DeleteRegionRenderer(RegionRendererHandle* renderer);

	// High-level rendering operations
	status_t RenderScanlinesAA(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
							   ScanlineRendererHandle* renderer, const rgb_color& color);
	status_t RenderScanlinesCompoundAA(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
									   RendererBaseHandle* baseRenderer, 
									   const rgb_color* colors, int32 styleCount);

	// Renderer configuration
	status_t SetScanlineRendererColor(ScanlineRendererHandle* renderer, const rgb_color& color);
	status_t ClearRendererBase(RendererBaseHandle* renderer, const rgb_color& color);
	BRect GetRendererBounds(RendererBaseHandle* renderer);
	status_t SetRendererClipBox(RendererBaseHandle* renderer, const BRect& clipRect);
	bool IsRendererValid(RendererBaseHandle* renderer);

	// ========== Colors & Gradients ==========
	// Gradient types
	enum gradient_type_e {
		GRADIENT_LINEAR_X,
		GRADIENT_RADIAL,
		GRADIENT_RADIAL_FOCUS,
		GRADIENT_DIAMOND,
		GRADIENT_CONIC
	};

	// Gamma correction types
	enum gamma_type_e {
		GAMMA_POWER,
		GAMMA_NONE,
		GAMMA_THRESHOLD
	};

	// Color and gradient handles
	struct ColorInterpolatorHandle {
		void* interpolator;
		int32 steps;
		int32 currentStep;
	};

	struct GradientHandle {
		gradient_type_e type;
		void* gradient;
	};

	struct GradientLUTHandle {
		void* lut;
		int32 size;
	};

	struct SpanGradientHandle {
		GradientHandle* gradient;
		GradientLUTHandle* lut;
		void* interpolator;
		void* spanGradient;
	};

	struct GammaLUTHandle {
		gamma_type_e type;
		float gamma;
		void* lut;
	};

	// Color utilities
	agg::rgba8 ConvertToAGGColor(const rgb_color& color);
	rgb_color ConvertFromAGGColor(const agg::rgba8& color);
	agg::rgba ConvertToAGGColorFloat(const rgb_color& color);
	rgb_color ConvertFromAGGColorFloat(const agg::rgba& color);

	// Color interpolation
	ColorInterpolatorHandle* CreateColorInterpolator(const rgb_color& color1, const rgb_color& color2, int32 steps);
	status_t DeleteColorInterpolator(ColorInterpolatorHandle* interpolator);
	rgb_color GetInterpolatedColor(ColorInterpolatorHandle* interpolator, float position);

	// Gradient creation and management
	GradientHandle* CreateGradient(gradient_type_e type);
	status_t DeleteGradient(GradientHandle* gradient);

	// Gradient LUT (Lookup Table)
	GradientLUTHandle* CreateGradientLUT(int32 size);
	status_t DeleteGradientLUT(GradientLUTHandle* lut);
	status_t BuildGradientLUT(GradientLUTHandle* lut, const rgb_color* colors, 
							  const float* stops, int32 colorCount);

	// Span gradient
	SpanGradientHandle* CreateSpanGradient(GradientHandle* gradient, GradientLUTHandle* lut,
										   const BAffineTransform& transform);
	status_t DeleteSpanGradient(SpanGradientHandle* spanGradient);

	// Gamma correction
	GammaLUTHandle* CreateGammaLUT(gamma_type_e type, float gamma = 1.0f);
	status_t DeleteGammaLUT(GammaLUTHandle* lut);
	uint8 ApplyGammaCorrection(GammaLUTHandle* lut, uint8 value);

	// Color manipulation utilities
	rgb_color BlendColors(const rgb_color& color1, const rgb_color& color2, float ratio);
	rgb_color AdjustColorBrightness(const rgb_color& color, float brightness);
	rgb_color AdjustColorSaturation(const rgb_color& color, float saturation);
	float CalculateColorDistance(const rgb_color& color1, const rgb_color& color2);

	// ========== Transforms & Math ==========
	// Transform handles (AffineTransformHandle declared earlier)

	struct BilinearTransformHandle {
		void* transform; // agg::trans_bilinear*
	};

	struct PerspectiveTransformHandle {
		void* transform; // agg::trans_perspective*
	};

	// Double precision rectangle
	struct RectD {
		double x1, y1, x2, y2;
	};

	// Affine transform creation and management
	AffineTransformHandle* CreateAffineTransform();
	AffineTransformHandle* CreateAffineTransformFromMatrix(double sx, double shy, double shx, 
														   double sy, double tx, double ty);
	status_t DeleteAffineTransform(AffineTransformHandle* transform);

	// Affine transform operations
	status_t ResetAffineTransform(AffineTransformHandle* transform);
	status_t TranslateAffine(AffineTransformHandle* transform, double dx, double dy);
	status_t ScaleAffine(AffineTransformHandle* transform, double sx, double sy);
	status_t RotateAffine(AffineTransformHandle* transform, double angle);
	status_t SkewAffine(AffineTransformHandle* transform, double sx, double sy);
	status_t MultiplyAffine(AffineTransformHandle* transform, AffineTransformHandle* other);
	status_t InvertAffine(AffineTransformHandle* transform);

	// Transform point and coordinate operations
	status_t TransformPoint(AffineTransformHandle* transform, double* x, double* y);
	status_t InverseTransformPoint(AffineTransformHandle* transform, double* x, double* y);

	// Specialized transform creation
	AffineTransformHandle* CreateTranslationTransform(double dx, double dy);
	AffineTransformHandle* CreateScalingTransform(double sx, double sy);
	AffineTransformHandle* CreateRotationTransform(double angle);
	AffineTransformHandle* CreateSkewingTransform(double sx, double sy);

	// Bilinear transform
	BilinearTransformHandle* CreateBilinearTransform(const BPoint* quad, const BRect& rect);
	status_t DeleteBilinearTransform(BilinearTransformHandle* transform);
	status_t TransformPointBilinear(BilinearTransformHandle* transform, double* x, double* y);

	// Perspective transform
	PerspectiveTransformHandle* CreatePerspectiveTransform(const BPoint* quad, const BRect& rect);
	status_t DeletePerspectiveTransform(PerspectiveTransformHandle* transform);
	status_t TransformPointPerspective(PerspectiveTransformHandle* transform, double* x, double* y);

	// Math utilities
	double DegreesToRadians(double degrees);
	double RadiansToDegrees(double radians);

	// Bounding rectangle calculations
	BRect CalculateBoundingRect(RenderPath* path, AffineTransformHandle* transform = NULL);
	BRect CalculateBoundingRectD(const BPoint* points, int32 count);

	// Rectangle operations with double precision
	RectD CreateRectD(double x1, double y1, double x2, double y2);
	RectD TransformRectD(const RectD& rect, AffineTransformHandle* transform);
	BRect RectDToBRect(const RectD& rect);
	RectD BRectToRectD(const BRect& rect);

	// Transform utility functions
	bool IsTransformIdentity(AffineTransformHandle* transform);
	bool IsTransformValid(AffineTransformHandle* transform);
	double GetTransformScale(AffineTransformHandle* transform);
	double GetTransformRotation(AffineTransformHandle* transform);

	// Convert between BAffineTransform and AGG transform
	status_t ConvertToAGGTransform(const BAffineTransform& haikuTransform, 
								   AffineTransformHandle** aggTransform);
	status_t ConvertFromAGGTransform(AffineTransformHandle* aggTransform,
									 BAffineTransform* haikuTransform);

	// ========== Geometric Utility Functions ==========
	virtual float CalculateDistance(BPoint p1, BPoint p2) const;
	virtual float CalculateAngle(BPoint center, BPoint point) const;
	virtual BPoint CalculateIntersection(BPoint line1Start, BPoint line1End,
		BPoint line2Start, BPoint line2End) const;
	virtual bool LineIntersectsRect(BPoint lineStart, BPoint lineEnd,
		const BRect& rect) const;
	virtual BRect CalculateStrokeBounds(BPoint start, BPoint end,
		float width, cap_mode cap, join_mode join) const;
	
	// Polygon and curve utilities
	virtual bool PointInPolygon(BPoint point, const BPoint* vertices, int32 count) const;
	virtual BRect CalculatePolygonBounds(const BPoint* vertices, int32 count) const;
	virtual float CalculatePolygonArea(const BPoint* vertices, int32 count) const;

#if DEBUG
	// ========== Debug and Profiling ==========
	virtual void StartProfiling(const char* operationName);
	virtual void EndProfiling();
	virtual void DumpProfile() const;
	virtual void SetDebugMode(bool enable);
	virtual void DumpState() const;
#endif

	// Forward declaration for StringRenderer (Following EXACT AGGTextRenderer Pattern)
	class StringRenderer;
	friend class StringRenderer;

	// Forward declarations for handles
	struct AffineTransformHandle {
		void* transform; // agg::trans_affine*
	};

	// AGG-specific structures - implementation details
	struct AGGCompoundShape : public RenderCompoundShape {
		agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer;
		uint32 styleCount;
		rgb_color* styles;
	};

	struct AGGPath : public RenderPath {
		void* fPath;      // Opaque AGG path_storage
		void* fCurve;     // Opaque AGG curve converter
	};

	struct AGGGradient : public RenderGradient {
		enum Type {
			LINEAR,
			RADIAL,
			RADIAL_FOCUS,
			DIAMOND,
			CONIC
		};
		
		Type type;
		void* fGradientData;  // Opaque gradient data
	};

	// Internal helper methods
	void _UpdateLineWidth();
	void _UpdateDrawingMode();
	void _UpdateFont();
	void _SetRendererColor(const rgb_color& color) const;
	float _Align(float coord, bool round, bool centerOffset) const;
	void _Align(BPoint* point, bool round, bool centerOffset) const;
	void _Align(BPoint* point, bool centerOffset = true) const;
	void _Align(BRect* rect, bool centerOffset = true) const;
	BPoint _Align(const BPoint& point, bool centerOffset = true) const;
	BRect _Clipped(const BRect& rect) const;
	
	// AGG-specific operations (migrated from scattered files)
	void SetTransformDirect(const BAffineTransform& transform, int32 xOffset, int32 yOffset);
	
	// Private helper methods for renderer access
	status_t _GetCurrentRendererBase(void** renderer, void** pixfmt);
	status_t _SetupAGGRenderer();
	
	// Path rasterization methods - implementation in .cpp files
	void _MakeLinearGradientMatrix(BPoint startPoint, BPoint endPoint,
		void* matrix, float gradient_d2) const;
	void _MakeRadialGradientMatrix(BPoint center, float fx, float fy,
		void* matrix, float gradient_d2) const;
	void _MakeGradientArray(void* array, const BGradient& gradient) const;
	
	// Font bitmap operations (from FontEngine.cpp)
	template<class Scanline, class ScanlineStorage>
	void DecomposeFTBitmapMono(const FT_Bitmap& bitmap, int x, int y,
		bool flip_y, Scanline& sl, ScanlineStorage& storage);
	template<class Scanline, class ScanlineStorage>
	void DecomposeFTBitmapGray8(const FT_Bitmap& bitmap, int x, int y,
		bool flip_y, Scanline& sl, ScanlineStorage& storage);
	template<class Scanline, class ScanlineStorage>
	void DecomposeFTBitmapSubpix(const FT_Bitmap& bitmap, int x, int y,
		bool flip_y, Scanline& sl, ScanlineStorage& storage);
	
	// Blur operations (from CursorManager.cpp)
	status_t CreateShadowBitmap(const ServerBitmap& source, ServerBitmap& shadow, 
		float shadowStrength, int32 offset);
	status_t ApplyRecursiveBlur(const BRect& rect, float radius);
	
	// Bounding box operations (from ServerFont.cpp)
	status_t GetBoundingBoxes(const ServerFont& font, const char* string, int32 numBytes, int32 numChars,
		BRect rectArray[], bool stringEscapement, font_metric_mode mode,
		escapement_delta delta, bool asString);
	status_t GetBoundingBoxesForStrings(const ServerFont& font, char *charArray[], size_t lengthArray[],
		int32 numStrings, BRect rectArray[], font_metric_mode mode,
		escapement_delta deltaArray[]);
	status_t CalculateTextBoundingBox(const char* string, int32 numBytes, 
		BRect& boundingBox) const;
	status_t CalculateAGGBoundingRect(void* path, BRect& bounds) const;
	
	// Advanced rendering helpers
	status_t _InitializeCompoundRasterizer(AGGCompoundShape* compound);
	status_t _RenderCompoundPaths(AGGCompoundShape* compound);
	status_t _ApplyStackBlur(uint8* buffer, int32 width, int32 height,
		int32 stride, float radius);
	status_t _CreateImageSpan(const ServerBitmap* bitmap,
		RenderBitmapFilter filter, BRect sourceRect, BRect destRect);
	status_t _SetupPerspectiveTransform(const float* matrix);
	status_t _ValidatePathOperation(RenderPath* path) const;
	BRect _CalculatePathBounds(const void* path,
		const void* transform = nullptr) const;
		
	// Stack blur helper methods (following EXACT Haiku StackBlurFilter pattern)
	void _ApplyStackBlurHorizontal(uint8* rowBits, int32 width, int32 blurRadius);
	void _ApplyStackBlurVertical(uint8* colBits, int32 height, int32 bytesPerRow, int32 blurRadius);
		
	// Gradient helper methods following EXACT Haiku pattern
	void _CalcLinearGradientTransform(BPoint startPoint, BPoint endPoint,
		void* mtx, float gradient_d2 = 100.0f) const;
	void _CalcRadialGradientTransform(BPoint center, void* mtx,
		float gradient_d2 = 100.0f) const;
	void _MakeGradient(const BGradient& gradient, int32 colorCount,
		uint32* colors, int32 arrayOffset, int32 arraySize) const;
	
	template<class Array>
	void _MakeGradient(Array& array, const BGradient& gradient) const;
	
	// Template methods following EXACT Haiku Painter pattern
	template<class VertexSource>
	BRect _BoundingBox(VertexSource& path) const;
	
	template<class VertexSource>
	BRect _StrokePath(VertexSource& path) const;
	
	template<class VertexSource>
	BRect _StrokePath(VertexSource& path, cap_mode capMode) const;
	
	template<class VertexSource>
	BRect _FillPath(VertexSource& path) const;
	
	template<class VertexSource>
	BRect _RasterizePath(VertexSource& path) const;
	
	template<class VertexSource>
	BRect _FillPath(VertexSource& path, const BGradient& gradient);
	
	template<class VertexSource>
	BRect _RasterizePath(VertexSource& path, const BGradient& gradient);
	
	// Gradient template method following EXACT Haiku pattern
	template<class VertexSource, typename GradientFunction>
	void _RasterizePath(VertexSource& path, const BGradient& gradient,
		void* function, void* gradientTransform,
		int gradientStop = 100);
	
	BRect _DrawTriangle(BPoint pt1, BPoint pt2, BPoint pt3, bool fill) const;
	
	void _IterateShapeData(const int32& opCount, const uint32* opList,
		const int32& ptCount, const BPoint* points,
		const BPoint& viewToScreenOffset, float viewScale) const;
	
	void _InvertRect32(BRect r) const;
	void _BlendRect32(const BRect& r, const rgb_color& c) const;
	float _CrossProduct(BPoint a, BPoint b) const;
	
	void _UpdateAGGClipping();
	void _UpdateAGGBlendingMode();
	void _SetRendererColor(const rgb_color& color);

	// Composition operations helpers
	static comp_op_e sCurrentOperation;
	
	// Internal composition helper functions
	static void _BlendPixelClear(uint8* dest);
	static void _BlendPixelSrcOver(uint8* dest, const uint8* src);
	static void _BlendPixelDarken(uint8* dest, const uint8* src);
	static void _BlendPixelLighten(uint8* dest, const uint8* src);
	static void _BlendPixelDifference(uint8* dest, const uint8* src);
	static void _BlendPixelXor(uint8* dest, const uint8* src);
	
	// Color math utilities
	static uint8 _MultiplyAlpha(uint8 value, uint8 alpha);
	static uint8 _PremultiplyAlpha(uint8 value, uint8 alpha);
	static uint8 _UnpremultiplyAlpha(uint8 value, uint8 alpha);

	// State structure for push/pop
	struct RenderState {
		BAffineTransform transform;
		BRegion* clippingRegion;
		rgb_color highColor;
		rgb_color lowColor;
		pattern renderPattern;  // Renamed to avoid conflict with pattern type
		drawing_mode drawingMode;
		source_alpha srcAlpha;
		alpha_function alphaFunc;
		float penSize;
		cap_mode lineCap;
		join_mode joinMode;
		float miterLimit;
		int32 fillRule;
		bool subpixelPrecise;
		RenderTextMode textMode;
		bool hinting;
		bool antialiasing;
		bool kerning;
	};
	
	// Forward declaration for internal text renderer
	class AGGTextRendererInternal;

	// AGG-specific members - direct AGG objects
	agg::rendering_buffer fAGGBuffer;
	agg::pixfmt_rgba32* fPixelFormat;
	agg::renderer_base<agg::pixfmt_rgba32>* fBaseRenderer;
	PatternHandler* fPatternHandler;
	AGGTextRendererInternal* fInternalTextRenderer;
	agg::trans_affine fTransform;
	
	// Rendering state
	RenderingBuffer* fBuffer;
	BRegion* fClippingRegion;
	bool fValidClipping;
	drawing_mode fDrawingMode;
	source_alpha fAlphaSrcMode;
	alpha_function fAlphaFncMode;
	float fPenSize;
	float fLineWidth;
	cap_mode fLineCapMode;
	join_mode fLineJoinMode;
	float fMiterLimit;
	int32 fFillRule;
	bool fSubpixelPrecise;
	bool fIdentityTransform;
	int32 fQualityLevel;
	
	// State stack for push/pop
	BObjectList<RenderState> fStateStack;
	
	// Renderer offset
	int32 fRendererOffsetX;
	int32 fRendererOffsetY;
	
	// Alpha mask support
	agg::alpha_mask_gray8* fAlphaMask;
	const ServerBitmap* fAlphaMaskBitmap;
	
	// Text rendering state
	RenderTextMode fTextMode;
	bool fHinting;
	bool fAntialiasing;
	bool fKerning;
	uint8 fSubpixelAverageWeight;
	float fTextGamma;
	mutable bool fFontNeedsUpdate;
	
	// Font manager (shared)
	FontManager* fFontManager;
	
	// Internal string renderer for text encapsulation
	StringRenderer* fStringRenderer;
	
	// Error handling
	mutable status_t fLastError;
	mutable BString fLastErrorString;
	
	// Capability flags
	uint32 fCapabilities;
	
	// Memory tracking
	mutable size_t fMemoryUsage;
	
#if DEBUG
	// Debug and profiling
	bool fDebugMode;
	BString fCurrentOperation;
	bigtime_t fProfileStart;
	bigtime_t fTotalProfileTime;
	uint32 fProfileCallCount;
#endif
};

#endif // AGG_RENDER_H