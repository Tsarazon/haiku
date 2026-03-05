/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "WindowManagement.h"

#include <string.h>

#include <InterfaceDefs.h>
#include <Message.h>
#include <Roster.h>
#include <WindowInfo.h>

#include <tracker_private.h>


WindowManagement::WindowManagement()
{
}


filter_result
WindowManagement::Filter(BMessage* message, BList* /*outList*/)
{
	if (message->what != B_KEY_DOWN)
		return B_DISPATCH_MESSAGE;

	const char* bytes;
	if (message->FindString("bytes", &bytes) != B_OK)
		return B_DISPATCH_MESSAGE;

	int32 modifiers;
	if (message->FindInt32("modifiers", &modifiers) != B_OK)
		return B_DISPATCH_MESSAGE;

	int32 held = modifiers & (B_COMMAND_KEY | B_CONTROL_KEY
		| B_OPTION_KEY | B_MENU_KEY | B_SHIFT_KEY);

	bool minimize;
	if (held == (B_COMMAND_KEY | B_CONTROL_KEY))
		minimize = true;
	else if (held == (B_COMMAND_KEY | B_CONTROL_KEY | B_SHIFT_KEY))
		minimize = false;
	else
		return B_DISPATCH_MESSAGE;

	// Check for 'm' key. The raw_char is easier to match than bytes
	// (which depends on modifiers).
	int32 rawChar;
	if (message->FindInt32("raw_char", &rawChar) != B_OK)
		return B_DISPATCH_MESSAGE;

	if (rawChar != 'm' && rawChar != 'M')
		return B_DISPATCH_MESSAGE;

	// Iterate all running teams and minimize/restore.
	int32 cookie = 0;
	team_info teamInfo;
	while (get_next_team_info(&cookie, &teamInfo) == B_OK) {
		app_info appInfo;
		if (be_roster->GetRunningAppInfo(teamInfo.team, &appInfo) != B_OK)
			continue;

		if ((appInfo.flags & B_BACKGROUND_APP) != 0)
			continue;

		if (strcasecmp(appInfo.signature, kDeskbarSignature) == 0)
			continue;

		team_id team = appInfo.team;
		be_roster->ActivateApp(team);

		BRect zoomRect;
		if (minimize)
			do_minimize_team(zoomRect, team, false);
		else
			do_bring_to_front_team(zoomRect, team, false);
	}

	return B_SKIP_MESSAGE;
}
