/*
 * Copyright 2009, Clemens Zeidler. All rights reserved.
 * Copyright 2006, Jérôme Duval. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */


#include <stdlib.h>
#include <string.h>


#include "ACPIPrivate.h"
extern "C" {
#include "acpi.h"
}
#include <dpc.h>
#include <bus/PCI.h>


//#define TRACE_ACPI_MODULE
#ifdef TRACE_ACPI_MODULE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


dk_keeper_info* gDeviceKeeper = NULL;
pci_module_info* gPCIManager = NULL;
dpc_module_info* gDPC = NULL;

module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_PCI_MODULE_NAME, (module_info**)&gPCIManager },
	{ B_DPC_MODULE_NAME, (module_info**)&gDPC },
	{}
};


//	#pragma mark - ACPI root driver


static const dk_match_rule sACPIRootMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "root"),
	DK_MATCH_END
};

static const dk_match_dict sACPIRootMatchDict = {
	sACPIRootMatchRules,
	0
};


static status_t
acpi_enumerate_child_devices(dk_node* node, const char* root)
{
	char result[255];
	void* counter = NULL;

	TRACE(("acpi_enumerate_child_devices: recursing from %s\n", root));

	while (get_next_entry(ACPI_TYPE_ANY, root, result,
			sizeof(result), &counter) == B_OK) {
		uint32 type = get_object_type(result);
		dk_node* deviceNode;

		switch (type) {
			case ACPI_TYPE_POWER:
			case ACPI_TYPE_PROCESSOR:
			case ACPI_TYPE_THERMAL:
			case ACPI_TYPE_DEVICE: {
				dk_property attrs[16] = {
					// info about device
					DK_PROP_STRING(KOSM_DEVICE_BUS, "acpi"),

					// location on ACPI bus
					DK_PROP_STRING(KOSM_ACPI_DEVICE_PATH, result),

					// info about the device
					DK_PROP_UINT32(KOSM_ACPI_DEVICE_TYPE, type),

					DK_PROP_UINT32(KOSM_DEVICE_FLAGS,
						KOSM_FIND_MULTIPLE_CHILDREN),
					DK_PROP_END
				};

				uint32 attrCount = 4;
				char* hid = NULL;
				char* cidList[11] = { NULL };
				char* uid = NULL;
				char* cls = NULL;
				if (type == ACPI_TYPE_DEVICE) {
					if (get_device_info(result, &hid, (char**)&cidList, 8,
						&uid, &cls) == B_OK) {
						if (hid != NULL) {
							attrs[attrCount].name = KOSM_ACPI_DEVICE_HID;
							attrs[attrCount].type = B_STRING_TYPE;
							attrs[attrCount].value.string = hid;
							attrCount++;
						}
						for (int i = 0; cidList[i] != NULL; i++) {
							attrs[attrCount].name = KOSM_ACPI_DEVICE_CID;
							attrs[attrCount].type = B_STRING_TYPE;
							attrs[attrCount].value.string = cidList[i];
							attrCount++;
						}
						if (uid != NULL) {
							attrs[attrCount].name = KOSM_ACPI_DEVICE_UID;
							attrs[attrCount].type = B_STRING_TYPE;
							attrs[attrCount].value.string = uid;
							attrCount++;
						}
						if (cls != NULL) {
							uint32 clsClass = strtoul(cls, NULL, 16);
							attrs[attrCount].name = KOSM_DEVICE_TYPE;
							attrs[attrCount].type = B_UINT16_TYPE;
							attrs[attrCount].value.ui16
								= (clsClass >> 16) & 0xff;
							attrCount++;
							attrs[attrCount].name = KOSM_DEVICE_SUB_TYPE;
							attrs[attrCount].type = B_UINT16_TYPE;
							attrs[attrCount].value.ui16
								= (clsClass >> 8) & 0xff;
							attrCount++;
							attrs[attrCount].name = KOSM_DEVICE_INTERFACE;
							attrs[attrCount].type = B_UINT16_TYPE;
							attrs[attrCount].value.ui16
								= (clsClass >> 0) & 0xff;
							attrCount++;
						}
					}
					uint32 addr;
					if (get_device_addr(result, &addr) == B_OK) {
						attrs[attrCount].name = KOSM_ACPI_DEVICE_ADR;
						attrs[attrCount].type = B_UINT32_TYPE;
						attrs[attrCount].value.ui32 = addr;
						attrCount++;
					}
				}

				status_t status = gDeviceKeeper->register_node(node,
						ACPI_DEVICE_MODULE_NAME, attrs, NULL, &deviceNode);
				free(hid);
				free(uid);
				free(cls);
				for (int i = 0; cidList[i] != NULL; i++)
					free(cidList[i]);
				if (status != B_OK)
					break;
				acpi_enumerate_child_devices(deviceNode, result);
				break;
			}
			default:
				acpi_enumerate_child_devices(node, result);
				break;
		}
	}

	return B_OK;
}


static status_t
acpi_root_attach(dk_node* node, void** _cookie)
{
	// Publish the acpi_root_info interface so leaf drivers (namespace
	// dump, acpi call) can retrieve it via get_interface.
	status_t status = gDeviceKeeper->publish_interface(node,
		ACPI_ROOT_INTERFACE_NAME, &gACPIRootInterface);
	if (status != B_OK)
		return status;

	// Register devfs nodes as child nodes. These drivers publish
	// themselves in their attach().
	dk_property nsDumpAttrs[] = {
		DK_PROP_STRING(KOSM_DEVICE_BUS, "acpi"),
		DK_PROP_END
	};
	gDeviceKeeper->register_node(node, ACPI_NS_DUMP_DRIVER_NAME,
		nsDumpAttrs, NULL, NULL);

	dk_property callAttrs[] = {
		DK_PROP_STRING(KOSM_DEVICE_BUS, "acpi"),
		DK_PROP_END
	};
	gDeviceKeeper->register_node(node, ACPI_CALL_DRIVER_NAME,
		callAttrs, NULL, NULL);

	if ((AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON) == 0) {
		dprintf("registering power button\n");
		dk_property attrs[] = {
			DK_PROP_STRING(KOSM_DEVICE_BUS, "acpi"),
			DK_PROP_STRING(KOSM_ACPI_DEVICE_HID, "ACPI_FPB"),
			DK_PROP_UINT32(KOSM_ACPI_DEVICE_TYPE, ACPI_TYPE_DEVICE),
			DK_PROP_UINT32(KOSM_DEVICE_FLAGS, KOSM_FIND_MULTIPLE_CHILDREN),
			DK_PROP_END
		};
		dk_node* deviceNode;
		gDeviceKeeper->register_node(node, ACPI_DEVICE_MODULE_NAME, attrs,
			NULL, &deviceNode);
	}
	if ((AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON) == 0) {
		dprintf("registering sleep button\n");
		dk_property attrs[] = {
			DK_PROP_STRING(KOSM_DEVICE_BUS, "acpi"),
			DK_PROP_STRING(KOSM_ACPI_DEVICE_HID, "ACPI_FSB"),
			DK_PROP_UINT32(KOSM_ACPI_DEVICE_TYPE, ACPI_TYPE_DEVICE),
			DK_PROP_UINT32(KOSM_DEVICE_FLAGS, KOSM_FIND_MULTIPLE_CHILDREN),
			DK_PROP_END
		};
		dk_node* deviceNode;
		gDeviceKeeper->register_node(node, ACPI_DEVICE_MODULE_NAME, attrs,
			NULL, &deviceNode);
	}

	acpi_enumerate_child_devices(node, "\\");

	*_cookie = node;
	return B_OK;
}


static void
acpi_root_detach(void* cookie)
{
}


static status_t
acpi_std_ops_stub(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			module_info* module;
			return get_module(B_ACPI_MODULE_NAME, &module);
				// this serializes our module initialization
		}

		case B_MODULE_UNINIT:
			return put_module(B_ACPI_MODULE_NAME);
	}

	return B_BAD_VALUE;
}


struct acpi_root_info gACPIRootInterface = {
	get_handle,
	get_name,
	acquire_global_lock,
	release_global_lock,
	install_notify_handler,
	remove_notify_handler,
	update_all_gpes,
	enable_gpe,
	disable_gpe,
	clear_gpe,
	set_gpe,
	finish_gpe,
	install_gpe_handler,
	remove_gpe_handler,
	install_address_space_handler,
	remove_address_space_handler,
	enable_fixed_event,
	disable_fixed_event,
	fixed_event_status,
	reset_fixed_event,
	install_fixed_event_handler,
	remove_fixed_event_handler,
	get_next_entry,
	get_next_object,
	get_device,
	get_device_info,
	get_object_type,
	get_object,
	get_object_typed,
	ns_handle_to_pathname,
	evaluate_object,
	evaluate_method,
	get_irq_routing_table,
	get_current_resources,
	get_possible_resources,
	set_current_resources,
	walk_resources,
	prepare_sleep_state,
	enter_sleep_state,
	reboot,
	get_table
};


struct dk_driver_info gACPIRootDriver = {
	.info		= { ACPI_ROOT_MODULE_NAME, 0, acpi_std_ops_stub },
	.match		= &sACPIRootMatchDict,
	.attach		= acpi_root_attach,
	.detach		= acpi_root_detach,
	.node_flags	= KOSM_FIND_MULTIPLE_CHILDREN | KOSM_KEEP_DRIVER_LOADED,
};


module_info* modules[] = {
	(module_info*)&gACPIModule,
	(module_info*)&gACPIRootDriver,
	(module_info*)&gACPINsDumpDriver,
	(module_info*)&gACPIDeviceDriver,
	(module_info*)&embedded_controller_driver_module,
	(module_info*)&gACPICallDriver,
	NULL
};
