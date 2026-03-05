/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 *
 * Screen lock / screen saver filter.
 * Desktop: full hot-corner support (blank + never-blank corners),
 *          launches screen_blanker after inactivity timeout.
 * Tablet: inactivity timeout only (no corner detection),
 *          triggers display sleep / lock screen.
 *
 * Compatible with existing ScreenSaverSettings file format.
 */
#ifndef SCREEN_LOCK_FILTER_H
#define SCREEN_LOCK_FILTER_H

#include "FilterChain.h"

#include <Looper.h>
#include <MessageRunner.h>
#include <Node.h>
#include <OS.h>
#include <Rect.h>

#include <ScreenSaverSettings.h>


class ScreenLockController;


class ScreenLockFilter : public SubFilter {
public:
								ScreenLockFilter(FilterChain* owner);
	virtual						~ScreenLockFilter();

	virtual	status_t			InitCheck();
	virtual	filter_result		Filter(BMessage* message, BList* outList);
	virtual	uint32				Stage() const { return kStagePolicy; }

	virtual	void				ReloadSettings();
			void				CheckTimeout();
			void				CheckCornerInvoke();
			void				SetBlankerRunning(bool running);

private:
			void				_Invoke();
			void				_UpdateCornerRects();
			BRect				_ScreenCorner(screen_corner corner,
									int32 size);
			void				_WatchSettings();

			FilterChain*		fOwner;
			ScreenLockController*	fController;

			// Inactivity tracking
			bigtime_t			fLastEventTime;
			bigtime_t			fBlankTime;
			bigtime_t			fSnoozeTime;
			bool				fEnabled;
			bool				fBlankerRunning;

			// Hot corners (desktop only)
			screen_corner		fBlankCorner;
			screen_corner		fNeverBlankCorner;
			screen_corner		fCurrentCorner;
			BRect				fBlankRect;
			BRect				fNeverBlankRect;
			uint32				fFrameNum;

			// Timers
			BMessageRunner*		fTimeoutRunner;
			BMessageRunner*		fCornerRunner;

			// Settings file watch
			ScreenSaverSettings	fSettings;
			node_ref			fSettingsNodeRef;
			bool				fWatchingFile;
			bool				fWatchingDirectory;
};


class ScreenLockController : public BLooper {
public:
								ScreenLockController(
									ScreenLockFilter* filter);
	virtual						~ScreenLockController();

	virtual	void				MessageReceived(BMessage* message);

private:
			ScreenLockFilter*	fFilter;
};

#endif
