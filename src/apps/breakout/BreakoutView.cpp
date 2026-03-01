/*
 * Copyright 2026 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Breakout Game - KosmVG test application
 */

#include "BreakoutView.h"

#include <Application.h>
#include <Bitmap.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <Window.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>


static const bigtime_t kPulseRate = 16667; // ~60 fps
static const float kBallSpeed = 350.0f;    // pixels/sec
static const float kPaddleSpeed = 600.0f;  // pixels/sec (keyboard)
static const float kBrickMargin = 4.0f;
static const float kBrickTopOffset = 80.0f;
static const float kBrickHeight = 22.0f;
static const float kBrickRadius = 4.0f;
static const float kPaddleWidth = 100.0f;
static const float kPaddleHeight = 14.0f;
static const float kPaddleRadius = 7.0f;
static const float kPaddleBottom = 40.0f; // distance from bottom
static const float kBallRadius = 7.0f;
static const float kSideMargin = 20.0f;

static const auto kGradExtend =
	kvg::GradientDrawingOptions::DrawsBeforeStart
	| kvg::GradientDrawingOptions::DrawsAfterEnd;


// Brick colors per row (top to bottom)
static const uint8 kBrickColors[kBrickRows][3] = {
	{220, 60,  60},   // red
	{220, 140, 40},   // orange
	{220, 200, 50},   // yellow
	{60,  190, 60},   // green
	{60,  140, 220},  // blue
};


BreakoutView::BreakoutView()
	:
	BView(BRect(0, 0, 799, 599), "BreakoutView", B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_PULSE_NEEDED),
	fBitmap(NULL),
	fLives(kMaxLives),
	fScore(0),
	fGameOver(false),
	fPaddleTargetX(-1),
	fKeyLeft(false),
	fKeyRight(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetEventMask(B_KEYBOARD_EVENTS);
	_InitLevel();
}


BreakoutView::~BreakoutView()
{
	delete fBitmap;
}


void
BreakoutView::AttachedToWindow()
{
	_InitCanvas();
	_InitFont();
	_Render();
	MakeFocus(true);
	Window()->SetPulseRate(kPulseRate);
}


void
BreakoutView::Pulse()
{
	float dt = kPulseRate / 1000000.0f;
	_Update(dt);
	_Render();
	Invalidate();
}


void
BreakoutView::Draw(BRect updateRect)
{
	if (fBitmap != NULL)
		DrawBitmap(fBitmap, B_ORIGIN);
}


void
BreakoutView::FrameResized(float width, float height)
{
	_InitCanvas();
	_InitLevel();
	_Render();
	Invalidate();
}


void
BreakoutView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	fPaddleTargetX = where.x;
}


void
BreakoutView::MouseDown(BPoint where)
{
	if (fGameOver) {
		fGameOver = false;
		fLives = kMaxLives;
		fScore = 0;
		_InitLevel();
		return;
	}

	if (!fBall.launched) {
		fBall.launched = true;
		// Launch at angle depending on paddle position
		float angle = -kvg::pi / 4.0f
			+ (fBall.x - fPaddle.x) / fPaddle.w * kvg::pi / 4.0f;
		// Clamp angle
		angle = std::max(-kvg::pi * 0.4f, std::min(kvg::pi * 0.4f, angle));
		fBall.vx = kBallSpeed * sinf(angle);
		fBall.vy = -kBallSpeed * cosf(angle);
	}
}


void
BreakoutView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes < 1)
		return;

	switch (bytes[0]) {
		case B_LEFT_ARROW:
			fKeyLeft = true;
			break;
		case B_RIGHT_ARROW:
			fKeyRight = true;
			break;
		case B_SPACE:
			MouseDown(BPoint(fPaddle.x + fPaddle.w / 2, fPaddle.y));
			break;
		case B_ESCAPE:
			Window()->PostMessage(B_QUIT_REQUESTED);
			break;
	}
}


void
BreakoutView::_InitCanvas()
{
	delete fBitmap;
	fBitmap = NULL;
	fCtx.reset();

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


void
BreakoutView::_InitLevel()
{
	float w = fBitmap ? (float)(fBitmap->Bounds().IntegerWidth() + 1) : 800;
	float h = fBitmap ? (float)(fBitmap->Bounds().IntegerHeight() + 1) : 600;

	// Bricks
	float fieldWidth = w - 2 * kSideMargin;
	float brickWidth = (fieldWidth - (kBrickCols - 1) * kBrickMargin)
		/ kBrickCols;

	for (int row = 0; row < kBrickRows; row++) {
		for (int col = 0; col < kBrickCols; col++) {
			int idx = row * kBrickCols + col;
			fBricks[idx].x = kSideMargin
				+ col * (brickWidth + kBrickMargin);
			fBricks[idx].y = kBrickTopOffset
				+ row * (kBrickHeight + kBrickMargin);
			fBricks[idx].w = brickWidth;
			fBricks[idx].h = kBrickHeight;
			fBricks[idx].row = row;
			fBricks[idx].alive = true;
		}
	}

	// Paddle
	fPaddle.w = kPaddleWidth;
	fPaddle.h = kPaddleHeight;
	fPaddle.x = (w - kPaddleWidth) / 2;
	fPaddle.y = h - kPaddleBottom - kPaddleHeight;

	// Ball
	_ResetBall();
}


void
BreakoutView::_InitFont()
{
	// Try to load font from app directory
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK) {
		BPath appPath(&info.ref);
		BPath dir;
		appPath.GetParent(&dir);
		BPath fontPath(dir);
		fontPath.Append("Nokia Sans S60 Regular.ttf");

		fFont = kvg::Font::load(fontPath.Path(), 16.0f);
		if (fFont)
			fFontLarge = fFont.with_size(48.0f);
	}

	// Fallback: try system font directory
	if (!fFont) {
		BPath fontDir;
		if (find_directory(B_SYSTEM_FONTS_DIRECTORY, &fontDir) == B_OK) {
			BPath fontPath(fontDir);
			fontPath.Append("ttfonts/NotoSansDisplay-Regular.ttf");
			fFont = kvg::Font::load(fontPath.Path(), 16.0f);
			if (fFont)
				fFontLarge = fFont.with_size(48.0f);
		}
	}
}


void
BreakoutView::_ResetBall()
{
	fBall.radius = kBallRadius;
	fBall.launched = false;
	fBall.x = fPaddle.x + fPaddle.w / 2;
	fBall.y = fPaddle.y - kBallRadius - 1;
	fBall.vx = 0;
	fBall.vy = 0;
}


void
BreakoutView::_Update(float dt)
{
	if (fGameOver)
		return;

	float w = fBitmap ? (float)(fBitmap->Bounds().IntegerWidth() + 1) : 800;
	float h = fBitmap ? (float)(fBitmap->Bounds().IntegerHeight() + 1) : 600;

	// Move paddle (mouse has priority)
	if (fPaddleTargetX >= 0) {
		fPaddle.x = fPaddleTargetX - fPaddle.w / 2;
	} else {
		if (fKeyLeft)
			fPaddle.x -= kPaddleSpeed * dt;
		if (fKeyRight)
			fPaddle.x += kPaddleSpeed * dt;
	}

	// Clamp paddle
	if (fPaddle.x < kSideMargin)
		fPaddle.x = kSideMargin;
	if (fPaddle.x + fPaddle.w > w - kSideMargin)
		fPaddle.x = w - kSideMargin - fPaddle.w;

	// Reset key state each frame (no KeyUp in simple setup)
	fKeyLeft = false;
	fKeyRight = false;

	if (!fBall.launched) {
		// Ball follows paddle
		fBall.x = fPaddle.x + fPaddle.w / 2;
		fBall.y = fPaddle.y - kBallRadius - 1;
		return;
	}

	// Move ball
	fBall.x += fBall.vx * dt;
	fBall.y += fBall.vy * dt;

	// Wall collisions
	float r = fBall.radius;

	// Left wall
	if (fBall.x - r < kSideMargin) {
		fBall.x = kSideMargin + r;
		fBall.vx = fabsf(fBall.vx);
	}
	// Right wall
	if (fBall.x + r > w - kSideMargin) {
		fBall.x = w - kSideMargin - r;
		fBall.vx = -fabsf(fBall.vx);
	}
	// Top wall
	if (fBall.y - r < 0) {
		fBall.y = r;
		fBall.vy = fabsf(fBall.vy);
	}

	// Bottom — lose life
	if (fBall.y - r > h) {
		fLives--;
		if (fLives <= 0) {
			fGameOver = true;
		} else {
			_ResetBall();
		}
		return;
	}

	// Paddle collision
	_CheckPaddleCollision();

	// Brick collisions
	_CheckBrickCollisions();

	// Check win
	bool allDead = true;
	for (int i = 0; i < kBrickCount; i++) {
		if (fBricks[i].alive) {
			allDead = false;
			break;
		}
	}
	if (allDead)
		_InitLevel();
}


void
BreakoutView::_CheckPaddleCollision()
{
	float bx = fBall.x, by = fBall.y, r = fBall.radius;
	float px = fPaddle.x, py = fPaddle.y;
	float pw = fPaddle.w, ph = fPaddle.h;

	// Simple AABB check
	if (bx + r < px || bx - r > px + pw)
		return;
	if (by + r < py || by - r > py + ph)
		return;

	// Only bounce if ball is moving downward
	if (fBall.vy <= 0)
		return;

	// Place ball above paddle
	fBall.y = py - r;

	// Angle depends on hit position relative to paddle center
	float center = px + pw / 2;
	float offset = (bx - center) / (pw / 2); // -1..1
	offset = std::max(-0.9f, std::min(0.9f, offset));

	float angle = offset * kvg::pi / 3.0f; // max +-60 degrees
	float speed = sqrtf(fBall.vx * fBall.vx + fBall.vy * fBall.vy);

	fBall.vx = speed * sinf(angle);
	fBall.vy = -speed * cosf(angle);
}


void
BreakoutView::_CheckBrickCollisions()
{
	float bx = fBall.x, by = fBall.y, r = fBall.radius;

	for (int i = 0; i < kBrickCount; i++) {
		if (!fBricks[i].alive)
			continue;

		Brick& brick = fBricks[i];

		// AABB vs circle
		float nearX = std::max(brick.x, std::min(bx, brick.x + brick.w));
		float nearY = std::max(brick.y, std::min(by, brick.y + brick.h));

		float dx = bx - nearX;
		float dy = by - nearY;

		if (dx * dx + dy * dy > r * r)
			continue;

		// Hit!
		brick.alive = false;
		fScore += 10;

		// Determine reflection direction
		// Find which edge is closest
		float overlapLeft = (bx + r) - brick.x;
		float overlapRight = (brick.x + brick.w) - (bx - r);
		float overlapTop = (by + r) - brick.y;
		float overlapBottom = (brick.y + brick.h) - (by - r);

		float minOverlapX = std::min(overlapLeft, overlapRight);
		float minOverlapY = std::min(overlapTop, overlapBottom);

		if (minOverlapX < minOverlapY) {
			fBall.vx = -fBall.vx;
		} else {
			fBall.vy = -fBall.vy;
		}

		// Only break one brick per frame
		break;
	}
}


// --- Rendering ---

void
BreakoutView::_Render()
{
	if (fBitmap == NULL || !fCtx.has_value() || !(*fCtx))
		return;

	kvg::BitmapContext& ctx = *fCtx;

	_DrawBackground(ctx);
	_DrawBricks(ctx);
	_DrawPaddle(ctx);
	_DrawBall(ctx);
	_DrawHUD(ctx);

	if (fGameOver)
		_DrawGameOver(ctx);
}


void
BreakoutView::_DrawBackground(kvg::BitmapContext& ctx)
{
	float w = (float)(fBitmap->Bounds().IntegerWidth() + 1);
	float h = (float)(fBitmap->Bounds().IntegerHeight() + 1);

	float diag = sqrtf(w * w + h * h) / 2.0f;

	kvg::Gradient::Stop stops[] = {
		{0.0f, kvg::Color::from_rgba8(35, 35, 60, 255)},
		{1.0f, kvg::Color::from_rgba8(8, 8, 16, 255)}
	};
	kvg::Gradient grad = kvg::Gradient::create(stops);

	kvg::Path bgRect = kvg::Path::Builder{}
		.add_rect(kvg::Rect(0, 0, w, h))
		.build();

	ctx.save_state();
	ctx.clip_to_path(bgRect);
	ctx.draw_radial_gradient(grad,
		kvg::Point(w / 2, h / 3), 0,
		kvg::Point(w / 2, h / 3), diag,
		nullptr, kGradExtend);
	ctx.restore_state();

	// Side walls — subtle lines
	ctx.set_stroke_color(kvg::Color::from_rgba8(80, 80, 120, 100));
	ctx.set_line_width(2.0f);

	kvg::Path walls = kvg::Path::Builder{}
		.move_to(kSideMargin, 0)
		.line_to(kSideMargin, h)
		.move_to(w - kSideMargin, 0)
		.line_to(w - kSideMargin, h)
		.build();
	ctx.stroke_path(walls);
}


void
BreakoutView::_DrawBricks(kvg::BitmapContext& ctx)
{
	for (int i = 0; i < kBrickCount; i++) {
		if (!fBricks[i].alive)
			continue;

		const Brick& b = fBricks[i];
		uint8 cr = kBrickColors[b.row][0];
		uint8 cg = kBrickColors[b.row][1];
		uint8 cb = kBrickColors[b.row][2];

		kvg::Gradient::Stop stops[] = {
			{0.0f, kvg::Color::from_rgba8(
				(uint8)std::min(255, cr + 40),
				(uint8)std::min(255, cg + 40),
				(uint8)std::min(255, cb + 40), 255)},
			{1.0f, kvg::Color::from_rgba8(
				(uint8)(cr * 2 / 3),
				(uint8)(cg * 2 / 3),
				(uint8)(cb * 2 / 3), 255)}
		};
		kvg::Gradient grad = kvg::Gradient::create(stops);

		kvg::Rect rect(b.x, b.y, b.w, b.h);

		kvg::Path brickPath = kvg::Path::Builder{}
			.add_round_rect(rect, kBrickRadius, kBrickRadius)
			.build();

		ctx.save_state();
		ctx.clip_to_path(brickPath);
		ctx.draw_linear_gradient(grad,
			kvg::Point(b.x, b.y),
			kvg::Point(b.x, b.y + b.h),
			nullptr, kGradExtend);
		ctx.restore_state();

		// Subtle highlight on top edge
		ctx.save_state();
		ctx.set_stroke_color(kvg::Color::from_rgba8(255, 255, 255, 50));
		ctx.set_line_width(1.0f);
		kvg::Path topEdge = kvg::Path::Builder{}
			.move_to(b.x + kBrickRadius, b.y + 0.5f)
			.line_to(b.x + b.w - kBrickRadius, b.y + 0.5f)
			.build();
		ctx.stroke_path(topEdge);
		ctx.restore_state();
	}
}


void
BreakoutView::_DrawPaddle(kvg::BitmapContext& ctx)
{
	kvg::Gradient::Stop stops[] = {
		{0.0f, kvg::Color::from_rgba8(180, 200, 230, 255)},
		{1.0f, kvg::Color::from_rgba8(80, 100, 140, 255)}
	};
	kvg::Gradient grad = kvg::Gradient::create(stops);

	kvg::Rect rect(fPaddle.x, fPaddle.y, fPaddle.w, fPaddle.h);

	ctx.save_state();
	ctx.set_shadow(kvg::Point(0, 3), 6,
		kvg::Color(0, 0, 0, 0.4f));

	kvg::Path paddlePath = kvg::Path::Builder{}
		.add_round_rect(rect, kPaddleRadius, kPaddleRadius)
		.build();

	ctx.save_state();
	ctx.clip_to_path(paddlePath);
	ctx.draw_linear_gradient(grad,
		kvg::Point(fPaddle.x, fPaddle.y),
		kvg::Point(fPaddle.x, fPaddle.y + fPaddle.h),
		nullptr, kGradExtend);
	ctx.restore_state();

	ctx.restore_state(); // shadow
}


void
BreakoutView::_DrawBall(kvg::BitmapContext& ctx)
{
	if (fGameOver)
		return;

	ctx.save_state();
	ctx.set_shadow(kvg::Point(2, 2), 6,
		kvg::Color(0, 0, 0, 0.5f));

	kvg::Gradient::Stop stops[] = {
		{0.0f, kvg::Color::from_rgba8(255, 255, 255, 255)},
		{1.0f, kvg::Color::from_rgba8(180, 200, 240, 255)}
	};
	kvg::Gradient grad = kvg::Gradient::create(stops);

	kvg::Path circle = kvg::Path::Builder{}
		.add_circle(kvg::Point(fBall.x, fBall.y), fBall.radius)
		.build();

	ctx.save_state();
	ctx.clip_to_path(circle);
	ctx.draw_radial_gradient(grad,
		kvg::Point(fBall.x - fBall.radius * 0.3f,
			fBall.y - fBall.radius * 0.3f), 0,
		kvg::Point(fBall.x, fBall.y), fBall.radius * 1.2f,
		nullptr, kGradExtend);
	ctx.restore_state();

	ctx.restore_state(); // shadow
}


void
BreakoutView::_DrawHUD(kvg::BitmapContext& ctx)
{
	if (!fFont)
		return;

	float w = (float)(fBitmap->Bounds().IntegerWidth() + 1);

	ctx.save_state();
	ctx.set_font(fFont);
	ctx.set_fill_color(kvg::Color::from_rgba8(220, 220, 240, 255));

	// Score — top left
	char scoreText[64];
	snprintf(scoreText, sizeof(scoreText), "SCORE: %d", fScore);
	ctx.draw_text(scoreText, kvg::Point(kSideMargin + 8, 24));

	// Lives — top right
	char livesText[64];
	snprintf(livesText, sizeof(livesText), "LIVES: %d", fLives);
	float livesWidth = fFont.measure(livesText);
	ctx.draw_text(livesText,
		kvg::Point(w - kSideMargin - livesWidth - 8, 24));

	// Launch hint
	if (!fBall.launched && !fGameOver) {
		const char* hint = "CLICK TO LAUNCH";
		float hintWidth = fFont.measure(hint);
		ctx.set_fill_color(kvg::Color::from_rgba8(180, 180, 200, 160));
		ctx.draw_text(hint,
			kvg::Point((w - hintWidth) / 2, fPaddle.y - 30));
	}

	ctx.restore_state();
}


void
BreakoutView::_DrawGameOver(kvg::BitmapContext& ctx)
{
	float w = (float)(fBitmap->Bounds().IntegerWidth() + 1);
	float h = (float)(fBitmap->Bounds().IntegerHeight() + 1);

	// Dim overlay
	ctx.save_state();
	ctx.set_fill_color(kvg::Color(0, 0, 0, 0.6f));
	ctx.fill_rect(kvg::Rect(0, 0, w, h));

	if (fFontLarge) {
		ctx.set_font(fFontLarge);
		ctx.set_fill_color(kvg::Color::from_rgba8(240, 80, 80, 255));

		const char* gameOver = "GAME OVER";
		float textWidth = fFontLarge.measure(gameOver);
		ctx.draw_text(gameOver,
			kvg::Point((w - textWidth) / 2, h / 2 - 10));
	}

	if (fFont) {
		ctx.set_font(fFont);
		ctx.set_fill_color(kvg::Color::from_rgba8(200, 200, 220, 200));

		char finalScore[64];
		snprintf(finalScore, sizeof(finalScore), "FINAL SCORE: %d", fScore);
		float scoreWidth = fFont.measure(finalScore);
		ctx.draw_text(finalScore,
			kvg::Point((w - scoreWidth) / 2, h / 2 + 30));

		const char* restart = "CLICK TO RESTART";
		float restartWidth = fFont.measure(restart);
		ctx.draw_text(restart,
			kvg::Point((w - restartWidth) / 2, h / 2 + 60));
	}

	ctx.restore_state();
}
