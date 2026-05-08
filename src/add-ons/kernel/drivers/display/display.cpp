#include "display_adapter.h"


typedef struct acpi_ns_device_info {
	dk_node *node;
	acpi_handle acpi_device;
} display_device_info;
	

/*

TODO: ACPI Spec 5 Appendix B: Implement:
_BCL Brightness control levels
_BCM Brightness control method
_BQC Brightness Query Current Level
_DCS Get current hardware status
_DDC Return the EDID for this device
_DGS Query desired hardware active \ inactive state
_DSS Set hardware active \ inactive state

Brightness notifications

*/


//	#pragma mark - dk_device_ops

	
static status_t
display_open(void *_cookie, const char* path, int flags, void** cookie)
{
	display_device_info *device = (display_device_info *)_cookie;
	*cookie = device;
	return B_OK;
}


static status_t
display_read(void *_cookie, off_t position, void *buf, size_t* num_bytes)
{
	return B_ERROR;
}


static status_t
display_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	*num_bytes = 0;
	return B_ERROR;
}


static status_t
display_control(void* cookie, uint32 op, void* arg, size_t len)
{
	return B_ERROR;
}


static status_t
display_close(void* cookie)
{
	return B_OK;
}


static status_t
display_free(void* cookie)
{
	display_device_info *device = (display_device_info *)cookie;
	(void)device;
	return B_OK;
}


static dk_device_ops sDisplayDeviceOps = {
	display_open,
	display_close,
	display_free,
	display_read,
	display_write,
	NULL,	// io
	display_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


//	#pragma mark - driver module API


static float
display_probe(dk_node *node)
{
	// Display child devices are registered by the parent display_adapter
	// driver, so this probe always succeeds when called on a matching node.
	return 0.6;
}


static status_t
display_attach(dk_node *node, void **_cookie)
{
	display_device_info *device = 
		(display_device_info *)calloc(1, sizeof(*device));

	if (device == NULL)
		return B_NO_MEMORY;

	device->node = node;

	char path[256];
	if (gDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_PATH,
			path, sizeof(path), NULL, false) != B_OK
		|| gAcpi->get_handle(NULL, path, &device->acpi_device) != B_OK) {
		dprintf("%s: failed to get acpi node.\n", __func__);
		free(device);
		return B_ERROR;
	}

	*_cookie = device;
	return B_OK;
}


static void
display_detach(void *_cookie)
{
	display_device_info *device = (display_device_info *)_cookie;
	free(device);
}


dk_driver_info display_device_driver = {
	.info = {
		DISPLAY_DEVICE_MODULE_NAME,
		0,
		NULL
	},
	.probe	= display_probe,
	.attach	= display_attach,
	.detach	= display_detach,
	.ops	= &sDisplayDeviceOps,
};
