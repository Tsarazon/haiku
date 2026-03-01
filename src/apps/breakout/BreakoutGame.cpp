/*
 * Copyright 2026 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Breakout Game - KosmVG test application
 */

#include "BreakoutView.h"

#include <Application.h>
#include <Window.h>


class BreakoutWindow : public BWindow {
public:
	BreakoutWindow()
		:
		BWindow(BRect(100, 100, 899, 699), "KosmVG Breakout",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new BreakoutView());
	}
};


class BreakoutApp : public BApplication {
public:
	BreakoutApp()
		:
		BApplication("application/x-vnd.Haiku-BreakoutGame")
	{
	}

	void ReadyToRun() override
	{
		(new BreakoutWindow())->Show();
	}
};


int
main()
{
	BreakoutApp app;
	app.Run();
	return 0;
}
