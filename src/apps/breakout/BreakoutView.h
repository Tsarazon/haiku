/*
 * Copyright 2026 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Breakout Game - KosmVG test application
 */
#ifndef BREAKOUT_VIEW_H
#define BREAKOUT_VIEW_H

#include <View.h>

#include <optional>

#include <kosmvg.hpp>

class BBitmap;


static const int kBrickRows = 5;
static const int kBrickCols = 10;
static const int kBrickCount = kBrickRows * kBrickCols;
static const int kMaxLives = 3;


struct Brick {
	float	x, y, w, h;
	int		row;
	bool	alive;
};


struct Ball {
	float	x, y;
	float	vx, vy;
	float	radius;
	bool	launched;
};


struct Paddle {
	float	x, y;
	float	w, h;
};


class BreakoutView : public BView {
public:
						BreakoutView();
	virtual				~BreakoutView();

	void				AttachedToWindow() override;
	void				Pulse() override;
	void				Draw(BRect updateRect) override;
	void				FrameResized(float width, float height) override;
	void				MouseMoved(BPoint where, uint32 transit,
							const BMessage* dragMessage) override;
	void				MouseDown(BPoint where) override;
	void				KeyDown(const char* bytes, int32 numBytes) override;

private:
	void				_InitCanvas();
	void				_InitLevel();
	void				_InitFont();
	void				_Update(float dt);
	void				_CheckBrickCollisions();
	void				_CheckPaddleCollision();
	void				_ResetBall();

	void				_Render();
	void				_DrawBackground(kvg::BitmapContext& ctx);
	void				_DrawBricks(kvg::BitmapContext& ctx);
	void				_DrawPaddle(kvg::BitmapContext& ctx);
	void				_DrawBall(kvg::BitmapContext& ctx);
	void				_DrawHUD(kvg::BitmapContext& ctx);
	void				_DrawGameOver(kvg::BitmapContext& ctx);

	// Rendering
	BBitmap*						fBitmap;
	std::optional<kvg::BitmapContext>	fCtx;
	kvg::Font						fFont;
	kvg::Font						fFontLarge;

	// Game state
	Brick							fBricks[kBrickCount];
	Ball							fBall;
	Paddle							fPaddle;
	int								fLives;
	int								fScore;
	bool							fGameOver;
	float							fPaddleTargetX;

	// Key state
	bool							fKeyLeft;
	bool							fKeyRight;
};


#endif // BREAKOUT_VIEW_H
