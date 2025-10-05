/*
 * Copyright 2007-2009, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Maxim Shemanarev <mcseemagg@yahoo.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 *		2025 Blend2D Migration
 */

#include "FontCacheEntry.h"

#include <string.h>
#include <stdio.h>

#include <new>

#include <Autolock.h>
#include <utf8_functions.h>
#include <util/OpenHashTable.h>

#include "GlobalSubpixelSettings.h"

BLocker FontCacheEntry::sUsageUpdateLock("FontCacheEntry usage lock");

// ============================================================================
// GlyphCachePool - хеш-таблица для кэширования глифов
// ============================================================================

class FontCacheEntry::GlyphCachePool {
	struct GlyphHashTableDefinition {
		typedef uint32		KeyType;
		typedef	GlyphCache	ValueType;

		size_t HashKey(uint32 key) const
		{
			return key;
		}

		size_t Hash(GlyphCache* value) const
		{
			return value->glyph_index;
		}

		bool Compare(uint32 key, GlyphCache* value) const
		{
			return value->glyph_index == key;
		}

		GlyphCache*& GetLink(GlyphCache* value) const
		{
			return value->hash_link;
		}
	};

public:
	GlyphCachePool()
	{
	}

	~GlyphCachePool()
	{
		GlyphCache* glyph = fGlyphTable.Clear(true);
		while (glyph != NULL) {
			GlyphCache* next = glyph->hash_link;
			delete glyph;
			glyph = next;
		}
	}

	status_t Init()
	{
		return fGlyphTable.Init();
	}

	const GlyphCache* FindGlyph(uint32 glyphIndex) const
	{
		return fGlyphTable.Lookup(glyphIndex);
	}

	GlyphCache* CacheGlyph(uint32 glyphIndex,
		uint32 dataSize, glyph_data_type dataType, const BLBox& bounds,
		float advanceX, float advanceY, float preciseAdvanceX,
		float preciseAdvanceY, float insetLeft, float insetRight)
	{
		GlyphCache* glyph = fGlyphTable.Lookup(glyphIndex);
		if (glyph != NULL)
			return NULL;

		glyph = new(std::nothrow) GlyphCache(glyphIndex, dataSize, dataType,
			bounds, advanceX, advanceY, preciseAdvanceX, preciseAdvanceY,
			insetLeft, insetRight);
		if (glyph == NULL || glyph->data == NULL) {
			delete glyph;
			return NULL;
		}

		fGlyphTable.Insert(glyph);

		return glyph;
	}

private:
	typedef BOpenHashTable<GlyphHashTableDefinition> GlyphTable;
	GlyphTable	fGlyphTable;
};

// ============================================================================
// FontCacheEntry Implementation
// ============================================================================

FontCacheEntry::FontCacheEntry()
	:
	MultiLocker("FontCacheEntry lock"),
	fGlyphCache(new(std::nothrow) GlyphCachePool()),
	fEngine(),
	fLastUsedTime(LONGLONG_MIN),
	fUseCounter(0)
{
}

FontCacheEntry::~FontCacheEntry()
{
}

bool
FontCacheEntry::Init(const ServerFont& font, bool forceVector)
{
	if (!fGlyphCache.IsSet())
		return false;

	if (fGlyphCache->Init() != B_OK)
		return false;

	glyph_rendering renderingType = _RenderTypeFor(font, forceVector);

	if (!fEngine.Init(font.Path(), font.Face(), font.Size(),
			FT_ENCODING_NONE, renderingType, font.Hinting())) {
		return false;
	}

	return true;
}

bool
FontCacheEntry::HasGlyphs(const char* utf8String, ssize_t glyphCount) const
{
	if (!fGlyphCache.IsSet())
		return false;

	uint32 charCode;
	const char* start = utf8String;

	for (ssize_t i = 0; i < glyphCount; i++) {
		charCode = UTF8ToCharCode(&utf8String);
		if (charCode == 0)
			break;

		uint32 glyphIndex = fEngine.GlyphIndexForGlyphCode(charCode);
		if (glyphIndex == 0 && charCode != 0)
			continue;

		if (fGlyphCache->FindGlyph(glyphIndex) == NULL)
			return false;
	}

	return true;
}

const GlyphCache*
FontCacheEntry::CachedGlyph(uint32 glyphCode)
{
	if (!fGlyphCache.IsSet())
		return NULL;

	uint32 glyphIndex = fEngine.GlyphIndexForGlyphCode(glyphCode);
	return fGlyphCache->FindGlyph(glyphIndex);
}

const GlyphCache*
FontCacheEntry::CreateGlyph(uint32 glyphCode, FontCacheEntry* fallbackEntry)
{
	if (!fGlyphCache.IsSet())
		return NULL;

	uint32 glyphIndex = fEngine.GlyphIndexForGlyphCode(glyphCode);

	// Проверяем, есть ли уже в кэше
	const GlyphCache* glyph = fGlyphCache->FindGlyph(glyphIndex);
	if (glyph != NULL)
		return glyph;

	// Пробуем использовать fallback
	if (glyphIndex == 0 && fallbackEntry != NULL) {
		glyph = fallbackEntry->CreateGlyph(glyphCode, NULL);
		if (glyph != NULL)
			return glyph;
	}

	// Подготавливаем глиф через FontEngine
	if (!fEngine.PrepareGlyph(glyphIndex))
		return NULL;

	// Получаем данные глифа
	uint32 dataSize = fEngine.DataSize();
	glyph_data_type dataType = fEngine.DataType();
	BLBox bounds = fEngine.Bounds();

	// Создаем запись в кэше
	GlyphCache* cache = fGlyphCache->CacheGlyph(glyphIndex,
		dataSize, dataType, bounds,
		fEngine.AdvanceX(), fEngine.AdvanceY(),
		fEngine.PreciseAdvanceX(), fEngine.PreciseAdvanceY(),
		fEngine.InsetLeft(), fEngine.InsetRight());

	if (cache == NULL)
		return NULL;

	// Записываем данные глифа
	if (dataType == glyph_data_outline) {
		// Сериализация BLPath
		const BLPath& path = fEngine.Path();
		SerializedPath::Serialize(path, cache->data);
	} else {
		// Растровые данные (mono, gray8, lcd)
		fEngine.WriteGlyphTo(cache->data);
	}

	return cache;
}

bool
FontCacheEntry::CanCreateGlyph(uint32 glyphCode)
{
	uint32 glyphIndex = fEngine.GlyphIndexForGlyphCode(glyphCode);
	return glyphIndex != 0;
}

bool
FontCacheEntry::GetKerning(uint32 glyphCode1, uint32 glyphCode2,
	double* x, double* y)
{
	return fEngine.GetKerning(glyphCode1, glyphCode2, x, y);
}

// static
void
FontCacheEntry::GenerateSignature(char* signature, size_t signatureSize,
	const ServerFont& font, bool forceVector)
{
	glyph_rendering renderingType = _RenderTypeFor(font, forceVector);

	snprintf(signature, signatureSize, "%s-%d-%.1f-%s-%s",
		font.GetFamilyAndStyle(),
		font.Face(),
		font.Size(),
		font.Hinting() ? "hinted" : "unhinted",
		renderingType == glyph_ren_outline ? "vector" : 
		renderingType == glyph_ren_lcd ? "lcd" :
		renderingType == glyph_ren_native_gray8 ? "gray8" : "mono");
}

void
FontCacheEntry::UpdateUsage()
{
	BAutolock _(sUsageUpdateLock);
	fLastUsedTime = system_time();
	fUseCounter++;
}

// static
glyph_rendering
FontCacheEntry::_RenderTypeFor(const ServerFont& font, bool forceVector)
{
	glyph_rendering renderingType;

	if (forceVector || font.Rotation() != 0.0 || font.Shear() != 90.0
		|| font.FalseBoldWidth() != 0.0) {
		renderingType = glyph_ren_outline;
	} else {
		// Используем LCD рендеринг если включен subpixel
		if (gSubpixelAntialiasing)
			renderingType = glyph_ren_lcd;
		else if (font.Flags() & B_DISABLE_ANTIALIASING)
			renderingType = glyph_ren_native_mono;
		else
			renderingType = glyph_ren_native_gray8;
	}

	return renderingType;
}
