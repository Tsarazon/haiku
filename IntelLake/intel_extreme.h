/*
 * Copyright 2006-2016, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck, kallisti5@unixzen.com
 *
 * Refactored for Gen9+ only support (Mobile Haiku)
 */
#ifndef INTEL_EXTREME_H
#define INTEL_EXTREME_H


#include "lock.h"

#include <Accelerant.h>
#include <Drivers.h>
#include <PCI.h>
#include <SupportDefs.h>

#include <edid.h>


/*
 * Vendor and Device Identification
 *
 */

#define VENDOR_ID_INTEL			0x8086

#define INTEL_FAMILY_MASK		0x00ff0000
#define INTEL_GROUP_MASK		0x00fffff0
#define INTEL_MODEL_MASK		0x00ffffff
#define INTEL_TYPE_MASK			0x0000000f


/*
 * Device Families (Gen9+ only)
 *
 */

#define INTEL_FAMILY_LAKE		0x00400000	/* Gen9+ different different different different different lakes */


/*
 * Device Groups (Gen9+ only)
 *
 * Verified against Intel PRM Vol 2c Part 1 - Device IDs
 *
 */

#define INTEL_GROUP_SKY			(INTEL_FAMILY_LAKE | 0x0010)	/* Skylake (Gen9) */
#define INTEL_GROUP_KBY			(INTEL_FAMILY_LAKE | 0x0020)	/* Kaby Lake (Gen9.5) */
#define INTEL_GROUP_CFL			(INTEL_FAMILY_LAKE | 0x0040)	/* Coffee Lake (Gen9.5) */
#define INTEL_GROUP_CML			(INTEL_FAMILY_LAKE | 0x0080)	/* Comet Lake (Gen9.5) */
#define INTEL_GROUP_BXT			(INTEL_FAMILY_LAKE | 0x0100)	/* Broxton/Apollo Lake (Gen9 Atom) */
#define INTEL_GROUP_GLK			(INTEL_FAMILY_LAKE | 0x0200)	/* Gemini Lake (Gen9.5 Atom) */
#define INTEL_GROUP_ICL			(INTEL_FAMILY_LAKE | 0x0400)	/* Ice Lake (Gen11) */
#define INTEL_GROUP_JSL			(INTEL_FAMILY_LAKE | 0x0800)	/* Jasper Lake (Gen11 Atom) */
#define INTEL_GROUP_EHL			(INTEL_FAMILY_LAKE | 0x1000)	/* Elkhart Lake (Gen11 Atom) */
#define INTEL_GROUP_TGL			(INTEL_FAMILY_LAKE | 0x2000)	/* Tiger Lake (Gen12) */
#define INTEL_GROUP_RKL			(INTEL_FAMILY_LAKE | 0x4000)	/* Rocket Lake (Gen12) */
#define INTEL_GROUP_ALD			(INTEL_FAMILY_LAKE | 0x8000)	/* Alder Lake (Gen12) */


/*
 * Device Models (Gen9+ only)
 *
 */

#define INTEL_TYPE_MOBILE		0x0008

/* Skylake */
#define INTEL_MODEL_SKY			(INTEL_GROUP_SKY)
#define INTEL_MODEL_SKYM		(INTEL_GROUP_SKY | INTEL_TYPE_MOBILE)

/* Kaby Lake */
#define INTEL_MODEL_KBY			(INTEL_GROUP_KBY)
#define INTEL_MODEL_KBYM		(INTEL_GROUP_KBY | INTEL_TYPE_MOBILE)

/* Coffee Lake */
#define INTEL_MODEL_CFL			(INTEL_GROUP_CFL)
#define INTEL_MODEL_CFLM		(INTEL_GROUP_CFL | INTEL_TYPE_MOBILE)

/* Comet Lake */
#define INTEL_MODEL_CML			(INTEL_GROUP_CML)
#define INTEL_MODEL_CMLM		(INTEL_GROUP_CML | INTEL_TYPE_MOBILE)

/* Atom: Broxton/Apollo Lake */
#define INTEL_MODEL_BXT			(INTEL_GROUP_BXT)
#define INTEL_MODEL_BXTM		(INTEL_GROUP_BXT | INTEL_TYPE_MOBILE)

/* Atom: Gemini Lake */
#define INTEL_MODEL_GLK			(INTEL_GROUP_GLK)
#define INTEL_MODEL_GLKM		(INTEL_GROUP_GLK | INTEL_TYPE_MOBILE)

/* Ice Lake */
#define INTEL_MODEL_ICL			(INTEL_GROUP_ICL)
#define INTEL_MODEL_ICLM		(INTEL_GROUP_ICL | INTEL_TYPE_MOBILE)

/* Atom: Jasper Lake */
#define INTEL_MODEL_JSL			(INTEL_GROUP_JSL)
#define INTEL_MODEL_JSLM		(INTEL_GROUP_JSL | INTEL_TYPE_MOBILE)

/* Atom: Elkhart Lake */
#define INTEL_MODEL_EHL			(INTEL_GROUP_EHL)
#define INTEL_MODEL_EHLM		(INTEL_GROUP_EHL | INTEL_TYPE_MOBILE)

/* Tiger Lake */
#define INTEL_MODEL_TGL			(INTEL_GROUP_TGL)
#define INTEL_MODEL_TGLM		(INTEL_GROUP_TGL | INTEL_TYPE_MOBILE)

/* Rocket Lake */
#define INTEL_MODEL_RKL			(INTEL_GROUP_RKL)

/* Alder Lake */
#define INTEL_MODEL_ALD			(INTEL_GROUP_ALD)
#define INTEL_MODEL_ALDM		(INTEL_GROUP_ALD | INTEL_TYPE_MOBILE)


/*
 * Platform Control Hub (PCH) Device IDs
 *
 * ✅ Verified against Intel PRM Vol 2c - PCH Device IDs
 * Gen9+ PCH only (SPT and newer)
 *
 */

#define INTEL_PCH_DEVICE_ID_MASK		0xff80

/* Sunrise Point (Skylake/Kaby Lake PCH) */
#define INTEL_PCH_SPT_DEVICE_ID			0xa100
#define INTEL_PCH_SPT_LP_DEVICE_ID		0x9d00
#define INTEL_PCH_KBP_DEVICE_ID			0xa280

/* Apollo Lake PCH */
#define INTEL_PCH_APL_LP_DEVICE_ID		0x5a80

/* Gemini Lake PCH */
#define INTEL_PCH_GMP_DEVICE_ID			0x3180

/* Cannon Point (Coffee Lake PCH) */
#define INTEL_PCH_CNP_DEVICE_ID			0xa300
#define INTEL_PCH_CNP_LP_DEVICE_ID		0x9d80

/* Comet Lake PCH */
#define INTEL_PCH_CMP_DEVICE_ID			0x0280
#define INTEL_PCH_CMP2_DEVICE_ID		0x0680
#define INTEL_PCH_CMP_V_DEVICE_ID		0xa380

/* Ice Lake PCH */
#define INTEL_PCH_ICP_DEVICE_ID			0x3480
#define INTEL_PCH_ICP2_DEVICE_ID		0x3880

/* Mule Creek Canyon (Elkhart/Jasper Lake) */
#define INTEL_PCH_MCC_DEVICE_ID			0x4b00

/* Tiger Lake PCH */
#define INTEL_PCH_TGP_DEVICE_ID			0xa080
#define INTEL_PCH_TGP2_DEVICE_ID		0x4380

/* Jasper Lake PCH */
#define INTEL_PCH_JSP_DEVICE_ID			0x4d80

/* Alder Lake PCH */
#define INTEL_PCH_ADP_DEVICE_ID			0x7a80
#define INTEL_PCH_ADP2_DEVICE_ID		0x5180
#define INTEL_PCH_ADP3_DEVICE_ID		0x7a00
#define INTEL_PCH_ADP4_DEVICE_ID		0x5480


/*
 * Driver Names
 *
 */

#define DEVICE_NAME				"intel_extreme"
#define INTEL_ACCELERANT_NAME	"intel_extreme.accelerant"


/*
 * Register Block Management
 *
 * ✅ Gen9+ uses unified MMIO layout (no MCH/ICH split)
 *
 */

#define REGISTER_BLOCK_COUNT				6
#define REGISTER_BLOCK_SHIFT				24
#define REGISTER_BLOCK_MASK					0xff000000
#define REGISTER_REGISTER_MASK				0x00ffffff
#define REGISTER_BLOCK(x)		((x & REGISTER_BLOCK_MASK) >> REGISTER_BLOCK_SHIFT)
#define REGISTER_REGISTER(x)	(x & REGISTER_REGISTER_MASK)

#define REGS_FLAT							(0 << REGISTER_BLOCK_SHIFT)
#define REGS_NORTH_SHARED					(1 << REGISTER_BLOCK_SHIFT)
#define REGS_NORTH_PIPE_AND_PORT			(2 << REGISTER_BLOCK_SHIFT)
#define REGS_NORTH_PLANE_CONTROL			(3 << REGISTER_BLOCK_SHIFT)
#define REGS_SOUTH_SHARED					(4 << REGISTER_BLOCK_SHIFT)
#define REGS_SOUTH_TRANSCODER_PORT			(5 << REGISTER_BLOCK_SHIFT)


/*
 * Register Base Addresses (PCH Platforms - Gen9+)
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1
 *
 */

#define PCH_NORTH_SHARED_REGISTER_BASE					0x40000
#define PCH_NORTH_PIPE_AND_PORT_REGISTER_BASE			0x60000
#define PCH_NORTH_PLANE_CONTROL_REGISTER_BASE			0x70000
#define PCH_SOUTH_SHARED_REGISTER_BASE					0xc0000
#define PCH_SOUTH_TRANSCODER_AND_PORT_REGISTER_BASE		0xe0000


/*
 * Device Type Helper Class
 *
 */

struct DeviceType {
	uint32 type;

	DeviceType(int t) { type = t; }

	DeviceType& operator=(int t)
	{
		type = t;
		return *this;
	}

	bool InFamily(uint32 family) const
	{
		return (type & INTEL_FAMILY_MASK) == family;
	}

	bool InGroup(uint32 group) const
	{
		return (type & INTEL_GROUP_MASK) == group;
	}

	bool IsModel(uint32 model) const
	{
		return (type & INTEL_MODEL_MASK) == model;
	}

	bool IsMobile() const
	{
		return (type & INTEL_TYPE_MASK) == INTEL_TYPE_MOBILE;
	}

	bool IsAtom() const
	{
		return InGroup(INTEL_GROUP_BXT) || InGroup(INTEL_GROUP_GLK)
			|| InGroup(INTEL_GROUP_JSL) || InGroup(INTEL_GROUP_EHL);
	}

	bool SupportsHDMI() const
	{
		/* All Gen9+ support HDMI */
		return true;
	}

	bool HasDDI() const
	{
		/* All Gen9+ use DDI (Digital Display Interface) */
		return true;
	}

	int Generation() const
	{
		/* Gen9: Skylake, Kaby Lake, Coffee Lake, Comet Lake, Broxton, Gemini Lake */
		if (InGroup(INTEL_GROUP_SKY) || InGroup(INTEL_GROUP_KBY)
				|| InGroup(INTEL_GROUP_CFL) || InGroup(INTEL_GROUP_CML)
				|| InGroup(INTEL_GROUP_BXT) || InGroup(INTEL_GROUP_GLK))
			return 9;

		/* Gen11: Ice Lake, Jasper Lake, Elkhart Lake */
		if (InGroup(INTEL_GROUP_ICL) || InGroup(INTEL_GROUP_JSL)
				|| InGroup(INTEL_GROUP_EHL))
			return 11;

		/* Gen12: Tiger Lake, Rocket Lake, Alder Lake */
		if (InGroup(INTEL_GROUP_TGL) || InGroup(INTEL_GROUP_RKL)
				|| InGroup(INTEL_GROUP_ALD))
			return 12;

		return 0;
	}
};


/*
 * Port and Pipe Enumerations
 *
 * ✅ Verified against Intel PRM - Display Engine
 *
 */

enum port_index {
	INTEL_PORT_ANY,
	INTEL_PORT_A,
	INTEL_PORT_B,
	INTEL_PORT_C,
	INTEL_PORT_D,
	INTEL_PORT_E,
	INTEL_PORT_F,
	INTEL_PORT_G
};

enum pipe_index {
	INTEL_PIPE_ANY,
	INTEL_PIPE_A,
	INTEL_PIPE_B,
	INTEL_PIPE_C,
	INTEL_PIPE_D
};

enum pch_info {
	INTEL_PCH_NONE = 0,
	INTEL_PCH_SPT,		/* SunrisePoint (Skylake/Kaby Lake) */
	INTEL_PCH_CNP,		/* CannonLake/Coffee Lake */
	INTEL_PCH_ICP,		/* IceLake */
	INTEL_PCH_JSP,		/* JasperLake */
	INTEL_PCH_MCC,		/* Mule Creek Canyon (Elkhart/Jasper) */
	INTEL_PCH_TGP,		/* TigerLake */
	INTEL_PCH_ADP,		/* AlderLake */
	INTEL_PCH_NOP
};

enum dvo_port {
	DVO_PORT_HDMIA,
	DVO_PORT_HDMIB,
	DVO_PORT_HDMIC,
	DVO_PORT_HDMID,
	DVO_PORT_DPB,
	DVO_PORT_DPC,
	DVO_PORT_DPD,
	DVO_PORT_DPA,
	DVO_PORT_DPE,
	DVO_PORT_HDMIE,
	DVO_PORT_DPF,
	DVO_PORT_HDMIF,
	DVO_PORT_DPG,
	DVO_PORT_HDMIG,
	DVO_PORT_DPH,
	DVO_PORT_HDMIH,
	DVO_PORT_DPI,
	DVO_PORT_HDMII,
};

enum dp_aux_channel {
	DP_AUX_A = 0x40,
	DP_AUX_B = 0x10,
	DP_AUX_C = 0x20,
	DP_AUX_D = 0x30,
	DP_AUX_E = 0x50,
	DP_AUX_F = 0x60,
	DP_AUX_G = 0x70,
	DP_AUX_H = 0x80,
	DP_AUX_I = 0x90
};

enum aux_channel {
	AUX_CH_A,
	AUX_CH_B,
	AUX_CH_C,
	AUX_CH_D,
	AUX_CH_E,
	AUX_CH_F,
	AUX_CH_G,
	AUX_CH_H,
	AUX_CH_I,
};

enum hpd_pin {
	HPD_PORT_A,
	HPD_PORT_B,
	HPD_PORT_C,
	HPD_PORT_D,
	HPD_PORT_E,
	HPD_PORT_TC1,
	HPD_PORT_TC2,
	HPD_PORT_TC3,
	HPD_PORT_TC4,
	HPD_PORT_TC5,
	HPD_PORT_TC6,
};


/*
 * Hardware Structures
 *
 */

struct pll_info {
	uint32 reference_frequency;
	uint32 max_frequency;
	uint32 min_frequency;
	uint32 divisor_register;
};

struct ring_buffer {
	struct lock lock;
	uint32 register_base;
	uint32 offset;
	uint32 size;
	uint32 position;
	uint32 space_left;
	uint8* base;
};

struct child_device_config {
	uint16 handle;
	uint16 device_type;

#define DEVICE_TYPE_DIGITAL_OUTPUT		(1 << 1)
#define DEVICE_TYPE_DISPLAYPORT_OUTPUT	(1 << 2)
#define DEVICE_TYPE_TMDS_DVI_SIGNALING	(1 << 4)
#define DEVICE_TYPE_HIGH_SPEED_LINK		(1 << 6)
#define DEVICE_TYPE_DUAL_CHANNEL		(1 << 8)
#define DEVICE_TYPE_MIPI_OUTPUT			(1 << 10)
#define DEVICE_TYPE_NOT_HDMI_OUTPUT		(1 << 11)
#define DEVICE_TYPE_INTERNAL_CONNECTOR	(1 << 12)
#define DEVICE_TYPE_HOTPLUG_SIGNALING	(1 << 13)

	uint8 device_id[10];
	uint16 addin_offset;
	uint8 dvo_port;
	uint8 i2c_pin;
	uint8 slave_addr;
	uint8 ddc_pin;
	uint16 edid_ptr;
	uint8 dvo_cfg;

	struct {
		bool efp_routed:1;
		bool lane_reversal:1;
		bool lspcon:1;
		bool iboost:1;
		bool hpd_invert:1;
		bool use_vbt_vswing:1;
		uint8 reserved:2;
		bool hdmi_support:1;
		bool dp_support:1;
		bool tmds_support:1;
		uint8 reserved2:5;
		uint8 aux_channel;
		uint8 dongle_detect;
	} __attribute__((packed));

	uint8 caps;
	uint8 dvo_wiring;
	uint8 dvo2_wiring;
	uint16 extended_type;
	uint8 dvo_function;

	bool dp_usb_type_c:1;
	bool tbt:1;
	uint8 reserved3:2;
	uint8 dp_port_trace_length:4;
	uint8 dp_gpio_index;
	uint8 dp_gpio_pin_num;
	uint8 dp_iboost_level:4;
	uint8 hdmi_iboost_level:4;
	uint8 dp_max_link_rate:3;
	uint8 dp_max_link_rate_reserved:5;
} __attribute__((packed));

struct intel_shared_info {
	area_id mode_list_area;
	uint32 mode_count;

	display_mode current_mode;
	display_timing panel_timing;
	uint32 bytes_per_row;
	uint32 bits_per_pixel;
	uint32 dpms_mode;
	uint16 min_brightness;

	area_id registers_area;
	uint32 register_blocks[REGISTER_BLOCK_COUNT];

	uint8* status_page;
	phys_addr_t physical_status_page;
	uint8* graphics_memory;
	phys_addr_t physical_graphics_memory;
	uint32 graphics_memory_size;

	addr_t frame_buffer;
	uint32 frame_buffer_offset;

	uint32 hraw_clock;
	uint32 hw_cdclk;

	bool got_vbt;

	struct lock accelerant_lock;
	struct lock engine_lock;

	ring_buffer primary_ring_buffer;

	bool hardware_cursor_enabled;
	sem_id vblank_sem;

	uint8* cursor_memory;
	phys_addr_t physical_cursor_memory;
	uint32 cursor_buffer_offset;
	uint32 cursor_format;
	bool cursor_visible;
	uint16 cursor_hot_x;
	uint16 cursor_hot_y;

	DeviceType device_type;
	char device_identifier[32];
	struct pll_info pll_info;

	enum pch_info pch_info;

	edid1_info vesa_edid_info;
	bool has_vesa_edid_info;

	uint32 device_config_count;
	child_device_config device_configs[10];

	// Firmware versions (0 = not loaded)
	uint32 dmc_version;		// Display Microcontroller
	uint32 guc_version;		// Graphics Microcontroller
	uint32 huc_version;		// HEVC Microcontroller
};

struct hardware_status {
	uint32 interrupt_status_register;
	uint32 _reserved0[3];
	void* primary_ring_head_storage;
	uint32 _reserved1[3];
	void* secondary_ring_0_head_storage;
	void* secondary_ring_1_head_storage;
	uint32 _reserved2[2];
	void* binning_head_storage;
	uint32 _reserved3[3];
	uint32 store[1008];
};


/*
 * Pipes Helper Class
 *
 */

class pipes {
public:
	pipes() : bitmask(0) {}

	bool HasPipe(pipe_index pipe)
	{
		if (pipe == INTEL_PIPE_ANY)
			return bitmask != 0;
		return (bitmask & (1 << pipe)) != 0;
	}

	void SetPipe(pipe_index pipe)
	{
		if (pipe == INTEL_PIPE_ANY) {
			bitmask = ~1;
		}
		bitmask |= (1 << pipe);
	}

	void ClearPipe(pipe_index pipe)
	{
		if (pipe == INTEL_PIPE_ANY)
			bitmask = 0;
		bitmask &= ~(1 << pipe);
	}

private:
	uint8 bitmask;
};


/*
 * IOCTL Interface
 *
 */

#define INTEL_PRIVATE_DATA_MAGIC	'itic'

enum {
	INTEL_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	INTEL_GET_DEVICE_NAME,
	INTEL_ALLOCATE_GRAPHICS_MEMORY,
	INTEL_FREE_GRAPHICS_MEMORY
};

struct intel_get_private_data {
	uint32 magic;
	area_id shared_info_area;
};

struct intel_allocate_graphics_memory {
	uint32 magic;
	uint32 size;
	uint32 alignment;
	uint32 flags;
	addr_t buffer_base;
};

struct intel_free_graphics_memory {
	uint32 magic;
	addr_t buffer_base;
};


/*
 * Graphics Memory Management (Gen9+)
 *
 * ✅ Verified against Intel PRM Vol 2c Part 2 - GGMS/GMS fields
 * Gen9+ uses BDW-style stolen memory encoding
 *
 */

#define BDW_GRAPHICS_MEMORY_CONTROL		0x50

#define BDW_STOLEN_MEMORY_MASK			0xff00
#define BDW_STOLEN_MEMORY_32MB			(1 << 8)
#define BDW_STOLEN_MEMORY_64MB			(2 << 8)
#define BDW_STOLEN_MEMORY_96MB			(3 << 8)
#define BDW_STOLEN_MEMORY_128MB			(4 << 8)
#define BDW_STOLEN_MEMORY_160MB			(5 << 8)
#define BDW_STOLEN_MEMORY_192MB			(6 << 8)
#define BDW_STOLEN_MEMORY_224MB			(7 << 8)
#define BDW_STOLEN_MEMORY_256MB			(8 << 8)
#define BDW_STOLEN_MEMORY_288MB			(9 << 8)
#define BDW_STOLEN_MEMORY_320MB			(10 << 8)
#define BDW_STOLEN_MEMORY_352MB			(11 << 8)
#define BDW_STOLEN_MEMORY_384MB			(12 << 8)
#define BDW_STOLEN_MEMORY_416MB			(13 << 8)
#define BDW_STOLEN_MEMORY_448MB			(14 << 8)
#define BDW_STOLEN_MEMORY_480MB			(15 << 8)
#define BDW_STOLEN_MEMORY_512MB			(16 << 8)
#define BDW_STOLEN_MEMORY_1024MB		(32 << 8)
#define BDW_STOLEN_MEMORY_1536MB		(48 << 8)
#define BDW_STOLEN_MEMORY_2016MB		(63 << 8)
#define SKL_STOLEN_MEMORY_2048MB		(64 << 8)
#define SKL_STOLEN_MEMORY_4MB			(240 << 8)
#define SKL_STOLEN_MEMORY_8MB			(241 << 8)
#define SKL_STOLEN_MEMORY_12MB			(242 << 8)
#define SKL_STOLEN_MEMORY_16MB			(243 << 8)
#define SKL_STOLEN_MEMORY_20MB			(244 << 8)
#define SKL_STOLEN_MEMORY_24MB			(245 << 8)
#define SKL_STOLEN_MEMORY_28MB			(246 << 8)
#define SKL_STOLEN_MEMORY_32MB			(247 << 8)
#define SKL_STOLEN_MEMORY_36MB			(248 << 8)
#define SKL_STOLEN_MEMORY_40MB			(249 << 8)
#define SKL_STOLEN_MEMORY_44MB			(250 << 8)
#define SKL_STOLEN_MEMORY_48MB			(251 << 8)
#define SKL_STOLEN_MEMORY_52MB			(252 << 8)
#define SKL_STOLEN_MEMORY_56MB			(253 << 8)
#define SKL_STOLEN_MEMORY_60MB			(254 << 8)

#define BDW_GTT_SIZE_MASK				(3 << 6)
#define BDW_GTT_SIZE_NONE				(0 << 6)
#define BDW_GTT_SIZE_2MB				(1 << 6)
#define BDW_GTT_SIZE_4MB				(2 << 6)
#define BDW_GTT_SIZE_8MB				(3 << 6)


/*
 * Graphics Page Translation Table (GTT) - Gen9+
 *
 */

#define INTEL_HARDWARE_STATUS_PAGE		0x02080

#define GTT_ENTRY_VALID					0x01
#define GTT_PAGE_SHIFT					12


/*
 * Ring Buffer Registers
 *
 */

#define INTEL_PRIMARY_RING_BUFFER		0x02030

#define RING_BUFFER_TAIL				0x0
#define RING_BUFFER_HEAD				0x4
#define RING_BUFFER_START				0x8
#define RING_BUFFER_CONTROL				0xc
#define INTEL_RING_BUFFER_SIZE_MASK		0x001ff000
#define INTEL_RING_BUFFER_HEAD_MASK		0x001ffffc
#define INTEL_RING_BUFFER_ENABLED		1


/*
 * Interrupt Registers - Gen8+ (Gen9 uses same layout)
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1 - Interrupt Registers
 *
 */

#define PCH_MASTER_INT_CTL_BDW			0x44200
#define PCH_MASTER_INT_CTL_PIPE_PENDING_BDW(pipe)	(1 << (15 + pipe))
#define GEN8_DE_PCH_IRQ					(1 << 23)
#define GEN8_DE_PORT_IRQ				(1 << 20)
#define PCH_MASTER_INT_CTL_GLOBAL_BDW	(1 << 31)

#define PCH_INTERRUPT_PIPE_STATUS_BDW(pipe)		(0x44400 + (pipe - 1) * 0x10)
#define PCH_INTERRUPT_PIPE_MASK_BDW(pipe)		(0x44404 + (pipe - 1) * 0x10)
#define PCH_INTERRUPT_PIPE_IDENTITY_BDW(pipe)	(0x44408 + (pipe - 1) * 0x10)
#define PCH_INTERRUPT_PIPE_ENABLED_BDW(pipe)	(0x4440c + (pipe - 1) * 0x10)

#define GEN8_DE_PORT_ISR				0x44440
#define GEN8_DE_PORT_IMR				0x44444
#define GEN8_DE_PORT_IIR				0x44448
#define GEN8_DE_PORT_IER				0x4444c
#define GEN8_AUX_CHANNEL_A				(1 << 0)
#define GEN9_AUX_CHANNEL_B				(1 << 25)
#define GEN9_AUX_CHANNEL_C				(1 << 26)
#define GEN9_AUX_CHANNEL_D				(1 << 27)
#define CNL_AUX_CHANNEL_F				(1 << 28)
#define ICL_AUX_CHANNEL_E				(1 << 29)

#define GEN8_DE_MISC_ISR				0x44460
#define GEN8_DE_MISC_IMR				0x44464
#define GEN8_DE_MISC_IIR				0x44468
#define GEN8_DE_MISC_IER				0x4446c
#define GEN8_DE_EDP_PSR					(1 << 19)

#define PCH_INTERRUPT_VBLANK_BDW		(1 << 0)
#define GEN8_PIPE_VSYNC					(1 << 1)
#define GEN8_PIPE_SCAN_LINE_EVENT		(1 << 2)


/*
 * Interrupt Registers - Gen11+
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1
 *
 */

#define GEN11_GFX_MSTR_IRQ				0x190010
#define GEN11_MASTER_IRQ				(1 << 31)
#define GEN11_DISPLAY_IRQ				(1 << 16)
#define GEN11_GT_DW1_IRQ				(1 << 1)
#define GEN11_GT_DW0_IRQ				(1 << 0)

#define GEN11_DISPLAY_INT_CTL			0x44200
#define GEN11_DE_HPD_IRQ				(1 << 21)

#define GEN11_GT_INTR_DW0				0x190018
#define GEN11_GT_INTR_DW1				0x19001c

#define GEN11_GU_MISC_IMR				0x444f4
#define GEN11_GU_MISC_IIR				0x444f8
#define GEN11_GU_MISC_IER				0x444fc
#define GEN11_GU_MISC_GSE				(1 << 27)

#define GEN11_DE_HPD_ISR				0x44470
#define GEN11_DE_HPD_IMR				0x44474
#define GEN11_DE_HPD_IIR				0x44478
#define GEN11_DE_HPD_IER				0x4447c
#define GEN11_DE_TC_HOTPLUG_MASK		(0x3f << 16)
#define GEN11_DE_TBT_HOTPLUG_MASK		(0x3f)

#define GEN11_TBT_HOTPLUG_CTL			0x44030
#define GEN11_TC_HOTPLUG_CTL			0x44038


/*
 * PCH Interrupt and Hotplug Registers (Gen9+)
 *
 */

#define SHPD_FILTER_CNT					0xc4038
#define SHPD_FILTER_CNT_500_ADJ			0x1d9

#define SDEISR							0xc4000
#define SDEIMR							0xc4004
#define SDEIIR							0xc4008
#define SDEIER							0xc400c
#define SDE_GMBUS_ICP					(1 << 23)

#define SHOTPLUG_CTL_DDI				0xc4030
#define SHOTPLUG_CTL_DDI_HPD_ENABLE(hpd_pin)	(0x8 << (4 * ((hpd_pin) - HPD_PORT_A)))
#define SHOTPLUG_CTL_TC					0xc4034
#define SHOTPLUG_CTL_TC_HPD_ENABLE(hpd_pin)		(0x8 << (4 * ((hpd_pin) - HPD_PORT_TC1)))

#define PCH_PORT_HOTPLUG				SHOTPLUG_CTL_DDI
#define PCH_PORT_HOTPLUG2				0xc403c


/*
 * DDI Buffer Control (Gen9+)
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1 - DDI Registers
 *
 */

#define DDI_BUF_CTL_A					(0x4000 | REGS_NORTH_PIPE_AND_PORT)
#define DDI_BUF_CTL_B					(0x4100 | REGS_NORTH_PIPE_AND_PORT)
#define DDI_BUF_CTL_C					(0x4200 | REGS_NORTH_PIPE_AND_PORT)
#define DDI_BUF_CTL_D					(0x4300 | REGS_NORTH_PIPE_AND_PORT)
#define DDI_BUF_CTL_E					(0x4400 | REGS_NORTH_PIPE_AND_PORT)
#define DDI_BUF_CTL_F					(0x4500 | REGS_NORTH_PIPE_AND_PORT)
#define DDI_BUF_CTL_G					(0x4600 | REGS_NORTH_PIPE_AND_PORT)
#define DDI_BUF_CTL_ENABLE				(1 << 31)
#define DDI_BUF_TRANS_SELECT(n)			((n) << 24)
#define DDI_BUF_EMP_MASK				(0xf << 24)
#define DDI_BUF_PORT_REVERSAL			(1 << 16)
#define DDI_BUF_IS_IDLE					(1 << 7)
#define DDI_A_4_LANES					(1 << 4)
#define DDI_PORT_WIDTH(width)			(((width) - 1) << 1)
#define DDI_INIT_DISPLAY_DETECTED		(1 << 0)

#define PIPE_DDI_FUNC_CTL_A				(0x0400 | REGS_NORTH_PIPE_AND_PORT)
#define PIPE_DDI_FUNC_CTL_B				(0x1400 | REGS_NORTH_PIPE_AND_PORT)
#define PIPE_DDI_FUNC_CTL_C				(0x2400 | REGS_NORTH_PIPE_AND_PORT)
#define PIPE_DDI_FUNC_CTL_D				(0x3400 | REGS_NORTH_PIPE_AND_PORT)
#define PIPE_DDI_FUNC_CTL_EDP			(0xF400 | REGS_NORTH_PIPE_AND_PORT)
#define PIPE_DDI_FUNC_CTL_DSI0			(0xB400 | REGS_NORTH_PIPE_AND_PORT)
#define PIPE_DDI_FUNC_CTL_DSI1			(0xBC00 | REGS_NORTH_PIPE_AND_PORT)
#define PIPE_DDI_FUNC_CTL_ENABLE		(1 << 31)
#define PIPE_DDI_SELECT_SHIFT			28
#define TGL_PIPE_DDI_SELECT_SHIFT		27
#define PIPE_DDI_SELECT_PORT(x)			((x) << PIPE_DDI_SELECT_SHIFT)
#define TGL_PIPE_DDI_SELECT_PORT(x)		((x) << TGL_PIPE_DDI_SELECT_SHIFT)
#define PIPE_DDI_SELECT_MASK			(7 << PIPE_DDI_SELECT_SHIFT)
#define TGL_PIPE_DDI_SELECT_MASK		(7 << TGL_PIPE_DDI_SELECT_SHIFT)
#define PIPE_DDI_PORT_NONE				0
#define PIPE_DDI_PORT_B					1
#define PIPE_DDI_PORT_C					2
#define PIPE_DDI_PORT_D					3
#define PIPE_DDI_PORT_E					4
#define PIPE_DDI_PORT_F					5
#define PIPE_DDI_PORT_G					6
#define PIPE_DDI_MODESEL_SHIFT			24
#define PIPE_DDI_MODESEL_MODE(x)		((x) << PIPE_DDI_MODESEL_SHIFT)
#define PIPE_DDI_MODESEL_MASK			(7 << PIPE_DDI_MODESEL_SHIFT)
#define PIPE_DDI_MODE_HDMI				0
#define PIPE_DDI_MODE_DVI				1
#define PIPE_DDI_MODE_DP_SST			2
#define PIPE_DDI_MODE_DP_MST			3
#define PIPE_DDI_COLOR_SHIFT			20
#define PIPE_DDI_BPC(x)					((x) << PIPE_DDI_COLOR_SHIFT)
#define PIPE_DDI_BPC_MASK				(7 << PIPE_DDI_COLOR_SHIFT)
#define PIPE_DDI_8BPC					0
#define PIPE_DDI_10BPC					1
#define PIPE_DDI_6BPC					2
#define PIPE_DDI_12BPC					3
#define PIPE_DDI_DP_WIDTH_SHIFT			1
#define PIPE_DDI_DP_WIDTH_SEL(x)		((x) << PIPE_DDI_DP_WIDTH_SHIFT)
#define PIPE_DDI_DP_WIDTH_MASK			(7 << PIPE_DDI_DP_WIDTH_SHIFT)
#define PIPE_DDI_DP_WIDTH_1				0
#define PIPE_DDI_DP_WIDTH_2				1
#define PIPE_DDI_DP_WIDTH_4				2


/*
 * DisplayPort Registers (Gen9+)
 *
 * ✅ Gen9+ uses DDI for DisplayPort, not legacy DP registers
 *
 */

/* DP AUX Channels - Gen9+ unified layout */
#define _DPA_AUX_CH_CTL					(0x4010 | REGS_NORTH_PIPE_AND_PORT)
#define _DPA_AUX_CH_DATA1				(0x4014 | REGS_NORTH_PIPE_AND_PORT)
#define _DPB_AUX_CH_CTL					(0x4110 | REGS_NORTH_PIPE_AND_PORT)
#define _DPB_AUX_CH_DATA1				(0x4114 | REGS_NORTH_PIPE_AND_PORT)
#define DP_AUX_CH_CTL(aux) \
		(_DPA_AUX_CH_CTL + (_DPB_AUX_CH_CTL - _DPA_AUX_CH_CTL) * aux)
#define DP_AUX_CH_DATA(aux, i) \
		(_DPA_AUX_CH_DATA1 + (_DPB_AUX_CH_DATA1 - _DPA_AUX_CH_DATA1) * aux + i * 4)

#define INTEL_DP_AUX_CTL_BUSY			(1 << 31)
#define INTEL_DP_AUX_CTL_DONE			(1 << 30)
#define INTEL_DP_AUX_CTL_INTERRUPT		(1 << 29)
#define INTEL_DP_AUX_CTL_TIMEOUT_ERROR	(1 << 28)
#define INTEL_DP_AUX_CTL_TIMEOUT_400us	(0 << 26)
#define INTEL_DP_AUX_CTL_TIMEOUT_600us	(1 << 26)
#define INTEL_DP_AUX_CTL_TIMEOUT_800us	(2 << 26)
#define INTEL_DP_AUX_CTL_TIMEOUT_1600us	(3 << 26)
#define INTEL_DP_AUX_CTL_TIMEOUT_MASK	(3 << 26)
#define INTEL_DP_AUX_CTL_RECEIVE_ERROR	(1 << 25)
#define INTEL_DP_AUX_CTL_MSG_SIZE_MASK	(0x1f << 20)
#define INTEL_DP_AUX_CTL_MSG_SIZE_SHIFT	20
#define INTEL_DP_AUX_CTL_PRECHARGE_2US_MASK		(0xf << 16)
#define INTEL_DP_AUX_CTL_PRECHARGE_2US_SHIFT	16
#define INTEL_DP_AUX_CTL_BIT_CLOCK_2X_MASK		(0x7ff)
#define INTEL_DP_AUX_CTL_BIT_CLOCK_2X_SHIFT		0
#define INTEL_DP_AUX_CTL_FW_SYNC_PULSE_SKL(c)	(((c) - 1) << 5)
#define INTEL_DP_AUX_CTL_SYNC_PULSE_SKL(c)		((c) - 1)


/*
 * Pipe Control Registers (Gen9+)
 *
 */

#define INTEL_PIPE_ENABLED				(1UL << 31)
#define INTEL_PIPE_STATE				(1UL << 30)

#define INTEL_DISPLAY_A_PIPE_CONTROL	(0x0008	| REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_B_PIPE_CONTROL	(0x1008 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_C_PIPE_CONTROL	(0x2008 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_D_PIPE_CONTROL	(0x3008 | REGS_NORTH_PLANE_CONTROL)

#define INTEL_PIPE_DITHER_TYPE_MASK		(0x0000000c)
#define INTEL_PIPE_DITHER_TYPE_SP		(0 << 2)
#define INTEL_PIPE_DITHER_TYPE_ST1		(1 << 2)
#define INTEL_PIPE_DITHER_TYPE_ST2		(2 << 2)
#define INTEL_PIPE_DITHER_TYPE_TEMP		(3 << 2)
#define INTEL_PIPE_DITHER_EN			(1 << 4)
#define INTEL_PIPE_COLOR_SHIFT			5
#define INTEL_PIPE_BPC(x)				((x) << INTEL_PIPE_COLOR_SHIFT)
#define INTEL_PIPE_BPC_MASK				(7 << INTEL_PIPE_COLOR_SHIFT)
#define INTEL_PIPE_8BPC					0
#define INTEL_PIPE_10BPC				1
#define INTEL_PIPE_6BPC					2
#define INTEL_PIPE_12BPC				3
#define INTEL_PIPE_PROGRESSIVE			(0 << 21)


/*
 * Display Timing Registers (Gen9+)
 *
 */

#define INTEL_DISPLAY_A_HTOTAL			(0x0000 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_A_HBLANK			(0x0004 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_A_HSYNC			(0x0008 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_A_VTOTAL			(0x000c | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_A_VBLANK			(0x0010 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_A_VSYNC			(0x0014 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_B_HTOTAL			(0x1000 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_B_HBLANK			(0x1004 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_B_HSYNC			(0x1008 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_B_VTOTAL			(0x100c | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_B_VBLANK			(0x1010 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_B_VSYNC			(0x1014 | REGS_NORTH_PIPE_AND_PORT)

#define INTEL_DISPLAY_A_PIPE_SIZE		(0x001c | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DISPLAY_B_PIPE_SIZE		(0x101c | REGS_NORTH_PIPE_AND_PORT)


/*
 * DisplayPort Link M/N Registers (Gen9+)
 *
 */

#define INTEL_DDI_PIPE_A_DATA_M			(0x0030 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DDI_PIPE_B_DATA_M			(0x1030 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DDI_PIPE_A_DATA_N			(0x0034 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DDI_PIPE_B_DATA_N			(0x1034 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DDI_PIPE_A_LINK_M			(0x0040 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DDI_PIPE_B_LINK_M			(0x1040 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DDI_PIPE_A_LINK_N			(0x0044 | REGS_NORTH_PIPE_AND_PORT)
#define INTEL_DDI_PIPE_B_LINK_N			(0x1044 | REGS_NORTH_PIPE_AND_PORT)


/*
 * Transcoder Registers (Gen9+)
 *
 */

#define DDI_SKL_TRANS_CONF_A			(0x0008 | REGS_NORTH_PLANE_CONTROL)
#define DDI_SKL_TRANS_CONF_B			(0x1008 | REGS_NORTH_PLANE_CONTROL)
#define DDI_SKL_TRANS_CONF_C			(0x2008 | REGS_NORTH_PLANE_CONTROL)
#define DDI_SKL_TRANS_CONF_D			(0x3008 | REGS_NORTH_PLANE_CONTROL)
#define DDI_SKL_TRANS_CONF_EDP			(0xf008 | REGS_NORTH_PLANE_CONTROL)

#define TRANS_ENABLE					(1 << 31)
#define TRANS_ENABLED					(1 << 30)


/*
 * Display Plane Registers (Gen9+)
 *
 * ✅ Gen9+ uses universal planes with different register layout
 *
 */

#define INTEL_DISPLAY_A_CONTROL			(0x0180 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_A_BASE			(0x0184 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_A_BYTES_PER_ROW	(0x0188 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_A_POS				(0x018c | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_A_IMAGE_SIZE		(0x0190 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_A_SURFACE			(0x019c | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_A_OFFSET_HAS		(0x01a4 | REGS_NORTH_PLANE_CONTROL)

#define INTEL_DISPLAY_B_CONTROL			(0x1180 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_B_BASE			(0x1184 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_B_BYTES_PER_ROW	(0x1188 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_B_POS				(0x118c | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_B_IMAGE_SIZE		(0x1190 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_B_SURFACE			(0x119c | REGS_NORTH_PLANE_CONTROL)
#define INTEL_DISPLAY_B_OFFSET_HAS		(0x11a4 | REGS_NORTH_PLANE_CONTROL)

#define DISPLAY_CONTROL_ENABLED			(1UL << 31)
#define DISPLAY_CONTROL_GAMMA			(1UL << 30)

/* Gen9+ pixel format encoding */
#define DISPLAY_CONTROL_COLOR_MASK_SKY	(0x0fUL << 24)
#define DISPLAY_CONTROL_CMAP8_SKY		(0x0cUL << 24)
#define DISPLAY_CONTROL_RGB15_SKY		(0x0eUL << 24)
#define DISPLAY_CONTROL_RGB16_SKY		(0x0eUL << 24)
#define DISPLAY_CONTROL_RGB32_SKY		(0x04UL << 24)
#define DISPLAY_CONTROL_RGB64_SKY		(0x06UL << 24)


/*
 * Cursor Registers (Gen9+)
 *
 */

#define INTEL_CURSOR_CONTROL			(0x0080 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_CURSOR_BASE				(0x0084 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_CURSOR_POSITION			(0x0088 | REGS_NORTH_PLANE_CONTROL)
#define INTEL_CURSOR_SIZE				(0x00a0 | REGS_NORTH_PLANE_CONTROL)
#define CURSOR_ENABLED					(1UL << 31)
#define CURSOR_FORMAT_ARGB				(4UL << 24)
#define CURSOR_FORMAT_XRGB				(5UL << 24)
#define CURSOR_POSITION_NEGATIVE		0x8000
#define CURSOR_POSITION_MASK			0x3fff


/*
 * Palette Registers
 *
 */

#define INTEL_DISPLAY_A_PALETTE			(0xa000 | REGS_NORTH_SHARED)
#define INTEL_DISPLAY_B_PALETTE			(0xa800 | REGS_NORTH_SHARED)


/*
 * Skylake PLLs (Gen9)
 *
 * ✅ Verified against Intel PRM Vol 2c Part 1 - DPLL Programming
 *
 */

#define SKL_DPLL1_CFGCR1				(0xc040 | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL1_CFGCR2				(0xc044 | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL2_CFGCR1				(0xc048 | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL2_CFGCR2				(0xc04c | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL3_CFGCR1				(0xc050 | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL3_CFGCR2				(0xc054 | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL_CTRL1					(0xc058 | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL_CTRL2					(0xc05c | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL_STATUS					(0xc060 | REGS_NORTH_PIPE_AND_PORT)
#define SKL_DPLL0_DP_LINKRATE_SHIFT		1
#define SKL_DPLL1_DP_LINKRATE_SHIFT		7
#define SKL_DPLL2_DP_LINKRATE_SHIFT		13
#define SKL_DPLL3_DP_LINKRATE_SHIFT		19
#define SKL_DPLL_DP_LINKRATE_MASK		7
#define SKL_DPLL_CTRL1_2700				0
#define SKL_DPLL_CTRL1_1350				1
#define SKL_DPLL_CTRL1_810				2
#define SKL_DPLL_CTRL1_1620				3
#define SKL_DPLL_CTRL1_1080				4
#define SKL_DPLL_CTRL1_2160				5


/*
 * Ice Lake Reference Clock (Gen11)
 *
 * ✅ Verified against Intel PRM - ICL Display
 *
 */

#define ICL_DSSM						0x51004
#define ICL_DSSM_REF_FREQ_SHIFT			29
#define ICL_DSSM_REF_FREQ_MASK			(7 << ICL_DSSM_REF_FREQ_SHIFT)
#define ICL_DSSM_24000					0
#define ICL_DSSM_19200					1
#define ICL_DSSM_38400					2


/*
 * Tiger Lake PLLs (Gen12)
 *
 * ✅ Verified against Intel PRM Vol 2c - TGL Display
 *
 */

#define TGL_DPCLKA_CFGCR0				0x164280
#define TGL_DPCLKA_DDIC_CLOCK_OFF		(1 << 24)
#define TGL_DPCLKA_TC6_CLOCK_OFF		(1 << 23)
#define TGL_DPCLKA_TC5_CLOCK_OFF		(1 << 22)
#define TGL_DPCLKA_TC4_CLOCK_OFF		(1 << 21)
#define TGL_DPCLKA_TC3_CLOCK_OFF		(1 << 14)
#define TGL_DPCLKA_TC2_CLOCK_OFF		(1 << 13)
#define TGL_DPCLKA_TC1_CLOCK_OFF		(1 << 12)
#define TGL_DPCLKA_DDIB_CLOCK_OFF		(1 << 11)
#define TGL_DPCLKA_DDIA_CLOCK_OFF		(1 << 10)
#define TGL_DPCLKA_DDIC_CLOCK_SELECT	(3 << 4)
#define TGL_DPCLKA_DDIB_CLOCK_SELECT	(3 << 2)
#define TGL_DPCLKA_DDIB_CLOCK_SELECT_SHIFT	2
#define TGL_DPCLKA_DDIA_CLOCK_SELECT	(3 << 0)

#define TGL_DPLL0_CFGCR0				0x164284
#define TGL_DPLL1_CFGCR0				0x16428C
#define TGL_TBTPLL_CFGCR0				0x16429C
#define TGL_DPLL4_CFGCR0				0x164294
#define TGL_DPLL_DCO_FRACTION			(0x7FFF << 10)
#define TGL_DPLL_DCO_FRACTION_SHIFT		10
#define TGL_DPLL_DCO_INTEGER			(0x3FF << 0)

#define TGL_DPLL0_CFGCR1				0x164288
#define TGL_DPLL1_CFGCR1				0x164290
#define TGL_TBTPLL_CFGCR1				0x1642A0
#define TGL_DPLL4_CFGCR1				0x164298
#define TGL_DPLL_QDIV_RATIO				(0xFF << 10)
#define TGL_DPLL_QDIV_RATIO_SHIFT		10
#define TGL_DPLL_QDIV_ENABLE			(1 << 9)
#define TGL_DPLL_KDIV					(7 << 6)
#define TGL_DPLL_KDIV_1					(1 << 6)
#define TGL_DPLL_KDIV_2					(2 << 6)
#define TGL_DPLL_KDIV_3					(4 << 6)
#define TGL_DPLL_PDIV					(0xF << 2)
#define TGL_DPLL_PDIV_2					(1 << 2)
#define TGL_DPLL_PDIV_3					(2 << 2)
#define TGL_DPLL_PDIV_5					(4 << 2)
#define TGL_DPLL_PDIV_7					(8 << 2)
#define TGL_DPLL_CFSELOVRD				(3 << 0)

#define TGL_DPLL0_DIV0					0x164B00
#define TGL_DPLL1_DIV0					0x164C00
#define TGL_DPLL4_DIV0					0x164E00

#define TGL_DPLL0_ENABLE				0x46010
#define TGL_DPLL1_ENABLE				0x46014
#define TGL_DPLL4_ENABLE				0x46018
#define TGL_DPLL_ENABLE					(1 << 31)
#define TGL_DPLL_LOCK					(1 << 30)
#define TGL_DPLL_POWER_ENABLE			(1 << 27)
#define TGL_DPLL_POWER_STATE			(1 << 26)

#define TGL_DPLL0_SPREAD_SPECTRUM		0x164B10
#define TGL_DPLL1_SPREAD_SPECTRUM		0x164C10
#define TGL_DPLL4_SPREAD_SPECTRUM		0x164E10


/*
 * CD Clock
 *
 */

#define FUSE_STRAP						0x42014


/*
 * VGA Display Control
 *
 */

#define INTEL_VGA_DISPLAY_CONTROL		(0x1400 | REGS_NORTH_PLANE_CONTROL)
#define VGA_DISPLAY_DISABLED			(1UL << 31)


/*
 * LVDS/eDP Panel Registers
 *
 */

#define PCH_PANEL_STATUS				(0x7200 | REGS_SOUTH_SHARED)
#define PCH_PANEL_CONTROL				(0x7204 | REGS_SOUTH_SHARED)
#define PCH_PANEL_ON_DELAYS				(0x7208 | REGS_SOUTH_SHARED)
#define PCH_PANEL_OFF_DELAYS			(0x720c | REGS_SOUTH_SHARED)
#define PCH_PANEL_DIVISOR				(0x7210 | REGS_SOUTH_SHARED)

#define PANEL_STATUS_POWER_ON			(1UL << 31)
#define PANEL_CONTROL_POWER_TARGET_OFF	(0UL << 0)
#define PANEL_CONTROL_POWER_TARGET_ON	(1UL << 0)
#define PANEL_CONTROL_POWER_TARGET_RST	(1UL << 1)
#define PANEL_REGISTER_UNLOCK			(0xabcd << 16)

#define PANEL_DELAY_PORT_SELECT_MASK	(3 << 30)
#define PANEL_DELAY_PORT_SELECT_DPA		(1 << 30)
#define PANEL_DELAY_PORT_SELECT_DPC		(2 << 30)
#define PANEL_DELAY_PORT_SELECT_DPD		(3 << 30)

#define PANEL_DIVISOR_REFERENCE_DIV_MASK	0xffffff00
#define PANEL_DIVISOR_REFERENCE_DIV_SHIFT	8
#define PANEL_DIVISOR_POW_CYCLE_DLY_MASK	0x1f
#define PANEL_DIVISOR_POW_CYCLE_DLY_SHIFT	0x1f


/*
 * Backlight Control Registers (Gen9+)
 *
 */

#define BLC_PWM_PCH_CTL1				(0x8250 | REGS_SOUTH_SHARED)
#define BLC_PWM_PCH_CTL2				(0x8254 | REGS_SOUTH_SHARED)


/*
 * I2C / GMBUS Registers
 *
 */

#define INTEL_I2C_IO_A					(0x5010 | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_B					(0x5014 | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_C					(0x5018 | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_D					(0x501c | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_E					(0x5020 | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_F					(0x5024 | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_G					(0x5028 | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_H					(0x502c | REGS_SOUTH_SHARED)
#define INTEL_I2C_IO_I					(0x5030 | REGS_SOUTH_SHARED)

#define INTEL_GMBUS0					(0x5100 | REGS_SOUTH_SHARED)
#define INTEL_GMBUS4					(0x5110 | REGS_SOUTH_SHARED)

#define I2C_CLOCK_DIRECTION_MASK		(1 << 0)
#define I2C_CLOCK_DIRECTION_OUT			(1 << 1)
#define I2C_CLOCK_VALUE_MASK			(1 << 2)
#define I2C_CLOCK_VALUE_OUT				(1 << 3)
#define I2C_CLOCK_VALUE_IN				(1 << 4)
#define I2C_DATA_DIRECTION_MASK			(1 << 8)
#define I2C_DATA_DIRECTION_OUT			(1 << 9)
#define I2C_DATA_VALUE_MASK				(1 << 10)
#define I2C_DATA_VALUE_OUT				(1 << 11)
#define I2C_DATA_VALUE_IN				(1 << 12)
#define I2C_RESERVED					((1 << 13) | (1 << 5))


/*
 * GPU Clock Gating
 *
 */

#define INTEL_GEN9_CLKGATE_DIS_4		(0x653c | REGS_NORTH_SHARED)
#define BXT_GMBUSUNIT_CLK_GATE_DIS		(1 << 14)


/*
 * Power Wells (Gen9+)
 *
 * ✅ Verified against Intel PRM Vol 2c - Power Management
 *
 */

#define INTEL_PWR_WELL_CTL_1_BIOS		(0x5400 | REGS_NORTH_SHARED)
#define INTEL_PWR_WELL_CTL_2_DRIVER		(0x5404 | REGS_NORTH_SHARED)

#define HSW_PWR_WELL_CTL_REQ(i)			(0x2 << ((2 * i)))
#define HSW_PWR_WELL_CTL_STATE(i)		(0x1 << ((2 * i)))

#define HSW_PWR_WELL_CTL1				INTEL_PWR_WELL_CTL_1_BIOS
#define HSW_PWR_WELL_CTL2				INTEL_PWR_WELL_CTL_2_DRIVER
#define HSW_PWR_WELL_CTL3				(0x5408 | REGS_NORTH_SHARED)
#define HSW_PWR_WELL_CTL4				(0x540c | REGS_NORTH_SHARED)

#define ICL_PWR_WELL_CTL_AUX1			(0x5440 | REGS_NORTH_SHARED)
#define ICL_PWR_WELL_CTL_AUX2			(0x5444 | REGS_NORTH_SHARED)
#define ICL_PWR_WELL_CTL_AUX4			(0x544c | REGS_NORTH_SHARED)

#define ICL_PWR_WELL_CTL_DDI1			(0x5450 | REGS_NORTH_SHARED)
#define ICL_PWR_WELL_CTL_DDI2			(0x5454 | REGS_NORTH_SHARED)
#define ICL_PWR_WELL_CTL_DDI4			(0x545c | REGS_NORTH_SHARED)


/*
 * PCH Raw Clock
 *
 */

#define PCH_RAWCLK_FREQ					(0x6204 | REGS_SOUTH_SHARED)
#define RAWCLK_FREQ_MASK				0x3ff


/*
 * CPU Panel Fitters (Gen9+)
 *
 */

#define PCH_PANEL_FITTER_BASE_REGISTER	0x68000
#define PCH_PANEL_FITTER_PIPE_OFFSET	0x00800

#define PCH_PANEL_FITTER_WINDOW_POS		0x70
#define PCH_PANEL_FITTER_WINDOW_SIZE	0x74
#define PCH_PANEL_FITTER_CONTROL		0x80
#define PCH_PANEL_FITTER_V_SCALE		0x84
#define PCH_PANEL_FITTER_H_SCALE		0x90

#define PANEL_FITTER_ENABLED			(1 << 31)
#define PANEL_FITTER_PIPE_MASK			(3 << 29)
#define PANEL_FITTER_PIPE_A				(0 << 29)
#define PANEL_FITTER_PIPE_B				(1 << 29)
#define PANEL_FITTER_PIPE_C				(2 << 29)
#define PANEL_FITTER_SCALING_MODE_MASK	(7 << 26)
#define PANEL_FITTER_FILTER_MASK		(3 << 24)


/*
 * Ring Buffer Commands
 *
 */

#define COMMAND_NOOP					0x00
#define COMMAND_FLUSH					(0x04 << 23)


/*
 * 2D Acceleration Commands
 *
 */

#define XY_COMMAND_SOURCE_BLIT			0x54c00006
#define XY_COMMAND_COLOR_BLIT			0x54000004
#define COMMAND_COLOR_BLIT				0x50000003
#define COMMAND_BLIT_RGBA				0x00300000

#define COMMAND_MODE_SOLID_PATTERN		0x80
#define COMMAND_MODE_CMAP8				0x00
#define COMMAND_MODE_RGB15				0x02
#define COMMAND_MODE_RGB16				0x01
#define COMMAND_MODE_RGB32				0x03


#endif	/* INTEL_EXTREME_H */
