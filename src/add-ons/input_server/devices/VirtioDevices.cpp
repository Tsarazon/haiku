/*
 * Copyright 2021-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "VirtioDevices.h"

#include <new>
#include <string.h>

#include <Autolock.h>

#include <virtio_defs.h>
#include <virtio_input_driver.h>


template<typename T>
static inline void SetBitTo(T& val, int bit, bool isSet)
{
	val ^= ((isSet ? -1 : 0) ^ val) & (T(1) << bit);
}

template<typename T>
static inline bool IsBitSet(T val, int bit)
{
	return (val & (T(1) << bit)) != 0;
}


// VirtioKeyboardDevice

VirtioKeyboardDevice::VirtioKeyboardDevice(InputDeviceManager* owner,
	const char* path)
	:
	InputDeviceBase(owner, path, B_KEYBOARD_DEVICE),
	fRepeat(owner),
	fModifiers(0),
	fNewModifiers(0),
	fWhen(0)
{
	memset(fKeys, 0, sizeof(fKeys));
	memset(fNewKeys, 0, sizeof(fNewKeys));
	memset(fPrevSyncKeys, 0, sizeof(fPrevSyncKeys));
}


VirtioKeyboardDevice::~VirtioKeyboardDevice()
{
	fRepeat.StopRepeating();
}


void
VirtioKeyboardDevice::OnStart()
{
	memset(fKeys, 0, sizeof(fKeys));
	memset(fNewKeys, 0, sizeof(fNewKeys));
	memset(fPrevSyncKeys, 0, sizeof(fPrevSyncKeys));
	fModifiers = 0;
	fNewModifiers = 0;
	fRepeat.StopRepeating();
}


void
VirtioKeyboardDevice::UpdateSettings(uint32 opcode)
{
	if (opcode == 0 || opcode == B_KEY_MAP_CHANGED) {
		BAutolock lock(fKeymap.Lock());
		fKeymap.ReloadKeymap();
	}

	if (opcode == 0 || opcode == B_KEY_REPEAT_DELAY_CHANGED) {
		bigtime_t delay;
		if (get_key_repeat_delay(&delay) == B_OK)
			fRepeat.SetRepeatDelay(delay);
	}

	if (opcode == 0 || opcode == B_KEY_REPEAT_RATE_CHANGED) {
		int32 rate;
		if (get_key_repeat_rate(&rate) == B_OK)
			fRepeat.SetRepeatRate(rate);
	}
}


status_t
VirtioKeyboardDevice::ReadAndDispatch()
{
	VirtioInputPacket pkt;
	status_t res = ioctl(fFD, virtioInputRead, &pkt, sizeof(pkt));
	if (res < B_OK)
		return B_INTERRUPTED;	// retry

	switch (pkt.type) {
		case kVirtioInputEvKey:
			if (pkt.code < 256) {
				if (pkt.value != 0)
					fNewKeys[pkt.code / 8] |= (1 << (pkt.code % 8));
				else
					fNewKeys[pkt.code / 8] &= ~(1 << (pkt.code % 8));
			}
			break;

		case kVirtioInputEvSyn:
			fWhen = system_time();
			_ProcessSync();
			break;

		default:
			break;
	}

	return B_OK;
}


bool
VirtioKeyboardDevice::_IsKeyPressed(const uint8* keys, uint32 key)
{
	return key < 256 && (keys[key / 8] & (1 << (key % 8))) != 0;
}


void
VirtioKeyboardDevice::_KeyString(uint32 code, char* str, size_t len)
{
	BAutolock lock(fKeymap.Lock());

	char* chars = NULL;
	int32 numBytes = 0;
	fKeymap.GetChars(code, fNewModifiers, 0, &chars, &numBytes);

	if (len > 0 && chars != NULL) {
		uint32 i;
		for (i = 0; i < (uint32)numBytes && i < len - 1; i++)
			str[i] = chars[i];
		str[i] = '\0';
	} else if (len > 0) {
		str[0] = '\0';
	}

	delete[] chars;
}


void
VirtioKeyboardDevice::_ProcessSync()
{
	BAutolock lock(fKeymap.Lock());

	// Update modifiers
	fNewModifiers = fModifiers
		& (B_CAPS_LOCK | B_SCROLL_LOCK | B_NUM_LOCK);

	uint32 leftShift = fKeymap.KeyForModifier(B_LEFT_SHIFT_KEY);
	uint32 rightShift = fKeymap.KeyForModifier(B_RIGHT_SHIFT_KEY);
	uint32 leftCmd = fKeymap.KeyForModifier(B_LEFT_COMMAND_KEY);
	uint32 rightCmd = fKeymap.KeyForModifier(B_RIGHT_COMMAND_KEY);
	uint32 leftCtrl = fKeymap.KeyForModifier(B_LEFT_CONTROL_KEY);
	uint32 rightCtrl = fKeymap.KeyForModifier(B_RIGHT_CONTROL_KEY);
	uint32 leftOpt = fKeymap.KeyForModifier(B_LEFT_OPTION_KEY);
	uint32 rightOpt = fKeymap.KeyForModifier(B_RIGHT_OPTION_KEY);
	uint32 capsKey = fKeymap.KeyForModifier(B_CAPS_LOCK);
	uint32 scrollKey = fKeymap.KeyForModifier(B_SCROLL_LOCK);
	uint32 numKey = fKeymap.KeyForModifier(B_NUM_LOCK);
	uint32 menuKey = fKeymap.KeyForModifier(B_MENU_KEY);

	if (_IsKeyPressed(fNewKeys, leftShift))
		fNewModifiers |= B_SHIFT_KEY | B_LEFT_SHIFT_KEY;
	if (_IsKeyPressed(fNewKeys, rightShift))
		fNewModifiers |= B_SHIFT_KEY | B_RIGHT_SHIFT_KEY;
	if (_IsKeyPressed(fNewKeys, leftCmd))
		fNewModifiers |= B_COMMAND_KEY | B_LEFT_COMMAND_KEY;
	if (_IsKeyPressed(fNewKeys, rightCmd))
		fNewModifiers |= B_COMMAND_KEY | B_RIGHT_COMMAND_KEY;
	if (_IsKeyPressed(fNewKeys, leftCtrl))
		fNewModifiers |= B_CONTROL_KEY | B_LEFT_CONTROL_KEY;
	if (_IsKeyPressed(fNewKeys, rightCtrl))
		fNewModifiers |= B_CONTROL_KEY | B_RIGHT_CONTROL_KEY;
	if (_IsKeyPressed(fNewKeys, leftOpt))
		fNewModifiers |= B_OPTION_KEY | B_LEFT_OPTION_KEY;
	if (_IsKeyPressed(fNewKeys, rightOpt))
		fNewModifiers |= B_OPTION_KEY | B_RIGHT_OPTION_KEY;

	// Lock keys: only toggle on key-down edge (not pressed -> pressed)
	if (_IsKeyPressed(fNewKeys, capsKey)
		&& !_IsKeyPressed(fPrevSyncKeys, capsKey))
		fNewModifiers ^= B_CAPS_LOCK;
	if (_IsKeyPressed(fNewKeys, scrollKey)
		&& !_IsKeyPressed(fPrevSyncKeys, scrollKey))
		fNewModifiers ^= B_SCROLL_LOCK;
	if (_IsKeyPressed(fNewKeys, numKey)
		&& !_IsKeyPressed(fPrevSyncKeys, numKey))
		fNewModifiers ^= B_NUM_LOCK;

	if (_IsKeyPressed(fNewKeys, menuKey))
		fNewModifiers |= B_MENU_KEY;

	// Track key state for next sync's edge detection
	memcpy(fPrevSyncKeys, fNewKeys, sizeof(fPrevSyncKeys));

	// Emit modifiers changed
	if (fModifiers != fNewModifiers) {
		BMessage* msg = new(std::nothrow) BMessage(B_MODIFIERS_CHANGED);
		if (msg != NULL) {
			msg->AddInt64("when", fWhen);
			msg->AddInt32("modifiers", fNewModifiers);
			msg->AddInt32("be:old_modifiers", fModifiers);
			msg->AddData("states", B_UINT8_TYPE, fNewKeys, 16);

			if (fOwner->EnqueueMessage(msg) == B_OK)
				fModifiers = fNewModifiers;
			else
				delete msg;
		}
	}

	// Emit key events
	uint8 diff[32];
	for (uint32 i = 0; i < 32; i++)
		diff[i] = fKeys[i] ^ fNewKeys[i];

	for (uint32 i = 0; i < 128; i++) {
		if (!(diff[i / 8] & (1 << (i % 8))))
			continue;

		char str[5];
		_KeyString(i, str, sizeof(str));

		BMessage* msg = new(std::nothrow) BMessage();
		if (msg == NULL)
			continue;

		msg->AddInt64("when", fWhen);
		msg->AddInt32("key", i);
		msg->AddInt32("modifiers", fNewModifiers);
		msg->AddData("states", B_UINT8_TYPE, fNewKeys, 16);

		if (str[0] != '\0') {
			// Get raw char
			char* rawChars = NULL;
			int32 rawNumBytes = 0;
			fKeymap.GetRawChars(i, &rawChars, &rawNumBytes);

			char rawCh = (rawChars != NULL && rawNumBytes > 0)
				? rawChars[0] : str[0];
			delete[] rawChars;

			for (uint32 j = 0; str[j] != '\0'; j++)
				msg->AddInt8("byte", str[j]);
			msg->AddString("bytes", str);
			msg->AddInt32("raw_char", (uint8)rawCh & 0x7f);
		}

		bool isKeyDown = (fNewKeys[i / 8] & (1 << (i % 8))) != 0;

		if (isKeyDown) {
			msg->what = (str[0] != '\0') ? B_KEY_DOWN : B_UNMAPPED_KEY_DOWN;
			msg->AddInt32("be:key_repeat", 1);
			fRepeat.StartRepeating(msg);
		} else {
			msg->what = (str[0] != '\0') ? B_KEY_UP : B_UNMAPPED_KEY_UP;
			fRepeat.StopRepeating();
		}

		if (fOwner->EnqueueMessage(msg) == B_OK)
			memcpy(fKeys, fNewKeys, sizeof(fKeys));
		else
			delete msg;
	}
}


// VirtioTabletDevice

VirtioTabletDevice::VirtioTabletDevice(InputDeviceManager* owner,
	const char* path)
	:
	InputDeviceBase(owner, path, B_POINTING_DEVICE),
	fX(0.5f), fY(0.5f),
	fNewX(0.5f), fNewY(0.5f),
	fPressure(0), fNewPressure(0),
	fButtons(0), fNewButtons(0),
	fWheelX(0), fWheelY(0),
	fNewWheelX(0), fNewWheelY(0),
	fWhen(0),
	fClicks(0),
	fLastClick(-1),
	fLastClickBtn(-1),
	fClickSpeed(500000)
{
}


void
VirtioTabletDevice::OnStart()
{
	fX = fNewX = 0.5f;
	fY = fNewY = 0.5f;
	fPressure = fNewPressure = 0;
	fButtons = fNewButtons = 0;
	fWheelX = fWheelY = 0;
	fNewWheelX = fNewWheelY = 0;
	fClicks = 0;
	fLastClick = -1;
	fLastClickBtn = -1;

	get_click_speed(DeviceRef()->name, &fClickSpeed);
}


void
VirtioTabletDevice::UpdateSettings(uint32 opcode)
{
	if (opcode == 0 || opcode == B_CLICK_SPEED_CHANGED)
		get_click_speed(DeviceRef()->name, &fClickSpeed);
}


status_t
VirtioTabletDevice::ReadAndDispatch()
{
	VirtioInputPacket pkt;
	status_t res = ioctl(fFD, virtioInputRead, &pkt, sizeof(pkt));
	if (res < B_OK)
		return B_INTERRUPTED;

	switch (pkt.type) {
		case kVirtioInputEvAbs:
			if (pkt.code == kVirtioInputAbsX)
				fNewX = float(pkt.value) / 32768.0f;
			else if (pkt.code == kVirtioInputAbsY)
				fNewY = float(pkt.value) / 32768.0f;
			break;

		case kVirtioInputEvRel:
			if (pkt.code == kVirtioInputRelWheel)
				fNewWheelY -= pkt.value;
			break;

		case kVirtioInputEvKey:
			if (pkt.code == kVirtioInputBtnLeft)
				SetBitTo(fNewButtons, 0, pkt.value != 0);
			else if (pkt.code == kVirtioInputBtnRight)
				SetBitTo(fNewButtons, 1, pkt.value != 0);
			else if (pkt.code == kVirtioInputBtnMiddle)
				SetBitTo(fNewButtons, 2, pkt.value != 0);
			break;

		case kVirtioInputEvSyn:
			fWhen = system_time();
			_ProcessSync();
			break;

		default:
			break;
	}

	return B_OK;
}


void
VirtioTabletDevice::_ProcessSync()
{
	// Position update
	if (fX != fNewX || fY != fNewY || fPressure != fNewPressure) {
		fX = fNewX;
		fY = fNewY;
		fPressure = fNewPressure;

		BMessage* msg = new(std::nothrow) BMessage(B_MOUSE_MOVED);
		if (msg != NULL && _FillMessage(*msg, fWhen, fButtons, fX, fY,
				fPressure)) {
			if (fOwner->EnqueueMessage(msg) != B_OK)
				delete msg;
		} else {
			delete msg;
		}
	}

	// Button updates
	for (int i = 0; i < 32; i++) {
		if (IsBitSet(fButtons, i) != IsBitSet(fNewButtons, i)) {
			fButtons ^= (1 << i);

			BMessage* msg = new(std::nothrow) BMessage();
			if (msg == NULL)
				continue;

			if (!_FillMessage(*msg, fWhen, fButtons, fX, fY, fPressure)) {
				delete msg;
				continue;
			}

			if (IsBitSet(fButtons, i)) {
				msg->what = B_MOUSE_DOWN;
				if (i == fLastClickBtn
					&& fWhen - fLastClick <= fClickSpeed)
					fClicks++;
				else
					fClicks = 1;
				fLastClickBtn = i;
				fLastClick = fWhen;
				msg->AddInt32("clicks", fClicks);
			} else {
				msg->what = B_MOUSE_UP;
			}

			if (fOwner->EnqueueMessage(msg) != B_OK)
				delete msg;
		}
	}

	// Wheel update
	if (fWheelX != fNewWheelX || fWheelY != fNewWheelY) {
		BMessage* msg = new(std::nothrow) BMessage(B_MOUSE_WHEEL_CHANGED);
		if (msg != NULL
			&& msg->AddInt64("when", fWhen) == B_OK
			&& msg->AddFloat("be:wheel_delta_x",
				fNewWheelX - fWheelX) == B_OK
			&& msg->AddFloat("be:wheel_delta_y",
				fNewWheelY - fWheelY) == B_OK) {
			fWheelX = fNewWheelX;
			fWheelY = fNewWheelY;
			if (fOwner->EnqueueMessage(msg) != B_OK)
				delete msg;
		} else {
			delete msg;
		}
	}
}


bool
VirtioTabletDevice::_FillMessage(BMessage& msg, bigtime_t when,
	uint32 buttons, float x, float y, float pressure)
{
	if (msg.AddInt64("when", when) < B_OK
		|| msg.AddInt32("buttons", buttons) < B_OK
		|| msg.AddFloat("x", x) < B_OK
		|| msg.AddFloat("y", y) < B_OK)
		return false;

	msg.AddFloat("be:tablet_x", x);
	msg.AddFloat("be:tablet_y", y);
	msg.AddFloat("be:tablet_pressure", pressure);
	return true;
}
