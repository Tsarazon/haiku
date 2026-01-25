/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_IMAGE_HPP
#define _KOSM_IMAGE_HPP

#include <KosmGeometry.hpp>
#include <SupportDefs.h>

class KosmSurface;

class KosmImage {
public:
							KosmImage();
							KosmImage(const KosmImage& other);
							~KosmImage();

			KosmImage&		operator=(const KosmImage& other);

	// Loading
			status_t		Load(const char* path);
			status_t		Load(const void* data, size_t size,
								const char* mimeType = nullptr);
			status_t		LoadSVG(const char* path);
			status_t		LoadSVG(const void* data, size_t size);

	// From raw pixels
			status_t		SetPixels(const uint32* pixels,
								uint32 width, uint32 height,
								bool premultiplied = true);
			status_t		SetPixels(const KosmSurface* surface);

	// Properties
			bool			IsValid() const;
			uint32			Width() const;
			uint32			Height() const;
			KosmSize		Size() const;

	// Transform
			void			SetSize(float width, float height);
			void			SetSize(const KosmSize& size);
			void			SetTransform(const KosmMatrix& matrix);
			KosmMatrix		Transform() const;

	// Opacity
			void			SetOpacity(float opacity);
			float			Opacity() const;

	// Internal
			void*			NativeHandle() const;

private:
			struct Data;
			Data*			fData;
};

#endif /* _KOSM_IMAGE_HPP */
