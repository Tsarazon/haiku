/*
 * Copyright 2009, Christian Packmann.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2005-2014, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "BitmapPainter.h"

#include <Bitmap.h>

#include "drawing_support.h"
#include "ServerBitmap.h"
#include "SystemPalette.h"


// #define TRACE_BITMAP_PAINTER
#ifdef TRACE_BITMAP_PAINTER
#	define TRACE(x...)		printf(x)
#else
#	define TRACE(x...)
#endif


Painter::BitmapPainter::BitmapPainter(const Painter* painter,
	const ServerBitmap* bitmap, uint32 options)
	:
	fPainter(painter),
	fStatus(B_NO_INIT),
	fOptions(options)
{
	if (bitmap == NULL || !bitmap->IsValid())
		return;

	fBitmapBounds = bitmap->Bounds();
	fBitmapBounds.OffsetBy(-fBitmapBounds.left, -fBitmapBounds.top);
		// Compensate for the lefttop offset the bitmap bounds might have
		// It has the right size, but put it at B_ORIGIN

	fColorSpace = bitmap->ColorSpace();

	// Create BLImage from existing ServerBitmap data
	// BL_DATA_ACCESS_READ means read-only, Blend2D will create copy if needed
	BLResult result = fBLImage.createFromData(
		bitmap->Width(),
		bitmap->Height(),
		_ConvertToBLFormat(fColorSpace),
		(void*)bitmap->Bits(),
		bitmap->BytesPerRow(),
		BL_DATA_ACCESS_READ,  // read-only access
		nullptr,              // destroyFunc - not needed, data owned by ServerBitmap
		nullptr               // userData
	);

	if (result == BL_SUCCESS)
		fStatus = B_OK;
}


void
Painter::BitmapPainter::Draw(const BRect& sourceRect,
	const BRect& destinationRect)
{
	if (fStatus != B_OK)
		return;

	TRACE("BitmapPainter::Draw()\n");
	TRACE("   bitmapBounds = (%.1f, %.1f) - (%.1f, %.1f)\n",
		fBitmapBounds.left, fBitmapBounds.top,
		fBitmapBounds.right, fBitmapBounds.bottom);
	TRACE("   sourceRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
		sourceRect.left, sourceRect.top,
		sourceRect.right, sourceRect.bottom);
	TRACE("   destinationRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
		destinationRect.left, destinationRect.top,
		destinationRect.right, destinationRect.bottom);

	bool success = _DetermineTransform(sourceRect, destinationRect);
	if (!success)
		return;

	// Convert color space if required
	BLImage workingImage;
	if (fColorSpace != B_RGBA32 && fColorSpace != B_RGB32) {
		_ConvertColorSpace(workingImage);
	} else {
		workingImage = fBLImage;  // shallow copy (increases reference count)
	}

	// Get Blend2D context from Painter
	BLContext& ctx = fPainter->fInternal.fBLContext;

	// CRITICAL: Setup image filtering quality hints
	if ((fOptions & B_FILTER_BITMAP_BILINEAR) != 0) {
		// Bilinear filtering for quality scaling
		ctx.setHint(BL_CONTEXT_HINT_RENDERING_QUALITY, 
					BL_RENDERING_QUALITY_ANTIALIAS);
		ctx.setHint(BL_CONTEXT_HINT_PATTERN_QUALITY,
					BL_PATTERN_QUALITY_BILINEAR);
	} else {
		// Nearest neighbor for pixel-perfect graphics (no smoothing)
		ctx.setHint(BL_CONTEXT_HINT_PATTERN_QUALITY,
					BL_PATTERN_QUALITY_NEAREST);
	}

	// Setup composition operator (SRC_COPY, SRC_OVER, etc.)
	_SetupCompOp(ctx);

	if ((fOptions & B_TILE_BITMAP) != 0) {
		// ===== TILING MODE =====
		// Use BLPattern for seamless image repetition
		BLPattern pattern(workingImage);
		
		// Set repeat mode for seamless tiling
		pattern.setExtendMode(BL_EXTEND_MODE_REPEAT);
		
		// Apply transformation to pattern (scale and offset)
		BLMatrix2D matrix;
		matrix.translate(fOffset.x, fOffset.y);
		if (fScaleX != 1.0 || fScaleY != 1.0) {
			matrix.scale(fScaleX, fScaleY);
		}
		pattern.setMatrix(matrix);
		
		// Fill rectangle with pattern
		ctx.fillRect(BLRect(
			fDestinationRect.left,
			fDestinationRect.top,
			fDestinationRect.Width() + 1,
			fDestinationRect.Height() + 1
		), pattern);
		
	} else {
		// ===== NORMAL MODE (NON-TILING) =====
		// Use blitImage for drawing (with automatic scaling)
		
		// Calculate source area from sourceRect
		BRect actualSourceRect = sourceRect;
		// Constrain to bitmap bounds
		if (actualSourceRect.left < fBitmapBounds.left)
			actualSourceRect.left = fBitmapBounds.left;
		if (actualSourceRect.top < fBitmapBounds.top)
			actualSourceRect.top = fBitmapBounds.top;
		if (actualSourceRect.right > fBitmapBounds.right)
			actualSourceRect.right = fBitmapBounds.right;
		if (actualSourceRect.bottom > fBitmapBounds.bottom)
			actualSourceRect.bottom = fBitmapBounds.bottom;
		
		BLRectI srcArea(
			(int)actualSourceRect.left,
			(int)actualSourceRect.top,
			(int)(actualSourceRect.Width() + 1),
			(int)(actualSourceRect.Height() + 1)
		);
		
		BLRect dstRect(
			fDestinationRect.left,
			fDestinationRect.top,
			fDestinationRect.Width() + 1,
			fDestinationRect.Height() + 1
		);
		
		// blitImage automatically performs scaling if srcArea != dstRect
		// Filter quality is determined by hints set above
		BLResult result = ctx.blitImage(dstRect, workingImage, srcArea);
		
		if (result != BL_SUCCESS) {
			fprintf(stderr, "BitmapPainter::Draw() - blitImage failed: %d\n", 
					(int)result);
		}
	}
}


bool
Painter::BitmapPainter::_DetermineTransform(BRect sourceRect,
	const BRect& destinationRect)
{
	if (!fPainter->fValidClipping
		|| !sourceRect.IsValid()
		|| ((fOptions & B_TILE_BITMAP) == 0
			&& !sourceRect.Intersects(fBitmapBounds))
		|| !destinationRect.IsValid()) {
		return false;
	}

	fDestinationRect = destinationRect;

	if (!fPainter->fSubpixelPrecise) {
		align_rect_to_pixels(&sourceRect);
		align_rect_to_pixels(&fDestinationRect);
	}

	if ((fOptions & B_TILE_BITMAP) == 0) {
		fScaleX = (fDestinationRect.Width() + 1) / (sourceRect.Width() + 1);
		fScaleY = (fDestinationRect.Height() + 1) / (sourceRect.Height() + 1);

		if (fScaleX == 0.0 || fScaleY == 0.0)
			return false;

		// constrain source rect to bitmap bounds and transfer the changes to
		// the destination rect with the right scale
		if (sourceRect.left < fBitmapBounds.left) {
			float diff = fBitmapBounds.left - sourceRect.left;
			fDestinationRect.left += diff * fScaleX;
			sourceRect.left = fBitmapBounds.left;
		}
		if (sourceRect.top < fBitmapBounds.top) {
			float diff = fBitmapBounds.top - sourceRect.top;
			fDestinationRect.top += diff * fScaleY;
			sourceRect.top = fBitmapBounds.top;
		}
		if (sourceRect.right > fBitmapBounds.right) {
			float diff = sourceRect.right - fBitmapBounds.right;
			fDestinationRect.right -= diff * fScaleX;
			sourceRect.right = fBitmapBounds.right;
		}
		if (sourceRect.bottom > fBitmapBounds.bottom) {
			float diff = sourceRect.bottom - fBitmapBounds.bottom;
			fDestinationRect.bottom -= diff * fScaleY;
			sourceRect.bottom = fBitmapBounds.bottom;
		}
	} else {
		fScaleX = 1.0;
		fScaleY = 1.0;
	}

	fOffset.x = fDestinationRect.left - sourceRect.left;
	fOffset.y = fDestinationRect.top - sourceRect.top;

	return true;
}


BLFormat
Painter::BitmapPainter::_ConvertToBLFormat(color_space cs)
{
	switch (cs) {
		case B_RGBA32:
		case B_RGB32:
			// Blend2D uses premultiplied alpha by default
			return BL_FORMAT_PRGB32;
		
		case B_RGB24:
			// 24-bit RGB without alpha
			// Blend2D converts to PRGB32 automatically
			return BL_FORMAT_XRGB32;
		
		case B_CMAP8:
			// 8-bit indexed color - needs conversion via palette
			// Convert to PRGB32
			return BL_FORMAT_PRGB32;
		
		case B_RGB15:
		case B_RGBA15:
			// 15/16-bit formats - convert to PRGB32
			return BL_FORMAT_PRGB32;
		
		default:
			// Default to PRGB32
			return BL_FORMAT_PRGB32;
	}
}


void
Painter::BitmapPainter::_SetupCompOp(BLContext& ctx)
{
	switch (fPainter->fDrawingMode) {
		case B_OP_COPY:
			// Simple copy (replace pixels)
			ctx.setCompOp(BL_COMP_OP_SRC_COPY);
			break;
		
		case B_OP_OVER:
			// Alpha blending (overlay with transparency)
			ctx.setCompOp(BL_COMP_OP_SRC_OVER);
			break;
		
		case B_OP_ALPHA:
			if (fPainter->fAlphaSrcMode == B_PIXEL_ALPHA &&
				fPainter->fAlphaFncMode == B_ALPHA_OVERLAY) {
				// Pixel alpha overlay mode
				ctx.setCompOp(BL_COMP_OP_SRC_OVER);
			} else {
				// Other alpha modes - use SRC_OVER as fallback
				ctx.setCompOp(BL_COMP_OP_SRC_OVER);
			}
			break;
		
		default:
			// Default - standard alpha blending
			ctx.setCompOp(BL_COMP_OP_SRC_OVER);
			break;
	}
	
	// Set global alpha if needed
	if (fPainter->fAlphaSrcMode != B_PIXEL_ALPHA) {
		// Constant alpha
		double alpha = fPainter->fPatternHandler.GetAlpha() / 255.0;
		ctx.setGlobalAlpha(alpha);
	}
}


void
Painter::BitmapPainter::_ConvertColorSpace(BLImage& outImage)
{
	// For formats requiring conversion
	if (fColorSpace == B_RGBA32 || fColorSpace == B_RGB32) {
		outImage = fBLImage;
		return;
	}

	// Create temporary bitmap for conversion
	BBitmap* conversionBitmap = new(std::nothrow) BBitmap(
		fBitmapBounds,
		B_BITMAP_NO_SERVER_LINK,
		B_RGBA32
	);
	
	if (conversionBitmap == NULL) {
		fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
			"out of memory\n");
		outImage = fBLImage;
		return;
	}

	ObjectDeleter<BBitmap> bitmapDeleter(conversionBitmap);

	// Convert via ImportBits
	BLImageData srcData;
	fBLImage.getData(&srcData);
	
	status_t err = conversionBitmap->ImportBits(
		srcData.pixelData,
		srcData.size.h * srcData.stride,
		srcData.stride,
		0,
		fColorSpace
	);
	
	if (err < B_OK) {
		fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
			"conversion failed: %s\n", strerror(err));
		outImage = fBLImage;
		return;
	}

	// Handle transparent magic colors
	switch (fColorSpace) {
		case B_RGB32: {
			uint32* bits = (uint32*)conversionBitmap->Bits();
			_TransparentMagicToAlpha(bits,
				conversionBitmap->Bounds().IntegerWidth() + 1,
				conversionBitmap->Bounds().IntegerHeight() + 1,
				conversionBitmap->BytesPerRow(),
				B_TRANSPARENT_MAGIC_RGBA32,
				conversionBitmap);
			break;
		}
		case B_RGB15: {
			uint16* bits = (uint16*)srcData.pixelData;
			_TransparentMagicToAlpha(bits,
				srcData.size.w,
				srcData.size.h,
				srcData.stride,
				B_TRANSPARENT_MAGIC_RGBA15,
				conversionBitmap);
			break;
		}
		default:
			break;
	}

	// Create BLImage from converted bitmap - make a COPY since conversionBitmap will be deleted
	BLResult result = outImage.create(
		conversionBitmap->Bounds().IntegerWidth() + 1,
		conversionBitmap->Bounds().IntegerHeight() + 1,
		BL_FORMAT_PRGB32
	);
	
	if (result == BL_SUCCESS) {
		// Copy pixel data
		BLImageData dstData;
		outImage.getData(&dstData);
		memcpy(dstData.pixelData, conversionBitmap->Bits(),
			conversionBitmap->BitsLength());
	} else {
		fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
			"BLImage creation failed\n");
		outImage = fBLImage;
	}
}


template<typename sourcePixel>
void
Painter::BitmapPainter::_TransparentMagicToAlpha(sourcePixel* buffer,
	uint32 width, uint32 height, uint32 sourceBytesPerRow,
	sourcePixel transparentMagic, BBitmap* output)
{
	uint8* sourceRow = (uint8*)buffer;
	uint8* destRow = (uint8*)output->Bits();
	uint32 destBytesPerRow = output->BytesPerRow();

	for (uint32 y = 0; y < height; y++) {
		sourcePixel* pixel = (sourcePixel*)sourceRow;
		uint32* destPixel = (uint32*)destRow;
		for (uint32 x = 0; x < width; x++, pixel++, destPixel++) {
			if (*pixel == transparentMagic)
				*destPixel &= 0x00ffffff;
		}

		sourceRow += sourceBytesPerRow;
		destRow += destBytesPerRow;
	}
}
