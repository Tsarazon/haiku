/*
 * Copyright 2025, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ruslan
 *
 * Gen 9+ Universal Planes implementation for overlay API.
 *
 * Reference: Intel PRM Vol 12 "Display Engine"
 * Reference: Linux i915 driver skl_universal_plane.c, skl_scaler.c
 *
 * Gen 9+ (Skylake+) replaced legacy overlay with Universal Planes:
 * - Each pipe has multiple planes (primary, sprites, cursor)
 * - All planes share the same register interface
 * - Hardware scaler available per-pipe (PS_CTRL)
 * - YUV formats supported via PLANE_CTL format bits
 *
 * Register offsets verified against i915 skl_universal_plane_regs.h:
 * - Plane 1 Pipe A base: 0x70180
 * - Plane 2 offset: +0x100
 * - Pipe B offset: +0x1000
 * - Pipe C offset: +0x2000
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <stdlib.h>
#include <string.h>

#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme planes: " x)
#define ERROR(x...) _sPrintf("intel_extreme planes: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


/*
 * Gen 9+ Universal Plane registers
 * Reference: i915 skl_universal_plane_regs.h
 *
 * Supported generations:
 * - Gen 9  (Skylake, Kaby Lake, Coffee Lake): 3 pipes, 3 planes/pipe
 * - Gen 11 (Ice Lake): 3 pipes, 7 planes/pipe, extended format mask
 * - Gen 12 (Tiger Lake+): 4 pipes, 7 planes/pipe
 */

/* Plane register base addresses */
#define _PLANE_CTL_1_A			0x70180
#define _PLANE_CTL_2_A			0x70280
#define _PLANE_CTL_1_B			0x71180
#define _PLANE_CTL_2_B			0x71280
#define _PLANE_CTL_1_C			0x72180  /* Gen 9+ */
#define _PLANE_CTL_1_D			0x73180  /* Gen 12+ Pipe D */

#define _PLANE_STRIDE_1_A		0x70188
#define _PLANE_STRIDE_2_A		0x70288

#define _PLANE_POS_1_A			0x7018C
#define _PLANE_POS_2_A			0x7028C

#define _PLANE_SIZE_1_A			0x70190
#define _PLANE_SIZE_2_A			0x70290

#define _PLANE_KEYVAL_1_A		0x70194
#define _PLANE_KEYVAL_2_A		0x70294

#define _PLANE_KEYMSK_1_A		0x70198
#define _PLANE_KEYMSK_2_A		0x70298
#define PLANE_KEYMSK_ENABLE		(1 << 31)

#define _PLANE_SURF_1_A			0x7019C
#define _PLANE_SURF_2_A			0x7029C
#define PLANE_SURF_ADDR_MASK		0xFFFFF000

#define _PLANE_OFFSET_1_A		0x701A4
#define _PLANE_OFFSET_2_A		0x702A4

/* Gen 11+ PLANE_COLOR_CTL */
#define _PLANE_COLOR_CTL_1_A		0x701CC
#define _PLANE_COLOR_CTL_2_A		0x702CC
#define PLANE_COLOR_CTL_ENABLE			(1 << 31)
#define PLANE_COLOR_ALPHA_MASK			(0x3 << 4)
#define PLANE_COLOR_ALPHA_DISABLE		(0 << 4)
#define PLANE_COLOR_ALPHA_SW_PREMULT		(2 << 4)
#define PLANE_COLOR_ALPHA_HW_PREMULT		(3 << 4)

/* PLANE_CTL bits - from i915 skl_universal_plane_regs.h */
#define PLANE_CTL_ENABLE			(1 << 31)
#define PLANE_CTL_PIPE_GAMMA_ENABLE		(1 << 30)

/* Format mask differs between generations */
#define PLANE_CTL_FORMAT_MASK_SKL		(0xF << 24)  /* Gen 9 */
#define PLANE_CTL_FORMAT_MASK_ICL		(0x1F << 23) /* Gen 11+ */

#define PLANE_CTL_FORMAT_YUV422			(0 << 24)
#define PLANE_CTL_FORMAT_NV12			(1 << 24)
#define PLANE_CTL_FORMAT_XRGB_2101010		(2 << 24)
#define PLANE_CTL_FORMAT_P010			(3 << 24)  /* Gen 11+ */
#define PLANE_CTL_FORMAT_XRGB_8888		(4 << 24)
#define PLANE_CTL_FORMAT_P012			(5 << 24)  /* Gen 11+ */
#define PLANE_CTL_FORMAT_XRGB_16161616F		(6 << 24)
#define PLANE_CTL_FORMAT_P016			(7 << 24)  /* Gen 11+ */
#define PLANE_CTL_FORMAT_XYUV			(8 << 24)  /* Gen 11+ */
#define PLANE_CTL_FORMAT_INDEXED		(12 << 24)
#define PLANE_CTL_FORMAT_RGB_565		(14 << 24)

#define PLANE_CTL_PIPE_CSC_ENABLE		(1 << 23)

#define PLANE_CTL_KEY_ENABLE_MASK		(0x3 << 21)
#define PLANE_CTL_KEY_ENABLE_SOURCE		(1 << 21)
#define PLANE_CTL_KEY_ENABLE_DEST		(2 << 21)

#define PLANE_CTL_YUV_TO_RGB_CSC_FORMAT_BT709	(1 << 18)

#define PLANE_CTL_YUV422_ORDER_MASK		(0x3 << 16)
#define PLANE_CTL_YUV422_ORDER_YUYV		(0 << 16)
#define PLANE_CTL_YUV422_ORDER_UYVY		(1 << 16)
#define PLANE_CTL_YUV422_ORDER_YVYU		(2 << 16)
#define PLANE_CTL_YUV422_ORDER_VYUY		(3 << 16)

#define PLANE_CTL_TILED_MASK			(0x7 << 10)
#define PLANE_CTL_TILED_LINEAR			(0 << 10)
#define PLANE_CTL_TILED_X			(1 << 10)
#define PLANE_CTL_TILED_Y			(4 << 10)
#define PLANE_CTL_TILED_YF			(5 << 10)

#define PLANE_CTL_FLIP_HORIZONTAL		(1 << 8)

#define PLANE_CTL_ALPHA_MASK			(0x3 << 4)
#define PLANE_CTL_ALPHA_DISABLE			(0 << 4)

#define PLANE_CTL_ROTATE_MASK			(0x3 << 0)
#define PLANE_CTL_ROTATE_0			(0 << 0)


/*
 * Pipe Scaler registers
 * Reference: i915 skl_scaler.c, i915_reg.h
 *
 * Scaler 1 Pipe A: 0x68180
 * Scaler 2 offset: +0x100
 * Pipe B offset: +0x800
 */
#define _PS_CTRL_1_A			0x68180
#define _PS_CTRL_2_A			0x68280
#define _PS_CTRL_1_B			0x68980

#define PS_CTRL_SCALER_EN		(1 << 31)
#define PS_CTRL_SCALER_MODE_MASK	(0x3 << 28)
#define PS_CTRL_SCALER_MODE_DYN		(0 << 28)
#define PS_CTRL_SCALER_MODE_HQ		(1 << 28)
#define PS_CTRL_SCALER_BINDING_MASK	(0x7 << 25)
#define PS_CTRL_PLANE_SEL_MASK		(0x7 << 25)
#define PS_CTRL_PLANE_SEL(p)		(((p) + 1) << 25)
#define PS_CTRL_FILTER_MASK		(0x3 << 23)
#define PS_CTRL_FILTER_MEDIUM		(0 << 23)

#define _PS_WIN_POS_1_A			0x68170
#define _PS_WIN_POS_2_A			0x68270

#define _PS_WIN_SZ_1_A			0x68174
#define _PS_WIN_SZ_2_A			0x68274


/*
 * Helper macros for register offsets
 */
#define PIPE_OFFSET(pipe)		((pipe) * 0x1000)
#define PLANE_OFFSET(plane)		((plane) * 0x100)
#define SCALER_PIPE_OFFSET(pipe)	((pipe) * 0x800)
#define SCALER_OFFSET(scaler)		((scaler) * 0x100)

#define PLANE_CTL(pipe, plane) \
	(_PLANE_CTL_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_STRIDE(pipe, plane) \
	(_PLANE_STRIDE_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_POS(pipe, plane) \
	(_PLANE_POS_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_SIZE(pipe, plane) \
	(_PLANE_SIZE_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_KEYVAL(pipe, plane) \
	(_PLANE_KEYVAL_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_KEYMSK(pipe, plane) \
	(_PLANE_KEYMSK_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_SURF(pipe, plane) \
	(_PLANE_SURF_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_OFFSET_REG(pipe, plane) \
	(_PLANE_OFFSET_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PLANE_COLOR_CTL(pipe, plane) \
	(_PLANE_COLOR_CTL_1_A + PIPE_OFFSET(pipe) + PLANE_OFFSET(plane))

#define PS_CTRL(pipe, scaler) \
	(_PS_CTRL_1_A + SCALER_PIPE_OFFSET(pipe) + SCALER_OFFSET(scaler))

#define PS_WIN_POS(pipe, scaler) \
	(_PS_WIN_POS_1_A + SCALER_PIPE_OFFSET(pipe) + SCALER_OFFSET(scaler))

#define PS_WIN_SZ(pipe, scaler) \
	(_PS_WIN_SZ_1_A + SCALER_PIPE_OFFSET(pipe) + SCALER_OFFSET(scaler))


/*
 * Overlay state uses struct overlay from accelerant.h
 * Gen 9+ fields: pipe, plane, scaler, plane_ctl
 */


static uint32
color_space_to_plane_ctl(color_space space)
{
	switch (space) {
		case B_RGB15:
		case B_RGB16:
			return PLANE_CTL_FORMAT_RGB_565;
		case B_RGB32:
		case B_RGBA32:
			return PLANE_CTL_FORMAT_XRGB_8888;
		case B_YCbCr422:
			return PLANE_CTL_FORMAT_YUV422
				| PLANE_CTL_YUV422_ORDER_YUYV
				| PLANE_CTL_YUV_TO_RGB_CSC_FORMAT_BT709;
		default:
			ERROR("Unsupported color space %d\n", space);
			return PLANE_CTL_FORMAT_XRGB_8888;
	}
}


static uint32
bytes_per_pixel(color_space space)
{
	switch (space) {
		case B_RGB15:
		case B_RGB16:
		case B_YCbCr422:
			return 2;
		case B_RGB32:
		case B_RGBA32:
			return 4;
		default:
			return 4;
	}
}


static int
gpu_generation(void)
{
	return gInfo->shared_info->device_type.Generation();
}


static void
plane_disable(int pipe, int plane)
{
	/* Gen 11+ requires clearing PLANE_COLOR_CTL */
	if (gpu_generation() >= 11)
		write32(PLANE_COLOR_CTL(pipe, plane), 0);

	write32(PLANE_CTL(pipe, plane), 0);
	/* Write to SURF arms the double-buffer update */
	write32(PLANE_SURF(pipe, plane), 0);
}


static void
scaler_disable(int pipe, int scaler)
{
	write32(PS_CTRL(pipe, scaler), 0);
}


static void
set_plane_color_key(int pipe, int plane, const overlay_window* window)
{
	uint32 keyval = 0;
	uint32 keymsk = 0;

	switch (gInfo->shared_info->current_mode.space) {
		case B_CMAP8:
			keyval = window->blue.value;
			keymsk = 0xFF;
			break;
		case B_RGB15:
			keyval = (window->red.value << 10)
				| (window->green.value << 5)
				| window->blue.value;
			keymsk = (window->red.mask << 10)
				| (window->green.mask << 5)
				| window->blue.mask;
			break;
		case B_RGB16:
			keyval = (window->red.value << 11)
				| (window->green.value << 5)
				| window->blue.value;
			keymsk = (window->red.mask << 11)
				| (window->green.mask << 5)
				| window->blue.mask;
			break;
		case B_RGB32:
		default:
			keyval = (window->red.value << 16)
				| (window->green.value << 8)
				| window->blue.value;
			keymsk = (window->red.mask << 16)
				| (window->green.mask << 8)
				| window->blue.mask;
			break;
	}

	write32(PLANE_KEYVAL(pipe, plane), keyval);
	write32(PLANE_KEYMSK(pipe, plane), keymsk | PLANE_KEYMSK_ENABLE);
}


static void
configure_scaler(int pipe, int scaler, int plane,
	uint32 srcW, uint32 srcH, uint32 dstW, uint32 dstH,
	uint32 dstX, uint32 dstY)
{
	/* Window position: x in [31:16], y in [15:0] */
	write32(PS_WIN_POS(pipe, scaler), (dstX << 16) | dstY);

	/* Window size: width in [31:16], height in [15:0] */
	write32(PS_WIN_SZ(pipe, scaler), (dstW << 16) | dstH);

	/*
	 * Enable scaler with:
	 * - Dynamic mode (auto select between 7-tap and 5-tap)
	 * - Bind to plane
	 * - Medium filter quality
	 */
	uint32 ctrl = PS_CTRL_SCALER_EN
		| PS_CTRL_SCALER_MODE_DYN
		| PS_CTRL_PLANE_SEL(plane)
		| PS_CTRL_FILTER_MEDIUM;

	write32(PS_CTRL(pipe, scaler), ctrl);
}


/*
 * Public overlay API
 */


uint32
intel_overlay_count(const display_mode* mode)
{
	CALLED();
	/*
	 * Gen 9+ has multiple sprite planes per pipe.
	 * For simplicity, expose 1 overlay.
	 */
	return 1;
}


const uint32*
intel_overlay_supported_spaces(const display_mode* mode)
{
	CALLED();
	/*
	 * Gen 9+ Universal Planes support:
	 * - RGB: 565, 8888, 2101010, 16F
	 * - YUV: 422 (YUYV/UYVY), NV12, P010
	 *
	 * Reference: PLANE_CTL format field
	 */
	static const uint32 kSupportedSpaces[] = {
		B_RGB15,
		B_RGB16,
		B_RGB32,
		B_YCbCr422,
		0
	};

	return kSupportedSpaces;
}


uint32
intel_overlay_supported_features(uint32 colorSpace)
{
	CALLED();
	/*
	 * Gen 9+ plane features:
	 * - Color keying (destination key)
	 * - Hardware scaling (via pipe scaler)
	 * - Horizontal flip
	 */
	return B_OVERLAY_COLOR_KEY
		| B_OVERLAY_HORIZONTAL_FILTERING
		| B_OVERLAY_VERTICAL_FILTERING
		| B_OVERLAY_HORIZONTAL_MIRRORING;
}


const overlay_buffer*
intel_allocate_overlay_buffer(color_space colorSpace, uint16 width,
	uint16 height)
{
	CALLED();
	TRACE("Allocate overlay buffer: %dx%d, space %d\n",
		width, height, colorSpace);

	uint32 bpp = bytes_per_pixel(colorSpace);

	struct overlay* overlay =
		(struct overlay*)malloc(sizeof(struct overlay));
	if (overlay == NULL)
		return NULL;

	memset(overlay, 0, sizeof(struct overlay));

	/*
	 * Gen 9+ stride alignment:
	 * - Linear: 64 bytes
	 * - Tiled X: 512 bytes
	 * - Tiled Y/Yf: 128 bytes
	 *
	 * We use linear for simplicity.
	 */
	overlay_buffer* buffer = &overlay->buffer;
	buffer->space = colorSpace;
	buffer->width = width;
	buffer->height = height;
	buffer->bytes_per_row = (width * bpp + 63) & ~63;

	status_t status = intel_allocate_memory(
		buffer->bytes_per_row * height,
		0,
		overlay->buffer_base);

	if (status != B_OK) {
		ERROR("Failed to allocate overlay buffer memory\n");
		free(overlay);
		return NULL;
	}

	overlay->buffer_offset = overlay->buffer_base
		- (addr_t)gInfo->shared_info->graphics_memory;

	buffer->buffer = (uint8*)overlay->buffer_base;
	buffer->buffer_dma = (uint8*)gInfo->shared_info->physical_graphics_memory
		+ overlay->buffer_offset;

	/* Use pipe 0, plane 1 (first sprite), scaler 0 */
	overlay->pipe = 0;
	overlay->plane = 1;
	overlay->scaler = 0;

	/* Pre-compute PLANE_CTL value */
	overlay->plane_ctl = PLANE_CTL_ENABLE
		| color_space_to_plane_ctl(colorSpace)
		| PLANE_CTL_TILED_LINEAR
		| PLANE_CTL_ALPHA_DISABLE;

	TRACE("Allocated overlay: base=0x%lx, offset=0x%x, stride=%d\n",
		overlay->buffer_base, overlay->buffer_offset, buffer->bytes_per_row);

	return buffer;
}


status_t
intel_release_overlay_buffer(const overlay_buffer* buffer)
{
	CALLED();

	struct overlay* overlay = (struct overlay*)buffer;

	if (gInfo->current_overlay == (struct overlay*)overlay) {
		plane_disable(overlay->pipe, overlay->plane);
		scaler_disable(overlay->pipe, overlay->scaler);
		gInfo->current_overlay = NULL;
	}

	intel_free_memory(overlay->buffer_base);
	free(overlay);

	return B_OK;
}


status_t
intel_get_overlay_constraints(const display_mode* mode,
	const overlay_buffer* buffer, overlay_constraints* constraints)
{
	CALLED();

	/*
	 * Gen 9+ Universal Plane constraints:
	 * Min size: 8x8
	 * Max size: 8192x4096 (SKL), 16384x16384 (ICL+)
	 * Scaling: 1/8x to 8x (with scaler)
	 */
	constraints->view.h_alignment = 0;
	constraints->view.v_alignment = 0;
	constraints->view.width_alignment =
		(buffer->space == B_YCbCr422) ? 1 : 0;
	constraints->view.height_alignment = 0;

	constraints->view.width.min = 8;
	constraints->view.height.min = 8;
	constraints->view.width.max = buffer->width;
	constraints->view.height.max = buffer->height;

	constraints->window.h_alignment = 0;
	constraints->window.v_alignment = 0;
	constraints->window.width_alignment = 0;
	constraints->window.height_alignment = 0;
	constraints->window.width.min = 8;
	constraints->window.width.max = mode->virtual_width;
	constraints->window.height.min = 8;
	constraints->window.height.max = mode->virtual_height;

	constraints->h_scale.min = 0.125f;
	constraints->h_scale.max = 8.0f;
	constraints->v_scale.min = 0.125f;
	constraints->v_scale.max = 8.0f;

	return B_OK;
}


overlay_token
intel_allocate_overlay(void)
{
	CALLED();

	if (atomic_or(&gInfo->shared_info->overlay_channel_used, 1) != 0)
		return NULL;

	return (overlay_token)++gInfo->shared_info->overlay_token;
}


status_t
intel_release_overlay(overlay_token overlayToken)
{
	CALLED();

	if (overlayToken != (overlay_token)gInfo->shared_info->overlay_token)
		return B_BAD_VALUE;

	atomic_and(&gInfo->shared_info->overlay_channel_used, 0);

	return B_OK;
}


status_t
intel_configure_overlay(overlay_token overlayToken,
	const overlay_buffer* buffer, const overlay_window* window,
	const overlay_view* view)
{
	CALLED();

	if (overlayToken != (overlay_token)gInfo->shared_info->overlay_token)
		return B_BAD_VALUE;

	struct overlay* overlay = (struct overlay*)buffer;

	/* Hide overlay if window/view is NULL */
	if (window == NULL || view == NULL) {
		if (overlay != NULL) {
			plane_disable(overlay->pipe, overlay->plane);
			scaler_disable(overlay->pipe, overlay->scaler);
		}
		gInfo->current_overlay = NULL;
		return B_OK;
	}

	int pipe = overlay->pipe;
	int plane = overlay->plane;

	/* Clip window to display bounds */
	int32 left = window->h_start;
	int32 top = window->v_start;
	int32 right = left + window->width;
	int32 bottom = top + window->height;

	int32 dispW = gInfo->shared_info->current_mode.timing.h_display;
	int32 dispH = gInfo->shared_info->current_mode.timing.v_display;

	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > dispW) right = dispW;
	if (bottom > dispH) bottom = dispH;

	if (left >= right || top >= bottom) {
		plane_disable(pipe, plane);
		scaler_disable(pipe, overlay->scaler);
		return B_OK;
	}

	uint32 dstW = right - left;
	uint32 dstH = bottom - top;

	/* Source dimensions */
	uint32 srcX = view->h_start;
	uint32 srcY = view->v_start;
	uint32 srcW = view->width;
	uint32 srcH = view->height;

	uint32 bpp = bytes_per_pixel(buffer->space);

	/* Calculate surface offset for source position */
	uint32 surfOffset = overlay->buffer_offset
		+ srcY * buffer->bytes_per_row
		+ srcX * bpp;

	/* Align to 4KB for PLANE_SURF */
	uint32 surfBase = surfOffset & PLANE_SURF_ADDR_MASK;

	/* Offset within the 4KB page */
	uint32 xOff = (surfOffset - surfBase) % buffer->bytes_per_row / bpp;
	uint32 yOff = (surfOffset - surfBase) / buffer->bytes_per_row;

	/* Configure scaler if scaling needed */
	bool needScaler = (srcW != dstW) || (srcH != dstH);
	if (needScaler) {
		configure_scaler(pipe, overlay->scaler, plane,
			srcW, srcH, dstW, dstH, left, top);
	} else {
		scaler_disable(pipe, overlay->scaler);
	}

	/* Build PLANE_CTL */
	uint32 planeCtl = overlay->plane_ctl;

	if (window->flags & B_OVERLAY_HORIZONTAL_MIRRORING)
		planeCtl |= PLANE_CTL_FLIP_HORIZONTAL;

	planeCtl |= PLANE_CTL_KEY_ENABLE_DEST;
	set_plane_color_key(pipe, plane, window);

	/* Write plane registers in correct order */

	/* Stride in 64-byte units */
	write32(PLANE_STRIDE(pipe, plane), buffer->bytes_per_row / 64);

	/* Position (only used if scaler not active) */
	if (!needScaler)
		write32(PLANE_POS(pipe, plane), (left << 16) | top);
	else
		write32(PLANE_POS(pipe, plane), 0);

	/* Size: (height-1) in [31:16], (width-1) in [15:0] */
	write32(PLANE_SIZE(pipe, plane), ((srcH - 1) << 16) | (srcW - 1));

	/* Offset within surface: y in [31:16], x in [15:0] */
	write32(PLANE_OFFSET_REG(pipe, plane), (yOff << 16) | xOff);

	/*
	 * Gen 11+ requires PLANE_COLOR_CTL
	 * Reference: i915 icl_plane_update_noarm()
	 */
	if (gpu_generation() >= 11) {
		uint32 colorCtl = PLANE_COLOR_CTL_ENABLE
			| PLANE_COLOR_ALPHA_DISABLE;
		write32(PLANE_COLOR_CTL(pipe, plane), colorCtl);
	}

	/* Control - must be written before SURF */
	write32(PLANE_CTL(pipe, plane), planeCtl);

	/* Surface address - writing this arms the update */
	write32(PLANE_SURF(pipe, plane),
		gInfo->shared_info->physical_graphics_memory + surfBase);

	gInfo->current_overlay = (struct overlay*)overlay;

	return B_OK;
}
