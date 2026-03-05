/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 *
 * Unified filter chain: one BInputServerFilter add-on containing
 * all built-in input filters, ordered by processing stage.
 *
 * Input profiles allow filters to declare whether they are relevant
 * for desktop (keyboard+mouse), tablet (touchscreen), or both.
 * The active profile is determined at runtime by which input devices
 * are currently connected.
 */
#ifndef FILTER_CHAIN_H
#define FILTER_CHAIN_H

#include <InputServerFilter.h>
#include <Locker.h>
#include <Looper.h>
#include <ObjectList.h>


// Processing stages, executed in ascending order.
// A filter at a lower stage sees the event before higher stages.
enum FilterStage {
	kStageBlocking		= 200,	// palm rejection, input blocking
	kStageRecognition	= 300,	// gesture recognition (future)
	kStageSemantic		= 400,	// shortcuts, workspace switching, window mgmt
	kStagePolicy		= 500,	// screen lock, power management
};

// Input profiles. A filter declares which profiles it supports.
// The chain activates/deactivates filters when the profile changes.
enum InputProfile {
	kProfileDesktop		= 0x01,	// keyboard + mouse present
	kProfileTablet		= 0x02,	// touchscreen, no physical keyboard
	kProfileHybrid		= 0x03,	// both (e.g. Surface-like device)
	kProfileAll			= 0xFF,
};


class FilterChainController;


class SubFilter {
public:
	virtual						~SubFilter();

	virtual	status_t			InitCheck();
	virtual	filter_result		Filter(BMessage* message, BList* outList);

	virtual	uint32				Stage() const = 0;
	virtual	uint32				SupportedProfiles() const
									{ return kProfileAll; }

	// Called when the sub-filter's settings file changes.
	// Default implementation does nothing.
	virtual	void				ReloadSettings();

			bool				IsActive() const { return fActive; }
			void				SetActive(bool active) { fActive = active; }

private:
			bool				fActive = false;
};


class FilterChain : public BInputServerFilter {
public:
								FilterChain();
	virtual						~FilterChain();

	virtual	status_t			InitCheck();
	virtual	filter_result		Filter(BMessage* message, BList* outList);

			FilterChainController*	Controller() const
									{ return fController; }

			void				SetProfile(InputProfile profile);
			InputProfile		ActiveProfile() const
									{ return fActiveProfile; }

			// Calls ReloadSettings() on all sub-filters.
			void				NotifySettingsChanged();

			// Find a sub-filter by dynamic_cast.
			template<typename T>
			T*					FindFilter() const;

private:
			void				_AddFilter(SubFilter* filter);
			void				_SortFilters();
			void				_ApplyProfile();

			BObjectList<SubFilter, true>	fFilters;
			BLocker				fLock;
			FilterChainController*	fController;
			InputProfile		fActiveProfile;
};


// Messages handled by FilterChainController.
enum {
	// Sent by device add-on when input devices change.
	// Contains "profile" (int32) with the new InputProfile value.
	kMsgSetInputProfile		= 'FCsp',

	// Sent by sub-filters when they want to inject events.
	// Contains "inject" (BMessage) with the actuator archive.
	kMsgInjectEvent			= 'FCie',
};


class FilterChainController : public BLooper {
public:
								FilterChainController(FilterChain* owner);
	virtual						~FilterChainController();

	virtual	void				MessageReceived(BMessage* message);

			FilterChain*		Owner() const { return fOwner; }

private:
			FilterChain*		fOwner;
};


extern "C" BInputServerFilter* instantiate_input_filter();


template<typename T>
T*
FilterChain::FindFilter() const
{
	for (int32 i = 0; i < fFilters.CountItems(); i++) {
		T* filter = dynamic_cast<T*>(fFilters.ItemAt(i));
		if (filter != NULL)
			return filter;
	}
	return NULL;
}

#endif
