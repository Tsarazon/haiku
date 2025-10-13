/*
 * Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2008, Jérôme Duval.
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <new>

#include <KernelExport.h>

#include <boot/kernel_args.h>
#include <smp.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>

#include <arch/vm.h>
#include <arch/int.h>
#include <arch/cpu.h>

#include <arch/x86/bios.h>


//#define TRACE_ARCH_VM
#ifdef TRACE_ARCH_VM
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define TRACE_MTRR_ARCH_VM 1

#if TRACE_MTRR_ARCH_VM >= 1
#	define TRACE_MTRR(x...) dprintf(x)
#else
#	define TRACE_MTRR(x...)
#endif

#if TRACE_MTRR_ARCH_VM >= 2
#	define TRACE_MTRR2(x...) dprintf(x)
#else
#	define TRACE_MTRR2(x...)
#endif


static const addr_t kDMARegionEnd = 0xa0000;
static const size_t kMaxUpdateMTRRRetries = 100;
static const uint32 kMaxMemoryTypeRegisters = 32;


void *gDmaAddress;


namespace {

	struct memory_type_range : DoublyLinkedListLinkImpl<memory_type_range> {
		uint64						base;
		uint64						size;
		uint32						type;
		area_id						area;
	};


	struct memory_type_range_point
	: DoublyLinkedListLinkImpl<memory_type_range_point> {
		uint64				address;
		memory_type_range*	range;

		bool IsStart() const	{ return range->base == address; }

		bool operator<(const memory_type_range_point& other) const
		{
			return address < other.address;
		}
	};


	struct update_mtrr_info {
		uint64	ignoreUncacheableSize;
		uint64	shortestUncacheableSize;
	};


	typedef DoublyLinkedList<memory_type_range> MemoryTypeRangeList;

} // namespace


static mutex sMemoryTypeLock = MUTEX_INITIALIZER("memory type ranges");
static MemoryTypeRangeList sMemoryTypeRanges;
static int32 sMemoryTypeRangeCount = 0;

static x86_mtrr_info sMemoryTypeRegisters[kMaxMemoryTypeRegisters];
static uint32 sMemoryTypeRegisterCount;
static uint32 sMemoryTypeRegistersUsed;

static memory_type_range* sTemporaryRanges = NULL;
static memory_type_range_point* sTemporaryRangePoints = NULL;
static int32 sTemporaryRangeCount = 0;
static int32 sTemporaryRangePointCount = 0;


static void
set_mtrrs()
{
	x86_set_mtrrs(IA32_MTR_WRITE_BACK, sMemoryTypeRegisters,
				  sMemoryTypeRegistersUsed);

	#if TRACE_MTRR_ARCH_VM
	TRACE_MTRR("set MTRRs to:\n");
	for (uint32 i = 0; i < sMemoryTypeRegistersUsed; i++) {
		const x86_mtrr_info& info = sMemoryTypeRegisters[i];
		TRACE_MTRR("  mtrr: %2" B_PRIu32 ": base: %#10" B_PRIx64  ", size: %#10"
		B_PRIx64 ", type: %u\n", i, info.base, info.size,
		info.type);
	}
	#endif
}


static bool
add_used_mtrr(uint64 base, uint64 size, uint32 type)
{
	switch (type) {
		case B_UNCACHED_MEMORY:
			type = IA32_MTR_UNCACHED;
			break;
		case B_WRITE_COMBINING_MEMORY:
			type = IA32_MTR_WRITE_COMBINING;
			break;
		case B_WRITE_THROUGH_MEMORY:
			type = IA32_MTR_WRITE_THROUGH;
			break;
		case B_WRITE_PROTECTED_MEMORY:
			type = IA32_MTR_WRITE_PROTECTED;
			break;
		case B_WRITE_BACK_MEMORY:
			type = IA32_MTR_WRITE_BACK;
			break;
		default:
			return false;
	}

	if (sMemoryTypeRegistersUsed >= sMemoryTypeRegisterCount) {
		TRACE_MTRR("add_used_mtrr: out of MTRRs (used %" B_PRIu32 ", max %"
		B_PRIu32 ")\n", sMemoryTypeRegistersUsed,
		sMemoryTypeRegisterCount);
		return false;
	}

	x86_mtrr_info& mtrr = sMemoryTypeRegisters[sMemoryTypeRegistersUsed++];
	mtrr.base = base;
	mtrr.size = size;
	mtrr.type = type;

	return true;
}


static bool
add_mtrrs_for_range(uint64 base, uint64 size, uint32 type)
{
	for (uint64 interval = B_PAGE_SIZE; size > 0; interval <<= 1) {
		if ((base & interval) != 0) {
			if (!add_used_mtrr(base, interval, type))
				return false;
			base += interval;
			size -= interval;
		}

		if ((size & interval) != 0) {
			if (!add_used_mtrr(base + size - interval, interval, type))
				return false;
			size -= interval;
		}
	}

	return true;
}


static memory_type_range*
find_range_locked(area_id areaID)
{
	ASSERT_LOCKED_MUTEX(&sMemoryTypeLock);

	for (MemoryTypeRangeList::Iterator it = sMemoryTypeRanges.GetIterator();
		 memory_type_range* range = it.Next();) {
		if (range->area == areaID)
			return range;
		 }

		 return NULL;
}


static void
optimize_memory_ranges(MemoryTypeRangeList& ranges, uint32 type,
					   bool removeRanges)
{
	uint64 previousEnd = 0;
	uint64 nextStart = 0;
	MemoryTypeRangeList::Iterator it = ranges.GetIterator();
	memory_type_range* range = it.Next();
	while (range != NULL) {
		if (range->type != type) {
			previousEnd = range->base + range->size;
			nextStart = 0;
			range = it.Next();
			continue;
		}

		if (nextStart == 0) {
			MemoryTypeRangeList::Iterator nextIt = it;
			while (memory_type_range* nextRange = nextIt.Next()) {
				if (nextRange->type != range->type) {
					nextStart = nextRange->base;
					break;
				}
			}

			if (nextStart == 0)
				nextStart = (uint64)1 << 32;
		}

		uint64 rangeBase = range->base;
		uint64 rangeEnd = rangeBase + range->size;
		uint64 interval = B_PAGE_SIZE * 2;
		while (true) {
			uint64 alignedBase = rangeBase & ~(interval - 1);
			uint64 alignedEnd = (rangeEnd + interval - 1) & ~(interval - 1);

			if (alignedBase < previousEnd)
				alignedBase += interval;

			if (alignedEnd > nextStart)
				alignedEnd -= interval;

			if (alignedBase >= alignedEnd)
				break;

			rangeBase = std::min(rangeBase, alignedBase);
			rangeEnd = std::max(rangeEnd, alignedEnd);

			interval <<= 1;
		}

		range->base = rangeBase;
		range->size = rangeEnd - rangeBase;

		if (removeRanges)
			it.Remove();

		previousEnd = rangeEnd;

		while ((range = it.Next()) != NULL) {
			if (range->base >= rangeEnd)
				break;

			if (range->base + range->size > rangeEnd) {
				range->size = range->base + range->size - rangeEnd;
				range->base = rangeEnd;
				break;
			}

			range->size = 0;
			it.Remove();
		}
	}
}


static bool
ensure_temporary_ranges_space(int32 count)
{
	if (sTemporaryRangeCount >= count && sTemporaryRangePointCount >= count)
		return true;

	int32 unalignedCount = count;
	count = 8;
	while (count < unalignedCount)
		count <<= 1;

	if (sTemporaryRangeCount < count) {
		memory_type_range* ranges = new(std::nothrow) memory_type_range[count];
		if (ranges == NULL)
			return false;

		delete[] sTemporaryRanges;
		sTemporaryRanges = ranges;
		sTemporaryRangeCount = count;
	}

	if (sTemporaryRangePointCount < count) {
		memory_type_range_point* points
		= new(std::nothrow) memory_type_range_point[count];
		if (points == NULL)
			return false;

		delete[] sTemporaryRangePoints;
		sTemporaryRangePoints = points;
		sTemporaryRangePointCount = count;
	}

	return true;
}


static status_t
update_mtrrs(update_mtrr_info& updateInfo)
{
	if (!ensure_temporary_ranges_space(sMemoryTypeRangeCount * 2))
		return B_NO_MEMORY;

	memory_type_range_point* rangePoints = sTemporaryRangePoints;
	int32 pointCount = 0;
	for (MemoryTypeRangeList::Iterator it = sMemoryTypeRanges.GetIterator();
		 memory_type_range* range = it.Next();) {
		if (range->type == B_UNCACHED_MEMORY) {
			if (range->size <= updateInfo.ignoreUncacheableSize)
				continue;
			if (range->size < updateInfo.shortestUncacheableSize)
				updateInfo.shortestUncacheableSize = range->size;
		}

		rangePoints[pointCount].address = range->base;
	rangePoints[pointCount++].range = range;
	rangePoints[pointCount].address = range->base + range->size;
	rangePoints[pointCount++].range = range;
		 }

		 std::sort(rangePoints, rangePoints + pointCount);

		 #if TRACE_MTRR_ARCH_VM >= 2
		 TRACE_MTRR2("memory type range points:\n");
		 for (int32 i = 0; i < pointCount; i++) {
			 TRACE_MTRR2("%12" B_PRIx64 " (%p)\n", rangePoints[i].address,
						 rangePoints[i].range);
		 }
		 #endif

		 memory_type_range* ranges = sTemporaryRanges;
		 typedef DoublyLinkedList<memory_type_range_point> PointList;
		 PointList pendingPoints;
		 memory_type_range* activeRange = NULL;
		 int32 rangeCount = 0;

		 for (int32 i = 0; i < pointCount; i++) {
			 memory_type_range_point* point = &rangePoints[i];
			 bool terminateRange = false;
			 if (point->IsStart()) {
				 pendingPoints.Add(point);
				 if (activeRange != NULL && activeRange->type > point->range->type)
					 terminateRange = true;
			 } else {
				 for (PointList::Iterator it = pendingPoints.GetIterator();
					  memory_type_range_point* pendingPoint = it.Next();) {
					 if (pendingPoint->range == point->range) {
						 it.Remove();
						 break;
					 }
					  }

					  if (point->range == activeRange)
						  terminateRange = true;
			 }

			 if (terminateRange) {
				 ranges[rangeCount].size = point->address - ranges[rangeCount].base;
				 rangeCount++;
				 activeRange = NULL;
			 }

			 if (activeRange != NULL || pendingPoints.IsEmpty())
				 continue;

			 for (PointList::Iterator it = pendingPoints.GetIterator();
				  memory_type_range_point* pendingPoint = it.Next();) {
				 memory_type_range* pendingRange = pendingPoint->range;
			 if (activeRange == NULL || activeRange->type > pendingRange->type)
				 activeRange = pendingRange;
				  }

				  memory_type_range* previousRange = rangeCount > 0
				  ? &ranges[rangeCount - 1] : NULL;
				  if (previousRange == NULL || previousRange->type != activeRange->type
					  || previousRange->base + previousRange->size
					  < activeRange->base) {
					  ranges[rangeCount].base = point->address;
				  ranges[rangeCount].type = activeRange->type;
					  } else
						  rangeCount--;
		 }

		 #if TRACE_MTRR_ARCH_VM >= 2
		 TRACE_MTRR2("effective memory type ranges:\n");
		 for (int32 i = 0; i < rangeCount; i++) {
			 TRACE_MTRR2("%12" B_PRIx64 " - %12" B_PRIx64 ": %" B_PRIu32 "\n",
						 ranges[i].base, ranges[i].base + ranges[i].size, ranges[i].type);
		 }
		 #endif

		 MemoryTypeRangeList rangeList;
		 for (int32 i = 0; i < rangeCount; i++)
			 rangeList.Add(&ranges[i]);

	static const uint32 kMemoryTypes[] = {
		B_UNCACHED_MEMORY,
		B_WRITE_COMBINING_MEMORY,
		B_WRITE_PROTECTED_MEMORY,
		B_WRITE_THROUGH_MEMORY,
		B_WRITE_BACK_MEMORY
	};
	static const int32 kMemoryTypeCount = B_COUNT_OF(kMemoryTypes);

	for (int32 i = 0; i < kMemoryTypeCount; i++) {
		uint32 type = kMemoryTypes[i];
		bool removeRanges = type == B_UNCACHED_MEMORY
		|| type == B_WRITE_THROUGH_MEMORY;
		optimize_memory_ranges(rangeList, type, removeRanges);
	}

	#if TRACE_MTRR_ARCH_VM >= 2
	TRACE_MTRR2("optimized memory type ranges:\n");
	for (int32 i = 0; i < rangeCount; i++) {
		if (ranges[i].size > 0) {
			TRACE_MTRR2("%12" B_PRIx64 " - %12" B_PRIx64 ": %" B_PRIu32 "\n",
						ranges[i].base, ranges[i].base + ranges[i].size,
			   ranges[i].type);
		}
	}
	#endif

	sMemoryTypeRegistersUsed = 0;
	for (int32 i = 0; i < kMemoryTypeCount; i++) {
		uint32 type = kMemoryTypes[i];

		if (type == B_WRITE_BACK_MEMORY)
			continue;

		for (int32 i = 0; i < rangeCount; i++) {
			if (ranges[i].size == 0 || ranges[i].type != type)
				continue;

			if (!add_mtrrs_for_range(ranges[i].base, ranges[i].size, type))
				return B_BUSY;
		}
	}

	set_mtrrs();

	return B_OK;
}


static status_t
update_mtrrs()
{
	if (sMemoryTypeRegisterCount == 0)
		return B_OK;

	update_mtrr_info updateInfo;
	updateInfo.ignoreUncacheableSize = 0;

	for (size_t retry = 0; retry < kMaxUpdateMTRRRetries; retry++) {
		TRACE_MTRR2("update_mtrrs(): attempt %" B_PRIuSIZE
		" with ignoreUncacheableSize %#" B_PRIx64 "\n",
		retry + 1, updateInfo.ignoreUncacheableSize);

		updateInfo.shortestUncacheableSize = ~(uint64)0;
		status_t error = update_mtrrs(updateInfo);
		if (error != B_BUSY) {
			if (error == B_OK && updateInfo.ignoreUncacheableSize > 0) {
				TRACE_MTRR("update_mtrrs(): succeeded after ignoring "
				"uncacheable ranges up to size %#" B_PRIx64 "\n",
			   updateInfo.ignoreUncacheableSize);
			}
			return error;
		}

		if (updateInfo.shortestUncacheableSize == ~(uint64)0) {
			dprintf("update_mtrrs: out of MTRRs after %" B_PRIuSIZE
			" attempts\n", retry + 1);
			return B_BUSY;
		}

		ASSERT(updateInfo.ignoreUncacheableSize
		< updateInfo.shortestUncacheableSize);

		updateInfo.ignoreUncacheableSize = updateInfo.shortestUncacheableSize;
	}

	dprintf("update_mtrrs: failed after %" B_PRIuSIZE " retries\n",
			kMaxUpdateMTRRRetries);
	return B_BUSY;
}


static status_t
add_memory_type_range(area_id areaID, uint64 base, uint64 size, uint32 type,
					  uint32 *effectiveType)
{
	if (type == 0)
		return B_OK;

	uint64 rangeEnd = base + size;
	if (rangeEnd < base) {
		dprintf("add_memory_type_range: overflow in range [%#" B_PRIx64
		", %#" B_PRIx64 "]\n", base, size);
		return B_BAD_VALUE;
	}

	TRACE_MTRR2("add_memory_type_range(%" B_PRId32 ", %#" B_PRIx64 ", %#"
	B_PRIx64 ", %" B_PRIu32 ")\n", areaID, base, size, type);

	MutexLocker locker(sMemoryTypeLock);

	for (MemoryTypeRangeList::Iterator it = sMemoryTypeRanges.GetIterator();
		 memory_type_range* range = it.Next(); ) {

		if (range->area == areaID || range->type == type
			|| base + size <= range->base
			|| base >= range->base + range->size) {
			continue;
			}

			if (range->area == -1 && !x86_use_pat()) {
				continue;
			}

			if (effectiveType != NULL) {
				type = *effectiveType = range->type;
				effectiveType = NULL;

				dprintf("assuming memory type %" B_PRIx32 " for overlapping %#"
				B_PRIx64 ", %#" B_PRIx64 " area %" B_PRId32 " from existing %#"
				B_PRIx64 ", %#" B_PRIx64 " area %" B_PRId32 "\n", type,
			base, size, areaID, range->base, range->size, range->area);
				continue;
			}

			(KDEBUG ? panic : dprintf)("incompatible overlapping memory %#" B_PRIx64
			", %#" B_PRIx64 " type %" B_PRIx32 " area %" B_PRId32
			" with existing %#" B_PRIx64 ", %#" B_PRIx64 " type %" B_PRIx32
			" area %" B_PRId32 "\n", base, size, type, areaID, range->base,
			range->size, range->type, range->area);
			return B_BUSY;
		 }

		 memory_type_range* range = areaID >= 0 ? find_range_locked(areaID) : NULL;
		 int32 oldRangeType = -1;
		 if (range != NULL) {
			 if (range->base != base || range->size != size) {
				 dprintf("add_memory_type_range: range mismatch for area %"
				 B_PRId32 "\n", areaID);
				 return B_BAD_VALUE;
			 }
			 if (range->type == type)
				 return B_OK;

			 oldRangeType = range->type;
			 range->type = type;
		 } else {
			 range = new(std::nothrow) memory_type_range;
			 if (range == NULL)
				 return B_NO_MEMORY;

			 range->area = areaID;
			 range->base = base;
			 range->size = size;
			 range->type = type;
			 sMemoryTypeRanges.Add(range);
			 sMemoryTypeRangeCount++;
		 }

		 status_t error = update_mtrrs();
		 if (error != B_OK) {
			 if (oldRangeType < 0) {
				 sMemoryTypeRanges.Remove(range);
				 sMemoryTypeRangeCount--;
				 delete range;
			 } else
				 range->type = oldRangeType;

			 update_mtrrs();
			 return error;
		 }

		 return B_OK;
}


static void
remove_memory_type_range(area_id areaID)
{
	MutexLocker locker(sMemoryTypeLock);

	memory_type_range* range = find_range_locked(areaID);
	if (range != NULL) {
		TRACE_MTRR2("remove_memory_type_range(%" B_PRId32 ", %#" B_PRIx64 ", %#"
		B_PRIx64 ", %" B_PRIu32 ")\n", range->area, range->base,
		range->size, range->type);

		sMemoryTypeRanges.Remove(range);
		sMemoryTypeRangeCount--;
		delete range;

		update_mtrrs();
	} else {
		dprintf("remove_memory_type_range(): no range known for area %" B_PRId32
		"\n", areaID);
	}
}


static const char *
memory_type_to_string(uint32 type)
{
	switch (type) {
		case B_UNCACHED_MEMORY:
			return "uncacheable";
		case B_WRITE_COMBINING_MEMORY:
			return "write combining";
		case B_WRITE_THROUGH_MEMORY:
			return "write-through";
		case B_WRITE_PROTECTED_MEMORY:
			return "write-protected";
		case B_WRITE_BACK_MEMORY:
			return "write-back";
		default:
			return "unknown";
	}
}


static int
dump_memory_type_ranges(int argc, char **argv)
{
	kprintf(
		"start            end              size             area     type\n");

	for (MemoryTypeRangeList::Iterator it = sMemoryTypeRanges.GetIterator();
		 memory_type_range* range = it.Next();) {

		kprintf("%#16" B_PRIx64 " %#16" B_PRIx64 " %#16" B_PRIx64 " % 8"
		B_PRId32 " %#" B_PRIx32 " %s\n", range->base,
		range->base + range->size, range->size, range->area, range->type,
		memory_type_to_string(range->type));
		 }

		 return 0;
}


//	#pragma mark -


status_t
arch_vm_init(kernel_args *args)
{
	TRACE(("arch_vm_init: entry\n"));
	return B_OK;
}


status_t
arch_vm_init_post_area(kernel_args *args)
{
	TRACE(("arch_vm_init_post_area: entry\n"));

	vm_mark_page_range_inuse(0x0, kDMARegionEnd / B_PAGE_SIZE);

	area_id id = map_physical_memory("dma_region", 0x0, kDMARegionEnd,
									 B_ANY_KERNEL_ADDRESS | B_WRITE_BACK_MEMORY,
								  B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &gDmaAddress);
	if (id < 0) {
		panic("arch_vm_init_post_area: unable to map dma region: %s",
			  strerror(id));
		return B_NO_MEMORY;
	}

	add_debugger_command_etc("memory_type_ranges", &dump_memory_type_ranges,
							 "List all configured memory type ranges",
						  "\n"
						  "Lists all memory type ranges with their types and areas.\n", 0);

	#ifndef __x86_64__
	return bios_init();
	#else
	return B_OK;
	#endif
}


status_t
arch_vm_init_end(kernel_args *args)
{
	TRACE(("arch_vm_init_end: entry\n"));

	vm_free_unused_boot_loader_range(KERNEL_LOAD_BASE,
									 args->arch_args.virtual_end - KERNEL_LOAD_BASE);

	return B_OK;
}


status_t
arch_vm_init_post_modules(kernel_args *args)
{
	sMemoryTypeRegisterCount = x86_count_mtrrs();
	if (sMemoryTypeRegisterCount == 0)
		return B_OK;

	if (sMemoryTypeRegisterCount > kMaxMemoryTypeRegisters) {
		dprintf("arch_vm_init_post_modules: capping MTRR count from %"
		B_PRIu32 " to %" B_PRIu32 "\n", sMemoryTypeRegisterCount,
		kMaxMemoryTypeRegisters);
		sMemoryTypeRegisterCount = kMaxMemoryTypeRegisters;
	}

	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		add_memory_type_range(-1, args->physical_memory_range[i].start,
							  args->physical_memory_range[i].size, B_WRITE_BACK_MEMORY, NULL);
	}

	return B_OK;
}


void
arch_vm_aspace_swap(struct VMAddressSpace *from, struct VMAddressSpace *to)
{
}


bool
arch_vm_supports_protection(uint32 protection)
{
	if ((protection & (B_READ_AREA | B_WRITE_AREA)) == B_READ_AREA
		&& (protection & B_KERNEL_WRITE_AREA) != 0) {
		return false;
		}

		if ((protection & (B_READ_AREA | B_WRITE_AREA)) != 0
			&& (protection & B_EXECUTE_AREA) == 0
			&& (protection & B_KERNEL_EXECUTE_AREA) != 0) {
			return false;
			}

			return true;
}


void
arch_vm_unset_memory_type(struct VMArea *area)
{
	if (area->MemoryType() == 0)
		return;

	remove_memory_type_range(area->id);
}


status_t
arch_vm_set_memory_type(struct VMArea *area, phys_addr_t physicalBase,
						uint32 type, uint32 *effectiveType)
{
	return add_memory_type_range(area->id, physicalBase, area->Size(), type,
								 effectiveType);
}
