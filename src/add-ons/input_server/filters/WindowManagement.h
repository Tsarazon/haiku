/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 *
 * Window management keyboard shortcuts.
 * Desktop-only:
 *   Ctrl+Cmd+M      - minimize all windows
 *   Ctrl+Cmd+Shift+M - bring all windows to front
 */
#ifndef WINDOW_MANAGEMENT_H
#define WINDOW_MANAGEMENT_H

#include "FilterChain.h"


class WindowManagement : public SubFilter {
public:
								WindowManagement();

	virtual	filter_result		Filter(BMessage* message, BList* outList);
	virtual	uint32				Stage() const { return kStageSemantic; }
	virtual	uint32				SupportedProfiles() const
									{ return kProfileDesktop
										| kProfileHybrid; }
};

#endif
