/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "InputUtils.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <input_globals.h>


// KeymapHandler

KeymapHandler::KeymapHandler()
	:
	fRawKeyMap(NULL),
	fRawChars(NULL),
	fRawCharsSize(0),
	fLock("keymap handler lock")
{
	ReloadKeymap();
}


KeymapHandler::~KeymapHandler()
{
	free(fRawKeyMap);
	free(fRawChars);
}


status_t
KeymapHandler::ReloadKeymap()
{
	BAutolock lock(fLock);

	free(fRawKeyMap);
	free(fRawChars);
	fRawKeyMap = NULL;
	fRawChars = NULL;

	_get_key_map(&fRawKeyMap, &fRawChars, &fRawCharsSize);
	if (fRawKeyMap == NULL) {
		fprintf(stderr, "KeymapHandler: failed to load keymap\n");
		return B_ERROR;
	}

	// Also update the BKeymap (used for GetChars/ActiveDeadKey)
	fKeymap.SetToCurrent();

	return B_OK;
}


uint32
KeymapHandler::ModifierForKey(uint32 keycode) const
{
	return fKeymap.Modifier(keycode);
}


bool
KeymapHandler::IsLockModifier(uint32 modifier) const
{
	return (modifier & (B_CAPS_LOCK | B_NUM_LOCK | B_SCROLL_LOCK)) != 0;
}


uint32
KeymapHandler::UpdateModifiers(uint32 keycode, bool isKeyDown,
	uint32 currentModifiers)
{
	uint32 modifier = ModifierForKey(keycode);
	if (modifier == 0)
		return currentModifiers;

	bool isLock = IsLockModifier(modifier);
	uint32 newModifiers = currentModifiers;

	if ((isKeyDown && !isLock)
		|| (isKeyDown && !(currentModifiers & modifier))) {
		newModifiers |= modifier;
	} else if (!isKeyDown || isLock) {
		newModifiers &= ~modifier;

		// Preserve combined modifier flags when individual keys remain pressed
		if (newModifiers & (B_LEFT_SHIFT_KEY | B_RIGHT_SHIFT_KEY))
			newModifiers |= B_SHIFT_KEY;
		if (newModifiers & (B_LEFT_COMMAND_KEY | B_RIGHT_COMMAND_KEY))
			newModifiers |= B_COMMAND_KEY;
		if (newModifiers & (B_LEFT_CONTROL_KEY | B_RIGHT_CONTROL_KEY))
			newModifiers |= B_CONTROL_KEY;
		if (newModifiers & (B_LEFT_OPTION_KEY | B_RIGHT_OPTION_KEY))
			newModifiers |= B_OPTION_KEY;
	}

	return newModifiers;
}


void
KeymapHandler::GetChars(uint32 keycode, uint32 modifiers,
	uint8 activeDeadKey, char** string, int32* numBytes)
{
	fKeymap.GetChars(keycode, modifiers, activeDeadKey, string, numBytes);
}


void
KeymapHandler::GetRawChars(uint32 keycode, char** string, int32* numBytes)
{
	fKeymap.GetChars(keycode, 0, 0, string, numBytes);
}


uint8
KeymapHandler::ActiveDeadKey(uint32 keycode, uint32 modifiers) const
{
	return fKeymap.ActiveDeadKey(keycode, modifiers);
}


uint32
KeymapHandler::KeyForModifier(uint32 modifier) const
{
	return fKeymap.KeyForModifier(modifier);
}


uint32
KeymapHandler::InitialLockState() const
{
	if (fRawKeyMap == NULL)
		return 0;

	return fRawKeyMap->lock_settings;
}


// SoftwareRepeat

SoftwareRepeat::SoftwareRepeat(BInputServerDevice* target)
	:
	fTarget(target),
	fThread(-1),
	fSem(-1),
	fDelay(500000),
	fRate(250)
{
}


SoftwareRepeat::~SoftwareRepeat()
{
	StopRepeating();
}


void
SoftwareRepeat::StartRepeating(BMessage* keyDown)
{
	StopRepeating();

	fRepeatMsg = *keyDown;
	fSem = create_sem(0, "key repeat sem");
	fThread = spawn_thread(_RepeatThread, "key repeat",
		B_REAL_TIME_DISPLAY_PRIORITY + 4, this);

	if (fThread >= B_OK)
		resume_thread(fThread);
}


void
SoftwareRepeat::StopRepeating()
{
	if (fThread >= B_OK) {
		release_sem(fSem);
		status_t res;
		wait_for_thread(fThread, &res);
		fThread = -1;
		delete_sem(fSem);
		fSem = -1;
	}
}


void
SoftwareRepeat::SetRepeatDelay(bigtime_t delay)
{
	fDelay = delay;
}


void
SoftwareRepeat::SetRepeatRate(int32 rate)
{
	fRate = rate < 1 ? 1 : rate;
}


status_t
SoftwareRepeat::_RepeatThread(void* arg)
{
	SoftwareRepeat* self = static_cast<SoftwareRepeat*>(arg);

	// Initial delay before first repeat
	status_t res = acquire_sem_etc(self->fSem, 1,
		B_RELATIVE_TIMEOUT, self->fDelay);
	if (res >= B_OK)
		return B_OK;

	while (true) {
		self->fRepeatMsg.ReplaceInt64("when", system_time());

		int32 count = 0;
		self->fRepeatMsg.FindInt32("be:key_repeat", &count);
		self->fRepeatMsg.ReplaceInt32("be:key_repeat", count + 1);

		BMessage* msg = new(std::nothrow) BMessage(self->fRepeatMsg);
		if (msg != NULL) {
			if (self->fTarget->EnqueueMessage(msg) != B_OK)
				delete msg;
		}

		bigtime_t interval = (bigtime_t)10000000 / self->fRate;
		res = acquire_sem_etc(self->fSem, 1, B_RELATIVE_TIMEOUT, interval);
		if (res >= B_OK)
			return B_OK;
	}
}
