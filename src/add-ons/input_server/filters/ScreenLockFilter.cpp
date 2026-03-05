/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "ScreenLockFilter.h"

#include <new>
#include <strings.h>
#include <syslog.h>

#include <Application.h>
#include <Autolock.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>


#ifndef SCREEN_BLANKER_SIG
#	define SCREEN_BLANKER_SIG "application/x-vnd.Haiku-ScreenBlanker"
#endif

static const int32 kBlankCornerSize = 5;
static const int32 kNeverBlankCornerSize = 10;
static const bigtime_t kCornerDelay = 1000000LL;	// 1 second

enum {
	kMsgCheckTimeout	= 'SLck',
	kMsgCornerInvoke	= 'SLci',
};


// ScreenLockController

ScreenLockController::ScreenLockController(ScreenLockFilter* filter)
	:
	BLooper("screen lock controller", B_LOW_PRIORITY),
	fFilter(filter)
{
}


ScreenLockController::~ScreenLockController()
{
}


void
ScreenLockController::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCheckTimeout:
			fFilter->CheckTimeout();
			break;

		case kMsgCornerInvoke:
			fFilter->CheckCornerInvoke();
			break;

		case B_NODE_MONITOR:
			fFilter->ReloadSettings();
			break;

		case B_SOME_APP_LAUNCHED:
		case B_SOME_APP_QUIT:
		{
			const char* signature;
			if (message->FindString("be:signature", &signature) == B_OK
				&& strcasecmp(signature, SCREEN_BLANKER_SIG) == 0) {
				fFilter->SetBlankerRunning(
					message->what == B_SOME_APP_LAUNCHED);
			}
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


// ScreenLockFilter

ScreenLockFilter::ScreenLockFilter(FilterChain* owner)
	:
	fOwner(owner),
	fController(NULL),
	fLastEventTime(system_time()),
	fBlankTime(0),
	fSnoozeTime(0),
	fEnabled(false),
	fBlankerRunning(false),
	fBlankCorner(NO_CORNER),
	fNeverBlankCorner(NO_CORNER),
	fCurrentCorner(NO_CORNER),
	fFrameNum(0),
	fTimeoutRunner(NULL),
	fCornerRunner(NULL),
	fWatchingFile(false),
	fWatchingDirectory(false)
{
	fController = new(std::nothrow) ScreenLockController(this);
	if (fController == NULL)
		return;

	fController->Run();
	be_roster->StartWatching(fController);

	ReloadSettings();
}


ScreenLockFilter::~ScreenLockFilter()
{
	delete fCornerRunner;
	delete fTimeoutRunner;

	if (fController != NULL) {
		be_roster->StopWatching(fController);

		if (fWatchingFile || fWatchingDirectory)
			watch_node(&fSettingsNodeRef, B_STOP_WATCHING, fController);

		if (fController->Lock())
			fController->Quit();
	}
}


status_t
ScreenLockFilter::InitCheck()
{
	return fController != NULL ? B_OK : B_NO_MEMORY;
}


filter_result
ScreenLockFilter::Filter(BMessage* message, BList* /*outList*/)
{
	fLastEventTime = system_time();

	if (message->what == B_MOUSE_MOVED) {
		BPoint where;
		if (message->FindPoint("where", &where) != B_OK)
			return B_DISPATCH_MESSAGE;

		// Refresh screen rectangles periodically.
		if ((fFrameNum++ % 32) == 0)
			_UpdateCornerRects();

		if (fBlankRect.IsValid() && fBlankRect.Contains(where)) {
			fCurrentCorner = fBlankCorner;
			// Start corner timer if not already running.
			if (fCornerRunner != NULL)
				fCornerRunner->SetInterval(kCornerDelay);
		} else if (fNeverBlankRect.IsValid()
			&& fNeverBlankRect.Contains(where)) {
			fCurrentCorner = fNeverBlankCorner;
		} else {
			fCurrentCorner = NO_CORNER;
		}
	}

	return B_DISPATCH_MESSAGE;
}


void
ScreenLockFilter::ReloadSettings()
{
	_WatchSettings();

	delete fCornerRunner;
	delete fTimeoutRunner;
	fCornerRunner = NULL;
	fTimeoutRunner = NULL;

	fSettings.Load();

	fEnabled = (fSettings.TimeFlags() & ENABLE_SAVER) != 0;
	fBlankTime = fSettings.BlankTime();
	fSnoozeTime = fBlankTime;
	fBlankCorner = fSettings.BlankCorner();
	fNeverBlankCorner = fSettings.NeverBlankCorner();

	_UpdateCornerRects();

	// Inactivity timeout runner.
	if (fEnabled && fBlankTime > 0) {
		BMessage check(kMsgCheckTimeout);
		fTimeoutRunner = new(std::nothrow) BMessageRunner(
			fController, &check, fSnoozeTime);
		if (fTimeoutRunner == NULL
			|| fTimeoutRunner->InitCheck() != B_OK) {
			syslog(LOG_ERR, "screen lock filter: "
				"failed to create timeout runner\n");
		}
	}

	// Corner invoke runner.
	if (fBlankCorner != NO_CORNER || fNeverBlankCorner != NO_CORNER) {
		BMessage invoke(kMsgCornerInvoke);
		fCornerRunner = new(std::nothrow) BMessageRunner(
			fController, &invoke, B_INFINITE_TIMEOUT);
	}
}


void
ScreenLockFilter::CheckTimeout()
{
	if (!fEnabled || fBlankerRunning)
		return;

	bigtime_t now = system_time();
	bigtime_t idle = now - fLastEventTime;

	if (idle >= fBlankTime) {
		_Invoke();
		fSnoozeTime = fBlankTime;
	} else {
		// Event happened mid-snooze; reschedule for remainder.
		fSnoozeTime = fBlankTime - idle;
	}

	if (fTimeoutRunner != NULL)
		fTimeoutRunner->SetInterval(fSnoozeTime);
}


void
ScreenLockFilter::CheckCornerInvoke()
{
	if (!fEnabled || fBlankerRunning)
		return;

	bigtime_t idle = system_time() - fLastEventTime;

	if (fCurrentCorner == fBlankCorner && fBlankCorner != NO_CORNER
		&& idle >= kCornerDelay) {
		_Invoke();
	}
}


void
ScreenLockFilter::SetBlankerRunning(bool running)
{
	fBlankerRunning = running;
}


void
ScreenLockFilter::_Invoke()
{
	// Never-blank corner overrides everything.
	if (fCurrentCorner == fNeverBlankCorner
		&& fNeverBlankCorner != NO_CORNER)
		return;

	if (fBlankerRunning || be_roster->IsRunning(SCREEN_BLANKER_SIG))
		return;

	if (be_roster->Launch(SCREEN_BLANKER_SIG) == B_OK) {
		fBlankerRunning = true;
		return;
	}

	// Fallback: launch by path.
	BPath path;
	if (find_directory(B_SYSTEM_BIN_DIRECTORY, &path) == B_OK
		&& path.Append("screen_blanker") == B_OK) {
		BEntry entry(path.Path());
		entry_ref ref;
		if (entry.GetRef(&ref) == B_OK
			&& be_roster->Launch(&ref) == B_OK) {
			fBlankerRunning = true;
		}
	}
}


void
ScreenLockFilter::_UpdateCornerRects()
{
	fBlankRect = _ScreenCorner(fBlankCorner, kBlankCornerSize);
	fNeverBlankRect = _ScreenCorner(fNeverBlankCorner,
		kNeverBlankCornerSize);
}


BRect
ScreenLockFilter::_ScreenCorner(screen_corner corner, int32 size)
{
	BRect frame = BScreen().Frame();

	switch (corner) {
		case UP_LEFT_CORNER:
			return BRect(frame.left, frame.top,
				frame.left + size - 1, frame.top + size - 1);
		case UP_RIGHT_CORNER:
			return BRect(frame.right - size + 1, frame.top,
				frame.right, frame.top + size - 1);
		case DOWN_RIGHT_CORNER:
			return BRect(frame.right - size + 1,
				frame.bottom - size + 1,
				frame.right, frame.bottom);
		case DOWN_LEFT_CORNER:
			return BRect(frame.left,
				frame.bottom - size + 1,
				frame.left + size - 1, frame.bottom);
		default:
			return BRect(-1, -1, -2, -2);
	}
}


void
ScreenLockFilter::_WatchSettings()
{
	BEntry entry(fSettings.Path().Path());

	if (entry.Exists()) {
		if (fWatchingFile)
			return;

		if (fWatchingDirectory) {
			watch_node(&fSettingsNodeRef, B_STOP_WATCHING, fController);
			fWatchingDirectory = false;
		}

		if (entry.GetNodeRef(&fSettingsNodeRef) == B_OK
			&& watch_node(&fSettingsNodeRef, B_WATCH_ALL,
				fController) == B_OK) {
			fWatchingFile = true;
		}
	} else {
		if (fWatchingDirectory)
			return;

		if (fWatchingFile) {
			watch_node(&fSettingsNodeRef, B_STOP_WATCHING, fController);
			fWatchingFile = false;
		}

		BEntry dir;
		if (entry.GetParent(&dir) == B_OK
			&& dir.GetNodeRef(&fSettingsNodeRef) == B_OK
			&& watch_node(&fSettingsNodeRef, B_WATCH_DIRECTORY,
				fController) == B_OK) {
			fWatchingDirectory = true;
		}
	}
}
