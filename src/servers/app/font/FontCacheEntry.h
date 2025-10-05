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

#ifndef FONT_CACHE_ENTRY_H
#define FONT_CACHE_ENTRY_H

#include <AutoDeleter.h>
#include <Locker.h>
#include <blend2d.h>

#include "ServerFont.h"
#include "FontEngine.h"
#include "MultiLocker.h"
#include "Referenceable.h"
#include "Transformable.h"

// ============================================================================
// BLPath Serialization
// ============================================================================

// Структура для сериализации BLPath
struct SerializedPath {
	uint32 commandCount;
	uint32 vertexCount;
	// За структурой следуют данные:
	// uint8 commands[commandCount]
	// BLPoint vertices[vertexCount]
	
	static uint32 CalculateSize(const BLPath& path) {
		return sizeof(SerializedPath) 
			+ path.size() * sizeof(uint8)  // commands
			+ path.size() * sizeof(BLPoint);  // vertices
	}
	
	static void Serialize(const BLPath& path, uint8* buffer) {
		SerializedPath* header = (SerializedPath*)buffer;
		header->commandCount = path.size();
		header->vertexCount = path.size();
		
		uint8* commands = buffer + sizeof(SerializedPath);
		BLPoint* vertices = (BLPoint*)(commands + header->commandCount);
		
		// Use BLPath direct access methods
		size_t cmdCount = path.size();
		const uint8* cmds = path.commandData();
		const BLPoint* vtx = path.vertexData();
		
		for (size_t i = 0; i < cmdCount; i++) {
			commands[i] = cmds[i];
			vertices[i] = vtx[i];
		}
	}
	
	static BLPath Deserialize(const uint8* buffer) {
		BLPath path;
		const SerializedPath* header = (const SerializedPath*)buffer;
		
		const uint8* commands = buffer + sizeof(SerializedPath);
		const BLPoint* vertices = (const BLPoint*)(commands + header->commandCount);
		
		for (uint32 i = 0; i < header->commandCount; i++) {
			switch (commands[i]) {
				case BL_PATH_CMD_MOVE:
					path.moveTo(vertices[i].x, vertices[i].y);
					break;
				case BL_PATH_CMD_ON:
					path.lineTo(vertices[i].x, vertices[i].y);
					break;
				case BL_PATH_CMD_QUAD:
					// Quadratic bezier требует 2 точки
					if (i + 1 < header->commandCount) {
						path.quadTo(vertices[i].x, vertices[i].y,
								   vertices[i+1].x, vertices[i+1].y);
						i++; // skip next point
					}
					break;
				case BL_PATH_CMD_CUBIC:
					// Cubic bezier требует 3 точки
					if (i + 2 < header->commandCount) {
						path.cubicTo(vertices[i].x, vertices[i].y,
								    vertices[i+1].x, vertices[i+1].y,
								    vertices[i+2].x, vertices[i+2].y);
						i += 2; // skip next 2 points
					}
					break;
				case BL_PATH_CMD_CLOSE:
					path.close();
					break;
			}
		}
		
		return path;
	}
};

// ============================================================================
// GlyphCache - кэш отдельного глифа
// ============================================================================

struct GlyphCache {
	GlyphCache(uint32 glyphIndex, uint32 dataSize, glyph_data_type dataType,
			const BLBox& blBounds, float advanceX, float advanceY,
			float preciseAdvanceX, float preciseAdvanceY,
			float insetLeft, float insetRight)
		:
		glyph_index(glyphIndex),
		data((uint8*)malloc(dataSize)),
		data_size(dataSize),
		data_type(dataType),
		bounds(blBounds),
		advance_x(advanceX),
		advance_y(advanceY),
		precise_advance_x(preciseAdvanceX),
		precise_advance_y(preciseAdvanceY),
		inset_left(insetLeft),
		inset_right(insetRight),
		hash_link(NULL)
	{
	}

	~GlyphCache()
	{
		free(data);
	}

	uint32			glyph_index;
	uint8*			data;
	uint32			data_size;
	glyph_data_type	data_type;
	BLBox			bounds;
	float			advance_x;
	float			advance_y;
	float			precise_advance_x;
	float			precise_advance_y;
	float			inset_left;
	float			inset_right;

	GlyphCache*		hash_link;
	
	// Восстановление BLPath из сериализованных данных
	BLPath GetPath() const {
		if (data_type == glyph_data_outline && data != NULL) {
			return SerializedPath::Deserialize(data);
		}
		return BLPath();
	}
	
	// Восстановление BLImage из данных
	BLImage GetImage() const {
		BLImage image;
		
		if (data == NULL)
			return image;
			
		switch (data_type) {
			case glyph_data_mono:
			case glyph_data_gray8:
			case glyph_data_lcd: {
				uint32 width = (uint32)(bounds.x1 - bounds.x0);
				uint32 height = (uint32)(bounds.y1 - bounds.y0);
				
				if (width == 0 || height == 0)
					return image;
				
				if (image.create(width, height, BL_FORMAT_A8) != BL_SUCCESS)
					return image;
				
				BLImageData imageData;
				if (image.makeMutable(&imageData) == BL_SUCCESS) {
					// Копируем данные
					uint32 srcStride = width; // A8 формат
					uint32 dstStride = imageData.stride;
					
					uint8* src = data;
					uint8* dst = (uint8*)imageData.pixelData;
					
					for (uint32 y = 0; y < height; y++) {
						memcpy(dst, src, width);
						src += srcStride;
						dst += dstStride;
					}
				}
				break;
			}
			default:
				break;
		}
		
		return image;
	}
};

// ============================================================================
// FontCacheEntry - кэш шрифта
// ============================================================================

class FontCache;

class FontCacheEntry : public MultiLocker, public BReferenceable {
 public:
								FontCacheEntry();
	virtual						~FontCacheEntry();

			bool				Init(const ServerFont& font, bool forceVector);

			bool				HasGlyphs(const char* utf8String,
									ssize_t glyphCount) const;

			const GlyphCache*	CachedGlyph(uint32 glyphCode);
			const GlyphCache*	CreateGlyph(uint32 glyphCode,
									FontCacheEntry* fallbackEntry = NULL);
			bool				CanCreateGlyph(uint32 glyphCode);

			bool				GetKerning(uint32 glyphCode1,
									uint32 glyphCode2, double* x, double* y);

	static	void				GenerateSignature(char* signature,
									size_t signatureSize,
									const ServerFont& font, bool forceVector);

	// private to FontCache class:
			void				UpdateUsage();
			bigtime_t			LastUsed() const
									{ return fLastUsedTime; }
			uint64				UsedCount() const
									{ return fUseCounter; }

 private:
								FontCacheEntry(const FontCacheEntry&);
			const FontCacheEntry& operator=(const FontCacheEntry&);

	static	glyph_rendering		_RenderTypeFor(const ServerFont& font,
									bool forceVector);

			class GlyphCachePool;

			ObjectDeleter<GlyphCachePool>
								fGlyphCache;
			FontEngine			fEngine;

	static	BLocker				sUsageUpdateLock;
			bigtime_t			fLastUsedTime;
			uint64				fUseCounter;
};

#endif // FONT_CACHE_ENTRY_H
