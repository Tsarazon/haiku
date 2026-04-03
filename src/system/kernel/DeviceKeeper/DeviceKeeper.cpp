/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceKeeper.h"
#include "FirmwareLoader.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <KernelExport.h>
#include <kmodule.h>
#include <util/AutoLock.h>

#include "IOSchedulerRoster.h"


// DkResourceAllocator

DkResourceAllocator::DkResourceAllocator()
{
	mutex_init(&fLock, "dk resources");
}


DkResourceAllocator::~DkResourceAllocator()
{
	while (DkResourceEntry* e = fMemory.RemoveHead())
		delete e;
	while (DkResourceEntry* e = fPorts.RemoveHead())
		delete e;
	while (DkResourceEntry* e = fDmaChannels.RemoveHead())
		delete e;
	mutex_destroy(&fLock);
}


/*static*/ bool
DkResourceAllocator::_Overlaps(const DkResourceEntry* existing,
	generic_addr_t base, generic_addr_t length)
{
	// correct overlap: a.start < b.end && b.start < a.end
	return existing->base < base + length
		&& base < existing->base + existing->length;
}


DkResourceAllocator::ResourceList&
DkResourceAllocator::_ListFor(uint32 type)
{
	switch (type) {
		case KOSM_RESOURCE_IO_PORT:			return fPorts;
		case KOSM_RESOURCE_DMA_CHANNEL:		return fDmaChannels;
		default:							return fMemory;
	}
}


status_t
DkResourceAllocator::Acquire(uint32 type, generic_addr_t base,
	generic_addr_t length)
{
	MutexLocker locker(&fLock);

	if (type < KOSM_RESOURCE_IO_MEMORY || type > KOSM_RESOURCE_DMA_CHANNEL)
		return B_BAD_VALUE;
	if (length == 0)
		return B_BAD_VALUE;
	if (type == KOSM_RESOURCE_DMA_CHANNEL && base > 7)
		return B_BAD_VALUE;
	if (type == KOSM_RESOURCE_IO_PORT) {
		if ((uint16)base != base || (uint16)length != length
			|| base + length <= base) {
			return B_BAD_VALUE;
		}
	}

	ResourceList& list = _ListFor(type);

	ResourceList::Iterator it = list.GetIterator();
	while (it.HasNext()) {
		DkResourceEntry* existing = it.Next();
		if (_Overlaps(existing, base, length))
			return B_RESOURCE_UNAVAILABLE;
	}

	DkResourceEntry* entry = new(std::nothrow) DkResourceEntry();
	if (entry == NULL)
		return B_NO_MEMORY;

	entry->type = type;
	entry->base = base;
	entry->length = length;

	list.Add(entry);
	return B_OK;
}


void
DkResourceAllocator::Release(uint32 type, generic_addr_t base)
{
	MutexLocker locker(&fLock);

	if (type < KOSM_RESOURCE_IO_MEMORY || type > KOSM_RESOURCE_DMA_CHANNEL)
		return;

	ResourceList& list = _ListFor(type);
	ResourceList::Iterator it = list.GetIterator();
	while (it.HasNext()) {
		DkResourceEntry* entry = it.Next();
		if (entry->base == base) {
			it.Remove();
			delete entry;
			return;
		}
	}
}


// DkIdGenerator

static const uint32 kInitialCapacity = 64;


DkIdGenerator::Generator::Generator(const char* _name)
	:
	name(strdup(_name)),
	bitmap(NULL),
	capacity(0),
	count(0),
	nextHint(0)
{
	bitmap = (uint8*)calloc(kInitialCapacity / 8, 1);
	if (bitmap != NULL)
		capacity = kInitialCapacity;
}


DkIdGenerator::Generator::~Generator()
{
	free(name);
	free(bitmap);
}


status_t
DkIdGenerator::Generator::Grow()
{
	uint32 newCapacity = capacity * 2;
	uint8* newBitmap = (uint8*)realloc(bitmap, newCapacity / 8);
	if (newBitmap == NULL)
		return B_NO_MEMORY;

	memset(newBitmap + capacity / 8, 0, (newCapacity - capacity) / 8);
	bitmap = newBitmap;
	capacity = newCapacity;
	return B_OK;
}


int32
DkIdGenerator::Generator::AllocateId()
{
	// scan from hint, wrap around once
	for (uint32 pass = 0; pass < capacity; pass++) {
		uint32 id = (nextHint + pass) % capacity;
		if ((bitmap[id / 8] & (1 << (id & 7))) == 0) {
			bitmap[id / 8] |= (1 << (id & 7));
			count++;
			nextHint = (id + 1) % capacity;
			return (int32)id;
		}
	}

	if (Grow() != B_OK)
		return B_NO_MEMORY;

	// first ID in the freshly expanded range
	uint32 id = capacity / 2;	// capacity already doubled by Grow()
	bitmap[id / 8] |= (1 << (id & 7));
	count++;
	nextHint = id + 1;
	return (int32)id;
}


status_t
DkIdGenerator::Generator::FreeId(uint32 id)
{
	if (id >= capacity)
		return B_BAD_VALUE;

	if ((bitmap[id / 8] & (1 << (id & 7))) == 0)
		return B_BAD_VALUE;

	bitmap[id / 8] &= ~(1 << (id & 7));
	count--;

	// reuse freed slot early
	if (id < nextHint)
		nextHint = id;

	return B_OK;
}


DkIdGenerator::DkIdGenerator()
{
	mutex_init(&fLock, "dk idgen");
}


DkIdGenerator::~DkIdGenerator()
{
	while (Generator* gen = fGenerators.RemoveHead())
		delete gen;
	mutex_destroy(&fLock);
}


DkIdGenerator::Generator*
DkIdGenerator::_Find(const char* name)
{
	DoublyLinkedList<Generator>::Iterator it = fGenerators.GetIterator();
	while (it.HasNext()) {
		Generator* gen = it.Next();
		if (strcmp(gen->name, name) == 0)
			return gen;
	}
	return NULL;
}


DkIdGenerator::Generator*
DkIdGenerator::_FindOrCreate(const char* name)
{
	Generator* gen = _Find(name);
	if (gen != NULL)
		return gen;

	gen = new(std::nothrow) Generator(name);
	if (gen == NULL || gen->name == NULL || gen->bitmap == NULL) {
		delete gen;
		return NULL;
	}

	fGenerators.Add(gen);
	return gen;
}


int32
DkIdGenerator::Allocate(const char* generatorName)
{
	MutexLocker locker(&fLock);

	Generator* gen = _FindOrCreate(generatorName);
	if (gen == NULL)
		return B_NO_MEMORY;

	return gen->AllocateId();
}


status_t
DkIdGenerator::Free(const char* generatorName, uint32 id)
{
	MutexLocker locker(&fLock);

	Generator* gen = _Find(generatorName);
	if (gen == NULL)
		return B_NAME_NOT_FOUND;

	status_t status = gen->FreeId(id);

	if (gen->count == 0) {
		fGenerators.Remove(gen);
		delete gen;
	}

	return status;
}


// DkKeeper

DkKeeper* DkKeeper::sInstance = NULL;


DkKeeper::DkKeeper()
	:
	fRootNode(NULL),
	fLifecycle(fMatcher),
	fFdtBlob(NULL)
{
	rw_lock_init(&fTreeLock, "device keeper tree");
	fLifecycle.SetPublisher(&fPublisher);
}


DkKeeper::~DkKeeper()
{
	if (fRootNode != NULL) {
		fRootNode->Release();
		fRootNode = NULL;
	}
	rw_lock_destroy(&fTreeLock);
}


status_t
DkKeeper::RegisterNode(DkNode* parent, const char* moduleName,
	const dk_property* properties, const dk_io_resource* resources,
	DkNode** _node)
{
	DkNode* node = new(std::nothrow) DkNode(moduleName);
	if (node == NULL)
		return B_NO_MEMORY;

	status_t status = node->InitCheck();
	if (status != B_OK) {
		delete node;
		return status;
	}

	status = node->Properties().Init();
	if (status != B_OK) {
		delete node;
		return status;
	}

	if (properties != NULL) {
		status = node->Properties().PutFromArray(properties);
		if (status != B_OK) {
			delete node;
			return status;
		}
	}

	node->Properties().Commit();

	// acquire I/O resources (each has its own lock now)
	if (resources != NULL) {
		for (const dk_io_resource* r = resources; r->type != 0; r++) {
			status = fResources.Acquire(r->type, r->base, r->length);
			if (status != B_OK) {
				_RollbackResources(node);
				delete node;
				return status;
			}

			DkOwnedResource* entry = new(std::nothrow) DkOwnedResource();
			if (entry == NULL) {
				fResources.Release(r->type, r->base);
				_RollbackResources(node);
				delete node;
				return B_NO_MEMORY;
			}

			entry->type = r->type;
			entry->base = r->base;
			entry->length = r->length;
			node->Resources().Add(entry);
		}
	}

	// insert into tree under tree write lock
	{
		WriteLocker treeLocker(&fTreeLock);

		if (parent != NULL) {
			parent->AddChild(node);
			parent->NotifyWatchers(KOSM_EVENT_CHILD_REGISTERED,
				reinterpret_cast<dk_node*>(node), NULL);
		}

		node->SetRegistered(true);
	}

	if (_node != NULL)
		*_node = node;

	// Defer fixed-child attachment and auto-probe if the parent is
	// still inside driver->attach(). The parent's driver cookie is
	// not yet set, so get_bus_ops() would return a NULL cookie and
	// any fixed-child that needs bus ops would fail. The caller
	// (ProbeAndAttach/ProbeAndAttachAll) will call
	// ProbePendingChildren() after setting the cookie.
	if (parent != NULL && parent->IsAttaching())
		return B_OK;

	_PostRegisterProbe(node);

	return B_OK;
}


void
DkKeeper::_PostRegisterProbe(DkNode* node)
{
	dprintf("DeviceKeeper: _PostRegisterProbe(%s) attached=%d\n",
		node->ModuleName(), node->IsAttached());

	// Auto-attach by module name FIRST: if the node's own module exports
	// dk_driver_info (suffix dk_driver_v1), load it and attach directly.
	// This is the standard path — parent calls register_node(MODULE_NAME)
	// and the module becomes the node's driver automatically.
	if (!node->IsAttached()) {
		const char* modName = node->ModuleName();
		size_t nameLen = strlen(modName);
		size_t suffixLen = strlen(DK_DRIVER_MODULE_SUFFIX);
		bool isDkDriver = (nameLen > suffixLen + 1
			&& modName[nameLen - suffixLen - 1] == '/'
			&& strcmp(modName + nameLen - suffixLen,
				DK_DRIVER_MODULE_SUFFIX) == 0);

		if (isDkDriver) {
			dk_driver_info* ownDriver = NULL;
			status_t s = get_module(modName, (module_info**)&ownDriver);
			if (s == B_OK && ownDriver->attach != NULL) {
				{
					MutexLocker nodeLocker(node->NodeLock());
					node->SetDriver(ownDriver, NULL);
					node->SetAttaching(true);
				}

				void* cookie = NULL;
				s = ownDriver->attach(
					reinterpret_cast<dk_node*>(node), &cookie);

				{
					MutexLocker nodeLocker(node->NodeLock());
					if (s == B_OK) {
						node->SetDriverCookie(cookie);
					} else {
						node->ClearDriver();
					}
					node->SetAttaching(false);
				}

				if (s == B_OK) {
					dprintf("DeviceKeeper: auto-attached %s\n", modName);
					ProbePendingChildren(node);
				} else {
					dprintf("DeviceKeeper: auto-attach %s failed: %s\n",
						modName, strerror(s));
					put_module(modName);
				}
			} else if (s == B_OK) {
				// Module loaded but no attach — just a module, not a driver
				put_module(modName);
			}
		}
	}

	// Fixed child: if the node declares KOSM_DEVICE_FIXED_CHILD,
	// always create a child node with that module name. Auto-attach
	// by module name will load and attach the module on the child.
	// Runs AFTER auto-attach so the node's own driver + bus_ops are
	// available for the fixed-child's attach via get_bus_ops walk-up.
	//
	// Use GetStringCopy: the raw pointer from GetString could be
	// invalidated by a concurrent SetAfterCommit on a different
	// thread (the property store frees the old string under node
	// lock, but we don't hold that lock here).
	char fixedChildBuf[B_PATH_NAME_LENGTH];
	{
		MutexLocker nodeLocker(node->NodeLock());
		status_t s = node->Properties().GetStringCopy(
			KOSM_DEVICE_FIXED_CHILD, fixedChildBuf, sizeof(fixedChildBuf));
		if (s != B_OK)
			fixedChildBuf[0] = '\0';
	}

	if (fixedChildBuf[0] != '\0') {
		const char* fixedChild = fixedChildBuf;
		size_t fcNameLen = strlen(fixedChild);
		size_t fcSuffixLen = strlen(DK_DRIVER_MODULE_SUFFIX);
		bool fcIsDk = (fcNameLen > fcSuffixLen + 1
			&& fixedChild[fcNameLen - fcSuffixLen - 1] == '/'
			&& strcmp(fixedChild + fcNameLen - fcSuffixLen,
				DK_DRIVER_MODULE_SUFFIX) == 0);

		if (fcIsDk) {
			DkNode* childNode = NULL;
			status_t s = RegisterNode(node, fixedChild,
				NULL, NULL, &childNode);
			if (s == B_OK) {
				dprintf("DeviceKeeper: fixed-child created %s under %s\n",
					fixedChild, node->ModuleName());
			} else {
				dprintf("DeviceKeeper: fixed-child RegisterNode(%s) "
					"failed: %s\n", fixedChild, strerror(s));
			}
		} else {
			dprintf("DeviceKeeper: fixed-child skipping non-dk "
				"module %s\n", fixedChild);
		}
	}

	// Fallback: probe-based attach for nodes not yet attached
	uint32 flags = node->Flags();
	if (!node->IsAttached() && (flags & KOSM_FIND_CHILD_ON_DEMAND) == 0)
		fLifecycle.ProbeAndAttach(node);

	// keep driver loaded: take extra reference so the node (and thus
	// its attached driver module) stays alive even when no consumers
	// hold references
	if (node->HasKeepLoaded() && node->IsAttached())
		node->Acquire();
}


void
DkKeeper::ProbePendingChildren(DkNode* node)
{
	// Called after a parent's attach() completes and its cookie is set.
	// Probe children that were registered during attach() but deferred
	// because the parent's cookie wasn't available yet.
	static const int32 kStackMax = 16;
	DkNode* stackBuf[kStackMax];
	DkNode** children = stackBuf;
	int32 count = 0;

	{
		ReadLocker treeLocker(&fTreeLock);

		DkNodeList::Iterator it = node->Children().GetIterator();
		while (it.HasNext()) {
			DkNode* child = it.Next();
			if (child->IsRegistered() && !child->IsAttached()
				&& !child->IsAttaching())
				count++;
		}

		if (count == 0)
			return;

		if (count > kStackMax) {
			children = (DkNode**)malloc(count * sizeof(DkNode*));
			if (children == NULL)
				return;
		}

		int32 i = 0;
		it = node->Children().GetIterator();
		while (it.HasNext() && i < count) {
			DkNode* child = it.Next();
			if (child->IsRegistered() && !child->IsAttached()
				&& !child->IsAttaching()) {
				child->Acquire();
				children[i++] = child;
			}
		}
		count = i;
	}

	for (int32 i = 0; i < count; i++) {
		_PostRegisterProbe(children[i]);
		children[i]->Release();
	}

	if (children != stackBuf)
		free(children);
}


status_t
DkKeeper::UnregisterNode(DkNode* node)
{
	// detach driver (called without locks, may call back into us)
	fLifecycle.Detach(node);

	// release I/O resources owned by this node
	_RollbackResources(node);

	// release keep-loaded extra reference
	if (node->HasKeepLoaded()) {
		node->SetKeepLoaded(false);
		node->Release();
	}

	// tree structure change under tree write lock
	{
		WriteLocker treeLocker(&fTreeLock);

		DkNode* parent = node->Parent();
		if (parent != NULL) {
			parent->NotifyWatchers(KOSM_EVENT_CHILD_UNREGISTERED,
				reinterpret_cast<dk_node*>(node), NULL);
			parent->RemoveChild(node);
		}

		{
			MutexLocker nodeLocker(node->NodeLock());
			node->ClearWatchers();
		}
		node->SetRegistered(false);
	}

	// release the creation reference (may delete the node)
	node->Release();

	return B_OK;
}


status_t
DkKeeper::Rescan(DkNode* node)
{
	return fLifecycle.Rescan(node);
}


void
DkKeeper::DeviceRemoved(DkNode* node)
{
	// Phase 1: notify device_removed callbacks on all published devices
	// in the subtree, bottom-up. This unblocks pending I/O before we
	// tear anything down.
	fLifecycle.NotifyRemoved(node);

	// Phase 2: detach drivers and unpublish devices (bottom-up).
	// Called WITHOUT locks — drivers may call back into DeviceKeeper.
	fLifecycle.Detach(node);

	// Phase 3: release I/O resources, keep-loaded refs, watchers,
	// and mark nodes as unregistered for the entire subtree.
	_DeviceRemovedCleanup(node);

	// Phase 4: detach from parent tree under tree write lock.
	{
		WriteLocker treeLocker(&fTreeLock);

		DkNode* parent = node->Parent();
		if (parent != NULL) {
			parent->NotifyWatchers(KOSM_EVENT_CHILD_UNREGISTERED,
				reinterpret_cast<dk_node*>(node), NULL);
			parent->RemoveChild(node);
		}
	}

	// Phase 5: release creation reference. If no external references
	// remain, this cascades deletion through the subtree via ~DkNode.
	node->Release();
}


void
DkKeeper::_DeviceRemovedCleanup(DkNode* node)
{
	// bottom-up: cleanup children, then detach them from this node.
	// Snapshot children under tree read lock (a concurrent
	// register_node could modify the list between unlock/relock).
	//
	// On malloc failure, process in batches of kStackMax. This
	// ensures all children are cleaned up even under OOM, at the
	// cost of re-acquiring the tree lock for each batch.
	static const int32 kStackMax = 16;
	DkNode* stackBuf[kStackMax];
	int32 totalCount = 0;
	int32 processed = 0;

	// first pass: count total children
	{
		ReadLocker treeLocker(&fTreeLock);
		DkNodeList::Iterator it = node->Children().GetIterator();
		while (it.HasNext()) {
			it.Next();
			totalCount++;
		}
	}

	if (totalCount == 0)
		goto cleanup_self;

	// Try heap allocation; fall back to stack-sized batches.
	{
		DkNode** snapshot = stackBuf;
		int32 snapshotCapacity = kStackMax;

		if (totalCount > kStackMax) {
			snapshot = (DkNode**)malloc(totalCount * sizeof(DkNode*));
			if (snapshot != NULL) {
				snapshotCapacity = totalCount;
			} else {
				// OOM: use stack buffer, process in batches
				snapshot = stackBuf;
				snapshotCapacity = kStackMax;
			}
		}

		while (processed < totalCount) {
			int32 batchCount = 0;

			{
				ReadLocker treeLocker(&fTreeLock);

				// Skip already-processed children (they've been
				// removed from the list in previous batch iterations)
				DkNodeList::Iterator it = node->Children().GetIterator();
				while (it.HasNext() && batchCount < snapshotCapacity) {
					DkNode* child = it.Next();
					child->Acquire();
					snapshot[batchCount++] = child;
				}
			}

			if (batchCount == 0)
				break;

			// recurse into each child without locks
			for (int32 i = 0; i < batchCount; i++)
				_DeviceRemovedCleanup(snapshot[i]);

			// detach all children under tree write lock
			{
				WriteLocker treeLocker(&fTreeLock);
				for (int32 i = 0; i < batchCount; i++) {
					if (snapshot[i]->Parent() == node)
						node->RemoveChild(snapshot[i]);
				}
			}

			// release snapshot refs
			for (int32 i = 0; i < batchCount; i++)
				snapshot[i]->Release();

			processed += batchCount;
		}

		if (snapshot != stackBuf)
			free(snapshot);
	}

cleanup_self:
	// release globally reserved I/O resources
	_RollbackResources(node);

	// release keep-loaded extra reference
	if (node->HasKeepLoaded()) {
		node->SetKeepLoaded(false);
		node->Release();
	}

	// watcher callbacks become invalid after removal
	{
		MutexLocker nodeLocker(node->NodeLock());
		node->ClearWatchers();
	}

	node->SetRegistered(false);
}


status_t
DkKeeper::DiscoverDrivers(const char* path)
{
	return fMatcher.DiscoverDrivers(path);
}


status_t
DkKeeper::DemandProbe(const char* devicePath)
{
	// discover drivers relevant to this device class
	if (devicePath != NULL && devicePath[0] != '\0') {
		char driverPath[B_PATH_NAME_LENGTH];
		int written = snprintf(driverPath, sizeof(driverPath),
			"drivers/dev/%s", devicePath);
		if (written >= 0 && (size_t)written < sizeof(driverPath))
			DiscoverDrivers(driverPath);
	}

	// Probe root node — let all matching drivers (PCI, ACPI, etc.)
	// attach and register their child nodes.
	_ProbeNode(fRootNode);

	_ProbeChildren(fRootNode, devicePath);
	return B_OK;
}


status_t
DkKeeper::WatchNode(DkNode* node, uint32 events,
	dk_node_callback callback, void* cookie)
{
	MutexLocker locker(node->NodeLock());
	return node->AddWatcher(events, callback, cookie);
}


status_t
DkKeeper::UnwatchNode(DkNode* node, dk_node_callback callback, void* cookie)
{
	MutexLocker locker(node->NodeLock());
	return node->RemoveWatcher(callback, cookie);
}


void
DkKeeper::_ProbeNode(DkNode* node)
{
	// For nodes with KOSM_FIND_MULTIPLE_CHILDREN, we need to let
	// ALL matching drivers attach (each registers its own child).
	// For normal nodes, just attach the best matching driver.
	if ((node->Flags() & KOSM_FIND_MULTIPLE_CHILDREN) != 0) {
		fLifecycle.ProbeAndAttachAll(node);
	} else if (!node->IsAttached()) {
		fLifecycle.ProbeAndAttach(node);
	}
}


void
DkKeeper::_ProbeChildren(DkNode* node, const char* devicePath)
{
	// Snapshot children under tree read lock. We Acquire each child
	// so that a concurrent UnregisterNode on a sibling cannot delete
	// it while we hold a pointer. Without the snapshot, the iterator's
	// internal fNext could dangle after unlock.
	static const int32 kStackMax = 16;
	DkNode* stackBuf[kStackMax];
	DkNode** children = stackBuf;
	int32 count = 0;

	{
		ReadLocker treeLocker(&fTreeLock);

		// first pass: count eligible children
		DkNodeList::Iterator it = node->Children().GetIterator();
		while (it.HasNext()) {
			DkNode* child = it.Next();
			if (child->IsRegistered())
				count++;
		}

		if (count == 0)
			return;

		if (count > kStackMax) {
			children = (DkNode**)malloc(count * sizeof(DkNode*));
			if (children == NULL)
				return;
		}

		// second pass: snapshot with Acquire
		int32 i = 0;
		it = node->Children().GetIterator();
		while (it.HasNext() && i < count) {
			DkNode* child = it.Next();
			if (child->IsRegistered()) {
				child->Acquire();
				children[i++] = child;
			}
		}
		count = i;
	}

	// probe without any locks held
	dprintf("DeviceKeeper: _ProbeChildren(%s): %d children to probe\n",
		node->ModuleName(), (int)count);
	for (int32 i = 0; i < count; i++) {
		DkNode* child = children[i];

		dprintf("DeviceKeeper: _ProbeChildren: [%d] child %s (attached=%d flags=0x%x)\n",
			(int)i, child->ModuleName(), child->IsAttached(), child->Flags());
		_ProbeNode(child);

		// If driver implements rescan_children, call it to discover
		// child devices (e.g. SCSI bus scan). This runs after
		// attach completed and interrupts are available.
		if (child->IsAttached()) {
			dk_driver_info* drv = child->DriverInfo();
			if (drv != NULL && drv->rescan_children != NULL) {
				dprintf("DeviceKeeper: calling rescan_children for %s\n",
					child->ModuleName());
				drv->rescan_children(child->DriverCookie());
			}
		}

		_ProbeChildren(child, devicePath);
		child->Release();
	}

	if (children != stackBuf)
		free(children);
}


void
DkKeeper::_RollbackResources(DkNode* node)
{
	DkOwnedResourceList& resources = node->Resources();
	while (DkOwnedResource* entry = resources.RemoveHead()) {
		fResources.Release(entry->type, entry->base);
		delete entry;
	}
}


// module API forwarding

static status_t
dk_register_node(dk_node* parent, const char* moduleName,
	const dk_property* properties, const dk_io_resource* resources,
	dk_node** _node)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	DkNode* parentNode = reinterpret_cast<DkNode*>(parent);
	DkNode* node = NULL;

	status_t status = keeper->RegisterNode(parentNode, moduleName,
		properties, resources, &node);
	if (status != B_OK)
		return status;

	if (_node != NULL)
		*_node = reinterpret_cast<dk_node*>(node);

	return B_OK;
}


static status_t
dk_unregister_node(dk_node* _node)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	return keeper->UnregisterNode(node);
}


static status_t
dk_rescan_node(dk_node* _node)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	return keeper->Rescan(node);
}


static void
dk_device_removed(dk_node* _node)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return;

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	keeper->DeviceRemoved(node);
}


static dk_node*
dk_get_root_node()
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return NULL;

	DkNode* root = keeper->RootNode();
	if (root != NULL)
		root->Acquire();

	return reinterpret_cast<dk_node*>(root);
}


static dk_node*
dk_get_parent_node(dk_node* _node)
{
	DkNode* node = reinterpret_cast<DkNode*>(_node);
	if (node == NULL)
		return NULL;

	ReadLocker locker(DkKeeper::Instance()->TreeLock());

	DkNode* parent = node->Parent();
	if (parent != NULL)
		parent->Acquire();

	return reinterpret_cast<dk_node*>(parent);
}


static status_t
dk_get_next_child_node(dk_node* _parent, const dk_match_rule* attrs,
	dk_node** _node)
{
	DkKeeper* keeper = DkKeeper::Instance();
	ReadLocker locker(keeper->TreeLock());

	DkNode* parent = reinterpret_cast<DkNode*>(_parent);
	DkNode* last = reinterpret_cast<DkNode*>(*_node);

	DkNodeList::Iterator it = parent->Children().GetIterator();

	if (last != NULL) {
		while (it.HasNext()) {
			if (it.Next() == last)
				break;
		}
	}

	while (it.HasNext()) {
		DkNode* child = it.Next();
		if (!child->IsRegistered())
			continue;

		if (attrs == NULL || child->Properties().Matches(attrs)) {
			if (last != NULL)
				last->Release();

			child->Acquire();
			*_node = reinterpret_cast<dk_node*>(child);
			return B_OK;
		}
	}

	if (last != NULL)
		last->Release();

	*_node = NULL;
	return B_ENTRY_NOT_FOUND;
}


static void
dk_put_node(dk_node* _node)
{
	DkNode* node = reinterpret_cast<DkNode*>(_node);
	if (node != NULL)
		node->Release();
}


static status_t
dk_find_child_node(dk_node* _parent, const dk_match_rule* attrs,
	dk_node** _node)
{
	DkKeeper* keeper = DkKeeper::Instance();
	ReadLocker locker(keeper->TreeLock());

	DkNode* parent = reinterpret_cast<DkNode*>(_parent);
	DkNode* last = reinterpret_cast<DkNode*>(*_node);

	DkNode* found = parent->FindChildDeep(attrs, last);

	if (last != NULL)
		last->Release();

	if (found != NULL) {
		found->Acquire();
		*_node = reinterpret_cast<dk_node*>(found);
		return B_OK;
	}

	*_node = NULL;
	return B_ENTRY_NOT_FOUND;
}


static status_t
dk_find_node(const dk_match_rule* rules, dk_node** _node)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL || keeper->RootNode() == NULL)
		return B_NO_INIT;

	ReadLocker locker(keeper->TreeLock());

	DkNode* last = reinterpret_cast<DkNode*>(*_node);

	DkNode* found = keeper->RootNode()->FindChildDeep(rules, last);

	if (last != NULL)
		last->Release();

	if (found != NULL) {
		found->Acquire();
		*_node = reinterpret_cast<dk_node*>(found);
		return B_OK;
	}

	*_node = NULL;
	return B_ENTRY_NOT_FOUND;
}


static status_t
dk_publish_device(dk_node* _node, const char* path, dk_device_ops* ops)
{
	DkKeeper* keeper = DkKeeper::Instance();

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	return keeper->GetPublisher().Publish(node, path, ops);
}


static status_t
dk_unpublish_device(dk_node* _node, const char* path)
{
	DkKeeper* keeper = DkKeeper::Instance();

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	return keeper->GetPublisher().Unpublish(node, path);
}


static int32
dk_create_id(const char* generator)
{
	DkKeeper* keeper = DkKeeper::Instance();
	return keeper->GetIdGenerator().Allocate(generator);
}


static status_t
dk_free_id(const char* generator, uint32 id)
{
	DkKeeper* keeper = DkKeeper::Instance();
	return keeper->GetIdGenerator().Free(generator, id);
}


// property accessor forwarding with recursive parent chain walk.
// Recursive lookups walk fParent, so they need tree read lock.
// Non-recursive lookups access only the committed property store
// (immutable for initial properties, node-locked for SetAfterCommit).

static status_t
dk_get_property_uint8(const dk_node* _node, const char* name,
	uint8* _value, bool recursive)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);
	if (!recursive)
		return node->Properties().GetUInt8(name, _value);

	ReadLocker locker(DkKeeper::Instance()->TreeLock());
	const DkPropertyEntry* entry = node->LookupRecursive(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT8_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui8;
	return B_OK;
}


static status_t
dk_get_property_uint16(const dk_node* _node, const char* name,
	uint16* _value, bool recursive)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);
	if (!recursive)
		return node->Properties().GetUInt16(name, _value);

	ReadLocker locker(DkKeeper::Instance()->TreeLock());
	const DkPropertyEntry* entry = node->LookupRecursive(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT16_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui16;
	return B_OK;
}


static status_t
dk_get_property_uint32(const dk_node* _node, const char* name,
	uint32* _value, bool recursive)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);
	if (!recursive)
		return node->Properties().GetUInt32(name, _value);

	ReadLocker locker(DkKeeper::Instance()->TreeLock());
	const DkPropertyEntry* entry = node->LookupRecursive(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT32_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui32;
	return B_OK;
}


static status_t
dk_get_property_uint64(const dk_node* _node, const char* name,
	uint64* _value, bool recursive)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);
	if (!recursive)
		return node->Properties().GetUInt64(name, _value);

	ReadLocker locker(DkKeeper::Instance()->TreeLock());
	const DkPropertyEntry* entry = node->LookupRecursive(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT64_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui64;
	return B_OK;
}


static status_t
dk_get_property_string(const dk_node* _node, const char* name,
	char* buffer, size_t bufferSize, bool recursive)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);

	if (!recursive) {
		MutexLocker locker(const_cast<DkNode*>(node)->NodeLock());
		return node->Properties().GetStringCopy(name, buffer, bufferSize);
	}

	DkKeeper* keeper = DkKeeper::Instance();
	ReadLocker treeLocker(keeper->TreeLock());

	const DkNode* current = node;
	while (current != NULL) {
		MutexLocker nodeLocker(const_cast<DkNode*>(current)->NodeLock());
		status_t status = current->Properties().GetStringCopy(name,
			buffer, bufferSize);
		if (status != B_NAME_NOT_FOUND)
			return status;
		nodeLocker.Unlock();
		current = current->Parent();
	}

	return B_NAME_NOT_FOUND;
}


static status_t
dk_get_property_raw(const dk_node* _node, const char* name,
	void* buffer, size_t bufferSize, size_t* _copiedSize, bool recursive)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);

	if (!recursive) {
		MutexLocker locker(const_cast<DkNode*>(node)->NodeLock());
		return node->Properties().GetRawCopy(name, buffer, bufferSize,
			_copiedSize);
	}

	DkKeeper* keeper = DkKeeper::Instance();
	ReadLocker treeLocker(keeper->TreeLock());

	const DkNode* current = node;
	while (current != NULL) {
		MutexLocker nodeLocker(const_cast<DkNode*>(current)->NodeLock());
		status_t status = current->Properties().GetRawCopy(name,
			buffer, bufferSize, _copiedSize);
		if (status != B_NAME_NOT_FOUND)
			return status;
		nodeLocker.Unlock();
		current = current->Parent();
	}

	return B_NAME_NOT_FOUND;
}


static status_t
dk_get_next_property(dk_node* _node, dk_property** _property)
{
	DkNode* node = reinterpret_cast<DkNode*>(_node);

	// Safe: DkPropertyEntry is layout-compatible with dk_property at
	// offset 0 (name, type, value). Requires -fno-strict-aliasing.
	//
	// Not synchronized with set_property(): iteration sees a
	// consistent snapshot of existing entries (insert_next chain
	// is append-only), but entries added via set_property during
	// iteration may be missed. Callers requiring a consistent view
	// must hold the node lock externally.
	DkPropertyEntry** entry = reinterpret_cast<DkPropertyEntry**>(_property);
	return node->Properties().GetNext(entry);
}


static status_t
dk_get_driver(dk_node* _node, dk_driver_info** _driver, void** _cookie)
{
	DkNode* node = reinterpret_cast<DkNode*>(_node);
	if (!node->IsAttached())
		return B_NO_INIT;

	if (_driver != NULL)
		*_driver = node->DriverInfo();
	if (_cookie != NULL)
		*_cookie = node->DriverCookie();

	return B_OK;
}


static status_t
dk_get_bus_ops(dk_node* _node, uint32 busType, const void** _ops,
	void** _cookie)
{
	// Walk up the tree until we find an ancestor with matching
	// bus_ops_type. This decouples drivers from specific tree depth.
	//
	// Tree read lock protects parent chain traversal (Parent() pointers
	// are only modified under tree write lock).
	//
	// Per-ancestor node lock protects driver state: ClearDriver() in
	// _DetachRecursive runs under node lock (not tree lock), so
	// without the node lock we could see IsAttached()==true but then
	// read NULL from DriverInfo() after concurrent ClearDriver().
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	ReadLocker treeLocker(keeper->TreeLock());

	DkNode* node = reinterpret_cast<DkNode*>(_node);

	dprintf("DeviceKeeper: get_bus_ops(%s, type=%u) walking up...\n",
		node->ModuleName(), busType);

	for (DkNode* ancestor = node->Parent(); ancestor != NULL;
		ancestor = ancestor->Parent()) {

		MutexLocker nodeLocker(ancestor->NodeLock());

		dk_driver_info* driver = ancestor->DriverInfo();
		void* cookie = ancestor->DriverCookie();
		bool attached = ancestor->IsAttached();

		dprintf("DeviceKeeper: get_bus_ops:   checking %s attached=%d "
			"driver=%s bus_ops=%p type=%u\n",
			ancestor->ModuleName(), attached,
			(attached && driver) ? driver->info.name : "none",
			(attached && driver) ? driver->bus_ops : NULL,
			(attached && driver) ? driver->bus_ops_type : 0);

		if (!attached || driver == NULL || driver->bus_ops == NULL)
			continue;

		if (driver->bus_ops_type == busType) {
			dprintf("DeviceKeeper: get_bus_ops(%s, type=%u) -> FOUND %s "
				"bus_ops=%p cookie=%p\n",
				node->ModuleName(), busType,
				ancestor->ModuleName(), driver->bus_ops, cookie);

			if (_ops != NULL)
				*_ops = driver->bus_ops;
			if (_cookie != NULL)
				*_cookie = cookie;
			return B_OK;
		}
	}

	dprintf("DeviceKeeper: get_bus_ops(%s, type=%u) -> not found\n",
		node->ModuleName(), busType);
	return B_NOT_SUPPORTED;
}


// resource forwarding (DkResourceAllocator has its own lock)

static status_t
dk_acquire_io_resource(uint32 type, generic_addr_t base,
	generic_addr_t length)
{
	DkKeeper* keeper = DkKeeper::Instance();
	return keeper->GetResources().Acquire(type, base, length);
}


static void
dk_release_io_resource(uint32 type, generic_addr_t base)
{
	DkKeeper* keeper = DkKeeper::Instance();
	keeper->GetResources().Release(type, base);
}


// node watcher forwarding (per-node lock)

static status_t
dk_watch_node(dk_node* _node, uint32 events, dk_node_callback callback,
	void* cookie)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	return keeper->WatchNode(node, events, callback, cookie);
}


static status_t
dk_unwatch_node(dk_node* _node, dk_node_callback callback, void* cookie)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	return keeper->UnwatchNode(node, callback, cookie);
}


// runtime PM stubs

static status_t
dk_pm_runtime_get(dk_node* _node)
{
	// TODO: implement runtime PM with refcount and wake-on-demand
	(void)_node;
	return B_OK;
}


static void
dk_pm_runtime_put(dk_node* _node)
{
	// TODO: implement runtime PM with auto-suspend on idle
	(void)_node;
}


static status_t
dk_pm_set_idle_timeout(dk_node* _node, bigtime_t timeout)
{
	// TODO: implement runtime PM idle timeout
	(void)_node;
	(void)timeout;
	return B_OK;
}


// mutable property update

static status_t
dk_set_property(dk_node* _node, const dk_property* prop)
{
	if (prop == NULL)
		return B_BAD_VALUE;

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	MutexLocker locker(node->NodeLock());

	return node->Properties().SetAfterCommit(*prop);
}


// firmware forwarding

static status_t
dk_firmware_load(dk_node* _node, const char* name, const void** _data,
	size_t* _size)
{
	(void)_node;
	return dk_load_firmware(name, _data, _size);
}


static void
dk_firmware_release(const void* data)
{
	dk_release_firmware(data);
}


// module standard ops

static status_t
dk_module_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return dk_keeper_init(NULL);
		case B_MODULE_UNINIT:
			dk_keeper_uninit();
			return B_OK;
		default:
			return B_ERROR;
	}
}


dk_keeper_info gDeviceKeeperModule = {
	{
		KOSM_DEVICE_KEEPER_MODULE_NAME,
		0,
		dk_module_std_ops
	},

	// node management
	dk_register_node,
	dk_unregister_node,
	dk_rescan_node,

	// surprise removal
	dk_device_removed,

	// tree navigation
	dk_get_root_node,
	dk_get_parent_node,
	dk_get_next_child_node,
	dk_find_child_node,
	dk_put_node,

	// global tree query
	dk_find_node,

	// property accessors (recursive)
	dk_get_property_uint8,
	dk_get_property_uint16,
	dk_get_property_uint32,
	dk_get_property_uint64,
	dk_get_property_string,
	dk_get_property_raw,

	// property iteration
	dk_get_next_property,

	// device publishing
	dk_publish_device,
	dk_unpublish_device,

	// ID generation
	dk_create_id,
	dk_free_id,

	// driver access
	dk_get_driver,

	// bus protocol
	dk_get_bus_ops,

	// I/O resource management
	dk_acquire_io_resource,
	dk_release_io_resource,

	// node watcher
	dk_watch_node,
	dk_unwatch_node,

	// runtime PM
	dk_pm_runtime_get,
	dk_pm_runtime_put,
	dk_pm_set_idle_timeout,

	// mutable property
	dk_set_property,

	// firmware
	dk_firmware_load,
	dk_firmware_release
};


// KDL debugger support

static void
_dump_node_recursive(DkNode* node, int32 level)
{
	for (int32 i = 0; i < level; i++)
		kprintf("   ");

	kprintf("@%p \"%s\" (ref %" B_PRId32 ", %s%s%s)\n",
		node, node->ModuleName(), node->RefCount(),
		node->IsRegistered() ? "reg" : "unreg",
		node->IsAttached() ? " attached" : "",
		node->HasKeepLoaded() ? " keep" : "");

	// print published paths
	const DkPublishedPathList& paths = node->PublishedPaths();
	DkPublishedPathList::ConstIterator pit = paths.GetIterator();
	while (pit.HasNext()) {
		for (int32 i = 0; i < level + 1; i++)
			kprintf("   ");
		kprintf("dev: %s\n", pit.Next()->path);
	}

	DkNodeList::Iterator it = node->Children().GetIterator();
	while (it.HasNext())
		_dump_node_recursive(it.Next(), level + 1);
}


static int
dump_dk_tree(int argc, char** argv)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL || keeper->RootNode() == NULL) {
		kprintf("DeviceKeeper not initialized\n");
		return 0;
	}

	_dump_node_recursive(keeper->RootNode(), 0);
	return 0;
}


static int
dump_dk_node(int argc, char** argv)
{
	if (argc != 2) {
		kprintf("usage: dk_node <address>\n");
		return 0;
	}

	DkNode* node = (DkNode*)parse_expression(argv[1]);
	if (node == NULL) {
		kprintf("invalid address\n");
		return 0;
	}

	kprintf("DkNode at %p\n", node);
	kprintf("  module:     %s\n", node->ModuleName());
	kprintf("  parent:     %p\n", node->Parent());
	kprintf("  ref count:  %" B_PRId32 "\n", node->RefCount());
	kprintf("  flags:      %#" B_PRIx32 "\n", node->Flags());
	kprintf("  registered: %s\n", node->IsRegistered() ? "yes" : "no");
	kprintf("  attached:   %s\n", node->IsAttached() ? "yes" : "no");
	kprintf("  keep loaded: %s\n", node->HasKeepLoaded() ? "yes" : "no");
	kprintf("  power:      %s\n",
		node->PowerState() == DkNode::kPowerActive ? "active" : "suspended");

	if (node->IsAttached()) {
		kprintf("  driver:     %p (%s)\n", node->DriverInfo(),
			node->DriverInfo()->info.name);
		kprintf("  cookie:     %p\n", node->DriverCookie());
	}

	kprintf("  published:\n");
	const DkPublishedPathList& paths = node->PublishedPaths();
	DkPublishedPathList::ConstIterator pit = paths.GetIterator();
	while (pit.HasNext())
		kprintf("    %s\n", pit.Next()->path);

	kprintf("  children:\n");
	DkNodeList::Iterator it = node->Children().GetIterator();
	while (it.HasNext()) {
		DkNode* child = it.Next();
		kprintf("    %p %s\n", child, child->ModuleName());
	}

	return 0;
}


// init / uninit

status_t
dk_keeper_init(kernel_args* args)
{
	if (DkKeeper::sInstance != NULL)
		return B_OK;

	DkKeeper* keeper = new(std::nothrow) DkKeeper();
	if (keeper == NULL)
		return B_NO_MEMORY;

	// I/O scheduler roster (KDL commands for I/O debugging)
	IOSchedulerRoster::Init();

	// root node
	DkNode* root = new(std::nothrow) DkNode(
		"system/device_keeper/root");
	if (root == NULL) {
		delete keeper;
		return B_NO_MEMORY;
	}

	status_t status = root->Properties().Init();
	if (status != B_OK) {
		delete root;
		delete keeper;
		return status;
	}

	root->Properties().PutString(KOSM_DEVICE_PRETTY_NAME, "Devices Root");
	root->Properties().PutString(KOSM_DEVICE_BUS, "root");
	root->Properties().PutUInt32(KOSM_DEVICE_FLAGS,
		KOSM_FIND_MULTIPLE_CHILDREN | KOSM_KEEP_DRIVER_LOADED);
	root->Properties().Commit();
	root->SetRegistered(true);

	keeper->fRootNode = root;

	// generic bus node: container for virtual devices (loopback,
	// /dev/null, /dev/random, tun/tap, virtio-mmio)
	DkNode* generic = new(std::nothrow) DkNode(
		"system/device_keeper/generic");
	if (generic != NULL && generic->Properties().Init() == B_OK) {
		generic->Properties().PutString(KOSM_DEVICE_PRETTY_NAME, "Generic");
		generic->Properties().PutString(KOSM_DEVICE_BUS, "generic");
		generic->Properties().PutUInt32(KOSM_DEVICE_FLAGS,
			KOSM_FIND_MULTIPLE_CHILDREN | KOSM_KEEP_DRIVER_LOADED
			| KOSM_FIND_CHILD_ON_DEMAND);
		generic->Properties().Commit();
		generic->SetRegistered(true);
		root->AddChild(generic);
	} else {
		delete generic;
		dprintf("DeviceKeeper: failed to create generic bus node\n");
	}

	DkKeeper::sInstance = keeper;

	// KDL debugger commands
	add_debugger_command("dk_tree", &dump_dk_tree,
		"dump device keeper node tree");
	add_debugger_command_etc("dk_node", &dump_dk_node,
		"print info on a device keeper node",
		"<address>\n"
		"Prints information on a DkNode at <address>.\n", 0);

	dprintf("DeviceKeeper: initialized\n");
	return B_OK;
}


status_t
dk_keeper_init_post_modules(kernel_args* args)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	// discover drivers in standard paths (modules now available from boot FS)
	static const char* sPaths[] = {
		"drivers",
		"busses",
		"bus_managers",
		NULL
	};

	for (const char** path = sPaths; *path != NULL; path++) {
		status_t s = keeper->DiscoverDrivers(*path);
		dprintf("DeviceKeeper: DiscoverDrivers(\"%s\"): %s\n",
			*path, s == B_OK ? "found" : "none");
	}

	// reprobe all on-demand nodes that weren't probed during early boot
	keeper->DemandProbe("");

	dprintf("DeviceKeeper: post-module reprobe complete\n");
	return B_OK;
}


// Called from devfs scan_for_drivers_if_needed()
status_t
dk_keeper_probe(const char* path, uint32 updateCycle)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	return keeper->DemandProbe(path);
}


void
dk_keeper_uninit()
{
	DkKeeper* keeper = DkKeeper::sInstance;
	if (keeper != NULL) {
		remove_debugger_command("dk_tree", &dump_dk_tree);
		remove_debugger_command("dk_node", &dump_dk_node);

		delete keeper;
		DkKeeper::sInstance = NULL;
	}
}
