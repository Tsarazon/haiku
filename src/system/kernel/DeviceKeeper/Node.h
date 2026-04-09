/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_NODE_H
#define DEVICE_KEEPER_NODE_H

#include <condition_variable.h>
#include <device_keeper.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>
#include <util/KernelReferenceable.h>
#include <util/OpenHashTable.h>


// DkPropertyEntry: single property stored in hash table

struct DkPropertyEntry {
	// layout-compatible with dk_property at offset 0:
	// name, type, value must come first so that
	// reinterpret_cast<dk_property*>(entry) works in
	// dk_get_next_property().
	char*				name;
	type_code			type;
	union {
		uint8			ui8;
		uint16			ui16;
		uint32			ui32;
		uint64			ui64;
		char*			string;
		struct {
			void*		data;
			size_t		length;
		} raw;
		struct {
			char**		items;
			uint32		count;
		} stringlist;
	} value;

	DkPropertyEntry*	hash_link;
	DkPropertyEntry*	insert_next;	// insertion-order chain

						DkPropertyEntry();
						~DkPropertyEntry();
	status_t			SetFrom(const dk_property& prop);

private:
	void				_Free();
						DkPropertyEntry(const DkPropertyEntry&);
	DkPropertyEntry&	operator=(const DkPropertyEntry&);
};


struct DkPropertyHashDefinition {
	typedef const char*		KeyType;
	typedef DkPropertyEntry	ValueType;

	size_t				HashKey(const char* key) const;
	size_t				Hash(const DkPropertyEntry* entry) const;
	bool				Compare(const char* key,
							const DkPropertyEntry* entry) const;
	DkPropertyEntry*&	GetLink(DkPropertyEntry* entry) const;
};


// DkPropertyStore: two-phase hash table (mutable until Commit, with
// limited mutability after via SetAfterCommit).
//
// Iteration consistency: every Put/SetAfterCommit bumps fVersion.
// GetNext() takes a version snapshot on first call and verifies it
// on every subsequent call — if the store has been mutated mid-
// iteration, it returns B_INTERRUPTED and the caller must restart.
// This turns the previously-undefined "miss concurrent inserts"
// behavior into a defined, detectable error.

class DkPropertyStore {
public:
						DkPropertyStore();
						~DkPropertyStore();

	status_t			Init(uint32 capacity = 16);

	// builder phase (before Commit)
	status_t			Put(const dk_property& property);
	status_t			PutUInt8(const char* name, uint8 value);
	status_t			PutUInt16(const char* name, uint16 value);
	status_t			PutUInt32(const char* name, uint32 value);
	status_t			PutUInt64(const char* name, uint64 value);
	status_t			PutString(const char* name, const char* value);
	status_t			PutStringList(const char* name, const char** items,
						uint32 count);
	status_t			PutRaw(const char* name, const void* data,
						size_t length);

	// convenience: from dk_property array (NULL-name terminated)
	status_t			PutFromArray(const dk_property* properties);

	void				Commit();
	bool				IsCommitted() const { return fCommitted; }

	// read phase (after Commit)
	// Lookup returns a pointer to the entry; integer fields may be
	// read without locks.  String/raw pointers may be invalidated
	// by concurrent SetAfterCommit — use GetStringCopy/GetRawCopy
	// under node lock for safe access to heap-backed values.
	const DkPropertyEntry*	Lookup(const char* name) const;

	status_t			GetUInt8(const char* name, uint8* _value) const;
	status_t			GetUInt16(const char* name, uint16* _value) const;
	status_t			GetUInt32(const char* name, uint32* _value) const;
	status_t			GetUInt64(const char* name, uint64* _value) const;
	status_t			GetString(const char* name,
						const char** _value) const;
	status_t			GetRaw(const char* name, const void** _data,
						size_t* _length) const;

	// copy-out accessors (safe under concurrent SetAfterCommit
	// when caller holds node lock).
	//
	// GetStringCopy: writes the string into buffer, NUL-terminated
	// if space permits. _actualLength (if non-NULL) is set to the
	// length of the source string EXCLUDING the NUL terminator —
	// callers can re-allocate `actualLength + 1` bytes and retry.
	// On B_BUFFER_OVERFLOW, the buffer is filled and truncated.
	status_t			GetStringCopy(const char* name, char* buffer,
						size_t bufferSize, size_t* _actualLength) const;
	status_t			GetRawCopy(const char* name, void* buffer,
						size_t bufferSize,
						size_t* _copiedSize) const;

	bool				ContainsInStringList(const char* name,
						const char* value) const;

	// matching: check if all rules match this store (NULL-name terminated)
	bool				Matches(const dk_match_rule* rules) const;

	// mutable update after Commit (caller must hold node lock).
	// Replaces an existing entry in-place or inserts a new one.
	// Integer types are updated directly; strings are replaced
	// via strdup + pointer swap (old freed immediately under lock).
	status_t			SetAfterCommit(const dk_property& property);

	uint32				CountProperties() const;

	// Version counter: bumped on every Put/SetAfterCommit. Readers
	// can snapshot this value and detect concurrent mutation during
	// iteration or multi-field reads. Monotonic (wraparound would
	// take 2^64 mutations — effectively impossible).
	uint64				Version() const { return fVersion; }

	// iteration: pass NULL to start, returns B_ENTRY_NOT_FOUND at end.
	// _iterVersion is INPUT/OUTPUT: pass uninitialized on first call
	// (the function will snapshot fVersion into it). On subsequent
	// calls, the function compares the snapshot to fVersion and
	// returns B_INTERRUPTED if the store has been mutated. Caller
	// should restart iteration from NULL on B_INTERRUPTED.
	//
	// Concurrency: DkPropertyEntry::insert_next is append-only, so
	// in-flight entries remain reachable even if SetAfterCommit
	// touches them. Version check exists to surface "you missed
	// some entries" rather than to prevent crashes.
	status_t			GetNext(DkPropertyEntry** _entry,
							uint64* _iterVersion) const;

private:
	void				_InsertListAppend(DkPropertyEntry* entry);
	void				_InsertListRemove(DkPropertyEntry* entry);

	typedef BOpenHashTable<DkPropertyHashDefinition, false> Table;

	Table				fTable;
	bool				fCommitted;
	uint64				fVersion;	// incremented on every mutation
	DkPropertyEntry*	fInsertHead;
	DkPropertyEntry*	fInsertTail;
};


// DkOwnedResource: I/O resource bound to a node, auto-released on unregister

struct DkOwnedResource : DoublyLinkedListLinkImpl<DkOwnedResource> {
	uint32			type;
	generic_addr_t	base;
	generic_addr_t	length;
};

typedef DoublyLinkedList<DkOwnedResource> DkOwnedResourceList;


// DkPublishedPathEntry: dynamically allocated published path

struct DkPublishedPathEntry : DoublyLinkedListLinkImpl<DkPublishedPathEntry> {
	char*			path;

					DkPublishedPathEntry(const char* _path);
					~DkPublishedPathEntry();
};

typedef DoublyLinkedList<DkPublishedPathEntry> DkPublishedPathList;


// DkInterfaceEntry: one published bus interface on a node.
//
// A node can publish multiple interfaces (e.g. an SDHCI controller
// publishes both pci_device_ops as a consumer and mmc_bus_interface
// as a producer). Entries are stored in a singly-linked list keyed
// by interface name. Lookups are O(N) which is acceptable because
// N is typically 1-2 per node.
//
// Caller must hold the owning DkNode's lock when adding/iterating.
struct DkInterfaceEntry {
	char*				name;
	const void*			ops;
	DkInterfaceEntry*	next;
};


// DkWatchEntry: node event subscriber

struct DkWatchEntry : DoublyLinkedListLinkImpl<DkWatchEntry> {
	uint32				events;
	dk_node_callback	callback;
	void*				cookie;
};

typedef DoublyLinkedList<DkWatchEntry> DkWatchList;


class DkNode : public KernelReferenceable,
			   public DoublyLinkedListLinkImpl<DkNode> {
	friend class DkKeeper;
	friend class DkLifecycle;
	friend class DkPublisher;
	friend class DkPublishedDevice;

public:
						DkNode(const char* moduleName);
						~DkNode();

	status_t			InitCheck() const;

	// property building (before registration)
	DkPropertyStore&		Properties() { return fProperties; }
	const DkPropertyStore&	Properties() const { return fProperties; }

	// recursive property lookup: walks parent chain if not found locally
	const DkPropertyEntry*	LookupRecursive(const char* name) const;

	// tree
	DkNode*				Parent() const { return fParent; }
	const DoublyLinkedList<DkNode>& Children() const { return fChildren; }
	DoublyLinkedList<DkNode>& Children() { return fChildren; }
	void				AddChild(DkNode* child);
	void				RemoveChild(DkNode* child);
	DkNode*				FindChild(const dk_match_rule* attrs) const;
	DkNode*				FindChildDeep(const dk_match_rule* attrs,
							DkNode* after = NULL) const;
		// Deep search through all descendants. Pass after=NULL to start,
		// or last found node to continue. Returns NULL when exhausted.

	// identity
	const char*			ModuleName() const { return fModuleName; }
	uint32				Flags() const { return fFlags; }
	void				SetFlags(uint32 flags) { fFlags = flags; }
	bool				IsRegistered() const { return fRegistered; }
	void				SetRegistered(bool registered);

	// Priority for probe ordering: higher = probed first.
	// Nodes with KOSM_FIND_MULTIPLE_CHILDREN are less specific
	// (bus enumerators) and get lower priority so that single-child
	// (more specific) drivers are probed and registered first.
	// This gives stable device naming across reboots.
	int32				Priority() const;

	// reference counting (delegates to KernelReferenceable)
	void				Acquire() { AcquireReference(); }
	void				Release() { ReleaseReference(); }
	int32				RefCount() const { return CountReferences(); }

	// per-node lock: protects driver state, published paths,
	// watchers, power state, and mutable properties.
	// Tree structure (fParent, fChildren) is protected by
	// DkKeeper::TreeLock() instead.
	mutex*				NodeLock() { return &fLock; }

	// driver state (read-only queries)
	dk_driver_info*		DriverInfo() const { return fDriver; }
	void*				DriverCookie() const { return fDriverCookie; }
	bool				IsAttached() const { return fDriver != NULL; }

	// Module reference accounting: set to true when a get_module()
	// ref was acquired for this node's driver and must be balanced
	// by a put_module() call in Detach(). Used by ~DkNode to ASSERT
	// that nothing leaks. Set via SetDriver() and cleared by
	// ReleaseDriverRef() (called from Detach() after put_module).
	bool				HasDriverRef() const { return fDriverRefHeld; }

	// attach-in-progress state: set by DkLifecycle around driver->attach()
	// so that concurrent Detach() can wait for attach to complete.
	bool				IsAttaching() const { return fAttaching; }
	void				WaitAttachDone();

	// KOSM_KEEP_DRIVER_LOADED
	bool				HasKeepLoaded() const { return fKeepLoaded; }

	// power state
	enum power_state {
		kPowerActive = 0,
		kPowerSuspended,
	};
	power_state			PowerState() const { return fPowerState; }

	// published paths (read-only queries)
	const char*			FirstPublishedPath() const;
	int32				CountPublishedPaths() const;
	const DkPublishedPathList& PublishedPaths() const
							{ return fPublishedPaths; }

	// bus interface published on this node (caller must hold node lock).
	// PublishInterface adds an entry to the per-node interface list,
	// or replaces the existing entry's ops if name already exists.
	// A single node may publish multiple interfaces — useful for
	// dual-personality drivers (e.g. SDHCI = PCI consumer + MMC producer).
	status_t			PublishInterface(const char* name, const void* ops);

	// Iteration accessor: returns the head of the interface list.
	// Caller must hold the node lock for the duration of the iteration.
	// Used by dk_get_interface and KDL dump.
	DkInterfaceEntry*	Interfaces() const { return fInterfaces; }

protected:
	virtual	void		LastReferenceReleased();

	// driver mutation (caller must hold node lock)
	// SetDriver implies the caller holds a module reference that
	// will be balanced by ClearDriver+ReleaseDriverRef (Detach path)
	// or by direct put_module on failure/rollback.
	void				SetDriver(dk_driver_info* driver, void* cookie);
	void				SetDriverCookie(void* cookie);
	void				ClearDriver();
	// Explicitly drop the "module ref held" flag without clearing
	// the driver pointer. Used by rollback paths that already called
	// put_module() before zeroing fDriver.
	void				ReleaseDriverRef() { fDriverRefHeld = false; }
	void				SetAttaching(bool attaching);

	void				SetKeepLoaded(bool keep) { fKeepLoaded = keep; }
	void				SetPowerState(power_state state);

	// I/O resources owned by this node (auto-released on unregister)
	DkOwnedResourceList&	Resources() { return fResources; }
	void				ReleaseAllResources();

	// published paths mutation (caller must hold node lock)
	status_t			AddPublishedPath(const char* path);
	void				RemovePublishedPath(const char* path);
	void				ClearPublishedPaths();

	// node watchers
	// AddWatcher/RemoveWatcher: caller must hold node lock.
	// NotifyWatchers: takes node lock internally for snapshot,
	// releases before invoking callbacks (re-entrant safe).
	// ClearWatchers: caller must hold node lock.
	status_t			AddWatcher(uint32 events, dk_node_callback callback,
							void* cookie);
	status_t			RemoveWatcher(dk_node_callback callback, void* cookie);
	void				NotifyWatchers(uint32 event, dk_node* eventNode,
							const char* path);
	void				ClearWatchers();

private:
	static bool			_FindDeepHelper(DkNode* root,
							const dk_match_rule* attrs,
							DkNode* after, bool* pastAfter,
							DkNode** _result);

	DkNode*				fParent;
	DoublyLinkedList<DkNode> fChildren;

	mutex				fLock;
	char*				fModuleName;
	uint32				fFlags;
	bool				fRegistered;
	bool				fKeepLoaded;

	DkPropertyStore		fProperties;

	dk_driver_info*		fDriver;
	void*				fDriverCookie;
	bool				fDriverRefHeld;	// get_module ref balance
	bool				fAttaching;
	ConditionVariable	fAttachCondition;
	power_state			fPowerState;

	// bus interfaces: linked list of (name, ops) published on this node.
	// Set via PublishInterface(), iterated by dk_get_interface.
	DkInterfaceEntry*	fInterfaces;

	DkOwnedResourceList	fResources;
	DkPublishedPathList	fPublishedPaths;
	int32				fPublishedPathCount;
	DkWatchList			fWatchers;
};


typedef DoublyLinkedList<DkNode> DkNodeList;


#endif // DEVICE_KEEPER_NODE_H
