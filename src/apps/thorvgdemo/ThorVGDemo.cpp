/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmVG Demo - bouncing shapes with collision
 * Showcases: gradients, shadows, strokes, transforms, bezier paths,
 *            dash patterns, opacity, per-corner radii, conic gradients.
 * Renders directly into BBitmap (no KosmSurface).
 *
 * Tracing focuses on KosmVG library performance and rendering correctness:
 *   - Per-shape render timing and fill rate (pixels/us)
 *   - Shadow overhead measurement
 *   - Per-feature breakdown (gradient type, stroke, clip, etc.)
 *   - Pixel correctness: verify rendered output at shape centers
 *   - Frame timing: min/avg/p50/p95/p99/max with histogram
 */

#include <Application.h>
#include <Bitmap.h>
#include <FindDirectory.h>
#include <OS.h>
#include <Path.h>
#include <View.h>
#include <Window.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <optional>

#include <kosmvg.hpp>


static const bigtime_t kPulseRate = 16667; // ~60 fps
static const int kShapeCount = 8;
static const int kTraceFrames = 1800; // trace first 1800 frames (~30 sec)

// Shape feature tags for analysis
enum ShapeFeature {
	kFeatureRadialGrad	= 0x01,
	kFeatureLinearGrad	= 0x02,
	kFeatureConicGrad	= 0x04,
	kFeatureSolidFill	= 0x08,
	kFeatureShadow		= 0x10,
	kFeatureStroke		= 0x20,
	kFeatureDashStroke	= 0x40,
	kFeatureBezier		= 0x80,
	kFeatureRotation	= 0x100,
	kFeatureOpacity		= 0x200,
	kFeatureRoundRect	= 0x400
};

struct ShapeInfo {
	const char*	name;
	int			features;	// bitmask of ShapeFeature
	float		areaFactor;	// multiplier: area ≈ factor * radius²
};

static const ShapeInfo kShapeInfo[] = {
	// 0: circle — radial gradient + shadow
	{"circle",    kFeatureRadialGrad | kFeatureShadow,           3.14159f},
	// 1: rect — linear gradient + stroke
	{"rect",      kFeatureLinearGrad | kFeatureStroke,            1.96f},  // (1.4r)²
	// 2: triangle — solid fill + shadow + rotation
	{"triangle",  kFeatureSolidFill | kFeatureShadow | kFeatureRotation, 1.30f},
	// 3: star — solid fill + dashed stroke + rotation
	{"star",      kFeatureSolidFill | kFeatureDashStroke | kFeatureRotation, 1.0f},
	// 4: heart — bezier + radial gradient + opacity + rotation
	{"heart",     kFeatureBezier | kFeatureRadialGrad | kFeatureOpacity | kFeatureRotation, 1.50f},
	// 5: ellipse — linear gradient + rotation
	{"ellipse",   kFeatureLinearGrad | kFeatureRotation,          2.65f},  // π*1.3*0.65
	// 6: rrect — rounded rect + shadow + stroke
	{"rrect",     kFeatureRoundRect | kFeatureShadow | kFeatureStroke, 2.25f}, // (1.5r)²
	// 7: hexagon — conic gradient + rotation
	{"hexagon",   kFeatureConicGrad | kFeatureRotation,           2.60f}   // 2.598*r²
};

// Enable fast gradient blend path (draws outside gradient range too)
static const auto kGradExtend =
	kvg::GradientDrawingOptions::DrawsBeforeStart
	| kvg::GradientDrawingOptions::DrawsAfterEnd;


struct Shape {
	float x, y;			// position (center)
	float vx, vy;		// velocity
	float radius;		// bounding radius for collision
	uint8 r, g, b;		// base color
	int type;			// 0..7
	float angle;		// rotation (radians)
	float omega;		// angular velocity (radians/sec)

	// Max visual extent from center (may differ from collision radius)
	float VisualRadius() const
	{
		switch (type) {
			case 4: return radius * 1.31f;				// heart diagonal when rotated
			case 5: return radius * 1.3f;				// ellipse major axis
			case 6: return radius * 0.75f * 1.414f;	// rounded rect half-diagonal
			default: return radius;
		}
	}

	// Approximate pixel area for fill rate calculation
	float ApproxArea() const
	{
		return kShapeInfo[type].areaFactor * radius * radius;
	}

	void Move(float dt, float width, float height)
	{
		x += vx * dt;
		y += vy * dt;
		angle += omega * dt;

		// Bounce off walls using visual extent
		float vr = VisualRadius();
		if (x - vr < 0) {
			x = vr;
			vx = -vx;
		}
		if (x + vr > width) {
			x = width - vr;
			vx = -vx;
		}
		if (y - vr < 0) {
			y = vr;
			vy = -vy;
		}
		if (y + vr > height) {
			y = height - vr;
			vy = -vy;
		}
	}

	bool CollidesWith(const Shape& other) const
	{
		float dx = x - other.x;
		float dy = y - other.y;
		float dist = sqrtf(dx * dx + dy * dy);
		return dist < (radius + other.radius);
	}

	void ResolveCollision(Shape& other)
	{
		float dx = other.x - x;
		float dy = other.y - y;
		float dist = sqrtf(dx * dx + dy * dy);

		if (dist < 0.001f)
			return;

		// Normalize
		float nx = dx / dist;
		float ny = dy / dist;

		// Relative velocity
		float dvx = vx - other.vx;
		float dvy = vy - other.vy;

		// Relative velocity along collision normal
		float dvn = dvx * nx + dvy * ny;

		// Only resolve if objects are approaching
		if (dvn > 0) {
			// Simple elastic collision (equal mass)
			vx -= dvn * nx;
			vy -= dvn * ny;
			other.vx += dvn * nx;
			other.vy += dvn * ny;

			// Separate objects
			float overlap = (radius + other.radius - dist) / 2.0f;
			x -= overlap * nx;
			y -= overlap * ny;
			other.x += overlap * nx;
			other.y += overlap * ny;
		}
	}
};


class KosmVGView : public BView {
public:
	KosmVGView()
		:
		BView(BRect(0, 0, 799, 599), "KosmVGView", B_FOLLOW_ALL,
			B_WILL_DRAW | B_FRAME_EVENTS | B_PULSE_NEEDED),
		fBitmap(NULL),
		fTraceFile(NULL),
		fFrameNum(0),
		fStatsCount(0),
		fBgRenderAccum(0),
		fPixelCheckPassed(0),
		fPixelCheckFailed(0)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		memset(fShapeRenderTime, 0, sizeof(fShapeRenderTime));
		memset(fShapeRenderAccum, 0, sizeof(fShapeRenderAccum));
		_InitShapes();
		_OpenTrace();
	}

	virtual ~KosmVGView()
	{
		if (fTraceFile) {
			_WriteSummary();
			fclose(fTraceFile);
		}
		delete fBitmap;
	}

	virtual void AttachedToWindow()
	{
		_InitCanvas();
		_Render();

		// Use Pulse() for animation — simpler, no BMessage overhead
		Window()->SetPulseRate(kPulseRate);
	}

	virtual void Pulse()
	{
		bigtime_t t0 = system_time();
		_Update();
		bigtime_t t1 = system_time();
		_Render();
		bigtime_t t2 = system_time();

		// Pixel correctness check (every 30 frames to keep overhead low)
		if (fFrameNum % 30 == 0)
			_CheckPixels();

		Invalidate();
		bigtime_t t3 = system_time();

		_TraceFrame(t0, t1, t2, t3);
	}

	virtual void Draw(BRect updateRect)
	{
		bigtime_t t0 = system_time();
		if (fBitmap != NULL)
			DrawBitmap(fBitmap, B_ORIGIN);
		bigtime_t t1 = system_time();

		bigtime_t elapsed = t1 - t0;
		if (fStatsCount > 0 && fStatsCount <= kTraceFrames)
			fDrawBitmapTimes[fStatsCount - 1] = elapsed;

		if (fTraceFile && fFrameNum <= kTraceFrames)
			fprintf(fTraceFile, "  blit: %lld us\n", (long long)elapsed);
	}

	virtual void FrameResized(float width, float height)
	{
		_InitCanvas();
		_Render();
		Invalidate();
	}

private:
	void _InitShapes()
	{
		// 0: Circle - radial gradient + shadow
		fShapes[0].x = 120;
		fShapes[0].y = 120;
		fShapes[0].vx = 110;
		fShapes[0].vy = 75;
		fShapes[0].radius = 42;
		fShapes[0].r = 220;
		fShapes[0].g = 60;
		fShapes[0].b = 60;
		fShapes[0].type = 0;
		fShapes[0].angle = 0;
		fShapes[0].omega = 0;

		// 1: Rectangle - linear gradient + stroke outline
		fShapes[1].x = 350;
		fShapes[1].y = 200;
		fShapes[1].vx = -95;
		fShapes[1].vy = 85;
		fShapes[1].radius = 45;
		fShapes[1].r = 60;
		fShapes[1].g = 190;
		fShapes[1].b = 60;
		fShapes[1].type = 1;
		fShapes[1].angle = 0;
		fShapes[1].omega = 0;

		// 2: Triangle - solid + shadow + rotation
		fShapes[2].x = 550;
		fShapes[2].y = 150;
		fShapes[2].vx = 65;
		fShapes[2].vy = -100;
		fShapes[2].radius = 38;
		fShapes[2].r = 60;
		fShapes[2].g = 100;
		fShapes[2].b = 220;
		fShapes[2].type = 2;
		fShapes[2].angle = 0;
		fShapes[2].omega = 1.5f;

		// 3: Star - solid gold + dashed stroke + rotation
		fShapes[3].x = 200;
		fShapes[3].y = 400;
		fShapes[3].vx = -75;
		fShapes[3].vy = -65;
		fShapes[3].radius = 40;
		fShapes[3].r = 230;
		fShapes[3].g = 200;
		fShapes[3].b = 50;
		fShapes[3].type = 3;
		fShapes[3].angle = 0;
		fShapes[3].omega = -1.0f;

		// 4: Heart - bezier + gradient + opacity
		fShapes[4].x = 600;
		fShapes[4].y = 400;
		fShapes[4].vx = 85;
		fShapes[4].vy = 95;
		fShapes[4].radius = 38;
		fShapes[4].r = 240;
		fShapes[4].g = 70;
		fShapes[4].b = 110;
		fShapes[4].type = 4;
		fShapes[4].angle = 0;
		fShapes[4].omega = 0.5f;

		// 5: Ellipse - linear gradient + rotation
		fShapes[5].x = 180;
		fShapes[5].y = 250;
		fShapes[5].vx = 55;
		fShapes[5].vy = -85;
		fShapes[5].radius = 55;
		fShapes[5].r = 60;
		fShapes[5].g = 200;
		fShapes[5].b = 200;
		fShapes[5].type = 5;
		fShapes[5].angle = 0;
		fShapes[5].omega = 2.0f;

		// 6: Rounded rect - per-corner radii + shadow + thick stroke
		fShapes[6].x = 480;
		fShapes[6].y = 120;
		fShapes[6].vx = -65;
		fShapes[6].vy = 80;
		fShapes[6].radius = 44;
		fShapes[6].r = 220;
		fShapes[6].g = 140;
		fShapes[6].b = 40;
		fShapes[6].type = 6;
		fShapes[6].angle = 0;
		fShapes[6].omega = 0;

		// 7: Hexagon - conic gradient + rotation
		fShapes[7].x = 400;
		fShapes[7].y = 450;
		fShapes[7].vx = 50;
		fShapes[7].vy = -60;
		fShapes[7].radius = 36;
		fShapes[7].r = 180;
		fShapes[7].g = 100;
		fShapes[7].b = 220;
		fShapes[7].type = 7;
		fShapes[7].angle = 0;
		fShapes[7].omega = -0.8f;
	}

	void _Update()
	{
		float w = fBitmap ? (float)(fBitmap->Bounds().IntegerWidth() + 1) : 800;
		float h = fBitmap ? (float)(fBitmap->Bounds().IntegerHeight() + 1) : 600;
		float dt = kPulseRate / 1000000.0f;

		for (int i = 0; i < kShapeCount; i++)
			fShapes[i].Move(dt, w, h);

		for (int i = 0; i < kShapeCount; i++) {
			for (int j = i + 1; j < kShapeCount; j++) {
				if (fShapes[i].CollidesWith(fShapes[j]))
					fShapes[i].ResolveCollision(fShapes[j]);
			}
		}

		// Clamp positions after collision resolution
		for (int i = 0; i < kShapeCount; i++) {
			Shape& s = fShapes[i];
			float vr = s.VisualRadius();
			if (s.x - vr < 0) {
				s.x = vr;
				if (s.vx < 0) s.vx = -s.vx;
			}
			if (s.x + vr > w) {
				s.x = w - vr;
				if (s.vx > 0) s.vx = -s.vx;
			}
			if (s.y - vr < 0) {
				s.y = vr;
				if (s.vy < 0) s.vy = -s.vy;
			}
			if (s.y + vr > h) {
				s.y = h - vr;
				if (s.vy > 0) s.vy = -s.vy;
			}
		}
	}

	void _InitCanvas()
	{
		delete fBitmap;
		fBitmap = NULL;
		fCtx.reset(); // release old context

		BRect bounds = Bounds();
		fBitmap = new BBitmap(bounds, B_RGBA32);
		if (fBitmap->InitCheck() != B_OK) {
			delete fBitmap;
			fBitmap = NULL;
			return;
		}

		int width = bounds.IntegerWidth() + 1;
		int height = bounds.IntegerHeight() + 1;
		int stride = fBitmap->BytesPerRow();

		fCtx.emplace(kvg::BitmapContext::create(
			(unsigned char*)fBitmap->Bits(), width, height,
			stride, kvg::PixelFormat::ARGB32_Premultiplied,
			kvg::ColorSpace::srgb(), kvg::ColorSpace::srgb()));
	}

	// Trace helper: log sub-step timing
	void _T(bool tracing, const char* label)
	{
		if (!tracing) return;
		bigtime_t now = system_time();
		fprintf(fTraceFile, "    %s: %lld us\n", label,
			(long long)(now - fRenderStart));
		fRenderStart = now;
	}

	void _Render()
	{
		if (fBitmap == NULL || !fCtx.has_value() || !(*fCtx))
			return;

		bool T = fTraceFile && fFrameNum <= kTraceFrames;
		float w = (float)(fBitmap->Bounds().IntegerWidth() + 1);
		float h = (float)(fBitmap->Bounds().IntegerHeight() + 1);
		kvg::BitmapContext& ctx = *fCtx;
		if (T)
			fRenderStart = system_time();

		// Background - radial gradient from center
		bigtime_t bgStart = system_time();
		{
			float diag = sqrtf(w * w + h * h) / 2.0f;
			kvg::Gradient::Stop stops[] = {
				{0.0f, kvg::Color::from_rgba8(45, 45, 70, 255)},
				{1.0f, kvg::Color::from_rgba8(10, 10, 18, 255)}
			};
			kvg::Gradient grad = kvg::Gradient::create(stops);
			_T(T, "bg_grad_create");

			kvg::Path bgRect = kvg::Path::Builder{}
				.add_rect(kvg::Rect(0, 0, w, h))
				.build();
			_T(T, "bg_path_build");

			ctx.save_state();
			ctx.clip_to_path(bgRect);
			_T(T, "bg_clip");

			ctx.draw_radial_gradient(grad,
				kvg::Point(w / 2, h / 2), 0,
				kvg::Point(w / 2, h / 2), diag,
				nullptr, kGradExtend);
			_T(T, "bg_draw_gradient");

			ctx.restore_state();
			_T(T, "bg_restore");
		}
		bigtime_t bgTime = system_time() - bgStart;
		fBgRenderAccum += bgTime;

		if (T) {
			float bgArea = w * h;
			float bgFillRate = bgTime > 0 ? bgArea / (float)bgTime : 0;
			fprintf(fTraceFile, "  bg: %lld us  %.0f px  %.1f px/us\n",
				(long long)bgTime, bgArea, bgFillRate);
		}

		// Draw each shape
		for (int i = 0; i < kShapeCount; i++) {
			bigtime_t shapeStart = system_time();
			Shape& s = fShapes[i];
			ctx.save_state();
			ctx.translate_ctm(s.x, s.y);

			switch (s.type) {
				case 0: // Circle - radial gradient + shadow
				{
					ctx.set_shadow(kvg::Point(4, 4), 10,
						kvg::Color(0, 0, 0, 0.5f));

					kvg::Gradient::Stop stops[] = {
						{0.0f, kvg::Color::from_rgba8(
							s.r + 35, s.g + 35, s.b + 35, 255)},
						{1.0f, kvg::Color::from_rgba8(
							s.r / 2, s.g / 2, s.b / 2, 255)}
					};
					kvg::Gradient grad = kvg::Gradient::create(stops);

					kvg::Path circle = kvg::Path::Builder{}
						.add_circle(kvg::Point(0, 0), s.radius)
						.build();
					_T(T, "circle_setup");

					ctx.clip_to_path(circle);
					_T(T, "circle_clip");

					ctx.draw_radial_gradient(grad,
						kvg::Point(-s.radius * 0.3f, -s.radius * 0.3f), 0,
						kvg::Point(-s.radius * 0.3f, -s.radius * 0.3f),
						s.radius * 1.2f,
						nullptr, kGradExtend);
					_T(T, "circle_draw_grad+shadow");
					break;
				}

				case 1: // Rectangle - linear gradient + stroke
				{
					float size = s.radius * 1.4f;
					float half = size / 2;

					kvg::Gradient::Stop stops[] = {
						{0.0f, kvg::Color::from_rgba8(s.r, s.g, s.b, 255)},
						{1.0f, kvg::Color::from_rgba8(
							s.r / 3, s.g / 3, s.b / 3, 255)}
					};
					kvg::Gradient grad = kvg::Gradient::create(stops);

					kvg::Path rect = kvg::Path::Builder{}
						.add_rect(kvg::Rect(-half, -half, size, size))
						.build();
					_T(T, "rect_setup");

					ctx.save_state();
					ctx.clip_to_path(rect);
					ctx.draw_linear_gradient(grad,
						kvg::Point(-half, -half), kvg::Point(half, half),
						nullptr, kGradExtend);
					ctx.restore_state();
					_T(T, "rect_fill_grad");

					ctx.set_stroke_color(
						kvg::Color::from_rgba8(255, 255, 255, 160));
					ctx.set_line_width(2.0f);
					ctx.stroke_path(rect);
					_T(T, "rect_stroke");
					break;
				}

				case 2: // Triangle - solid + shadow + rotation
				{
					ctx.rotate_ctm(s.angle);
					ctx.set_shadow(kvg::Point(3, 3), 8,
						kvg::Color(0, 0, 0, 0.45f));
					ctx.set_fill_color(
						kvg::Color::from_rgba8(s.r, s.g, s.b, 255));

					float rad = s.radius;
					kvg::Path tri = kvg::Path::Builder{}
						.move_to(0, -rad)
						.line_to(-rad * 0.866f, rad * 0.5f)
						.line_to(rad * 0.866f, rad * 0.5f)
						.close()
						.build();
					_T(T, "tri_setup");

					ctx.fill_path(tri);
					_T(T, "tri_fill+shadow");
					break;
				}

				case 3: // Star - solid + dashed stroke + rotation
				{
					ctx.rotate_ctm(s.angle);
					ctx.set_fill_color(
						kvg::Color::from_rgba8(s.r, s.g, s.b, 255));

					float rad = s.radius;
					float inner = rad * 0.4f;
					kvg::Path::Builder builder;
					for (int k = 0; k < 5; k++) {
						float a1 = k * kvg::two_pi / 5.0f
							- kvg::half_pi;
						float a2 = a1 + kvg::pi / 5.0f;
						if (k == 0)
							builder.move_to(
								rad * cosf(a1), rad * sinf(a1));
						else
							builder.line_to(
								rad * cosf(a1), rad * sinf(a1));
						builder.line_to(
							inner * cosf(a2), inner * sinf(a2));
					}
					builder.close();
					kvg::Path star = builder.build();
					_T(T, "star_setup");

					ctx.fill_path(star);
					_T(T, "star_fill");

					float dashes[] = {5.0f, 3.0f};
					ctx.set_line_dash(0, dashes);
					ctx.set_line_cap(kvg::LineCap::Round);
					ctx.set_stroke_color(
						kvg::Color::from_rgba8(255, 255, 255, 180));
					ctx.set_line_width(1.5f);
					ctx.stroke_path(star);
					_T(T, "star_stroke_dash");
					break;
				}

				case 4: // Heart - bezier + gradient + opacity
				{
					ctx.rotate_ctm(s.angle);
					ctx.set_opacity(0.8f);

					kvg::Gradient::Stop stops[] = {
						{0.0f, kvg::Color::from_rgba8(
							255, 140, 170, 255)},
						{1.0f, kvg::Color::from_rgba8(
							200, 30, 60, 255)}
					};
					kvg::Gradient grad = kvg::Gradient::create(stops);

					float rad = s.radius;
					kvg::Path heart = kvg::Path::Builder{}
						.move_to(0, rad * 0.7f)
						.cubic_to(
							-rad * 0.3f, rad * 0.3f,
							-rad, rad * 0.0f,
							-rad, -rad * 0.3f)
						.cubic_to(
							-rad, -rad * 0.75f,
							-rad * 0.3f, -rad * 0.85f,
							0, -rad * 0.5f)
						.cubic_to(
							rad * 0.3f, -rad * 0.85f,
							rad, -rad * 0.75f,
							rad, -rad * 0.3f)
						.cubic_to(
							rad, rad * 0.0f,
							rad * 0.3f, rad * 0.3f,
							0, rad * 0.7f)
						.close()
						.build();
					_T(T, "heart_setup");

					ctx.save_state();
					ctx.clip_to_path(heart);
					_T(T, "heart_clip");

					ctx.draw_radial_gradient(grad,
						kvg::Point(0, -s.radius * 0.4f), 0,
						kvg::Point(0, -s.radius * 0.2f),
						s.radius * 1.2f,
						nullptr, kGradExtend);
					_T(T, "heart_draw_grad");

					ctx.restore_state();
					break;
				}

				case 5: // Ellipse - linear gradient + rotation
				{
					ctx.rotate_ctm(s.angle);

					kvg::Gradient::Stop stops[] = {
						{0.0f, kvg::Color::from_rgba8(s.r, s.g, s.b, 255)},
						{0.5f, kvg::Color::from_rgba8(
							255, 255, 255, 180)},
						{1.0f, kvg::Color::from_rgba8(s.r, s.g, s.b, 255)}
					};
					kvg::Gradient grad = kvg::Gradient::create(stops);

					kvg::Path ellipse = kvg::Path::Builder{}
						.add_ellipse(kvg::Rect(
							-s.radius * 1.3f, -s.radius * 0.65f,
							s.radius * 2.6f, s.radius * 1.3f))
						.build();
					_T(T, "ellipse_setup");

					ctx.save_state();
					ctx.clip_to_path(ellipse);
					_T(T, "ellipse_clip");

					ctx.draw_linear_gradient(grad,
						kvg::Point(0, -s.radius),
						kvg::Point(0, s.radius),
						nullptr, kGradExtend);
					_T(T, "ellipse_draw_grad");

					ctx.restore_state();
					break;
				}

				case 6: // Rounded rect - per-corner radii + shadow + stroke
				{
					float size = s.radius * 1.5f;
					float half = size / 2;

					ctx.set_shadow(kvg::Point(5, 5), 12,
						kvg::Color(0, 0, 0, 0.5f));
					ctx.set_fill_color(
						kvg::Color::from_rgba8(s.r, s.g, s.b, 255));

					kvg::CornerRadii radii(
						size * 0.3f, size * 0.05f,
						size * 0.3f, size * 0.05f);
					kvg::Path rrect = kvg::Path::Builder{}
						.add_round_rect(
							kvg::Rect(-half, -half, size, size), radii)
						.build();
					_T(T, "rrect_setup");

					ctx.fill_path(rrect);
					_T(T, "rrect_fill+shadow");

					ctx.clear_shadow();
					ctx.set_stroke_color(kvg::Color::from_rgba8(
						(uint8)(s.r * 0.5f), (uint8)(s.g * 0.5f),
						(uint8)(s.b * 0.5f), 255));
					ctx.set_line_width(3.0f);
					ctx.set_line_join(kvg::LineJoin::Round);
					ctx.stroke_path(rrect);
					_T(T, "rrect_stroke");
					break;
				}

				case 7: // Hexagon - conic gradient + rotation
				{
					ctx.rotate_ctm(s.angle);

					kvg::Gradient::Stop stops[] = {
						{0.0f, kvg::Color::from_rgba8(s.r, s.g, s.b, 255)},
						{0.33f, kvg::Color::from_rgba8(
							s.g, s.b, s.r, 255)},
						{0.66f, kvg::Color::from_rgba8(
							s.b, s.r, s.g, 255)},
						{1.0f, kvg::Color::from_rgba8(s.r, s.g, s.b, 255)}
					};
					kvg::Gradient grad = kvg::Gradient::create(stops);

					float rad = s.radius;
					kvg::Path::Builder builder;
					for (int k = 0; k < 6; k++) {
						float a = k * kvg::pi / 3.0f - kvg::half_pi;
						if (k == 0)
							builder.move_to(rad * cosf(a), rad * sinf(a));
						else
							builder.line_to(rad * cosf(a), rad * sinf(a));
					}
					builder.close();
					kvg::Path hex = builder.build();
					_T(T, "hex_setup");

					ctx.save_state();
					ctx.clip_to_path(hex);
					_T(T, "hex_clip");

					ctx.draw_conic_gradient(grad,
						kvg::Point(0, 0), 0,
						nullptr, kGradExtend);
					_T(T, "hex_draw_conic");

					ctx.restore_state();
					break;
				}
			}

			ctx.restore_state();
			_T(T, "restore");

			bigtime_t shapeTime = system_time() - shapeStart;
			fShapeRenderTime[i] = shapeTime;
			fShapeRenderAccum[i] += shapeTime;

			if (T) {
				float area = fShapes[i].ApproxArea();
				float fillRate = shapeTime > 0
					? area / (float)shapeTime : 0;
				fprintf(fTraceFile, "  %s: %lld us  ~%.0f px  %.1f px/us\n",
					kShapeInfo[s.type].name,
					(long long)shapeTime, area, fillRate);
			}
		}
	}

	// Verify KosmVG actually rendered pixels at expected shape positions
	void _CheckPixels()
	{
		if (fBitmap == NULL)
			return;

		int width = fBitmap->Bounds().IntegerWidth() + 1;
		int height = fBitmap->Bounds().IntegerHeight() + 1;
		int stride = fBitmap->BytesPerRow();
		uint8* bits = (uint8*)fBitmap->Bits();
		bool T = fTraceFile && fFrameNum <= kTraceFrames;

		for (int i = 0; i < kShapeCount; i++) {
			Shape& s = fShapes[i];
			int px = (int)s.x;
			int py = (int)s.y;

			// Skip if center is outside bitmap
			if (px < 0 || px >= width || py < 0 || py >= height)
				continue;

			// Read ARGB pixel at shape center
			uint32 pixel = *(uint32*)(bits + py * stride + px * 4);
			uint8 alpha = (pixel >> 24) & 0xFF;

			if (alpha > 0) {
				fPixelCheckPassed++;
			} else {
				fPixelCheckFailed++;
				if (T) {
					fprintf(fTraceFile,
						"  *** PIXEL_EMPTY: %s[%d] at (%d,%d) = 0x%08x\n",
						kShapeInfo[s.type].name, i, px, py, pixel);
				}
			}
		}

		// Check background pixel at (5,5) — should be non-transparent
		if (5 < width && 5 < height) {
			uint32 bgPixel = *(uint32*)(bits + 5 * stride + 5 * 4);
			uint8 bgAlpha = (bgPixel >> 24) & 0xFF;
			if (bgAlpha == 0) {
				fPixelCheckFailed++;
				if (T) {
					fprintf(fTraceFile,
						"  *** BG_EMPTY: pixel at (5,5) = 0x%08x\n",
						bgPixel);
				}
			} else {
				fPixelCheckPassed++;
			}
		}
	}

	void _OpenTrace()
	{
		BPath desktop;
		if (find_directory(B_DESKTOP_DIRECTORY, &desktop) != B_OK)
			return;
		BPath path(desktop);
		path.Append("kvg_trace.log");
		fTraceFile = fopen(path.Path(), "w");
		if (fTraceFile) {
			fprintf(fTraceFile,
				"# KosmVG performance trace (times in microseconds)\n"
				"# Shape features:\n");
			for (int i = 0; i < kShapeCount; i++) {
				fprintf(fTraceFile, "#   [%d] %s  area=%.0f px  features:",
					i, kShapeInfo[i].name,
					kShapeInfo[i].areaFactor
						* fShapes[i].radius * fShapes[i].radius);
				int f = kShapeInfo[i].features;
				if (f & kFeatureRadialGrad)	fprintf(fTraceFile, " radial_grad");
				if (f & kFeatureLinearGrad)	fprintf(fTraceFile, " linear_grad");
				if (f & kFeatureConicGrad)	fprintf(fTraceFile, " conic_grad");
				if (f & kFeatureSolidFill)	fprintf(fTraceFile, " solid_fill");
				if (f & kFeatureShadow)		fprintf(fTraceFile, " shadow");
				if (f & kFeatureStroke)		fprintf(fTraceFile, " stroke");
				if (f & kFeatureDashStroke)	fprintf(fTraceFile, " dash_stroke");
				if (f & kFeatureBezier)		fprintf(fTraceFile, " bezier");
				if (f & kFeatureRotation)	fprintf(fTraceFile, " rotation");
				if (f & kFeatureOpacity)	fprintf(fTraceFile, " opacity");
				if (f & kFeatureRoundRect)	fprintf(fTraceFile, " round_rect");
				fprintf(fTraceFile, "\n");
			}
			fprintf(fTraceFile, "#\n");
		}
	}

	void _TraceFrame(bigtime_t t0, bigtime_t t1, bigtime_t t2, bigtime_t t3)
	{
		bigtime_t update_us = t1 - t0;
		bigtime_t render_us = t2 - t1;
		bigtime_t total_us  = t3 - t0;

		// Accumulate stats
		if (fStatsCount < kTraceFrames) {
			fFrameTimes[fStatsCount] = total_us;
			fRenderTimes[fStatsCount] = render_us;
			fUpdateTimes[fStatsCount] = update_us;
			fStatsCount++;
		}

		if (!fTraceFile || fFrameNum > kTraceFrames) {
			fFrameNum++;
			return;
		}

		bigtime_t inval_us = t3 - t2;

		fprintf(fTraceFile,
			"frame %d: total=%lld  render=%lld  update=%lld  "
			"invalidate=%lld\n",
			fFrameNum, (long long)total_us, (long long)render_us,
			(long long)update_us, (long long)inval_us);

		// Flag slow frames (> 2x target)
		if (total_us > kPulseRate * 2)
			fprintf(fTraceFile, "  *** SLOW FRAME (%.1fx target) ***\n",
				(float)total_us / kPulseRate);

		fFrameNum++;

		// Flush periodically
		if ((fFrameNum % 60) == 0)
			fflush(fTraceFile);
	}

	static int _CmpBigtime(const void* a, const void* b)
	{
		bigtime_t va = *(const bigtime_t*)a;
		bigtime_t vb = *(const bigtime_t*)b;
		return (va > vb) - (va < vb);
	}

	void _WriteTimingStats(const char* label, bigtime_t* data, int count)
	{
		if (count == 0) return;

		bigtime_t sorted[kTraceFrames];
		memcpy(sorted, data, count * sizeof(bigtime_t));
		qsort(sorted, count, sizeof(bigtime_t), _CmpBigtime);

		bigtime_t sum = 0;
		for (int i = 0; i < count; i++)
			sum += sorted[i];

		bigtime_t avg = sum / count;
		bigtime_t p50 = sorted[count / 2];
		bigtime_t p95 = sorted[(int)(count * 0.95f)];
		bigtime_t p99 = sorted[(int)(count * 0.99f)];

		fprintf(fTraceFile,
			"  %s: min=%lld  avg=%lld  p50=%lld  p95=%lld  "
			"p99=%lld  max=%lld us\n",
			label,
			(long long)sorted[0], (long long)avg, (long long)p50,
			(long long)p95, (long long)p99,
			(long long)sorted[count - 1]);

		int bins[7] = {};
		const bigtime_t edges[] = {1000, 2000, 5000, 10000, 16667, 33333};
		for (int i = 0; i < count; i++) {
			int b = 6;
			for (int e = 0; e < 6; e++) {
				if (sorted[i] < edges[e]) { b = e; break; }
			}
			bins[b]++;
		}
		fprintf(fTraceFile,
			"    histogram: <1ms=%d  1-2=%d  2-5=%d  5-10=%d  "
			"10-16=%d  16-33=%d  >33ms=%d\n",
			bins[0], bins[1], bins[2], bins[3], bins[4], bins[5], bins[6]);
	}

	void _WriteSummary()
	{
		fprintf(fTraceFile,
			"\n"
			"========================================\n"
			"  KosmVG PERFORMANCE SUMMARY (%d frames)\n"
			"========================================\n",
			fFrameNum);

		// --- Frame timing ---
		fprintf(fTraceFile, "\n--- Frame Timing ---\n");
		_WriteTimingStats("frame_total", fFrameTimes, fStatsCount);
		_WriteTimingStats("render", fRenderTimes, fStatsCount);
		_WriteTimingStats("update", fUpdateTimes, fStatsCount);
		_WriteTimingStats("blit", fDrawBitmapTimes, fStatsCount);

		if (fStatsCount > 0) {
			bigtime_t sum = 0;
			for (int i = 0; i < fStatsCount; i++)
				sum += fFrameTimes[i];
			float avgFps = fStatsCount / ((float)sum / 1000000.0f);
			fprintf(fTraceFile, "  avg_fps: %.1f\n", avgFps);

			// Render vs blit ratio
			bigtime_t renderSum = 0, blitSum = 0;
			for (int i = 0; i < fStatsCount; i++) {
				renderSum += fRenderTimes[i];
				blitSum += fDrawBitmapTimes[i];
			}
			fprintf(fTraceFile,
				"  render_pct: %.1f%%  blit_pct: %.1f%%\n",
				100.0f * renderSum / sum,
				100.0f * blitSum / sum);
		}

		// --- Per-shape performance ---
		fprintf(fTraceFile, "\n--- Per-Shape Render Cost ---\n");
		fprintf(fTraceFile,
			"  %-10s %8s %8s %10s  features\n",
			"shape", "avg(us)", "px/us", "area(px)");

		for (int i = 0; i < kShapeCount; i++) {
			float avgTime = fFrameNum > 0
				? (float)fShapeRenderAccum[i] / fFrameNum : 0;
			float area = fShapes[i].ApproxArea();
			float fillRate = avgTime > 0 ? area / avgTime : 0;

			fprintf(fTraceFile, "  %-10s %8.1f %8.1f %10.0f ",
				kShapeInfo[i].name, avgTime, fillRate, area);

			// List features
			int f = kShapeInfo[i].features;
			if (f & kFeatureRadialGrad)	fprintf(fTraceFile, " radial");
			if (f & kFeatureLinearGrad)	fprintf(fTraceFile, " linear");
			if (f & kFeatureConicGrad)	fprintf(fTraceFile, " conic");
			if (f & kFeatureSolidFill)	fprintf(fTraceFile, " solid");
			if (f & kFeatureShadow)		fprintf(fTraceFile, " shadow");
			if (f & kFeatureStroke)		fprintf(fTraceFile, " stroke");
			if (f & kFeatureDashStroke)	fprintf(fTraceFile, " dash");
			if (f & kFeatureBezier)		fprintf(fTraceFile, " bezier");
			if (f & kFeatureRotation)	fprintf(fTraceFile, " rot");
			if (f & kFeatureOpacity)	fprintf(fTraceFile, " opacity");
			if (f & kFeatureRoundRect)	fprintf(fTraceFile, " rrect");
			fprintf(fTraceFile, "\n");
		}

		// --- Shadow overhead ---
		fprintf(fTraceFile, "\n--- Shadow Overhead ---\n");
		// Shadow shapes: 0 (circle), 2 (triangle), 6 (rrect)
		// Non-shadow shapes: 1 (rect), 3 (star), 5 (ellipse), 7 (hexagon)
		float shadowAvg = 0, noShadowAvg = 0;
		float shadowArea = 0, noShadowArea = 0;
		int shadowIdx[] = {0, 2, 6};
		int noShadowIdx[] = {1, 3, 5, 7};

		for (int k = 0; k < 3; k++) {
			int i = shadowIdx[k];
			float t = fFrameNum > 0
				? (float)fShapeRenderAccum[i] / fFrameNum : 0;
			shadowAvg += t;
			shadowArea += fShapes[i].ApproxArea();
		}
		for (int k = 0; k < 4; k++) {
			int i = noShadowIdx[k];
			float t = fFrameNum > 0
				? (float)fShapeRenderAccum[i] / fFrameNum : 0;
			noShadowAvg += t;
			noShadowArea += fShapes[i].ApproxArea();
		}

		float shadowFillRate = shadowAvg > 0
			? shadowArea / shadowAvg : 0;
		float noShadowFillRate = noShadowAvg > 0
			? noShadowArea / noShadowAvg : 0;

		fprintf(fTraceFile,
			"  with_shadow (3 shapes): avg=%.1f us  %.1f px/us\n"
			"  no_shadow   (4 shapes): avg=%.1f us  %.1f px/us\n"
			"  shadow_overhead: %.1fx slower per pixel\n",
			shadowAvg, shadowFillRate,
			noShadowAvg, noShadowFillRate,
			noShadowFillRate > 0
				? shadowFillRate > 0
					? noShadowFillRate / shadowFillRate : 0
				: 0);

		// --- Gradient type comparison ---
		fprintf(fTraceFile, "\n--- Gradient Fill Rate (px/us) ---\n");
		// Radial: shapes 0, 4.  Linear: 1, 5.  Conic: 7.  Solid: 2, 3.
		struct { const char* name; int idx[2]; int count; } gradTypes[] = {
			{"radial",  {0, 4}, 2},
			{"linear",  {1, 5}, 2},
			{"conic",   {7, -1}, 1},
			{"solid",   {2, 3}, 2}
		};

		for (int g = 0; g < 4; g++) {
			float totalTime = 0, totalArea = 0;
			for (int k = 0; k < gradTypes[g].count; k++) {
				int i = gradTypes[g].idx[k];
				totalTime += fFrameNum > 0
					? (float)fShapeRenderAccum[i] / fFrameNum : 0;
				totalArea += fShapes[i].ApproxArea();
			}
			float rate = totalTime > 0 ? totalArea / totalTime : 0;
			fprintf(fTraceFile, "  %-8s: %.1f px/us  (%.1f us total)\n",
				gradTypes[g].name, rate, totalTime);
		}

		// --- Background fill rate ---
		fprintf(fTraceFile, "\n--- Background ---\n");
		if (fFrameNum > 0 && fBitmap) {
			float w = fBitmap->Bounds().IntegerWidth() + 1;
			float h = fBitmap->Bounds().IntegerHeight() + 1;
			float bgAvg = (float)fBgRenderAccum / fFrameNum;
			float bgRate = bgAvg > 0 ? (w * h) / bgAvg : 0;
			fprintf(fTraceFile,
				"  canvas: %.0fx%.0f = %.0f px\n"
				"  avg_time: %.1f us\n"
				"  fill_rate: %.1f px/us\n",
				w, h, w * h, bgAvg, bgRate);
		}

		// --- Pixel correctness ---
		fprintf(fTraceFile, "\n--- Rendering Correctness ---\n");
		int total = fPixelCheckPassed + fPixelCheckFailed;
		fprintf(fTraceFile,
			"  pixel_checks: %d total  %d passed  %d failed",
			total, fPixelCheckPassed, fPixelCheckFailed);
		if (total > 0) {
			fprintf(fTraceFile, "  (%.1f%% pass rate)",
				100.0f * fPixelCheckPassed / total);
		}
		fprintf(fTraceFile, "\n");
		if (fPixelCheckFailed > 0) {
			fprintf(fTraceFile,
				"  *** KosmVG may have rendering bugs: "
				"%d empty pixels at expected shape centers\n",
				fPixelCheckFailed);
		}

		fprintf(fTraceFile,
			"========================================\n");
	}

	// Rendering
	BBitmap*						fBitmap;
	std::optional<kvg::BitmapContext>	fCtx;
	Shape							fShapes[kShapeCount];
	FILE*							fTraceFile;
	int								fFrameNum;
	bigtime_t						fRenderStart;

	// Frame statistics
	bigtime_t						fFrameTimes[kTraceFrames];
	bigtime_t						fRenderTimes[kTraceFrames];
	bigtime_t						fUpdateTimes[kTraceFrames];
	bigtime_t						fDrawBitmapTimes[kTraceFrames];
	int								fStatsCount;

	// Per-shape render timing
	bigtime_t						fShapeRenderTime[kShapeCount];
	bigtime_t						fShapeRenderAccum[kShapeCount];

	// Background timing
	bigtime_t						fBgRenderAccum;

	// Pixel correctness
	int								fPixelCheckPassed;
	int								fPixelCheckFailed;
};


class KosmVGWindow : public BWindow {
public:
	KosmVGWindow()
		:
		BWindow(BRect(100, 100, 899, 699), "KosmVG Demo - Bouncing Shapes",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new KosmVGView());
	}
};


class KosmVGApp : public BApplication {
public:
	KosmVGApp()
		:
		BApplication("application/x-vnd.Haiku-ThorVGDemo")
	{
	}

	virtual void ReadyToRun()
	{
		(new KosmVGWindow())->Show();
	}
};


int
main()
{
	KosmVGApp app;
	app.Run();
	return 0;
}