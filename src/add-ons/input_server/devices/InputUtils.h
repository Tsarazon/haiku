/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */
#ifndef INPUT_UTILS_H
#define INPUT_UTILS_H

#include <InputServerDevice.h>
#include <InterfaceDefs.h>
#include <Keymap.h>
#include <Autolock.h>
#include <Locker.h>
#include <Message.h>
#include <OS.h>

class KeymapHandler {
public:
								KeymapHandler();
								~KeymapHandler();

			status_t			ReloadKeymap();

			// Modifier tracking
			uint32				UpdateModifiers(uint32 keycode,
									bool isKeyDown, uint32 currentModifiers);
			uint32				ModifierForKey(uint32 keycode) const;
			bool				IsLockModifier(uint32 modifier) const;

			// Character lookup
			void				GetChars(uint32 keycode, uint32 modifiers,
									uint8 activeDeadKey,
									char** string, int32* numBytes);
			void				GetRawChars(uint32 keycode,
									char** string, int32* numBytes);
			uint8				ActiveDeadKey(uint32 keycode,
									uint32 modifiers) const;

			// Key for specific modifier (e.g. left control)
			uint32				KeyForModifier(uint32 modifier) const;

			// Lock state from keymap
			uint32				InitialLockState() const;

			BLocker&			Lock() { return fLock; }

private:
			BKeymap				fKeymap;
			key_map*			fRawKeyMap;
			char*				fRawChars;
			ssize_t				fRawCharsSize;
			BLocker				fLock;
};

class SoftwareRepeat {
public:
								SoftwareRepeat(BInputServerDevice* target);
								~SoftwareRepeat();

			void				StartRepeating(BMessage* keyDown);
			void				StopRepeating();

			void				SetRepeatDelay(bigtime_t delay);
			void				SetRepeatRate(int32 rate);

private:
	static	status_t			_RepeatThread(void* arg);

			BInputServerDevice*	fTarget;
			BMessage			fRepeatMsg;
			thread_id			fThread;
			sem_id				fSem;
			bigtime_t			fDelay;
			int32				fRate;
};

#endif
