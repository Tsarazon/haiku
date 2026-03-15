/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * kosm_dot — kernel private definitions.
 *
 * kosm_dot is a fully independent VM primitive parallel to Haiku area.
 * It does NOT wrap VMArea. It directly owns a VMCache, manages its own
 * page tables via VMTranslationMap, handles its own page faults, and
 * reserves address space independently.
 *
 * Integration with Haiku VM (minimal touching):
 *
 *   1. Address space: kosm_dot reserves its virtual range via
 *      vm_reserve_address_range(), preventing VMArea from using it.
 *      kosm_dot tracks its own base/size in a per-address-space
 *      AVL tree (KosmDotAddrTree) for O(log n) lookup by address.
 *
 *   2. Page faults: In vm_page_fault(), after LookupArea() returns
 *      NULL, a single hook calls kosm_dot_fault(). This is the
 *      ONLY modification to Haiku VM core — ~10 lines in vm.cpp.
 *
 *   3. Team death: kosm_dot_team_gone() is called from
 *      team_death_entry(), same pattern as kosm_ray_team_gone().
 *
 *   4. VMCache: kosm_dot creates VMAnonymousNoSwapCache (or
 *      KosmDotPurgeableCache for purgeable kosm_dots). No swap on mobile.
 *      The cache has no knowledge of kosm_dot — it's a plain Haiku cache.
 *      We drive it ourselves: fault, commit, discard.
 *
 *   5. Page tracking: kosm_dot pages use wired count (increment on map,
 *      decrement on unmap) instead of vm_page_mapping objects. This
 *      keeps pages out of the page daemon's active/inactive cycling.
 *      map->Map() / map->Unmap() are called directly, bypassing the
 *      VMArea-dependent map_page() / unmap_page() helpers in vm.cpp.
 *
 * Design patterns (same as kosm_ray):
 *   - BOpenHashTable for internal ID → kosm_dot lookup
 *   - Per-address-space AVL tree for address → kosm_dot lookup
 *   - Per-process handle table (KosmHandleTable) for access control
 *   - Atomic state: kUnused → kActive → kDeleted
 *   - ConditionVariable for waits
 *   - Refcount via KosmDotObject (handle table holds refs)
 *   - KDL debugger commands
 */
#ifndef _KERNEL_VM_KOSM_DOT_H
#define _KERNEL_VM_KOSM_DOT_H

#include <KosmOS.h>
#include <kosm_handle.h>

#include <condition_variable.h>
#include <lock.h>
#include <util/AVLTree.h>
#include <util/OpenHashTable.h>
#include <util/DoublyLinkedList.h>

#include <vm/vm_types.h>


/* Block type for slab identification */
#define KOSM_DOT_BLOCK_TYPE		0x4B53		/* 'KS' */

/* Limits */
#define KOSM_DOT_MAX_PER_TEAM	4096
#define KOSM_DOT_MAX_TOTAL		32768
#define KOSM_DOT_NAME_LENGTH	B_OS_NAME_LENGTH


/* Forward declarations */
struct VMAddressSpace;
struct VMCache;
struct VMTranslationMap;
struct vm_page;


/* --- kosm_dot states (atomic) --- */

enum {
	DOT_STATE_UNUSED	= 0,
	DOT_STATE_ACTIVE	= 1,
	DOT_STATE_DELETED	= 2
};


/* --- Per-team mapping entry --- */
/*
 * Each team that has this kosm_dot mapped gets one entry.
 * The owner team always has one. Additional mappings come from
 * kosm_map_dot().
 */
struct KosmDotMapping
	: DoublyLinkedListLinkImpl<KosmDotMapping> {
	kosm_dot_id			dot_id;
	team_id				team;
	addr_t				base;		/* virtual address in this team */
	size_t				size;		/* mapped size (for vm_unreserve_address_range) */
	uint32				granted_protection;	/* protection granted at map time (for ceiling) */
	bool				is_owner;
};

typedef DoublyLinkedList<KosmDotMapping> KosmDotMappingList;


/* --- Per-address-space kosm_dot tree --- */
/*
 * Each VMAddressSpace gets a KosmDotAddrTree — an AVL tree keyed by
 * virtual address range. This allows O(log n) lookup in page fault path.
 *
 * Stored externally to VMAddressSpace (in a static hash: team_id → tree)
 * so we don't modify VMAddressSpace.h at all.
 */
struct KosmDot;

struct KosmDotTreeNode {
	AVLTreeNode			tree_node;
	addr_t				base;
	size_t				size;
	KosmDot*		kosm_dot;
	team_id				team;
};

struct KosmDotTreeDefinition {
	typedef addr_t				Key;
	typedef KosmDotTreeNode		Value;

	AVLTreeNode* GetAVLTreeNode(KosmDotTreeNode* value) const
	{
		return &value->tree_node;
	}

	KosmDotTreeNode* GetValue(AVLTreeNode* node) const
	{
		return reinterpret_cast<KosmDotTreeNode*>(
			reinterpret_cast<addr_t>(node)
			- offsetof(KosmDotTreeNode, tree_node));
	}

	int Compare(addr_t key, const KosmDotTreeNode* value) const
	{
		if (key < value->base)
			return -1;
		if (key >= value->base + value->size)
			return 1;
		return 0;		/* key is within [base, base+size) */
	}

	int Compare(const KosmDotTreeNode* a,
		const KosmDotTreeNode* b) const
	{
		return Compare(a->base, b);
	}
};

typedef AVLTree<KosmDotTreeDefinition> KosmDotAddrTree;


/* --- Explicit wired range tracking (DMA/GPU) --- */
/*
 * Note: ALL kosm_dot pages use wired count for basic lifecycle tracking
 * (incremented on map, decremented on unmap). KosmDotWiredRange tracks
 * additional explicit wiring via kosm_dot_wire()/kosm_dot_unwire() which
 * prevents purging and guarantees physical address stability for DMA.
 */

struct KosmDotWiredRange
	: DoublyLinkedListLinkImpl<KosmDotWiredRange> {
	size_t				offset;
	size_t				size;
	bool				writable;
	int32				wire_count;
};

typedef DoublyLinkedList<KosmDotWiredRange> KosmDotWiredRangeList;


/* --- The core kosm_dot object --- */

struct KosmDot {
	kosm_dot_id		id;			/* kernel-internal ID (NOT a handle) */
	char				name[KOSM_DOT_NAME_LENGTH];

	int32				state;			/* atomic: DOT_STATE_* */
	int32				ref_count;		/* atomic (internal refs: fault, purge) */

	mutex				lock;

	/* Diagnostic/tracking only. Updated on create.
	   NOT used for access control (handle table provides that). */
	team_id				owner_team;

	/* Geometry */
	size_t				size;			/* page-aligned */
	uint32				guard_size;		/* guard pages at bottom (stack) */

	/* Properties */
	uint32				protection;		/* KOSM_PROT_* */
	uint32				flags;			/* KOSM_DOT_* creation flags */
	uint8				cache_policy;	/* KOSM_CACHE_* */
	uint8				purgeable_state;/* KOSM_PURGE_* (atomic for reads) */
	uint32				tag;			/* KOSM_TAG_* */
	uint32				wiring;			/* derived B_NO_LOCK / B_FULL_LOCK / B_CONTIGUOUS */

	/* Backing store -- owned by us, not by VMArea */
	VMCache*			cache;		/* VMAnonymousNoSwapCache, KosmDotPurgeableCache, or NULL (device) */
	off_t				cache_offset;

	/* Physical base for KOSM_DOT_DEVICE kosm_dots.
	   For anonymous kosm_dots this is 0. */
	phys_addr_t			phys_base;

	/* Memory type as set via arch_vm_set_memory_type.
	   Stored shifted, same convention as VMArea::memory_type. */
	uint16				memory_type;

	/* All team mappings */
	KosmDotMappingList mappings;

	/* Wired ranges (for DMA/GPU) */
	KosmDotWiredRangeList	wired_ranges;

	/* Waitable events: purge, delete, unwire */
	ConditionVariable	condition;

	/* Hash table link (internal ID lookup) */
	KosmDot*		hash_link;

	/* --- Methods --- */

	void		AcquireRef()		{ atomic_add(&ref_count, 1); }
	bool		ReleaseRef()		{ return atomic_add(&ref_count, -1) == 1; }
	int32		RefCount() const	{ return ref_count; }

	bool		IsActive() const	{ return state == DOT_STATE_ACTIVE; }
	bool		IsPurgeable() const	{ return (flags & KOSM_DOT_PURGEABLE) != 0; }
	bool		IsDevice() const	{ return (flags & KOSM_DOT_DEVICE) != 0; }
	bool		IsWired() const		{ return !wired_ranges.IsEmpty(); }
	bool		IsStack() const		{ return (flags & KOSM_DOT_STACK) != 0; }
	bool		IsReclaimable() const { return (flags & KOSM_DOT_RECLAIMABLE) != 0; }
	bool		IsFileBacked() const { return (flags & KOSM_DOT_FILE) != 0; }

	KosmDotMapping* FindMapping(team_id team) const;

	uint32		MemoryType() const;
	void		SetMemoryType(uint32 type);

	static uint32 ToHaikuProtection(uint32 kosmProt);
};


/* --- KosmDotObject: handle table wrapper --- */
/*
 * Refcounted wrapper stored in per-process handle tables.
 * The global hash table (sDotTable) is the authoritative store for dot state;
 * KosmDotObject bridges the handle table to an internal ID.
 *
 * When the last handle reference is released (all teams closed their handles),
 * the destructor triggers dot destruction: unmaps remaining mappings, releases
 * cache, removes from hash table.
 *
 * internal_id is set at creation and never changes.
 */

class KosmDotObject : public KernelReferenceable {
public:
	kosm_dot_id		internal_id;

					KosmDotObject(kosm_dot_id id)
						: internal_id(id) {}

	virtual			~KosmDotObject();
};

#define KOSM_RIGHT_DOT_DEFAULT		KOSM_RIGHT_ALL


/* --- Global hash table (ID lookup) --- */

struct KosmDotHashDefinition {
	typedef kosm_dot_id	KeyType;
	typedef KosmDot		ValueType;

	size_t HashKey(kosm_dot_id key) const		{ return (size_t)key; }
	size_t Hash(const KosmDot* s) const			{ return HashKey(s->id); }
	bool Compare(kosm_dot_id key, const KosmDot* s) const
		{ return s->id == key; }
	KosmDot*& GetLink(KosmDot* s) const		{ return s->hash_link; }
};

typedef BOpenHashTable<KosmDotHashDefinition> KosmDotHashTable;


/* --- Purgeable cache (VMCache subclass) --- */
/*
 * KosmDotPurgeableCache extends VMAnonymousNoSwapCache with purge support.
 * No swap on mobile — pages are simply discarded under memory pressure.
 * When purged (EMPTY state), re-commitment allocates fresh zeroed pages.
 */
/* Defined in KosmDotPurgeableCache.cpp; forward-declared here. */
struct KosmDotPurgeableCache;


/* ============================================================ */
/*  Kernel-only API                                              */
/* ============================================================ */

#ifdef __cplusplus
extern "C" {
#endif


/* --- Init / shutdown --- */

status_t	kosm_dot_init(void);
status_t	kosm_dot_init_post_sem(void);
status_t	kosm_dot_init_post_thread(void);


/* --- Page fault hook --- */
/*
 * Called from vm_soft_fault() when LookupArea() returns NULL.
 * This is the SOLE integration point with Haiku VM core.
 *
 * Returns B_OK if we handled the fault.
 * Returns B_BAD_ADDRESS if the address doesn't belong to any kosm_dot
 * (caller should proceed with normal fault handling / SIGSEGV).
 *
 * The patch in vm.cpp, inside vm_soft_fault():
 *
 *   VMArea* area = addressSpace->LookupArea(address);
 *   if (area == NULL) {
 *       status = kosm_dot_fault(addressSpace,
 *           address, isWrite, isExecute, isUser);
 *       if (status != B_BAD_ADDRESS)
 *           break;
 *       // else fall through to original error path
 *   }
 *
 * Page tracking: kosm_dot pages use wired count (like B_FULL_LOCK areas),
 * NOT vm_page_mapping objects. This means the page daemon does not
 * touch kosm_dot pages. We manage lifecycle ourselves via purgeable
 * logic and low_resource_handler.
 */
status_t	kosm_dot_fault(VMAddressSpace* addressSpace,
				addr_t address, bool isWrite, bool isExecute,
				bool isUser);


/* --- Team death / address space cleanup --- */
void		kosm_dot_team_gone(team_id team);
void		kosm_dot_aspace_gone(team_id team);


/* --- Memory pressure --- */
/*
 * Called by low_resource_handler.
 * Purges volatile purgeable kosm_dots, prioritized by QoS
 * (background first, interactive last).
 * Returns number of pages freed.
 */
size_t		kosm_dot_purge_volatile(size_t targetPages);


/* --- Kernel-internal creation --- */
/*
 * Creates the dot and inserts a handle into the specified team's table.
 * Returns the handle value (> 0) or negative error.
 * The internal dot ID is allocated internally and never exposed to userspace.
 */
kosm_handle_t kosm_create_dot_for(team_id team, const char* name,
				void** address, uint32 addressSpec, size_t size,
				uint32 protection, uint32 flags, uint32 tag,
				phys_addr_t physicalAddress);

/* File-backed dot creation. Uses existing VMVnodeCache from
 * the file's vnode. Multiple dots (and areas) can share the
 * same vnode cache -- coherence is automatic at the page level.
 */
kosm_handle_t kosm_create_dot_file_for(team_id team, int fd,
				off_t fileOffset, const char* name,
				void** address, uint32 addressSpec, size_t size,
				uint32 protection, uint32 flags, uint32 tag);

/* Sync dirty pages of a file-backed dot back to disk. */
status_t	kosm_dot_sync_internal(int32 internalId,
				size_t offset, size_t size);


/* --- Handle resolution --- */
/*
 * Resolve a per-process handle to an internal dot ID.
 * Used after handle table lookup in syscall implementations.
 * Returns B_BAD_VALUE on invalid handle.
 */
status_t	kosm_dot_resolve_handle(kosm_handle_t handle,
				uint32 requiredRights,
				int32* outInternalId);

/* Internal: close/destroy a dot by internal ID.
 * Called when a handle is closed (unmap this team's mapping) and
 * when KosmDotObject is destroyed (last handle, final cleanup).
 */
status_t	kosm_dot_close_for_team(int32 internalId, team_id team);
void		kosm_dot_destroy(int32 internalId);

/* Extract internal dot ID from a KernelReferenceable* obtained
   from the handle table. Does not modify refcounts. */
int32		kosm_dot_object_id(KernelReferenceable* object);


/* --- Memory tag accounting --- */

typedef struct {
	uint32		tag;
	size_t		total_size;
	size_t		resident_size;
	size_t		purgeable_volatile;
	size_t		purgeable_empty;
	uint32		dot_count;
} kosm_dot_tag_stats;

status_t	kosm_dot_get_tag_stats(uint32 tag, kosm_dot_tag_stats* stats);
status_t	kosm_dot_get_next_tag_stats(int32* cookie, kosm_dot_tag_stats* stats);


/* --- Address-space kosm_dot tree access --- */

KosmDotTreeNode* kosm_dot_lookup_address(team_id team,
					addr_t address);


/* --- KDL commands --- */
/*
 * Registered in kosm_dot_init():
 *   kosm_dots          -- list all active kosm_dots
 *   kosm_dots <team>   -- list kosm_dots for a team
 *   kosm_dots -t <tag> -- list kosm_dots by memory tag
 *   kosm_dot <id>      -- detailed info for one kosm_dot
 *   kosm_purge         -- manually trigger volatile purge
 */


/* --- Syscall entries --- */
/* All take per-process handles, not internal IDs.
 * Handle resolution + rights checking happens inside each syscall.
 * _user_kosm_transfer_dot removed: transfer is via handle table
 * (kosm_ray handle passing or kosm_duplicate_handle).
 */

kosm_dot_id	_user_kosm_create_dot(const char* name,
				void** address, uint32 addressSpec, size_t size,
				uint32 protection, uint32 flags, uint32 tag,
				phys_addr_t physicalAddress);
status_t	_user_kosm_delete_dot(kosm_dot_id handle);
status_t	_user_kosm_map_dot(kosm_dot_id handle,
				void** address, uint32 addressSpec, uint32 protection);
status_t	_user_kosm_unmap_dot(kosm_dot_id handle);
status_t	_user_kosm_protect_dot(kosm_dot_id handle,
				uint32 newProtection);
status_t	_user_kosm_dot_set_cache_policy(kosm_dot_id handle,
				uint8 cachePolicy);
status_t	_user_kosm_dot_mark_volatile(kosm_dot_id handle,
				uint8* oldState);
status_t	_user_kosm_dot_mark_nonvolatile(kosm_dot_id handle,
				uint8* oldState);
status_t	_user_kosm_dot_wire(kosm_dot_id handle,
				size_t offset, size_t size);
status_t	_user_kosm_dot_unwire(kosm_dot_id handle,
				size_t offset, size_t size);
status_t	_user_kosm_dot_get_phys(kosm_dot_id handle,
				size_t offset, phys_addr_t* physicalAddress);
status_t	_user_kosm_get_dot_info(kosm_dot_id handle,
				kosm_dot_info* info, size_t size);
status_t	_user_kosm_get_next_dot_info(team_id team, int32* cookie,
				kosm_dot_info* info, size_t size);

kosm_dot_id	_user_kosm_create_dot_file(int fd, off_t fileOffset,
				const char* name,
				void** address, uint32 addressSpec, size_t size,
				uint32 protection, uint32 flags, uint32 tag);
status_t	_user_kosm_dot_sync(kosm_dot_id handle,
				size_t offset, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_VM_KOSM_DOT_H */
