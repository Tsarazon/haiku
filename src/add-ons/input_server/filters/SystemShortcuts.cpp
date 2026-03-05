/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "SystemShortcuts.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Autolock.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Input.h>
#include <InterfaceDefs.h>
#include <Path.h>
#include <PathFinder.h>
#include <PathMonitor.h>
#include <WindowScreen.h>
#include <StringList.h>

#include "BitFieldTesters.h"
#include "CommandActuators.h"
#include "KeyInfos.h"


#define SHORTCUTS_SETTINGS_FILE		"shortcuts_settings"
#define SHORTCUTS_CATCHER_PORT_NAME	"ShortcutsCatcherPort"

enum {
	kMsgFileUpdated		= 'fiUp',
	kMsgExecuteAsync	= 'exAs',
	kMsgExecuteCommand	= 'exec',
	kMsgReplenish		= 'repl',
};


// KeyBinding

KeyBinding::KeyBinding(int32 key, BitFieldTester* tester,
	CommandActuator* actuator, const BMessage& actuatorArchive)
	:
	fKey(key),
	fTester(tester),
	fActuator(actuator),
	fActuatorArchive(actuatorArchive)
{
}


KeyBinding::~KeyBinding()
{
	delete fActuator;
	delete fTester;
}


bool
KeyBinding::ModifiersMatch(uint32 modifiers) const
{
	return fTester != NULL && fTester->IsMatching(modifiers);
}


// ShortcutExecutor

ShortcutExecutor::ShortcutExecutor()
	:
	BLooper("shortcut executor")
{
}


ShortcutExecutor::~ShortcutExecutor()
{
}


void
ShortcutExecutor::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_KEY_DOWN:
		case B_UNMAPPED_KEY_DOWN:
		{
			// Async execution of a CommandActuator.
			BMessage actuatorMsg;
			void* asyncData;
			if (message->FindMessage("act", &actuatorMsg) == B_OK
				&& message->FindPointer("adata", &asyncData) == B_OK) {
				BArchivable* obj = instantiate_object(&actuatorMsg);
				if (obj != NULL) {
					CommandActuator* actuator
						= dynamic_cast<CommandActuator*>(obj);
					if (actuator != NULL)
						actuator->KeyEventAsync(message, asyncData);
					delete obj;
				}
			}
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


// SystemShortcuts

SystemShortcuts::SystemShortcuts(FilterChain* owner)
	:
	fOwner(owner),
	fExecutor(NULL),
	fBindingsLock("shortcuts bindings"),
	fBindings(16),
	fPendingInjects(4),
	fSettingsPath(NULL),
	fPort(-1)
{
	// Resolve settings path.
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append(SHORTCUTS_SETTINGS_FILE) == B_OK) {
		fSettingsPath = strdup(path.Path());
	}

	// Initialize key name tables (shared library function).
	InitKeyIndices();

	// Start executor looper.
	fExecutor = new(std::nothrow) ShortcutExecutor();
	if (fExecutor != NULL) {
		fExecutor->Run();
		fExecutorMessenger = BMessenger(fExecutor);
	}

	// Create port for IPC with Shortcuts preflet.
	fPort = create_port(1, SHORTCUTS_CATCHER_PORT_NAME);
	_PutMessengerToPort();

	// Watch settings file.
	if (fSettingsPath != NULL) {
		BPrivate::BPathMonitor::StartWatching(fSettingsPath,
			B_WATCH_STAT | B_WATCH_FILES_ONLY,
			fOwner->Controller());
	}

	// Load initial settings.
	_LoadSettings();
}


SystemShortcuts::~SystemShortcuts()
{
	if (fPort >= 0)
		close_port(fPort);

	if (fSettingsPath != NULL) {
		BPrivate::BPathMonitor::StopWatching(
			BMessenger(fOwner->Controller()));
	}

	if (fExecutor != NULL && fExecutor->Lock())
		fExecutor->Quit();

	free(fSettingsPath);
}


status_t
SystemShortcuts::InitCheck()
{
	return fExecutor != NULL ? B_OK : B_NO_MEMORY;
}


filter_result
SystemShortcuts::Filter(BMessage* message, BList* outList)
{
	// Track mouse state for CommandActuators that need it
	// (MoveMouseBy, MouseButton, etc.).
	switch (message->what) {
		case B_MOUSE_MOVED:
		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
			fLastMouseMessage = *message;
			break;
	}

	// Drain any injected events (from Shortcuts preflet's EXECUTE_COMMAND).
	// Transfer items under lock, process outside lock.
	BObjectList<BMessage, true> pending(4);
	if (fBindingsLock.Lock()) {
		while (fPendingInjects.CountItems() > 0)
			pending.AddItem(fPendingInjects.RemoveItemAt(0));
		fBindingsLock.Unlock();
	}

	for (int32 i = 0; i < pending.CountItems(); i++) {
		BMessage* injected = pending.ItemAt(i);
		BArchivable* obj = instantiate_object(injected);
		if (obj != NULL) {
			CommandActuator* actuator
				= dynamic_cast<CommandActuator*>(obj);
			if (actuator != NULL) {
				BMessage keyMsg(*message);
				keyMsg.what = B_KEY_DOWN;

				void* asyncData = NULL;
				actuator->KeyEvent(&keyMsg, outList, &asyncData,
					&fLastMouseMessage);

				if (asyncData != NULL) {
					keyMsg.AddMessage("act", injected);
					keyMsg.AddPointer("adata", asyncData);
					fExecutorMessenger.SendMessage(&keyMsg);
				}
			}
			delete obj;
		}
	}

	// Only handle key events.
	if (message->what != B_KEY_DOWN
		&& message->what != B_KEY_UP
		&& message->what != B_UNMAPPED_KEY_DOWN
		&& message->what != B_UNMAPPED_KEY_UP)
		return B_DISPATCH_MESSAGE;

	int32 key;
	uint32 modifiers;
	if (message->FindInt32("key", &key) != B_OK
		|| message->FindInt32("modifiers", (int32*)&modifiers) != B_OK)
		return B_DISPATCH_MESSAGE;

	filter_result result = B_DISPATCH_MESSAGE;

	if (fBindingsLock.Lock()) {
		for (int32 i = 0; i < fBindings.CountItems(); i++) {
			KeyBinding* binding = fBindings.ItemAt(i);

			if (key != binding->Key()
				|| !binding->ModifiersMatch(modifiers))
				continue;

			void* asyncData = NULL;
			filter_result sub = binding->Actuator()->KeyEvent(
				message, outList, &asyncData, &fLastMouseMessage);

			if (asyncData != NULL) {
				BMessage asyncMsg(*message);
				asyncMsg.AddMessage("act", &binding->ActuatorArchive());
				asyncMsg.AddPointer("adata", asyncData);
				fExecutorMessenger.SendMessage(&asyncMsg);
			}

			if (sub == B_SKIP_MESSAGE)
				result = B_SKIP_MESSAGE;
		}
		fBindingsLock.Unlock();
	}

	return result;
}


void
SystemShortcuts::ReloadSettings()
{
	_LoadSettings();
}


void
SystemShortcuts::_LoadSettings()
{
	BAutolock _(fBindingsLock);
	_ClearBindings();

	if (fSettingsPath == NULL)
		return;

	BFile file(fSettingsPath, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage archive;
	if (archive.Unflatten(&file) != B_OK)
		return;

	file.GetNodeRef(&fSettingsNodeRef);

	// Read specs in the format generated by the Shortcuts preflet:
	// archive contains "spec" sub-messages, each with:
	//   "key"        - int32 keycode
	//   "modtester"  - BMessage (archived BitFieldTester)
	//   "act"        - BMessage (archived CommandActuator)
	//   "command"    - string (optional, original command text)

	BMessage spec;
	for (int32 i = 0; archive.FindMessage("spec", i, &spec) == B_OK; i++) {
		int32 key;
		BMessage testerMsg;
		BMessage actuatorMsg;

		if (spec.FindInt32("key", &key) != B_OK
			|| spec.FindMessage("modtester", &testerMsg) != B_OK
			|| spec.FindMessage("act", &actuatorMsg) != B_OK)
			continue;

		// Skip Tracker add-on shortcuts (handled by Tracker itself).
		BString command;
		if (spec.FindString("command", &command) == B_OK) {
			BStringList paths;
			BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY,
				"Tracker", paths);
			bool isTrackerAddOn = false;
			for (int32 j = 0; j < paths.CountStrings(); j++) {
				if (command.StartsWith(paths.StringAt(j))) {
					isTrackerAddOn = true;
					break;
				}
			}
			if (isTrackerAddOn)
				continue;
		}

		// Instantiate tester.
		BArchivable* testerObj = instantiate_object(&testerMsg);
		BitFieldTester* tester = dynamic_cast<BitFieldTester*>(testerObj);
		if (tester == NULL) {
			delete testerObj;
			continue;
		}

		// Instantiate actuator.
		BArchivable* actuatorObj = instantiate_object(&actuatorMsg);
		CommandActuator* actuator
			= dynamic_cast<CommandActuator*>(actuatorObj);
		if (actuator == NULL) {
			delete actuatorObj;
			delete tester;
			continue;
		}

		fBindings.AddItem(
			new(std::nothrow) KeyBinding(key, tester, actuator,
				actuatorMsg));
	}
}


void
SystemShortcuts::_ClearBindings()
{
	fBindings.MakeEmpty();
}


void
SystemShortcuts::_PutMessengerToPort()
{
	if (fPort < 0 || fOwner->Controller() == NULL)
		return;

	BMessage msg;
	msg.AddMessenger("target", BMessenger(fOwner->Controller()));

	char buffer[2048];
	ssize_t size = msg.FlattenedSize();
	if (size <= (ssize_t)sizeof(buffer)
		&& msg.Flatten(buffer, size) == B_OK) {
		write_port_etc(fPort, 0, buffer, size, B_TIMEOUT, 250000);
	}
}


void
SystemShortcuts::AddInject(BMessage* actuatorArchive)
{
	if (actuatorArchive == NULL)
		return;

	BAutolock _(fBindingsLock);
	fPendingInjects.AddItem(new(std::nothrow) BMessage(*actuatorArchive));

	// Force input_server to call Filter() so we can process the inject.
	// Moving the mouse to its current position generates a B_MOUSE_MOVED.
	BPoint where;
	if (fLastMouseMessage.FindPoint("where", &where) == B_OK)
		set_mouse_position((int32)where.x, (int32)where.y);
}
