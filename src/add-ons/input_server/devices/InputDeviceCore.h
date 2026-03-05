/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */
#ifndef INPUT_DEVICE_CORE_H
#define INPUT_DEVICE_CORE_H

#include <InputServerDevice.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
#include <String.h>

class InputDeviceManager;
class TeamMonitorWindow;

class InputDeviceBase {
public:
								InputDeviceBase(InputDeviceManager* owner,
									const char* path,
									input_device_type type);
	virtual						~InputDeviceBase();

			status_t			Start();
			void				Stop();

	virtual	void				ScheduleSettingsUpdate(uint32 opcode = 0,
									BMessage* message = NULL);

			const char*			Path() const { return fPath; }
			input_device_ref*	DeviceRef() { return &fDeviceRef; }
			input_device_type	Type() const { return fDeviceRef.type; }
			bool				IsActive() const { return fActive; }

protected:
	virtual	char*				BuildShortName() const;
	virtual	uint32				ThreadPriority() const;

	// Called once after fd is opened, before entering the read loop.
	virtual	void				OnStart() {}

	// Main read-and-dispatch: read one event from fFD, enqueue BMessages.
	// Return B_OK to continue, B_INTERRUPTED to retry,
	// B_ERROR (or other) to exit the control thread.
	virtual	status_t			ReadAndDispatch() = 0;

	// Called when settings update is triggered from the main thread.
	virtual	void				UpdateSettings(uint32 opcode) {}

	// Called on control thread exit due to error.
	// Default implementation removes the device from the manager.
	virtual	void				ControlThreadCleanup();

			InputDeviceManager*	fOwner;
			int					fFD;

private:
	static	int32				_ControlThreadEntry(void* arg);
			int32				_ControlThread();

			input_device_ref	fDeviceRef;
			char				fPath[B_PATH_NAME_LENGTH];
			thread_id			fThread;
	volatile bool				fActive;
	volatile bool				fNeedsSettingsUpdate;
	volatile uint32				fSettingsCommand;
};

enum DeviceCategory {
	kCategoryHWKeyboard,
	kCategoryHWMouse,
	kCategoryHWTouchpad,
	kCategoryHWTablet,
	kCategoryVirtio,
	kCategoryTouchscreen,
	kCategoryUnknown
};

struct MonitoredDirectory {
	const char*		path;
	DeviceCategory	category;
};

class InputDeviceManager : public BInputServerDevice {
public:
								InputDeviceManager();
	virtual						~InputDeviceManager();

	virtual	status_t			InitCheck();
	virtual	status_t			SystemShuttingDown();

	virtual	status_t			Start(const char* name, void* cookie);
	virtual	status_t			Stop(const char* name, void* cookie);
	virtual	status_t			Control(const char* name, void* cookie,
									uint32 command, BMessage* message);

			// Called by InputDeviceBase on control thread error.
			status_t			RemoveDevice(const char* path);

			// For keyboard devices to access.
			TeamMonitorWindow*	GetTeamMonitorWindow();

private:
			status_t			_HandleMonitor(BMessage* message);
			void				_RecursiveScan(const char* directory,
									DeviceCategory category);

			InputDeviceBase*	_FindDevice(const char* path) const;
			status_t			_AddDevice(const char* path,
									DeviceCategory category);
			status_t			_RemoveDevice(const char* path);

			DeviceCategory		_CategoryForPath(const char* path) const;

			InputDeviceBase*	_CreateDevice(const char* path,
									DeviceCategory category);

	static	DeviceCategory		_DetectVirtioType(const char* path);

			BObjectList<InputDeviceBase, true>	fDevices;
			BLocker				fDeviceListLock;
			TeamMonitorWindow*	fTeamMonitorWindow;
};

extern "C" BInputServerDevice* instantiate_input_device();

#endif
