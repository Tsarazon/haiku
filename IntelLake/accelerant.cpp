/*
 * Copyright 2006-2016, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Gen 9+ only refactoring for Mobile Haiku.
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <Debug.h>

#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include <new>

#include <AGP.h>
#include <AutoDeleterOS.h>


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

	// Gen 9+ only: no legacy 3D context allocation needed
	// (i965 3D context was for Gen 4 overlay workaround)

	gInfo->pipe_count = 0;

	// Allocate pipes - Gen 9+ has 3 pipes, Gen 12+ has 4 pipes
	// Reference: Intel PRM Vol 2c, Display Engine
	int pipeCnt = 3;
	if (gInfo->shared_info->device_type.Generation() >= 12)
		pipeCnt = 4;

	for (int i = 0; i < pipeCnt; i++) {
		switch (i) {
			case 0:
				gInfo->pipes[i] = new(std::nothrow) Pipe(INTEL_PIPE_A);
				break;
			case 1:
				gInfo->pipes[i] = new(std::nothrow) Pipe(INTEL_PIPE_B);
				break;
			case 2:
				gInfo->pipes[i] = new(std::nothrow) Pipe(INTEL_PIPE_C);
				break;
			case 3:
				gInfo->pipes[i] = new(std::nothrow) Pipe(INTEL_PIPE_D);
				break;
			default:
				ERROR("%s: Unknown pipe %d\n", __func__, i);
		}
		if (gInfo->pipes[i] == NULL)
			ERROR("%s: Error allocating pipe %d\n", __func__, i);
		else
			gInfo->pipe_count++;
	}

	return B_OK;
}


/*! Clean up data common to both primary and cloned accelerant */
static void
uninit_common(void)
{
	// Gen 9+: no context memory to free (was for i965 only)

	delete_area(gInfo->regs_area);
	delete_area(gInfo->shared_info_area);

	gInfo->regs_area = gInfo->shared_info_area = -1;

	// close the file handle ONLY if we're the clone
	if (gInfo->is_clone)
		close(gInfo->device);

	free(gInfo);
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


static status_t
probe_ports()
{
	// Gen 9+ uses DDI (Digital Display Interface) for all outputs.
	// No legacy DisplayPort, HDMI, LVDS, DVI, or Analog port probing needed.
	//
	// Reference: Intel PRM Vol 12, Display Connections
	// DDI ports handle DP, HDMI, DVI, and eDP through a unified interface.

	TRACE("dp_a: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_A));
	TRACE("dp_b: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_B));
	TRACE("dp_c: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_C));
	TRACE("dp_d: %08" B_PRIx32 "\n", read32(INTEL_DISPLAY_PORT_D));

	gInfo->port_count = 0;

	// Digital Display Interface (DDI) - Gen 9+ only path
	// Reference: Intel PRM Vol 2c, DDI Buffer Control
	if (gInfo->shared_info->device_type.HasDDI()) {
		int maxPort = INTEL_PORT_F;
		if (gInfo->shared_info->device_type.Generation() >= 12)
			maxPort = INTEL_PORT_G;

		for (int i = INTEL_PORT_A; i <= maxPort; i++) {
			TRACE("Probing DDI %d\n", i);

			Port* ddiPort
				= new(std::nothrow) DigitalDisplayInterface((port_index)i);

			if (ddiPort == NULL)
				return B_NO_MEMORY;

			if (ddiPort->IsConnected()) {
				gInfo->ports[gInfo->port_count++] = ddiPort;
			} else
				delete ddiPort;
		}
	}

	if (gInfo->port_count == 0)
		return B_ERROR;

	// Gen 9+ does not need legacy reference clock activation
	// Reference clocks are handled by the DDI PLL configuration

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

	// Gen 9+: All are HD/Iris/Xe Graphics
	// Skylake-Coffee Lake: HD Graphics, Iris Graphics, Iris Pro
	// Ice Lake+: Iris Plus, Iris Xe
	if (type->InFamily(INTEL_FAMILY_LAKE))
		strcpy(info->name, "Intel Iris Xe");
	else
		strcpy(info->name, "Intel HD/Iris");

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
