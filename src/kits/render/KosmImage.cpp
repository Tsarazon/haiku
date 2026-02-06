/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmImage.hpp>
#include <KosmSurface.hpp>

#include "RenderBackend.hpp"

#include <cstring>
#include <new>


struct KosmImage::Data {
	RenderBackend*	backend;
	void*			handle;
	KosmMatrix		transform;
	float			opacity;

	Data()
		:
		backend(RenderBackend::Instance()),
		handle(nullptr),
		transform(),
		opacity(1.0f)
	{
		if (backend != nullptr)
			handle = backend->CreateImage();
	}

	~Data()
	{
		if (backend != nullptr && handle != nullptr)
			backend->DestroyImage(handle);
		// Don't delete backend - it's a singleton
	}
};


KosmImage::KosmImage()
	:
	fData(new(std::nothrow) Data)
{
}


KosmImage::KosmImage(const KosmImage& other)
	:
	fData(nullptr)
{
	*this = other;
}


KosmImage::~KosmImage()
{
	delete fData;
}


KosmImage&
KosmImage::operator=(const KosmImage& other)
{
	if (this == &other)
		return *this;

	delete fData;
	fData = nullptr;

	if (other.fData == nullptr || !other.IsValid())
		return *this;

	fData = new(std::nothrow) Data;
	if (fData == nullptr)
		return *this;

	// Copy properties
	fData->transform = other.fData->transform;
	fData->opacity = other.fData->opacity;

	// For a proper copy, we would need to copy the image pixels.
	// For now, reload from same data would be needed.
	// This is a shallow copy scenario - the handle is not duplicated.

	return *this;
}


status_t
KosmImage::Load(const char* path)
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return B_NO_INIT;

	if (path == nullptr)
		return B_BAD_VALUE;

	return fData->backend->ImageLoad(fData->handle, path);
}


status_t
KosmImage::Load(const void* data, size_t size, const char* mimeType)
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return B_NO_INIT;

	if (data == nullptr || size == 0)
		return B_BAD_VALUE;

	return fData->backend->ImageLoadData(fData->handle, data, size, mimeType);
}


status_t
KosmImage::LoadSVG(const char* path)
{
	return Load(path);
}


status_t
KosmImage::LoadSVG(const void* data, size_t size)
{
	return Load(data, size, "image/svg+xml");
}


status_t
KosmImage::SetPixels(const uint32* pixels, uint32 width, uint32 height,
	bool premultiplied)
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return B_NO_INIT;

	if (pixels == nullptr || width == 0 || height == 0)
		return B_BAD_VALUE;

	return fData->backend->ImageSetPixels(fData->handle, pixels,
		width, height, premultiplied);
}


status_t
KosmImage::SetPixels(const KosmSurface* surface)
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return B_NO_INIT;

	if (surface == nullptr)
		return B_BAD_VALUE;

	const uint32* pixels = reinterpret_cast<const uint32*>(surface->BaseAddress());
	if (pixels == nullptr)
		return B_BAD_VALUE;

	return fData->backend->ImageSetPixels(fData->handle, pixels,
		surface->Width(), surface->Height(), true);
}


bool
KosmImage::IsValid() const
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return false;

	return fData->backend->ImageWidth(fData->handle) > 0 &&
		fData->backend->ImageHeight(fData->handle) > 0;
}


uint32
KosmImage::Width() const
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return 0;

	return fData->backend->ImageWidth(fData->handle);
}


uint32
KosmImage::Height() const
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return 0;

	return fData->backend->ImageHeight(fData->handle);
}


KosmSize
KosmImage::Size() const
{
	return KosmSize(static_cast<float>(Width()), static_cast<float>(Height()));
}


void
KosmImage::SetSize(float width, float height)
{
	if (fData != nullptr && fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->ImageSetSize(fData->handle, width, height);
}


void
KosmImage::SetSize(const KosmSize& size)
{
	SetSize(size.width, size.height);
}


void
KosmImage::SetTransform(const KosmMatrix& matrix)
{
	if (fData == nullptr)
		return;

	fData->transform = matrix;

	if (fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->ImageSetTransform(fData->handle, matrix);
}


KosmMatrix
KosmImage::Transform() const
{
	if (fData != nullptr)
		return fData->transform;
	return KosmMatrix::Identity();
}


void
KosmImage::SetOpacity(float opacity)
{
	if (fData == nullptr)
		return;

	fData->opacity = opacity;

	if (fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->ImageSetOpacity(fData->handle, opacity);
}


float
KosmImage::Opacity() const
{
	return fData != nullptr ? fData->opacity : 1.0f;
}


void*
KosmImage::NativeHandle() const
{
	return fData != nullptr ? fData->handle : nullptr;
}
