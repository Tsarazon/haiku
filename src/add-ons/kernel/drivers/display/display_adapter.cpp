#include "display_adapter.h"


typedef struct acpi_ns_device_info {
	dk_node *node;
	acpi_handle acpi_device;
} displayadapter_device_info;


dk_keeper_info *gDeviceKeeper = NULL;
acpi_module_info *gAcpi = NULL;


/*

TODO: ACPI Spec 5 Appendix B: Implement:
_DOS Method to control display output switching
(  _DOD Method to retrieve information about child output devices
	- You can already do this by listing child devices )
_ROM Method to retrieve the ROM image for this device
_GPD Method for determining which VGA device will post
_SPD Method for controlling which VGA device will post
_VPO Method for determining the post options

Display cycling notifications

*/


//	#pragma mark - dk_device_ops


static status_t
displayadapter_open(void *_cookie, const char *path, int flags, void** cookie)
{
	displayadapter_device_info *device = (displayadapter_device_info *)_cookie;

	if (device->acpi_device == NULL) {
		// First open — resolve ACPI handle
		char acpiPath[256];
		if (gDeviceKeeper->get_property_string(device->node,
				KOSM_ACPI_DEVICE_PATH, acpiPath, sizeof(acpiPath),
				NULL, false) != B_OK
			|| gAcpi->get_handle(NULL, acpiPath, &device->acpi_device)
				!= B_OK) {
			dprintf("%s: failed to get acpi node.\n", __func__);
			return B_ERROR;
		}
	}

	*cookie = device;
	return B_OK;
}


static status_t
displayadapter_read(void* _cookie, off_t position, void *buf,
	size_t* num_bytes)
{
	return B_ERROR;
}


static status_t
displayadapter_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	return B_ERROR;
}


static status_t
displayadapter_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	return B_ERROR;
}


static status_t
displayadapter_close(void* cookie)
{
	return B_OK;
}


static status_t
displayadapter_free(void* cookie)
{
	return B_OK;
}


static dk_device_ops sAdapterDeviceOps = {
	displayadapter_open,
	displayadapter_close,
	displayadapter_free,
	displayadapter_read,
	displayadapter_write,
	NULL,	// io
	displayadapter_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


//	#pragma mark - child display enumeration


static status_t
register_displays(const char *parentName, dk_node *node)
{
	acpi_handle acpiHandle;
	char path[256];

	if (gDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_PATH,
			path, sizeof(path), NULL, true) != B_OK
		|| gAcpi->get_handle(NULL, path, &acpiHandle) != B_OK) {
		dprintf("%s: failed to get acpi node.\n", __func__);
		return B_ERROR;
	}

	// get list of ids from _DOD
	acpi_object_type *pkgData = (acpi_object_type *)malloc(128);
	if (pkgData == NULL)
		return B_ERROR;

	status_t status = gAcpi->evaluate_object(acpiHandle, "_DOD", NULL,
		pkgData, 128);
	if (status != B_OK || pkgData->object_type != ACPI_TYPE_PACKAGE) {
		dprintf("%s: fail. %" B_PRId32 " %" B_PRIu32 "\n", __func__, status,
			pkgData->object_type);
		free(pkgData);
		return status;
	}

	acpi_object_type *displayIDs = pkgData->package.objects;
	for (uint32 i = 0; i < pkgData->package.count; i++) {
		dprintf("Display ID = %" B_PRIu64 "\n",
			displayIDs[i].integer.integer);
	}

	acpi_object_type result;
	acpi_handle child = NULL;
	while (gAcpi->get_next_object(ACPI_TYPE_DEVICE, acpiHandle, &child)
		== B_OK) {
		char name[5] = {0};
		if (gAcpi->get_name(child, 1, name, 5) == B_OK)
			dprintf("name: %s\n", name);
		if (gAcpi->evaluate_object(child, "_ADR", NULL, &result,
				sizeof(result)) != B_OK)
			continue;

		dprintf("Child _adr %" B_PRIu64 "\n", result.integer.integer);
		uint32 i;
		for (i = 0; i < pkgData->package.count; i++)
			if (displayIDs[i].integer.integer == result.integer.integer)
				break;

		if (i == pkgData->package.count)
			continue;

		// Register child display node — this is a bus-like operation,
		// the display_device driver will attach to the child node.
		dk_property props[] = {
			{ KOSM_LABEL, B_STRING_TYPE, { .string = name } },
			{ KOSM_DEVICE_FLAGS, B_UINT32_TYPE,
				{ .ui32 = KOSM_KEEP_DRIVER_LOADED } },
			{}
		};

		dk_node *deviceNode = NULL;
		gDeviceKeeper->register_node(node, DISPLAY_DEVICE_MODULE_NAME,
			props, NULL, &deviceNode);

		char deviceName[B_PATH_NAME_LENGTH];
		snprintf(deviceName, sizeof(deviceName), "%s/%s", parentName, name);
		gDeviceKeeper->publish_device(node, deviceName, &sAdapterDeviceOps);
	}

	free(pkgData);
	return B_OK;
}


//	#pragma mark - dk_driver_info


static const dk_match_rule sAdapterMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" } },
	{}
};

static const dk_match_dict sAdapterMatchDict = {
	sAdapterMatchRules,
	0
};


static float
displayadapter_probe(dk_node *node)
{
	acpi_handle handle, method;
	char path[256];
	uint32 device_type;

	if (gDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_PATH,
			path, sizeof(path), NULL, false) != B_OK)
		return 0.0;

	// check whether it's really a device
	if (gDeviceKeeper->get_property_uint32(node, KOSM_ACPI_DEVICE_TYPE,
			&device_type, false) != B_OK
		|| device_type != ACPI_TYPE_DEVICE) {
		return 0.0;
	}

	if (gAcpi->get_handle(NULL, path, &handle) != B_OK)
		return 0.0;

	if (gAcpi->get_handle(handle, "_DOD", &method) != B_OK ||
		gAcpi->get_handle(handle, "_DOS", &method) != B_OK) {
		return 0.0;
	}

	dprintf("%s: found at path: %s\n", __func__, path);
	return 0.6;
}


static status_t
displayadapter_attach(dk_node *node, void **_cookie)
{
	displayadapter_device_info *device =
		(displayadapter_device_info *)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	device->node = node;

	// Publish the adapter device
	int path_id = gDeviceKeeper->create_id(DISPLAYADAPTER_PATHID_GENERATOR);
	if (path_id < 0) {
		dprintf("displayadapter_attach: error creating path\n");
		free(device);
		return B_ERROR;
	}

	char name[B_PATH_NAME_LENGTH];
	snprintf(name, sizeof(name), DISPLAYADAPTER_BASENAME, path_id);
	status_t status = gDeviceKeeper->publish_device(node, name,
		&sAdapterDeviceOps);

	if (status == B_OK)
		register_displays(name, node);

	*_cookie = device;
	return B_OK;
}


static void
displayadapter_detach(void *_cookie)
{
	displayadapter_device_info *device =
		(displayadapter_device_info *)_cookie;
	free(device);
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ B_ACPI_MODULE_NAME, (module_info **)&gAcpi},
	{}
};


dk_driver_info displayadapter_driver_module = {
	.info = {
		DISPLAYADAPTER_MODULE_NAME,
		0,
		NULL
	},
	.match	= &sAdapterMatchDict,
	.probe	= displayadapter_probe,
	.attach	= displayadapter_attach,
	.detach	= displayadapter_detach,
	.ops	= &sAdapterDeviceOps,
};


module_info *modules[] = {
	(module_info *)&display_device_driver,
	(module_info *)&displayadapter_driver_module,
	NULL
};
