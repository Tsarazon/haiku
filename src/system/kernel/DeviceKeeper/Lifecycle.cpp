/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Lifecycle.h"
#include "DeviceKeeper.h"
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

	dprintf("DeviceKeeper: ProbeAndAttach(%s): best driver %s "
		"(info=%p attach=%p probe=%p match=%p)\n",
		node->ModuleName(), driver->info.name,
		driver, driver->attach, driver->probe, driver->match);

	if (driver->attach == NULL) {
		dprintf("DeviceKeeper: ProbeAndAttach(%s): BUG — driver %s "
			"has NULL attach, skipping\n",
			node->ModuleName(), driver->info.name);
		MutexLocker nodeLocker(node->NodeLock());
		node->SetAttaching(false);
		return B_BAD_VALUE;
	}

	dprintf("DeviceKeeper: ProbeAndAttach: calling attach %p for %s on %s\n",
		driver->attach, driver->info.name, node->ModuleName());
	status = driver->attach(reinterpret_cast<dk_node*>(node), &cookie);
	dprintf("DeviceKeeper: ProbeAndAttach: attach returned %s for %s\n",
		strerror(status), node->ModuleName());

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

	// Probe children that were registered during attach() but
	// deferred because this node's cookie wasn't set yet.
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper != NULL)
		keeper->ProbePendingChildren(node);

	// a successful attach may unblock deferred nodes
	RetryDeferred();

	return B_OK;
}


status_t
DkLifecycle::ProbeAndAttachAll(DkNode* node)
{
	// For KOSM_FIND_MULTIPLE_CHILDREN nodes (like root):
	// find ALL matching drivers. For each, create a child node
	// under this node — child auto-attaches via ProbeAndAttach.
	// This gives each driver its own node with bus_ops, cookie,
	// and proper parent/child lifecycle.
	static const int32 kMaxDrivers = 64;
	DkMatchResult results[kMaxDrivers];
	int32 count = 0;

	status_t status = fMatcher.FindAllDrivers(node, results,
		kMaxDrivers, &count);
	if (status != B_OK || count == 0) {
		dprintf("DeviceKeeper: ProbeAndAttachAll(%s): no drivers found\n",
			node->ModuleName());
		return status;
	}

	dprintf("DeviceKeeper: ProbeAndAttachAll(%s): %d drivers matched\n",
		node->ModuleName(), (int)count);

	DkKeeper* keeper = DkKeeper::Instance();

	for (int32 i = 0; i < count; i++) {
		dk_driver_info* driver = results[i].driver;
		if (driver == NULL || driver->attach == NULL)
			continue;

		const char* driverName = driver->info.name;

		// Skip if a child with this module name already exists
		// (prevents duplicates on repeated DemandProbe calls)
		bool exists = false;
		{
			ReadLocker treeLocker(DkKeeper::Instance()->TreeLock());
			int32 childCount = 0;
			DkNodeList::Iterator it = node->Children().GetIterator();
			while (it.HasNext()) {
				DkNode* existing = it.Next();
				childCount++;
				dprintf("DeviceKeeper: dedup check: child[%d]=%s vs %s\n",
					(int)childCount, existing->ModuleName(), driverName);
				if (strcmp(existing->ModuleName(), driverName) == 0) {
					exists = true;
					break;
				}
			}
			if (childCount == 0)
				dprintf("DeviceKeeper: dedup check: %s has 0 children\n",
					node->ModuleName());
		}
		if (exists) {
			dprintf("DeviceKeeper: ProbeAndAttachAll: %s already exists "
				"under %s, skipping\n", driverName, node->ModuleName());
			continue;
		}

		dprintf("DeviceKeeper: ProbeAndAttachAll: creating child %s "
			"under %s\n", driverName, node->ModuleName());

		// Create child node with KOSM_FIND_CHILD_ON_DEMAND so
		// RegisterNode does NOT auto-probe (we attach directly).
		dk_property props[] = {
			{ KOSM_DEVICE_FLAGS, B_UINT32_TYPE,
				{ .ui32 = KOSM_FIND_CHILD_ON_DEMAND } },
			{}
		};

		DkNode* child = NULL;
		status_t s = keeper->RegisterNode(node, driverName,
			props, NULL, &child);
		if (s != B_OK) {
			dprintf("DeviceKeeper: ProbeAndAttachAll: RegisterNode(%s) "
				"failed: %s\n", driverName, strerror(s));
			continue;
		}

		// RegisterNode may have auto-attached via _PostRegisterProbe
		// (auto-attach by module name). If so, skip direct attach.
		if (child->IsAttached()) {
			dprintf("DeviceKeeper: ProbeAndAttachAll: %s already "
				"auto-attached by RegisterNode\n", driverName);
			if (child->HasKeepLoaded())
				child->Acquire();
			continue;
		}

		// Set driver on child BEFORE attach so that IsAttached()
		// returns true for get_bus_ops() lookups from grandchildren.
		// Also set attaching flag so that RegisterNode called from
		// within attach() defers fixed-child/auto-probe until the
		// cookie is set (otherwise get_bus_ops returns NULL cookie).
		{
			MutexLocker nodeLocker(child->NodeLock());
			child->SetDriver(driver, NULL);
			child->SetAttaching(true);
		}

		void* cookie = NULL;
		s = driver->attach(reinterpret_cast<dk_node*>(child), &cookie);

		// Set cookie (on success) or rollback driver (on failure)
		// in one locked step, then clear attaching.
		{
			MutexLocker nodeLocker(child->NodeLock());
			if (s == B_OK) {
				child->SetDriverCookie(cookie);
			} else {
				// Rollback: clear driver so IsAttached() returns false.
				// Without this, a failed attach leaves a zombie state
				// where IsAttached() is true but driver is not functional.
				child->ClearDriver();
			}
			child->SetAttaching(false);
		}

		if (s == B_OK) {
			dprintf("DeviceKeeper: ProbeAndAttachAll: %s attached\n",
				driverName);
			// Now that the cookie is set, probe children that were
			// registered during attach() but deferred (fixed-child,
			// auto-probe). This is the core fix for the PCI bus
			// manager initialization ordering.
			keeper->ProbePendingChildren(child);
		} else if (s == KOSM_DEFER_PROBE) {
			_AddDeferred(child);
			dprintf("DeviceKeeper: ProbeAndAttachAll: %s deferred\n",
				driverName);
		} else {
			dprintf("DeviceKeeper: ProbeAndAttachAll: %s attach failed: "
				"%s\n", driverName, strerror(s));
		}

		// Take keep-loaded ref if needed
		if (child->HasKeepLoaded() && child->IsAttached())
			child->Acquire();
	}

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

			// success — take keep-loaded ref if needed
			if (node->HasKeepLoaded())
				node->Acquire();

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
