/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _THORVG_BACKEND_HPP
#define _THORVG_BACKEND_HPP

#include "../RenderBackend.hpp"
#include <vector>

namespace tvg {
	class SwCanvas;
	class Scene;
	class Shape;
	class Picture;
	class Text;
	class LinearGradient;
	class RadialGradient;
	class Fill;
	class Paint;
	enum class BlendMethod;
	enum class StrokeCap;
	enum class StrokeJoin;
	enum class FillSpread;
	enum class TextWrap;
}

class ThorVGBackend : public RenderBackend {
public:
								ThorVGBackend();
	virtual						~ThorVGBackend();

	// Target
	virtual status_t			SetTarget(void* buffer, uint32 stride,
									uint32 width, uint32 height,
									pixel_format format);
	virtual uint32				Width() const { return fWidth; }
	virtual uint32				Height() const { return fHeight; }

	// Clear
	virtual void				Clear();
	virtual void				Clear(const KosmColor& color);

	// Fill with solid color
	virtual void				FillRect(const KosmRect& rect,
									const KosmColor& color);
	virtual void				FillRoundRect(const KosmRect& rect,
									float rx, float ry,
									const KosmColor& color);
	virtual void				FillCircle(const KosmPoint& center,
									float radius, const KosmColor& color);
	virtual void				FillEllipse(const KosmPoint& center,
									float rx, float ry,
									const KosmColor& color);
	virtual void				FillPath(void* pathHandle,
									const KosmColor& color);

	// Fill with gradient
	virtual void				FillRectGradient(const KosmRect& rect,
									void* gradientHandle);
	virtual void				FillRoundRectGradient(const KosmRect& rect,
									float rx, float ry,
									void* gradientHandle);
	virtual void				FillCircleGradient(const KosmPoint& center,
									float radius, void* gradientHandle);
	virtual void				FillEllipseGradient(const KosmPoint& center,
									float rx, float ry,
									void* gradientHandle);
	virtual void				FillPathGradient(void* pathHandle,
									void* gradientHandle);

	// Stroke
	virtual void				StrokeRect(const KosmRect& rect,
									const KosmColor& color,
									const KosmStrokeStyle& style);
	virtual void				StrokeRoundRect(const KosmRect& rect,
									float rx, float ry,
									const KosmColor& color,
									const KosmStrokeStyle& style);
	virtual void				StrokeCircle(const KosmPoint& center,
									float radius, const KosmColor& color,
									const KosmStrokeStyle& style);
	virtual void				StrokeEllipse(const KosmPoint& center,
									float rx, float ry,
									const KosmColor& color,
									const KosmStrokeStyle& style);
	virtual void				StrokeLine(const KosmPoint& from,
									const KosmPoint& to,
									const KosmColor& color,
									const KosmStrokeStyle& style);
	virtual void				StrokePath(void* pathHandle,
									const KosmColor& color,
									const KosmStrokeStyle& style);
	virtual void				StrokePathGradient(void* pathHandle,
									void* gradientHandle,
									const KosmStrokeStyle& style);

	// Image
	virtual void				DrawImage(void* imageHandle,
									const KosmPoint& position);
	virtual void				DrawImage(void* imageHandle,
									const KosmRect& destRect);
	virtual void				DrawImage(void* imageHandle,
									const KosmRect& srcRect,
									const KosmRect& destRect);

	// Text
	virtual void				DrawText(const char* text,
									const KosmPoint& position,
									void* fontHandle,
									const KosmColor& color);
	virtual void				DrawTextGradient(const char* text,
									const KosmPoint& position,
									void* fontHandle,
									void* gradientHandle);
	virtual void				DrawTextWithOutline(const char* text,
									const KosmPoint& position,
									void* fontHandle,
									const KosmColor& fillColor,
									const KosmColor& outlineColor,
									float outlineWidth);
	virtual void				DrawTextInRect(const char* text,
									const KosmRect& rect,
									void* fontHandle,
									const KosmColor& color,
									kosm_text_align align,
									kosm_text_wrap wrap);

	// State
	virtual void				PushState();
	virtual void				PopState();

	// Transform
	virtual void				SetTransform(const KosmMatrix& matrix);
	virtual KosmMatrix			GetTransform() const;

	// Clip
	virtual void				SetClipRect(const KosmRect& rect);
	virtual void				SetClipRoundRect(const KosmRect& rect,
									float radius);
	virtual void				SetClipCircle(const KosmPoint& center,
									float radius);
	virtual void				SetClipPath(void* pathHandle);
	virtual void				ResetClip();

	// Opacity & Blend
	virtual void				SetOpacity(float opacity);
	virtual float				GetOpacity() const;
	virtual void				SetBlendMode(kosm_blend_mode mode);
	virtual kosm_blend_mode		GetBlendMode() const;

	// Shadow & Effects
	virtual void				SetShadow(const KosmColor& color,
									float offsetX, float offsetY,
									float blur);
	virtual void				ClearShadow();
	virtual void				SetBlur(float sigma);
	virtual void				ClearBlur();

	// Mask
	virtual void				BeginMask();
	virtual void				EndMask();
	virtual void				ApplyMask();
	virtual void				ClearMask();

	// Layer
	virtual void				BeginLayer(const KosmRect& bounds,
									float opacity);
	virtual void				EndLayer();

	// Flush
	virtual status_t			Flush();

	// Path
	virtual void*				CreatePath();
	virtual void				DestroyPath(void* path);
	virtual void*				DuplicatePath(void* path);
	virtual void				PathMoveTo(void* path, float x, float y);
	virtual void				PathLineTo(void* path, float x, float y);
	virtual void				PathCubicTo(void* path,
									float cx1, float cy1,
									float cx2, float cy2,
									float x, float y);
	virtual void				PathClose(void* path);
	virtual void				PathReset(void* path);
	virtual void				PathAddRect(void* path, const KosmRect& rect);
	virtual void				PathAddRoundRect(void* path,
									const KosmRect& rect, float rx, float ry);
	virtual void				PathAddCircle(void* path,
									const KosmPoint& center, float radius);
	virtual void				PathAddEllipse(void* path,
									const KosmPoint& center,
									float rx, float ry);
	virtual void				PathAddArc(void* path,
									const KosmPoint& center, float radius,
									float startAngle, float sweepAngle);
	virtual void				PathAppend(void* path, void* other);
	virtual KosmRect			PathBounds(void* path);
	virtual void				PathSetFillRule(void* path,
									kosm_fill_rule rule);

	// Gradient
	virtual void*				CreateLinearGradient(float x1, float y1,
									float x2, float y2);
	virtual void*				CreateRadialGradient(float cx, float cy,
									float radius, float fx, float fy,
									float focalRadius);
	virtual void				DestroyGradient(void* gradient);
	virtual void				GradientAddColorStop(void* gradient,
									float offset, const KosmColor& color);
	virtual void				GradientSetColorStops(void* gradient,
									const KosmColorStop* stops,
									uint32 count);
	virtual void				GradientSetSpread(void* gradient,
									kosm_gradient_spread spread);
	virtual void				GradientSetTransform(void* gradient,
									const KosmMatrix& matrix);

	// Image
	virtual void*				CreateImage();
	virtual void				DestroyImage(void* image);
	virtual status_t			ImageLoad(void* image, const char* path);
	virtual status_t			ImageLoadData(void* image,
									const void* data, size_t size,
									const char* mimeType);
	virtual status_t			ImageSetPixels(void* image,
									const uint32* pixels,
									uint32 width, uint32 height,
									bool premultiplied);
	virtual uint32				ImageWidth(void* image);
	virtual uint32				ImageHeight(void* image);
	virtual void				ImageSetSize(void* image,
									float width, float height);
	virtual void				ImageSetTransform(void* image,
									const KosmMatrix& matrix);
	virtual void				ImageSetOpacity(void* image, float opacity);

	// Font
	virtual status_t			LoadFont(const char* path);
	virtual status_t			LoadFontData(const char* name,
									const void* data, size_t size);
	virtual status_t			UnloadFont(const char* path);
	virtual void*				CreateFont(const char* family, float size);
	virtual void				DestroyFont(void* font);
	virtual void				FontSetSize(void* font, float size);
	virtual void				FontSetItalic(void* font, float shear);
	virtual void				FontSetLetterSpacing(void* font,
									float spacing);
	virtual void				FontSetLineSpacing(void* font, float spacing);
	virtual float				FontMeasureWidth(void* font, const char* text);
	virtual KosmRect			FontMeasureBounds(void* font, const char* text);

private:
	struct State {
		KosmMatrix			transform;
		KosmRect			clipRect;
		bool				hasClipRect;
		tvg::Shape*			clipPath;
		float				opacity;
		kosm_blend_mode		blendMode;

		// Shadow
		bool				hasShadow;
		KosmColor			shadowColor;
		float				shadowOffsetX;
		float				shadowOffsetY;
		float				shadowBlur;

		// Blur
		float				blur;

		State()
			:
			hasClipRect(false),
			clipPath(nullptr),
			opacity(1.0f),
			blendMode(KOSM_BLEND_NORMAL),
			hasShadow(false),
			shadowOffsetX(0),
			shadowOffsetY(0),
			shadowBlur(0),
			blur(0)
		{
		}
	};

	struct FontInfo {
		char*				family;
		float				size;
		float				italicShear;
		float				letterSpacing;
		float				lineSpacing;
	};

			void				_ApplyFill(tvg::Shape* shape,
									const KosmColor& color);
			void				_ApplyGradientFill(tvg::Shape* shape,
									void* gradientHandle);
			void				_ApplyStroke(tvg::Shape* shape,
									const KosmColor& color,
									const KosmStrokeStyle& style);
			void				_ApplyGradientStroke(tvg::Shape* shape,
									void* gradientHandle,
									const KosmStrokeStyle& style);
			void				_ApplyState(tvg::Shape* shape);
			void				_ApplyStateToScene(tvg::Scene* scene);
			tvg::Shape*			_CreateClipShape();
			void				_DrawShadow(tvg::Shape* shape);
			tvg::BlendMethod	_ConvertBlendMode(kosm_blend_mode mode);
			tvg::StrokeCap		_ConvertLineCap(kosm_line_cap cap);
			tvg::StrokeJoin		_ConvertLineJoin(kosm_line_join join);
			tvg::FillSpread		_ConvertSpread(kosm_gradient_spread spread);
			tvg::TextWrap		_ConvertTextWrap(kosm_text_wrap wrap);

			tvg::SwCanvas*		fCanvas;
			tvg::Scene*			fScene;
			uint32				fWidth;
			uint32				fHeight;

			State				fCurrentState;
			std::vector<State>	fStateStack;

			// For masking
			tvg::Scene*			fMaskScene;
			bool				fInMask;

			// For layers
			struct LayerInfo {
				tvg::Scene*		scene;
				float			opacity;
			};
			std::vector<LayerInfo> fLayerStack;
};

#endif /* _THORVG_BACKEND_HPP */
