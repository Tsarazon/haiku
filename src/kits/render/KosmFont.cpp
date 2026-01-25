/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmFont.hpp>

#include "RenderBackend.hpp"

#include <cstring>
#include <new>
#include <string>


struct KosmFont::Data {
	RenderBackend*	backend;
	void*			handle;
	std::string		family;
	float			size;
	bool			bold;
	bool			italic;
	float			italicShear;
	float			letterSpacing;
	float			lineSpacing;

	Data()
		:
		backend(RenderBackend::Create()),
		handle(nullptr),
		family("sans-serif"),
		size(12.0f),
		bold(false),
		italic(false),
		italicShear(0.2f),
		letterSpacing(0),
		lineSpacing(1.2f)
	{
		if (backend != nullptr)
			handle = backend->CreateFont(family.c_str(), size);
	}

	Data(const char* familyName, float fontSize)
		:
		backend(RenderBackend::Create()),
		handle(nullptr),
		family(familyName != nullptr ? familyName : "sans-serif"),
		size(fontSize),
		bold(false),
		italic(false),
		italicShear(0.2f),
		letterSpacing(0),
		lineSpacing(1.2f)
	{
		if (backend != nullptr)
			handle = backend->CreateFont(family.c_str(), size);
	}

	~Data()
	{
		if (backend != nullptr && handle != nullptr)
			backend->DestroyFont(handle);
		delete backend;
	}
};


KosmFont::KosmFont()
	:
	fData(new(std::nothrow) Data)
{
}


KosmFont::KosmFont(const char* family, float size)
	:
	fData(new(std::nothrow) Data(family, size))
{
}


KosmFont::KosmFont(const KosmFont& other)
	:
	fData(nullptr)
{
	*this = other;
}


KosmFont::~KosmFont()
{
	delete fData;
}


KosmFont&
KosmFont::operator=(const KosmFont& other)
{
	if (this == &other)
		return *this;

	delete fData;
	fData = nullptr;

	if (other.fData == nullptr)
		return *this;

	fData = new(std::nothrow) Data(other.fData->family.c_str(), other.fData->size);
	if (fData == nullptr)
		return *this;

	fData->bold = other.fData->bold;
	fData->italic = other.fData->italic;
	fData->italicShear = other.fData->italicShear;
	fData->letterSpacing = other.fData->letterSpacing;
	fData->lineSpacing = other.fData->lineSpacing;

	// Apply settings to handle
	if (fData->backend != nullptr && fData->handle != nullptr) {
		if (fData->italic)
			fData->backend->FontSetItalic(fData->handle, fData->italicShear);
		if (fData->letterSpacing != 0)
			fData->backend->FontSetLetterSpacing(fData->handle, fData->letterSpacing);
		if (fData->lineSpacing != 1.2f)
			fData->backend->FontSetLineSpacing(fData->handle, fData->lineSpacing);
	}

	return *this;
}


// Static font loading functions
status_t
KosmFont::LoadFont(const char* path)
{
	return RenderBackend::Create()->LoadFont(path);
}


status_t
KosmFont::LoadFont(const char* name, const void* data, size_t size)
{
	return RenderBackend::Create()->LoadFontData(name, data, size);
}


status_t
KosmFont::UnloadFont(const char* path)
{
	return RenderBackend::Create()->UnloadFont(path);
}


void
KosmFont::SetFamily(const char* family)
{
	if (fData == nullptr || fData->backend == nullptr)
		return;

	fData->family = family != nullptr ? family : "sans-serif";

	// Recreate font handle with new family
	if (fData->handle != nullptr)
		fData->backend->DestroyFont(fData->handle);

	fData->handle = fData->backend->CreateFont(fData->family.c_str(), fData->size);

	// Reapply settings
	if (fData->handle != nullptr) {
		if (fData->italic)
			fData->backend->FontSetItalic(fData->handle, fData->italicShear);
		if (fData->letterSpacing != 0)
			fData->backend->FontSetLetterSpacing(fData->handle, fData->letterSpacing);
		if (fData->lineSpacing != 1.2f)
			fData->backend->FontSetLineSpacing(fData->handle, fData->lineSpacing);
	}
}


const char*
KosmFont::Family() const
{
	return fData != nullptr ? fData->family.c_str() : "sans-serif";
}


void
KosmFont::SetSize(float size)
{
	if (fData == nullptr)
		return;

	fData->size = size;

	if (fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->FontSetSize(fData->handle, size);
}


float
KosmFont::Size() const
{
	return fData != nullptr ? fData->size : 12.0f;
}


void
KosmFont::SetBold(bool bold)
{
	if (fData != nullptr)
		fData->bold = bold;
}


bool
KosmFont::IsBold() const
{
	return fData != nullptr ? fData->bold : false;
}


void
KosmFont::SetItalic(bool italic)
{
	if (fData == nullptr)
		return;

	fData->italic = italic;

	if (fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->FontSetItalic(fData->handle, italic ? fData->italicShear : 0);
}


bool
KosmFont::IsItalic() const
{
	return fData != nullptr ? fData->italic : false;
}


void
KosmFont::SetItalicShear(float shear)
{
	if (fData == nullptr)
		return;

	fData->italicShear = shear;

	if (fData->italic && fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->FontSetItalic(fData->handle, shear);
}


float
KosmFont::ItalicShear() const
{
	return fData != nullptr ? fData->italicShear : 0.2f;
}


void
KosmFont::SetLetterSpacing(float spacing)
{
	if (fData == nullptr)
		return;

	fData->letterSpacing = spacing;

	if (fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->FontSetLetterSpacing(fData->handle, spacing);
}


float
KosmFont::LetterSpacing() const
{
	return fData != nullptr ? fData->letterSpacing : 0;
}


void
KosmFont::SetLineSpacing(float spacing)
{
	if (fData == nullptr)
		return;

	fData->lineSpacing = spacing;

	if (fData->backend != nullptr && fData->handle != nullptr)
		fData->backend->FontSetLineSpacing(fData->handle, spacing);
}


float
KosmFont::LineSpacing() const
{
	return fData != nullptr ? fData->lineSpacing : 1.2f;
}


float
KosmFont::Ascent() const
{
	// Approximate ascent as 80% of font size
	return fData != nullptr ? fData->size * 0.8f : 9.6f;
}


float
KosmFont::Descent() const
{
	// Approximate descent as 20% of font size
	return fData != nullptr ? fData->size * 0.2f : 2.4f;
}


float
KosmFont::Leading() const
{
	// Leading (line gap) based on line spacing
	if (fData == nullptr)
		return 2.4f;
	return fData->size * (fData->lineSpacing - 1.0f);
}


float
KosmFont::LineHeight() const
{
	return Ascent() + Descent() + Leading();
}


float
KosmFont::MeasureWidth(const char* text) const
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return 0;

	if (text == nullptr || text[0] == '\0')
		return 0;

	return fData->backend->FontMeasureWidth(fData->handle, text);
}


KosmRect
KosmFont::MeasureBounds(const char* text) const
{
	if (fData == nullptr || fData->backend == nullptr || fData->handle == nullptr)
		return KosmRect();

	if (text == nullptr || text[0] == '\0')
		return KosmRect();

	return fData->backend->FontMeasureBounds(fData->handle, text);
}


void*
KosmFont::NativeHandle() const
{
	return fData != nullptr ? fData->handle : nullptr;
}
