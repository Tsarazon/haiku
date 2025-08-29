/*
 * Copyright 2005, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

// TODO: introduce means to define event stream features (like local vs. net)
// TODO: introduce the possibility to identify a stream by a unique name


#include "EventStream.h"
#include "InputManager.h"

#include <Autolock.h>


InputManager* gInputManager;
	// the global input manager will be created by the AppServer


InputManager::InputManager()
	:
	BLocker("input manager"),
	fFreeStreams(2),
	fUsedStreams(2)
{
}


InputManager::~InputManager()
{
}


bool
InputManager::AddStream(EventStream* stream)
{
	BAutolock _(this);
	return fFreeStreams.AddItem(stream);
}


void
InputManager::RemoveStream(EventStream* stream)
{
	BAutolock _(this);
	fFreeStreams.RemoveItem(stream);
}


EventStream*
InputManager::GetStream()
{
	BAutolock _(this);
	
	auto* stream = static_cast<EventStream*>(nullptr);
	do {
		delete stream;
			// this deletes the previous invalid stream

		stream = fFreeStreams.RemoveItemAt(0);
	} while (stream != nullptr && !stream->IsValid());

	if (stream == nullptr)
		return nullptr;

	fUsedStreams.AddItem(stream);
	return stream;
}


void
InputManager::PutStream(EventStream* stream)
{
	if (stream == nullptr)
		return;

	BAutolock _(this);

	fUsedStreams.RemoveItem(stream, false);
	if (stream->IsValid())
		fFreeStreams.AddItem(stream);
	else
		delete stream;
}


void
InputManager::UpdateScreenBounds(BRect bounds)
{
	BAutolock _(this);

	for (auto i = fUsedStreams.CountItems(); i-- > 0;) {
		fUsedStreams.ItemAt(i)->UpdateScreenBounds(bounds);
	}

	for (auto i = fFreeStreams.CountItems(); i-- > 0;) {
		fFreeStreams.ItemAt(i)->UpdateScreenBounds(bounds);
	}
}
