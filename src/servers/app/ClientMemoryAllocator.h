/*
 * Copyright 2006-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef CLIENT_MEMORY_ALLOCATOR_H
#define CLIENT_MEMORY_ALLOCATOR_H


#include <Locker.h>
#include <Referenceable.h>

#include <util/DoublyLinkedList.h>

#include <cstdint>
#include <memory>


class ServerApp;
struct chunk;
struct block;

struct chunk : DoublyLinkedListLinkImpl<chunk> {
	area_id	area{};
	uint8*	base{nullptr};
	size_t	size{0};
};

struct block : DoublyLinkedListLinkImpl<block> {
	struct chunk* chunk{nullptr};
	uint8*	base{nullptr};
	size_t	size{0};
};

using block_list = DoublyLinkedList<block>;
using chunk_list = DoublyLinkedList<chunk>;


class ClientMemoryAllocator : public BReferenceable {
public:
								ClientMemoryAllocator(ServerApp* application);
								~ClientMemoryAllocator();
								
								// Disable copy construction and assignment
								ClientMemoryAllocator(const ClientMemoryAllocator&) = delete;
								ClientMemoryAllocator& operator=(const ClientMemoryAllocator&) = delete;

			void*				Allocate(size_t size, block** _address,
									bool& newArea);
			void				Free(block* cookie);

			void				Detach();

			void				Dump();

private:
			block*				_AllocateChunk(size_t size, bool& newArea);

private:
			ServerApp*			fApplication;
			BLocker				fLock;
			chunk_list			fChunks;
			block_list			fFreeBlocks;
};


class AreaMemory {
public:
	virtual						~AreaMemory() = default;

	virtual area_id				Area() = 0;
	virtual uint8*				Address() = 0;
	virtual uint32				AreaOffset() = 0;
};


class ClientMemory : public AreaMemory {
public:
								ClientMemory() = default;

	virtual						~ClientMemory() override;

			void*				Allocate(ClientMemoryAllocator* allocator,
									size_t size, bool& newArea);

	area_id						Area() override;
	uint8*						Address() override;
	uint32						AreaOffset() override;

private:
			BReference<ClientMemoryAllocator>
								fAllocator;
			block*				fBlock{nullptr};
};


/*! Just clones an existing area. */
class ClonedAreaMemory : public AreaMemory {
public:
								ClonedAreaMemory() = default;
	virtual						~ClonedAreaMemory() override;

			void*				Clone(area_id area, uint32 offset);

	area_id						Area() override;
	uint8*						Address() override;
	uint32						AreaOffset() override;

private:
			area_id		fClonedArea{-1};
			uint32		fOffset{0};
			uint8*		fBase{nullptr};
};


#endif	/* CLIENT_MEMORY_ALLOCATOR_H */
