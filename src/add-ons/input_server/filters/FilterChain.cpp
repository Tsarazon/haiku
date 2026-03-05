/*
 * Copyright 2004-2025, Haiku/KosmOS.
 * Distributed under the terms of the MIT License.
 */

#include "FilterChain.h"
#include "InputBlocker.h"
#include "ScreenLockFilter.h"
#include "SystemShortcuts.h"
#include "WindowManagement.h"
#include "WorkspaceSwitch.h"

#include <new>

#include <Autolock.h>
#include <PathMonitor.h>

#include "ShortcutsFilterConstants.h"


// SubFilter

SubFilter::~SubFilter()
{
}


status_t
SubFilter::InitCheck()
{
	return B_OK;
}


filter_result
SubFilter::Filter(BMessage* /*message*/, BList* /*outList*/)
{
	return B_DISPATCH_MESSAGE;
}


void
SubFilter::ReloadSettings()
{
}


// FilterChain

extern "C" BInputServerFilter*
instantiate_input_filter()
{
	return new(std::nothrow) FilterChain();
}


FilterChain::FilterChain()
	:
	fFilters(8),
	fLock("filter chain lock"),
	fController(NULL),
	fActiveProfile(kProfileDesktop)
{
	fController = new(std::nothrow) FilterChainController(this);
	if (fController != NULL)
		fController->Run();

	// Stage 200: blocking
	_AddFilter(new(std::nothrow) InputBlocker(this));

	// Stage 400: semantic
	_AddFilter(new(std::nothrow) SystemShortcuts(this));
	_AddFilter(new(std::nothrow) WorkspaceSwitch());
	_AddFilter(new(std::nothrow) WindowManagement());

	// Stage 500: policy
	_AddFilter(new(std::nothrow) ScreenLockFilter(this));

	_SortFilters();
	_ApplyProfile();
}


FilterChain::~FilterChain()
{
	if (fController != NULL && fController->Lock())
		fController->Quit();
}


status_t
FilterChain::InitCheck()
{
	return fController != NULL ? B_OK : B_NO_MEMORY;
}


filter_result
FilterChain::Filter(BMessage* message, BList* outList)
{
	BAutolock _(fLock);

	filter_result result = B_DISPATCH_MESSAGE;

	for (int32 i = 0; i < fFilters.CountItems(); i++) {
		SubFilter* filter = fFilters.ItemAt(i);
		if (!filter->IsActive())
			continue;

		filter_result sub = filter->Filter(message, outList);
		if (sub == B_SKIP_MESSAGE) {
			result = B_SKIP_MESSAGE;
			break;
		}
	}

	return result;
}


void
FilterChain::SetProfile(InputProfile profile)
{
	BAutolock _(fLock);

	if (fActiveProfile == profile)
		return;

	fActiveProfile = profile;
	_ApplyProfile();
}


void
FilterChain::NotifySettingsChanged()
{
	BAutolock _(fLock);

	for (int32 i = 0; i < fFilters.CountItems(); i++)
		fFilters.ItemAt(i)->ReloadSettings();
}


void
FilterChain::_AddFilter(SubFilter* filter)
{
	if (filter == NULL)
		return;

	if (filter->InitCheck() != B_OK) {
		delete filter;
		return;
	}

	fFilters.AddItem(filter);
}


static int
CompareFilterStage(const SubFilter* a, const SubFilter* b)
{
	if (a->Stage() < b->Stage())
		return -1;
	if (a->Stage() > b->Stage())
		return 1;
	return 0;
}


void
FilterChain::_SortFilters()
{
	fFilters.SortItems(CompareFilterStage);
}


void
FilterChain::_ApplyProfile()
{
	for (int32 i = 0; i < fFilters.CountItems(); i++) {
		SubFilter* filter = fFilters.ItemAt(i);
		bool supported = (filter->SupportedProfiles() & fActiveProfile) != 0;
		filter->SetActive(supported);
	}
}


// FilterChainController

FilterChainController::FilterChainController(FilterChain* owner)
	:
	BLooper("filter chain controller", B_LOW_PRIORITY),
	fOwner(owner)
{
}


FilterChainController::~FilterChainController()
{
}


void
FilterChainController::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetInputProfile:
		{
			int32 profile;
			if (message->FindInt32("profile", &profile) == B_OK)
				fOwner->SetProfile((InputProfile)profile);
			break;
		}

		case B_PATH_MONITOR:
		{
			// Settings file changed — notify all sub-filters.
			fOwner->NotifySettingsChanged();
			break;
		}

		case EXECUTE_COMMAND:
		{
			// From Shortcuts preflet via named port.
			BMessage actuatorMsg;
			if (message->FindMessage("act", &actuatorMsg) == B_OK) {
				SystemShortcuts* shortcuts
					= fOwner->FindFilter<SystemShortcuts>();
				if (shortcuts != NULL)
					shortcuts->AddInject(&actuatorMsg);
			}
			break;
		}

		case REPLENISH_MESSENGER:
		{
			// Shortcuts preflet asks us to re-advertise our messenger.
			SystemShortcuts* shortcuts
				= fOwner->FindFilter<SystemShortcuts>();
			if (shortcuts != NULL)
				shortcuts->ReloadSettings();
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}
}
