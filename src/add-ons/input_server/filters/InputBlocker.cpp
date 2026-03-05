/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "InputBlocker.h"

#include <stdlib.h>
#include <string.h>

#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Path.h>
#include <PathMonitor.h>

#include <touchpad_settings.h>

// Pointing device subtypes used in be:device_subtype field.
// Defined in InterfaceDefs.h on Haiku; guarded here for portability.
#ifndef B_MOUSE_POINTING_DEVICE
enum {
	B_MOUSE_POINTING_DEVICE		= 0,
	B_TOUCHPAD_POINTING_DEVICE	= 1,
	B_TABLET_POINTING_DEVICE	= 2,
};
#endif


InputBlocker::InputBlocker(FilterChain* owner)
	:
	fOwner(owner),
	fLastKeystroke(0),
	fBlockThreshold(0),
	fSettingsPath(NULL)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append(TOUCHPAD_SETTINGS_FILE) == B_OK) {
		fSettingsPath = strdup(path.Path());
	}

	// Watch for settings file changes.
	if (fSettingsPath != NULL && fOwner->Controller() != NULL) {
		BPrivate::BPathMonitor::StartWatching(fSettingsPath,
			B_WATCH_STAT | B_WATCH_FILES_ONLY,
			fOwner->Controller());
	}

	ReloadSettings();
}


InputBlocker::~InputBlocker()
{
	if (fSettingsPath != NULL && fOwner->Controller() != NULL) {
		BPrivate::BPathMonitor::StopWatching(
			BMessenger(fOwner->Controller()));
	}

	free(fSettingsPath);
}


status_t
InputBlocker::InitCheck()
{
	return B_OK;
}


void
InputBlocker::ReloadSettings()
{
	fBlockThreshold = 0;

	if (fSettingsPath == NULL)
		return;

	BFile file(fSettingsPath, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) == B_OK) {
		int16 threshold = 0;
		if (settings.FindInt16("padblocker_threshold", &threshold) == B_OK
			&& threshold > 0) {
			fBlockThreshold = (bigtime_t)threshold * 1000;
		}
	} else {
		// Legacy 28-byte binary format.
		touchpad_settings legacy;
		file.Seek(0, SEEK_SET);
		off_t size;
		if (file.GetSize(&size) == B_OK && size >= 28) {
			if (file.Read(&legacy, sizeof(legacy)) >= 20
				&& legacy.padblocker_threshold > 0) {
				fBlockThreshold = (bigtime_t)legacy.padblocker_threshold * 1000;
			}
		}
	}

	if (fBlockThreshold == 0)
		fBlockThreshold = kDefaultTouchpadSettings.padblocker_threshold * 1000;
}


filter_result
InputBlocker::Filter(BMessage* message, BList* /*outList*/)
{
	switch (message->what) {
		case B_KEY_DOWN:
		case B_KEY_UP:
		{
			// Only real physical keystrokes, not repeats.
			int32 repeat;
			if (message->FindInt32("be:key_repeat", &repeat) != B_OK)
				fLastKeystroke = system_time();
			break;
		}

		case B_MOUSE_DOWN:
		{
			if (fBlockThreshold == 0)
				break;

			// Only block touch-based devices, never mice.
			int32 subtype;
			if (message->FindInt32("be:device_subtype", &subtype) != B_OK)
				break;

			if (subtype != B_TOUCHPAD_POINTING_DEVICE
				&& subtype != B_TABLET_POINTING_DEVICE)
				break;

			bigtime_t elapsed = system_time() - fLastKeystroke;
			if (elapsed < fBlockThreshold)
				return B_SKIP_MESSAGE;

			break;
		}
	}

	return B_DISPATCH_MESSAGE;
}
