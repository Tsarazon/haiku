/*
 * Copyright 2021-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */
#ifndef VIRTIO_DEVICES_H
#define VIRTIO_DEVICES_H

#include "InputDeviceCore.h"
#include "InputUtils.h"

#include <AutoDeleter.h>

struct VirtioInputPacket;

class VirtioKeyboardDevice : public InputDeviceBase {
public:
								VirtioKeyboardDevice(
									InputDeviceManager* owner,
									const char* path);
	virtual						~VirtioKeyboardDevice();

protected:
	virtual	void				OnStart();
	virtual	status_t			ReadAndDispatch();
	virtual	void				UpdateSettings(uint32 opcode);

private:
			void				_ProcessSync();
	static	bool				_IsKeyPressed(const uint8* keys,
									uint32 key);
			void				_KeyString(uint32 code, char* str,
									size_t len);

			KeymapHandler		fKeymap;
			SoftwareRepeat		fRepeat;

			uint8				fKeys[32];
			uint8				fNewKeys[32];
			uint8				fPrevSyncKeys[32];
			uint32				fModifiers;
			uint32				fNewModifiers;
			bigtime_t			fWhen;
};

class VirtioTabletDevice : public InputDeviceBase {
public:
								VirtioTabletDevice(
									InputDeviceManager* owner,
									const char* path);

protected:
	virtual	void				OnStart();
	virtual	status_t			ReadAndDispatch();
	virtual	void				UpdateSettings(uint32 opcode);

private:
			void				_ProcessSync();
	static	bool				_FillMessage(BMessage& msg,
									bigtime_t when, uint32 buttons,
									float x, float y, float pressure);

			float				fX, fY;
			float				fNewX, fNewY;
			float				fPressure, fNewPressure;
			uint32				fButtons, fNewButtons;
			int32				fWheelX, fWheelY;
			int32				fNewWheelX, fNewWheelY;
			bigtime_t			fWhen;

			int32				fClicks;
			bigtime_t			fLastClick;
			int					fLastClickBtn;
			bigtime_t			fClickSpeed;
};

#endif
