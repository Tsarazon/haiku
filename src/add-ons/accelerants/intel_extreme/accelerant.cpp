/*
 * Copyright 2006-2016, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include "utility.h"

#include <Debug.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include <new>

#include <AGP.h>
#include <AutoDeleterOS.h>


#undef TRACE
#define TRACE_ACCELERANT
#ifdef TRACE_ACCELERANT
#	define TRACE(x...) _sPrintf("intel_extreme: " x)
#else
#	define TRACE(x...)
#endif

#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


struct accelerant_info* gInfo;
uint32 gDumpCount;


//	#pragma mark -


// intel_reg --mmio=ie-0001.bin --devid=27a2 dump
void
dump_registers()
{
	char filename[255];

	sprintf(filename, "/boot/system/cache/tmp/ie-%04" B_PRId32 ".bin",
		gDumpCount);

	ERROR("%s: Taking register dump #%" B_PRId32 "\n", __func__, gDumpCount);

	area_info areaInfo;
	get_area_info(gInfo->shared_info->registers_area, &areaInfo);

	int fd = open(filename, O_CREAT | O_WRONLY, 0644);
	uint32 data = 0;
	if (fd >= 0) {
		for (uint32 i = 0; i < areaInfo.size; i += sizeof(data)) {
			//char line[512];
			//int length = sprintf(line, "%05" B_PRIx32 ": "
			//	"%08" B_PRIx32 " %08" B_PRIx32 " %08" B_PRIx32 " %08" B_PRIx32 "\n",
			//	i, read32(i), read32(i + 4), read32(i + 8), read32(i + 12));
			data = read32(i);
			write(fd, &data, sizeof(data));
		}
		close(fd);
		sync();
	}

	gDumpCount++;
}


/*! This is the common accelerant_info initializer. It is called by
	both, the first accelerant and all clones.
*/
static status_t
init_common(int device, bool isClone)
{
	// initialize global accelerant info structure

	// Number of register dumps we have... taken.
	gDumpCount = 0;

	gInfo = (accelerant_info*)malloc(sizeof(accelerant_info));
	if (gInfo == NULL)
		return B_NO_MEMORY;
	MemoryDeleter infoDeleter(gInfo);

	memset(gInfo, 0, sizeof(accelerant_info));

	gInfo->is_clone = isClone;
	gInfo->device = device;

	// get basic info from driver

	intel_get_private_data data;
	data.magic = INTEL_PRIVATE_DATA_MAGIC;

	if (ioctl(device, INTEL_GET_PRIVATE_DATA, &data,
			sizeof(intel_get_private_data)) != 0)
		return B_ERROR;

	AreaDeleter sharedDeleter(clone_area("intel extreme shared info",
		(void**)&gInfo->shared_info, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		data.shared_info_area));
	status_t status = gInfo->shared_info_area = sharedDeleter.Get();
	if (status < B_OK)
		return status;

	AreaDeleter regsDeleter(clone_area("intel extreme regs",
		(void**)&gInfo->registers, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		gInfo->shared_info->registers_area));
	status = gInfo->regs_area = regsDeleter.Get();
	if (status < B_OK)
		return status;

	infoDeleter.Detach();
	sharedDeleter.Detach();
	regsDeleter.Detach();

	// The overlay registers, hardware status, and cursor memory share
	// a single area with the shared_info

	if (gInfo->shared_info->overlay_offset != 0) {
		gInfo->overlay_registers = (struct overlay_registers*)
			(gInfo->shared_info->graphics_memory
			+ gInfo->shared_info->overlay_offset);
	}

	// Gen 6+: No 3D context allocation needed (Gen 4 i965 code removed)

	gInfo->pipe_count = 0;

	// Determine pipe count based on generation
	int pipeCnt = 2;  // Default for older GPUs (Gen 6)
	if (gInfo->shared_info->device_type.Generation() >= 12)
		pipeCnt = 4;
	else if (gInfo->shared_info->device_type.Generation() >= 7)
		pipeCnt = 3;

	// Array for mapping indices to constants
	static const pipe_index pipeIndices[] = {
		INTEL_PIPE_A, INTEL_PIPE_B, INTEL_PIPE_C, INTEL_PIPE_D
	};

	// Allocate pipes with error checking
	for (int i = 0; i < pipeCnt && i < MAX_PIPES; i++) {
		gInfo->pipes[i] = new(std::nothrow) Pipe(pipeIndices[i]);

		if (gInfo->pipes[i] == NULL) {
			ERROR("%s: Failed to allocate pipe %d\n", __func__, i);
			// Clean up already created pipes
			for (int j = 0; j < i; j++) {
				delete gInfo->pipes[j];
				gInfo->pipes[j] = NULL;
			}
			return B_NO_MEMORY;
		}
		gInfo->pipe_count++;
	}

	return B_OK;
}


/*! Clean up data common to both primary and cloned accelerant */
static void
uninit_common(void)
{
	if (gInfo == NULL)
		return;

	// Free 3D context memory (if allocated)
	if (gInfo->context_base != 0)
		intel_free_memory(gInfo->context_base);

	// Delete areas in reverse order of creation
	if (gInfo->regs_area >= 0) {
		delete_area(gInfo->regs_area);
		gInfo->regs_area = -1;
	}

	if (gInfo->shared_info_area >= 0) {
		delete_area(gInfo->shared_info_area);
		gInfo->shared_info_area = -1;
	}

	// Close device handle only for clones
	if (gInfo->is_clone && gInfo->device >= 0)
		close(gInfo->device);

	// Free structure
	free(gInfo);
	gInfo = NULL;  // Important to prevent use-after-free
}


static void
dump_ports()
{
	if (gInfo->port_count == 0) {
		TRACE("%s: No ports connected\n", __func__);
		return;
	}

	TRACE("%s: Connected ports: (port_count: %" B_PRIu32 ")\n", __func__,
		gInfo->port_count);

	for (uint32 i = 0; i < gInfo->port_count; i++) {
		Port* port = gInfo->ports[i];
		if (!port) {
			TRACE("port %" B_PRIu32 ":: INVALID ALLOC!\n", i);
			continue;
		}
		TRACE("port %" B_PRIu32 ": %s %s\n", i, port->PortName(),
			port->IsConnected() ? "connected" : "disconnected");
	}
}


static bool
has_connected_port(port_index portIndex, uint32 type)
{
	for (uint32 i = 0; i < gInfo->port_count; i++) {
		Port* port = gInfo->ports[i];
		if (type != INTEL_PORT_TYPE_ANY && port->Type() != type)
			continue;
		if (portIndex != INTEL_PORT_ANY && port->PortIndex() != portIndex)
			continue;

		return true;
	}

	return false;
}


// Helper structure for port type information
struct PortProbeInfo {
	port_index start;
	port_index end;
	const char* name;
	Port* (*factory)(port_index);
};


// Factory functions for creating ports
static Port* CreateDisplayPort(port_index idx)
{
	return new(std::nothrow) DisplayPort(idx);
}


static Port* CreateDDIPort(port_index idx)
{
	return new(std::nothrow) DigitalDisplayInterface(idx);
}


static Port* CreateHDMIPort(port_index idx)
{
	return new(std::nothrow) HDMIPort(idx);
}


static Port* CreateDVIPort(port_index idx)
{
	return new(std::nothrow) DigitalPort(idx, "DVI");
}


static Port* CreateLVDS()
{
	return new(std::nothrow) LVDSPort();
}


static Port* CreateAnalog()
{
	return new(std::nothrow) AnalogPort();
}


// Universal port probing function
static status_t
_ProbePortRange(const PortProbeInfo& info, bool& foundAny)
{
	for (int i = info.start; i <= info.end && gInfo->port_count < MAX_PORTS; i++) {
		TRACE("Probing %s %d\n", info.name, i);

		Port* port = info.factory((port_index)i);
		if (port == NULL) {
			ERROR("Failed to allocate %s port %d\n", info.name, i);
			return B_NO_MEMORY;
		}

		if (port->IsConnected()) {
			foundAny = true;
			gInfo->ports[gInfo->port_count++] = port;
		} else {
			delete port;
		}
	}

	return B_OK;
}


static status_t
probe_ports()
{
	// Try to determine what ports to use. We use the following heuristic:
	// * Check for DisplayPort, these can be more or less detected reliably.
	// * Check for HDMI, it'll fail on devices not having HDMI for us to fall
	//   back to DVI.
	// * Assume DVI B if no HDMI and no DisplayPort is present, confirmed by
	//   reading EDID in the IsConnected() call.
	// * Check for analog if possible (there's a detection bit on PCH),
	//   otherwise the assumed presence is confirmed by reading EDID in
	//   IsConnected().

	TRACE("adpa: %08" B_PRIx32 "\n", read32(INTEL_ANALOG_PORT));
	TRACE("dova: %08" B_PRIx32 ", dovb: %08" B_PRIx32
		", dovc: %08" B_PRIx32 "\n", read32(INTEL_DIGITAL_PORT_A),
		read32(INTEL_DIGITAL_PORT_B), read32(INTEL_DIGITAL_PORT_C));
	TRACE("lvds: %08" B_PRIx32 "\n", read32(INTEL_DIGITAL_LVDS_PORT));

	TRACE("dp_a: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_A));
	TRACE("dp_b: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_B));
	TRACE("dp_c: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_C));
	TRACE("dp_d: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_D));
	TRACE("tra_dp: %08" B_PRIx32 "\n", read32(INTEL_TRANSCODER_A_DP_CTL));
	TRACE("trb_dp: %08" B_PRIx32 "\n", read32(INTEL_TRANSCODER_B_DP_CTL));
	TRACE("trc_dp: %08" B_PRIx32 "\n", read32(INTEL_TRANSCODER_C_DP_CTL));

	bool foundLVDS = false;
	bool foundDP = false;
	bool foundDDI = false;
	status_t result;

	gInfo->port_count = 0;

	// Check DisplayPort on older GPUs (pre-DDI)
	if (!gInfo->shared_info->device_type.HasDDI()) {
		PortProbeInfo dpInfo = {
			INTEL_PORT_A, INTEL_PORT_D,
			"DisplayPort", CreateDisplayPort
		};
		result = _ProbePortRange(dpInfo, foundDP);
		if (result != B_OK)
			return result;
	}

	// Check Digital Display Interface on newer GPUs
	if (gInfo->shared_info->device_type.HasDDI()) {
		port_index maxPort = INTEL_PORT_F;
		if (gInfo->shared_info->device_type.Generation() >= 12)
			maxPort = INTEL_PORT_G;

		PortProbeInfo ddiInfo = {
			INTEL_PORT_A, maxPort,
			"DDI", CreateDDIPort
		};
		result = _ProbePortRange(ddiInfo, foundDDI);
		if (result != B_OK)
			return result;
	}

	// Check HDMI ports (only if no DDI, skip already claimed ports)
	if (!gInfo->shared_info->device_type.HasDDI()) {
		for (int i = INTEL_PORT_B; i <= INTEL_PORT_D; i++) {
			if (has_connected_port((port_index)i, INTEL_PORT_TYPE_ANY)) {
				TRACE("Port %d already claimed\n", i);
				continue;
			}

			TRACE("Probing HDMI %d\n", i);
			Port* hdmiPort = CreateHDMIPort((port_index)i);
			if (hdmiPort == NULL)
				return B_NO_MEMORY;

			if (hdmiPort->IsConnected())
				gInfo->ports[gInfo->port_count++] = hdmiPort;
			else
				delete hdmiPort;
		}
	}

	// Check LVDS panel (only on non-DDI chipsets)
	if (!gInfo->shared_info->device_type.HasDDI()) {
		TRACE("Probing LVDS\n");
		Port* lvdsPort = CreateLVDS();
		if (lvdsPort == NULL)
			return B_NO_MEMORY;

		if (lvdsPort->IsConnected()) {
			foundLVDS = true;
			gInfo->ports[gInfo->port_count++] = lvdsPort;
			gInfo->head_mode |= HEAD_MODE_LVDS_PANEL;
			gInfo->head_mode |= HEAD_MODE_B_DIGITAL;
		} else {
			delete lvdsPort;
		}
	}

	// Check DVI fallback (if no other digital ports found)
	if (!gInfo->shared_info->device_type.HasDDI()) {
		if (!has_connected_port(INTEL_PORT_ANY, INTEL_PORT_TYPE_ANY)) {
			TRACE("Probing DVI fallback\n");
			PortProbeInfo dviInfo = {
				INTEL_PORT_B, INTEL_PORT_C,
				"DVI", CreateDVIPort
			};
			bool foundDVI = false;
			result = _ProbePortRange(dviInfo, foundDVI);
			if (result != B_OK)
				return result;

			if (foundDVI)
				gInfo->head_mode |= HEAD_MODE_B_DIGITAL;
		}
	}

	// Check analog VGA (Gen <= 8 with internal CRT support)
	if (gInfo->shared_info->device_type.Generation() <= 8
		&& gInfo->shared_info->internal_crt_support) {
		TRACE("Probing Analog\n");
		Port* analogPort = CreateAnalog();
		if (analogPort == NULL)
			return B_NO_MEMORY;

		if (analogPort->IsConnected()) {
			gInfo->ports[gInfo->port_count++] = analogPort;
			gInfo->head_mode |= HEAD_MODE_A_ANALOG;
		} else {
			delete analogPort;
		}
	}

	if (gInfo->port_count == 0)
		return B_ERROR;

	// Activate reference clocks if needed
	if (gInfo->shared_info->pch_info == INTEL_PCH_IBX
		|| gInfo->shared_info->pch_info == INTEL_PCH_CPT) {
		TRACE("Activating clocks\n");
		refclk_activate_ilk(foundLVDS || foundDP || foundDDI);
	}
	/*
	} else if (gInfo->shared_info->pch_info == INTEL_PCH_LPT) {
		// TODO: Some kind of stepped bend thing?
		// only needed for vga
		refclk_activate_lpt(foundLVDS);
	}
	*/

	TRACE("Probing complete.\n");
	return B_OK;
}


static status_t
assign_pipes()
{
	// TODO: At some point we should "group" ports to pipes with the same mode.
	// You can drive multiple ports from a single pipe as long as the mode is
	// the same. For the moment we could get displays with the wrong pipes
	// assigned when the count is > 1;

	uint32 current = 0;

	bool assigned[gInfo->pipe_count];
	memset(assigned, 0, gInfo->pipe_count);

	// Some ports need to be assigned to a fixed pipe on old hardware (or due
	// to limitations in the current driver on current hardware). Assign those
	// first
	for (uint32 i = 0; i < gInfo->port_count; i++) {
		if (!gInfo->ports[i]->IsConnected())
			continue;

		pipe_index preference = gInfo->ports[i]->PipePreference();
		if (preference != INTEL_PIPE_ANY) {
			int index = (int)preference - 1;
			if (assigned[index]) {
				TRACE("Pipe %d is already assigned, it will drive multiple "
					"displays\n", index);
			}
			gInfo->ports[i]->SetPipe(gInfo->pipes[index]);
			assigned[index] = true;
			continue;
		}
	}

	// In a second pass, assign the remaining ports to the remaining pipes
	for (uint32 i = 0; i < gInfo->port_count; i++) {
		if (gInfo->ports[i]->IsConnected() && gInfo->ports[i]->GetPipe() == NULL) {
			while (current < gInfo->pipe_count && assigned[current])
				current++;

			if (current >= gInfo->pipe_count) {
				ERROR("%s: No pipes left to assign to port %s!\n", __func__,
					gInfo->ports[i]->PortName());
				continue;
			}

			gInfo->ports[i]->SetPipe(gInfo->pipes[current]);
			assigned[current] = true;
		}
	}

	return B_OK;
}


//	#pragma mark - public accelerant functions


/*! Init primary accelerant */
status_t
intel_init_accelerant(int device)
{
	CALLED();

	status_t status = init_common(device, false);
	if (status != B_OK)
		return status;

	intel_shared_info &info = *gInfo->shared_info;

	init_lock(&info.accelerant_lock, "intel extreme accelerant");
	init_lock(&info.engine_lock, "intel extreme engine");

	setup_ring_buffer(info.primary_ring_buffer, "intel primary ring buffer");

	// Probe all ports
	status = probe_ports();

	// On TRACE, dump ports and states
	dump_ports();

	if (status != B_OK)
		ERROR("Warning: zero active displays were found!\n");

	status = assign_pipes();

	if (status != B_OK)
		ERROR("Warning: error while assigning pipes!\n");

	status = create_mode_list();
	if (status != B_OK) {
		uninit_common();
		return status;
	}

	return B_OK;
}


ssize_t
intel_accelerant_clone_info_size(void)
{
	CALLED();
	// clone info is device name, so return its maximum size
	return B_PATH_NAME_LENGTH;
}


void
intel_get_accelerant_clone_info(void* info)
{
	CALLED();
	ioctl(gInfo->device, INTEL_GET_DEVICE_NAME, info, B_PATH_NAME_LENGTH);
}


status_t
intel_clone_accelerant(void* info)
{
	CALLED();

	// create full device name
	char path[B_PATH_NAME_LENGTH];
	strcpy(path, "/dev/");
#ifdef __HAIKU__
	strlcat(path, (const char*)info, sizeof(path));
#else
	strcat(path, (const char*)info);
#endif

	int fd = open(path, B_READ_WRITE);
	if (fd < 0)
		return errno;

	status_t status = init_common(fd, true);
	if (status != B_OK)
		goto err1;

	// get read-only clone of supported display modes
	status = gInfo->mode_list_area = clone_area(
		"intel extreme cloned modes", (void**)&gInfo->mode_list,
		B_ANY_ADDRESS, B_READ_AREA, gInfo->shared_info->mode_list_area);
	if (status < B_OK)
		goto err2;

	return B_OK;

err2:
	uninit_common();
err1:
	close(fd);
	return status;
}


/*! This function is called for both, the primary accelerant and all of
	its clones.
*/
void
intel_uninit_accelerant(void)
{
	CALLED();

	// delete accelerant instance data
	delete_area(gInfo->mode_list_area);
	gInfo->mode_list = NULL;

	if (!gInfo->is_clone) {
		intel_shared_info &info = *gInfo->shared_info;
		uninit_lock(&info.accelerant_lock);
		uninit_lock(&info.engine_lock);
		uninit_ring_buffer(info.primary_ring_buffer);
	}
	uninit_common();
}


status_t
intel_get_accelerant_device_info(accelerant_device_info* info)
{
	CALLED();

	info->version = B_ACCELERANT_VERSION;

	DeviceType* type = &gInfo->shared_info->device_type;

	// Gen 6+ naming (removed Gen < 6: INTEL_FAMILY_8xx, INTEL_FAMILY_9xx)
	if (type->InFamily(INTEL_FAMILY_SOC0))
		strcpy(info->name, "Intel Atom");
	else if (type->InFamily(INTEL_FAMILY_SER5))
		strcpy(info->name, "Intel HD/Iris");
	else
		strcpy(info->name, "Intel");

	strcpy(info->chipset, gInfo->shared_info->device_identifier);
	strcpy(info->serial_no, "None");

	info->memory = gInfo->shared_info->graphics_memory_size;
	info->dac_speed = gInfo->shared_info->pll_info.max_frequency;

	return B_OK;
}


sem_id
intel_accelerant_retrace_semaphore()
{
	CALLED();
	return gInfo->shared_info->vblank_sem;
}
