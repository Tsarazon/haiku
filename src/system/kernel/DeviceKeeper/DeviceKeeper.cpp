/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceKeeper.h"
#include "FirmwareLoader.h"
#include "LockDebug.h"

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

	// insert into tree under tree write lock, but first check for a
	// duplicate child under the same parent. If one already exists
	// with the same moduleName AND the same values for every property
	// passed in `properties`, return B_NAME_IN_USE without creating a
	// new node. This preserves the legacy device_manager semantic
	// that bus managers (e.g. scsi_scan_lun) rely on to make bus
	// rescan idempotent.
	bool isDuplicate = false;
	{
		WriteLocker treeLocker(&fTreeLock);

		if (parent != NULL) {
			DkNodeList::Iterator it = parent->Children().GetIterator();
			while (it.HasNext()) {
				DkNode* existing = it.Next();
				if (!existing->IsRegistered())
					continue;
				if (strcmp(existing->ModuleName(), moduleName) != 0)
					continue;
				if (existing->Properties().MatchesProperties(properties)) {
					isDuplicate = true;
					break;
				}
			}
		}

		if (!isDuplicate) {
			if (parent != NULL) {
				parent->AddChild(node);
				parent->NotifyWatchers(KOSM_EVENT_CHILD_REGISTERED,
					reinterpret_cast<dk_node*>(node), NULL);
			}

			node->SetRegistered(true);
		}
	}

	if (isDuplicate) {
		_RollbackResources(node);
		delete node;
		return B_NAME_IN_USE;
	}

	if (_node != NULL)
		*_node = node;

	// Defer fixed-child attachment and auto-probe if the parent is
	// still inside driver->attach(). The parent's driver cookie is
	// not yet set, so get_interface() would return a NULL cookie and
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
	DK_TRACE("_PostRegisterProbe(%s) attached=%d\n",
		node->ModuleName(), node->IsAttached());

	// Auto-attach by module name FIRST: if the node's own module exports
	// dk_driver_info (suffix dk_driver_v1), load it and attach via the
	// unified attach helper. This is the standard path — parent calls
	// register_node(MODULE_NAME) and the module becomes the node's
	// driver automatically.
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
				DkModuleRef ref(ownDriver);
				// AutoAttachByName sets fAttaching, invokes
				// _AttachDriverToNode, and on success transfers
				// ref ownership to the node. On failure, ref dtor
				// releases the module automatically.
				s = fLifecycle.AutoAttachByName(node, ref);
				if (s == B_OK)
					DK_INFO("attached %s\n", modName);
				else if (s != KOSM_DEFER_PROBE)
					DK_ERROR("auto-attach %s failed: %s\n",
						modName, strerror(s));
			} else if (s == B_OK) {
				// Module loaded but no attach — just a plain kernel
				// module, not a driver. Release the ref immediately.
				put_module(modName);
			}
		}
	}

	// Propagate node_flags from dk_driver_info to the node.
	// Bus managers declare KOSM_FIND_MULTIPLE_CHILDREN in their
	// dk_driver_info::node_flags once — the framework applies it
	// to every node the driver attaches to. No manual property
	// boilerplate needed.
	if (node->IsAttached()) {
		dk_driver_info* drv = node->DriverInfo();
		if (drv != NULL && drv->node_flags != 0) {
			uint32 merged = node->Flags() | drv->node_flags;
			if (merged != node->Flags())
				node->SetFlags(merged);
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
	const char* busFilter[8];
	int32 busFilterCount = 0;

	if (devicePath != NULL && devicePath[0] != '\0') {
		char driverPath[B_PATH_NAME_LENGTH];
		int written = snprintf(driverPath, sizeof(driverPath),
			"drivers/dev/%s", devicePath);
		if (written >= 0 && (size_t)written < sizeof(driverPath))
			DiscoverDrivers(driverPath);

		// Collect bus names from newly discovered drivers' match rules.
		// This tells us which bus types are relevant for this device
		// class — e.g. "graphics" drivers match "pci" bus, so we only
		// need to probe PCI device nodes, not ACPI/USB/etc.
		busFilterCount = fMatcher.CollectBusNames(driverPath,
			busFilter, 8);
	}

	// Probe root node — let all matching drivers (PCI, ACPI, etc.)
	// attach and register their child nodes.
	_ProbeNode(fRootNode);

	_ProbeChildren(fRootNode, devicePath, busFilter, busFilterCount);
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


/*static*/ bool
DkKeeper::_NodeMatchesBusFilter(DkNode* node, const char** busFilter,
	int32 busFilterCount)
{
	if (busFilterCount == 0)
		return true;	// no filter — probe everything

	// Node's bus name from KOSM_DEVICE_BUS property.
	// If the node has no bus property (intermediate nodes like root,
	// bus managers without own bus) — let it through so we recurse
	// into its children.
	const char* nodeBus = NULL;
	node->Properties().GetString(KOSM_DEVICE_BUS, &nodeBus);
	if (nodeBus == NULL)
		return true;

	for (int32 i = 0; i < busFilterCount; i++) {
		if (strcmp(nodeBus, busFilter[i]) == 0)
			return true;
	}

	return false;
}


void
DkKeeper::_ProbeChildren(DkNode* node, const char* devicePath,
	const char** busFilter, int32 busFilterCount)
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
	for (int32 i = 0; i < count; i++) {
		DkNode* child = children[i];

		// Bus filter: only probe nodes whose bus matches the
		// discovered drivers' bus types. Nodes without a bus
		// property (intermediate bus managers) always pass.
		if (_NodeMatchesBusFilter(child, busFilter, busFilterCount)) {
			_ProbeNode(child);

			// If driver implements rescan_children, call it to discover
			// child devices (e.g. SCSI bus scan).
			if (child->IsAttached()) {
				dk_driver_info* drv = child->DriverInfo();
				if (drv != NULL && drv->rescan_children != NULL) {
					drv->rescan_children(child->DriverCookie());
				}
			}
		}

		// Always recurse — intermediate nodes may have matching
		// children deeper in the tree.
		_ProbeChildren(child, devicePath, busFilter, busFilterCount);
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
dk_find_node_global(const dk_match_rule* rules, dk_node** _node)
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
//
// Consistency rule (post-hardening): EVERY accessor that touches a
// property store also takes the node lock on that store. This is
// needed because SetAfterCommit can mutate entries in place, and
// concurrent readers must not see torn state. The cost is small —
// node locks are uncontended in the common read path.
//
// Recursive accessors walk fParent under the tree read lock, then
// briefly take each ancestor's node lock to look up the property.

static status_t
_get_integer_property_locked(const DkNode* node, const char* name,
	type_code expectedType, void* out, size_t size)
{
	const DkPropertyEntry* entry = node->Properties().Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != expectedType)
		return B_BAD_TYPE;
	switch (expectedType) {
		case B_UINT8_TYPE:
			*(uint8*)out = entry->value.ui8;
			break;
		case B_UINT16_TYPE:
			*(uint16*)out = entry->value.ui16;
			break;
		case B_UINT32_TYPE:
			*(uint32*)out = entry->value.ui32;
			break;
		case B_UINT64_TYPE:
			*(uint64*)out = entry->value.ui64;
			break;
		default:
			return B_BAD_VALUE;
	}
	(void)size;
	return B_OK;
}


template<typename T>
static status_t
_dk_get_property_integer(const dk_node* _node, const char* name,
	T* _value, bool recursive, type_code expectedType)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);

	if (!recursive) {
		MutexLocker locker(const_cast<DkNode*>(node)->NodeLock());
		return _get_integer_property_locked(node, name, expectedType,
			_value, sizeof(T));
	}

	DkKeeper* keeper = DkKeeper::Instance();
	ReadLocker treeLocker(keeper->TreeLock());

	const DkNode* current = node;
	while (current != NULL) {
		MutexLocker nodeLocker(const_cast<DkNode*>(current)->NodeLock());
		status_t status = _get_integer_property_locked(current, name,
			expectedType, _value, sizeof(T));
		if (status != B_NAME_NOT_FOUND)
			return status;
		nodeLocker.Unlock();
		current = current->Parent();
	}

	return B_NAME_NOT_FOUND;
}


static status_t
dk_get_property_uint8(const dk_node* _node, const char* name,
	uint8* _value, bool recursive)
{
	return _dk_get_property_integer<uint8>(_node, name, _value,
		recursive, B_UINT8_TYPE);
}


static status_t
dk_get_property_uint16(const dk_node* _node, const char* name,
	uint16* _value, bool recursive)
{
	return _dk_get_property_integer<uint16>(_node, name, _value,
		recursive, B_UINT16_TYPE);
}


static status_t
dk_get_property_uint32(const dk_node* _node, const char* name,
	uint32* _value, bool recursive)
{
	return _dk_get_property_integer<uint32>(_node, name, _value,
		recursive, B_UINT32_TYPE);
}


static status_t
dk_get_property_uint64(const dk_node* _node, const char* name,
	uint64* _value, bool recursive)
{
	return _dk_get_property_integer<uint64>(_node, name, _value,
		recursive, B_UINT64_TYPE);
}


static status_t
dk_get_property_string(const dk_node* _node, const char* name,
	char* buffer, size_t bufferSize, size_t* _actualLength, bool recursive)
{
	const DkNode* node = reinterpret_cast<const DkNode*>(_node);

	if (!recursive) {
		MutexLocker locker(const_cast<DkNode*>(node)->NodeLock());
		return node->Properties().GetStringCopy(name, buffer, bufferSize,
			_actualLength);
	}

	DkKeeper* keeper = DkKeeper::Instance();
	ReadLocker treeLocker(keeper->TreeLock());

	const DkNode* current = node;
	while (current != NULL) {
		MutexLocker nodeLocker(const_cast<DkNode*>(current)->NodeLock());
		status_t status = current->Properties().GetStringCopy(name,
			buffer, bufferSize, _actualLength);
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
dk_get_next_property(dk_node* _node, dk_property** _property,
	uint64* _iterVersion)
{
	DkNode* node = reinterpret_cast<DkNode*>(_node);

	// Safe: DkPropertyEntry is layout-compatible with dk_property at
	// offset 0 (name, type, value). Requires -fno-strict-aliasing.
	//
	// Version-tracked iteration: caller passes uninitialized uint64*,
	// the store snapshots its mutation counter on first call and
	// returns B_INTERRUPTED on later calls if the store has changed.
	// Callers should restart iteration on B_INTERRUPTED.
	DkPropertyEntry** entry = reinterpret_cast<DkPropertyEntry**>(_property);
	return node->Properties().GetNext(entry, _iterVersion);
}


static status_t
dk_get_node_driver(dk_node* _node, dk_driver_info** _driver, void** _cookie)
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
dk_publish_interface(dk_node* _node, const char* name, const void* ops)
{
	if (name == NULL || ops == NULL)
		return B_BAD_VALUE;

	DkNode* node = reinterpret_cast<DkNode*>(_node);
	MutexLocker locker(node->NodeLock());

	return node->PublishInterface(name, ops);
}


static status_t
dk_get_interface(dk_node* _node, const char* name, uint32 flags,
	const void** _ops, void** _cookie)
{
	// Flag-controlled interface search:
	//   KOSM_INTERFACE_SELF      — check the node itself
	//   KOSM_INTERFACE_ANCESTORS — walk up parents (skips self)
	// Combine to check self first, then ancestors.
	//
	// Each visited node iterates its full DkInterfaceEntry list
	// (a single node may publish multiple interfaces, e.g. SDHCI
	// publishing both PCI consumer and MMC producer ops).
	//
	// Tree read lock protects parent chain traversal.
	// Per-ancestor node lock protects the interface list.
	if (name == NULL)
		return B_BAD_VALUE;
	if ((flags & (KOSM_INTERFACE_SELF | KOSM_INTERFACE_ANCESTORS)) == 0)
		return B_BAD_VALUE;

	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return B_NO_INIT;

	ReadLocker treeLocker(keeper->TreeLock());

	DkNode* node = reinterpret_cast<DkNode*>(_node);

	DK_TRACE("get_interface(%s, \"%s\", flags=%#x) searching...\n",
		node->ModuleName(), name, (unsigned)flags);

	// Start with self if requested, then walk parents.
	DkNode* start = (flags & KOSM_INTERFACE_SELF) != 0
		? node
		: node->Parent();
	bool walkUp = (flags & KOSM_INTERFACE_ANCESTORS) != 0;

	for (DkNode* cur = start; cur != NULL;
		cur = walkUp ? cur->Parent() : NULL) {

		MutexLocker nodeLocker(cur->NodeLock());

		// Iterate this node's published interface list.
		for (DkInterfaceEntry* e = cur->Interfaces(); e != NULL;
				e = e->next) {

			DK_TRACE("get_interface:   %s has %s\n",
				cur->ModuleName(), e->name);

			if (strcmp(e->name, name) == 0) {
				DK_TRACE("get_interface(%s, \"%s\") -> FOUND on %s "
					"ops=%p cookie=%p\n",
					node->ModuleName(), name,
					cur->ModuleName(), e->ops,
					cur->DriverCookie());

				if (_ops != NULL)
					*_ops = e->ops;
				if (_cookie != NULL)
					*_cookie = cur->DriverCookie();
				return B_OK;
			}
		}

		// If only self was requested, stop after one iteration.
		if (!walkUp)
			break;
	}

	DK_TRACE("get_interface(%s, \"%s\") -> not found\n",
		node->ModuleName(), name);
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
	dk_find_node_global,

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
	dk_get_node_driver,

	// bus interface
	dk_publish_interface,
	dk_get_interface,

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

	// print published interfaces
	for (DkInterfaceEntry* e = node->Interfaces(); e != NULL; e = e->next) {
		for (int32 i = 0; i < level + 1; i++)
			kprintf("   ");
		kprintf("iface: %s\n", e->name);
	}

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

	kprintf("  interfaces:\n");
	for (DkInterfaceEntry* e = node->Interfaces(); e != NULL; e = e->next)
		kprintf("    %s -> ops=%p\n", e->name, e->ops);

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


static int
dump_dk_drivers(int argc, char** argv)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL) {
		kprintf("DeviceKeeper not initialized\n");
		return 0;
	}

	keeper->GetMatcher().DumpRegistry();
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

	root->Properties().PutString(KOSM_LABEL, "Devices Root");
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
		generic->Properties().PutString(KOSM_LABEL, "Generic");
		generic->Properties().PutString(KOSM_DEVICE_BUS, "generic");
		generic->Properties().PutUInt32(KOSM_DEVICE_FLAGS,
			KOSM_FIND_MULTIPLE_CHILDREN | KOSM_KEEP_DRIVER_LOADED
			| KOSM_FIND_CHILD_ON_DEMAND);
		generic->Properties().Commit();
		generic->SetRegistered(true);
		root->AddChild(generic);
	} else {
		delete generic;
		DK_ERROR("failed to create generic bus node\n");
	}

	DkKeeper::sInstance = keeper;

	// KDL debugger commands
	add_debugger_command("dk_tree", &dump_dk_tree,
		"dump device keeper node tree");
	add_debugger_command_etc("dk_node", &dump_dk_node,
		"print info on a device keeper node",
		"<address>\n"
		"Prints information on a DkNode at <address>.\n", 0);
	add_debugger_command("dk_drivers", &dump_dk_drivers,
		"dump device keeper driver registry");

	DK_INFO("initialized\n");
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
		DK_INFO("DiscoverDrivers(\"%s\"): %s\n",
			*path, s == B_OK ? "found" : "none");
	}

	// reprobe all on-demand nodes that weren't probed during early boot
	keeper->DemandProbe("");

	DK_INFO("post-module reprobe complete\n");
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
		remove_debugger_command("dk_drivers", &dump_dk_drivers);

		delete keeper;
		DkKeeper::sInstance = NULL;
	}
}
