/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Lifecycle.h"
#include "DeviceKeeper.h"
#include "LockDebug.h"
#include "Matcher.h"
#include "Node.h"
#include "Publisher.h"

#include <new>
#include <stdlib.h>

#include <KernelExport.h>
#include <OS.h>
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
DkLifecycle::_AttachDriverToNode(DkNode* node, DkModuleRef& _ref)
{
	// Unified attach helper. Precondition: fAttaching was set to true
	// by the caller (so concurrent Detach waits for us). The caller
	// also passes an owned module reference via _ref.
	//
	// This helper is called from:
	//   - ProbeAndAttach (via FindBestDriver)
	//   - AutoAttachByName (via _PostRegisterProbe auto-attach)
	//   - RetryDeferred (via FindBestDriver on retry)
	//   - ProbeAndAttachAll (per child, via FindAllDrivers)
	//
	// All four share the same state transitions:
	//   1. call driver->attach() WITHOUT any DK locks
	//   2. under node lock: set driver+cookie on success, clear on
	//      failure, clear fAttaching
	//   3. on KOSM_DEFER_PROBE: queue node, release ref (fresh one on retry)
	//   4. on B_OK: ProbePendingChildren + RetryDeferred
	//
	// DK_ASSERT_NO_LOCKS_HELD guards the attach call — in debug builds
	// any caller that accidentally holds a DK lock will panic here
	// rather than deadlock or corrupt state.

	dk_driver_info* driver = _ref.Get();
	if (driver == NULL || driver->attach == NULL) {
		MutexLocker nodeLocker(node->NodeLock());
		node->SetAttaching(false);
		return B_BAD_VALUE;
	}

	DK_ASSERT_NO_LOCKS_HELD();

	void* cookie = NULL;
	DK_TRACE("_AttachDriverToNode: calling attach %p for %s on %s\n",
		driver->attach, driver->info.name, node->ModuleName());
	status_t status = driver->attach(
		reinterpret_cast<dk_node*>(node), &cookie);
	DK_TRACE("_AttachDriverToNode: attach returned %s for %s\n",
		strerror(status), node->ModuleName());

	// Clear attaching, set driver (on success) in one locked step.
	{
		MutexLocker nodeLocker(node->NodeLock());
		if (status == B_OK) {
			// Node takes ownership of the module reference.
			node->SetDriver(driver, cookie);
			_ref.Detach();
		}
		node->SetAttaching(false);
	}

	if (status == KOSM_DEFER_PROBE) {
		// _ref dtor releases the module reference — RetryDeferred
		// will take a fresh one via FindBestDriver.
		_AddDeferred(node);
		DK_INFO("deferred probe for %s\n", node->ModuleName());
		return status;
	}

	if (status != B_OK) {
		// _ref dtor releases the module reference automatically.
		DK_ERROR("attach failed for %s: %s\n",
			node->ModuleName(), strerror(status));
		return status;
	}

	// Probe children registered during attach() but deferred because
	// this node's cookie wasn't set yet.
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper != NULL)
		keeper->ProbePendingChildren(node);

	// Keep-loaded: extra reference so the node stays live even when
	// no external consumers hold refs.
	if (node->HasKeepLoaded())
		node->Acquire();

	// Successful attach may unblock deferred nodes.
	RetryDeferred();

	return B_OK;
}


status_t
DkLifecycle::AutoAttachByName(DkNode* node, DkModuleRef& _ref)
{
	// Called by DkKeeper::_PostRegisterProbe when a registered node's
	// own module name matches dk_driver_v1 suffix and loads as a
	// driver. The caller has already taken the module reference and
	// passes it via _ref.
	//
	// We must set fAttaching under the node lock ourselves, because
	// _PostRegisterProbe does not (unlike ProbeAndAttach which always
	// sets it before calling us via the unified helper).
	{
		MutexLocker nodeLocker(node->NodeLock());
		if (node->IsAttached())
			return B_BUSY;
		node->SetAttaching(true);
	}
	return _AttachDriverToNode(node, _ref);
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

	DkModuleRef ref;
	status_t status = fMatcher.FindBestDriver(node, ref);
	if (status != B_OK) {
		MutexLocker nodeLocker(node->NodeLock());
		node->SetAttaching(false);
		return status;
	}

	DK_TRACE("ProbeAndAttach(%s): best driver %s "
		"(attach=%p probe=%p match=%p)\n",
		node->ModuleName(), ref->info.name,
		ref->attach, ref->probe, ref->match);

	return _AttachDriverToNode(node, ref);
}


status_t
DkLifecycle::ProbeAndAttachAll(DkNode* node)
{
	// For KOSM_FIND_MULTIPLE_CHILDREN nodes (like root):
	// find ALL matching drivers. For each, create a child node
	// under this node — child auto-attaches via ProbeAndAttach.
	// This gives each driver its own node with interface ops, cookie,
	// and proper parent/child lifecycle.
	static const int32 kMaxDrivers = 64;
	DkMatchResult results[kMaxDrivers];
	int32 count = 0;

	status_t status = fMatcher.FindAllDrivers(node, results,
		kMaxDrivers, &count);
	if (status != B_OK || count == 0) {
		DK_TRACE("ProbeAndAttachAll(%s): no drivers found\n",
			node->ModuleName());
		return status;
	}

	DK_TRACE("ProbeAndAttachAll(%s): %d drivers matched\n",
		node->ModuleName(), (int)count);

	DkKeeper* keeper = DkKeeper::Instance();

	for (int32 i = 0; i < count; i++) {
		// Wrap each result's module ref in DkModuleRef immediately so
		// that any subsequent early-continue automatically releases it.
		DkModuleRef ref(results[i].driver);
		if (!ref.IsSet() || ref->attach == NULL)
			continue;

		const char* driverName = ref->info.name;

		// Skip if a child with this module name already exists
		// (prevents duplicates on repeated DemandProbe calls)
		bool exists = false;
		{
			ReadLocker treeLocker(DkKeeper::Instance()->TreeLock());
			DkNodeList::Iterator it = node->Children().GetIterator();
			while (it.HasNext()) {
				DkNode* existing = it.Next();
				if (strcmp(existing->ModuleName(), driverName) == 0) {
					exists = true;
					break;
				}
			}
		}
		if (exists) {
			DK_TRACE("ProbeAndAttachAll: %s already exists "
				"under %s, skipping\n", driverName, node->ModuleName());
			continue;
		}

		DK_TRACE("ProbeAndAttachAll: creating child %s "
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
			DK_ERROR("ProbeAndAttachAll: RegisterNode(%s) "
				"failed: %s\n", driverName, strerror(s));
			continue;
		}

		// RegisterNode may have auto-attached via _PostRegisterProbe
		// (auto-attach by module name). If so, skip direct attach —
		// _PostRegisterProbe already took and owns its own module ref.
		// Our DkModuleRef dtor drops the duplicate FindAllDrivers ref.
		if (child->IsAttached()) {
			DK_TRACE("ProbeAndAttachAll: %s already "
				"auto-attached by RegisterNode\n", driverName);
			continue;
		}

		// Mark attaching + delegate to unified attach helper.
		{
			MutexLocker nodeLocker(child->NodeLock());
			child->SetAttaching(true);
		}
		_AttachDriverToNode(child, ref);
		// ref ownership may have transferred into child; ref dtor
		// handles whatever case we ended up in.
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
	// may recursively call register_node -> ProbeAndAttach -> _AddDeferred.
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

		const bigtime_t now = system_time();

		DkDeferredEntry* entry = local.Head();
		while (entry != NULL) {
			DkDeferredEntry* next = local.GetNext(entry);
			DkNode* node = entry->node;

			// Watchdog: emit a WARN once per entry if it has been
			// stuck in the queue longer than kDeferWatchdogTimeout.
			// We mark the warning by clamping retries to a sentinel
			// so it only prints once per lifetime of the entry.
			if (entry->enqueue_time > 0
					&& now - entry->enqueue_time > kDeferWatchdogTimeout
					&& entry->retries < kMaxDeferRetries) {
				DK_ERROR("WATCHDOG: deferred probe for %s stuck for "
					"%" B_PRId64 "ms (retries=%" B_PRId32 ")\n",
					node->ModuleName(),
					(now - entry->enqueue_time) / 1000,
					entry->retries);
				// reset enqueue_time so we don't spam every retry
				entry->enqueue_time = now;
			}

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

			DkModuleRef ref;
			status_t status = fMatcher.FindBestDriver(node, ref);
			if (status != B_OK) {
				MutexLocker nodeLocker(node->NodeLock());
				node->SetAttaching(false);
				entry = next;
				continue;
			}

			// _AttachDriverToNode handles set/clear driver, KOSM_DEFER_PROBE
			// queueing, ProbePendingChildren, and ref ownership.
			status = _AttachDriverToNode(node, ref);

			if (status == KOSM_DEFER_PROBE) {
				// _AttachDriverToNode re-queued the node via _AddDeferred,
				// which adds a FRESH entry. We still have the OLD entry
				// in local — drop it with a retry counter bump.
				// The new entry inherits enqueue_time = now, which is
				// correct for watchdog purposes (forward progress).
				entry->retries++;
				if (entry->retries >= kMaxDeferRetries) {
					DK_ERROR("giving up on deferred "
						"probe for %s after %" B_PRId32 " retries\n",
						node->ModuleName(), entry->retries);
					// _AttachDriverToNode already re-added — but we need
					// to remove THAT entry to actually give up.
					_RemoveDeferred(node);
				}
				local.Remove(entry);
				node->Release();
				delete entry;
				entry = next;
				continue;
			}

			if (status != B_OK) {
				DK_ERROR("deferred attach failed for "
					"%s: %s\n", node->ModuleName(), strerror(status));
				local.Remove(entry);
				node->Release();
				delete entry;
				entry = next;
				continue;
			}

			// success — _AttachDriverToNode already took keep-loaded ref
			// and probed pending children. Just drop the deferred entry.
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
	// Snapshot children under tree read lock, then recurse without
	// locks held. This prevents iterator invalidation if a concurrent
	// RegisterNode modifies the children list between iterations.
	// Each child is Acquired to prevent deletion during processing.
	static const int32 kStackMax = 64;
	DkNode* stackBuf[kStackMax];
	DkNode** children = stackBuf;
	int32 count = 0;

	{
		DkKeeper* keeper = DkKeeper::Instance();
		ReadLocker treeLocker(keeper->TreeLock());

		DkNodeList::Iterator it = node->Children().GetIterator();
		while (it.HasNext()) {
			it.Next();
			count++;
		}

		if (count > 0) {
			if (count > kStackMax) {
				children = (DkNode**)malloc(count * sizeof(DkNode*));
				if (children == NULL) {
					children = stackBuf;
					DK_ERROR("_DetachRecursive(%s): "
						"OOM for %" B_PRId32 " children, processing "
						"first %d only\n",
						node->ModuleName(), count, kStackMax);
				}
			}

			int32 max = (children == stackBuf) ? kStackMax : count;
			int32 i = 0;
			it = node->Children().GetIterator();
			while (it.HasNext() && i < max) {
				DkNode* child = it.Next();
				child->Acquire();
				children[i++] = child;
			}
			count = i;
		}
	}

	// bottom-up: detach children first (without locks held)
	for (int32 i = 0; i < count; i++) {
		// Wait for any in-progress attach on this child before
		// checking IsAttached(). Without this, a concurrent
		// ProbeAndAttach could complete after our check, leaving
		// the child with an attached driver but detached parent.
		children[i]->WaitAttachDone();
		_DetachRecursive(children[i]);
		children[i]->Release();
	}

	if (children != stackBuf)
		free(children);

	if (!node->IsAttached())
		return;

	// unpublish all devices for this node
	if (fPublisher != NULL)
		fPublisher->UnpublishAll(node);

	MutexLocker nodeLocker(node->NodeLock());

	dk_driver_info* driver = node->DriverInfo();
	void* cookie = node->DriverCookie();
	bool hadRef = node->HasDriverRef();
	node->ClearDriver();
	// Drop the "holds module ref" accounting flag. The actual
	// put_module call happens below (without locks).
	node->ReleaseDriverRef();

	nodeLocker.Unlock();

	// detach() is called WITHOUT any DeviceKeeper locks held.
	// The driver may freely call unregister_node, unpublish_device, etc.
	DK_ASSERT_NO_LOCKS_HELD();
	if (driver->detach != NULL)
		driver->detach(cookie);

	// Release the module reference acquired by FindBestDriver/
	// FindAllDrivers or _PostRegisterProbe's get_module().
	// This balances the get_module() that was taken when the
	// driver was first matched and attached to this node.
	if (hadRef)
		put_module(driver->info.name);
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
	// Snapshot children under tree read lock to avoid iterator
	// invalidation from concurrent RegisterNode.
	static const int32 kStackMax = 64;
	DkNode* stackBuf[kStackMax];
	DkNode** children = stackBuf;
	int32 count = 0;

	{
		DkKeeper* keeper = DkKeeper::Instance();
		ReadLocker treeLocker(keeper->TreeLock());

		DkNodeList::Iterator it = node->Children().GetIterator();
		while (it.HasNext()) {
			it.Next();
			count++;
		}

		if (count > 0) {
			if (count > kStackMax) {
				children = (DkNode**)malloc(count * sizeof(DkNode*));
				if (children == NULL) {
					// Partial suspend is dangerous: unsuspended
					// children may keep operating on hardware that
					// higher layers expect to be powered down. Fail
					// the entire suspend rather than leave an
					// inconsistent state.
					DK_ERROR("Suspend(%s): OOM for "
						"%" B_PRId32 " children, aborting\n",
						node->ModuleName(), count);
					return B_NO_MEMORY;
				}
			}

			int32 i = 0;
			it = node->Children().GetIterator();
			while (it.HasNext() && i < count) {
				DkNode* child = it.Next();
				child->Acquire();
				children[i++] = child;
			}
			count = i;
		}
	}

	// bottom-up: suspend children first
	for (int32 i = 0; i < count; i++) {
		status_t status = Suspend(children[i], state);
		if (status != B_OK) {
			// Release remaining snapshot refs
			for (int32 j = i; j < count; j++)
				children[j]->Release();
			if (children != stackBuf)
				free(children);
			return status;
		}
		children[i]->Release();
	}

	if (children != stackBuf)
		free(children);

	if (!node->IsAttached())
		return B_OK;

	dk_driver_info* driver = node->DriverInfo();
	if (driver->suspend == NULL)
		return B_OK;

	status_t status = driver->suspend(node->DriverCookie(), state);
	if (status != B_OK) {
		DK_ERROR("suspend failed for %s: %s\n",
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
				DK_ERROR("resume failed for %s: %s\n",
					node->ModuleName(), strerror(status));
				return status;
			}
		}

		MutexLocker nodeLocker(node->NodeLock());
		node->SetPowerState(DkNode::kPowerActive);
	}

	// Snapshot children under tree read lock
	static const int32 kStackMax = 64;
	DkNode* stackBuf[kStackMax];
	DkNode** children = stackBuf;
	int32 count = 0;

	{
		DkKeeper* keeper = DkKeeper::Instance();
		ReadLocker treeLocker(keeper->TreeLock());

		DkNodeList::Iterator it = node->Children().GetIterator();
		while (it.HasNext()) {
			it.Next();
			count++;
		}

		if (count > 0) {
			if (count > kStackMax) {
				children = (DkNode**)malloc(count * sizeof(DkNode*));
				if (children == NULL) {
					// Skipping children would leave them suspended
					// while the parent is active. Fail so the caller
					// can retry when memory is available.
					DK_ERROR("Resume(%s): OOM for "
						"%" B_PRId32 " children, aborting\n",
						node->ModuleName(), count);
					return B_NO_MEMORY;
				}
			}

			int32 i = 0;
			it = node->Children().GetIterator();
			while (it.HasNext() && i < count) {
				DkNode* child = it.Next();
				child->Acquire();
				children[i++] = child;
			}
			count = i;
		}
	}

	for (int32 i = 0; i < count; i++) {
		status_t status = Resume(children[i]);
		if (status != B_OK) {
			for (int32 j = i; j < count; j++)
				children[j]->Release();
			if (children != stackBuf)
				free(children);
			return status;
		}
		children[i]->Release();
	}

	if (children != stackBuf)
		free(children);

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
	// Snapshot children under tree read lock, then recurse without
	// locks held. Same pattern as _DetachRecursive — prevents
	// iterator invalidation from concurrent RegisterNode.
	static const int32 kStackMax = 64;
	DkNode* stackBuf[kStackMax];
	DkNode** children = stackBuf;
	int32 count = 0;

	{
		DkKeeper* keeper = DkKeeper::Instance();
		ReadLocker treeLocker(keeper->TreeLock());

		DkNodeList::Iterator it = node->Children().GetIterator();
		while (it.HasNext()) {
			it.Next();
			count++;
		}

		if (count > 0) {
			if (count > kStackMax) {
				children = (DkNode**)malloc(count * sizeof(DkNode*));
				if (children == NULL) {
					children = stackBuf;
					DK_ERROR("_NotifyRemovedRecursive(%s): "
						"OOM for %" B_PRId32 " children, processing "
						"first %d only\n",
						node->ModuleName(), count, kStackMax);
				}
			}

			int32 max = (children == stackBuf) ? kStackMax : count;
			int32 i = 0;
			it = node->Children().GetIterator();
			while (it.HasNext() && i < max) {
				DkNode* child = it.Next();
				child->Acquire();
				children[i++] = child;
			}
			count = i;
		}
	}

	// bottom-up: children first
	for (int32 i = 0; i < count; i++) {
		_NotifyRemovedRecursive(children[i]);
		children[i]->Release();
	}

	if (children != stackBuf)
		free(children);

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
	entry->enqueue_time = system_time();
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
