# Surface Kit — Mobile Haiku

Подсистема управления графическими буферами для Mobile Haiku.
Минимальный API по образцу iOS IOSurface.

Surface Kit — это только контейнер памяти. Он не занимается:
- Рендерингом (blit, fill, blend) → Graphics Kit
- Dirty tracking → Animation Kit / Compositor
- Buffer pooling → Display Kit
- Синхронизацией → OS primitives

## Сравнение с iOS

| iOS IOSurface | Surface Kit |
|---------------|-------------|
| `IOSurfaceCreate()` | `KosmSurfaceAllocator::Allocate()` |
| `IOSurfaceLookup()` | `KosmSurfaceAllocator::Lookup()` |
| `IOSurfaceGetID()` | `KosmSurface::ID()` |
| `IOSurfaceGetWidth/Height/...` | `KosmSurface::Width/Height/...` |
| `IOSurfaceLock/Unlock()` | `KosmSurface::Lock/Unlock()` |
| `IOSurfaceGetSeed()` | `KosmSurface::Seed()` |
| `IOSurfaceGetBaseAddress()` | `KosmSurface::BaseAddress()` |
| `IOSurfaceGet*OfPlane()` | `KosmSurface::*OfPlane()` |
| `IOSurfaceIncrement/DecrementUseCount()` | `KosmSurface::Increment/DecrementUseCount()` |
| `IOSurfaceGetUseCount()` | `KosmSurface::LocalUseCount()` |
| `IOSurfaceIsInUse()` | `KosmSurface::IsInUse()` (global) |
| `IOSurfaceSet/Copy/RemoveValue()` | `KosmSurface::Set/Get/RemoveAttachment()` |
| `IOSurfaceCreateMachPort()` | `KosmSurface::Area()` + `clone_area()` |

## Структура файлов

```
headers/
└── os/
    └── surface/
        ├── SurfaceTypes.h
        ├── KosmSurface.h
        └── KosmSurfaceAllocator.h

headers/
└── private/
    └── surface/
        ├── SurfaceBuffer.h
        ├── SurfaceRegistry.h
        ├── AllocationBackend.h
        └── PlanarLayout.h

src/kits/surface/
├── KosmSurface.cpp
├── KosmSurfaceAllocator.cpp
├── SurfaceBuffer.cpp
├── SurfaceRegistry.cpp
├── AreaBackend.cpp
└── PlanarLayout.cpp

src/tests/kits/surface/
├── SurfaceTest.cpp
├── PlanarSurfaceTest.cpp
├── AllocatorTest.cpp
└── IPCTest.cpp
```

## Публичные заголовки

### SurfaceTypes.h

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SURFACE_TYPES_H
#define _SURFACE_TYPES_H

#include <SupportDefs.h>

typedef uint32 surface_id;

enum {
	PIXEL_FORMAT_RGBA8888 = 0,
	PIXEL_FORMAT_BGRA8888,
	PIXEL_FORMAT_RGB565,
	PIXEL_FORMAT_RGBX8888,
	PIXEL_FORMAT_NV12,
	PIXEL_FORMAT_NV21,
	PIXEL_FORMAT_YV12
};
typedef uint32 pixel_format;

enum {
	SURFACE_USAGE_CPU_READ		= 0x0001,
	SURFACE_USAGE_CPU_WRITE		= 0x0002,
	SURFACE_USAGE_GPU_TEXTURE	= 0x0004,
	SURFACE_USAGE_GPU_RENDER	= 0x0008,
	SURFACE_USAGE_COMPOSITOR	= 0x0010,
	SURFACE_USAGE_VIDEO			= 0x0020,
	SURFACE_USAGE_CAMERA		= 0x0040,
	SURFACE_USAGE_PROTECTED		= 0x0080,
	SURFACE_USAGE_PURGEABLE		= 0x0100
};

enum {
	SURFACE_LOCK_READ_ONLY		= 0x0001,
	SURFACE_LOCK_AVOID_SYNC		= 0x0002
};

typedef struct {
	uint32			width;
	uint32			height;
	pixel_format	format;
	uint32			usage;
	uint32			bytesPerElement;
	uint32			bytesPerRow;
} surface_desc;

typedef struct {
	uint32			width;
	uint32			height;
	uint32			bytesPerElement;
	uint32			bytesPerRow;
	size_t			offset;
} plane_info;

enum {
	B_SURFACE_NOT_LOCKED = B_ERRORS_END + 0x1000,
	B_SURFACE_ALREADY_LOCKED,
	B_SURFACE_IN_USE,
	B_SURFACE_PURGEABLE
};

#ifdef __cplusplus

inline void
surface_desc_init(surface_desc* desc)
{
	desc->width = 0;
	desc->height = 0;
	desc->format = PIXEL_FORMAT_BGRA8888;
	desc->usage = SURFACE_USAGE_CPU_READ | SURFACE_USAGE_CPU_WRITE;
	desc->bytesPerElement = 0;
	desc->bytesPerRow = 0;
}

#endif

#endif /* _SURFACE_TYPES_H */
```

### KosmSurface.h

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_H
#define _KOSM_SURFACE_H

#include <OS.h>
#include <SurfaceTypes.h>

class BMessage;

class KosmSurface {
public:
			surface_id			ID() const;

			uint32				Width() const;
			uint32				Height() const;
			pixel_format		Format() const;
			uint32				BytesPerElement() const;
			uint32				BytesPerRow() const;
			size_t				AllocSize() const;

			uint32				PlaneCount() const;
			uint32				WidthOfPlane(uint32 planeIndex) const;
			uint32				HeightOfPlane(uint32 planeIndex) const;
			uint32				BytesPerElementOfPlane(uint32 planeIndex) const;
			uint32				BytesPerRowOfPlane(uint32 planeIndex) const;
			void*				BaseAddressOfPlane(uint32 planeIndex) const;

			status_t			Lock(uint32 options = 0,
									uint32* outSeed = NULL);
			status_t			Unlock(uint32 options = 0,
									uint32* outSeed = NULL);
			void*				BaseAddress() const;

			uint32				Seed() const;

			void				IncrementUseCount();
			void				DecrementUseCount();
			int32				LocalUseCount() const;
			bool				IsInUse() const;

			status_t			SetAttachment(const char* key,
									const BMessage& value);
			status_t			GetAttachment(const char* key,
									BMessage* outValue) const;
			status_t			RemoveAttachment(const char* key);
			status_t			CopyAllAttachments(BMessage* outValues) const;
			status_t			RemoveAllAttachments();

			area_id				Area() const;

private:
	friend class KosmSurfaceAllocator;

								KosmSurface();
								~KosmSurface();
								KosmSurface(const KosmSurface&);
			KosmSurface&		operator=(const KosmSurface&);

			struct Data;
			Data*				fData;
};

#endif /* _KOSM_SURFACE_H */
```

### KosmSurfaceAllocator.h

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_ALLOCATOR_H
#define _KOSM_SURFACE_ALLOCATOR_H

#include <SurfaceTypes.h>

class KosmSurface;

class KosmSurfaceAllocator {
public:
	static	KosmSurfaceAllocator* Default();

			status_t			Allocate(const surface_desc& desc,
									KosmSurface** outSurface);
			void				Free(KosmSurface* surface);

			status_t			Lookup(surface_id id,
									KosmSurface** outSurface);

			size_t				GetPropertyMaximum(const char* property) const;
			size_t				GetPropertyAlignment(const char* property) const;

			bool				IsFormatSupported(pixel_format format) const;

private:
								KosmSurfaceAllocator();
								~KosmSurfaceAllocator();

			struct Impl;
			Impl*				fImpl;
};

#endif /* _KOSM_SURFACE_ALLOCATOR_H */
```

## Приватные заголовки

### SurfaceBuffer.h

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SURFACE_BUFFER_H
#define _SURFACE_BUFFER_H

#include <Locker.h>
#include <Message.h>
#include <SurfaceTypes.h>

#define SURFACE_MAX_PLANES 4

struct SurfaceBuffer {
								SurfaceBuffer();

			surface_id			id;

			surface_desc		desc;
			size_t				allocSize;

			uint32				planeCount;
			plane_info			planes[SURFACE_MAX_PLANES];

			area_id				areaId;
			void*				baseAddress;

			int32				lockState;
			thread_id			lockOwner;
			uint32				seed;

			int32				localUseCount;

			BMessage			attachments;
			BLocker				lock;

private:
								SurfaceBuffer(const SurfaceBuffer&);
			SurfaceBuffer&		operator=(const SurfaceBuffer&);
};

#endif /* _SURFACE_BUFFER_H */
```

### SurfaceRegistry.h

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * System-wide surface registry for global use count tracking.
 * Similar to iOS IOSurface global/local use count model.
 */
#ifndef _SURFACE_REGISTRY_H
#define _SURFACE_REGISTRY_H

#include <OS.h>
#include <SurfaceTypes.h>

#define SURFACE_REGISTRY_MAX_ENTRIES 4096

// Special marker for deleted entries (linear probing tombstone)
#define SURFACE_ID_TOMBSTONE ((surface_id)-1)

struct SurfaceRegistryEntry {
	surface_id		id;			// 0 = empty, TOMBSTONE = deleted, other = valid
	int32			globalUseCount;
	team_id			ownerTeam;
	area_id			sourceArea;
};

class SurfaceRegistry {
public:
	static	SurfaceRegistry*	Default();

			status_t			Register(surface_id id, area_id sourceArea);
			status_t			Unregister(surface_id id);

			status_t			IncrementGlobalUseCount(surface_id id);
			status_t			DecrementGlobalUseCount(surface_id id);
			int32				GlobalUseCount(surface_id id) const;
			bool				IsInUse(surface_id id) const;

			status_t			LookupArea(surface_id id,
									area_id* outArea) const;

private:
								SurfaceRegistry();
								~SurfaceRegistry();

			status_t			_InitSharedArea();
			int32				_IndexFor(surface_id id) const;

			area_id				fRegistryArea;
			SurfaceRegistryEntry* fEntries;
			sem_id				fLock;
};

#endif /* _SURFACE_REGISTRY_H */
```

### AllocationBackend.h

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ALLOCATION_BACKEND_H
#define _ALLOCATION_BACKEND_H

#include <SurfaceTypes.h>

struct SurfaceBuffer;

class AllocationBackend {
public:
	virtual						~AllocationBackend();

	virtual	status_t			Allocate(const surface_desc& desc,
									SurfaceBuffer** outBuffer) = 0;
	virtual	void				Free(SurfaceBuffer* buffer) = 0;

	virtual	status_t			Map(SurfaceBuffer* buffer) = 0;
	virtual	status_t			Unmap(SurfaceBuffer* buffer) = 0;

	virtual	size_t				GetStrideAlignment(pixel_format format) = 0;
	virtual	size_t				GetMaxWidth() = 0;
	virtual	size_t				GetMaxHeight() = 0;
	virtual	bool				SupportsFormat(pixel_format format) = 0;
	virtual	bool				SupportsUsage(uint32 usage) = 0;
};

#endif /* _ALLOCATION_BACKEND_H */
```

### PlanarLayout.h

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PLANAR_LAYOUT_H
#define _PLANAR_LAYOUT_H

#include <SurfaceTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32		planar_get_plane_count(pixel_format format);
uint32		planar_get_bits_per_pixel(pixel_format format);
bool		planar_is_format_planar(pixel_format format);

void		planar_calculate_plane(pixel_format format, uint32 planeIndex,
				uint32 width, uint32 height, size_t strideAlignment,
				plane_info* outInfo);

size_t		planar_calculate_total_size(pixel_format format,
				uint32 width, uint32 height, size_t strideAlignment);

#ifdef __cplusplus
}
#endif

#endif /* _PLANAR_LAYOUT_H */
```

## Реализация

### SurfaceBuffer.cpp

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SurfaceBuffer.h"

#include <string.h>


SurfaceBuffer::SurfaceBuffer()
	:
	id(0),
	allocSize(0),
	planeCount(1),
	areaId(-1),
	baseAddress(NULL),
	lockState(0),
	lockOwner(-1),
	seed(0),
	localUseCount(0),
	lock("surface_buffer")
{
	surface_desc_init(&desc);
	memset(planes, 0, sizeof(planes));
}
```

### SurfaceRegistry.cpp

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SurfaceRegistry.h"

#include <new>
#include <string.h>


SurfaceRegistry*
SurfaceRegistry::Default()
{
	static SurfaceRegistry sDefault;
	return &sDefault;
}


SurfaceRegistry::SurfaceRegistry()
	:
	fRegistryArea(-1),
	fEntries(NULL),
	fLock(-1)
{
	_InitSharedArea();
}


SurfaceRegistry::~SurfaceRegistry()
{
	if (fLock >= 0)
		delete_sem(fLock);
	if (fRegistryArea >= 0)
		delete_area(fRegistryArea);
}


status_t
SurfaceRegistry::_InitSharedArea()
{
	size_t size = sizeof(SurfaceRegistryEntry) * SURFACE_REGISTRY_MAX_ENTRIES;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	void* address = NULL;
	fRegistryArea = create_area("surface_registry", &address,
		B_ANY_ADDRESS, size, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (fRegistryArea < 0)
		return fRegistryArea;

	fEntries = (SurfaceRegistryEntry*)address;
	memset(fEntries, 0, size);

	fLock = create_sem(1, "surface_registry_lock");
	if (fLock < 0) {
		delete_area(fRegistryArea);
		fRegistryArea = -1;
		return fLock;
	}

	return B_OK;
}


int32
SurfaceRegistry::_IndexFor(surface_id id) const
{
	return (id - 1) % SURFACE_REGISTRY_MAX_ENTRIES;
}


status_t
SurfaceRegistry::Register(surface_id id, area_id sourceArea)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	// Linear probing to find empty slot or existing entry
	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == 0 || entry.id == SURFACE_ID_TOMBSTONE || entry.id == id) {
			entry.id = id;
			entry.globalUseCount = 1;

			thread_info info;
			get_thread_info(find_thread(NULL), &info);
			entry.ownerTeam = info.team;

			entry.sourceArea = sourceArea;

			release_sem(fLock);
			return B_OK;
		}

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NO_MEMORY;
}


status_t
SurfaceRegistry::Unregister(surface_id id)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	// Linear probing to find entry
	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			if (entry.globalUseCount > 0) {
				release_sem(fLock);
				return B_SURFACE_IN_USE;
			}

			entry.id = SURFACE_ID_TOMBSTONE;
			entry.globalUseCount = 0;
			entry.ownerTeam = -1;
			entry.sourceArea = -1;

			release_sem(fLock);
			return B_OK;
		}

		if (entry.id == 0)
			break;  // Empty slot - entry not found (tombstone continues search)

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NAME_NOT_FOUND;
}


status_t
SurfaceRegistry::IncrementGlobalUseCount(surface_id id)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			atomic_add(&entry.globalUseCount, 1);
			release_sem(fLock);
			return B_OK;
		}

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NAME_NOT_FOUND;
}


status_t
SurfaceRegistry::DecrementGlobalUseCount(surface_id id)
{
	if (acquire_sem(fLock) != B_OK)
		return B_ERROR;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			atomic_add(&entry.globalUseCount, -1);
			release_sem(fLock);
			return B_OK;
		}

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	release_sem(fLock);
	return B_NAME_NOT_FOUND;
}


int32
SurfaceRegistry::GlobalUseCount(surface_id id) const
{
	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		const SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id)
			return atomic_get(&entry.globalUseCount);

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return 0;
}


bool
SurfaceRegistry::IsInUse(surface_id id) const
{
	return GlobalUseCount(id) > 0;
}


status_t
SurfaceRegistry::LookupArea(surface_id id, area_id* outArea) const
{
	if (outArea == NULL)
		return B_BAD_VALUE;

	int32 startIndex = _IndexFor(id);
	int32 index = startIndex;

	do {
		const SurfaceRegistryEntry& entry = fEntries[index];

		if (entry.id == id) {
			*outArea = entry.sourceArea;
			return B_OK;
		}

		if (entry.id == 0)
			break;

		index = (index + 1) % SURFACE_REGISTRY_MAX_ENTRIES;
	} while (index != startIndex);

	return B_NAME_NOT_FOUND;
}
```

### KosmSurface.cpp

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.h>

#include <Autolock.h>
#include <Message.h>

#include "SurfaceBuffer.h"
#include "SurfaceRegistry.h"


struct KosmSurface::Data {
	SurfaceBuffer*	buffer;
};


KosmSurface::KosmSurface()
	:
	fData(new(std::nothrow) Data)
{
	if (fData != NULL)
		fData->buffer = NULL;
}


KosmSurface::~KosmSurface()
{
	delete fData;
}


surface_id
KosmSurface::ID() const
{
	return fData->buffer->id;
}


uint32
KosmSurface::Width() const
{
	return fData->buffer->desc.width;
}


uint32
KosmSurface::Height() const
{
	return fData->buffer->desc.height;
}


pixel_format
KosmSurface::Format() const
{
	return fData->buffer->desc.format;
}


uint32
KosmSurface::BytesPerElement() const
{
	return fData->buffer->desc.bytesPerElement;
}


uint32
KosmSurface::BytesPerRow() const
{
	return fData->buffer->desc.bytesPerRow;
}


size_t
KosmSurface::AllocSize() const
{
	return fData->buffer->allocSize;
}


uint32
KosmSurface::PlaneCount() const
{
	return fData->buffer->planeCount;
}


uint32
KosmSurface::WidthOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].width;
}


uint32
KosmSurface::HeightOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].height;
}


uint32
KosmSurface::BytesPerElementOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerElement;
}


uint32
KosmSurface::BytesPerRowOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerRow;
}


void*
KosmSurface::BaseAddressOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return NULL;
	if (fData->buffer->lockState == 0)
		return NULL;

	uint8* base = (uint8*)fData->buffer->baseAddress;
	return base + fData->buffer->planes[planeIndex].offset;
}


status_t
KosmSurface::Lock(uint32 options, uint32* outSeed)
{
	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->lockState != 0) {
		if (fData->buffer->lockOwner == find_thread(NULL))
			return B_SURFACE_ALREADY_LOCKED;
		return B_BUSY;
	}

	fData->buffer->lockState = (options & SURFACE_LOCK_READ_ONLY) ? 1 : 2;
	fData->buffer->lockOwner = find_thread(NULL);

	if (outSeed != NULL)
		*outSeed = fData->buffer->seed;

	return B_OK;
}


status_t
KosmSurface::Unlock(uint32 options, uint32* outSeed)
{
	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->lockState == 0)
		return B_SURFACE_NOT_LOCKED;

	if (fData->buffer->lockOwner != find_thread(NULL))
		return B_NOT_ALLOWED;

	if (fData->buffer->lockState == 2)
		fData->buffer->seed++;

	fData->buffer->lockState = 0;
	fData->buffer->lockOwner = -1;

	if (outSeed != NULL)
		*outSeed = fData->buffer->seed;

	return B_OK;
}


void*
KosmSurface::BaseAddress() const
{
	if (fData->buffer->lockState == 0)
		return NULL;
	return fData->buffer->baseAddress;
}


uint32
KosmSurface::Seed() const
{
	return fData->buffer->seed;
}


void
KosmSurface::IncrementUseCount()
{
	int32 oldCount = atomic_add(&fData->buffer->localUseCount, 1);
	if (oldCount == 0) {
		SurfaceRegistry::Default()->IncrementGlobalUseCount(
			fData->buffer->id);
	}
}


void
KosmSurface::DecrementUseCount()
{
	int32 oldCount = atomic_add(&fData->buffer->localUseCount, -1);
	if (oldCount == 1) {
		SurfaceRegistry::Default()->DecrementGlobalUseCount(
			fData->buffer->id);
	}
}


int32
KosmSurface::LocalUseCount() const
{
	return atomic_get(&fData->buffer->localUseCount);
}


bool
KosmSurface::IsInUse() const
{
	return SurfaceRegistry::Default()->IsInUse(fData->buffer->id);
}


status_t
KosmSurface::SetAttachment(const char* key, const BMessage& value)
{
	if (key == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	fData->buffer->attachments.RemoveName(key);

	return fData->buffer->attachments.AddMessage(key, &value);
}


status_t
KosmSurface::GetAttachment(const char* key, BMessage* outValue) const
{
	if (key == NULL || outValue == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	return fData->buffer->attachments.FindMessage(key, outValue);
}


status_t
KosmSurface::RemoveAttachment(const char* key)
{
	if (key == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	return fData->buffer->attachments.RemoveName(key);
}


status_t
KosmSurface::CopyAllAttachments(BMessage* outValues) const
{
	if (outValues == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	*outValues = fData->buffer->attachments;
	return B_OK;
}


status_t
KosmSurface::RemoveAllAttachments()
{
	BAutolock locker(fData->buffer->lock);

	fData->buffer->attachments.MakeEmpty();
	return B_OK;
}


area_id
KosmSurface::Area() const
{
	return fData->buffer->areaId;
}
```

### AreaBackend.cpp

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AllocationBackend.h"
#include "PlanarLayout.h"
#include "SurfaceBuffer.h"

#include <OS.h>
#include <new>
#include <stdio.h>
#include <string.h>


class AreaBackend : public AllocationBackend {
public:
								AreaBackend();
	virtual						~AreaBackend();

	virtual	status_t			Allocate(const surface_desc& desc,
									SurfaceBuffer** outBuffer);
	virtual	void				Free(SurfaceBuffer* buffer);

	virtual	status_t			Map(SurfaceBuffer* buffer);
	virtual	status_t			Unmap(SurfaceBuffer* buffer);

	virtual	size_t				GetStrideAlignment(pixel_format format);
	virtual	size_t				GetMaxWidth();
	virtual	size_t				GetMaxHeight();
	virtual	bool				SupportsFormat(pixel_format format);
	virtual	bool				SupportsUsage(uint32 usage);

private:
	static	const size_t		kStrideAlignment = 64;
	static	const size_t		kMaxDimension = 16384;
};


AllocationBackend::~AllocationBackend()
{
}


AreaBackend::AreaBackend()
{
}


AreaBackend::~AreaBackend()
{
}


status_t
AreaBackend::Allocate(const surface_desc& desc, SurfaceBuffer** outBuffer)
{
	if (outBuffer == NULL)
		return B_BAD_VALUE;

	if (desc.width > kMaxDimension || desc.height > kMaxDimension)
		return B_BAD_VALUE;

	SurfaceBuffer* buffer = new(std::nothrow) SurfaceBuffer;
	if (buffer == NULL)
		return B_NO_MEMORY;

	buffer->desc = desc;
	buffer->planeCount = planar_get_plane_count(desc.format);

	for (uint32 i = 0; i < buffer->planeCount; i++) {
		planar_calculate_plane(desc.format, i, desc.width, desc.height,
			kStrideAlignment, &buffer->planes[i]);
	}

	buffer->allocSize = planar_calculate_total_size(desc.format,
		desc.width, desc.height, kStrideAlignment);

	if (buffer->desc.bytesPerElement == 0)
		buffer->desc.bytesPerElement = buffer->planes[0].bytesPerElement;
	if (buffer->desc.bytesPerRow == 0)
		buffer->desc.bytesPerRow = buffer->planes[0].bytesPerRow;

	size_t areaSize = (buffer->allocSize + B_PAGE_SIZE - 1)
		& ~(B_PAGE_SIZE - 1);

	char name[B_OS_NAME_LENGTH];
	snprintf(name, sizeof(name), "surface_%ux%u",
		desc.width, desc.height);

	buffer->baseAddress = NULL;
	buffer->areaId = create_area(name, &buffer->baseAddress,
		B_ANY_ADDRESS, areaSize, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (buffer->areaId < 0) {
		status_t error = buffer->areaId;
		delete buffer;
		return error;
	}

	memset(buffer->baseAddress, 0, buffer->allocSize);

	*outBuffer = buffer;
	return B_OK;
}


void
AreaBackend::Free(SurfaceBuffer* buffer)
{
	if (buffer == NULL)
		return;

	if (buffer->areaId >= 0)
		delete_area(buffer->areaId);

	delete buffer;
}


status_t
AreaBackend::Map(SurfaceBuffer* buffer)
{
	return B_OK;
}


status_t
AreaBackend::Unmap(SurfaceBuffer* buffer)
{
	return B_OK;
}


size_t
AreaBackend::GetStrideAlignment(pixel_format format)
{
	return kStrideAlignment;
}


size_t
AreaBackend::GetMaxWidth()
{
	return kMaxDimension;
}


size_t
AreaBackend::GetMaxHeight()
{
	return kMaxDimension;
}


bool
AreaBackend::SupportsFormat(pixel_format format)
{
	switch (format) {
		case PIXEL_FORMAT_RGBA8888:
		case PIXEL_FORMAT_BGRA8888:
		case PIXEL_FORMAT_RGB565:
		case PIXEL_FORMAT_RGBX8888:
		case PIXEL_FORMAT_NV12:
		case PIXEL_FORMAT_NV21:
		case PIXEL_FORMAT_YV12:
			return true;
		default:
			return false;
	}
}


bool
AreaBackend::SupportsUsage(uint32 usage)
{
	uint32 supported = SURFACE_USAGE_CPU_READ | SURFACE_USAGE_CPU_WRITE
		| SURFACE_USAGE_COMPOSITOR;
	return (usage & ~supported) == 0;
}


AllocationBackend*
create_area_backend()
{
	return new(std::nothrow) AreaBackend;
}
```

### KosmSurfaceAllocator.cpp

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurfaceAllocator.h>
#include <KosmSurface.h>

#include <Autolock.h>
#include <Locker.h>

#include <new>
#include <string.h>

#include "AllocationBackend.h"
#include "SurfaceBuffer.h"
#include "SurfaceRegistry.h"


extern AllocationBackend* create_area_backend();


static const int32 kMaxSurfaces = 1024;


struct KosmSurfaceAllocator::Impl {
	AllocationBackend*	backend;
	KosmSurface*		surfaces[kMaxSurfaces];
	surface_id			nextId;
	BLocker				lock;

	Impl()
		:
		backend(create_area_backend()),
		nextId(1),
		lock("surface_allocator")
	{
		memset(surfaces, 0, sizeof(surfaces));
	}

	~Impl()
	{
		delete backend;
	}

	int32 IndexFor(surface_id id) const
	{
		return (id - 1) % kMaxSurfaces;
	}
};


KosmSurfaceAllocator*
KosmSurfaceAllocator::Default()
{
	static KosmSurfaceAllocator sDefault;
	return &sDefault;
}


KosmSurfaceAllocator::KosmSurfaceAllocator()
	:
	fImpl(new Impl)
{
}


KosmSurfaceAllocator::~KosmSurfaceAllocator()
{
	delete fImpl;
}


status_t
KosmSurfaceAllocator::Allocate(const surface_desc& desc,
	KosmSurface** outSurface)
{
	if (outSurface == NULL)
		return B_BAD_VALUE;

	if (desc.width == 0 || desc.height == 0)
		return B_BAD_VALUE;

	if (!fImpl->backend->SupportsFormat(desc.format))
		return B_BAD_VALUE;

	SurfaceBuffer* buffer = NULL;
	status_t status = fImpl->backend->Allocate(desc, &buffer);
	if (status != B_OK)
		return status;

	BAutolock locker(fImpl->lock);

	buffer->id = fImpl->nextId++;

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == NULL || surface->fData == NULL) {
		fImpl->backend->Free(buffer);
		delete surface;
		return B_NO_MEMORY;
	}

	surface->fData->buffer = buffer;

	status = SurfaceRegistry::Default()->Register(buffer->id, buffer->areaId);
	if (status != B_OK) {
		fImpl->backend->Free(buffer);
		delete surface;
		return status;
	}

	int32 index = fImpl->IndexFor(buffer->id);
	fImpl->surfaces[index] = surface;

	*outSurface = surface;
	return B_OK;
}


void
KosmSurfaceAllocator::Free(KosmSurface* surface)
{
	if (surface == NULL)
		return;

	BAutolock locker(fImpl->lock);

	surface_id id = surface->ID();
	int32 index = fImpl->IndexFor(id);

	if (fImpl->surfaces[index] == surface)
		fImpl->surfaces[index] = NULL;

	status_t status = SurfaceRegistry::Default()->Unregister(id);
	if (status == B_SURFACE_IN_USE)
		debugger("Freeing surface that is still in use");

	fImpl->backend->Free(surface->fData->buffer);
	delete surface;
}


status_t
KosmSurfaceAllocator::Lookup(surface_id id, KosmSurface** outSurface)
{
	if (outSurface == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fImpl->lock);

	int32 index = fImpl->IndexFor(id);
	KosmSurface* surface = fImpl->surfaces[index];

	if (surface == NULL || surface->ID() != id)
		return B_NAME_NOT_FOUND;

	*outSurface = surface;
	return B_OK;
}


size_t
KosmSurfaceAllocator::GetPropertyMaximum(const char* property) const
{
	if (strcmp(property, "width") == 0)
		return fImpl->backend->GetMaxWidth();
	if (strcmp(property, "height") == 0)
		return fImpl->backend->GetMaxHeight();
	return 0;
}


size_t
KosmSurfaceAllocator::GetPropertyAlignment(const char* property) const
{
	if (strcmp(property, "bytesPerRow") == 0)
		return fImpl->backend->GetStrideAlignment(PIXEL_FORMAT_BGRA8888);
	return 1;
}


bool
KosmSurfaceAllocator::IsFormatSupported(pixel_format format) const
{
	return fImpl->backend->SupportsFormat(format);
}
```

### PlanarLayout.cpp

```cpp
/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PlanarLayout.h"


typedef struct {
	uint32	planeCount;
	uint32	bitsPerPixel;
} format_info;


static const format_info kFormats[] = {
	{ 1, 32 },	// PIXEL_FORMAT_RGBA8888
	{ 1, 32 },	// PIXEL_FORMAT_BGRA8888
	{ 1, 16 },	// PIXEL_FORMAT_RGB565
	{ 1, 32 },	// PIXEL_FORMAT_RGBX8888
	{ 2, 12 },	// PIXEL_FORMAT_NV12
	{ 2, 12 },	// PIXEL_FORMAT_NV21
	{ 3, 12 }	// PIXEL_FORMAT_YV12
};

static const uint32 kFormatCount = sizeof(kFormats) / sizeof(kFormats[0]);


uint32
planar_get_plane_count(pixel_format format)
{
	if (format >= kFormatCount)
		return 1;
	return kFormats[format].planeCount;
}


uint32
planar_get_bits_per_pixel(pixel_format format)
{
	if (format >= kFormatCount)
		return 32;
	return kFormats[format].bitsPerPixel;
}


bool
planar_is_format_planar(pixel_format format)
{
	return planar_get_plane_count(format) > 1;
}


void
planar_calculate_plane(pixel_format format, uint32 planeIndex,
	uint32 width, uint32 height, size_t strideAlignment,
	plane_info* outInfo)
{
	if (outInfo == NULL)
		return;

	outInfo->width = 0;
	outInfo->height = 0;
	outInfo->bytesPerElement = 0;
	outInfo->bytesPerRow = 0;
	outInfo->offset = 0;

	if (planeIndex == 0) {
		outInfo->width = width;
		outInfo->height = height;

		switch (format) {
			case PIXEL_FORMAT_RGBA8888:
			case PIXEL_FORMAT_BGRA8888:
			case PIXEL_FORMAT_RGBX8888:
				outInfo->bytesPerElement = 4;
				break;
			case PIXEL_FORMAT_RGB565:
				outInfo->bytesPerElement = 2;
				break;
			case PIXEL_FORMAT_NV12:
			case PIXEL_FORMAT_NV21:
			case PIXEL_FORMAT_YV12:
				outInfo->bytesPerElement = 1;
				break;
			default:
				outInfo->bytesPerElement = 4;
				break;
		}

		outInfo->bytesPerRow = (width * outInfo->bytesPerElement
			+ strideAlignment - 1) & ~(strideAlignment - 1);
		outInfo->offset = 0;
	} else if (format == PIXEL_FORMAT_NV12 || format == PIXEL_FORMAT_NV21) {
		outInfo->width = width / 2;
		outInfo->height = height / 2;
		outInfo->bytesPerElement = 2;
		outInfo->bytesPerRow = (outInfo->width * 2 + strideAlignment - 1)
			& ~(strideAlignment - 1);

		plane_info plane0;
		planar_calculate_plane(format, 0, width, height, strideAlignment,
			&plane0);
		outInfo->offset = plane0.bytesPerRow * plane0.height;
	} else if (format == PIXEL_FORMAT_YV12) {
		outInfo->width = width / 2;
		outInfo->height = height / 2;
		outInfo->bytesPerElement = 1;
		outInfo->bytesPerRow = (outInfo->width + strideAlignment - 1)
			& ~(strideAlignment - 1);

		plane_info plane0;
		planar_calculate_plane(format, 0, width, height, strideAlignment,
			&plane0);
		size_t plane0Size = plane0.bytesPerRow * plane0.height;
		size_t uvPlaneSize = outInfo->bytesPerRow * outInfo->height;

		if (planeIndex == 1)
			outInfo->offset = plane0Size;
		else
			outInfo->offset = plane0Size + uvPlaneSize;
	}
}


size_t
planar_calculate_total_size(pixel_format format, uint32 width, uint32 height,
	size_t strideAlignment)
{
	size_t total = 0;
	uint32 planeCount = planar_get_plane_count(format);

	for (uint32 i = 0; i < planeCount; i++) {
		plane_info plane;
		planar_calculate_plane(format, i, width, height, strideAlignment,
			&plane);
		size_t planeEnd = plane.offset + plane.bytesPerRow * plane.height;
		if (planeEnd > total)
			total = planeEnd;
	}

	return total;
}
```

## IPC между процессами

```cpp
// Процесс A (создатель)
KosmSurface* surface;
KosmSurfaceAllocator::Default()->Allocate(desc, &surface);

// Передать surface другому процессу
surface->IncrementUseCount();  // Увеличивает global count

// Получить данные для передачи через BMessage
BMessage msg;
msg.AddInt32("surface_id", surface->ID());
msg.AddInt32("area_id", surface->Area());
msg.AddInt32("width", surface->Width());
msg.AddInt32("height", surface->Height());
msg.AddInt32("format", surface->Format());
msg.AddInt32("bytes_per_row", surface->BytesPerRow());
// Отправить msg через port


// Процесс B (получатель)
surface_id id;
area_id sourceArea;
msg.FindInt32("surface_id", (int32*)&id);
msg.FindInt32("area_id", &sourceArea);

void* address = NULL;
area_id localArea = clone_area("imported_surface", &address,
	B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sourceArea);

if (localArea >= 0) {
	// Увеличить global use count
	SurfaceRegistry::Default()->IncrementGlobalUseCount(id);
	
	// Доступ к данным через address
	// Размеры и формат из msg
	
	// При завершении работы
	SurfaceRegistry::Default()->DecrementGlobalUseCount(id);
	delete_area(localArea);
}
```

## Пример использования

```cpp
#include <KosmSurface.h>
#include <KosmSurfaceAllocator.h>
#include <string.h>

void
Example()
{
	surface_desc desc;
	surface_desc_init(&desc);
	desc.width = 800;
	desc.height = 600;
	desc.format = PIXEL_FORMAT_BGRA8888;
	desc.usage = SURFACE_USAGE_CPU_WRITE | SURFACE_USAGE_COMPOSITOR;

	KosmSurface* surface;
	status_t status = KosmSurfaceAllocator::Default()->Allocate(desc,
		&surface);
	if (status != B_OK)
		return;

	// Lock для записи
	uint32 seed;
	surface->Lock(0, &seed);

	// Получить указатель на данные
	void* data = surface->BaseAddress();
	uint32 stride = surface->BytesPerRow();

	// Заполнить белым
	memset(data, 0xFF, stride * surface->Height());

	// Unlock
	surface->Unlock(0, &seed);

	// Передать compositor'у
	surface->IncrementUseCount();

	// Проверить глобальное использование
	if (surface->IsInUse()) {
		// Surface используется где-то в системе
	}

	// После использования
	surface->DecrementUseCount();

	// Освободить
	KosmSurfaceAllocator::Default()->Free(surface);
}
```

## Что НЕ входит в Surface Kit

| Функционал | Где реализовать |
|------------|-----------------|
| Blit/Copy/Fill | Graphics Kit |
| Dirty tracking | Animation Kit |
| Buffer pool | Display Kit |
| Fence/Sync | OS primitives (sem_id) |
| Compositor protocol | Window Kit / Orbita |
