/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_HPP
#define _KOSM_SURFACE_HPP

#include <OS.h>
#include <SurfaceTypes.hpp>

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

			// Purgeable support
			status_t			SetPurgeable(surface_purgeable_state newState,
									surface_purgeable_state* outOldState = NULL);
			bool				IsVolatile() const;

			// State accessors
			uint32				Usage() const;
			bool				IsLocked() const;
			bool				IsValid() const;
			thread_id			LockOwner() const;

private:
	friend class KosmSurfaceAllocator;

								KosmSurface();
								~KosmSurface();
								KosmSurface(const KosmSurface&);
			KosmSurface&		operator=(const KosmSurface&);

			struct Data;
			Data*				fData;
};

#endif /* _KOSM_SURFACE_HPP */
