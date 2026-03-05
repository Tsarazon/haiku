/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 *
 * Workspace switching via keyboard shortcuts.
 * Desktop-only: Ctrl+Cmd+Arrows to switch workspaces,
 * Ctrl+Cmd+Shift+Arrows to move current window to workspace.
 *
 * Communicates with app_server via AppServerLink to query
 * workspace layout and activate target workspace.
 */
#ifndef WORKSPACE_SWITCH_H
#define WORKSPACE_SWITCH_H

#include "FilterChain.h"


class WorkspaceSwitch : public SubFilter {
public:
								WorkspaceSwitch();

	virtual	filter_result		Filter(BMessage* message, BList* outList);
	virtual	uint32				Stage() const { return kStageSemantic; }
	virtual	uint32				SupportedProfiles() const
									{ return kProfileDesktop
										| kProfileHybrid; }
};

#endif
