/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Lifecycle.h"
#include "Matcher.h"
#include "Node.h"
#include "Publisher.h"

#include <new>

#include <KernelExport.h>
#include <util/AutoLock.h>


DkLifecycle::DkLifecycle(DkMatcher& matcher)
	:
	fMatcher(matcher),
	fPublisher(NULL)
{
	mutex_init(&fDeferredLock, "dk deferred");
}


DkLifecycle::~DkLifecycle()
{
	while (DkDeferredEntry* entry = fDeferred.RemoveHead())
		delete entry;
	mutex_destroy(&fDeferredLock);
}


void
DkLifecycle::SetPublisher(DkPublisher* publisher)
{
	fPublisher = publisher;
}


status_t
DkLifecycle::ProbeAndAttach(DkNode* node)
{
	// Mark node as attaching before the probe so that a concurrent
	// Detach() will wait. This must precede FindBestDriver: if
	// Detach hits the window between FindBestDriver and attach(),
	// it would miss the attach entirely and leave a zombie driver.
	{
		MutexLocker nodeLocker(node->NodeLock());
		if (node->IsAttached())
			return B_BUSY;
		node->SetAttaching(true);
	}

	dk_driver_info* driver = NULL;
	status_t status = fMatcher.FindBestDriver(node, &driver);
	if (status != B_OK) {
		MutexLocker nodeLocker(node->NodeLock());
		node->SetAttaching(false);
		return status;
	}

	// attach() is called WITHOUT any DeviceKeeper locks held.
	// The driver may freely call register_node, publish_device, etc.
	void* cookie = NULL;
	status = driver->attach(reinterpret_cast<dk_node*>(node), &cookie);

	// Clear attaching and set driver (on success) in one locked step.
	{
		MutexLocker nodeLocker(node->NodeLock());
		if (status == B_OK)
			node->SetDriver(driver, cookie);
		node->SetAttaching(false);
	}

	if (status == KOSM_DEFER_PROBE) {
		_AddDeferred(node);
		dprintf("DeviceKeeper: deferred probe for %s\n",
			node->ModuleName());
		return status;
	}

	if (status != B_OK) {
		dprintf("DeviceKeeper: attach failed for %s: %s\n",
			node->ModuleName(), strerror(status));
		return status;
	}

	// a successful attach may unblock deferred nodes
	RetryDeferred();

	return B_OK;
}


void
DkLifecycle::RetryDeferred()
{
	// Each successful attach may satisfy a dependency for a deferred
	// node. Loop until no more progress is made (fixed-point).
	//
	// attach() is called WITHOUT fDeferredLock held because drivers
	// may recursively call register_node → ProbeAndAttach → _AddDeferred.
	// Strategy: move all entries to a local list under lock, process
	// without lock, put remaining entries back under lock.
	bool progress = true;
	while (progress) {
		progress = false;

		DeferredList local;
		{
			MutexLocker locker(&fDeferredLock);
			while (DkDeferredEntry* e = fDeferred.RemoveHead())
				local.Add(e);
		}

		if (local.IsEmpty())
			break;

		DkDeferredEntry* entry = local.Head();
		while (entry != NULL) {
			DkDeferredEntry* next = local.GetNext(entry);
			DkNode* node = entry->node;

			bool alreadyAttached;
			{
				MutexLocker nodeLocker(node->NodeLock());
				alreadyAttached = node->IsAttached();
				if (!alreadyAttached)
					node->SetAttaching(true);
			}

			if (alreadyAttached) {
				local.Remove(entry);
				node->Release();
				delete entry;
				entry = next;
				continue;
			}

			dk_driver_info* driver = NULL;
			status_t status = fMatcher.FindBestDriver(node, &driver);
			if (status != B_OK) {
				MutexLocker nodeLocker(node->NodeLock());
				node->SetAttaching(false);
				entry = next;
				continue;
			}

			void* cookie = NULL;
			status = driver->attach(reinterpret_cast<dk_node*>(node),
				&cookie);

			{
				MutexLocker nodeLocker(node->NodeLock());
				if (status == B_OK)
					node->SetDriver(driver, cookie);
				node->SetAttaching(false);
			}

			if (status == KOSM_DEFER_PROBE) {
				entry->retries++;
				if (entry->retries >= kMaxDeferRetries) {
					dprintf("DeviceKeeper: giving up on deferred "
						"probe for %s after %" B_PRId32 " retries\n",
						node->ModuleName(), entry->retries);
					local.Remove(entry);
					node->Release();
					delete entry;
				}
				entry = next;
				continue;
			}

			if (status != B_OK) {
				dprintf("DeviceKeeper: deferred attach failed for "
					"%s: %s\n", node->ModuleName(), strerror(status));
				local.Remove(entry);
				node->Release();
				delete entry;
				entry = next;
				continue;
			}

			// success
			local.Remove(entry);
			node->Release();
			delete entry;

			progress = true;
			entry = next;
		}

		// put unresolved entries back
		if (!local.IsEmpty()) {
			MutexLocker locker(&fDeferredLock);
			while (DkDeferredEntry* remaining = local.RemoveHead())
				fDeferred.Add(remaining);
		}
	}
}


void
DkLifecycle::Detach(DkNode* node)
{
	_RemoveDeferred(node);

	// If a concurrent ProbeAndAttach or RetryDeferred is currently
	// inside driver->attach() for this node, wait for it to finish.
	// Without this, we could return with the node still unattached
	// while attach() succeeds a moment later, leaving a zombie driver.
	node->WaitAttachDone();

	_DetachRecursive(node);
}


void
DkLifecycle::_DetachRecursive(DkNode* node)
{
	// bottom-up: detach children first
	DkNodeList::Iterator it = node->Children().GetIterator();
	while (it.HasNext()) {
		DkNode* child = it.Next();
		_DetachRecursive(child);
	}

	if (!node->IsAttached())
		return;

	// unpublish all devices for this node
	if (fPublisher != NULL)
		fPublisher->UnpublishAll(node);

	MutexLocker nodeLocker(node->NodeLock());

	dk_driver_info* driver = node->DriverInfo();
	void* cookie = node->DriverCookie();
	node->ClearDriver();

	nodeLocker.Unlock();

	// detach() is called WITHOUT any DeviceKeeper locks held.
	// The driver may freely call unregister_node, unpublish_device, etc.
	if (driver->detach != NULL)
		driver->detach(cookie);
}


status_t
DkLifecycle::Rescan(DkNode* node)
{
	if (!node->IsAttached())
		return B_NO_INIT;

	dk_driver_info* driver = node->DriverInfo();
	if (driver->rescan_children == NULL)
		return B_UNSUPPORTED;

	return driver->rescan_children(node->DriverCookie());
}


status_t
DkLifecycle::Suspend(DkNode* node, int32 state)
{
	// bottom-up: suspend children first
	DkNodeList::Iterator it = node->Children().GetIterator();
	while (it.HasNext()) {
		DkNode* child = it.Next();
		status_t status = Suspend(child, state);
		if (status != B_OK)
			return status;
	}

	if (!node->IsAttached())
		return B_OK;

	dk_driver_info* driver = node->DriverInfo();
	if (driver->suspend == NULL)
		return B_OK;

	status_t status = driver->suspend(node->DriverCookie(), state);
	if (status != B_OK) {
		dprintf("DeviceKeeper: suspend failed for %s: %s\n",
			node->ModuleName(), strerror(status));
		return status;
	}

	MutexLocker nodeLocker(node->NodeLock());
	node->SetPowerState(DkNode::kPowerSuspended);

	return B_OK;
}


status_t
DkLifecycle::Resume(DkNode* node)
{
	// top-down: resume node first, then children
	if (node->IsAttached()
		&& node->PowerState() == DkNode::kPowerSuspended) {
		dk_driver_info* driver = node->DriverInfo();
		if (driver->resume != NULL) {
			status_t status = driver->resume(node->DriverCookie());
			if (status != B_OK) {
				dprintf("DeviceKeeper: resume failed for %s: %s\n",
					node->ModuleName(), strerror(status));
				return status;
			}
		}

		MutexLocker nodeLocker(node->NodeLock());
		node->SetPowerState(DkNode::kPowerActive);
	}

	DkNodeList::Iterator it = node->Children().GetIterator();
	while (it.HasNext()) {
		DkNode* child = it.Next();
		status_t status = Resume(child);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


void
DkLifecycle::NotifyRemoved(DkNode* node)
{
	_NotifyRemovedRecursive(node);
}


void
DkLifecycle::_NotifyRemovedRecursive(DkNode* node)
{
	// bottom-up: children first
	DkNodeList::Iterator it = node->Children().GetIterator();
	while (it.HasNext())
		_NotifyRemovedRecursive(it.Next());

	if (fPublisher != NULL)
		fPublisher->NotifyDeviceRemoved(node);
}


void
DkLifecycle::_AddDeferred(DkNode* node)
{
	MutexLocker locker(&fDeferredLock);

	// avoid duplicates
	DkDeferredEntry* entry = fDeferred.Head();
	while (entry != NULL) {
		if (entry->node == node)
			return;
		entry = fDeferred.GetNext(entry);
	}

	entry = new(std::nothrow) DkDeferredEntry();
	if (entry == NULL)
		return;

	entry->node = node;
	entry->retries = 0;
	node->Acquire();
	fDeferred.Add(entry);
}


void
DkLifecycle::_RemoveDeferred(DkNode* node)
{
	MutexLocker locker(&fDeferredLock);

	DkDeferredEntry* entry = fDeferred.Head();
	while (entry != NULL) {
		DkDeferredEntry* next = fDeferred.GetNext(entry);
		if (entry->node == node) {
			fDeferred.Remove(entry);
			locker.Unlock();
			node->Release();
			delete entry;
			return;
		}
		entry = next;
	}
}
