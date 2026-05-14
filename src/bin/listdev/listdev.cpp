/*
 * Copyright 2006-2012, Haiku, Inc. All Rights Reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 	Jérôme Duval
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drivers/device_keeper.h>
#include <drivers/module.h>
#include <drivers/bus/PCI.h>
#include <drivers/bus/SCSI.h>
#include <drivers/bus/USB.h>

extern "C" {
	#include "dm_wrapper.h"
	#include "pcihdr.h"
	#include "pci-utils.h"
	#include "usb-utils.h"
}


extern const char *__progname;

#define DUMP_MODE	0
#define USER_MODE	1
int gMode = USER_MODE;

#define BUS_ISA		1
#define BUS_PCI		2
#define BUS_SCSI 	3
#define BUS_USB		4


static const char *
get_scsi_device_type(uint8 type)
{
	switch (type) {
		case 0x0: return "Direct Access";
		case 0x1: return "Sequential Access";
		case 0x2: return "Printer";
		case 0x3: return "Processor";
		case 0x4: return "WORM";
		case 0x5: return "CDROM";
		case 0x6: return "Scanner";
		case 0x7: return "Optical memory";
		case 0x8: return "Medium changer";
		case 0x9: return "Communication";
		case 0xc: return "Storage array controller";
		case 0xd: return "Enclosure services";
		case 0xe: return "Simplified Direct Access";
		default: return "";
	}
}


static void
usage()
{
	fprintf(stderr, "usage: %s [-d]\n", __progname);
	fprintf(stderr, "Displays devices in a user friendly way\n");
	fprintf(stderr, "-d : dumps the tree\n");
	exit(0);
}


static void
put_level(int32 level)
{
	while (level-- > 0)
		printf("   ");
}


// Helper: get a string property, returns empty string on failure.
static bool
get_string_prop(kosm_handle_t node, const char* name, char* buf, size_t bufSize)
{
	kosm_dk_prop_value val;
	if (dm_get_property(node, name, &val) == B_OK && val.type == B_STRING_TYPE) {
		strlcpy(buf, val.value.string, bufSize);
		return true;
	}
	buf[0] = '\0';
	return false;
}


static bool
get_uint8_prop(kosm_handle_t node, const char* name, uint8* out)
{
	kosm_dk_prop_value val;
	if (dm_get_property(node, name, &val) == B_OK && val.type == B_UINT8_TYPE) {
		*out = val.value.ui8;
		return true;
	}
	return false;
}


static bool
get_uint16_prop(kosm_handle_t node, const char* name, uint16* out)
{
	kosm_dk_prop_value val;
	if (dm_get_property(node, name, &val) == B_OK && val.type == B_UINT16_TYPE) {
		*out = val.value.ui16;
		return true;
	}
	return false;
}


static bool
get_uint32_prop(kosm_handle_t node, const char* name, uint32* out)
{
	kosm_dk_prop_value val;
	if (dm_get_property(node, name, &val) == B_OK && val.type == B_UINT32_TYPE) {
		*out = val.value.ui32;
		return true;
	}
	return false;
}


static void
dump_device(kosm_handle_t node, uint8 level)
{
	put_level(level);
	printf("(%d)", level);

	// Try to print pretty name and bus
	char pretty[128];
	if (get_string_prop(node, KOSM_LABEL, pretty, sizeof(pretty)))
		printf(" \"%s\"", pretty);

	char bus[64];
	if (get_string_prop(node, KOSM_DEVICE_BUS, bus, sizeof(bus)))
		printf(" [bus=%s]", bus);

	uint16 vid, did;
	if (get_uint16_prop(node, KOSM_DEVICE_VENDOR_ID, &vid))
		printf(" vendor=%04x", vid);
	if (get_uint16_prop(node, KOSM_DEVICE_ID, &did))
		printf(" device=%04x", did);

	printf("\n");
}


static void
dump_nodes(kosm_handle_t node, uint8 level)
{
	dump_device(node, level);

	kosm_handle_t child;
	if (get_child(node, &child) != B_OK)
		return;

	do {
		dump_nodes(child, level + 1);
		kosm_handle_t next;
		if (get_next_child(node, child, &next) != B_OK) {
			close_node(child);
			break;
		}
		close_node(child);
		child = next;
	} while (true);
}


static int32
display_device(kosm_handle_t node, uint8 level)
{
	uint8 new_level = level;

	char device_bus[64] = {};
	uint8 scsi_path_id = 255;
	int bus = 0;
	uint16 vendor_id = 0, device_id = 0;

	uint8 pci_class_base_id = 0, pci_class_sub_id = 0, pci_class_api_id = 0;
	uint16 pci_subsystem_vendor_id = 0, pci_subsystem_id = 0;

	uint8 scsi_target_lun = 0, scsi_target_id = 0, scsi_type = 255;
	char scsi_vendor[64] = {}, scsi_product[64] = {};

	uint8 usb_class_base_id = 0, usb_class_sub_id = 0, usb_class_proto_id = 0;

	get_string_prop(node, KOSM_DEVICE_BUS, device_bus, sizeof(device_bus));
	get_uint8_prop(node, "scsi/path_id", &scsi_path_id);
	get_uint16_prop(node, KOSM_DEVICE_TYPE, &vendor_id);
	pci_class_base_id = (uint8)vendor_id; vendor_id = 0;
	get_uint16_prop(node, KOSM_DEVICE_SUB_TYPE, &device_id);
	pci_class_sub_id = (uint8)device_id; device_id = 0;
	uint16 tmp16;
	if (get_uint16_prop(node, KOSM_DEVICE_INTERFACE, &tmp16))
		pci_class_api_id = (uint8)tmp16;
	get_uint16_prop(node, KOSM_DEVICE_VENDOR_ID, &vendor_id);
	get_uint16_prop(node, KOSM_DEVICE_ID, &device_id);
	get_uint8_prop(node, SCSI_DEVICE_TARGET_LUN_ITEM, &scsi_target_lun);
	get_uint8_prop(node, SCSI_DEVICE_TARGET_ID_ITEM, &scsi_target_id);
	get_uint8_prop(node, SCSI_DEVICE_TYPE_ITEM, &scsi_type);
	get_string_prop(node, SCSI_DEVICE_VENDOR_ITEM, scsi_vendor, sizeof(scsi_vendor));
	get_string_prop(node, SCSI_DEVICE_PRODUCT_ITEM, scsi_product, sizeof(scsi_product));
	get_uint8_prop(node, USB_DEVICE_CLASS, &usb_class_base_id);
	get_uint8_prop(node, USB_DEVICE_SUBCLASS, &usb_class_sub_id);
	get_uint8_prop(node, USB_DEVICE_PROTOCOL, &usb_class_proto_id);

	if (!strcmp(device_bus, "isa"))
		bus = BUS_ISA;
	else if (!strcmp(device_bus, "pci"))
		bus = BUS_PCI;
	else if (!strcmp(device_bus, "usb"))
		bus = BUS_USB;
	else if (scsi_path_id < 255)
		bus = BUS_SCSI;

	switch (bus) {
		case BUS_ISA:
			new_level = level + 1;
			break;
		case BUS_PCI:
			printf("\n");
			{
				char classInfo[128];
				get_class_info(pci_class_base_id, pci_class_sub_id,
					pci_class_api_id, classInfo, 64);
				put_level(level);
				printf("device %s [%x|%x|%x]\n", classInfo, pci_class_base_id,
					pci_class_sub_id, pci_class_api_id);
			}

			put_level(level);
			printf("  ");
			{
				const char *venShort, *venFull, *devShort, *devFull;
				get_vendor_info(vendor_id, &venShort, &venFull);
				if (!venShort && !venFull)
					printf("vendor %04x: Unknown\n", vendor_id);
				else if (venShort && venFull)
					printf("vendor %04x: %s - %s\n", vendor_id, venShort, venFull);
				else
					printf("vendor %04x: %s\n", vendor_id, venShort ? venShort : venFull);

				put_level(level);
				printf("  ");
				get_device_info(vendor_id, device_id, pci_subsystem_vendor_id,
					pci_subsystem_id, &devShort, &devFull);
				if (!devShort && !devFull)
					printf("device %04x: Unknown\n", device_id);
				else if (devShort && devFull)
					printf("device %04x: %s (%s)\n", device_id, devShort, devFull);
				else
					printf("device %04x: %s\n", device_id, devShort ? devShort : devFull);
			}
			new_level = level + 1;
			break;
		case BUS_SCSI:
			if (scsi_type == 255)
				break;
			put_level(level);
			printf("  device [%x|%x]\n", scsi_target_id, scsi_target_lun);
			put_level(level);
			printf("  vendor %15s\tmodel %15s\ttype %s\n", scsi_vendor,
				scsi_product, get_scsi_device_type(scsi_type));
			new_level = level + 1;
			break;
		case BUS_USB:
			{
				printf("\n");
				char classInfo[128];
				usb_get_class_info(usb_class_base_id, usb_class_sub_id,
					usb_class_proto_id, classInfo, sizeof(classInfo));
				put_level(level);
				printf("device %s [%x|%x|%x]\n", classInfo, usb_class_base_id,
					usb_class_sub_id, usb_class_proto_id);
				put_level(level);
				printf("  ");
				const char* vendorName = NULL;
				const char* deviceName = NULL;
				usb_get_vendor_info(vendor_id, &vendorName);
				usb_get_device_info(vendor_id, device_id, &deviceName);
				printf("vendor %04x: %s\n", vendor_id,
					vendorName != NULL ? vendorName : "Unknown");
				put_level(level);
				printf("  ");
				printf("device %04x: %s\n", device_id,
					deviceName != NULL ? deviceName : "Unknown");
				new_level = level + 1;
				break;
			}
	}

	return new_level;
}


static void
display_nodes(kosm_handle_t node, uint8 level)
{
	level = display_device(node, level);

	kosm_handle_t child;
	if (get_child(node, &child) != B_OK)
		return;

	do {
		display_nodes(child, level);
		kosm_handle_t next;
		if (get_next_child(node, child, &next) != B_OK) {
			close_node(child);
			break;
		}
		close_node(child);
		child = next;
	} while (true);
}


int
main(int argc, char **argv)
{
	status_t error;
	kosm_handle_t root;

	if ((error = init_dm_wrapper()) < 0) {
		printf("Error initializing DeviceKeeper (%s)\n", strerror(error));
		return error;
	}

	if (argc > 2)
		usage();

	if (argc == 2) {
		if (!strcmp(argv[1], "-d")) {
			gMode = DUMP_MODE;
		} else {
			usage();
		}
	}

	if (get_root(&root) != B_OK) {
		printf("Error: cannot get root node\n");
		return 1;
	}

	if (gMode == DUMP_MODE) {
		dump_nodes(root, 0);
	} else {
		display_nodes(root, 0);
	}

	close_node(root);
	uninit_dm_wrapper();

	return 0;
}
