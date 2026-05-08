/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Distributed under the terms of the MIT license.
 */


#define DRIVER_NAME "wmi_acpi"
#include "WMIPrivate.h"


#define ACPI_NAME_ACPI_WMI "PNP0C14"

#define ACPI_WMI_REGFLAG_EXPENSIVE	(1 << 0)
#define ACPI_WMI_REGFLAG_METHOD		(1 << 1)
#define ACPI_WMI_REGFLAG_STRING		(1 << 2)
#define ACPI_WMI_REGFLAG_EVENT		(1 << 3)


dk_keeper_info *gDeviceKeeper;
smbios_module_info *gSMBios;


acpi_status wmi_acpi_adr_space_handler(uint32 function,
	acpi_physical_address address, uint32 bitWidth, int *value,
	void *handlerContext, void *regionContext)
{
	return B_OK;
}


WMIACPI::WMIACPI(dk_node *node)
	:
	fNode(node)
{
	CALLED();

	fStatus = gDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void **)&acpi, (void **)&acpi_cookie);
	if (fStatus != B_OK) {
		ERROR("failed to get ACPI interface\n");
		return;
	}
	char uidBuf[64];
	dk_node *parent = gDeviceKeeper->get_parent_node(node);
	if (parent != NULL
		&& gDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_UID,
			uidBuf, sizeof(uidBuf), NULL, false) == B_OK)
		fUid = strdup(uidBuf);
	else
		fUid = NULL;
	if (parent != NULL)
		gDeviceKeeper->put_node(parent);

	// install notify handler
	fStatus = acpi->install_notify_handler(acpi_cookie,
		ACPI_ALL_NOTIFY, _NotifyHandler, this);
	if (fStatus != B_OK) {
		ERROR("install_notify_handler failed\n");
		return;
	}

	fStatus = acpi->install_address_space_handler(acpi_cookie,
		ACPI_ADR_SPACE_EC, wmi_acpi_adr_space_handler, NULL, this);
	if (fStatus != B_OK) {
		ERROR("wmi_acpi_adr_space_handler failed\n");
		return;
	}

	acpi_data buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	fStatus = acpi->evaluate_method(acpi_cookie, "_WDG", NULL, &buffer);
	if (fStatus != B_OK) {
		ERROR("Method call _WDG failed\n");
		return;
	}

	acpi_object_type* object = (acpi_object_type*)buffer.pointer;
	fWMIInfoCount = object->buffer.length / sizeof(guid_info);
	guid_info *info = (guid_info*)object->buffer.buffer;
	fWMIInfos = (wmi_info *)calloc(fWMIInfoCount, sizeof(wmi_info));
	TRACE("found %" B_PRIu32 " objects\n", fWMIInfoCount);
	for (uint32 i = 0; i < fWMIInfoCount; i++, info++) {
		wmi_info *wmi = &fWMIInfos[i];
		wmi->guid = *info;
		fList.Add(wmi);
	}
	free(object);
}


WMIACPI::~WMIACPI()
{
	free(fWMIInfos);

	acpi->remove_notify_handler(acpi_cookie,
		ACPI_ALL_NOTIFY, _NotifyHandler);
}


status_t
WMIACPI::InitCheck()
{
	return fStatus;
}


status_t
WMIACPI::Scan()
{
	CALLED();
	status_t status;
	wmi_info* wmiInfo = NULL;
	uint32 index = 0;
	for (WMIInfoList::Iterator it = fList.GetIterator();
			(wmiInfo = it.Next()) != NULL; index++) {
		uint8* guid = wmiInfo->guid.guid;
		char guidString[37] = {};
		_GuidToGuidString(guid, guidString);
		dk_property attrs[] = {
			// connection
			{ WMI_GUID_STRING_ITEM, B_STRING_TYPE, { .string = guidString }},

			{ WMI_BUS_COOKIE, B_UINT32_TYPE, { .ui32 = index }},

			// description of peripheral drivers
			{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "wmi" }},

			{ KOSM_DEVICE_FLAGS, B_UINT32_TYPE,
				{ .ui32 = KOSM_FIND_MULTIPLE_CHILDREN }},

			{ NULL }
		};

		status = gDeviceKeeper->register_node(fNode, WMI_DEVICE_MODULE_NAME,
			attrs, NULL, NULL);
		if (status != B_OK)
			return status;
	}

	return B_OK;

}


status_t
WMIACPI::GetBlock(uint32 busCookie, uint8 instance, uint32 methodId,
	acpi_data* out)
{
	CALLED();
	if (busCookie >= fWMIInfoCount)
		return B_BAD_VALUE;
	wmi_info* info = &fWMIInfos[busCookie];
	if ((info->guid.flags & ACPI_WMI_REGFLAG_METHOD) != 0
		|| (info->guid.flags & ACPI_WMI_REGFLAG_EVENT) != 0) {
		return B_BAD_VALUE;
	} else if (instance > info->guid.max_instance)
		return B_BAD_VALUE;

	char method[5] = "WQ";
	strncat(method, info->guid.oid, 2);
	char wcMethod[5] = "WC";
	strncat(wcMethod, info->guid.oid, 2);
	status_t wcStatus = B_OK;
	status_t status = B_OK;

	if ((info->guid.flags & ACPI_WMI_REGFLAG_EXPENSIVE) != 0)
		 wcStatus = _EvaluateMethodSimple(wcMethod, 1);

	acpi_object_type object;
	object.object_type = ACPI_TYPE_INTEGER;
	object.integer.integer = instance;
	acpi_objects objects = { 1, &object};
	TRACE("GetBlock calling %s\n", method);
	status = acpi->evaluate_method(acpi_cookie, method, &objects, out);

	if ((info->guid.flags & ACPI_WMI_REGFLAG_EXPENSIVE) != 0
		&& wcStatus == B_OK) {
		 _EvaluateMethodSimple(wcMethod, 0);
	}

	return status;
}


status_t
WMIACPI::SetBlock(uint32 busCookie, uint8 instance, uint32 methodId,
	const acpi_data* in)
{
	CALLED();
	if (busCookie >= fWMIInfoCount)
		return B_BAD_VALUE;
	wmi_info* info = &fWMIInfos[busCookie];
	if ((info->guid.flags & ACPI_WMI_REGFLAG_METHOD) != 0
		|| (info->guid.flags & ACPI_WMI_REGFLAG_EVENT) != 0) {
		return B_BAD_VALUE;
	} else if (instance > info->guid.max_instance)
		return B_BAD_VALUE;

	char method[5] = "WS";
	strncat(method, info->guid.oid, 2);

	acpi_object_type object[2];
	object[0].object_type = ACPI_TYPE_INTEGER;
	object[0].integer.integer = instance;
	object[1].object_type = ACPI_TYPE_BUFFER;
	if ((info->guid.flags & ACPI_WMI_REGFLAG_STRING) != 0)
		object[1].object_type = ACPI_TYPE_STRING;
	object[1].buffer.buffer = in->pointer;
	object[1].buffer.length = in->length;
	acpi_objects objects = { 2, object};
	TRACE("SetBlock calling %s\n", method);
	return acpi->evaluate_method(acpi_cookie, method, &objects, NULL);
}


status_t
WMIACPI::EvaluateMethod(uint32 busCookie, uint8 instance, uint32 methodId,
	const acpi_data* in, acpi_data* out)
{
	CALLED();
	if (busCookie >= fWMIInfoCount)
		return B_BAD_VALUE;
	wmi_info* info = &fWMIInfos[busCookie];
	if ((info->guid.flags & ACPI_WMI_REGFLAG_METHOD) == 0)
		return B_BAD_VALUE;

	char method[5] = "WM";
	strncat(method, info->guid.oid, 2);

	acpi_object_type object[3];
	object[0].object_type = ACPI_TYPE_INTEGER;
	object[0].integer.integer = instance;
	object[1].object_type = ACPI_TYPE_INTEGER;
	object[1].integer.integer = methodId;
	uint32 count = 2;
	if (in != NULL) {
		object[2].object_type = ACPI_TYPE_BUFFER;
		if ((info->guid.flags & ACPI_WMI_REGFLAG_STRING) != 0)
			object[2].object_type = ACPI_TYPE_STRING;
		object[2].buffer.buffer = in->pointer;
		object[2].buffer.length = in->length;
		count++;
	}
	acpi_objects objects = { count, object};
	TRACE("EvaluateMethod calling %s\n", method);
	return acpi->evaluate_method(acpi_cookie, method, &objects, out);
}


status_t
WMIACPI::InstallEventHandler(const char* guidString,
	acpi_notify_handler handler, void* context)
{
	CALLED();
	char string[37] = {};
	for (uint32 i = 0; i < fWMIInfoCount; i++) {
		wmi_info* info = &fWMIInfos[i];
		_GuidToGuidString(info->guid.guid, string);
		if (strcmp(guidString, string) == 0) {
			status_t status = B_OK;
			if (info->handler == NULL)
				status = _SetEventGeneration(info, true);
			if (status == B_OK) {
				info->handler = handler;
				info->handler_context = context;
			}
			return status;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
WMIACPI::RemoveEventHandler(const char* guidString)
{
	CALLED();
	char string[37] = {};
	for (uint32 i = 0; i < fWMIInfoCount; i++) {
		wmi_info* info = &fWMIInfos[i];
		_GuidToGuidString(info->guid.guid, string);
		if (strcmp(guidString, string) == 0) {
			status_t status = _SetEventGeneration(info, false);
			info->handler = NULL;
			info->handler_context = NULL;
			return status;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
WMIACPI::GetEventData(uint32 notify, acpi_data* out)
{
	CALLED();

	acpi_object_type object;
	object.object_type = ACPI_TYPE_INTEGER;
	object.integer.integer = notify;
	acpi_objects objects = { 1, &object };

	for (uint32 i = 0; i < fWMIInfoCount; i++) {
		wmi_info* info = &fWMIInfos[i];
		if (info->guid.notify_id == notify
			&& (info->guid.flags & ACPI_WMI_REGFLAG_EVENT) != 0) {
			return acpi->evaluate_method(acpi_cookie, "_WED", &objects, out);
		}
	}
	return B_ENTRY_NOT_FOUND;
}


const char*
WMIACPI::GetUid(uint32 busCookie)
{
	return fUid;
}


status_t
WMIACPI::_SetEventGeneration(wmi_info* info, bool enabled)
{
	char method[5];
	sprintf(method, "WE%02X", info->guid.notify_id);
	TRACE("_SetEventGeneration calling %s\n", method);
	status_t status = _EvaluateMethodSimple(method, enabled ? 1 : 0);
	// the method is allowed not to exist
	if (status == B_ERROR)
		status = B_OK;
	return status;
}


status_t
WMIACPI::_EvaluateMethodSimple(const char* method, uint64 integer)
{
	acpi_object_type object;
	object.object_type = ACPI_TYPE_INTEGER;
	object.integer.integer = integer;
	acpi_objects objects = { 1, &object};
	return acpi->evaluate_method(acpi_cookie, method, &objects, NULL);
}


void
WMIACPI::_NotifyHandler(acpi_handle device, uint32 value, void *context)
{
	WMIACPI* bus = (WMIACPI*)context;
	bus->_Notify(device, value);
}


void
WMIACPI::_Notify(acpi_handle device, uint32 value)
{
	for (uint32 i = 0; i < fWMIInfoCount; i++) {
		wmi_info* wmi = &fWMIInfos[i];
		if (wmi->guid.notify_id == value) {
			TRACE("_Notify found event 0x%" B_PRIx32 "\n", value);
			if (wmi->handler != NULL) {
				TRACE("_Notify found handler for event 0x%" B_PRIx32 "\n",
					value);
				wmi->handler(device, value, wmi->handler_context);
			}
			break;
		}
	}
}


void
WMIACPI::_GuidToGuidString(uint8 guid[16], char* guidString)
{
	sprintf(guidString,
		"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid[3], guid[2], guid[1], guid[0], guid[5], guid[4], guid[7], guid[6],
		guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14],
		guid[15]);
}


//	#pragma mark - driver module API


static float
wmi_acpi_support(dk_node *parent)
{
	CALLED();

	char bus[64];
	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "acpi") != 0)
		return -1.0f;

	uint32 device_type;
	if (gDeviceKeeper->get_property_uint32(parent, KOSM_DEVICE_TYPE,
			&device_type, false) != B_OK
		|| device_type != ACPI_TYPE_DEVICE) {
		return -1.0f;
	}

	char name[64];
	if (gDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_HID, name,
		sizeof(name), NULL, false) != B_OK || strcmp(name, ACPI_NAME_ACPI_WMI) != 0) {
		return -1.0f;
	}

	TRACE("found an acpi wmi device\n");

	return 0.6f;
}


static status_t
wmi_acpi_attach(dk_node *node, void **driverCookie)
{
	CALLED();
	WMIACPI* device = new(std::nothrow) WMIACPI(node);
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = device->Scan();
	if (status != B_OK) {
		delete device;
		return status;
	}

	*driverCookie = device;
	return B_OK;
}


static void
wmi_acpi_detach(void *driverCookie)
{
	CALLED();
	WMIACPI *device = (WMIACPI*)driverCookie;
	delete device;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ SMBIOS_MODULE_NAME, (module_info**)&gSMBios },
	{}
};


static const dk_match_rule sWMIACPIMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "acpi"),
	DK_MATCH_STRING(KOSM_ACPI_DEVICE_HID, ACPI_NAME_ACPI_WMI),
	DK_MATCH_END
};

static const dk_match_dict sWMIACPIMatchDict = {
	sWMIACPIMatchRules,
	0
};


static dk_driver_info sWMIACPIDriverModule = {
	.info       = { WMI_ACPI_DRIVER_NAME, 0, NULL },
	.match      = &sWMIACPIMatchDict,
	.probe      = wmi_acpi_support,
	.attach     = wmi_acpi_attach,
	.detach     = wmi_acpi_detach,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};


module_info *modules[] = {
	(module_info *)&sWMIACPIDriverModule,
	(module_info *)&gWMIAsusDriverModule,
	(module_info *)&gWMIDeviceModule,
	NULL
};
