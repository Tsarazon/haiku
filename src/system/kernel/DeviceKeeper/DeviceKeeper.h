/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_DEVICE_KEEPER_H
#define DEVICE_KEEPER_DEVICE_KEEPER_H

#include <device_keeper.h>
#include <kdevice_keeper.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>

#include "Lifecycle.h"
#include "Matcher.h"
#include "Node.h"
#include "Publisher.h"


struct kernel_args;


// DkResourceAllocator: I/O resource tracking with overlap detection

struct DkResourceEntry : DoublyLinkedListLinkImpl<DkResourceEntry> {
	uint32			type;
	generic_addr_t	base;
	generic_addr_t	length;
};


class DkResourceAllocator {
public:
						DkResourceAllocator();
						~DkResourceAllocator();

	status_t			Acquire(uint32 type, generic_addr_t base,
							generic_addr_t length);
	void				Release(uint32 type, generic_addr_t base);

private:
	static bool			_Overlaps(const DkResourceEntry* existing,
							generic_addr_t base, generic_addr_t length);

	typedef DoublyLinkedList<DkResourceEntry> ResourceList;

	mutex				fLock;
	ResourceList		fMemory;
	ResourceList		fPorts;
	ResourceList		fDmaChannels;

	ResourceList&		_ListFor(uint32 type);
};


// DkIdGenerator: named ID generators with growable bitmaps

class DkIdGenerator {
public:
						DkIdGenerator();
						~DkIdGenerator();

	int32				Allocate(const char* generatorName);
	status_t			Free(const char* generatorName, uint32 id);

private:
	struct Generator : DoublyLinkedListLinkImpl<Generator> {
						Generator(const char* name);
						~Generator();

		char*			name;
		uint8*			bitmap;
		uint32			capacity;
		uint32			count;
		uint32			nextHint;	// start search here

		int32			AllocateId();
		status_t		FreeId(uint32 id);
		status_t		Grow();
	};

	Generator*			_Find(const char* name);
	Generator*			_FindOrCreate(const char* name);

	mutex				fLock;
	DoublyLinkedList<Generator> fGenerators;
};


class DkKeeper {
public:
	static DkKeeper*		Instance() { return sInstance; }

	// node management
	status_t				RegisterNode(DkNode* parent,
								const char* moduleName,
								const dk_property* properties,
								const dk_io_resource* resources,
								DkNode** _node);
	status_t				UnregisterNode(DkNode* node);
	status_t				Rescan(DkNode* node);

	// surprise removal: full teardown of a subtree when hardware
	// is physically removed while devices may still be open
	void					DeviceRemoved(DkNode* node);

	DkNode*					RootNode() const { return fRootNode; }
	rw_lock*				TreeLock() { return &fTreeLock; }

	// components (owned)
	DkMatcher&				GetMatcher() { return fMatcher; }
	DkLifecycle&			GetLifecycle() { return fLifecycle; }
	DkPublisher&			GetPublisher() { return fPublisher; }
	DkResourceAllocator&	GetResources() { return fResources; }
	DkIdGenerator&			GetIdGenerator() { return fIdGenerator; }

	// module scanning: discover dk_driver_info modules in a path
	status_t				DiscoverDrivers(const char* path);

	// demand probe: called from devfs when open() hits unknown path
	status_t				DemandProbe(const char* devicePath);

	// node watchers (forwarded from module API)
	status_t				WatchNode(DkNode* node, uint32 events,
								dk_node_callback callback, void* cookie);
	status_t				UnwatchNode(DkNode* node,
								dk_node_callback callback, void* cookie);

	// Probe children that were registered during a parent's attach()
	// but deferred because the parent's cookie wasn't set yet.
	// Called by DkLifecycle after setting the driver cookie.
	void					ProbePendingChildren(DkNode* node);

	// FDT blob (set by platform init, NULL on ACPI-only platforms)
	void					SetFdtBlob(const void* blob) { fFdtBlob = blob; }
	const void*				FdtBlob() const { return fFdtBlob; }

private:
	friend status_t			dk_keeper_init(kernel_args* args);
	friend status_t			dk_keeper_init_post_modules(kernel_args* args);
	friend void				dk_keeper_uninit();

	void					_ProbeChildren(DkNode* node,
									const char* devicePath,
									const char** busFilter = NULL,
									int32 busFilterCount = 0);
	void					_ProbeNode(DkNode* node);
	void					_PostRegisterProbe(DkNode* node);
	void					_RollbackResources(DkNode* node);
	void					_DeviceRemovedCleanup(DkNode* node);

	static bool				_NodeMatchesBusFilter(DkNode* node,
									const char** busFilter,
									int32 busFilterCount);

	// Locking strategy:
	//   fTreeLock (rw_lock): protects tree structure only
	//     - read lock for traversal (parent/children pointers)
	//     - write lock for AddChild/RemoveChild
	//   DkNode::fLock (mutex): protects per-node state
	//     - driver/cookie, power state, published paths, watchers,
	//       mutable properties
	//   DkResourceAllocator::fLock (mutex): I/O resource tracking
	//   DkIdGenerator::fLock (mutex): ID bitmap allocation
	//   DkMatcher::fLock (rw_lock): driver registry
	//
	// attach()/detach() are called WITHOUT any locks held, so
	// drivers may freely call register_node, publish_device, etc.
	//
	// Haiku rw_lock is recursive for write locks — nested write
	// lock from the same thread increments owner_count and
	// returns B_OK.
	rw_lock					fTreeLock;
	DkNode*					fRootNode;
	DkMatcher				fMatcher;
	DkLifecycle				fLifecycle;
	DkPublisher				fPublisher;
	DkResourceAllocator		fResources;
	DkIdGenerator			fIdGenerator;
	const void*				fFdtBlob;

	static DkKeeper*		sInstance;

							DkKeeper();
							~DkKeeper();
};


#endif // DEVICE_KEEPER_DEVICE_KEEPER_H
