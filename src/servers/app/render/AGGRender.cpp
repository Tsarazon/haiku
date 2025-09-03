/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender.cpp - Main implementation of AGG render engine
 */

#include "AGGRender.h"

#include <agg_pixfmt_rgba.h>
#include <agg_renderer_base.h>
#include <agg_renderer_scanline.h>
#include <agg_scanline_u.h>
#include <agg_rasterizer_scanline_aa.h>

#include <SupportDefs.h>


// Constructor
AGGRender::AGGRender()
	:
	fAggInterface(NULL),
	fPatternHandler(NULL), 
	fInternalTextRenderer(NULL),
	fBuffer(NULL),
	fClippingRegion(NULL),
	fValidClipping(false),
	fDrawingMode(B_OP_COPY),
	fAlphaSrcMode(B_PIXEL_ALPHA),
	fAlphaFncMode(B_ALPHA_OVERLAY),
	fPenSize(1.0),
	fLineWidth(1.0),
	fLineCapMode(B_BUTT_CAP),
	fLineJoinMode(B_MITER_JOIN),
	fMiterLimit(B_DEFAULT_MITER_LIMIT),
	fFillRule(0),
	fSubpixelPrecise(false),
	fIdentityTransform(true),
	fQualityLevel(0),
	fStateStack(),
	fRendererOffsetX(0),
	fRendererOffsetY(0),
	fAlphaMask(NULL),
	fAlphaMaskBitmap(NULL),
	fTextMode(RENDER_TEXT_ALIASED),
	fHinting(true),
	fAntialiasing(true),
	fKerning(true),
	fSubpixelAverageWeight(102),
	fTextGamma(1.8f),
	fFontNeedsUpdate(true),
	fFontManager(NULL),
	fStringRenderer(NULL),
	fLastError(B_OK),
	fCapabilities(0),
	fMemoryUsage(0)
#if DEBUG
	, fDebugMode(false),
	fProfileStart(0),
	fTotalProfileTime(0),
	fProfileCallCount(0)
#endif
{
	// Initialize transform to identity
	fTransform.reset();
	
	// Initialize pattern handler for AGG interface with default colors
	fPatternHandler = new(std::nothrow) PatternHandler();
	if (fPatternHandler != NULL) {
		fPatternHandler->SetColors(rgb_color{0, 0, 0, 255}, rgb_color{255, 255, 255, 255});
	}
}


// Destructor
AGGRender::~AGGRender()
{
	DetachFromBuffer();
	
	delete fClippingRegion;
	delete fAlphaMask;
	delete fPatternHandler;
	delete fAggInterface;
	// StringRenderer is forward declared - set to NULL without delete
	// Actual cleanup will be handled by the complete StringRenderer implementation
	fStringRenderer = NULL;
}


// Buffer Management
status_t
AGGRender::AttachToBuffer(RenderingBuffer* buffer)
{
	if (buffer == NULL)
		return B_BAD_VALUE;
		
	fBuffer = buffer;
	fValidClipping = false;
	
	// Setup AGG interface when buffer is attached
	if (fPatternHandler != NULL) {
		status_t status = _SetupAGGRenderer();
		if (status != B_OK)
			return status;
	}
	
	return B_OK;
}


void
AGGRender::DetachFromBuffer()
{
	fBuffer = NULL;
	fValidClipping = false;
}


BRect
AGGRender::GetBufferBounds() const
{
	if (fBuffer == NULL)
		return BRect();
		
	// Get actual buffer dimensions
	uint32 width = fBuffer->Width();
	uint32 height = fBuffer->Height();
	
	if (width == 0 || height == 0)
		return BRect();
		
	return BRect(0, 0, width - 1, height - 1);
}


// Engine Information
const char*
AGGRender::GetRendererName() const
{
	return "Anti-Grain Geometry Renderer";
}


const char* 
AGGRender::GetRendererVersion() const
{
	return "2.4";
}


uint32
AGGRender::GetCapabilities() const
{
	return RENDER_CAP_SUBPIXEL_AA | 
		   RENDER_CAP_GRADIENTS |
		   RENDER_CAP_BEZIER_PATHS |
		   RENDER_CAP_PATH_CLIPPING |
		   RENDER_CAP_ALPHA_MASK;
}


bool
AGGRender::HasCapability(RenderCapability cap) const
{
	return (GetCapabilities() & cap) != 0;
}


// State Management  
status_t
AGGRender::SetDrawState(const DrawState* state, int32 xOffset, int32 yOffset)
{
	if (state == NULL)
		return B_BAD_VALUE;
		
	// Update internal state from DrawState
	fDrawingMode = state->GetDrawingMode();
	fAlphaSrcMode = state->AlphaSrcMode();
	fAlphaFncMode = state->AlphaFncMode();
	fPenSize = state->PenSize();
	fLineCapMode = state->LineCapMode();
	fLineJoinMode = state->LineJoinMode();
	fMiterLimit = state->MiterLimit();
	fFillRule = state->FillRule();
	fSubpixelPrecise = state->SubPixelPrecise();
	
	// Set transform with offsets
	BAffineTransform transform = state->CombinedTransform();
	SetTransform(transform, xOffset, yOffset);
	
	return B_OK;
}


status_t
AGGRender::GetDrawState(DrawState* state) const
{
	if (state == NULL)
		return B_BAD_VALUE;
		
	// Fill DrawState from internal state
	state->SetDrawingMode(fDrawingMode);
	state->SetBlendingMode(fAlphaSrcMode, fAlphaFncMode);
	state->SetPenSize(fPenSize);
	state->SetLineCapMode(fLineCapMode);
	state->SetLineJoinMode(fLineJoinMode);
	state->SetMiterLimit(fMiterLimit);
	state->SetFillRule(fFillRule);
	state->SetSubPixelPrecise(fSubpixelPrecise);
	
	// Set transform
	BAffineTransform transform = GetTransform();
	state->SetTransform(transform);
	
	return B_OK;
}


// Transform Management
status_t
AGGRender::SetTransform(const BAffineTransform& transform, int32 xOffset, int32 yOffset)
{
	// Convert BAffineTransform to agg::trans_affine
	fTransform = agg::trans_affine(
		transform.sx, transform.shy, transform.shx,
		transform.sy, transform.tx + xOffset, transform.ty + yOffset);
		
	fIdentityTransform = transform.IsIdentity() && (xOffset == 0) && (yOffset == 0);
	
	return B_OK;
}


BAffineTransform
AGGRender::GetTransform() const
{
	BAffineTransform transform;
	transform.sx = fTransform.sx;
	transform.shy = fTransform.shy; 
	transform.shx = fTransform.shx;
	transform.sy = fTransform.sy;
	transform.tx = fTransform.tx;
	transform.ty = fTransform.ty;
	return transform;
}


bool
AGGRender::IsIdentityTransform() const
{
	return fIdentityTransform;
}


// Push/Pop State
status_t
AGGRender::PushState()
{
	RenderState* state = new(std::nothrow) RenderState;
	if (state == NULL)
		return B_NO_MEMORY;
		
	// Save current state
	state->transform = GetTransform();
	state->clippingRegion = fClippingRegion ? new BRegion(*fClippingRegion) : NULL;
	state->drawingMode = fDrawingMode;
	state->penSize = fPenSize;
	// ... save other state members
	
	fStateStack.AddItem(state);
	return B_OK;
}


status_t
AGGRender::PopState()
{
	if (fStateStack.CountItems() == 0)
		return B_ERROR;
		
	RenderState* state = fStateStack.RemoveItemAt(fStateStack.CountItems() - 1);
	if (state == NULL)
		return B_ERROR;
		
	// Restore state
	SetTransform(state->transform);
	delete fClippingRegion;
	fClippingRegion = state->clippingRegion;
	fDrawingMode = state->drawingMode;
	fPenSize = state->penSize;
	// ... restore other state members
	
	delete state;
	return B_OK;
}


int32
AGGRender::GetStateDepth() const
{
	return fStateStack.CountItems();
}


// Get current AGG renderer and pixel format for rasterization operations
status_t
AGGRender::_GetCurrentRendererBase(void** renderer, void** pixfmt)
{
	if (renderer == NULL || pixfmt == NULL)
		return B_BAD_VALUE;
		
	if (fAggInterface == NULL || fBuffer == NULL)
		return B_NO_INIT;
		
	// Return pointers to actual AGG components for rasterization
	*renderer = &(fAggInterface->fBaseRenderer);
	*pixfmt = &(fAggInterface->fPixelFormat);
	
	return B_OK;
}


status_t
AGGRender::_SetupAGGRenderer()
{
	if (fBuffer == NULL)
		return B_BAD_VALUE;
		
	if (fPatternHandler == NULL)
		return B_NO_INIT;
		
	// Create AGG interface if not exists
	if (fAggInterface == NULL) {
		fAggInterface = new(std::nothrow) PainterAggInterface(*fPatternHandler);
		if (fAggInterface == NULL)
			return B_NO_MEMORY;
	}
	
	// Attach AGG rendering buffer to Haiku RenderingBuffer using real AGG API
	fAggInterface->fBuffer.attach((uint8*)fBuffer->Bits(),
		fBuffer->Width(), fBuffer->Height(), fBuffer->BytesPerRow());
	
	return B_OK;
}


// Error handling
status_t
AGGRender::GetLastError() const
{
	return fLastError;
}


const char*
AGGRender::GetLastErrorString() const
{
	return fLastErrorString.String();
}


void
AGGRender::ClearError()
{
	fLastError = B_OK;
	fLastErrorString = "";
}