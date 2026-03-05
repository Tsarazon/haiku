/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 *
 * System-wide keyboard shortcuts.
 * Desktop-only in pure tablet mode (no physical keyboard).
 *
 * Backward compatible with the existing shortcuts_settings file format
 * (BArchivable-based BitFieldTesters + CommandActuators). The Shortcuts
 * preflet generates this format, so we must continue to read it.
 *
 * Uses the shared libshortcuts_shared.a library for:
 *   - BitFieldTesters (modifier matching)
 *   - CommandActuators (action execution)
 *   - KeyInfos (key name lookups)
 *   - ParseCommandLine (argument parsing)
 *
 * Architecture:
 *   SystemShortcuts   - SubFilter, handles key events in input thread
 *   ShortcutsMap      - Loads/watches settings, manages binding list
 *   ShortcutExecutor  - BLooper, runs async actions off input thread
 */
#ifndef SYSTEM_SHORTCUTS_H
#define SYSTEM_SHORTCUTS_H

#include "FilterChain.h"

#include <Locker.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <ObjectList.h>
#include <OS.h>

class BitFieldTester;
class CommandActuator;


// A single key binding: key code + modifier tester + action.
class KeyBinding {
public:
								KeyBinding(int32 key,
									BitFieldTester* tester,
									CommandActuator* actuator,
									const BMessage& actuatorArchive);
								~KeyBinding();

			int32				Key() const { return fKey; }
			bool				ModifiersMatch(uint32 modifiers) const;
			CommandActuator*	Actuator() { return fActuator; }
			const BMessage&		ActuatorArchive() const
									{ return fActuatorArchive; }

private:
			int32				fKey;
			BitFieldTester*		fTester;
			CommandActuator*	fActuator;
			BMessage			fActuatorArchive;
};


class ShortcutExecutor : public BLooper {
public:
								ShortcutExecutor();
	virtual						~ShortcutExecutor();

	virtual	void				MessageReceived(BMessage* message);
};


class SystemShortcuts : public SubFilter {
public:
								SystemShortcuts(FilterChain* owner);
	virtual						~SystemShortcuts();

	virtual	status_t			InitCheck();
	virtual	filter_result		Filter(BMessage* message, BList* outList);
	virtual	uint32				Stage() const { return kStageSemantic; }
	virtual	uint32				SupportedProfiles() const
									{ return kProfileDesktop
										| kProfileHybrid; }

	virtual	void				ReloadSettings();

			// Called from FilterChainController when the Shortcuts
			// preflet sends EXECUTE_COMMAND via the named port.
			// Thread-safe: acquires fBindingsLock internally.
			void				AddInject(BMessage* actuatorArchive);

private:
			void				_LoadSettings();
			void				_ClearBindings();
			void				_PutMessengerToPort();

			FilterChain*		fOwner;
			ShortcutExecutor*	fExecutor;
			BMessenger			fExecutorMessenger;

			BLocker				fBindingsLock;
			BObjectList<KeyBinding, true>	fBindings;
			BObjectList<BMessage, true>	fPendingInjects;

			BMessage			fLastMouseMessage;

			char*				fSettingsPath;
			node_ref			fSettingsNodeRef;
			port_id				fPort;
};

#endif
