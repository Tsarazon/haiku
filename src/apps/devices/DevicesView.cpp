/*
 * Copyright 2008-2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Pieter Panman
 */


#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <ScrollView.h>
#include <String.h>

#include <iostream>

#include <device_keeper.h>

#include "DevicesView.h"
#include "dm_wrapper.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DevicesView"

DevicesView::DevicesView()
	:
	BView("DevicesView", B_WILL_DRAW | B_FRAME_EVENTS)
{
	CreateLayout();
	RescanDevices();
	RebuildDevicesOutline();
}


void
DevicesView::CreateLayout()
{
	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* menu = new BMenu(B_TRANSLATE("Devices"));
	BMenuItem* item;
	menu->AddItem(new BMenuItem(B_TRANSLATE("Refresh devices"),
		new BMessage(kMsgRefresh), 'R'));
	menu->AddSeparatorItem();
	menu->AddItem(item = new BMenuItem(B_TRANSLATE("Report compatibility"),
		new BMessage(kMsgReportCompatibility)));
	item->SetEnabled(false);
	menu->AddItem(item = new BMenuItem(B_TRANSLATE("Generate system information"),
		new BMessage(kMsgGenerateSysInfo)));
	item->SetEnabled(false);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menu->SetTargetForItems(this);
	item->SetTarget(be_app);
	menuBar->AddItem(menu);

	fDevicesOutline = new BOutlineListView("devices_list");
	fDevicesOutline->SetTarget(this);
	fDevicesOutline->SetSelectionMessage(new BMessage(kMsgSelectionChanged));

	BScrollView *scrollView = new BScrollView("devicesScrollView",
		fDevicesOutline, B_WILL_DRAW | B_FRAME_EVENTS, true, true);
	// Horizontal scrollbar doesn't behave properly like the vertical
	// scrollbar... If you make the view bigger (exposing a larger percentage
	// of the view), it does not adjust the width of the scroll 'dragger'
	// why? Bug? In scrollview or in outlinelistview?

	BPopUpMenu* orderByPopupMenu = new BPopUpMenu("orderByMenu");
	BMenuItem* byCategory = new BMenuItem(B_TRANSLATE("Category"),
		new BMessage(kMsgOrderCategory));
	BMenuItem* byConnection = new BMenuItem(B_TRANSLATE("Connection"),
		new BMessage(kMsgOrderConnection));
	byCategory->SetMarked(true);
	fOrderBy = byCategory->IsMarked() ? ORDER_BY_CATEGORY : ORDER_BY_CONNECTION;
	orderByPopupMenu->AddItem(byCategory);
	orderByPopupMenu->AddItem(byConnection);
	fOrderByMenu = new BMenuField(B_TRANSLATE("Order by:"), orderByPopupMenu);
	fAttributesView = new PropertyList("attributesView");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddSplit(B_HORIZONTAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddGroup(B_VERTICAL)
				.Add(fOrderByMenu, 1)
				.Add(scrollView, 2)
				.End()
			.Add(fAttributesView, 2);
}


void
DevicesView::RescanDevices()
{
	// Empty the outline and delete the devices in the list, incl. categories
	fDevicesOutline->MakeEmpty();
	DeleteDevices();
	DeleteCategoryMap();

	// Fill the devices list
	status_t error;
	if ((error = init_dm_wrapper()) < 0) {
		std::cerr << "Error initializing device manager: " << strerror(error)
			<< std::endl;
		return;
	}

	kosm_handle_t rootHandle;
	if (get_root(&rootHandle) == B_OK) {
		AddDeviceAndChildren(rootHandle, NULL);
		close_node(rootHandle);
	}

	uninit_dm_wrapper();

	CreateCategoryMap();
}


void
DevicesView::DeleteDevices()
{
	while (fDevices.size() > 0) {
		delete fDevices.back();
		fDevices.pop_back();
	}
}


void
DevicesView::CreateCategoryMap()
{
	CategoryMapIterator iter;
	for (unsigned int i = 0; i < fDevices.size(); i++) {
		Category category = fDevices[i]->GetCategory();
		if (category < 0 || category >= kCategoryStringLength) {
			std::cerr << "CreateCategoryMap: device " << fDevices[i]->GetName()
				<< " returned an unknown category index (" << category << "). "
				<< "Skipping device." << std::endl;
			continue;
		}

		const char* categoryName = kCategoryString[category];

		iter = fCategoryMap.find(category);
		if (iter == fCategoryMap.end()) {
			// This category has not yet been added, add it.
			fCategoryMap[category] = new Device(NULL, BUS_NONE, CAT_NONE, categoryName);
		}
	}
}


void
DevicesView::DeleteCategoryMap()
{
	CategoryMapIterator iter;
	for (iter = fCategoryMap.begin(); iter != fCategoryMap.end(); iter++) {
		delete iter->second;
	}
	fCategoryMap.clear();
}


int
DevicesView::SortItemsCompare(const BListItem *item1, const BListItem *item2)
{
	const BStringItem* stringItem1 = dynamic_cast<const BStringItem*>(item1);
	const BStringItem* stringItem2 = dynamic_cast<const BStringItem*>(item2);
	if (!(stringItem1 && stringItem2)) {
		// is this check necessary?
		std::cerr << "Could not cast BListItem to BStringItem, file a bug\n";
		return 0;
	}
	return Compare(stringItem1->Text(), stringItem2->Text());
}


void
DevicesView::RebuildDevicesOutline()
{
	// Rearranges existing Devices into the proper hierarchy
	fDevicesOutline->MakeEmpty();

	if (fOrderBy == ORDER_BY_CONNECTION) {
		for (unsigned int i = 0; i < fDevices.size(); i++) {
			if (fDevices[i]->GetPhysicalParent() == NULL) {
				// process each parent device and its children
				fDevicesOutline->AddItem(fDevices[i]);
				AddChildrenToOutlineByConnection(fDevices[i]);
			}
		}
	} else if (fOrderBy == ORDER_BY_CATEGORY) {
		// Add all categories to the outline
		CategoryMapIterator iter;
		for (iter = fCategoryMap.begin(); iter != fCategoryMap.end(); iter++) {
			fDevicesOutline->AddItem(iter->second);
		}

		// Add all devices under the categories
		for (unsigned int i = 0; i < fDevices.size(); i++) {
			Category category = fDevices[i]->GetCategory();

			iter = fCategoryMap.find(category);
			if (iter == fCategoryMap.end()) {
				std::cerr
					<< "Tried to add device without category, file a bug\n";
				continue;
			} else {
				fDevicesOutline->AddUnder(fDevices[i], iter->second);
			}
		}
		fDevicesOutline->SortItemsUnder(NULL, true, SortItemsCompare);
	}
	// TODO: Implement BY_BUS
}


void
DevicesView::AddChildrenToOutlineByConnection(Device* parent)
{
	for (unsigned int i = 0; i < fDevices.size(); i++) {
		if (fDevices[i]->GetPhysicalParent() == parent) {
			fDevicesOutline->AddUnder(fDevices[i], parent);
			AddChildrenToOutlineByConnection(fDevices[i]);
		}
	}
}


void
DevicesView::AddDeviceAndChildren(kosm_handle_t node, Device* parent)
{
	Device* newDevice = NULL;

	// Get node metadata in one syscall
	kosm_dk_node_info nodeInfo;
	if (dm_get_node_info(node, &nodeInfo) != B_OK) {
		newDevice = new Device(parent, BUS_NONE,
			CAT_NONE, B_TRANSLATE("Unknown device"));
		fDevices.push_back(newDevice);
		return;
	}

	BString prettyName(nodeInfo.pretty_name);
	BString bus(nodeInfo.bus);

	// Classify the device based on bus and pretty_name
	if (prettyName == "Devices Root") {
		newDevice = new Device(parent, BUS_NONE,
			CAT_COMPUTER, B_TRANSLATE("Computer"));
	} else if (prettyName == "ACPI") {
		newDevice = new Device(parent, BUS_ACPI,
			CAT_BUS, B_TRANSLATE("ACPI bus"));
	} else if (prettyName == "PCI") {
		newDevice = new Device(parent, BUS_PCI,
			CAT_BUS, B_TRANSLATE("PCI bus"));
	} else if (prettyName == "USB") {
		newDevice = new Device(parent, BUS_USB,
			CAT_BUS, B_TRANSLATE("USB bus"));
	} else if (bus == "isa") {
		newDevice = new Device(parent, BUS_ISA,
			CAT_BUS, B_TRANSLATE("ISA bus"));
	} else if (bus == "pci") {
		newDevice = new DevicePCI(parent);
	} else if (bus == "acpi") {
		newDevice = new DeviceACPI(parent);
	} else if (bus == "usb") {
		newDevice = new DeviceUSB(parent);
	} else if (bus == "scsi") {
		newDevice = new DeviceSCSI(parent);
	} else if (prettyName.Length() > 0) {
		newDevice = new Device(parent, BUS_NONE,
			CAT_NONE, prettyName);
	} else {
		newDevice = new Device(parent, BUS_NONE,
			CAT_NONE, B_TRANSLATE("Unknown device"));
	}

	// Store known attributes from node_info
	newDevice->SetAttribute(KOSM_LABEL, prettyName);
	newDevice->SetAttribute(KOSM_DEVICE_BUS, bus);
	if (nodeInfo.module_name[0] != '\0')
		newDevice->SetAttribute("device/driver", nodeInfo.module_name);

	// Query additional properties by name for detailed device info
	kosm_dk_prop_value propVal;
	static const char* kPropertyNames[] = {
		KOSM_DEVICE_VENDOR_ID, KOSM_DEVICE_ID,
		KOSM_DEVICE_TYPE, KOSM_DEVICE_SUB_TYPE,
		"controller_name", "usb/vendor", "usb/product",
		NULL
	};

	for (const char** name = kPropertyNames; *name != NULL; name++) {
		if (dm_get_property(node, *name, &propVal) == B_OK) {
			BString value;
			switch (propVal.type) {
				case B_STRING_TYPE:
					value = propVal.value.string;
					break;
				case B_UINT8_TYPE:
					value << propVal.value.ui8;
					break;
				case B_UINT16_TYPE:
					value << propVal.value.ui16;
					break;
				case B_UINT32_TYPE:
					value << propVal.value.ui32;
					break;
				case B_UINT64_TYPE:
					value << propVal.value.ui64;
					break;
				default:
					value = "Raw data";
			}
			newDevice->SetAttribute(*name, value);
		}
	}

	newDevice->InitFromAttributes();
	fDevices.push_back(newDevice);

	// Process children
	kosm_handle_t child;
	if (get_child(node, &child) != B_OK)
		return;

	do {
		AddDeviceAndChildren(child, newDevice);
		kosm_handle_t next;
		if (get_next_child(node, child, &next) != B_OK) {
			close_node(child);
			break;
		}
		close_node(child);
		child = next;
	} while (true);
}


DevicesView::~DevicesView()
{
	DeleteDevices();
}


void
DevicesView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case kMsgSelectionChanged:
		{
			int32 selected = fDevicesOutline->CurrentSelection(0);
			if (selected >= 0) {
				Device* device = (Device*)fDevicesOutline->ItemAt(selected);
				fAttributesView->AddAttributes(device->GetAllAttributes());
				fAttributesView->Invalidate();
			}
			break;
		}

		case kMsgOrderCategory:
		{
			fOrderBy = ORDER_BY_CATEGORY;
			RescanDevices();
			RebuildDevicesOutline();
			break;
		}

		case kMsgOrderConnection:
		{
			fOrderBy = ORDER_BY_CONNECTION;
			RescanDevices();
			RebuildDevicesOutline();
			break;
		}

		case kMsgRefresh:
		{
			fAttributesView->RemoveAll();
			RescanDevices();
			RebuildDevicesOutline();
			break;
		}

		case kMsgReportCompatibility:
		{
			// To be implemented...
			break;
		}

		case kMsgGenerateSysInfo:
		{
			// To be implemented...
			break;
		}

		default:
			BView::MessageReceived(msg);
			break;
	}
}
