/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmVG Demo - bouncing shapes with collision
 * Showcases: gradients, shadows, strokes, transforms, bezier paths,
 *            dash patterns, opacity, per-corner radii, conic gradients.
 * Renders directly into BBitmap (no KosmSurface).
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
		fFrameNum(0)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		_InitShapes();
		_OpenTrace();
	}

	virtual ~KosmVGView()
	{
		if (fTraceFile) {
			fprintf(fTraceFile, "# tracing done, %d frames\n", fFrameNum);
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

		if (fTraceFile && fFrameNum <= kTraceFrames)
			fprintf(fTraceFile, "  draw_bitmap: %lld us\n", (long long)(t1 - t0));
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

		// Draw each shape
		for (int i = 0; i < kShapeCount; i++) {
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
		if (fTraceFile)
			fprintf(fTraceFile, "# KosmVG frame trace (times in microseconds)\n");
	}

	void _TraceFrame(bigtime_t t0, bigtime_t t1, bigtime_t t2, bigtime_t t3)
	{
		if (!fTraceFile || fFrameNum > kTraceFrames)
			return;

		bigtime_t update_us = t1 - t0;
		bigtime_t render_us = t2 - t1;
		bigtime_t inval_us  = t3 - t2;
		bigtime_t total_us  = t3 - t0;

		fprintf(fTraceFile, "frame %d: total=%lld  update=%lld  render=%lld  invalidate=%lld\n",
			fFrameNum, (long long)total_us, (long long)update_us,
			(long long)render_us, (long long)inval_us);

		// Flag slow frames (> 2x target)
		if (total_us > kPulseRate * 2)
			fprintf(fTraceFile, "  *** SLOW FRAME ***\n");

		fFrameNum++;

		// Flush periodically so we see data even if app crashes
		if ((fFrameNum % 60) == 0)
			fflush(fTraceFile);
	}

	BBitmap*						fBitmap;
	std::optional<kvg::BitmapContext>	fCtx;
	Shape							fShapes[kShapeCount];
	FILE*							fTraceFile;
	int								fFrameNum;
	bigtime_t						fRenderStart;
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
