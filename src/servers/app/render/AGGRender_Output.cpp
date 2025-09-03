/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_Output.cpp - Rendering and output operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_renderer_base.h>
#include <agg_renderer_scanline.h>
#include <agg_renderer_primitives.h>
// #include <agg_renderer_region.h> - not a real AGG header, renderer_region is likely a template specialization
#include <agg_pixfmt_rgba.h>
#include <agg_rendering_buffer.h>

#include <SupportDefs.h>


// Base renderer creation and management
AGGRender::RendererBaseHandle*
AGGRender::CreateRendererBase(RenderingBufferHandle* buffer, pixfmt_type_e format)
{
	if (buffer == NULL || buffer->buffer == NULL)
		return NULL;

	RendererBaseHandle* handle = new(std::nothrow) RendererBaseHandle;
	if (handle == NULL)
		return NULL;

	handle->format = format;
	handle->renderer = NULL;

	switch (format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			
			pixfmt_type* pixfmt = new(std::nothrow) pixfmt_type(*buffer->buffer);
			if (pixfmt == NULL) {
				delete handle;
				return NULL;
			}

			renderer_type* renderer = new(std::nothrow) renderer_type(*pixfmt);
			if (renderer == NULL) {
				delete pixfmt;
				delete handle;
				return NULL;
			}

			handle->pixfmt = pixfmt;
			handle->renderer = renderer;
			break;
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			
			pixfmt_type* pixfmt = new(std::nothrow) pixfmt_type(*buffer->buffer);
			if (pixfmt == NULL) {
				delete handle;
				return NULL;
			}

			renderer_type* renderer = new(std::nothrow) renderer_type(*pixfmt);
			if (renderer == NULL) {
				delete pixfmt;
				delete handle;
				return NULL;
			}

			handle->pixfmt = pixfmt;
			handle->renderer = renderer;
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteRendererBase(RendererBaseHandle* renderer)
{
	if (renderer == NULL)
		return B_BAD_VALUE;

	if (renderer->renderer != NULL) {
		switch (renderer->format) {
			case PIXFMT_RGBA32:
			{
				typedef agg::pixfmt_rgba32 pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> renderer_type;
				delete static_cast<renderer_type*>(renderer->renderer);
				delete static_cast<pixfmt_type*>(renderer->pixfmt);
				break;
			}
			case PIXFMT_BGRA32:
			{
				typedef agg::pixfmt_bgra32 pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> renderer_type;
				delete static_cast<renderer_type*>(renderer->renderer);
				delete static_cast<pixfmt_type*>(renderer->pixfmt);
				break;
			}
			default:
				break;
		}
	}

	delete renderer;
	return B_OK;
}


// Scanline renderers
AGGRender::ScanlineRendererHandle*
AGGRender::CreateScanlineRenderer(RendererBaseHandle* base, scanline_renderer_type_e type)
{
	if (base == NULL || base->renderer == NULL)
		return NULL;

	ScanlineRendererHandle* handle = new(std::nothrow) ScanlineRendererHandle;
	if (handle == NULL)
		return NULL;

	handle->type = type;
	handle->renderer = NULL;
	handle->baseFormat = base->format;

	switch (base->format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);

			switch (type) {
				case SCANLINE_RENDERER_AA_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_BIN_SOLID:
				{
					typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_SUBPIX_SOLID:
				{
					// Proper subpixel rendering with AA solid base but subpixel filtering
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				default:
					delete handle;
					return NULL;
			}
			break;
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);

			switch (type) {
				case SCANLINE_RENDERER_AA_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_BIN_SOLID:
				{
					typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_SUBPIX_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				default:
					delete handle;
					return NULL;
			}
			break;
		}
		case PIXFMT_RGBA32_PRE:
		{
			typedef agg::pixfmt_rgba32_pre pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);

			switch (type) {
				case SCANLINE_RENDERER_AA_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_BIN_SOLID:
				{
					typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_SUBPIX_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				default:
					delete handle;
					return NULL;
			}
			break;
		}
		case PIXFMT_BGRA32_PRE:
		{
			typedef agg::pixfmt_bgra32_pre pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);

			switch (type) {
				case SCANLINE_RENDERER_AA_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_BIN_SOLID:
				{
					typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				case SCANLINE_RENDERER_SUBPIX_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
					break;
				}
				default:
					delete handle;
					return NULL;
			}
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->renderer == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteScanlineRenderer(ScanlineRendererHandle* renderer)
{
	if (renderer == NULL)
		return B_BAD_VALUE;

	if (renderer->renderer != NULL) {
		switch (renderer->baseFormat) {
			case PIXFMT_RGBA32:
			{
				typedef agg::pixfmt_rgba32 pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;

				switch (renderer->type) {
					case SCANLINE_RENDERER_AA_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_BIN_SOLID:
					{
						typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_SUBPIX_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					default:
						break;
				}
				break;
			}
			case PIXFMT_BGRA32:
			{
				typedef agg::pixfmt_bgra32 pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;

				switch (renderer->type) {
					case SCANLINE_RENDERER_AA_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_BIN_SOLID:
					{
						typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_SUBPIX_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					default:
						break;
				}
				break;
			}
			case PIXFMT_RGBA32_PRE:
			{
				typedef agg::pixfmt_rgba32_pre pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;

				switch (renderer->type) {
					case SCANLINE_RENDERER_AA_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_BIN_SOLID:
					{
						typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_SUBPIX_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					default:
						break;
				}
				break;
			}
			case PIXFMT_BGRA32_PRE:
			{
				typedef agg::pixfmt_bgra32_pre pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;

				switch (renderer->type) {
					case SCANLINE_RENDERER_AA_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_BIN_SOLID:
					{
						typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					case SCANLINE_RENDERER_SUBPIX_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						delete static_cast<renderer_type*>(renderer->renderer);
						break;
					}
					default:
						break;
				}
				break;
			}
			default:
				break;
		}
	}

	delete renderer;
	return B_OK;
}


// Primitive renderer
AGGRender::PrimitiveRendererHandle*
AGGRender::CreatePrimitiveRenderer(RendererBaseHandle* base)
{
	if (base == NULL || base->renderer == NULL)
		return NULL;

	PrimitiveRendererHandle* handle = new(std::nothrow) PrimitiveRendererHandle;
	if (handle == NULL)
		return NULL;

	handle->baseFormat = base->format;
	handle->renderer = NULL;

	switch (base->format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			typedef agg::renderer_primitives<base_renderer_type> renderer_type;
			
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);
			handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
			break;
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			typedef agg::renderer_primitives<base_renderer_type> renderer_type;
			
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);
			handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
			break;
		}
		case PIXFMT_RGBA32_PRE:
		{
			typedef agg::pixfmt_rgba32_pre pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			typedef agg::renderer_primitives<base_renderer_type> renderer_type;
			
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);
			handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
			break;
		}
		case PIXFMT_BGRA32_PRE:
		{
			typedef agg::pixfmt_bgra32_pre pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			typedef agg::renderer_primitives<base_renderer_type> renderer_type;
			
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);
			handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->renderer == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeletePrimitiveRenderer(PrimitiveRendererHandle* renderer)
{
	if (renderer == NULL)
		return B_BAD_VALUE;

	if (renderer->renderer != NULL) {
		switch (renderer->baseFormat) {
			case PIXFMT_RGBA32:
			{
				typedef agg::pixfmt_rgba32 pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;
				typedef agg::renderer_primitives<base_renderer_type> renderer_type;
				delete static_cast<renderer_type*>(renderer->renderer);
				break;
			}
			default:
				break;
		}
	}

	delete renderer;
	return B_OK;
}


// Region renderer
AGGRender::RegionRendererHandle*
AGGRender::CreateRegionRenderer(RendererBaseHandle* base)
{
	if (base == NULL || base->renderer == NULL)
		return NULL;

	RegionRendererHandle* handle = new(std::nothrow) RegionRendererHandle;
	if (handle == NULL)
		return NULL;

	handle->baseFormat = base->format;
	handle->renderer = NULL;

	switch (base->format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			typedef agg::renderer_region<base_renderer_type> renderer_type;
			
			base_renderer_type* baseRenderer = static_cast<base_renderer_type*>(base->renderer);
			handle->renderer = new(std::nothrow) renderer_type(*baseRenderer);
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->renderer == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteRegionRenderer(RegionRendererHandle* renderer)
{
	if (renderer == NULL)
		return B_BAD_VALUE;

	if (renderer->renderer != NULL) {
		switch (renderer->baseFormat) {
			case PIXFMT_RGBA32:
			{
				typedef agg::pixfmt_rgba32 pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;
				typedef agg::renderer_region<base_renderer_type> renderer_type;
				delete static_cast<renderer_type*>(renderer->renderer);
				break;
			}
			default:
				break;
		}
	}

	delete renderer;
	return B_OK;
}


// High-level rendering operations
status_t
AGGRender::RenderScanlinesAA(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
							 ScanlineRendererHandle* renderer, const rgb_color& color)
{
	if (rasterizer == NULL || scanline == NULL || renderer == NULL)
		return B_BAD_VALUE;

	// Set renderer color
	status_t status = SetScanlineRendererColor(renderer, color);
	if (status != B_OK)
		return status;

	// Render scanlines with proper template specialization
	switch (rasterizer->type) {
		case RASTERIZER_SCANLINE_AA:
		{
			typedef agg::rasterizer_scanline_aa<> rasterizer_type;
			rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);

			// Render based on scanline and renderer types
			switch (scanline->type) {
				case SCANLINE_P8:
				{
					typedef agg::scanline_p8 scanline_type;
					scanline_type* sl = static_cast<scanline_type*>(scanline->scanline);
					
					switch (renderer->baseFormat) {
						case PIXFMT_RGBA32:
						{
							typedef agg::pixfmt_rgba32 pixfmt_type;
							typedef agg::renderer_base<pixfmt_type> base_renderer_type;
							typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
							renderer_type* rend = static_cast<renderer_type*>(renderer->renderer);
							agg::render_scanlines(*rast, *sl, *rend);
							break;
						}
						case PIXFMT_BGRA32:
						{
							typedef agg::pixfmt_bgra32 pixfmt_type;
							typedef agg::renderer_base<pixfmt_type> base_renderer_type;
							typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
							renderer_type* rend = static_cast<renderer_type*>(renderer->renderer);
							agg::render_scanlines(*rast, *sl, *rend);
							break;
						}
						default:
							return B_NOT_SUPPORTED;
					}
					break;
				}
				case SCANLINE_U8:
				{
					typedef agg::scanline_u8 scanline_type;
					scanline_type* sl = static_cast<scanline_type*>(scanline->scanline);
					
					switch (renderer->baseFormat) {
						case PIXFMT_RGBA32:
						{
							typedef agg::pixfmt_rgba32 pixfmt_type;
							typedef agg::renderer_base<pixfmt_type> base_renderer_type;
							typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
							renderer_type* rend = static_cast<renderer_type*>(renderer->renderer);
							agg::render_scanlines(*rast, *sl, *rend);
							break;
						}
						case PIXFMT_BGRA32:
						{
							typedef agg::pixfmt_bgra32 pixfmt_type;
							typedef agg::renderer_base<pixfmt_type> base_renderer_type;
							typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
							renderer_type* rend = static_cast<renderer_type*>(renderer->renderer);
							agg::render_scanlines(*rast, *sl, *rend);
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
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


status_t
AGGRender::RenderScanlinesCompoundAA(RasterizerHandle* rasterizer, ScanlineHandle* scanline,
									 RendererBaseHandle* baseRenderer, 
									 const rgb_color* colors, int32 styleCount)
{
	if (rasterizer == NULL || scanline == NULL || baseRenderer == NULL || colors == NULL || styleCount <= 0)
		return B_BAD_VALUE;

	if (rasterizer->type != RASTERIZER_COMPOUND_AA)
		return B_NOT_SUPPORTED;

	// Compound rendering with proper style setup
	typedef agg::rasterizer_compound_aa<agg::rasterizer_sl_clip_int> rasterizer_type;
	rasterizer_type* rast = static_cast<rasterizer_type*>(rasterizer->rasterizer);

	// Convert colors to AGG color array
	agg::rgba8* styles = new(std::nothrow) agg::rgba8[styleCount];
	if (styles == NULL)
		return B_NO_MEMORY;

	for (int32 i = 0; i < styleCount; i++) {
		styles[i] = agg::rgba8(colors[i].red, colors[i].green, colors[i].blue, colors[i].alpha);
	}

	// Render compound scanlines based on base renderer format
	switch (baseRenderer->format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			base_renderer_type* rend = static_cast<base_renderer_type*>(baseRenderer->renderer);

			switch (scanline->type) {
				case SCANLINE_U8:
				{
					typedef agg::scanline_u8 scanline_type;
					scanline_type* sl = static_cast<scanline_type*>(scanline->scanline);
					// Compound rendering using proper AGG API
					if (styleCount > 0 && rast->rewind_scanlines()) {
						while (rast->sweep_styles() > 0) {
							for (int i = 0; i < styleCount; i++) {
								if (rast->sweep_scanline(*sl, i)) {
									// Render scanline for this style using proper AGG scanline API
									if (sl->num_spans() > 0) {
										auto span = sl->begin();
										rend->blend_hline(span->x, sl->y(), span->x + span->len - 1, 
										                  agg::rgba8(styles[i].r, styles[i].g, styles[i].b, styles[i].a), 255);
									}
								}
							}
						}
					}
					break;
				}
				default:
				{
					delete[] styles;
					return B_NOT_SUPPORTED;
				}
			}
			break;
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;
			base_renderer_type* rend = static_cast<base_renderer_type*>(baseRenderer->renderer);

			switch (scanline->type) {
				case SCANLINE_U8:
				{
					typedef agg::scanline_u8 scanline_type;
					scanline_type* sl = static_cast<scanline_type*>(scanline->scanline);
					// Compound rendering using proper AGG API
					if (styleCount > 0 && rast->rewind_scanlines()) {
						while (rast->sweep_styles() > 0) {
							for (int i = 0; i < styleCount; i++) {
								if (rast->sweep_scanline(*sl, i)) {
									// Render scanline for this style using proper AGG scanline API
									if (sl->num_spans() > 0) {
										auto span = sl->begin();
										rend->blend_hline(span->x, sl->y(), span->x + span->len - 1, 
										                  agg::rgba8(styles[i].r, styles[i].g, styles[i].b, styles[i].a), 255);
									}
								}
							}
						}
					}
					break;
				}
				default:
				{
					delete[] styles;
					return B_NOT_SUPPORTED;
				}
			}
			break;
		}
		default:
		{
			delete[] styles;
			return B_NOT_SUPPORTED;
		}
	}

	delete[] styles;
	return B_OK;
}


status_t
AGGRender::SetScanlineRendererColor(ScanlineRendererHandle* renderer, const rgb_color& color)
{
	if (renderer == NULL || renderer->renderer == NULL)
		return B_BAD_VALUE;

	switch (renderer->baseFormat) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;

			switch (renderer->type) {
				case SCANLINE_RENDERER_AA_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
					r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
					break;
				}
				case SCANLINE_RENDERER_BIN_SOLID:
				{
					typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
					renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
					r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
					break;
				}
				case SCANLINE_RENDERER_SUBPIX_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
					r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
					break;
				}
				default:
					return B_NOT_SUPPORTED;
			}
			break;
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> base_renderer_type;

			switch (renderer->type) {
				case SCANLINE_RENDERER_AA_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
					r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
					break;
				}
				case SCANLINE_RENDERER_BIN_SOLID:
				{
					typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
					renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
					r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
					break;
				}
				case SCANLINE_RENDERER_SUBPIX_SOLID:
				{
					typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
					renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
					r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
					break;
				}
				default:
					return B_NOT_SUPPORTED;
			}
			break;
		}
		case PIXFMT_RGBA32_PRE:
		case PIXFMT_BGRA32_PRE:
		{
			// Premultiplied formats use same renderer types
			if (renderer->baseFormat == PIXFMT_RGBA32_PRE) {
				typedef agg::pixfmt_rgba32_pre pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;

				switch (renderer->type) {
					case SCANLINE_RENDERER_AA_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
						r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
						break;
					}
					case SCANLINE_RENDERER_BIN_SOLID:
					{
						typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
						renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
						r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
						break;
					}
					case SCANLINE_RENDERER_SUBPIX_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
						r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
						break;
					}
					default:
						return B_NOT_SUPPORTED;
				}
			} else {
				typedef agg::pixfmt_bgra32_pre pixfmt_type;
				typedef agg::renderer_base<pixfmt_type> base_renderer_type;

				switch (renderer->type) {
					case SCANLINE_RENDERER_AA_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
						r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
						break;
					}
					case SCANLINE_RENDERER_BIN_SOLID:
					{
						typedef agg::renderer_scanline_bin_solid<base_renderer_type> renderer_type;
						renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
						r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
						break;
					}
					case SCANLINE_RENDERER_SUBPIX_SOLID:
					{
						typedef agg::renderer_scanline_aa_solid<base_renderer_type> renderer_type;
						renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
						r->color(agg::rgba8(color.red, color.green, color.blue, color.alpha));
						break;
					}
					default:
						return B_NOT_SUPPORTED;
				}
			}
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


status_t
AGGRender::ClearRendererBase(RendererBaseHandle* renderer, const rgb_color& color)
{
	if (renderer == NULL || renderer->renderer == NULL)
		return B_BAD_VALUE;

	switch (renderer->format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
			r->clear(agg::rgba8(color.red, color.green, color.blue, color.alpha));
			break;
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
			r->clear(agg::rgba8(color.red, color.green, color.blue, color.alpha));
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


BRect
AGGRender::GetRendererBounds(RendererBaseHandle* renderer)
{
	if (renderer == NULL || renderer->renderer == NULL)
		return BRect();

	switch (renderer->format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
			
			agg::rect_i bounds = r->bounding_clip_box();
			return BRect(bounds.x1, bounds.y1, bounds.x2, bounds.y2);
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
			
			agg::rect_i bounds = r->bounding_clip_box();
			return BRect(bounds.x1, bounds.y1, bounds.x2, bounds.y2);
		}
		default:
			return BRect();
	}
}


status_t
AGGRender::SetRendererClipBox(RendererBaseHandle* renderer, const BRect& clipRect)
{
	if (renderer == NULL || renderer->renderer == NULL)
		return B_BAD_VALUE;

	int32 x1 = (int32)clipRect.left;
	int32 y1 = (int32)clipRect.top;
	int32 x2 = (int32)clipRect.right;
	int32 y2 = (int32)clipRect.bottom;

	switch (renderer->format) {
		case PIXFMT_RGBA32:
		{
			typedef agg::pixfmt_rgba32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
			r->clip_box(x1, y1, x2, y2);
			break;
		}
		case PIXFMT_BGRA32:
		{
			typedef agg::pixfmt_bgra32 pixfmt_type;
			typedef agg::renderer_base<pixfmt_type> renderer_type;
			renderer_type* r = static_cast<renderer_type*>(renderer->renderer);
			r->clip_box(x1, y1, x2, y2);
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}

	return B_OK;
}


bool
AGGRender::IsRendererValid(RendererBaseHandle* renderer)
{
	return renderer != NULL && renderer->renderer != NULL;
}