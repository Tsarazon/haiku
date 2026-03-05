/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "WorkspaceSwitch.h"

#include <InterfaceDefs.h>
#include <Message.h>

#include <AppServerLink.h>
#include <ServerProtocol.h>


WorkspaceSwitch::WorkspaceSwitch()
{
}


filter_result
WorkspaceSwitch::Filter(BMessage* message, BList* /*outList*/)
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

	bool takeMeThere;
	if (held == (B_COMMAND_KEY | B_CONTROL_KEY))
		takeMeThere = false;
	else if (held == (B_COMMAND_KEY | B_CONTROL_KEY | B_SHIFT_KEY))
		takeMeThere = true;
	else
		return B_DISPATCH_MESSAGE;

	int32 deltaX = 0;
	int32 deltaY = 0;

	switch (bytes[0]) {
		case B_LEFT_ARROW:	deltaX = -1; break;
		case B_RIGHT_ARROW:	deltaX = 1; break;
		case B_UP_ARROW:	deltaY = -1; break;
		case B_DOWN_ARROW:	deltaY = 1; break;
		default:
			return B_DISPATCH_MESSAGE;
	}

	// Query workspace layout from app_server.
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_WORKSPACE_LAYOUT);

	status_t status;
	int32 columns, rows;
	if (link.FlushWithReply(status) != B_OK || status != B_OK)
		return B_DISPATCH_MESSAGE;

	link.Read<int32>(&columns);
	link.Read<int32>(&rows);

	int32 current = current_workspace();

	int32 col = current % columns + deltaX;
	if (col >= columns)
		col = columns - 1;
	else if (col < 0)
		col = 0;

	int32 row = current / columns + deltaY;
	if (row >= rows)
		row = rows - 1;
	else if (row < 0)
		row = 0;

	int32 target = col + row * columns;
	if (target != current) {
		BPrivate::AppServerLink activate;
		activate.StartMessage(AS_ACTIVATE_WORKSPACE);
		activate.Attach<int32>(target);
		activate.Attach<bool>(takeMeThere);
		activate.Flush();
	}

	return B_SKIP_MESSAGE;
}
