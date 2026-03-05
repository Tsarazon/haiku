/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */
#ifndef HW_DEVICES_H
#define HW_DEVICES_H

#include "InputDeviceCore.h"
#include "InputUtils.h"

#include <Handler.h>
#include <Locker.h>
#include <Message.h>

#include <kb_mouse_settings.h>
#include <keyboard_mouse_driver.h>


class TouchpadMovement;

class HWKeyboardDevice : public InputDeviceBase, public BHandler {
public:
								HWKeyboardDevice(InputDeviceManager* owner,
									const char* path);
	virtual						~HWKeyboardDevice();

	// BHandler
	virtual	void				MessageReceived(BMessage* message);

protected:
	virtual	char*				BuildShortName() const;
	virtual	void				OnStart();
	virtual	status_t			ReadAndDispatch();
	virtual	void				UpdateSettings(uint32 opcode);

private:
			void				_UpdateLEDs();
			status_t			_EnqueueInlineInputMethod(int32 opcode,
									const char* string = NULL,
									bool confirmed = false,
									BMessage* keyDown = NULL);

			KeymapHandler		fKeymap;
			uint32				fModifiers;
			uint32				fCommandKey;
			uint32				fControlKey;
			uint16				fKeyboardID;

			uint8				fActiveDeadKey;
			uint32				fLastKeyCode;
			uint32				fRepeatCount;
			uint8				fStates[16];
			bool				fCtrlAltDelPressed;

	volatile bool				fInputMethodStarted;
};

#include "movement_maker.h"

class HWMouseDevice : public InputDeviceBase {
public:
								HWMouseDevice(InputDeviceManager* owner,
									const char* path,
									bool expectTouchpad);
	virtual						~HWMouseDevice();

	virtual	void				ScheduleSettingsUpdate(uint32 opcode,
									BMessage* message = NULL);

protected:
	virtual	char*				BuildShortName() const;
	virtual	void				OnStart();
	virtual	status_t			ReadAndDispatch();
	virtual	void				UpdateSettings(uint32 opcode);

private:
			BMessage*			_BuildMouseMessage(uint32 what,
									uint64 when, uint32 buttons,
									int32 deltaX, int32 deltaY) const;
			void				_ComputeAcceleration(
									const mouse_movement& movements,
									int32& deltaX, int32& deltaY,
									float& historyDeltaX,
									float& historyDeltaY) const;
			uint32				_RemapButtons(uint32 buttons) const;
			status_t			_GetTouchpadSettingsPath(BPath& path);
			status_t			_UpdateTouchpadSettings(BMessage* message);

			mouse_settings		fSettings;
			bool				fDeviceRemapsButtons;
			bool				fExpectTouchpad;
			bool				fIsTouchpad;

			uint32				fLastButtons;
			float				fHistoryDeltaX;
			float				fHistoryDeltaY;

			TouchpadMovement	fTouchpadMovement;
			BMessage*			fTouchpadSettingsMessage;
			BLocker				fTouchpadSettingsLock;

			// Touchpad-specific
			touchpad_movement	fLastTouchpadMovement;
			bigtime_t			fTouchpadEventTimeout;

			bigtime_t			fNextTransferTime;
};

class HWTabletDevice : public InputDeviceBase {
public:
								HWTabletDevice(InputDeviceManager* owner,
									const char* path);

protected:
	virtual	char*				BuildShortName() const;
	virtual	void				OnStart();
	virtual	status_t			ReadAndDispatch();
	virtual	void				UpdateSettings(uint32 opcode);

private:
			BMessage*			_BuildMouseMessage(uint32 what,
									uint64 when, uint32 buttons,
									float xPosition, float yPosition) const;

			mouse_settings		fSettings;
			uint32				fLastButtons;
			float				fLastXPosition;
			float				fLastYPosition;
			bigtime_t			fNextTransferTime;
};

class TouchscreenDevice : public InputDeviceBase {
public:
								TouchscreenDevice(
									InputDeviceManager* owner,
									const char* path);

protected:
	virtual	char*				BuildShortName() const;
	virtual	void				OnStart();
	virtual	status_t			ReadAndDispatch();
	virtual	void				UpdateSettings(uint32 opcode);

private:
			bigtime_t			fClickSpeed;
			uint32				fButtons;
			float				fX, fY;
			float				fPressure;
			int32				fClicks;
			bigtime_t			fLastClick;
			bigtime_t			fNextTransferTime;
};

#endif
