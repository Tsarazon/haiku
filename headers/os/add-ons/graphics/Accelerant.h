/*
 * Copyright 2009-2012, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCELERANT_H_
#define _ACCELERANT_H_


#include <GraphicsDefs.h>
#include <OS.h>


#if defined(__cplusplus)
	extern "C" {
#endif


#define B_ACCELERANT_ENTRY_POINT	"get_accelerant_hook"
#define B_ACCELERANT_VERSION		1


typedef void* (*GetAccelerantHook)(uint32, void*);

void* get_accelerant_hook(uint32 feature, void* data);


enum {
	/* initialization */
	B_INIT_ACCELERANT = 0,				/* required */
	B_ACCELERANT_CLONE_INFO_SIZE,		/* required */
	B_GET_ACCELERANT_CLONE_INFO,		/* required */
	B_CLONE_ACCELERANT,					/* required */
	B_UNINIT_ACCELERANT,				/* required */
	B_GET_ACCELERANT_DEVICE_INFO,		/* required */
	B_ACCELERANT_RETRACE_SEMAPHORE,		/* optional */

	/* mode configuration */
	B_ACCELERANT_MODE_COUNT = 0x100,	/* required */
	B_GET_MODE_LIST,					/* required */
	B_PROPOSE_DISPLAY_MODE,				/* optional */
	B_SET_DISPLAY_MODE,					/* required */
	B_GET_DISPLAY_MODE,					/* required */
	B_GET_FRAME_BUFFER_CONFIG,			/* required */
	B_GET_PIXEL_CLOCK_LIMITS,			/* required */
	B_GET_TIMING_CONSTRAINTS,			/* optional */
	B_MOVE_DISPLAY,						/* optional */
	B_SET_INDEXED_COLORS,				/* required if driver supports 8bit */
										/* indexed modes */
	B_DPMS_CAPABILITIES,				/* required if driver supports DPMS */
	B_DPMS_MODE,						/* required if driver supports DPMS */
	B_SET_DPMS_MODE,					/* required if driver supports DPMS */
	B_GET_PREFERRED_DISPLAY_MODE,		/* optional */
	B_GET_MONITOR_INFO,					/* optional */
	B_GET_EDID_INFO,					/* optional */
	B_SET_BRIGHTNESS,                   /* optional */
	B_GET_BRIGHTNESS,                   /* optional */

	/* cursor managment */
	B_MOVE_CURSOR = 0x200,				/* optional */
	B_SET_CURSOR_SHAPE,					/* optional */
	B_SHOW_CURSOR,						/* optional */
	B_SET_CURSOR_BITMAP,				/* optional */

	/* synchronization */
	B_ACCELERANT_ENGINE_COUNT = 0x300,	/* required */
	B_ACQUIRE_ENGINE,					/* required */
	B_RELEASE_ENGINE,					/* required */
	B_WAIT_ENGINE_IDLE,					/* required */
	B_GET_SYNC_TOKEN,					/* required */
	B_SYNC_TO_TOKEN,					/* required */

	/* 2D acceleration */
	B_SCREEN_TO_SCREEN_BLIT = 0x400,	/* optional */
	B_FILL_RECTANGLE,					/* optional */
	B_INVERT_RECTANGLE,					/* optional */
	B_FILL_SPAN,						/* optional */
	B_SCREEN_TO_SCREEN_TRANSPARENT_BLIT,	/* optional */
	B_SCREEN_TO_SCREEN_SCALED_FILTERED_BLIT,	/* optional.
		NOTE: source and dest may NOT overlap */

	/* 3D acceleration */
	B_ACCELERANT_PRIVATE_START = (int)0x80000000
};


typedef struct {
	uint32	version;					/* structure version number */
	char 	name[32];					/* a name the user will recognize */
										/* the device by */
	char	chipset[32];				/* the chipset used by the device */
	char	serial_no[32];				/* serial number for the device */
	uint32	memory;						/* amount of memory on the device, */
										/* in bytes */
	uint32	dac_speed;					/* nominal DAC speed, in MHz */
} accelerant_device_info;


typedef struct {
	uint32	pixel_clock;				/* kHz */
	uint16	h_display;					/* in pixels (not character clocks) */
	uint16	h_sync_start;
	uint16	h_sync_end;
	uint16	h_total;
	uint16	v_display;					/* in lines */
	uint16	v_sync_start;
	uint16	v_sync_end;
	uint16	v_total;
	uint32	flags;						/* sync polarity, etc. */
} display_timing;


typedef struct {
	display_timing	timing;				/* CTRC info */
	uint32			space;				/* pixel configuration */
	uint16			virtual_width;		/* in pixels */
	uint16			virtual_height;		/* in lines */
	uint16			h_display_start;	/* first displayed pixel in line */
	uint16			v_display_start;	/* first displayed line */
	uint32			flags;				/* mode flags (Some drivers use this */
										/* for dual head related options.) */
} display_mode;

typedef struct {
	void*	frame_buffer;				/* pointer to first byte of frame */
										/* buffer in virtual memory */

	void*	frame_buffer_dma;			/* pointer to first byte of frame */
										/* buffer in physical memory for DMA */

	uint32	bytes_per_row;				/* number of bytes in one */
										/* virtual_width line */
										/* not neccesarily the same as */
										/* virtual_width * byte_per_pixel */
} frame_buffer_config;


typedef struct {
	uint16	h_res;						/* minimum effective change in */
										/* horizontal pixels, usually 8 */

	uint16	h_sync_min;					/* min/max horizontal sync pulse */
										/* width in pixels, a multiple of */
										/* h_res */
	uint16	h_sync_max;
	uint16	h_blank_min;				/* min/max horizontal blank pulse */
										/* width in pixels, a multiple of */
										/* h_res */
	uint16	h_blank_max;
	uint16	v_res;						/* minimum effective change in */
										/* vertical lines, usually 1 */

	uint16	v_sync_min;					/* min/max vertical sync pulse */
										/* width in lines, a multiple of */
										/* v_res */
	uint16	v_sync_max;
	uint16	v_blank_min;				/* min/max vertical blank pulse */
										/* width in linex, a multiple of */
										/* v_res */
	uint16	v_blank_max;
} display_timing_constraints;


// WARNING: This is experimental new Haiku API
typedef struct {
	uint32	version;
	char	vendor[128];
	char	name[128];
	char	serial_number[128];
	uint32	product_id;
	struct {
		uint16	week;
		uint16	year;
	}		produced;
	float	width;
	float	height;
	uint32	min_horizontal_frequency;	/* in kHz */
	uint32	max_horizontal_frequency;
	uint32	min_vertical_frequency;		/* in Hz */
	uint32	max_vertical_frequency;
	uint32	max_pixel_clock;			/* in kHz */
} monitor_info;


/* mode flags */
enum {
	B_SCROLL			= 1 << 0,
	B_8_BIT_DAC			= 1 << 1,
	B_HARDWARE_CURSOR	= 1 << 2,
	B_PARALLEL_ACCESS	= 1 << 3,
	B_DPMS				= 1 << 4,
	B_IO_FB_NA			= 1 << 5,
	B_SUPPORTS_OVERLAYS	= 1U << 31	/* hardware video overlay capable */
};


/* power saver flags */
enum {
	B_DPMS_ON			= 1 << 0,
	B_DPMS_STAND_BY		= 1 << 1,
	B_DPMS_SUSPEND		= 1 << 2,
	B_DPMS_OFF			= 1 << 3
};


/* timing flags */
enum {
	B_BLANK_PEDESTAL	= 1 << 27,
	B_TIMING_INTERLACED	= 1 << 28,
	B_POSITIVE_HSYNC	= 1 << 29,
	B_POSITIVE_VSYNC	= 1 << 30,
	B_SYNC_ON_GREEN		= 1 << 31
};


typedef struct {
	uint16	src_left;					/* guaranteed constrained to */
										/* virtual width and height */
	uint16	src_top;
	uint16	dest_left;
	uint16	dest_top;
	uint16	width;						/* 0 to N, where zero means */
										/* one pixel, one means two pixels, */
										/* etc. */
	uint16	height;						/* 0 to M, where zero means one */
										/* line, one means two lines, etc. */
} blit_params;


typedef struct {
	uint16	src_left;					/* guaranteed constrained to */
										/* virtual width and height */
	uint16	src_top;
	uint16	src_width;					/* 0 to N, where zero means one */
										/* pixel, one means two pixels, */
										/* etc. */
	uint16	src_height;					/* 0 to M, where zero means one */
										/* line, one means two lines, etc. */
	uint16	dest_left;
	uint16	dest_top;
	uint16	dest_width;					/* 0 to N, where zero means one */
										/* pixel, one means two pixels, etc. */
	uint16	dest_height;				/* 0 to M, where zero means one */
										/* line, one means two lines, etc. */
} scaled_blit_params;


typedef struct {
	uint16	left;						/* guaranteed constrained to */
										/* virtual width and height */
	uint16	top;
	uint16	right;
	uint16	bottom;
} fill_rect_params;


typedef struct {
	uint32	engine_id;					/* 0 == no engine, 1,2,3 etc */
										/* individual engines */
	uint32	capability_mask;			/* features this engine supports */
	void*	opaque;						/* optional pointer to engine */
										/* private storage */
} engine_token;


enum {	/* engine capabilities */
	B_2D_ACCELERATION = 1 << 0,
	B_3D_ACCELERATION = 1 << 1
};


typedef struct {
	uint64	counter;					/* counts issued primatives */
	uint32	engine_id;					/* what engine the counter is for */
	char	opaque[12];					/* 12 bytes of private storage */
} sync_token;


/* Masks for color info */
/* B_CMAP8    - 0x000000ff */
/* B_RGB15/16 - 0x0000ffff */
/* B_RGB24    - 0x00ffffff */
/* B_RGB32    - 0xffffffff */

typedef status_t (*init_accelerant)(int fd);
typedef ssize_t (*accelerant_clone_info_size)(void);
typedef void (*get_accelerant_clone_info)(void* data);
typedef status_t (*clone_accelerant)(void* data);
typedef void (*uninit_accelerant)(void);
typedef status_t (*get_accelerant_device_info)(accelerant_device_info* adi);

typedef uint32 (*accelerant_mode_count)(void);
typedef status_t (*get_mode_list)(display_mode*);
typedef status_t (*propose_display_mode)(display_mode* target,
	display_mode* low, display_mode* high);
typedef status_t (*set_display_mode)(display_mode* modeToSet);
typedef status_t (*get_display_mode)(display_mode* currentMode);
typedef status_t (*get_frame_buffer_config)(frame_buffer_config* frameBuffer);
typedef status_t (*get_pixel_clock_limits)(display_mode* dm, uint32* low,
	uint32* high);
typedef status_t (*move_display_area)(uint16 hDisplayStart,
	uint16 vDisplayStart);
typedef status_t (*get_timing_constraints)(display_timing_constraints* dtc);
typedef void (*set_indexed_colors)(uint count, uint8 first,
	const uint8* colorData, uint32 flags);
typedef uint32 (*dpms_capabilities)(void);
typedef uint32 (*dpms_mode)(void);
typedef status_t (*set_dpms_mode)(uint32 dpms_flags);
typedef status_t (*get_preferred_display_mode)(display_mode* preferredMode);
typedef status_t (*get_monitor_info)(monitor_info* info);
typedef status_t (*get_edid_info)(void* info, uint32 size, uint32* _version);
typedef status_t (*set_brightness)(float brightness);
typedef status_t (*get_brightness)(float* brightness);

typedef sem_id (*accelerant_retrace_semaphore)(void);

typedef status_t (*set_cursor_shape)(uint16 width, uint16 height,
	uint16 hotX, uint16 hotY, const uint8* andMask, const uint8* xorMask);
typedef status_t (*set_cursor_bitmap)(uint16 width, uint16 height,
	uint16 hotX, uint16 hotY, color_space colorSpace, uint16 bytesPerRow,
	const uint8* bitmapData);
typedef void (*move_cursor)(uint16 x, uint16 y);
typedef void (*show_cursor)(bool isVisible);

typedef uint32 (*accelerant_engine_count)(void);
typedef status_t (*acquire_engine)(uint32 capabilities, uint32 maxWait,
	sync_token* st, engine_token** et);
typedef status_t (*release_engine)(engine_token* et, sync_token* st);
typedef void (*wait_engine_idle)(void);
typedef status_t (*get_sync_token)(engine_token* et, sync_token* st);
typedef status_t (*sync_to_token)(sync_token* st);

typedef void (*screen_to_screen_blit)(engine_token* et, blit_params* list,
	uint32 count);
typedef void (*fill_rectangle)(engine_token* et, uint32 color,
	fill_rect_params* list, uint32 count);
typedef void (*invert_rectangle)(engine_token* et, fill_rect_params* list,
	uint32 count);
typedef void (*screen_to_screen_transparent_blit)(engine_token* et,
	uint32 transparent_color, blit_params* list, uint32 count);
typedef void (*screen_to_screen_scaled_filtered_blit)(engine_token* et,
	scaled_blit_params* list, uint32 count);

typedef void (*fill_span)(engine_token* et, uint32 color, uint16* list,
	uint32 count);
/*
	The uint16* list points to a list of tripples:
		list[N+0]  Y co-ordinate of span
		list[N+1]  Left x co-ordinate of span
		list[N+2]  Right x co-ordinate of span
	where N is in the range 0 to count-1.
*/


/* ======================================================================
   Hardware video overlays
   (Originally in <video_overlay.h>; merged here per the long-standing
   "will eventually be merged into Accelerant.h" note from Be Inc.)
   ====================================================================== */

/* Overlay feature codes (used with get_accelerant_hook()). */
enum {
	B_OVERLAY_COUNT = 0x08000000,
	B_OVERLAY_SUPPORTED_SPACES,
	B_OVERLAY_SUPPORTED_FEATURES,
	B_ALLOCATE_OVERLAY_BUFFER,
	B_RELEASE_OVERLAY_BUFFER,
	B_GET_OVERLAY_CONSTRAINTS,
	B_ALLOCATE_OVERLAY,
	B_RELEASE_OVERLAY,
	B_CONFIGURE_OVERLAY
};

typedef struct {
	uint32	space;	/* color_space of buffer */
	uint16	width;	/* width in pixels */
	uint16	height;	/* height in lines */
	uint32	bytes_per_row;		/* number of bytes in one line */
	void	*buffer;		/* pointer to first byte of overlay buffer in virtual memory */
	void	*buffer_dma;	/* pointer to first byte of overlay buffer in physical memory for DMA */
} overlay_buffer;

typedef struct {
	uint16	h_start;
	uint16	v_start;
	uint16	width;
	uint16	height;
} overlay_view;

/* Overlay window flags (overlay_window::flags). */
enum {
	B_OVERLAY_COLOR_KEY = 1 << 0,
	B_OVERLAY_CHROMA_KEY = 1 << 1,
	B_OVERLAY_HORIZONTAL_FILTERING = 1 << 2,
	B_OVERLAY_VERTICAL_FILTERING = 1 << 3,
	B_OVERLAY_HORIZONTAL_MIRRORING = 1 << 4,
	B_OVERLAY_KEYING_USES_ALPHA = 1 << 5
};

typedef struct {
	uint8	value;	/* if DAC color component of graphic pixel & mask == value, */
	uint8	mask;	/*  then show overlay pixel, else show graphic pixel */
} overlay_key_color;

typedef struct {
	int16	h_start;	/* Top left un-clipped corner of the window in the virtual display */
	int16	v_start;	/* Note that these are _signed_ values */
	uint16	width;		/* un-clipped width of the overlay window */
	uint16	height;		/* un-clipped height of the overlay window */

	uint16	offset_left;	/* that portion of the overlay_window which is actually displayed */
	uint16	offset_top;		/* ie, the first line displayed is v_start + offset_top           */
	uint16	offset_right;	/*     and the first pixel displayed is h_start + offset_left     */
	uint16	offset_bottom;

	overlay_key_color	red;	/* when using color keying, all components must match */
	overlay_key_color	green;
	overlay_key_color	blue;
	overlay_key_color	alpha;
	uint32	flags;	/* which features should be enabled.  See enum above. */
} overlay_window;

typedef struct {
	uint16	min;
	uint16	max;
} overlay_uint16_minmax;

typedef struct {
	float	min;
	float	max;
} overlay_float_minmax;

typedef struct {
	uint16	h_alignment;	/* alignments: a 1 bit set in every bit which must be zero */
	uint16	v_alignment;
	uint16	width_alignment;
	uint16	height_alignment;
	overlay_uint16_minmax	width;	/* min and max sizes in each axis */
	overlay_uint16_minmax	height;
} overlay_limits;

typedef struct {
	overlay_limits	view;
	overlay_limits	window;
	overlay_float_minmax	h_scale;
	overlay_float_minmax	v_scale;
} overlay_constraints;

typedef void * overlay_token;

typedef uint32 (*overlay_count)(const display_mode *dm);
typedef const uint32 *(*overlay_supported_spaces)(const display_mode *dm);
typedef uint32 (*overlay_supported_features)(uint32 a_color_space);
typedef const overlay_buffer *(*allocate_overlay_buffer)(color_space cs,
	uint16 width, uint16 height);
typedef status_t (*release_overlay_buffer)(const overlay_buffer *ob);
typedef status_t (*get_overlay_constraints)(const display_mode *dm,
	const overlay_buffer *ob, overlay_constraints *oc);
typedef overlay_token (*allocate_overlay)(void);
typedef status_t (*release_overlay)(overlay_token ot);
typedef status_t (*configure_overlay)(overlay_token ot, const overlay_buffer *ob,
	const overlay_window *ow, const overlay_view *ov);


#if defined(__cplusplus)
}
#endif

#endif
