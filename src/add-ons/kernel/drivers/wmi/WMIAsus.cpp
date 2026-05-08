/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Distributed under the terms of the MIT license.
 */


#define DRIVER_NAME "wmi_asus"
#include "WMIPrivate.h"


#define ACPI_ASUS_WMI_MGMT_GUID		"97845ED0-4E6D-11DE-8A39-0800200C9A66"
#define ACPI_ASUS_WMI_EVENT_GUID	"0B3CBB35-E3C2-45ED-91C2-4C5A6D195D1C"
#define ACPI_ASUS_UID_ASUSWMI		"ASUSWMI"

#define ASUS_WMI_METHODID_INIT		0x54494E49
#define ASUS_WMI_METHODID_SPEC		0x43455053
#define ASUS_WMI_METHODID_SFUN		0x4E554653
#define ASUS_WMI_METHODID_DCTS		0x53544344
#define ASUS_WMI_METHODID_DSTS		0x53545344
#define ASUS_WMI_METHODID_DEVS		0x53564544


#define ASUS_WMI_DEVID_ALS_ENABLE		0x00050001
#define ASUS_WMI_DEVID_BRIGHTNESS		0x00050012
#define ASUS_WMI_DEVID_KBD_BACKLIGHT	0x00050021
#define ASUS_WMI_DEVID_FN_LOCK			0x00100023

#define ASUS_WMI_DSTS_PRESENCE_BIT		0x10000


class WMIAsus {
public:
								WMIAsus(dk_node *node);
								~WMIAsus();

private:
			status_t 			_EvaluateMethod(uint32 methodId, uint32 arg0,
									uint32 arg1, uint32 *returnValue);
			status_t			_GetDevState(uint32 devId, uint32* value);
			status_t			_SetDevState(uint32 devId, uint32 arg,
									uint32* value);
	static	void				_NotifyHandler(acpi_handle handle,
									uint32 notify, void *context);
			void				_Notify(acpi_handle handle, uint32 notify);
			status_t			_EnableAls(uint32 enable);
private:
			dk_node*		fNode;
			wmi_device_interface* wmi;
			wmi_device			wmi_cookie;
			uint32				fDstsID;
			const char*			fEventGuidString;
			bool				fEnableALS;
			bool				fFnLock;
};



WMIAsus::WMIAsus(dk_node *node)
	:
	fNode(node),
	fDstsID(ASUS_WMI_METHODID_DSTS),
	fEventGuidString(NULL),
	fEnableALS(false),
	fFnLock(true)
{
	CALLED();

	status_t status = gDeviceKeeper->get_interface(node,
		WMI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void **)&wmi, (void **)&wmi_cookie);
	if (status != B_OK) {
		ERROR("failed to get WMI device interface\n");
		return;
	}

	const char* uid = wmi->get_uid(wmi_cookie);
	if (uid != NULL && strcmp(uid, ACPI_ASUS_UID_ASUSWMI) == 0)
		fDstsID = ASUS_WMI_METHODID_DCTS;
	TRACE("WMIAsus using _UID %s\n", uid);
	uint32 value;
	if (_EvaluateMethod(ASUS_WMI_METHODID_INIT, 0, 0, &value) == B_OK) {
		TRACE("_INIT: %x\n", value);
	}
	if (_EvaluateMethod(ASUS_WMI_METHODID_SPEC, 0, 9, &value) == B_OK) {
		TRACE("_SPEC: %x\n", value);
	}
	if (_EvaluateMethod(ASUS_WMI_METHODID_SFUN, 0, 0, &value) == B_OK) {
		TRACE("_SFUN: %x\n", value);
	}

	// some ASUS laptops need to be ALS forced
	fEnableALS =
		gSMBios->match_vendor_product("ASUSTeK COMPUTER INC.", "UX430UAR");
	if (fEnableALS && _EnableAls(1) == B_OK) {
		TRACE("ALSC enabled\n");
	}

	if (_GetDevState(ASUS_WMI_DEVID_FN_LOCK, &value) == B_OK
		&& (value & ASUS_WMI_DSTS_PRESENCE_BIT) != 0) {
		// set fn lock
		_SetDevState(ASUS_WMI_DEVID_FN_LOCK, fFnLock, NULL);
	}

	// install event handler
	if (wmi->install_event_handler(wmi_cookie, ACPI_ASUS_WMI_EVENT_GUID,
		_NotifyHandler, this) == B_OK) {
		fEventGuidString = ACPI_ASUS_WMI_EVENT_GUID;
	}
}


WMIAsus::~WMIAsus()
{
	// for ALS
	if (fEnableALS && _EnableAls(0) == B_OK) {
		TRACE("ALSC disabled\n");
	}

	if (fEventGuidString != NULL)
		wmi->remove_event_handler(wmi_cookie, fEventGuidString);
}


status_t
WMIAsus::_EvaluateMethod(uint32 methodId, uint32 arg0, uint32 arg1,
	uint32 *returnValue)
{
	CALLED();
	uint32 params[] = { arg0, arg1, 0, 0, 0 };
	acpi_data inBuffer = { sizeof(params), params };
	acpi_data outBuffer = { ACPI_ALLOCATE_BUFFER, NULL };
	status_t status = wmi->evaluate_method(wmi_cookie, 0, methodId, &inBuffer,
		&outBuffer);
	if (status != B_OK)
		return status;

	acpi_object_type* object = (acpi_object_type*)outBuffer.pointer;
	uint32 result = 0;
	if (object != NULL) {
		if (object->object_type == ACPI_TYPE_INTEGER)
			result = object->integer.integer;
		free(object);
	}
	if (returnValue != NULL)
		*returnValue = result;

	return B_OK;
}


status_t
WMIAsus::_EnableAls(uint32 enable)
{
	CALLED();
	return _SetDevState(ASUS_WMI_DEVID_ALS_ENABLE, enable, NULL);
}


status_t
WMIAsus::_GetDevState(uint32 devId, uint32 *value)
{
	return _EvaluateMethod(fDstsID, devId, 0, value);
}


status_t
WMIAsus::_SetDevState(uint32 devId, uint32 arg, uint32 *value)
{
	return _EvaluateMethod(ASUS_WMI_METHODID_DEVS, devId, arg, value);
}


void
WMIAsus::_NotifyHandler(acpi_handle handle, uint32 notify, void *context)
{
	WMIAsus* device = (WMIAsus*)context;
	device->_Notify(handle, notify);
}


void
WMIAsus::_Notify(acpi_handle handle, uint32 notify)
{
	CALLED();

	acpi_data response = { ACPI_ALLOCATE_BUFFER, NULL };
	wmi->get_event_data(wmi_cookie, notify, &response);

	acpi_object_type* object = (acpi_object_type*)response.pointer;
	uint32 result = 0;
	if (object != NULL) {
		if (object->object_type == ACPI_TYPE_INTEGER)
			result = object->integer.integer;
		free(object);
	}
	if (result != 0) {
		if (result == 0x4e) {
			TRACE("WMIAsus::_Notify() keyboard fnlock key\n");
			fFnLock = !fFnLock;
			_SetDevState(ASUS_WMI_DEVID_FN_LOCK, fFnLock, NULL);
			TRACE("WMIAsus::_Notify() keyboard fnlock key %" B_PRIx32 "\n",
				fFnLock);
		} else if (result == 0xc4 || result == 0xc5 || result == 0xc7) {
			TRACE("WMIAsus::_Notify() keyboard backlight key\n");
			uint32 value;
			if (_GetDevState(ASUS_WMI_DEVID_KBD_BACKLIGHT, &value) == B_OK) {
				TRACE("WMIAsus::_Notify() getkeyboard backlight key %"
					B_PRIx32 "\n", value);
				value &= 0x7;
				if (result == 0xc4) {
					if (value < 3)
						value++;
				} else if (result == 0xc5) {
					if (value > 0)
						value--;
				} else {
					value++;
					value &= 0x3;
				}
				TRACE("WMIAsus::_Notify() set keyboard backlight key %"
					B_PRIx32 "\n", value);
				_SetDevState(ASUS_WMI_DEVID_KBD_BACKLIGHT, value | 0x80, NULL);
			}
		} else if (result == 0x6b) {
			TRACE("WMIAsus::_Notify() touchpad control\n");
		} else {
			TRACE("WMIAsus::_Notify() key 0x%" B_PRIx32 "\n", result);
		}
	}
}


//	#pragma mark - driver module API


static float
wmi_asus_support(dk_node *parent)
{
	char bus[64];
	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "wmi") != 0)
		return -1.0f;

	char guid[64];
	if (gDeviceKeeper->get_property_string(parent, WMI_GUID_STRING_ITEM, guid,
		sizeof(guid), NULL, false) != B_OK || strcmp(guid, ACPI_ASUS_WMI_MGMT_GUID) != 0) {
		return -1.0f;
	}

	TRACE("found an asus wmi device\n");

	return 0.6f;
}


static status_t
wmi_asus_attach(dk_node *node, void **driverCookie)
{
	CALLED();

	WMIAsus* device = new(std::nothrow) WMIAsus(node);
	if (device == NULL)
		return B_NO_MEMORY;
	*driverCookie = device;

	return B_OK;
}


static void
wmi_asus_detach(void *driverCookie)
{
	CALLED();
	WMIAsus* device = (WMIAsus*)driverCookie;
	delete device;
}


static const dk_match_rule sWMIAsusMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "wmi"),
	DK_MATCH_STRING(WMI_GUID_STRING_ITEM, ACPI_ASUS_WMI_MGMT_GUID),
	DK_MATCH_END
};

static const dk_match_dict sWMIAsusMatchDict = {
	sWMIAsusMatchRules,
	0
};


dk_driver_info gWMIAsusDriverModule = {
	.info   = { WMI_ASUS_DRIVER_NAME, 0, NULL },
	.match  = &sWMIAsusMatchDict,
	.probe  = wmi_asus_support,
	.attach = wmi_asus_attach,
	.detach = wmi_asus_detach,
};

