/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * PlutoVG Demo - bouncing shapes with collision
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


struct Shape {
	float x, y;			// position (center)
	float vx, vy;		// velocity
	float radius;		// bounding radius for collision
	uint8 r, g, b;		// color
	int type;			// 0=circle, 1=rect, 2=triangle

	void Move(float dt, float width, float height)
	{
		x += vx * dt;
		y += vy * dt;

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
		BView(BRect(0, 0, 599, 399), "PlutoVGView", B_FOLLOW_ALL, B_WILL_DRAW),
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
		srand(12345);

		// Circle - red
		fShapes[0].x = 100;
		fShapes[0].y = 100;
		fShapes[0].vx = 120;
		fShapes[0].vy = 80;
		fShapes[0].radius = 40;
		fShapes[0].r = 220;
		fShapes[0].g = 60;
		fShapes[0].b = 60;
		fShapes[0].type = 0;

		// Rectangle - green
		fShapes[1].x = 300;
		fShapes[1].y = 200;
		fShapes[1].vx = -100;
		fShapes[1].vy = 90;
		fShapes[1].radius = 45;
		fShapes[1].r = 60;
		fShapes[1].g = 180;
		fShapes[1].b = 60;
		fShapes[1].type = 1;

		// Triangle - blue
		fShapes[2].x = 450;
		fShapes[2].y = 150;
		fShapes[2].vx = 70;
		fShapes[2].vy = -110;
		fShapes[2].radius = 35;
		fShapes[2].r = 60;
		fShapes[2].g = 100;
		fShapes[2].b = 220;
		fShapes[2].type = 2;

		// Ellipse - yellow
		fShapes[3].x = 200;
		fShapes[3].y = 300;
		fShapes[3].vx = -80;
		fShapes[3].vy = -70;
		fShapes[3].radius = 50;
		fShapes[3].r = 220;
		fShapes[3].g = 200;
		fShapes[3].b = 60;
		fShapes[3].type = 0;

		// Purple circle
		fShapes[4].x = 500;
		fShapes[4].y = 300;
		fShapes[4].vx = 90;
		fShapes[4].vy = 100;
		fShapes[4].radius = 30;
		fShapes[4].r = 180;
		fShapes[4].g = 60;
		fShapes[4].b = 200;
		fShapes[4].type = 0;
	}

	void _Update()
	{
		float dt = kFrameTime / 1000000.0f;
		float w = fBitmap ? fBitmap->Bounds().Width() + 1 : 600;
		float h = fBitmap ? fBitmap->Bounds().Height() + 1 : 400;

		// Move all shapes
		for (int i = 0; i < 5; i++)
			fShapes[i].Move(dt, w, h);

		// Check collisions
		for (int i = 0; i < 5; i++) {
			for (int j = i + 1; j < 5; j++) {
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

		// Create a fresh PlutoVG surface wrapping the BBitmap pixels.
		// Must be the sole owner (refcount 1) so Canvas writes directly
		// into the BBitmap buffer instead of COW-detaching to a new one.
		// B_RGBA32 = BGRA in memory = 0xAARRGGBB as uint32 = PlutoVG ARGB.
		plutovg::Surface surface = plutovg::Surface::create_for_data(
			(unsigned char*)fBitmap->Bits(), width, height,
			fBitmap->BytesPerRow());
		if (!surface)
			return;

		plutovg::Canvas canvas(std::move(surface));

		// Background
		canvas.set_color(plutovg::Color::from_rgba8(30, 30, 40, 255));
		canvas.fill_rect(0, 0, w, h);

		// Draw each shape
		for (int i = 0; i < 5; i++) {
			Shape& s = fShapes[i];

			canvas.set_color(plutovg::Color::from_rgba8(s.r, s.g, s.b, 255));

			switch (s.type) {
				case 0: // Circle
					canvas.circle(s.x, s.y, s.radius);
					canvas.fill();
					break;
				case 1: // Rounded rectangle
				{
					float size = s.radius * 1.4f;
					canvas.round_rect(s.x - size / 2, s.y - size / 2,
						size, size, 8, 8);
					canvas.fill();
					break;
				}
				case 2: // Triangle
				{
					float r = s.radius;
					canvas.move_to(s.x, s.y - r);
					canvas.line_to(s.x - r * 0.866f, s.y + r * 0.5f);
					canvas.line_to(s.x + r * 0.866f, s.y + r * 0.5f);
					canvas.close_path();
					canvas.fill();
					break;
				}
			}
		}
	}

	BBitmap*			fBitmap;
	BMessageRunner*		fRunner;
	Shape				fShapes[5];
};


class PlutoVGWindow : public BWindow {
public:
	PlutoVGWindow()
		:
		BWindow(BRect(100, 100, 699, 499), "PlutoVG Demo - Bouncing Shapes",
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