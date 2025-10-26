/*
 * Copyright 2002-2013 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "InputServer.h"
#include "InputServerTypes.h"
#include "BottomlineWindow.h"
#include "MethodReplicant.h"

#include <driver_settings.h>
#include <keyboard_mouse_driver.h>
#include <safemode_defs.h>
#include <syscalls.h>

#include <AppServerLink.h>
#include <MessagePrivate.h>
#include <ObjectListPrivate.h>
#include <RosterPrivate.h>

#include <Autolock.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <stdio.h>
#include <strings.h>

#include "SystemKeymap.h"
#include <ServerProtocol.h>

using std::nothrow;

InputServer* gInputServer;
BList InputServer::gInputFilterList;
BLocker InputServer::gInputFilterListLocker("is_filter_queue_sem");
BList InputServer::gInputMethodList;
BLocker InputServer::gInputMethodListLocker("is_method_queue_sem");
KeymapMethod InputServer::gKeymapMethod;

extern "C" _EXPORT BView* instantiate_deskbar_item();

// #pragma mark - InputDeviceListItem

InputDeviceListItem::InputDeviceListItem(BInputServerDevice& serverDevice,
		const input_device_ref& device)
	: fServerDevice(&serverDevice), fDevice(), fRunning(false)
{
	fDevice.name = strdup(device.name);
	fDevice.type = device.type;
	fDevice.cookie = device.cookie;
}

InputDeviceListItem::~InputDeviceListItem()
{
	free(fDevice.name);
}

void
InputDeviceListItem::Start()
{
	PRINT(("  Starting: %s\n", fDevice.name));
	status_t err = fServerDevice->Start(fDevice.name, fDevice.cookie);
	if (err != B_OK)
		PRINTERR(("      error: %s (%" B_PRIx32 ")\n", strerror(err), err));
	fRunning = err == B_OK;
}

void
InputDeviceListItem::Stop()
{
	PRINT(("  Stopping: %s\n", fDevice.name));
	fServerDevice->Stop(fDevice.name, fDevice.cookie);
	fRunning = false;
}

void
InputDeviceListItem::Control(uint32 code, BMessage* message)
{
	fServerDevice->Control(fDevice.name, fDevice.cookie, code, message);
}

bool
InputDeviceListItem::HasName(const char* name) const
{
	return name != NULL && strcmp(name, fDevice.name) == 0;
}

bool
InputDeviceListItem::HasType(input_device_type type) const
{
	return type == fDevice.type;
}

bool
InputDeviceListItem::Matches(const char* name, input_device_type type) const
{
	return name != NULL ? HasName(name) : HasType(type);
}

// #pragma mark - InputServer

InputServer::InputServer()
	: BApplication(INPUTSERVER_SIGNATURE),
	fKeyboardID(0),
	fInputDeviceListLocker("input server device list"),
	fKeyboardSettings(),
	fMouseSettings(),
	fChars(NULL),
	fScreen(B_MAIN_SCREEN_ID),
	fEventQueueLock("input server event queue"),
	fReplicantMessenger(NULL),
	fInputMethodWindow(NULL),
	fInputMethodAware(false),
	fCursorSem(-1),
	fAppServerPort(-1),
	fAppServerTeam(-1),
	fCursorArea(-1)
{
	CALLED();
	gInputServer = this;

	set_thread_priority(find_thread(NULL), B_URGENT_DISPLAY_PRIORITY);

	_StartEventLoop();
	_InitKeyboardMouseStates();

	fAddOnManager = new(std::nothrow) ::AddOnManager();
	if (fAddOnManager != NULL) {
		fAddOnManager->LoadState();
		fAddOnManager->Run();
	}

	BMessenger messenger(this);
	BRoster().StartWatching(messenger, B_REQUEST_LAUNCHED);
}

InputServer::~InputServer()
{
	CALLED();
	if (fAddOnManager != NULL && fAddOnManager->Lock())
		fAddOnManager->Quit();

	_ReleaseInput(NULL);
}

void
InputServer::ArgvReceived(int32 argc, char** argv)
{
	CALLED();
	if (argc == 2 && strcmp(argv[1], "-q") == 0) {
		PRINT(("InputServer::ArgvReceived - Restarting ...\n"));
		PostMessage(B_QUIT_REQUESTED);
	}
}

void
InputServer::_InitKeyboardMouseStates()
{
	CALLED();
	fFrame = fScreen.Frame();
	if (fFrame == BRect(0, 0, 0, 0))
		fFrame = BRect(0, 0, 799, 599);
	
	fMousePos = BPoint((int32)((fFrame.right + 1) / 2),
		(int32)((fFrame.bottom + 1) / 2));

	memset(&fKeyInfo, 0, sizeof(fKeyInfo));

	if (_LoadKeymap() != B_OK)
		_LoadSystemKeymap();

	BMessage msg(B_MOUSE_MOVED);
	HandleSetMousePosition(&msg, &msg);

	fActiveMethod = &gKeymapMethod;
}

status_t
InputServer::_LoadKeymap()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_BAD_VALUE;

	path.Append("Key_map");

	BFile file(path.Path(), B_READ_ONLY);
	status_t err = file.InitCheck();
	if (err != B_OK)
		return err;

	if (file.Read(&fKeys, sizeof(fKeys)) < (ssize_t)sizeof(fKeys))
		return B_BAD_VALUE;

	for (uint32 i = 0; i < sizeof(fKeys) / 4; i++)
		((uint32*)&fKeys)[i] = B_BENDIAN_TO_HOST_INT32(((uint32*)&fKeys)[i]);

	if (file.Read(&fCharsSize, sizeof(uint32)) < (ssize_t)sizeof(uint32))
		return B_BAD_VALUE;

	fCharsSize = B_BENDIAN_TO_HOST_INT32(fCharsSize);
	if (fCharsSize <= 0)
		return B_BAD_VALUE;

	delete[] fChars;
	fChars = new(nothrow) char[fCharsSize];
	if (fChars == NULL)
		return B_NO_MEMORY;

	if (file.Read(fChars, fCharsSize) != (signed)fCharsSize)
		return B_BAD_VALUE;

	return B_OK;
}

status_t
InputServer::_LoadSystemKeymap()
{
	delete[] fChars;
	fKeys = kSystemKeymap;
	fCharsSize = kSystemKeyCharsSize;
	fChars = new(nothrow) char[fCharsSize];
	if (fChars == NULL)
		return B_NO_MEMORY;

	memcpy(fChars, kSystemKeyChars, fCharsSize);

	// TODO: why are we doing this?
	return _SaveKeymap(true);
}

status_t
InputServer::_SaveKeymap(bool isDefault)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_BAD_VALUE;

	path.Append("Key_map");

	BFile file;
	status_t err = file.SetTo(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK) {
		PRINTERR(("error %s\n", strerror(err)));
		return err;
	}

	for (uint32 i = 0; i < sizeof(fKeys) / sizeof(uint32); i++)
		((uint32*)&fKeys)[i] = B_HOST_TO_BENDIAN_INT32(((uint32*)&fKeys)[i]);

	if ((err = file.Write(&fKeys, sizeof(fKeys))) < (ssize_t)sizeof(fKeys))
		return err;

	for (uint32 i = 0; i < sizeof(fKeys) / sizeof(uint32); i++)
		((uint32*)&fKeys)[i] = B_BENDIAN_TO_HOST_INT32(((uint32*)&fKeys)[i]);

	uint32 size = B_HOST_TO_BENDIAN_INT32(fCharsSize);
	if ((err = file.Write(&size, sizeof(uint32))) < (ssize_t)sizeof(uint32))
		return B_BAD_VALUE;

	if ((err = file.Write(fChars, fCharsSize)) < (ssize_t)fCharsSize)
		return err;

	if (isDefault) {
		const BString systemKeymapName(kSystemKeymapName);
		file.WriteAttrString("keymap:name", &systemKeymapName);
	}

	return B_OK;
}

bool
InputServer::QuitRequested()
{
	CALLED();
	if (!BApplication::QuitRequested())
		return false;

	PostMessage(SYSTEM_SHUTTING_DOWN);

	bool shutdown = false;
	CurrentMessage()->FindBool("_shutdown_", &shutdown);

	if (shutdown) {
		return false;
	} else {
		fAddOnManager->SaveState();
		delete_port(fEventLooperPort);
		fEventLooperPort = -1;
		return true;
	}
}

void
InputServer::ReadyToRun()
{
	CALLED();
	BPrivate::AppServerLink link;
	link.StartMessage(AS_REGISTER_INPUT_SERVER);
	link.Flush();
}

status_t
InputServer::_AcquireInput(BMessage& message, BMessage& reply)
{
	area_id area;
	if (message.FindInt32("cursor area", &area) == B_OK) {
		fCursorBuffer = NULL;
		fCursorSem = create_sem(0, "cursor semaphore");
		if (fCursorSem >= B_OK) {
			fCursorArea = clone_area("input server cursor", (void**)&fCursorBuffer,
				B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, area);
		}
	}

	if (message.FindInt32("remote team", &fAppServerTeam) != B_OK)
		fAppServerTeam = -1;

	fAppServerPort = create_port(200, "input server target");
	if (fAppServerPort < B_OK) {
		_ReleaseInput(&message);
		return fAppServerPort;
	}

	reply.AddBool("has keyboard", true);
	reply.AddBool("has mouse", true);
	reply.AddInt32("event port", fAppServerPort);

	if (fCursorBuffer != NULL)
		reply.AddInt32("cursor semaphore", fCursorSem);

	return B_OK;
}

void
InputServer::_ReleaseInput(BMessage* /*message*/)
{
	if (fCursorBuffer != NULL) {
		fCursorBuffer = NULL;
		delete_sem(fCursorSem);
		delete_area(fCursorArea);
		fCursorSem = -1;
		fCursorArea = -1;
	}
	delete_port(fAppServerPort);
}

void
InputServer::MessageReceived(BMessage* message)
{
	CALLED();

	BMessage reply;
	status_t status = B_OK;

	PRINT(("%s what:%c%c%c%c\n", __PRETTY_FUNCTION__, (char)(message->what >> 24),
		(char)(message->what >> 16), (char)(message->what >> 8), (char)message->what));

	switch (message->what) {
		case IS_SET_METHOD:
			HandleSetMethod(message);
			break;
		case IS_GET_MOUSE_TYPE:
		case IS_SET_MOUSE_TYPE:
			status = HandleGetSetMouseType(message, &reply);
			break;
		case IS_GET_MOUSE_ACCELERATION:
		case IS_SET_MOUSE_ACCELERATION:
			status = HandleGetSetMouseAcceleration(message, &reply);
			break;
		case IS_GET_KEY_REPEAT_DELAY:
		case IS_SET_KEY_REPEAT_DELAY:
			status = HandleGetSetKeyRepeatDelay(message, &reply);
			break;
		case IS_GET_KEY_INFO:
			status = HandleGetKeyInfo(message, &reply);
			break;
		case IS_GET_MODIFIERS:
			status = HandleGetModifiers(message, &reply);
			break;
		case IS_GET_MODIFIER_KEY:
			status = HandleGetModifierKey(message, &reply);
			break;
		case IS_SET_MODIFIER_KEY:
			status = HandleSetModifierKey(message, &reply);
			break;
		case IS_SET_KEYBOARD_LOCKS:
			status = HandleSetKeyboardLocks(message, &reply);
			break;
		case IS_GET_MOUSE_SPEED:
		case IS_SET_MOUSE_SPEED:
			status = HandleGetSetMouseSpeed(message, &reply);
			break;
		case IS_SET_MOUSE_POSITION:
			status = HandleSetMousePosition(message, &reply);
			break;
		case IS_GET_MOUSE_MAP:
		case IS_SET_MOUSE_MAP:
			status = HandleGetSetMouseMap(message, &reply);
			break;
		case IS_GET_KEYBOARD_ID:
		case IS_SET_KEYBOARD_ID:
			status = HandleGetSetKeyboardID(message, &reply);
			break;
		case IS_GET_CLICK_SPEED:
		case IS_SET_CLICK_SPEED:
			status = HandleGetSetClickSpeed(message, &reply);
			break;
		case IS_GET_KEY_REPEAT_RATE:
		case IS_SET_KEY_REPEAT_RATE:
			status = HandleGetSetKeyRepeatRate(message, &reply);
			break;
		case IS_GET_KEY_MAP:
		case IS_RESTORE_KEY_MAP:
			status = HandleGetSetKeyMap(message, &reply);
			break;
		case IS_FOCUS_IM_AWARE_VIEW:
		case IS_UNFOCUS_IM_AWARE_VIEW:
			status = HandleFocusUnfocusIMAwareView(message, &reply);
			break;
		case IS_ACQUIRE_INPUT:
			status = _AcquireInput(*message, reply);
			break;
		case IS_RELEASE_INPUT:
			_ReleaseInput(message);
			return;
		case IS_SCREEN_BOUNDS_UPDATED:
		{
			BRect frame;
			if (message->FindRect("screen_bounds", &frame) != B_OK)
				frame = fScreen.Frame();

			if (frame != fFrame) {
				BPoint pos(fMousePos.x * frame.Width() / fFrame.Width(),
					fMousePos.y * frame.Height() / fFrame.Height());
				fFrame = frame;

				BMessage set;
				set.AddPoint("where", pos);
				HandleSetMousePosition(&set, NULL);
			}
			break;
		}
		case IS_FIND_DEVICES:
		case IS_WATCH_DEVICES:
		case IS_IS_DEVICE_RUNNING:
		case IS_START_DEVICE:
		case IS_STOP_DEVICE:
		case IS_CONTROL_DEVICES:
		case SYSTEM_SHUTTING_DOWN:
		case IS_METHOD_REGISTER:
			fAddOnManager->PostMessage(message);
			return;
		case IS_SAVE_SETTINGS:
			fKeyboardSettings.Save();
			fMouseSettings.SaveSettings();
			return;
		case IS_SAVE_KEYMAP:
			_SaveKeymap();
			return;
		case B_SOME_APP_LAUNCHED:
			// TODO: what's this for?
			return;
		case kMsgAppServerRestarted:
		{
			BApplication::MessageReceived(message);
			BPrivate::AppServerLink link;
			link.StartMessage(AS_REGISTER_INPUT_SERVER);
			link.Flush();
			return;
		}
		default:
			return;
	}

	reply.AddInt32("status", status);
	message->SendReply(&reply);
}

void
InputServer::HandleSetMethod(BMessage* message)
{
	CALLED();
	int32 cookie;
	if (message->FindInt32("cookie", &cookie) != B_OK)
		return;

	if (cookie == gKeymapMethod.fOwner->Cookie()) {
		SetActiveMethod(&gKeymapMethod);
	} else {
		AutoLocker lock(gInputMethodListLocker);
		if (!lock.IsLocked())
			return;

		for (int32 i = 0; i < gInputMethodList.CountItems(); i++) {
			auto method = static_cast<BInputServerMethod*>(
				gInputMethodList.ItemAt(i));
			if (method->fOwner->Cookie() == cookie) {
				PRINT(("%s cookie %" B_PRId32 "\n", __PRETTY_FUNCTION__, cookie));
				SetActiveMethod(method);
				break;
			}
		}
	}
}

status_t
InputServer::HandleGetSetKeyRepeatDelay(BMessage* message, BMessage* reply)
{
	bigtime_t delay;
	if (message->FindInt64("delay", &delay) == B_OK) {
		fKeyboardSettings.SetKeyboardRepeatDelay(delay);
		be_app_messenger.SendMessage(IS_SAVE_SETTINGS);

		BMessage msg(IS_CONTROL_DEVICES);
		msg.AddInt32("type", B_KEYBOARD_DEVICE);
		msg.AddInt32("code", B_KEY_REPEAT_DELAY_CHANGED);
		return fAddOnManager->PostMessage(&msg);
	}

	return reply->AddInt64("delay", fKeyboardSettings.KeyboardRepeatDelay());
}

status_t
InputServer::HandleGetKeyInfo(BMessage* message, BMessage* reply)
{
	return reply->AddData("key_info", B_ANY_TYPE, &fKeyInfo, sizeof(fKeyInfo));
}

status_t
InputServer::HandleGetModifiers(BMessage* message, BMessage* reply)
{
	return reply->AddInt32("modifiers", fKeyInfo.modifiers);
}

status_t
InputServer::HandleGetModifierKey(BMessage* message, BMessage* reply)
{
	int32 modifier;
	if (message->FindInt32("modifier", &modifier) != B_OK)
		return B_ERROR;

	switch (modifier) {
		case B_CAPS_LOCK: return reply->AddInt32("key", fKeys.caps_key);
		case B_NUM_LOCK: return reply->AddInt32("key", fKeys.num_key);
		case B_SCROLL_LOCK: return reply->AddInt32("key", fKeys.scroll_key);
		case B_LEFT_SHIFT_KEY: return reply->AddInt32("key", fKeys.left_shift_key);
		case B_RIGHT_SHIFT_KEY: return reply->AddInt32("key", fKeys.right_shift_key);
		case B_LEFT_COMMAND_KEY: return reply->AddInt32("key", fKeys.left_command_key);
		case B_RIGHT_COMMAND_KEY: return reply->AddInt32("key", fKeys.right_command_key);
		case B_LEFT_CONTROL_KEY: return reply->AddInt32("key", fKeys.left_control_key);
		case B_RIGHT_CONTROL_KEY: return reply->AddInt32("key", fKeys.right_control_key);
		case B_LEFT_OPTION_KEY: return reply->AddInt32("key", fKeys.left_option_key);
		case B_RIGHT_OPTION_KEY: return reply->AddInt32("key", fKeys.right_option_key);
		case B_MENU_KEY: return reply->AddInt32("key", fKeys.menu_key);
	}
	return B_ERROR;
}

status_t
InputServer::HandleSetModifierKey(BMessage* message, BMessage* reply)
{
	int32 modifier, key;
	if (message->FindInt32("modifier", &modifier) != B_OK
		|| message->FindInt32("key", &key) != B_OK)
		return B_ERROR;

	switch (modifier) {
		case B_CAPS_LOCK: fKeys.caps_key = key; break;
		case B_NUM_LOCK: fKeys.num_key = key; break;
		case B_SCROLL_LOCK: fKeys.scroll_key = key; break;
		case B_LEFT_SHIFT_KEY: fKeys.left_shift_key = key; break;
		case B_RIGHT_SHIFT_KEY: fKeys.right_shift_key = key; break;
		case B_LEFT_COMMAND_KEY: fKeys.left_command_key = key; break;
		case B_RIGHT_COMMAND_KEY: fKeys.right_command_key = key; break;
		case B_LEFT_CONTROL_KEY: fKeys.left_control_key = key; break;
		case B_RIGHT_CONTROL_KEY: fKeys.right_control_key = key; break;
		case B_LEFT_OPTION_KEY: fKeys.left_option_key = key; break;
		case B_RIGHT_OPTION_KEY: fKeys.right_option_key = key; break;
		case B_MENU_KEY: fKeys.menu_key = key; break;
		default: return B_ERROR;
	}

	// TODO: unmap the key ?

	be_app_messenger.SendMessage(IS_SAVE_KEYMAP);

	BMessage msg(IS_CONTROL_DEVICES);
	msg.AddInt32("type", B_KEYBOARD_DEVICE);
	msg.AddInt32("code", B_KEY_MAP_CHANGED);
	return fAddOnManager->PostMessage(&msg);
}

status_t
InputServer::HandleSetKeyboardLocks(BMessage* message, BMessage* reply)
{
	if (message->FindInt32("locks", (int32*)&fKeys.lock_settings) == B_OK) {
		be_app_messenger.SendMessage(IS_SAVE_KEYMAP);

		BMessage msg(IS_CONTROL_DEVICES);
		msg.AddInt32("type", B_KEYBOARD_DEVICE);
		msg.AddInt32("code", B_KEY_LOCKS_CHANGED);
		return fAddOnManager->PostMessage(&msg);
	}
	return B_ERROR;
}

// #pragma mark - Mouse settings

status_t
InputServer::_PostMouseControlMessage(int32 code, const BString& mouseName)
{
	BMessage message(IS_CONTROL_DEVICES);
	message.AddInt32("code", code);
	if (mouseName.IsEmpty())
		message.AddInt32("type", B_POINTING_DEVICE);
	else
		message.AddString("device", mouseName);

	return fAddOnManager->PostMessage(&message);
}

void
InputServer::_DeviceStarted(InputDeviceListItem& item)
{
	if (item.Type() == B_POINTING_DEVICE && item.Running()) {
		AutoLocker lock(fRunningMouseListLocker);
		if (lock.IsLocked())
			fRunningMouseList.Add(item.Name());
	}
}

void
InputServer::_DeviceStopping(InputDeviceListItem& item)
{
	if (item.Type() == B_POINTING_DEVICE) {
		AutoLocker lock(fRunningMouseListLocker);
		if (lock.IsLocked())
			fRunningMouseList.Remove(item.Name());
	}
}

MouseSettings*
InputServer::_RunningMouseSettings()
{
	AutoLocker lock(fRunningMouseListLocker);
	if (!lock.IsLocked() || fRunningMouseList.IsEmpty())
		return &fDefaultMouseSettings;
	return _GetSettingsForMouse(fRunningMouseList.First());
}

void
InputServer::_RunningMiceSettings(BList& settings)
{
	AutoLocker lock(fRunningMouseListLocker);
	if (!lock.IsLocked())
		return;

	int32 count = fRunningMouseList.CountStrings();
	for (int32 i = 0; i < count; i++) {
		auto ms = _GetSettingsForMouse(fRunningMouseList.StringAt(i));
		if (ms != NULL)
			settings.AddItem(ms);
	}
}

MouseSettings*
InputServer::_GetSettingsForMouse(BString mouseName)
{
	if (mouseName.IsEmpty())
		return NULL;
	return fMouseSettings.AddMouseSettings(mouseName);
}

status_t
InputServer::HandleGetSetMouseType(BMessage* message, BMessage* reply)
{
	BString mouseName;
	message->FindString("mouse_name", &mouseName);

	MouseSettings* settings = _GetSettingsForMouse(mouseName);

	int32 type;
	if (message->FindInt32("mouse_type", &type) == B_OK) {
		if (settings == NULL) {
			BList allSettings;
			_RunningMiceSettings(allSettings);
			for (int32 i = 0; i < allSettings.CountItems(); i++)
				static_cast<MouseSettings*>(allSettings.ItemAt(i))->SetMouseType(type);
		} else {
			settings->SetMouseType(type);
		}

		be_app_messenger.SendMessage(IS_SAVE_SETTINGS);
		return _PostMouseControlMessage(B_MOUSE_TYPE_CHANGED, mouseName);
	}

	if (settings == NULL)
		settings = _RunningMouseSettings();
	return reply->AddInt32("mouse_type", settings->MouseType());
}

status_t
InputServer::HandleGetSetMouseAcceleration(BMessage* message, BMessage* reply)
{
	BString mouseName;
	message->FindString("mouse_name", &mouseName);

	MouseSettings* settings = _GetSettingsForMouse(mouseName);

	int32 factor;
	if (message->FindInt32("speed", &factor) == B_OK) {
		if (settings == NULL) {
			BList allSettings;
			_RunningMiceSettings(allSettings);
			for (int32 i = 0; i < allSettings.CountItems(); i++)
				static_cast<MouseSettings*>(allSettings.ItemAt(i))->SetAccelerationFactor(factor);
		} else {
			settings->SetAccelerationFactor(factor);
		}

		be_app_messenger.SendMessage(IS_SAVE_SETTINGS);
		return _PostMouseControlMessage(B_MOUSE_ACCELERATION_CHANGED, mouseName);
	}

	if (settings == NULL)
		settings = _RunningMouseSettings();
	return reply->AddInt32("speed", settings->AccelerationFactor());
}

status_t
InputServer::HandleGetSetMouseSpeed(BMessage* message, BMessage* reply)
{
	BString mouseName;
	message->FindString("mouse_name", &mouseName);

	MouseSettings* settings = _GetSettingsForMouse(mouseName);

	int32 speed;
	if (message->FindInt32("speed", &speed) == B_OK) {
		if (settings == NULL) {
			BList allSettings;
			_RunningMiceSettings(allSettings);
			for (int32 i = 0; i < allSettings.CountItems(); i++)
				static_cast<MouseSettings*>(allSettings.ItemAt(i))->SetMouseSpeed(speed);
		} else {
			settings->SetMouseSpeed(speed);
		}

		be_app_messenger.SendMessage(IS_SAVE_SETTINGS);
		return _PostMouseControlMessage(B_MOUSE_SPEED_CHANGED, mouseName);
	}

	if (settings == NULL)
		settings = _RunningMouseSettings();
	return reply->AddInt32("speed", settings->MouseSpeed());
}

status_t
InputServer::HandleGetSetMouseMap(BMessage* message, BMessage* reply)
{
	BString mouseName;
	message->FindString("mouse_name", &mouseName);

	MouseSettings* settings = _GetSettingsForMouse(mouseName);

	mouse_map* map;
	ssize_t size;
	if (message->FindData("mousemap", B_RAW_TYPE, (const void**)&map, &size) == B_OK) {
		if (settings == NULL) {
			BList allSettings;
			_RunningMiceSettings(allSettings);
			for (int32 i = 0; i < allSettings.CountItems(); i++)
				static_cast<MouseSettings*>(allSettings.ItemAt(i))->SetMapping(*map);
		} else {
			settings->SetMapping(*map);
		}

		be_app_messenger.SendMessage(IS_SAVE_SETTINGS);
		return _PostMouseControlMessage(B_MOUSE_MAP_CHANGED, mouseName);
	}

	if (settings == NULL)
		settings = _RunningMouseSettings();
	mouse_map getmap;
	settings->Mapping(getmap);
	return reply->AddData("mousemap", B_RAW_TYPE, &getmap, sizeof(mouse_map));
}

status_t
InputServer::HandleGetSetClickSpeed(BMessage* message, BMessage* reply)
{
	BString mouseName;
	message->FindString("mouse_name", &mouseName);

	MouseSettings* settings = _GetSettingsForMouse(mouseName);

	bigtime_t clickSpeed;
	if (message->FindInt64("speed", &clickSpeed) == B_OK) {
		if (settings == NULL) {
			BList allSettings;
			_RunningMiceSettings(allSettings);
			for (int32 i = 0; i < allSettings.CountItems(); i++)
				static_cast<MouseSettings*>(allSettings.ItemAt(i))->SetClickSpeed(clickSpeed);
		} else {
			settings->SetClickSpeed(clickSpeed);
		}

		be_app_messenger.SendMessage(IS_SAVE_SETTINGS);
		return _PostMouseControlMessage(B_CLICK_SPEED_CHANGED, mouseName);
	}

	if (settings == NULL)
		settings = _RunningMouseSettings();
	return reply->AddInt64("speed", settings->ClickSpeed());
}

status_t
InputServer::HandleSetMousePosition(BMessage* message, BMessage* reply)
{
	CALLED();

	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		return B_BAD_VALUE;

	auto event = new(nothrow) BMessage(B_MOUSE_MOVED);
	if (event == NULL)
		return B_NO_MEMORY;

	event->AddPoint("where", where);
	event->AddBool("be:set_mouse", true);
	if (EnqueueDeviceMessage(event) != B_OK) {
		delete event;
		return B_NO_MEMORY;
	}

	return B_OK;
}

// #pragma mark - Keyboard settings

status_t
InputServer::HandleGetSetKeyboardID(BMessage* message, BMessage* reply)
{
	int16 id;
	if (message->FindInt16("id", &id) == B_OK) {
		fKeyboardID = (uint16)id;
		return B_OK;
	}
	return reply->AddInt16("id", fKeyboardID);
}

status_t
InputServer::HandleGetSetKeyRepeatRate(BMessage* message, BMessage* reply)
{
	int32 keyRepeatRate;
	if (message->FindInt32("rate", &keyRepeatRate) == B_OK) {
		fKeyboardSettings.SetKeyboardRepeatRate(keyRepeatRate);
		be_app_messenger.SendMessage(IS_SAVE_SETTINGS);

		BMessage msg(IS_CONTROL_DEVICES);
		msg.AddInt32("type", B_KEYBOARD_DEVICE);
		msg.AddInt32("code", B_KEY_REPEAT_RATE_CHANGED);
		return fAddOnManager->PostMessage(&msg);
	}

	return reply->AddInt32("rate", fKeyboardSettings.KeyboardRepeatRate());
}

status_t
InputServer::HandleGetSetKeyMap(BMessage* message, BMessage* reply)
{
	CALLED();

	if (message->what == IS_GET_KEY_MAP) {
		status_t status = reply->AddData("keymap", B_ANY_TYPE, &fKeys, sizeof(fKeys));
		if (status == B_OK)
			status = reply->AddData("key_buffer", B_ANY_TYPE, fChars, fCharsSize);
		return status;
	}

	status_t status = _LoadKeymap();
	if (status != B_OK) {
		status = _LoadSystemKeymap();
		if (status != B_OK)
			return status;
	}

	BMessage msg(IS_CONTROL_DEVICES);
	msg.AddInt32("type", B_KEYBOARD_DEVICE);
	msg.AddInt32("code", B_KEY_MAP_CHANGED);
	status = fAddOnManager->PostMessage(&msg);

	if (status == B_OK) {
		BMessage appMsg(B_KEY_MAP_LOADED);
		be_roster->Broadcast(&appMsg);
	}

	return status;
}

status_t
InputServer::HandleFocusUnfocusIMAwareView(BMessage* message, BMessage* reply)
{
	CALLED();

	BMessenger messenger;
	status_t status = message->FindMessenger("view", &messenger);
	if (status != B_OK)
		return status;

	fInputMethodAware = (message->what == IS_FOCUS_IM_AWARE_VIEW);
	PRINT(("HandleFocusUnfocusIMAwareView : %s\n",
		fInputMethodAware ? "entering" : "leaving"));

	return B_OK;
}

status_t
InputServer::EnqueueDeviceMessage(BMessage* message)
{
	CALLED();

	AutoLocker lock(fEventQueueLock);
	if (!lock.IsLocked())
		return B_ERROR;

	if (!fEventQueue.AddItem(message))
		return B_NO_MEMORY;

	if (fEventQueue.CountItems() == 1)
		write_port_etc(fEventLooperPort, 1, NULL, 0, B_RELATIVE_TIMEOUT, 0);

	return B_OK;
}

status_t
InputServer::EnqueueMethodMessage(BMessage* message)
{
	CALLED();
	PRINT(("%s what:%c%c%c%c\n", __PRETTY_FUNCTION__,
		(char)(message->what >> 24), (char)(message->what >> 16),
		(char)(message->what >> 8), (char)message->what));

#ifdef DEBUG
	if (message->what == 'IMEV') {
		int32 code;
		message->FindInt32("be:opcode", &code);
		PRINT(("%s be:opcode %" B_PRId32 "\n", __PRETTY_FUNCTION__, code));
	}
#endif

	AutoLocker lock(fEventQueueLock);
	if (!lock.IsLocked())
		return B_ERROR;

	if (!fMethodQueue.AddItem(message))
		return B_NO_MEMORY;

	if (fMethodQueue.CountItems() == 1)
		write_port_etc(fEventLooperPort, 0, NULL, 0, B_RELATIVE_TIMEOUT, 0);

	return B_OK;
}

status_t
InputServer::SetNextMethod(bool direction)
{
	AutoLocker lock(gInputMethodListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	int32 index = gInputMethodList.IndexOf(fActiveMethod);
	int32 oldIndex = index;

	index += (direction ? 1 : -1);

	if (index < -1)
		index = gInputMethodList.CountItems() - 1;
	if (index >= gInputMethodList.CountItems())
		index = -1;

	if (index == oldIndex)
		return B_BAD_INDEX;

	BInputServerMethod* method = &gKeymapMethod;
	if (index != -1)
		method = static_cast<BInputServerMethod*>(gInputMethodList.ItemAt(index));

	SetActiveMethod(method);
	return B_OK;
}

void
InputServer::SetActiveMethod(BInputServerMethod* method)
{
	CALLED();
	if (fActiveMethod != NULL)
		fActiveMethod->fOwner->MethodActivated(false);

	fActiveMethod = method;

	if (fActiveMethod != NULL)
		fActiveMethod->fOwner->MethodActivated(true);
}

const BMessenger*
InputServer::MethodReplicant()
{
	return fReplicantMessenger;
}

void
InputServer::SetMethodReplicant(const BMessenger* messenger)
{
	fReplicantMessenger = messenger;
}

bool
InputServer::EventLoopRunning()
{
	return fEventLooperPort >= B_OK;
}

status_t
InputServer::GetDeviceInfo(const char* name, input_device_type* _type,
	bool* _isRunning)
{
	AutoLocker lock(fInputDeviceListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	for (int32 i = fInputDeviceList.CountItems() - 1; i >= 0; i--) {
		auto item = static_cast<InputDeviceListItem*>(fInputDeviceList.ItemAt(i));
		if (item->HasName(name)) {
			if (_type != NULL)
				*_type = item->Type();
			if (_isRunning != NULL)
				*_isRunning = item->Running();
			return B_OK;
		}
	}

	return B_NAME_NOT_FOUND;
}

status_t
InputServer::GetDeviceInfos(BMessage* msg)
{
	CALLED();
	AutoLocker lock(fInputDeviceListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	for (int32 i = fInputDeviceList.CountItems() - 1; i >= 0; i--) {
		auto item = static_cast<InputDeviceListItem*>(fInputDeviceList.ItemAt(i));
		msg->AddString("device", item->Name());
		msg->AddInt32("type", item->Type());
	}
	return B_OK;
}

status_t
InputServer::UnregisterDevices(BInputServerDevice& serverDevice,
	input_device_ref** devices)
{
	CALLED();
	AutoLocker lock(fInputDeviceListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	if (devices != NULL) {
		for (int32 i = 0; devices[i] != NULL; i++) {
			for (int32 j = fInputDeviceList.CountItems() - 1; j >= 0; j--) {
				auto item = static_cast<InputDeviceListItem*>(
					fInputDeviceList.ItemAt(j));

				if (item->ServerDevice() == &serverDevice
					&& item->HasName(devices[i]->name)) {
					_DeviceStopping(*item);
					item->Stop();
					if (fInputDeviceList.RemoveItem(j)) {
						BMessage message(IS_NOTIFY_DEVICE);
						message.AddBool("added", false);
						message.AddString("name", item->Name());
						message.AddInt32("type", item->Type());
						fAddOnManager->PostMessage(&message);
						delete item;
					}
					break;
				}
			}
		}
	} else {
		for (int32 i = fInputDeviceList.CountItems() - 1; i >= 0; i--) {
			auto item = static_cast<InputDeviceListItem*>(
				fInputDeviceList.ItemAt(i));

			if (item->ServerDevice() == &serverDevice) {
				_DeviceStopping(*item);
				item->Stop();
				if (fInputDeviceList.RemoveItem(i))
					delete item;
			}
		}
	}

	return B_OK;
}

status_t
InputServer::RegisterDevices(BInputServerDevice& serverDevice,
	input_device_ref** devices)
{
	if (devices == NULL)
		return B_BAD_VALUE;

	AutoLocker lock(fInputDeviceListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	for (int32 i = 0; devices[i] != NULL; i++) {
		auto device = devices[i];
		if (device->type != B_POINTING_DEVICE
			&& device->type != B_KEYBOARD_DEVICE
			&& device->type != B_UNDEFINED_DEVICE)
			continue;

		bool found = false;
		for (int32 j = fInputDeviceList.CountItems() - 1; j >= 0; j--) {
			auto item = static_cast<InputDeviceListItem*>(
				fInputDeviceList.ItemAt(j));
			if (item->HasName(device->name)) {
				debug_printf("InputServer::RegisterDevices() device_ref already exists: %s\n",
					device->name);
				PRINT(("RegisterDevices found %s\n", device->name));
				found = true;
				break;
			}
		}

		if (!found) {
			PRINT(("RegisterDevices not found %s\n", device->name));
			auto item = new(nothrow) InputDeviceListItem(serverDevice, *device);
			if (item != NULL && fInputDeviceList.AddItem(item)) {
				item->Start();
				_DeviceStarted(*item);
				BMessage message(IS_NOTIFY_DEVICE);
				message.AddBool("added", true);
				message.AddString("name", item->Name());
				message.AddInt32("type", item->Type());
				fAddOnManager->PostMessage(&message);
			} else {
				delete item;
				return B_NO_MEMORY;
			}
		}
	}

	return B_OK;
}

status_t
InputServer::StartStopDevices(const char* name, input_device_type type,
	bool doStart)
{
	CALLED();
	AutoLocker lock(fInputDeviceListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	for (int32 i = fInputDeviceList.CountItems() - 1; i >= 0; i--) {
		auto item = static_cast<InputDeviceListItem*>(
			fInputDeviceList.ItemAt(i));
		if (item == NULL)
			continue;

		if (item->Matches(name, type)) {
			if (doStart == item->Running()) {
				if (name != NULL)
					return B_OK;
				else
					continue;
			}

			if (doStart) {
				item->Start();
				_DeviceStarted(*item);
			} else {
				_DeviceStopping(*item);
				item->Stop();
			}

			BMessage message(IS_NOTIFY_DEVICE);
			message.AddBool("started", doStart);
			message.AddString("name", item->Name());
			message.AddInt32("type", item->Type());
			fAddOnManager->PostMessage(&message);

			if (name != NULL)
				return B_OK;
		}
	}

	return name != NULL ? B_ERROR : B_OK;
}

status_t
InputServer::StartStopDevices(BInputServerDevice& serverDevice, bool doStart)
{
	CALLED();
	AutoLocker lock(fInputDeviceListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	for (int32 i = fInputDeviceList.CountItems() - 1; i >= 0; i--) {
		auto item = static_cast<InputDeviceListItem*>(
			fInputDeviceList.ItemAt(i));

		if (item->ServerDevice() != &serverDevice)
			continue;

		if (doStart == item->Running())
			continue;

		if (doStart) {
			item->Start();
			_DeviceStarted(*item);
		} else {
			_DeviceStopping(*item);
			item->Stop();
		}

		BMessage message(IS_NOTIFY_DEVICE);
		message.AddBool("started", doStart);
		message.AddString("name", item->Name());
		message.AddInt32("type", item->Type());
		fAddOnManager->PostMessage(&message);
	}

	return B_OK;
}

status_t
InputServer::ControlDevices(const char* name, input_device_type type,
	uint32 code, BMessage* message)
{
	CALLED();
	AutoLocker lock(fInputDeviceListLocker);
	if (!lock.IsLocked())
		return B_ERROR;

	for (int32 i = fInputDeviceList.CountItems() - 1; i >= 0; i--) {
		auto item = static_cast<InputDeviceListItem*>(
			fInputDeviceList.ItemAt(i));
		if (item == NULL)
			continue;

		if (item->Matches(name, type)) {
			item->Control(code, message);
			if (name != NULL)
				return B_OK;
		}
	}

	return name != NULL ? B_ERROR : B_OK;
}

bool
InputServer::SafeMode()
{
	char parameter[32];
	size_t parameterLength = sizeof(parameter);

	if (_kern_get_safemode_option(B_SAFEMODE_SAFE_MODE, parameter,
			&parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1"))
			return true;
	}

	if (_kern_get_safemode_option(B_SAFEMODE_DISABLE_USER_ADD_ONS, parameter,
			&parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1"))
			return true;
	}

	return false;
}

status_t
InputServer::_StartEventLoop()
{
	CALLED();
	fEventLooperPort = create_port(100, "input server events");
	if (fEventLooperPort < 0) {
		PRINTERR(("InputServer: create_port error: (0x%" B_PRIx32 ") %s\n",
			fEventLooperPort, strerror(fEventLooperPort)));
		return fEventLooperPort;
	}

	thread_id thread = spawn_thread(_EventLooper, "_input_server_event_loop_",
		B_REAL_TIME_DISPLAY_PRIORITY + 3, this);
	if (thread < B_OK || resume_thread(thread) < B_OK) {
		if (thread >= B_OK)
			kill_thread(thread);
		delete_port(fEventLooperPort);
		fEventLooperPort = -1;
		return thread < B_OK ? thread : B_ERROR;
	}

	return B_OK;
}

status_t
InputServer::_EventLooper(void* arg)
{
	auto self = static_cast<InputServer*>(arg);
	self->_EventLoop();
	return B_OK;
}

void
InputServer::_EventLoop()
{
	while (true) {
		ssize_t length = port_buffer_size(fEventLooperPort);
		if (length < B_OK) {
			PRINT(("[Event Looper] port gone, exiting.\n"));
			return;
		}

		PRINT(("[Event Looper] BMessage Size = %lu\n", length));

		char buffer[length];
		int32 code;
		status_t err = read_port(fEventLooperPort, &code, buffer, length);
		if (err != length) {
			if (err >= 0) {
				PRINTERR(("InputServer: failed to read full packet "
					"(read %" B_PRIu32 " of %lu)\n", err, length));
			} else {
				PRINTERR(("InputServer: read_port error: (0x%" B_PRIx32
					") %s\n", err, strerror(err)));
			}
			continue;
		}

		EventList events;
		{
			AutoLocker lock(fEventQueueLock);
			if (lock.IsLocked()) {
				events.AddList(&fEventQueue);
				fEventQueue.MakeEmpty();
			}
		}

		if (length > 0) {
			auto event = new(nothrow) BMessage;
			if (event != NULL && event->Unflatten(buffer) == B_OK) {
				events.AddItem(event);
			} else {
				PRINTERR(("[InputServer] Unflatten() error\n"));
				delete event;
				continue;
			}
		}

		if (_SanitizeEvents(events)
			&& _MethodizeEvents(events)
			&& _FilterEvents(events)) {
			_UpdateMouseAndKeys(events);
			_DispatchEvents(events);
		}
	}
}

void
InputServer::_ProcessMouseEvent(BMessage* event)
{
	event->FindPoint("where", &fMousePos);
}

bool
InputServer::_ProcessKeyEvent(BMessage* event, EventList& events, int32 index)
{
	uint32 modifiers;
	if (event->FindInt32("modifiers", (int32*)&modifiers) == B_OK)
		fKeyInfo.modifiers = modifiers;

	const uint8* data;
	ssize_t size;
	if (event->FindData("states", B_UINT8_TYPE,
			(const void**)&data, &size) == B_OK) {
		PRINT(("updated keyinfo\n"));
		if (size == sizeof(fKeyInfo.key_states))
			memcpy(fKeyInfo.key_states, data, size);
	}

	if (fActiveMethod == NULL)
		return false;

	PRINT(("SanitizeEvents: %" B_PRIx32 ", %x\n", fKeyInfo.modifiers,
		fKeyInfo.key_states[KEY_Spacebar >> 3]));

	uint8 byte;
	if (event->FindInt8("byte", (int8*)&byte) < B_OK)
		byte = 0;

	if ((((fKeyInfo.modifiers & B_COMMAND_KEY) != 0 && byte == ' ')
			|| byte == B_HANKAKU_ZENKAKU)
		&& SetNextMethod((fKeyInfo.modifiers & B_SHIFT_KEY) == 0) == B_OK) {
		events.RemoveItemAt(index);
		delete event;
		return true;  // Event was removed
	}
	
	return false;  // Event was not removed
}

void
InputServer::_UpdateMouseAndKeys(EventList& events)
{
	for (int32 i = 0; i < events.CountItems(); i++) {
		auto event = events.ItemAt(i);
		if (event == NULL)
			continue;

		switch (event->what) {
			case B_MOUSE_DOWN:
			case B_MOUSE_UP:
			case B_MOUSE_MOVED:
				_ProcessMouseEvent(event);
				break;

			case B_KEY_DOWN:
			case B_UNMAPPED_KEY_DOWN:
				// Returns true if event was removed
				if (_ProcessKeyEvent(event, events, i))
					i--;  // Don't skip next event after removal
				break;
		}
	}
}

bool
InputServer::_SanitizeEvents(EventList& events)
{
	CALLED();

	for (int32 index = 0; index < events.CountItems(); index++) {
		auto event = events.ItemAt(index);
		if (event == NULL)
			continue;

		switch (event->what) {
			case B_MOUSE_MOVED:
			case B_MOUSE_DOWN:
			{
				int32 buttons;
				if (event->FindInt32("buttons", &buttons) != B_OK)
					event->AddInt32("buttons", 0);
			}
			case B_MOUSE_UP:
			{
				BPoint where;
				int32 x, y;
				float absX, absY;

				if (event->FindInt32("x", &x) == B_OK
					&& event->FindInt32("y", &y) == B_OK) {
					where.x = fMousePos.x + x;
					where.y = fMousePos.y - y;

					event->RemoveName("x");
					event->RemoveName("y");
					event->AddInt32("be:delta_x", x);
					event->AddInt32("be:delta_y", y);

					PRINT(("new position: %f, %f, %" B_PRId32 ", %" B_PRId32
						"\n", where.x, where.y, x, y));
				} else if (event->FindFloat("x", &absX) == B_OK
					&& event->FindFloat("y", &absY) == B_OK) {
					where.x = absX * fFrame.Width();
					where.y = absY * fFrame.Height();

					event->RemoveName("x");
					event->RemoveName("y");
					PRINT(("new position : %f, %f\n", where.x, where.y));
				} else if (event->FindPoint("where", &where) == B_OK) {
					PRINT(("new position : %f, %f\n", where.x, where.y));
				}

				where.x = roundf(where.x);
				where.y = roundf(where.y);
				where.ConstrainTo(fFrame);
				if (event->ReplacePoint("where", where) != B_OK)
					event->AddPoint("where", where);

				if (!event->HasInt64("when"))
					event->AddInt64("when", system_time());

				event->AddInt32("modifiers", fKeyInfo.modifiers);
				break;
			}
			case B_KEY_DOWN:
			case B_UNMAPPED_KEY_DOWN:
				if (!event->HasInt32("modifiers"))
					event->AddInt32("modifiers", fKeyInfo.modifiers);

				if (!event->HasData("states", B_UINT8_TYPE)) {
					event->AddData("states", B_UINT8_TYPE, fKeyInfo.key_states,
						sizeof(fKeyInfo.key_states));
				}
				break;
		}
	}

	return true;
}

bool
InputServer::_MethodizeEvents(EventList& events)
{
	CALLED();

	if (fActiveMethod == NULL)
		return true;

	int32 count = events.CountItems();
	for (int32 i = 0; i < count;) {
		_FilterEvent(fActiveMethod, events, i, count);
	}

	{
		AutoLocker lock(fEventQueueLock);
		if (lock.IsLocked()) {
			events.AddList(&fMethodQueue);
			fMethodQueue.MakeEmpty();
		}
	}

	if (!fInputMethodAware) {
		int32 newCount = events.CountItems();

		for (int32 i = 0; i < newCount; i++) {
			auto event = events.ItemAt(i);
			if (event->what != B_INPUT_METHOD_EVENT)
				continue;

			SERIAL_PRINT(("IME received\n"));

			bool removeEvent = true;
			int32 opcode;
			if (event->FindInt32("be:opcode", &opcode) == B_OK) {
				bool inlineOnly;
				if (event->FindBool("be:inline_only", &inlineOnly) != B_OK)
					inlineOnly = false;

				if (inlineOnly) {
					BMessage translated;
					bool confirmed;
					if (opcode == B_INPUT_METHOD_CHANGED
						&& event->FindBool("be:confirmed", &confirmed) == B_OK && confirmed
						&& event->FindMessage("be:translated", &translated) == B_OK) {
						*event = translated;
						removeEvent = false;
					}
				} else {
					if (fInputMethodWindow == NULL
						&& opcode == B_INPUT_METHOD_STARTED)
						fInputMethodWindow = new(nothrow) BottomlineWindow();

					if (fInputMethodWindow != NULL) {
						EventList newEvents;
						fInputMethodWindow->HandleInputMethodEvent(event, newEvents);

						if (!newEvents.IsEmpty()) {
							events.AddList(&newEvents);
							opcode = B_INPUT_METHOD_STOPPED;
						}

						if (opcode == B_INPUT_METHOD_STOPPED) {
							fInputMethodWindow->PostMessage(B_QUIT_REQUESTED);
							fInputMethodWindow = NULL;
						}
					}
				}
			}

			if (removeEvent) {
				events.RemoveItemAt(i--);
				delete event;
				newCount--;
			}
		}
	}

	return events.CountItems() > 0;
}

bool
InputServer::_FilterEvents(EventList& events)
{
	CALLED();
	AutoLocker lock(gInputFilterListLocker);
	if (!lock.IsLocked())
		return false;

	int32 count = gInputFilterList.CountItems();
	int32 eventCount = events.CountItems();

	for (int32 i = 0; i < count; i++) {
		auto filter = static_cast<BInputServerFilter*>(
			gInputFilterList.ItemAt(i));

		for (int32 eventIndex = 0; eventIndex < eventCount;) {
			_FilterEvent(filter, events, eventIndex, eventCount);
		}
	}

	return eventCount != 0;
}

void
InputServer::_DispatchEvents(EventList& events)
{
	CALLED();

	for (int32 i = 0; i < events.CountItems(); i++) {
		auto event = events.ItemAt(i);
		_DispatchEvent(event);
		delete event;
	}

	events.MakeEmpty();
}

void
InputServer::_FilterEvent(BInputServerFilter* filter, EventList& events,
	int32& index, int32& count)
{
	auto event = events.ItemAt(index);

	BList newEvents;
	filter_result result = filter->Filter(event, &newEvents);

	if (result == B_SKIP_MESSAGE || newEvents.CountItems() > 0) {
		events.RemoveItemAt(index);
		delete event;

		if (result == B_DISPATCH_MESSAGE) {
			EventList addedEvents;
			EventList::Private(&addedEvents).AsBList()->AddList(&newEvents);
			_SanitizeEvents(addedEvents);
			events.AddList(&addedEvents, index);
			index += newEvents.CountItems();
			count = events.CountItems();
		} else
			count--;
	} else
		index++;
}

status_t
InputServer::_DispatchEvent(BMessage* event)
{
	CALLED();

	switch (event->what) {
		case B_MOUSE_MOVED:
		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
			if (fCursorBuffer != NULL) {
				atomic_set((int32*)&fCursorBuffer->pos,
					(uint32)fMousePos.x << 16UL | ((uint32)fMousePos.y & 0xffff));
				if (atomic_or(&fCursorBuffer->read, 1) == 0)
					release_sem(fCursorSem);
			}
			break;

		case B_KEY_DOWN:
		case B_KEY_UP:
		case B_UNMAPPED_KEY_DOWN:
		case B_UNMAPPED_KEY_UP:
		case B_MODIFIERS_CHANGED:
		{
			uint32 modifiers;
			if (event->FindInt32("modifiers", (int32*)&modifiers) == B_OK)
				fKeyInfo.modifiers = modifiers;
			else
				event->AddInt32("modifiers", fKeyInfo.modifiers);

			const uint8* data;
			ssize_t size;
			if (event->FindData("states", B_UINT8_TYPE,
					(const void**)&data, &size) == B_OK) {
				PRINT(("updated keyinfo\n"));
				if (size == sizeof(fKeyInfo.key_states))
					memcpy(fKeyInfo.key_states, data, size);
			} else {
				event->AddData("states", B_UINT8_TYPE, fKeyInfo.key_states,
					sizeof(fKeyInfo.key_states));
			}
			break;
		}

		default:
			break;
	}

	BMessenger reply;
	BMessage::Private messagePrivate(event);
	return messagePrivate.SendMessage(fAppServerPort, fAppServerTeam, 0, 0,
		false, reply);
}

// #pragma mark -

extern "C" void
RegisterDevices(input_device_ref** devices)
{
	CALLED();
}

BView*
instantiate_deskbar_item()
{
	return new MethodReplicant(INPUTSERVER_SIGNATURE);
}

int
main(int /*argc*/, char** /*argv*/)
{
	auto inputServer = new InputServer;
	inputServer->Run();
	delete inputServer;
	return 0;
}
