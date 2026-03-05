/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 *
 * Input blocker: suppresses touchpad/touchscreen clicks while typing.
 * Desktop: blocks accidental touchpad taps during keyboard use.
 * Tablet: palm rejection while using on-screen keyboard (future).
 */
#ifndef INPUT_BLOCKER_H
#define INPUT_BLOCKER_H

#include "FilterChain.h"

#include <OS.h>


class InputBlocker : public SubFilter {
public:
								InputBlocker(FilterChain* owner);
	virtual						~InputBlocker();

	virtual	status_t			InitCheck();
	virtual	filter_result		Filter(BMessage* message, BList* outList);
	virtual	uint32				Stage() const { return kStageBlocking; }
	virtual	uint32				SupportedProfiles() const
									{ return kProfileAll; }

	virtual	void				ReloadSettings();

private:
			FilterChain*		fOwner;
			bigtime_t			fLastKeystroke;
			bigtime_t			fBlockThreshold;
			char*				fSettingsPath;
};

#endif
