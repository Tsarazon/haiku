/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_Rasterization.cpp - Rasterization and scanline operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_rasterizer_compound_aa.h>
#include <agg_rasterizer_outline.h>
#include <agg_rasterizer_outline_aa.h>
#include <agg_rasterizer_sl_clip.h>
#include <agg_gamma_functions.h>
#include <agg_scanline_p.h>
#include <agg_scanline_u.h>
#include <agg_scanline_bin.h>
#include <agg_scanline_boolean_algebra.h>
#include <agg_scanline_storage_aa.h>
#include <agg_scanline_storage_bin.h>
#include "agg_scanline_p_subpix.h"
#include "agg_scanline_u_subpix.h"
#include "agg_scanline_storage_subpix.h"
#include <agg_renderer_base.h>
#include <agg_renderer_scanline.h>
#include <agg_renderer_outline_aa.h>
#include <agg_renderer_primitives.h>
#include <agg_rendering_buffer.h>

#include <SupportDefs.h>


// Rasterizer creation and management
AGGRender::RasterizerHandle*
AGGRender::CreateRasterizer(rasterizer_type_e type)
{
	RasterizerHandle* handle = new(std::nothrow) RasterizerHandle;
	if (handle == NULL)
		return NULL;

	handle->type = type;
	handle->rasterizer = NULL;

	switch (type) {
		case RASTERIZER_SCANLINE_AA:
		{
			typedef agg::rasterizer_scanline_aa<> rasterizer_type;
			handle->rasterizer = new(std::nothrow) rasterizer_type;
			break;
		}
		case RASTERIZER_COMPOUND_AA:
		{
			typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer_type;
			handle->rasterizer = new(std::nothrow) rasterizer_type;
			break;
		}
		case RASTERIZER_OUTLINE:
		{
			if (fAggInterface == NULL)
				return NULL;
			
			// Use existing base renderer from AGG interface
			typedef agg::renderer_primitives<renderer_base> primitive_renderer_type;
			primitive_renderer_type* primRend = new(std::nothrow) primitive_renderer_type(fAggInterface->fBaseRenderer);
			if (primRend == NULL)
				return NULL;
			
			// Create outline rasterizer with primitive renderer
			typedef agg::rasterizer_outline<primitive_renderer_type> rasterizer_type;
			handle->rasterizer = new(std::nothrow) rasterizer_type(*primRend);
			break;
		}
		case RASTERIZER_OUTLINE_AA:
		{
			if (fAggInterface == NULL)
				return NULL;
			
			// Use existing base renderer from AGG interface
			typedef agg::renderer_outline_aa<renderer_base> outline_renderer_type;
			outline_renderer_type* outlineRend = new(std::nothrow) outline_renderer_type(fAggInterface->fBaseRenderer);
			if (outlineRend == NULL)
				return NULL;
			
			// Create outline AA rasterizer with outline renderer
			typedef agg::rasterizer_outline_aa<outline_renderer_type> rasterizer_type;
			handle->rasterizer = new(std::nothrow) rasterizer_type(*outlineRend);
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->rasterizer == NULL && (type == RASTERIZER_SCANLINE_AA || type == RASTERIZER_COMPOUND_AA)) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteRasterizer(RasterizerHandle* rasterizer)
{
	if (rasterizer == NULL)
		return B_BAD_VALUE;

	if (rasterizer->rasterizer != NULL) {
		switch (rasterizer->type) {
			case RASTERIZER_SCANLINE_AA:
			{
				typedef agg::rasterizer_scanline_aa<> rasterizer_type;
				delete static_cast<rasterizer_type*>(rasterizer->rasterizer);
				break;
			}
			case RASTERIZER_COMPOUND_AA:
			{
				typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer_type;
				delete static_cast<rasterizer_type*>(rasterizer->rasterizer);
				break;
			}
			default:
				break;
		}
	}

	delete rasterizer;
	return B_OK;
}


status_t
AGGRender::ResetRasterizer(RasterizerHandle* rasterizer)
{
	if (rasterizer == NULL || rasterizer->rasterizer == NULL)
		return B_BAD_VALUE;

	switch (rasterizer->type) {
		case RASTERIZER_SCANLINE_AA:
		{
			typedef agg::rasterizer_scanline_aa<> rasterizer_type;
			static_cast<rasterizer_type*>(rasterizer->rasterizer)->reset();
			break;
		}
		case RASTERIZER_COMPOUND_AA:
		{
			typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer_type;
			static_cast<rasterizer_type*>(rasterizer->rasterizer)->reset();
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


// Scanline creation and management
AGGRender::ScanlineHandle*
AGGRender::CreateScanline(scanline_type_e type)
{
	ScanlineHandle* handle = new(std::nothrow) ScanlineHandle;
	if (handle == NULL)
		return NULL;

	handle->type = type;
	handle->scanline = NULL;

	switch (type) {
		case SCANLINE_P8:
		{
			typedef agg::scanline_p8 scanline_type;
			handle->scanline = new(std::nothrow) scanline_type;
			break;
		}
		case SCANLINE_U8:
		{
			typedef agg::scanline_u8 scanline_type;
			handle->scanline = new(std::nothrow) scanline_type;
			break;
		}
		case SCANLINE_BIN:
		{
			typedef agg::scanline_bin scanline_type;
			handle->scanline = new(std::nothrow) scanline_type;
			break;
		}
		case SCANLINE_U8_AM:
		{
			typedef agg::scanline_u8_am<agg::alpha_mask_gray8> scanline_type;
			handle->scanline = new(std::nothrow) scanline_type;
			break;
		}
		case SCANLINE_P8_SUBPIX:
		{
			typedef agg::scanline_p8_subpix scanline_type;
			handle->scanline = new(std::nothrow) scanline_type;
			break;
		}
		case SCANLINE_U8_SUBPIX:
		{
			typedef agg::scanline_u8_subpix scanline_type;
			handle->scanline = new(std::nothrow) scanline_type;
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->scanline == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteScanline(ScanlineHandle* scanline)
{
	if (scanline == NULL)
		return B_BAD_VALUE;

	if (scanline->scanline != NULL) {
		switch (scanline->type) {
			case SCANLINE_P8:
			{
				typedef agg::scanline_p8 scanline_type;
				delete static_cast<scanline_type*>(scanline->scanline);
				break;
			}
			case SCANLINE_U8:
			{
				typedef agg::scanline_u8 scanline_type;
				delete static_cast<scanline_type*>(scanline->scanline);
				break;
			}
			case SCANLINE_BIN:
			{
				typedef agg::scanline_bin scanline_type;
				delete static_cast<scanline_type*>(scanline->scanline);
				break;
			}
			case SCANLINE_U8_AM:
			{
				typedef agg::scanline_u8_am<agg::alpha_mask_gray8> scanline_type;
				delete static_cast<scanline_type*>(scanline->scanline);
				break;
			}
			case SCANLINE_P8_SUBPIX:
			{
				typedef agg::scanline_p8_subpix scanline_type;
				delete static_cast<scanline_type*>(scanline->scanline);
				break;
			}
			case SCANLINE_U8_SUBPIX:
			{
				typedef agg::scanline_u8_subpix scanline_type;
				delete static_cast<scanline_type*>(scanline->scanline);
				break;
			}
			default:
				break;
		}
	}

	delete scanline;
	return B_OK;
}


// Scanline storage management
AGGRender::ScanlineStorageHandle*
AGGRender::CreateScanlineStorage(scanline_storage_type_e type)
{
	ScanlineStorageHandle* handle = new(std::nothrow) ScanlineStorageHandle;
	if (handle == NULL)
		return NULL;

	handle->type = type;
	handle->storage = NULL;

	switch (type) {
		case SCANLINE_STORAGE_AA8:
		{
			typedef agg::scanline_storage_aa8 storage_type;
			handle->storage = new(std::nothrow) storage_type;
			break;
		}
		case SCANLINE_STORAGE_BIN:
		{
			typedef agg::scanline_storage_bin storage_type;
			handle->storage = new(std::nothrow) storage_type;
			break;
		}
		case SCANLINE_STORAGE_SUBPIX8:
		{
			typedef agg::scanline_storage_subpix<agg::int8u> storage_type;
			handle->storage = new(std::nothrow) storage_type;
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->storage == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteScanlineStorage(ScanlineStorageHandle* storage)
{
	if (storage == NULL)
		return B_BAD_VALUE;

	if (storage->storage != NULL) {
		switch (storage->type) {
			case SCANLINE_STORAGE_AA8:
			{
				typedef agg::scanline_storage_aa8 storage_type;
				delete static_cast<storage_type*>(storage->storage);
				break;
			}
			case SCANLINE_STORAGE_BIN:
			{
				typedef agg::scanline_storage_bin storage_type;
				delete static_cast<storage_type*>(storage->storage);
				break;
			}
			case SCANLINE_STORAGE_SUBPIX8:
			{
				typedef agg::scanline_storage_subpix<agg::int8u> storage_type;
				delete static_cast<storage_type*>(storage->storage);
				break;
			}
			default:
				break;
		}
	}

	delete storage;
	return B_OK;
}


// Rendering buffer management
AGGRender::RenderingBufferHandle*
AGGRender::CreateRenderingBuffer(uint8* buffer, int32 width, int32 height, int32 stride)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0)
		return NULL;

	RenderingBufferHandle* handle = new(std::nothrow) RenderingBufferHandle;
	if (handle == NULL)
		return NULL;

	handle->buffer = new(std::nothrow) agg::rendering_buffer(buffer, width, height, stride);
	if (handle->buffer == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteRenderingBuffer(RenderingBufferHandle* buffer)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	delete buffer->buffer;
	delete buffer;
	return B_OK;
}


status_t
AGGRender::AttachBufferToRenderingBuffer(RenderingBufferHandle* handle, uint8* buffer,
										 int32 width, int32 height, int32 stride)
{
	if (handle == NULL || handle->buffer == NULL || buffer == NULL)
		return B_BAD_VALUE;

	handle->buffer->attach(buffer, width, height, stride);
	return B_OK;
}


// Rasterization operations
status_t
AGGRender::AddPathToRasterizer(RasterizerHandle* rasterizer, RenderPath* path)
{
	if (rasterizer == NULL || path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	switch (rasterizer->type) {
		case RASTERIZER_SCANLINE_AA:
		{
			typedef agg::rasterizer_scanline_aa<> rasterizer_type;
			rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);
			rast->add_path(*storage);
			break;
		}
		case RASTERIZER_COMPOUND_AA:
		{
			typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer_type;
			rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);
			rast->add_path(*storage);
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


status_t
AGGRender::RenderScanlines(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
						   renderer_type_e rendererType, const rgb_color& color)
{
	if (rasterizer == NULL || scanline == NULL || fBuffer == NULL)
		return B_BAD_VALUE;

	// Setup current renderer (color will be set by pixel format)
	status_t status = _SetupAGGRenderer();
	if (status != B_OK)
		return status;

	void* renderer = NULL;
	void* pixfmt = NULL;
	status = _GetCurrentRendererBase(&renderer, &pixfmt);
	if (status != B_OK || renderer == NULL)
		return B_ERROR;

	switch (rasterizer->type) {
		case RASTERIZER_SCANLINE_AA:
		{
			typedef agg::rasterizer_scanline_aa<> rasterizer_type;
			rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);

			switch (scanline->type) {
				case SCANLINE_P8:
				{
					typedef agg::scanline_p8 scanline_type;
					scanline_type* sl = static_cast<scanline_type*>(scanline->scanline);
					
					// Render with appropriate renderer based on type
					if (rendererType == RENDERER_SCANLINE_AA_SOLID) {
						// Now we can use the actual renderer
						typedef agg::renderer_base<agg::pixfmt_bgra32> renderer_base_type;
						typedef agg::renderer_scanline_aa_solid<renderer_base_type> renderer_solid_type;
						
						renderer_base_type* rendBase = static_cast<renderer_base_type*>(renderer);
						renderer_solid_type rendSolid(*rendBase);
						rendSolid.color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
						
						agg::render_scanlines(*rast, *sl, rendSolid);
					}
					break;
				}
				default:
					return B_NOT_SUPPORTED;
			}
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


status_t
AGGRender::RenderScanlinesCompound(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
								   const rgb_color* colors, int32 styleCount)
{
	if (rasterizer == NULL || scanline == NULL || colors == NULL || styleCount <= 0)
		return B_BAD_VALUE;

	if (rasterizer->type != RASTERIZER_COMPOUND_AA)
		return B_NOT_SUPPORTED;

	typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer_type;
	rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);

	// Simplified compound rendering - full implementation would require
	// proper style setup and compound renderer configuration
	// agg::render_scanlines_compound(*rast, *scanline, renderer, styles);
	(void)rast; // Suppress unused variable warning
	return B_NOT_SUPPORTED; // Not fully implemented yet

	return B_OK;
}


// Scanline boolean operations
status_t
AGGRender::ScanlineUnion(ScanlineHandle* sl1, ScanlineHandle* sl2, ScanlineHandle* result)
{
	if (sl1 == NULL || sl2 == NULL || result == NULL)
		return B_BAD_VALUE;

	if (sl1->type != sl2->type || sl2->type != result->type)
		return B_BAD_VALUE;

	switch (sl1->type) {
		case SCANLINE_U8:
		{
			typedef agg::scanline_u8 scanline_type;
			scanline_type* scanline1 = static_cast<scanline_type*>(sl1->scanline);
			scanline_type* scanline2 = static_cast<scanline_type*>(sl2->scanline);
			scanline_type* resultScanline = static_cast<scanline_type*>(result->scanline);
			
			// Use AGG scanline-level union operation
			agg::sbool_add_span_aa<scanline_type, scanline_type> add_functor1;
			agg::sbool_add_span_aa<scanline_type, scanline_type> add_functor2;
			agg::sbool_unite_spans_aa<scanline_type, scanline_type, scanline_type> combine_functor;
			
			agg::sbool_unite_scanlines(*scanline1, *scanline2, *resultScanline,
											add_functor1, add_functor2, combine_functor);
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


status_t
AGGRender::ScanlineIntersection(ScanlineHandle* sl1, ScanlineHandle* sl2, ScanlineHandle* result)
{
	if (sl1 == NULL || sl2 == NULL || result == NULL)
		return B_BAD_VALUE;

	if (sl1->type != sl2->type || sl2->type != result->type)
		return B_BAD_VALUE;

	switch (sl1->type) {
		case SCANLINE_U8:
		{
			typedef agg::scanline_u8 scanline_type;
			scanline_type* scanline1 = static_cast<scanline_type*>(sl1->scanline);
			scanline_type* scanline2 = static_cast<scanline_type*>(sl2->scanline);
			scanline_type* resultScanline = static_cast<scanline_type*>(result->scanline);
			
			// Use AGG scanline-level intersection operation
			agg::sbool_intersect_spans_aa<scanline_type, scanline_type, scanline_type> combine_functor;
			
			agg::sbool_intersect_scanlines(*scanline1, *scanline2, *resultScanline, combine_functor);
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


status_t
AGGRender::ScanlineXor(ScanlineHandle* sl1, ScanlineHandle* sl2, ScanlineHandle* result)
{
	if (sl1 == NULL || sl2 == NULL || result == NULL)
		return B_BAD_VALUE;

	if (sl1->type != sl2->type || sl2->type != result->type)
		return B_BAD_VALUE;

	switch (sl1->type) {
		case SCANLINE_U8:
		{
			typedef agg::scanline_u8 scanline_type;
			scanline_type* scanline1 = static_cast<scanline_type*>(sl1->scanline);
			scanline_type* scanline2 = static_cast<scanline_type*>(sl2->scanline);
			scanline_type* resultScanline = static_cast<scanline_type*>(result->scanline);
			
			// Use AGG scanline-level XOR operation
			agg::sbool_add_span_aa<scanline_type, scanline_type> add_functor1;
			agg::sbool_add_span_aa<scanline_type, scanline_type> add_functor2;
			agg::sbool_xor_spans_aa<scanline_type, scanline_type, scanline_type, agg::sbool_xor_formula_linear<>> combine_functor;
			
			agg::sbool_unite_scanlines(*scanline1, *scanline2, *resultScanline,
											add_functor1, add_functor2, combine_functor);
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


// Subpixel rendering support
status_t
AGGRender::SetSubpixelAccuracy(RasterizerHandle* rasterizer, float accuracy)
{
	if (rasterizer == NULL)
		return B_BAD_VALUE;

	switch (rasterizer->type) {
		case RASTERIZER_SCANLINE_AA:
		{
			typedef agg::rasterizer_scanline_aa<> rasterizer_type;
			rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);
			
			// Use gamma function to control subpixel accuracy
			// Higher gamma values give sharper edges (more accurate subpixel rendering)
			// Lower gamma values give softer edges
			agg::gamma_power gamma_func(accuracy);
			rast->gamma(gamma_func);
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


bool
AGGRender::IsRasterizerReady(RasterizerHandle* rasterizer)
{
	if (rasterizer == NULL || rasterizer->rasterizer == NULL)
		return false;

	switch (rasterizer->type) {
		case RASTERIZER_SCANLINE_AA:
		{
			typedef agg::rasterizer_scanline_aa<> rasterizer_type;
			rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);
			return rast->rewind_scanlines();
		}
		case RASTERIZER_COMPOUND_AA:
		{
			typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer_type;
			rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);
			return rast->rewind_scanlines();
		}
		default:
			return false;
	}
}