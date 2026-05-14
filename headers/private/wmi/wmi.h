/*
 * Copyright 2020, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _WMI_H_
#define _WMI_H_


#include <ACPI.h>
#include <device_keeper.h>
#include <KernelExport.h>



// Device node

// guid (string)
#define WMI_GUID_STRING_ITEM "wmi/guid_string"

// device cookie, issued by wmi bus manager
typedef void* wmi_device;


// Interface name for get_interface/publish_interface.
// Published by WMIDevice on each WMI child node; consumed by
// peripheral drivers (WMIAsus etc.) via KOSM_INTERFACE_ANCESTORS.
#define WMI_DEVICE_INTERFACE_NAME	"interface/wmi/device/v1"

// bus manager device interface for peripheral driver
typedef struct {
	status_t (*evaluate_method)(wmi_device device, uint8 instance,
		uint32 methodId, const acpi_data* in, acpi_data* out);
	status_t (*install_event_handler)(wmi_device device,
		const char* guidString, acpi_notify_handler handler, void* context);
	status_t (*remove_event_handler)(wmi_device device,
		const char* guidString);
	status_t (*get_event_data)(wmi_device device, uint32 notify,
		acpi_data* out);
	const char* (*get_uid)(wmi_device device);
} wmi_device_interface;


#endif	/* _WMI_H_ */
