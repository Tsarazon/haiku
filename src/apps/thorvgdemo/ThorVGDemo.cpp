/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * PlutoVG Demo - bouncing shapes with collision
 * Showcases: gradients, shadows, strokes, transforms, bezier paths,
 *            dash patterns, opacity, per-corner radii, conic gradients.
 */

#include <Application.h>
#include <Bitmap.h>
#include <MessageRunner.h>
#include <View.h>
#include <Window.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <plutovg.hpp>


static const uint32 kMsgAnimate = 'anim';
static const bigtime_t kFrameTime = 16667; // ~60 fps
static const int kShapeCount = 8;


struct Shape {
	float x, y;			// position (center)
	float vx, vy;		// velocity
	float radius;		// bounding radius for collision
	uint8 r, g, b;		// base color
	int type;			// 0..7
	float angle;		// rotation (radians)
	float omega;		// angular velocity (radians/sec)

	void Move(float dt, float width, float height)
	{
		x += vx * dt;
		y += vy * dt;
		angle += omega * dt;

		// Bounce off walls
		if (x - radius < 0) {
			x = radius;
			vx = -vx;
		}
		if (x + radius > width) {
			x = width - radius;
			vx = -vx;
		}
		if (y - radius < 0) {
			y = radius;
			vy = -vy;
		}
		if (y + radius > height) {
			y = height - radius;
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


class PlutoVGView : public BView {
public:
	PlutoVGView()
		:
		BView(BRect(0, 0, 799, 599), "PlutoVGView", B_FOLLOW_ALL,
			B_WILL_DRAW | B_FRAME_EVENTS),
		fBitmap(NULL),
		fRunner(NULL)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		_InitShapes();
	}

	virtual ~PlutoVGView()
	{
		delete fRunner;
		delete fBitmap;
	}

	virtual void AttachedToWindow()
	{
		_InitCanvas();
		_Render();

		// Start animation timer
		BMessage msg(kMsgAnimate);
		fRunner = new BMessageRunner(BMessenger(this), &msg, kFrameTime);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgAnimate:
				_Update();
				_Render();
				Invalidate();
				break;
			default:
				BView::MessageReceived(message);
				break;
		}
	}

	virtual void Draw(BRect updateRect)
	{
		if (fBitmap != NULL)
			DrawBitmap(fBitmap, B_ORIGIN);
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
		fShapes[5].radius = 42;
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
		float dt = kFrameTime / 1000000.0f;
		float w = fBitmap ? fBitmap->Bounds().Width() + 1 : 800;
		float h = fBitmap ? fBitmap->Bounds().Height() + 1 : 600;

		for (int i = 0; i < kShapeCount; i++)
			fShapes[i].Move(dt, w, h);

		for (int i = 0; i < kShapeCount; i++) {
			for (int j = i + 1; j < kShapeCount; j++) {
				if (fShapes[i].CollidesWith(fShapes[j]))
					fShapes[i].ResolveCollision(fShapes[j]);
			}
		}
	}

	void _InitCanvas()
	{
		delete fBitmap;
		fBitmap = NULL;

		BRect bounds = Bounds();

		fBitmap = new BBitmap(bounds, B_RGBA32);
		if (fBitmap->InitCheck() != B_OK) {
			delete fBitmap;
			fBitmap = NULL;
		}
	}

	void _Render()
	{
		if (fBitmap == NULL)
			return;

		int width = (int)fBitmap->Bounds().Width() + 1;
		int height = (int)fBitmap->Bounds().Height() + 1;
		float w = (float)width;
		float h = (float)height;

		plutovg::Surface surface = plutovg::Surface::create_for_data(
			(unsigned char*)fBitmap->Bits(), width, height,
			fBitmap->BytesPerRow());
		if (!surface)
			return;

		plutovg::Canvas canvas(std::move(surface));

		// Background - radial gradient from center
		{
			float diag = sqrtf(w * w + h * h) / 2.0f;
			plutovg::GradientStop stops[] = {
				{0.0f, plutovg::Color::from_rgba8(45, 45, 70, 255)},
				{1.0f, plutovg::Color::from_rgba8(10, 10, 18, 255)}
			};
			canvas.set_radial_gradient(w / 2, h / 2, diag,
				w / 2, h / 2, 0,
				plutovg::SpreadMethod::Pad, stops);
			canvas.fill_rect(0, 0, w, h);
		}

		// Draw each shape
		for (int i = 0; i < kShapeCount; i++) {
			Shape& s = fShapes[i];
			canvas.save();
			canvas.translate(s.x, s.y);

			switch (s.type) {
				case 0: // Circle - radial gradient + shadow
				{
					canvas.set_shadow(4, 4, 10,
						plutovg::Color(0, 0, 0, 0.5f));

					plutovg::GradientStop stops[] = {
						{0.0f, plutovg::Color::from_rgba8(
							s.r + 35, s.g + 35, s.b + 35, 255)},
						{1.0f, plutovg::Color::from_rgba8(
							s.r / 2, s.g / 2, s.b / 2, 255)}
					};
					canvas.set_radial_gradient(
						-s.radius * 0.3f, -s.radius * 0.3f, s.radius * 1.2f,
						-s.radius * 0.3f, -s.radius * 0.3f, 0,
						plutovg::SpreadMethod::Pad, stops);

					canvas.circle(0, 0, s.radius);
					canvas.fill();
					break;
				}

				case 1: // Rectangle - linear gradient + stroke
				{
					float size = s.radius * 1.4f;
					float half = size / 2;

					plutovg::GradientStop stops[] = {
						{0.0f, plutovg::Color::from_rgba8(s.r, s.g, s.b, 255)},
						{1.0f, plutovg::Color::from_rgba8(
							s.r / 3, s.g / 3, s.b / 3, 255)}
					};
					canvas.set_linear_gradient(-half, -half, half, half,
						plutovg::SpreadMethod::Pad, stops);

					canvas.rect(-half, -half, size, size);
					canvas.fill_preserve();

					canvas.set_color(
						plutovg::Color::from_rgba8(255, 255, 255, 160));
					canvas.set_line_width(2.0f);
					canvas.stroke();
					break;
				}

				case 2: // Triangle - solid + shadow + rotation
				{
					canvas.rotate(s.angle);
					canvas.set_shadow(3, 3, 8,
						plutovg::Color(0, 0, 0, 0.45f));
					canvas.set_color(
						plutovg::Color::from_rgba8(s.r, s.g, s.b, 255));

					float rad = s.radius;
					canvas.move_to(0, -rad);
					canvas.line_to(-rad * 0.866f, rad * 0.5f);
					canvas.line_to(rad * 0.866f, rad * 0.5f);
					canvas.close_path();
					canvas.fill();
					break;
				}

				case 3: // Star - solid + dashed stroke + rotation
				{
					canvas.rotate(s.angle);
					canvas.set_color(
						plutovg::Color::from_rgba8(s.r, s.g, s.b, 255));

					float rad = s.radius;
					float inner = rad * 0.4f;
					for (int k = 0; k < 5; k++) {
						float a1 = k * plutovg::two_pi / 5.0f
							- plutovg::half_pi;
						float a2 = a1 + plutovg::pi / 5.0f;
						if (k == 0)
							canvas.move_to(
								rad * cosf(a1), rad * sinf(a1));
						else
							canvas.line_to(
								rad * cosf(a1), rad * sinf(a1));
						canvas.line_to(
							inner * cosf(a2), inner * sinf(a2));
					}
					canvas.close_path();
					canvas.fill_preserve();

					// Dashed stroke outline
					float dashes[] = {5.0f, 3.0f};
					canvas.set_dash(0, dashes);
					canvas.set_line_cap(plutovg::LineCap::Round);
					canvas.set_color(
						plutovg::Color::from_rgba8(255, 255, 255, 180));
					canvas.set_line_width(1.5f);
					canvas.stroke();
					break;
				}

				case 4: // Heart - bezier + gradient + opacity
				{
					canvas.rotate(s.angle);
					canvas.set_opacity(0.8f);

					plutovg::GradientStop stops[] = {
						{0.0f, plutovg::Color::from_rgba8(
							255, 140, 170, 255)},
						{1.0f, plutovg::Color::from_rgba8(
							200, 30, 60, 255)}
					};
					canvas.set_radial_gradient(
						0, -s.radius * 0.2f, s.radius * 1.2f,
						0, -s.radius * 0.4f, 0,
						plutovg::SpreadMethod::Pad, stops);

					float rad = s.radius;
					canvas.move_to(0, rad * 0.7f);
					canvas.cubic_to(
						-rad * 0.3f, rad * 0.3f,
						-rad, rad * 0.0f,
						-rad, -rad * 0.3f);
					canvas.cubic_to(
						-rad, -rad * 0.75f,
						-rad * 0.3f, -rad * 0.85f,
						0, -rad * 0.5f);
					canvas.cubic_to(
						rad * 0.3f, -rad * 0.85f,
						rad, -rad * 0.75f,
						rad, -rad * 0.3f);
					canvas.cubic_to(
						rad, rad * 0.0f,
						rad * 0.3f, rad * 0.3f,
						0, rad * 0.7f);
					canvas.close_path();
					canvas.fill();
					break;
				}

				case 5: // Ellipse - linear gradient + rotation
				{
					canvas.rotate(s.angle);

					plutovg::GradientStop stops[] = {
						{0.0f, plutovg::Color::from_rgba8(s.r, s.g, s.b, 255)},
						{0.5f, plutovg::Color::from_rgba8(
							255, 255, 255, 180)},
						{1.0f, plutovg::Color::from_rgba8(s.r, s.g, s.b, 255)}
					};
					canvas.set_linear_gradient(
						0, -s.radius, 0, s.radius,
						plutovg::SpreadMethod::Pad, stops);

					canvas.ellipse(0, 0, s.radius * 1.3f, s.radius * 0.65f);
					canvas.fill();
					break;
				}

				case 6: // Rounded rect - per-corner radii + shadow + stroke
				{
					float size = s.radius * 1.5f;
					float half = size / 2;

					canvas.set_shadow(5, 5, 12,
						plutovg::Color(0, 0, 0, 0.5f));
					canvas.set_color(
						plutovg::Color::from_rgba8(s.r, s.g, s.b, 255));

					plutovg::CornerRadii radii(
						size * 0.3f, size * 0.05f,
						size * 0.3f, size * 0.05f);
					canvas.round_rect(-half, -half, size, size, radii);
					canvas.fill_preserve();

					// Thick darker stroke (shadow is still on from
					// save state, restore will clear it)
					canvas.set_color(plutovg::Color::from_rgba8(
						(uint8)(s.r * 0.5f), (uint8)(s.g * 0.5f),
						(uint8)(s.b * 0.5f), 255));
					canvas.set_line_width(3.0f);
					canvas.set_line_join(plutovg::LineJoin::Round);
					canvas.stroke();
					break;
				}

				case 7: // Hexagon - conic gradient + rotation
				{
					canvas.rotate(s.angle);

					plutovg::GradientStop stops[] = {
						{0.0f, plutovg::Color::from_rgba8(s.r, s.g, s.b, 255)},
						{0.33f, plutovg::Color::from_rgba8(
							s.g, s.b, s.r, 255)},
						{0.66f, plutovg::Color::from_rgba8(
							s.b, s.r, s.g, 255)},
						{1.0f, plutovg::Color::from_rgba8(s.r, s.g, s.b, 255)}
					};
					canvas.set_conic_gradient(0, 0, 0,
						plutovg::SpreadMethod::Pad, stops);

					float rad = s.radius;
					for (int k = 0; k < 6; k++) {
						float a = k * plutovg::pi / 3.0f - plutovg::half_pi;
						if (k == 0)
							canvas.move_to(rad * cosf(a), rad * sinf(a));
						else
							canvas.line_to(rad * cosf(a), rad * sinf(a));
					}
					canvas.close_path();
					canvas.fill();
					break;
				}
			}

			canvas.restore();
		}
	}

	BBitmap*			fBitmap;
	BMessageRunner*		fRunner;
	Shape				fShapes[kShapeCount];
};


class PlutoVGWindow : public BWindow {
public:
	PlutoVGWindow()
		:
		BWindow(BRect(100, 100, 899, 699), "PlutoVG Demo - Bouncing Shapes",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new PlutoVGView());
	}
};


class PlutoVGApp : public BApplication {
public:
	PlutoVGApp()
		:
		BApplication("application/x-vnd.Haiku-ThorVGDemo")
	{
	}

	virtual void ReadyToRun()
	{
		(new PlutoVGWindow())->Show();
	}
};


int
main()
{
	PlutoVGApp app;
	app.Run();
	return 0;
}