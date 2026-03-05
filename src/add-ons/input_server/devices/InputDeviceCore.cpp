/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "InputDeviceCore.h"
#include "HWDevices.h"
#include "VirtioDevices.h"
#include "TeamMonitorWindow.h"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <NodeMonitor.h>
#include <Path.h>

#include <keyboard_mouse_driver.h>


// InputDeviceBase

static const uint32 kDefaultThreadPriority = B_FIRST_REAL_TIME_PRIORITY + 4;


static char*
build_short_name_from_path(const char* longName, const char* suffix)
{
	BString string(longName);
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

	name << " " << suffix << " " << index;
	return strdup(name.String());
}


InputDeviceBase::InputDeviceBase(InputDeviceManager* owner,
	const char* path, input_device_type type)
	:
	fOwner(owner),
	fFD(-1),
	fThread(-1),
	fActive(false),
	fNeedsSettingsUpdate(false),
	fSettingsCommand(0)
{
	strlcpy(fPath, path, B_PATH_NAME_LENGTH);
	fDeviceRef.name = BuildShortName();
	fDeviceRef.type = type;
	fDeviceRef.cookie = this;
}


InputDeviceBase::~InputDeviceBase()
{
	if (fActive)
		Stop();

	free(fDeviceRef.name);
}


status_t
InputDeviceBase::Start()
{
	fFD = open(fPath, O_RDWR);
	if (fFD < 0)
		fFD = -errno;

	char threadName[B_OS_NAME_LENGTH];
	snprintf(threadName, B_OS_NAME_LENGTH, "%s watcher", fDeviceRef.name);

	fThread = spawn_thread(_ControlThreadEntry, threadName,
		ThreadPriority(), this);

	if (fThread < B_OK)
		return fThread;

	fActive = true;
	resume_thread(fThread);

	return fFD >= 0 ? B_OK : B_ERROR;
}


void
InputDeviceBase::Stop()
{
	fActive = false;

	if (fFD >= 0) {
		close(fFD);
		fFD = -1;
	}

	if (fThread >= 0) {
		suspend_thread(fThread);
		resume_thread(fThread);
		status_t dummy;
		wait_for_thread(fThread, &dummy);
		fThread = -1;
	}
}


void
InputDeviceBase::ScheduleSettingsUpdate(uint32 opcode, BMessage* /*message*/)
{
	if (fThread < 0)
		return;

	fSettingsCommand = opcode;
	fNeedsSettingsUpdate = true;
}


char*
InputDeviceBase::BuildShortName() const
{
	const char* suffix = "Device";
	switch (fDeviceRef.type) {
		case B_KEYBOARD_DEVICE:
			suffix = "Keyboard";
			break;
		case B_POINTING_DEVICE:
			suffix = "Pointer";
			break;
		default:
			break;
	}
	return build_short_name_from_path(fPath, suffix);
}


uint32
InputDeviceBase::ThreadPriority() const
{
	return kDefaultThreadPriority;
}


void
InputDeviceBase::ControlThreadCleanup()
{
	if (fActive) {
		fThread = -1;
		fOwner->RemoveDevice(fPath);
	}
}


int32
InputDeviceBase::_ControlThreadEntry(void* arg)
{
	InputDeviceBase* device = static_cast<InputDeviceBase*>(arg);
	return device->_ControlThread();
}


int32
InputDeviceBase::_ControlThread()
{
	if (fFD < 0) {
		debug_printf("InputDeviceBase: failed to open %s: %s\n",
			fPath, strerror(-fFD));
		ControlThreadCleanup();
		return B_ERROR;
	}

	OnStart();
	UpdateSettings(0);

	while (fActive) {
		if (fNeedsSettingsUpdate) {
			UpdateSettings(fSettingsCommand);
			fNeedsSettingsUpdate = false;
		}

		status_t status = ReadAndDispatch();

		if (status == B_INTERRUPTED)
			continue;
		if (status == B_BUSY) {
			snooze(100000);
			continue;
		}
		if (status != B_OK) {
			ControlThreadCleanup();
			return 0;
		}
	}

	return 0;
}


// InputDeviceManager

static const MonitoredDirectory kMonitoredDirs[] = {
	{ "/dev/input/keyboard",	kCategoryHWKeyboard },
	{ "/dev/input/mouse",		kCategoryHWMouse },
	{ "/dev/input/touchpad",	kCategoryHWTouchpad },
	{ "/dev/input/tablet",		kCategoryHWTablet },
	{ "/dev/input/virtio",		kCategoryVirtio },
	{ "/dev/input/touchscreen",	kCategoryTouchscreen },
};

static const size_t kMonitoredDirCount
	= sizeof(kMonitoredDirs) / sizeof(kMonitoredDirs[0]);


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) InputDeviceManager();
}


InputDeviceManager::InputDeviceManager()
	:
	fDevices(8),
	fDeviceListLock("InputDeviceManager lock"),
	fTeamMonitorWindow(NULL)
{
	for (size_t i = 0; i < kMonitoredDirCount; i++) {
		StartMonitoringDevice(kMonitoredDirs[i].path);
		_RecursiveScan(kMonitoredDirs[i].path, kMonitoredDirs[i].category);
	}
}


InputDeviceManager::~InputDeviceManager()
{
	if (fTeamMonitorWindow) {
		fTeamMonitorWindow->PostMessage(B_QUIT_REQUESTED);
		fTeamMonitorWindow = NULL;
	}

	for (size_t i = 0; i < kMonitoredDirCount; i++)
		StopMonitoringDevice(kMonitoredDirs[i].path);

	fDevices.MakeEmpty();
}


status_t
InputDeviceManager::InitCheck()
{
	return BInputServerDevice::InitCheck();
}


status_t
InputDeviceManager::SystemShuttingDown()
{
	if (fTeamMonitorWindow)
		fTeamMonitorWindow->PostMessage(kMsgSystemShuttingDown);

	return B_OK;
}


status_t
InputDeviceManager::Start(const char* name, void* cookie)
{
	InputDeviceBase* device = static_cast<InputDeviceBase*>(cookie);
	return device->Start();
}


status_t
InputDeviceManager::Stop(const char* name, void* cookie)
{
	InputDeviceBase* device = static_cast<InputDeviceBase*>(cookie);
	device->Stop();
	return B_OK;
}


status_t
InputDeviceManager::Control(const char* name, void* cookie,
	uint32 command, BMessage* message)
{
	if (command == B_NODE_MONITOR)
		return _HandleMonitor(message);

	InputDeviceBase* device = static_cast<InputDeviceBase*>(cookie);

	// Keyboard settings
	if (command >= B_KEY_MAP_CHANGED
		&& command <= B_KEY_REPEAT_RATE_CHANGED) {
		device->ScheduleSettingsUpdate(command);
		return B_OK;
	}

	// Mouse settings (includes B_CLICK_SPEED_CHANGED)
	if (command >= B_MOUSE_TYPE_CHANGED
		&& command <= B_MOUSE_ACCELERATION_CHANGED) {
		device->ScheduleSettingsUpdate(command);
		return B_OK;
	}

	// Touchpad settings
	if (command == B_SET_TOUCHPAD_SETTINGS) {
		device->ScheduleSettingsUpdate(command, message);
		return B_OK;
	}

	return B_OK;
}


TeamMonitorWindow*
InputDeviceManager::GetTeamMonitorWindow()
{
	if (fTeamMonitorWindow == NULL)
		fTeamMonitorWindow = new(std::nothrow) TeamMonitorWindow();

	return fTeamMonitorWindow;
}


status_t
InputDeviceManager::RemoveDevice(const char* path)
{
	return _RemoveDevice(path);
}


status_t
InputDeviceManager::_HandleMonitor(BMessage* message)
{
	const char* path;
	int32 opcode;
	if (message->FindInt32("opcode", &opcode) != B_OK
		|| (opcode != B_ENTRY_CREATED && opcode != B_ENTRY_REMOVED)
		|| message->FindString("path", &path) != B_OK)
		return B_BAD_VALUE;

	if (opcode == B_ENTRY_CREATED) {
		DeviceCategory category = _CategoryForPath(path);
		return _AddDevice(path, category);
	}

	// Don't handle B_ENTRY_REMOVED here.
	// Let the control thread detect the error and clean up.
	return B_OK;
}


DeviceCategory
InputDeviceManager::_CategoryForPath(const char* path) const
{
	BString p(path);

	for (size_t i = 0; i < kMonitoredDirCount; i++) {
		if (p.StartsWith(kMonitoredDirs[i].path))
			return kMonitoredDirs[i].category;
	}

	return kCategoryUnknown;
}


void
InputDeviceManager::_RecursiveScan(const char* directory,
	DeviceCategory category)
{
	BEntry entry;
	BDirectory dir(directory);

	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);

		if (!strcmp(path.Leaf(), "serial"))
			continue;

		if (entry.IsDirectory())
			_RecursiveScan(path.Path(), category);
		else
			_AddDevice(path.Path(), category);
	}
}


InputDeviceBase*
InputDeviceManager::_FindDevice(const char* path) const
{
	for (int32 i = fDevices.CountItems() - 1; i >= 0; i--) {
		InputDeviceBase* device = fDevices.ItemAt(i);
		if (strcmp(device->Path(), path) == 0)
			return device;
	}

	return NULL;
}


status_t
InputDeviceManager::_AddDevice(const char* path, DeviceCategory category)
{
	BAutolock _(fDeviceListLock);

	_RemoveDevice(path);

	InputDeviceBase* device = _CreateDevice(path, category);
	if (device == NULL)
		return B_NO_MEMORY;

	fDevices.AddItem(device);

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	return RegisterDevices(devices);
}


status_t
InputDeviceManager::_RemoveDevice(const char* path)
{
	BAutolock _(fDeviceListLock);

	InputDeviceBase* device = _FindDevice(path);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	input_device_ref* devices[2];
	devices[0] = device->DeviceRef();
	devices[1] = NULL;

	UnregisterDevices(devices);
	fDevices.RemoveItem(device);

	return B_OK;
}


InputDeviceBase*
InputDeviceManager::_CreateDevice(const char* path, DeviceCategory category)
{
	switch (category) {
		case kCategoryHWKeyboard:
			return new(std::nothrow) HWKeyboardDevice(this, path);

		case kCategoryHWMouse:
			return new(std::nothrow) HWMouseDevice(this, path, false);

		case kCategoryHWTouchpad:
			return new(std::nothrow) HWMouseDevice(this, path, true);

		case kCategoryHWTablet:
			return new(std::nothrow) HWTabletDevice(this, path);

		case kCategoryVirtio: {
			DeviceCategory virtioType = _DetectVirtioType(path);
			if (virtioType == kCategoryHWKeyboard)
				return new(std::nothrow) VirtioKeyboardDevice(this, path);
			else
				return new(std::nothrow) VirtioTabletDevice(this, path);
		}

		case kCategoryTouchscreen:
			return new(std::nothrow) TouchscreenDevice(this, path);

		default:
			return NULL;
	}
}


DeviceCategory
InputDeviceManager::_DetectVirtioType(const char* path)
{
	// Try to detect virtio device type by probing.
	// Open the device and try a tablet-specific ioctl;
	// if that fails, assume keyboard.
	int fd = open(path, O_RDWR);
	if (fd < 0)
		return kCategoryHWKeyboard;

	// For now, use a simple heuristic based on path naming.
	// Virtio devices under /dev/input/virtio/N/raw:
	// Even indices are tablets, odd are keyboards.
	// This matches QEMU's typical device ordering.
	// TODO: proper detection via device capability query
	BString p(path);
	close(fd);

	// Extract device index from path like /dev/input/virtio/0/raw
	int32 virtioPos = p.FindFirst("/virtio/");
	if (virtioPos >= 0) {
		int32 numStart = virtioPos + 8;
		int32 numEnd = p.FindFirst("/", numStart);
		if (numEnd > numStart) {
			BString numStr;
			p.CopyInto(numStr, numStart, numEnd - numStart);
			int index = atoi(numStr.String());
			if (index % 2 == 0)
				return kCategoryHWTablet;	// tablet/pointer
			else
				return kCategoryHWKeyboard;	// keyboard
		}
	}

	return kCategoryHWKeyboard;
}
