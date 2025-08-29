/*
 * Copyright 2006-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


/*!	This class manages a pool of areas for one client. The client is supposed
	to clone these areas into its own address space to access the data.
	This mechanism is only used for bitmaps for far.
*/


// TODO: areas could be relocated if needed (to be able to resize them)
//		However, this would require a lock whenever a block of memory
//		allocated by this allocator is accessed.


#include "ClientMemoryAllocator.h"

#include <stdio.h>
#include <stdlib.h>

#include <Autolock.h>

#include "ServerApp.h"


using block_iterator = block_list::Iterator;
using chunk_iterator = chunk_list::Iterator;


ClientMemoryAllocator::ClientMemoryAllocator(ServerApp* application)
	:
	fApplication(application),
	fLock("client memory lock")
{
}


ClientMemoryAllocator::~ClientMemoryAllocator()
{
	// delete all areas and chunks/blocks that are still allocated

	while (auto* block = fFreeBlocks.RemoveHead()) {
		free(block);
	}

	while (auto* chunk = fChunks.RemoveHead()) {
		delete_area(chunk->area);
		free(chunk);
	}
}


void*
ClientMemoryAllocator::Allocate(size_t size, block** _address, bool& newArea)
{
	// A detached allocator no longer allows any further allocations
	if (fApplication == nullptr)
		return nullptr;

	BAutolock locker(fLock);

	// Search best matching free block from the list

	auto iterator = fFreeBlocks.GetIterator();
	block* best = nullptr;

	while (auto* block = iterator.Next()) {
		if (block->size >= size && (best == nullptr || block->size < best->size))
			best = block;
	}

	if (best == nullptr) {
		// We didn't find a free block - we need to allocate
		// another chunk, or resize an existing chunk
		best = _AllocateChunk(size, newArea);
		if (best == nullptr)
			return nullptr;
	} else
		newArea = false;

	// We need to split the chunk into two parts: the one to keep
	// and the one to give away

	if (best->size == size) {
		// The simple case: the free block has exactly the size we wanted to have
		fFreeBlocks.Remove(best);
		*_address = best;
		return best->base;
	}

	// TODO: maybe we should have the user reserve memory in its object
	//	for us, so we don't have to do this here...

	auto* usedBlock = static_cast<block*>(malloc(sizeof(block)));
	if (usedBlock == nullptr)
		return nullptr;

	usedBlock->base = best->base;
	usedBlock->size = size;
	usedBlock->chunk = best->chunk;

	best->base += size;
	best->size -= size;

	*_address = usedBlock;
	return usedBlock->base;
}


void
ClientMemoryAllocator::Free(block* freeBlock)
{
	if (freeBlock == nullptr)
		return;

	BAutolock locker(fLock);

	// search for an adjacent free block

	auto iterator = fFreeBlocks.GetIterator();
	block* before = nullptr;
	block* after = nullptr;
	bool inFreeList = true;

	if (freeBlock->size != freeBlock->chunk->size) {
		// TODO: this could be done better if free blocks are sorted,
		//	and if we had one free blocks list per chunk!
		//	IOW this is a bit slow...

		while (auto* block = iterator.Next()) {
			if (block->chunk != freeBlock->chunk)
				continue;

			if (block->base + block->size == freeBlock->base)
				before = block;

			if (block->base == freeBlock->base + freeBlock->size)
				after = block;
		}

		if (before != nullptr && after != nullptr) {
			// merge with adjacent blocks
			before->size += after->size + freeBlock->size;
			fFreeBlocks.Remove(after);
			free(after);
			free(freeBlock);
			freeBlock = before;
		} else if (before != nullptr) {
			before->size += freeBlock->size;
			free(freeBlock);
			freeBlock = before;
		} else if (after != nullptr) {
			after->base -= freeBlock->size;
			after->size += freeBlock->size;
			free(freeBlock);
			freeBlock = after;
		} else
			fFreeBlocks.Add(freeBlock);
	} else
		inFreeList = false;

	if (freeBlock->size == freeBlock->chunk->size) {
		// We can delete the chunk now
		auto* chunk = freeBlock->chunk;

		if (inFreeList)
			fFreeBlocks.Remove(freeBlock);
		free(freeBlock);

		fChunks.Remove(chunk);
		delete_area(chunk->area);

		if (fApplication != nullptr)
			fApplication->NotifyDeleteClientArea(chunk->area);

		free(chunk);
	}
}


void
ClientMemoryAllocator::Detach()
{
	BAutolock locker(fLock);
	fApplication = nullptr;
}


void
ClientMemoryAllocator::Dump()
{
	if (fApplication != nullptr) {
		debug_printf("Application %" B_PRId32 ", %s: chunks:\n",
			fApplication->ClientTeam(), fApplication->Signature());
	}

	auto iterator = fChunks.GetIterator();
	int32 i = 0;
	while (auto* chunk = iterator.Next()) {
		debug_printf("  [%4" B_PRId32 "] %p, area %" B_PRId32 ", base %p, "
			"size %lu\n", i++, chunk, chunk->area, chunk->base, chunk->size);
	}

	debug_printf("free blocks:\n");

	auto blockIterator = fFreeBlocks.GetIterator();
	i = 0;
	while (auto* block = blockIterator.Next()) {
		debug_printf("  [%6" B_PRId32 "] %p, chunk %p, base %p, size %lu\n",
			i++, block, block->chunk, block->base, block->size);
	}
}


block*
ClientMemoryAllocator::_AllocateChunk(size_t size, bool& newArea)
{
	// round up to multiple of page size
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	// At first, try to resize our existing areas

	auto iterator = fChunks.GetIterator();
	chunk* chunk = nullptr;
	while ((chunk = iterator.Next()) != nullptr) {
		status_t status = resize_area(chunk->area, chunk->size + size);
		if (status == B_OK) {
			newArea = false;
			break;
		}
	}

	// TODO: resize and relocate while holding the write lock

	block* block;
	uint8* address;

	if (chunk == nullptr) {
		// TODO: temporary measurement as long as resizing areas doesn't
		//	work the way we need (with relocating the area, if needed)
		if (size < B_PAGE_SIZE * 32)
			size = B_PAGE_SIZE * 32;

		// create new area for this allocation
		chunk = static_cast<struct chunk*>(malloc(sizeof(struct chunk)));
		if (chunk == nullptr)
			return nullptr;

		block = static_cast<struct block*>(malloc(sizeof(struct block)));
		if (block == nullptr) {
			free(chunk);
			return nullptr;
		}

		char name[B_OS_NAME_LENGTH];
#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
		strcpy(name, "client heap");
#else
		snprintf(name, sizeof(name), "heap:%" B_PRId32 ":%s",
			fApplication->ClientTeam(), fApplication->SignatureLeaf());
#endif
		area_id area = create_area(name, (void**)&address, B_ANY_ADDRESS, size,
			B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
		if (area < B_OK) {
			free(block);
			free(chunk);
			return nullptr;
		}

		// add chunk to list

		chunk->area = area;
		chunk->base = address;
		chunk->size = size;

		fChunks.Add(chunk);
		newArea = true;
	} else {
		// create new free block for this chunk
		block = static_cast<struct block*>(malloc(sizeof(struct block)));
		if (block == nullptr)
			return nullptr;

		address = chunk->base + chunk->size;
		chunk->size += size;
	}

	// add block to free list

	block->chunk = chunk;
	block->base = address;
	block->size = size;

	fFreeBlocks.Add(block);

	return block;
}


// #pragma mark -


// Constructor now defaulted in header file


ClientMemory::~ClientMemory()
{
	if (fAllocator != nullptr) {
		if (fBlock != nullptr)
			fAllocator->Free(fBlock);
		fAllocator.Unset();
	}
}


void*
ClientMemory::Allocate(ClientMemoryAllocator* allocator, size_t size,
	bool& newArea)
{
	fAllocator.SetTo(allocator, false);

	return fAllocator->Allocate(size, &fBlock, newArea);
}


area_id
ClientMemory::Area()
{
	if (fBlock != nullptr)
		return fBlock->chunk->area;
	return B_ERROR;
}


uint8*
ClientMemory::Address()
{
	if (fBlock != nullptr)
		return fBlock->base;
	return nullptr;
}


uint32
ClientMemory::AreaOffset()
{
	if (fBlock != nullptr)
		return fBlock->base - fBlock->chunk->base;
	return 0;
}


// #pragma mark -


// Constructor now defaulted in header file


ClonedAreaMemory::~ClonedAreaMemory()
{
	if (fClonedArea >= 0)
		delete_area(fClonedArea);
}


void*
ClonedAreaMemory::Clone(area_id area, uint32 offset)
{
	fClonedArea = clone_area("server_memory", (void**)&fBase, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, area);
	if (fBase == nullptr)
		return nullptr;
	fOffset = offset;
	return Address();
}


area_id
ClonedAreaMemory::Area()
{
	return fClonedArea;
}


uint8*
ClonedAreaMemory::Address()
{
	return fBase + fOffset;
}


uint32
ClonedAreaMemory::AreaOffset()
{
	return fOffset;
}
