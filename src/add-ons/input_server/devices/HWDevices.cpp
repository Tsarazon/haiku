/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "HWDevices.h"
#include "TeamMonitorWindow.h"

#include <errno.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <AutoDeleter.h>
#include <Autolock.h>
#include <File.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <Path.h>

#include <InputServerTypes.h>
#include <keyboard_mouse_driver.h>
#include <touchpad_settings.h>


// HWKeyboardDevice

HWKeyboardDevice::HWKeyboardDevice(InputDeviceManager* owner,
	const char* path)
	:
	InputDeviceBase(owner, path, B_KEYBOARD_DEVICE),
	BHandler("hw keyboard"),
	fModifiers(0),
	fCommandKey(0),
	fControlKey(0),
	fKeyboardID(0),
	fActiveDeadKey(0),
	fLastKeyCode(0),
	fRepeatCount(1),
	fCtrlAltDelPressed(false),
	fInputMethodStarted(false)
{
	memset(fStates, 0, sizeof(fStates));

	if (be_app->Lock()) {
		be_app->AddHandler(this);
		be_app->Unlock();
	}
}


HWKeyboardDevice::~HWKeyboardDevice()
{
	if (be_app->Lock()) {
		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
}


void
HWKeyboardDevice::MessageReceived(BMessage* message)
{
	if (message->what != B_INPUT_METHOD_EVENT) {
		BHandler::MessageReceived(message);
		return;
	}

	int32 opcode;
	if (message->FindInt32("be:opcode", &opcode) == B_OK
		&& opcode == B_INPUT_METHOD_STOPPED) {
		fInputMethodStarted = false;
	}
}


char*
HWKeyboardDevice::BuildShortName() const
{
	// Reuse base logic but with "Keyboard" suffix
	// The base already handles this for B_KEYBOARD_DEVICE
	return InputDeviceBase::BuildShortName();
}


void
HWKeyboardDevice::OnStart()
{
	if (fKeyboardID == 0) {
		if (ioctl(fFD, KB_GET_KEYBOARD_ID, &fKeyboardID,
				sizeof(fKeyboardID)) == 0) {
			BMessage message(IS_SET_KEYBOARD_ID);
			message.AddInt16("id", fKeyboardID);
			be_app->PostMessage(&message);
		}
	}
}


void
HWKeyboardDevice::UpdateSettings(uint32 opcode)
{
	if (opcode == 0 || opcode == B_KEY_REPEAT_RATE_CHANGED) {
		int32 rate;
		if (get_key_repeat_rate(&rate) == B_OK)
			ioctl(fFD, KB_SET_KEY_REPEAT_RATE, &rate, sizeof(int32));
	}

	if (opcode == 0 || opcode == B_KEY_REPEAT_DELAY_CHANGED) {
		bigtime_t delay;
		if (get_key_repeat_delay(&delay) == B_OK)
			ioctl(fFD, KB_SET_KEY_REPEAT_DELAY, &delay, sizeof(bigtime_t));
	}

	if (opcode == 0 || opcode == B_KEY_MAP_CHANGED
		|| opcode == B_KEY_LOCKS_CHANGED) {
		BAutolock lock(fKeymap.Lock());
		fKeymap.ReloadKeymap();
		fModifiers = fKeymap.InitialLockState();
		_UpdateLEDs();
		fControlKey = fKeymap.KeyForModifier(B_LEFT_CONTROL_KEY);
		fCommandKey = fKeymap.KeyForModifier(B_LEFT_COMMAND_KEY);
	}
}


status_t
HWKeyboardDevice::ReadAndDispatch()
{
	raw_key_info keyInfo;
	status_t status = ioctl(fFD, KB_READ, &keyInfo, sizeof(keyInfo));
	if (status < 0)
		status = errno;

	if (status == B_INTERRUPTED)
		return B_INTERRUPTED;
	if (status == B_BUSY)
		return B_BUSY;
	if (status != B_OK)
		return B_ERROR;

	uint32 keycode = keyInfo.keycode;
	bool isKeyDown = keyInfo.is_keydown;

	if (keycode == 0)
		return B_OK;

	// Menu key (Deskbar)
	if (isKeyDown && keycode == 0x68) {
		bool noOtherKeyPressed = true;
		for (int32 i = 0; i < 16; i++) {
			if (fStates[i] != 0) {
				noOtherKeyPressed = false;
				break;
			}
		}
		if (noOtherKeyPressed) {
			BMessenger deskbar("application/x-vnd.Be-TSKB");
			if (deskbar.IsValid())
				deskbar.SendMessage('BeMn');
		}
	}

	// Update key states
	if (keycode < 256) {
		if (isKeyDown)
			fStates[keycode >> 3] |= (1 << (7 - (keycode & 0x7)));
		else
			fStates[keycode >> 3] &= ~(1 << (7 - (keycode & 0x7)));
	}

	// Ctrl+Alt+Delete -> Team Monitor
	if (isKeyDown && keycode == 0x34
		&& (fStates[fCommandKey >> 3] & (1 << (7 - (fCommandKey & 0x7))))
		&& (fStates[fControlKey >> 3] & (1 << (7 - (fControlKey & 0x7))))) {
		TeamMonitorWindow* tmw = fOwner->GetTeamMonitorWindow();
		if (tmw != NULL)
			tmw->Enable();
		fCtrlAltDelPressed = true;
	}

	if (fCtrlAltDelPressed) {
		TeamMonitorWindow* tmw = fOwner->GetTeamMonitorWindow();
		if (tmw != NULL) {
			BMessage message(kMsgCtrlAltDelPressed);
			message.AddBool("key down", isKeyDown);
			tmw->PostMessage(&message);
		}
		if (!isKeyDown)
			fCtrlAltDelPressed = false;
	}

	// Modifier handling
	BAutolock lock(fKeymap.Lock());

	uint32 modifier = fKeymap.ModifierForKey(keycode);
	bool isLock = fKeymap.IsLockModifier(modifier);

	if (modifier != 0 && (!isLock || isKeyDown)) {
		uint32 oldModifiers = fModifiers;
		fModifiers = fKeymap.UpdateModifiers(keycode, isKeyDown, fModifiers);

		if (fModifiers != oldModifiers) {
			BMessage* message = new(std::nothrow) BMessage(B_MODIFIERS_CHANGED);
			if (message == NULL)
				return B_OK;

			message->AddInt64("when", keyInfo.timestamp);
			message->AddInt32("be:old_modifiers", oldModifiers);
			message->AddInt32("modifiers", fModifiers);
			message->AddData("states", B_UINT8_TYPE, fStates, 16);

			if (fOwner->EnqueueMessage(message) != B_OK)
				delete message;

			if (isLock)
				_UpdateLEDs();
		}
	}

	// Dead key handling
	uint8 newDeadKey = 0;
	if (fActiveDeadKey == 0 || !isKeyDown)
		newDeadKey = fKeymap.ActiveDeadKey(keycode, fModifiers);

	char* string = NULL;
	char* rawString = NULL;
	int32 numBytes = 0, rawNumBytes = 0;

	ArrayDeleter<char> stringDeleter;
	if (newDeadKey == 0) {
		fKeymap.GetChars(keycode, fModifiers, fActiveDeadKey,
			&string, &numBytes);
		stringDeleter.SetTo(string);
	}

	fKeymap.GetRawChars(keycode, &rawString, &rawNumBytes);
	ArrayDeleter<char> rawStringDeleter(rawString);

	// Build key message
	BMessage* msg = new(std::nothrow) BMessage;
	if (msg == NULL)
		return B_OK;

	if (numBytes > 0)
		msg->what = isKeyDown ? B_KEY_DOWN : B_KEY_UP;
	else
		msg->what = isKeyDown ? B_UNMAPPED_KEY_DOWN : B_UNMAPPED_KEY_UP;

	msg->AddInt64("when", keyInfo.timestamp);
	msg->AddInt32("key", keycode);
	msg->AddInt32("modifiers", fModifiers);
	msg->AddData("states", B_UINT8_TYPE, fStates, 16);

	if (numBytes > 0) {
		for (int i = 0; i < numBytes; i++)
			msg->AddInt8("byte", (int8)string[i]);
		msg->AddData("bytes", B_STRING_TYPE, string, numBytes + 1);

		if (rawNumBytes <= 0) {
			rawNumBytes = 1;
			rawString = string;
		}

		if (isKeyDown && fLastKeyCode == keycode) {
			fRepeatCount++;
			msg->AddInt32("be:key_repeat", fRepeatCount);
		} else {
			fRepeatCount = 1;
		}
	}

	if (rawNumBytes > 0)
		msg->AddInt32("raw_char", (uint32)((uint8)rawString[0] & 0x7f));

	// Dead key state machine
	if (newDeadKey == 0) {
		if (isKeyDown && !modifier && fActiveDeadKey != 0) {
			fActiveDeadKey = 0;
			if (fInputMethodStarted) {
				_EnqueueInlineInputMethod(B_INPUT_METHOD_CHANGED,
					string, true, msg);
				_EnqueueInlineInputMethod(B_INPUT_METHOD_STOPPED);
				fInputMethodStarted = false;
				msg = NULL;
			}
		}
	} else if (isKeyDown
		&& _EnqueueInlineInputMethod(B_INPUT_METHOD_STARTED) == B_OK) {
		char* deadString = NULL;
		int32 deadNumBytes = 0;
		fKeymap.GetChars(keycode, fModifiers, 0, &deadString, &deadNumBytes);

		if (_EnqueueInlineInputMethod(B_INPUT_METHOD_CHANGED, deadString)
				== B_OK)
			fInputMethodStarted = true;

		fActiveDeadKey = newDeadKey;
		delete[] deadString;
	}

	if (msg != NULL && fOwner->EnqueueMessage(msg) != B_OK)
		delete msg;

	fLastKeyCode = isKeyDown ? keycode : 0;

	return B_OK;
}


void
HWKeyboardDevice::_UpdateLEDs()
{
	if (fFD < 0)
		return;

	char lockIO[3] = {0, 0, 0};
	if (fModifiers & B_NUM_LOCK)
		lockIO[0] = 1;
	if (fModifiers & B_CAPS_LOCK)
		lockIO[1] = 1;
	if (fModifiers & B_SCROLL_LOCK)
		lockIO[2] = 1;

	ioctl(fFD, KB_SET_LEDS, &lockIO, sizeof(lockIO));
}


status_t
HWKeyboardDevice::_EnqueueInlineInputMethod(int32 opcode,
	const char* string, bool confirmed, BMessage* keyDown)
{
	BMessage* message = new(std::nothrow) BMessage(B_INPUT_METHOD_EVENT);
	if (message == NULL)
		return B_NO_MEMORY;

	message->AddInt32("be:opcode", opcode);
	message->AddBool("be:inline_only", true);

	if (string != NULL)
		message->AddString("be:string", string);
	if (confirmed)
		message->AddBool("be:confirmed", true);
	if (keyDown)
		message->AddMessage("be:translated", keyDown);
	if (opcode == B_INPUT_METHOD_STARTED)
		message->AddMessenger("be:reply_to", this);

	status_t status = fOwner->EnqueueMessage(message);
	if (status != B_OK)
		delete message;

	return status;
}


// HWMouseDevice

HWMouseDevice::HWMouseDevice(InputDeviceManager* owner, const char* path,
	bool expectTouchpad)
	:
	InputDeviceBase(owner, path, B_POINTING_DEVICE),
	fDeviceRemapsButtons(false),
	fExpectTouchpad(expectTouchpad),
	fIsTouchpad(false),
	fLastButtons(0),
	fHistoryDeltaX(0.0f),
	fHistoryDeltaY(0.0f),
	fTouchpadSettingsMessage(NULL),
	fTouchpadSettingsLock("touchpad settings lock"),
	fTouchpadEventTimeout(B_INFINITE_TIMEOUT),
	fNextTransferTime(0)
{
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	for (int i = 0; i < B_MAX_MOUSE_BUTTONS; i++)
		fSettings.map.button[i] = B_MOUSE_BUTTON(i + 1);
#endif
	memset(&fLastTouchpadMovement, 0, sizeof(fLastTouchpadMovement));
}


HWMouseDevice::~HWMouseDevice()
{
	delete fTouchpadSettingsMessage;
}


void
HWMouseDevice::ScheduleSettingsUpdate(uint32 opcode, BMessage* message)
{
	if (opcode == B_SET_TOUCHPAD_SETTINGS && message != NULL) {
		BAutolock _(fTouchpadSettingsLock);
		delete fTouchpadSettingsMessage;
		fTouchpadSettingsMessage = new(std::nothrow) BMessage(*message);
	}

	InputDeviceBase::ScheduleSettingsUpdate(opcode, message);
}


char*
HWMouseDevice::BuildShortName() const
{
	BString string(Path());
	BString deviceName;
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(deviceName, slash + 1, string.Length() - slash);
	int32 index = atoi(deviceName.String()) + 1;

	int32 previousSlash = string.FindLast("/", slash);
	string.CopyInto(name, previousSlash + 1, slash - previousSlash - 1);

	if (name == "ps2")
		name = "PS/2";

	if (name.Length() <= 4)
		name.ToUpper();
	else
		name.Capitalize();

	if (string.FindFirst("touchpad") >= 0)
		name << " Touchpad ";
	else if (deviceName.FindFirst("trackpoint") >= 0)
		name = "Trackpoint ";
	else {
		if (deviceName.FindFirst("intelli") >= 0)
			name.Prepend("Extended ");
		name << " Mouse ";
	}

	name << index;
	return strdup(name.String());
}


void
HWMouseDevice::OnStart()
{
	static const bigtime_t kTransferDelay = 1000000 / 125;
	fNextTransferTime = system_time() + kTransferDelay;

	// Detect touchpad
	touchpad_specs specs;
	if (ioctl(fFD, MS_IS_TOUCHPAD, &specs, sizeof(specs)) == B_OK) {
		fIsTouchpad = true;

		touchpad_settings settings = kDefaultTouchpadSettings;

		BPath path;
		if (_GetTouchpadSettingsPath(path) == B_OK) {
			BFile settingsFile(path.Path(), B_READ_ONLY);
			BMessage settingsMsg;
			if (settingsMsg.Unflatten(&settingsFile) == B_OK) {
				_UpdateTouchpadSettings(&settingsMsg);
			} else {
				// Try reading old 28-byte format
				off_t size;
				settingsFile.Seek(0, SEEK_SET);
				if (settingsFile.GetSize(&size) == B_OK && size == 28) {
					if (settingsFile.Read(&settings, 20) == 20)
						fTouchpadMovement.SetSettings(settings);
				} else {
					fTouchpadMovement.SetSettings(settings);
				}
			}
		}

		fTouchpadMovement.SetSpecs(specs);
	}
}


void
HWMouseDevice::UpdateSettings(uint32 opcode)
{
	if (opcode == B_SET_TOUCHPAD_SETTINGS && fIsTouchpad) {
		BAutolock _(fTouchpadSettingsLock);
		if (fTouchpadSettingsMessage != NULL) {
			_UpdateTouchpadSettings(fTouchpadSettingsMessage);
			delete fTouchpadSettingsMessage;
			fTouchpadSettingsMessage = NULL;
		}
		return;
	}

	if (get_mouse_map(DeviceRef()->name, &fSettings.map) == B_OK)
		fDeviceRemapsButtons = ioctl(fFD, MS_SET_MAP, &fSettings.map) == B_OK;

	if (get_click_speed(DeviceRef()->name, &fSettings.click_speed) == B_OK) {
		if (fIsTouchpad)
			fTouchpadMovement.click_speed = fSettings.click_speed;
		ioctl(fFD, MS_SET_CLICKSPEED, &fSettings.click_speed);
	}

	if (get_mouse_speed(DeviceRef()->name, &fSettings.accel.speed) == B_OK
		&& get_mouse_acceleration(DeviceRef()->name,
			&fSettings.accel.accel_factor) == B_OK) {
		ioctl(fFD, MS_SET_ACCEL, &fSettings.accel);
	}

	if (get_mouse_type(DeviceRef()->name, &fSettings.type) == B_OK)
		ioctl(fFD, MS_SET_TYPE, &fSettings.type);
}


status_t
HWMouseDevice::ReadAndDispatch()
{
	static const bigtime_t kTransferDelay = 1000000 / 125;

	snooze_until(fNextTransferTime, B_SYSTEM_TIMEBASE);
	fNextTransferTime += kTransferDelay;

	mouse_movement movements;

	if (!fIsTouchpad) {
		if (ioctl(fFD, MS_READ, &movements, sizeof(movements)) != B_OK) {
			if (errno == B_INTERRUPTED)
				return B_INTERRUPTED;
			return B_ERROR;
		}
	} else {
		touchpad_read read;
		read.timeout = fTouchpadEventTimeout;

		status_t status = ioctl(fFD, MS_READ_TOUCHPAD, &read, sizeof(read));
		if (status < 0)
			status = errno;

		if (status == B_TIMED_OUT || status == B_BAD_DATA) {
			read.event = MS_READ_TOUCHPAD;
			read.u.touchpad = fLastTouchpadMovement;
		} else if (status != B_OK && status != B_INTERRUPTED) {
			return B_ERROR;
		} else if (status == B_INTERRUPTED) {
			return B_INTERRUPTED;
		}

		if (read.event == MS_READ_TOUCHPAD) {
			status = fTouchpadMovement.EventToMovement(&read.u.touchpad,
				&movements, fTouchpadEventTimeout);
			if (status == B_OK)
				fLastTouchpadMovement = read.u.touchpad;
		} else if (read.event == MS_READ) {
			movements = read.u.mouse;
			fTouchpadEventTimeout = -1;
		}

		if (status != B_OK)
			return B_OK;	// not an error, just no movement yet
	}

	uint32 buttons = fLastButtons ^ movements.buttons;
	uint32 remappedButtons = _RemapButtons(movements.buttons);

	int32 deltaX, deltaY;
	_ComputeAcceleration(movements, deltaX, deltaY,
		fHistoryDeltaX, fHistoryDeltaY);

	// Button events
	if (buttons != 0) {
		bool pressedButton = (buttons & movements.buttons) > 0;
		BMessage* message = _BuildMouseMessage(
			pressedButton ? B_MOUSE_DOWN : B_MOUSE_UP,
			movements.timestamp, remappedButtons, deltaX, deltaY);
		if (message != NULL) {
			if (pressedButton)
				message->AddInt32("clicks", movements.clicks);
			if (fOwner->EnqueueMessage(message) == B_OK)
				fLastButtons = movements.buttons;
			else
				delete message;
		}
	}

	// Movement events
	if (movements.xdelta != 0 || movements.ydelta != 0) {
		BMessage* message = _BuildMouseMessage(B_MOUSE_MOVED,
			movements.timestamp, remappedButtons, deltaX, deltaY);
		if (message != NULL) {
			if (fOwner->EnqueueMessage(message) != B_OK)
				delete message;
		}
	}

	// Wheel events
	if (movements.wheel_ydelta != 0 || movements.wheel_xdelta != 0) {
		BMessage* message = new(std::nothrow) BMessage(B_MOUSE_WHEEL_CHANGED);
		if (message != NULL
			&& message->AddInt64("when", movements.timestamp) == B_OK
			&& message->AddFloat("be:wheel_delta_x",
				movements.wheel_xdelta) == B_OK
			&& message->AddFloat("be:wheel_delta_y",
				movements.wheel_ydelta) == B_OK
			&& message->AddInt32("be:device_subtype",
				fIsTouchpad ? B_TOUCHPAD_POINTING_DEVICE
					: B_MOUSE_POINTING_DEVICE) == B_OK) {
			if (fOwner->EnqueueMessage(message) != B_OK)
				delete message;
		} else {
			delete message;
		}
	}

	return B_OK;
}


BMessage*
HWMouseDevice::_BuildMouseMessage(uint32 what, uint64 when, uint32 buttons,
	int32 deltaX, int32 deltaY) const
{
	BMessage* message = new(std::nothrow) BMessage(what);
	if (message == NULL)
		return NULL;

	if (message->AddInt64("when", when) < B_OK
		|| message->AddInt32("buttons", buttons) < B_OK
		|| message->AddInt32("x", deltaX) < B_OK
		|| message->AddInt32("y", deltaY) < B_OK
		|| message->AddInt32("be:device_subtype",
			fIsTouchpad ? B_TOUCHPAD_POINTING_DEVICE
				: B_MOUSE_POINTING_DEVICE) < B_OK) {
		delete message;
		return NULL;
	}

	return message;
}


void
HWMouseDevice::_ComputeAcceleration(const mouse_movement& movements,
	int32& _deltaX, int32& _deltaY, float& historyDeltaX,
	float& historyDeltaY) const
{
	float deltaX = (float)movements.xdelta * fSettings.accel.speed / 65536.0f
		+ historyDeltaX;
	float deltaY = (float)movements.ydelta * fSettings.accel.speed / 65536.0f
		+ historyDeltaY;

	double acceleration = 1;
	if (fSettings.accel.accel_factor) {
		acceleration = 1 + sqrt(deltaX * deltaX + deltaY * deltaY)
			* fSettings.accel.accel_factor / 524288.0;
	}

	deltaX *= acceleration;
	deltaY *= acceleration;

	_deltaX = (deltaX >= 0) ? (int32)floorf(deltaX) : (int32)ceilf(deltaX);
	_deltaY = (deltaY >= 0) ? (int32)floorf(deltaY) : (int32)ceilf(deltaY);

	historyDeltaX = deltaX - _deltaX;
	historyDeltaY = deltaY - _deltaY;
}


uint32
HWMouseDevice::_RemapButtons(uint32 buttons) const
{
	if (fDeviceRemapsButtons)
		return buttons;

	uint32 newButtons = 0;
	for (int32 i = 0; buttons; i++) {
		if (buttons & 0x1) {
#if defined(HAIKU_TARGET_PLATFORM_HAIKU)
			newButtons |= fSettings.map.button[i];
#else
			if (i == 0) newButtons |= fSettings.map.left;
			if (i == 1) newButtons |= fSettings.map.right;
			if (i == 2) newButtons |= fSettings.map.middle;
#endif
		}
		buttons >>= 1;
	}

	return newButtons;
}


status_t
HWMouseDevice::_GetTouchpadSettingsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;
	return path.Append(TOUCHPAD_SETTINGS_FILE);
}


status_t
HWMouseDevice::_UpdateTouchpadSettings(BMessage* message)
{
	touchpad_settings settings = kDefaultTouchpadSettings;

	message->FindBool("scroll_reverse", &settings.scroll_reverse);
	message->FindBool("scroll_twofinger", &settings.scroll_twofinger);
	message->FindBool("scroll_twofinger_horizontal",
		&settings.scroll_twofinger_horizontal);
	message->FindFloat("scroll_rightrange", &settings.scroll_rightrange);
	message->FindFloat("scroll_bottomrange", &settings.scroll_bottomrange);
	message->FindInt16("scroll_xstepsize",
		(int16*)&settings.scroll_xstepsize);
	message->FindInt16("scroll_ystepsize",
		(int16*)&settings.scroll_ystepsize);
	message->FindInt8("scroll_acceleration",
		(int8*)&settings.scroll_acceleration);
	message->FindInt8("tapgesture_sensibility",
		(int8*)&settings.tapgesture_sensibility);
	message->FindBool("scroll_twofinger_natural_scrolling",
		&settings.scroll_twofinger_natural_scrolling);
	message->FindInt8("edge_motion", (int8*)&settings.edge_motion);
	message->FindBool("finger_click", &settings.finger_click);
	message->FindBool("software_button_areas",
		&settings.software_button_areas);

	if (fIsTouchpad)
		fTouchpadMovement.SetSettings(settings);

	return B_OK;
}


// HWTabletDevice

HWTabletDevice::HWTabletDevice(InputDeviceManager* owner, const char* path)
	:
	InputDeviceBase(owner, path, B_POINTING_DEVICE),
	fLastButtons(0),
	fLastXPosition(0),
	fLastYPosition(0),
	fNextTransferTime(0)
{
}


char*
HWTabletDevice::BuildShortName() const
{
	BString string(Path());
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(name, slash + 1, string.Length() - slash);
	int32 index = atoi(name.String()) + 1;

	int32 previousSlash = string.FindLast("/", slash);
	string.CopyInto(name, previousSlash + 1, slash - previousSlash - 1);

	if (name.Length() < 4)
		name.ToUpper();
	else
		name.Capitalize();

	name << " Tablet " << index;
	return strdup(name.String());
}


void
HWTabletDevice::UpdateSettings(uint32 opcode)
{
	if (get_click_speed(DeviceRef()->name, &fSettings.click_speed) == B_OK)
		ioctl(fFD, MS_SET_CLICKSPEED, &fSettings.click_speed,
			sizeof(bigtime_t));
}


void
HWTabletDevice::OnStart()
{
	static const bigtime_t kTransferDelay = 1000000 / 125;
	fNextTransferTime = system_time() + kTransferDelay;
}


status_t
HWTabletDevice::ReadAndDispatch()
{
	static const bigtime_t kTransferDelay = 1000000 / 125;

	snooze_until(fNextTransferTime, B_SYSTEM_TIMEBASE);
	fNextTransferTime += kTransferDelay;

	tablet_movement movements;

	if (ioctl(fFD, MS_READ, &movements, sizeof(movements)) != B_OK) {
		if (errno == B_INTERRUPTED)
			return B_INTERRUPTED;
		return B_ERROR;
	}

	if (!movements.has_contact)
		return B_OK;

	movements.buttons |= (movements.switches & B_TIP_SWITCH);
	movements.buttons |= (movements.switches & B_BARREL_SWITCH) >> 1;
	bool eraser = (movements.switches & B_ERASER) != 0;

	// Button events
	uint32 buttons = fLastButtons ^ movements.buttons;
	if (buttons != 0) {
		bool pressedButton = (buttons & movements.buttons) > 0;
		BMessage* message = _BuildMouseMessage(
			pressedButton ? B_MOUSE_DOWN : B_MOUSE_UP,
			movements.timestamp, movements.buttons,
			movements.xpos, movements.ypos);
		if (message != NULL) {
			if (pressedButton)
				message->AddInt32("clicks", movements.clicks);
			if (fOwner->EnqueueMessage(message) == B_OK)
				fLastButtons = movements.buttons;
			else
				delete message;
		}
	}

	// Movement events
	if (movements.xpos != fLastXPosition
		|| movements.ypos != fLastYPosition) {
		BMessage* message = _BuildMouseMessage(B_MOUSE_MOVED,
			movements.timestamp, movements.buttons,
			movements.xpos, movements.ypos);
		if (message != NULL) {
			message->AddFloat("be:tablet_x", movements.xpos);
			message->AddFloat("be:tablet_y", movements.ypos);
			message->AddFloat("be:tablet_pressure", movements.pressure);
			message->AddInt32("be:tablet_eraser", eraser);

			if (movements.tilt_x != 0.0 || movements.tilt_y != 0.0) {
				message->AddFloat("be:tablet_tilt_x", movements.tilt_x);
				message->AddFloat("be:tablet_tilt_y", movements.tilt_y);
			}

			if (fOwner->EnqueueMessage(message) == B_OK) {
				fLastXPosition = movements.xpos;
				fLastYPosition = movements.ypos;
			} else {
				delete message;
			}
		}
	}

	// Wheel events
	if (movements.wheel_ydelta != 0 || movements.wheel_xdelta != 0) {
		BMessage* message = new(std::nothrow) BMessage(B_MOUSE_WHEEL_CHANGED);
		if (message != NULL
			&& message->AddInt64("when", movements.timestamp) == B_OK
			&& message->AddFloat("be:wheel_delta_x",
				movements.wheel_xdelta) == B_OK
			&& message->AddFloat("be:wheel_delta_y",
				movements.wheel_ydelta) == B_OK
			&& message->AddInt32("be:device_subtype",
				B_TABLET_POINTING_DEVICE) == B_OK) {
			if (fOwner->EnqueueMessage(message) != B_OK)
				delete message;
		} else {
			delete message;
		}
	}

	return B_OK;
}


BMessage*
HWTabletDevice::_BuildMouseMessage(uint32 what, uint64 when, uint32 buttons,
	float xPosition, float yPosition) const
{
	BMessage* message = new(std::nothrow) BMessage(what);
	if (message == NULL)
		return NULL;

	if (message->AddInt64("when", when) < B_OK
		|| message->AddInt32("buttons", buttons) < B_OK
		|| message->AddFloat("x", xPosition) < B_OK
		|| message->AddFloat("y", yPosition) < B_OK
		|| message->AddInt32("be:device_subtype",
			B_TABLET_POINTING_DEVICE) < B_OK) {
		delete message;
		return NULL;
	}

	return message;
}


// TouchscreenDevice

TouchscreenDevice::TouchscreenDevice(InputDeviceManager* owner,
	const char* path)
	:
	InputDeviceBase(owner, path, B_POINTING_DEVICE),
	fClickSpeed(500000),
	fButtons(0),
	fX(0), fY(0),
	fPressure(0),
	fClicks(0),
	fLastClick(-1),
	fNextTransferTime(0)
{
}


char*
TouchscreenDevice::BuildShortName() const
{
	BString string(Path());
	BString name;

	int32 slash = string.FindLast("/");
	string.CopyInto(name, slash + 1, string.Length() - slash);
	int32 index = atoi(name.String()) + 1;

	int32 previousSlash = string.FindLast("/", slash);
	string.CopyInto(name, previousSlash + 1, slash - previousSlash - 1);

	if (name.Length() < 4)
		name.ToUpper();
	else
		name.Capitalize();

	name << " Touchscreen " << index;
	return strdup(name.String());
}


void
TouchscreenDevice::UpdateSettings(uint32 opcode)
{
	if (opcode == 0 || opcode == B_CLICK_SPEED_CHANGED)
		get_click_speed(DeviceRef()->name, &fClickSpeed);
}


void
TouchscreenDevice::OnStart()
{
	static const bigtime_t kTransferDelay = 1000000 / 125;
	fNextTransferTime = system_time() + kTransferDelay;
	fClicks = 0;
	fLastClick = -1;
}


status_t
TouchscreenDevice::ReadAndDispatch()
{
	// TODO: Read from /dev/input/touchscreen/N
	// Expected protocol: multitouch slot-based events
	// For now, read tablet_movement as a single-touch fallback

	static const bigtime_t kTransferDelay = 1000000 / 125;

	snooze_until(fNextTransferTime, B_SYSTEM_TIMEBASE);
	fNextTransferTime += kTransferDelay;

	tablet_movement movements;

	if (ioctl(fFD, MS_READ, &movements, sizeof(movements)) != B_OK) {
		if (errno == B_INTERRUPTED)
			return B_INTERRUPTED;
		return B_ERROR;
	}

	if (!movements.has_contact) {
		// Finger lifted: send mouse up if buttons were pressed
		if (fButtons != 0) {
			BMessage* msg = new(std::nothrow) BMessage(B_MOUSE_UP);
			if (msg != NULL) {
				msg->AddInt64("when", movements.timestamp);
				msg->AddInt32("buttons", 0);
				msg->AddFloat("x", fX);
				msg->AddFloat("y", fY);
				if (fOwner->EnqueueMessage(msg) != B_OK)
					delete msg;
			}
			fButtons = 0;
		}
		return B_OK;
	}

	// Single-touch: treat as absolute pointer with implicit left button
	bool posChanged = (movements.xpos != fX || movements.ypos != fY);
	bigtime_t now = movements.timestamp;

	if (fButtons == 0) {
		// Touch start -> mouse down
		fButtons = 1;

		// Double-click detection
		if (now - fLastClick <= fClickSpeed)
			fClicks++;
		else
			fClicks = 1;
		fLastClick = now;

		BMessage* msg = new(std::nothrow) BMessage(B_MOUSE_DOWN);
		if (msg != NULL) {
			msg->AddInt64("when", now);
			msg->AddInt32("buttons", fButtons);
			msg->AddFloat("x", movements.xpos);
			msg->AddFloat("y", movements.ypos);
			msg->AddInt32("clicks", fClicks);
			msg->AddFloat("be:tablet_x", movements.xpos);
			msg->AddFloat("be:tablet_y", movements.ypos);
			msg->AddFloat("be:tablet_pressure", movements.pressure);
			if (fOwner->EnqueueMessage(msg) != B_OK)
				delete msg;
		}
	}

	if (posChanged) {
		BMessage* msg = new(std::nothrow) BMessage(B_MOUSE_MOVED);
		if (msg != NULL) {
			msg->AddInt64("when", now);
			msg->AddInt32("buttons", fButtons);
			msg->AddFloat("x", movements.xpos);
			msg->AddFloat("y", movements.ypos);
			msg->AddFloat("be:tablet_x", movements.xpos);
			msg->AddFloat("be:tablet_y", movements.ypos);
			msg->AddFloat("be:tablet_pressure", movements.pressure);
			if (fOwner->EnqueueMessage(msg) != B_OK)
				delete msg;
		}
	}

	fX = movements.xpos;
	fY = movements.ypos;
	fPressure = movements.pressure;

	return B_OK;
}
