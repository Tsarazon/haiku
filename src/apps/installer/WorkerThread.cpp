/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2005-2008, Jérôme DUVAL.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "WorkerThread.h"

#include <errno.h>
#include <new>
#include <stdio.h>

#include <algorithm>
#include <set>
#include <string>
#include <strings.h>

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Directory.h>
#include <DiskDeviceTypes.h>
#include <DiskDeviceVisitor.h>
#include <disk_device_types.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_index.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <PartitioningInfo.h>
#include <Path.h>
#include <String.h>
#include <VolumeRoster.h>

#include "AutoLocker.h"
#include "InstallEngine.h"
#include "InstallerApp.h"
#include "InstallerWindow.h"
#include "PackageViews.h"
#include "StringForSize.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstallProgress"


//#define COPY_TRACE
#ifdef COPY_TRACE
#define CALLED() 		printf("CALLED %s\n",__PRETTY_FUNCTION__)
#define ERR2(x, y...)	fprintf(stderr, "WorkerThread: "x" %s\n", y, strerror(err))
#define ERR(x)			fprintf(stderr, "WorkerThread: "x" %s\n", strerror(err))
#else
#define CALLED()
#define ERR(x)
#define ERR2(x, y...)
#endif


static const char* const kESPTypeName = "EFI system data";
static const char* const kGPTPartitionMapName = EFI_PARTITION_NAME;
static const char* const kSystemPackagesPath = "system/packages";

static const size_t kFileCopyBufferSize = 65536;
static const off_t kSmallFileCopyThreshold = 131072;	// 128 KB

const uint32 MSG_START_INSTALLING = 'eSRT';


class SourceVisitor : public BDiskDeviceVisitor {
public:
								SourceVisitor(BMenu* menu, off_t* maxSourceSize);

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

private:
	static	bool				_ContainsHaikuSystem(BPartition* partition);

			BMenu*				fMenu;
			off_t*				fMaxSourceSize;
};


class TargetVisitor : public BDiskDeviceVisitor {
public:
								TargetVisitor(BMenu* menu, off_t minTargetSize);

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

private:
			BMenu*				fMenu;
			off_t				fMinTargetSize;
};


class EFIVisitor : public BDiskDeviceVisitor {
public:
								EFIVisitor(BMenu* menu, partition_id bootId);

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

private:
			BMenu*				fMenu;
			partition_id		fBootId;
};


class WorkerThread::ESPPartitionVisitor : public BDiskDeviceVisitor {
public:
								ESPPartitionVisitor(BPath* espPath,
									bool* found);

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

private:
			BPath*				fEspPath;
			bool*				fFound;
};


// #pragma mark - WorkerThread::EntryFilter


class WorkerThread::EntryFilter : public CopyEngine::EntryFilter {
public:
	EntryFilter(const char* sourceDirectory)
		:
		fIgnorePaths(),
		fSourceDevice(-1)
	{
		try {
			fIgnorePaths.insert(kPackagesDirectoryPath);
			fIgnorePaths.insert(kSourcesDirectoryPath);
			fIgnorePaths.insert("rr_moved");
			fIgnorePaths.insert("boot.catalog");
			fIgnorePaths.insert("haiku-boot-floppy.image");
			fIgnorePaths.insert("system/var/swap");
			fIgnorePaths.insert("system/var/shared_memory");
			fIgnorePaths.insert("system/var/log/syslog");
			fIgnorePaths.insert("system/var/log/syslog.old");
			fIgnorePaths.insert("system/settings/ssh/ssh_host_ecdsa_key");
			fIgnorePaths.insert("system/settings/ssh/ssh_host_ecdsa_key.pub");
			fIgnorePaths.insert("system/settings/ssh/ssh_host_ed25519_key");
			fIgnorePaths.insert("system/settings/ssh/ssh_host_ed25519_key.pub");
			fIgnorePaths.insert("system/settings/ssh/ssh_host_rsa_key");
			fIgnorePaths.insert("system/settings/ssh/ssh_host_rsa_key.pub");

			fPackageFSRootPaths.insert("system");
			fPackageFSRootPaths.insert("home/config");
		} catch (std::bad_alloc&) {
		}

		struct stat st;
		if (stat(sourceDirectory, &st) == 0)
			fSourceDevice = st.st_dev;
	}

	virtual bool ShouldCopyEntry(const BEntry& entry, const char* path,
		const struct stat& statInfo) const
	{
		if (S_ISBLK(statInfo.st_mode) || S_ISCHR(statInfo.st_mode)
				|| S_ISFIFO(statInfo.st_mode) || S_ISSOCK(statInfo.st_mode)) {
			printf("skipping '%s', it is a special file.\n", path);
			return false;
		}

		if (fIgnorePaths.find(path) != fIgnorePaths.end()) {
			printf("ignoring '%s'.\n", path);
			return false;
		}

		if (statInfo.st_dev != fSourceDevice) {
			// Allow that only for the root of the packagefs mounts, since
			// those contain directories that shine through from the
			// underlying volume.
			if (fPackageFSRootPaths.find(path) == fPackageFSRootPaths.end())
				return false;
		}

		return true;
	}

private:
	typedef std::set<std::string> StringSet;

			StringSet			fIgnorePaths;
			StringSet			fPackageFSRootPaths;
			dev_t				fSourceDevice;
};


// #pragma mark - WorkerThread::ESPPartitionVisitor


WorkerThread::ESPPartitionVisitor::ESPPartitionVisitor(BPath* espPath,
	bool* found)
	:
	fEspPath(espPath),
	fFound(found)
{
}


bool
WorkerThread::ESPPartitionVisitor::Visit(BDiskDevice* device)
{
	return false;
}


bool
WorkerThread::ESPPartitionVisitor::Visit(BPartition* partition, int32 level)
{
	if (*fFound)
		return true;

	const char* type = partition->Type();
	if (type == NULL || strcmp(type, kESPTypeName) != 0)
		return false;

	printf("Found ESP partition: %s\n", partition->ContentName().String());

	if (partition->IsMounted()) {
		BPath mountPoint;
		if (partition->GetMountPoint(&mountPoint) == B_OK) {
			*fEspPath = mountPoint;
			*fFound = true;
			printf("ESP already mounted at: %s\n", mountPoint.Path());
			return true;
		}
	}

	printf("ESP not mounted, attempting to mount...\n");
	status_t result = partition->Mount();
	if (result == B_OK) {
		BPath mountPoint;
		if (partition->GetMountPoint(&mountPoint) == B_OK) {
			*fEspPath = mountPoint;
			*fFound = true;
			printf("Successfully mounted ESP at: %s\n", mountPoint.Path());
			return true;
		}
	} else {
		fprintf(stderr, "Warning: Failed to mount ESP: %s\n",
			strerror(result));
	}

	return false;
}


// #pragma mark - WorkerThread


WorkerThread::WorkerThread(const BMessenger& owner)
	:
	BLooper("copy_engine"),
	fOwner(owner),
	fPackages(NULL),
	fSpaceRequired(0),
	fCancelSemaphore(-1)
{
	Run();
}


void
WorkerThread::MessageReceived(BMessage* message)
{
	CALLED();

	switch (message->what) {
		case MSG_START_INSTALLING:
		{
			partition_id source = message->GetInt32("source", -1);
			partition_id target = message->GetInt32("target", -1);
			if (source < 0 || target < 0) {
				_SetStatusMessage(B_TRANSLATE("Installation failed due to "
					"invalid partition selection."));
				break;
			}
			_PerformInstall(source, target);
			break;
		}

		case MSG_WRITE_BOOT_SECTOR:
		{
			int32 id;
			if (message->FindInt32("id", &id) != B_OK) {
				_SetStatusMessage(B_TRANSLATE("Boot sector not written "
					"because of an internal error."));
				break;
			}

			BPath targetDirectory;
			status_t err = _GetMountPoint(id, targetDirectory);
			if (err != B_OK) {
				_SetStatusMessage(B_TRANSLATE("The partition can't be "
					"mounted. Please choose a different partition."));
				break;
			}

			if (_WriteBootSector(targetDirectory) != B_OK) {
				_SetStatusMessage(
					B_TRANSLATE("Error writing boot sector."));
				break;
			}
			_SetStatusMessage(
				B_TRANSLATE("Boot sector successfully written."));
			break;
		}

		default:
			BLooper::MessageReceived(message);
	}
}


static BString
arch_efi_default_prefix()
{
#if defined(__i386__)
	return BString("BOOTIA32");
#elif defined(__x86_64__)
	return BString("BOOTX64");
#elif defined(__arm__) || defined(__ARM__)
	return BString("BOOTARM");
#elif defined(__aarch64__) || defined(__arm64__)
	return BString("BOOTAA64");
#elif defined(__riscv) && __riscv_xlen == 32
	return BString("BOOTRISCV32");
#elif defined(__riscv) && __riscv_xlen == 64
	return BString("BOOTRISCV64");
#else
	#error "Error: Unknown EFI Architecture!"
#endif
}


void
WorkerThread::InstallEFILoader(partition_id id, bool rename)
{
	// Executed in window thread.
	BDiskDevice device;
	BPartition* partition;
	BDirectory destDir;
	BPath loaderPath;
	BFile loaderToCopy;
	BFile loaderDest;
	BPath destPath;
	BEntry existingEntry;
	off_t size;
	BString errText;
	status_t err = B_OK;

	BString archLoader = arch_efi_default_prefix();
	archLoader.Append(".EFI");
	BString archLoaderBackup = arch_efi_default_prefix();
	archLoaderBackup.Append("_old.EFI");

	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &loaderPath) != B_OK
		|| loaderPath.Append("platform_loaders/haiku_loader.efi") != B_OK
		|| loaderToCopy.SetTo(loaderPath.Path(), B_READ_ONLY) != B_OK
		|| loaderToCopy.InitCheck() != B_OK
		|| loaderToCopy.GetSize(&size) != B_OK)
		errText.SetTo(B_TRANSLATE("Failed to find EFI loader file!"));

	char* buffer = new char[size];
	if (errText.IsEmpty() && loaderToCopy.Read(buffer, size) != size)
		errText.SetTo(B_TRANSLATE("Failed to read EFI loader file!"));

	if (errText.IsEmpty()
		&& (fDDRoster.GetPartitionWithID(id, &device, &partition) != B_OK
		|| (!partition->IsMounted() && partition->Mount() != B_OK)
		|| partition->GetMountPoint(&destPath) != B_OK))
		errText.SetTo(B_TRANSLATE("Failed to access installation destination!"));

	if (errText.IsEmpty()
		&& (destPath.Append("EFI/BOOT") != B_OK
		|| create_directory(destPath.Path(), 0755) != B_OK
		|| destDir.SetTo(destPath.Path()) != B_OK
		|| destDir.InitCheck() != B_OK))
		errText.SetTo(B_TRANSLATE("Failed to create EFI loader directory!"));

	if (errText.IsEmpty() && rename
		&& (destDir.FindEntry(archLoader, &existingEntry) != B_OK
		|| existingEntry.Rename(archLoaderBackup, true) != B_OK))
		errText.SetTo(B_TRANSLATE("Failed to rename existing loader!"));

	if (errText.IsEmpty()
		&& (err = destDir.CreateFile(archLoader, &loaderDest, true)) == B_FILE_EXISTS) {
		BAlert* confirmAlert = new BAlert("", B_TRANSLATE("An EFI loader is already installed "
			"on the selected partition! Would you like to rename it?"),
			B_TRANSLATE("Rename"), B_TRANSLATE("Cancel"));
		confirmAlert->SetFlags(confirmAlert->Flags() | B_CLOSE_ON_ESCAPE);
		if (confirmAlert->Go() == 0)
			InstallEFILoader(id, true);
		delete[] buffer;
		return;
	} else if (errText.IsEmpty() && (err != B_OK || loaderDest.Write(buffer, size) != size))
		errText.SetTo(B_TRANSLATE("Failed to copy EFI loader to selected partition!"));

	delete[] buffer;
	BAlert* alert = new BAlert("", B_TRANSLATE("EFI loader successfully installed!"),
		B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->SetType(B_INFO_ALERT);
	if (!errText.IsEmpty()) {
		alert->SetType(B_STOP_ALERT);
		alert->SetText(errText);
	}
	alert->Go();
}


void
WorkerThread::ScanDisksPartitions(BMenu* srcMenu, BMenu* targetMenu,
	BMenu* EFIMenu)
{
	// NOTE: This is actually executed in the window thread.
	BDiskDevice device;
	BPartition* partition = NULL;

	off_t maxSourceSize = 0;
	SourceVisitor srcVisitor(srcMenu, &maxSourceSize);
	fDDRoster.VisitEachMountedPartition(&srcVisitor, &device, &partition);

	off_t minTargetSize = std::max(maxSourceSize, kMinTargetPartitionSize);
	TargetVisitor targetVisitor(targetMenu, minTargetSize);
	fDDRoster.VisitEachPartition(&targetVisitor, &device, &partition);

	BDiskDevice bootDevice;
	BPartition* bootPartition;
	partition_id bootId = -1;
	if (fDDRoster.FindPartitionByMountPoint(kBootPath, &bootDevice,
			&bootPartition) == B_OK && bootPartition->Parent() != NULL) {
		bootId = bootPartition->Parent()->ID();
	}

	EFIVisitor efiVisitor(EFIMenu, bootId);
	fDDRoster.VisitEachPartition(&efiVisitor, &device, &partition);
}


void
WorkerThread::SetPackagesList(BList* list)
{
	// Executed in window thread.
	BAutolock _(this);

	delete fPackages;
	fPackages = list;
}


bool
WorkerThread::Cancel()
{
	if (fCancelSemaphore < 0)
		return false;

	return release_sem(fCancelSemaphore) == B_OK;
}


void
WorkerThread::StartInstall(partition_id sourcePartitionID,
	partition_id targetPartitionID)
{
	// Executed in window thread.
	BMessage message(MSG_START_INSTALLING);
	message.AddInt32("source", sourcePartitionID);
	message.AddInt32("target", targetPartitionID);

	PostMessage(&message, this);
}


void
WorkerThread::WriteBootSector(BMenu* targetMenu)
{
	CALLED();

	PartitionMenuItem* item = (PartitionMenuItem*)targetMenu->FindMarked();
	if (item == NULL) {
		ERR("bad menu items\n");
		return;
	}

	BMessage message(MSG_WRITE_BOOT_SECTOR);
	message.AddInt32("id", item->ID());
	PostMessage(&message, this);
}


void
WorkerThread::InstallEFILoader(partition_id id, bool rename)
{
	BPath mountPoint;
	status_t status = _GetMountPoint(id, mountPoint);
	if (status != B_OK) {
		_SetStatusMessage(B_TRANSLATE("The partition can't be mounted."));
		return;
	}

	status = _InstallEFIBootloader(mountPoint);
	if (status != B_OK) {
		_SetStatusMessage(B_TRANSLATE("Failed to install EFI bootloader."));
	} else {
		_SetStatusMessage(B_TRANSLATE("EFI bootloader installed."));
	}
}


// #pragma mark - Private methods


/*static*/ BPartition*
WorkerThread::_FindESPChild(BDiskDevice* device)
{
	if (device == NULL)
		return NULL;

	for (int32 i = 0; i < device->CountChildren(); i++) {
		BPartition* child = device->ChildAt(i);
		if (child == NULL)
			continue;

		const char* type = child->Type();
		if (type != NULL && strcmp(type, kESPTypeName) == 0)
			return child;
	}

	return NULL;
}


status_t
WorkerThread::_GetMountPoint(partition_id partitionID, BPath& mountPoint,
	BVolume* volume)
{
	BDiskDevice device;
	BPartition* partition;

	if (fDDRoster.GetPartitionWithID(partitionID, &device, &partition) == B_OK) {
		if (!partition->IsMounted()) {
			status_t err = partition->Mount();
			if (err < B_OK)
				return err;
		}
		if (volume != NULL) {
			status_t err = partition->GetVolume(volume);
			if (err != B_OK)
				return err;
		}
		return partition->GetMountPoint(&mountPoint);
	}

	if (fDDRoster.GetDeviceWithID(partitionID, &device) == B_OK) {
		if (!device.IsMounted()) {
			status_t err = device.Mount();
			if (err < B_OK)
				return err;
		}
		if (volume != NULL) {
			status_t err = device.GetVolume(volume);
			if (err != B_OK)
				return err;
		}
		return device.GetMountPoint(&mountPoint);
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
WorkerThread::_WriteBootSector(BPath& path)
{
	BPath bootPath;
	find_directory(B_BEOS_BOOT_DIRECTORY, &bootPath);
	BString command;
	command.SetToFormat("makebootable \"%s\"", path.Path());
	_SetStatusMessage(B_TRANSLATE("Writing bootsector."));
	return system(command.String());
}


status_t
WorkerThread::_CreateESPIfNeeded(BDiskDevice* targetDevice)
{
	if (targetDevice == NULL)
		return B_BAD_VALUE;

	_SetStatusMessage(B_TRANSLATE("Checking EFI System Partition..."));

	// 1. Check if haiku_loader.efi exists in source image
	//    If not, this is a BIOS-only image and ESP is not needed
	BVolumeRoster roster;
	BVolume bootVolume;
	roster.GetBootVolume(&bootVolume);

	BPath efiLoaderPath;
	if (find_directory(B_SYSTEM_DIRECTORY, &efiLoaderPath, false,
			&bootVolume) == B_OK) {
		efiLoaderPath.Append("boot/efi/haiku_loader.efi");
		BEntry efiEntry(efiLoaderPath.Path());
		if (!efiEntry.Exists()) {
			printf("No EFI loader found in source - BIOS-only image, "
				"skipping ESP creation\n");
			return B_OK;
		}
	}

	// 2. Check that target disk uses GPT partitioning
	const char* partitioningSystem = targetDevice->ContentType();
	if (partitioningSystem == NULL
			|| strcmp(partitioningSystem, kGPTPartitionMapName) != 0) {
		printf("Target disk is not GPT (%s) - skipping ESP creation\n",
			partitioningSystem != NULL ? partitioningSystem : "unknown");
		return B_OK;
	}

	// 3. Check if ESP already exists on this disk
	if (_FindESPChild(targetDevice) != NULL) {
		printf("ESP already exists on target disk\n");
		return B_OK;
	}

	// 4. Find free space for ESP
	_SetStatusMessage(B_TRANSLATE("Creating EFI System Partition (360 MB)..."));

	BPartitioningInfo partitioningInfo;
	status_t result = targetDevice->GetPartitioningInfo(&partitioningInfo);
	if (result != B_OK) {
		fprintf(stderr, "Failed to get partitioning info: %s\n",
			strerror(result));
		return result;
	}

	off_t espOffset = -1;

	// Find suitable free space (prefer beginning of disk)
	int32 spacesCount = partitioningInfo.CountPartitionableSpaces();
	for (int32 i = 0; i < spacesCount; i++) {
		off_t offset, size;
		if (partitioningInfo.GetPartitionableSpaceAt(i, &offset, &size)
				== B_OK) {
			if (size >= kESPSize) {
				espOffset = offset;
				printf("Found free space at offset %" B_PRIdOFF ", size %"
					B_PRIdOFF " MB\n", offset, size / (1024 * 1024));
				break;
			}
		}
	}

	if (espOffset < 0) {
		fprintf(stderr, "No space available for ESP partition "
			"(need 360 MB, found none)\n");
		return B_DEVICE_FULL;
	}

	// 5. Prepare disk for modifications
	result = targetDevice->PrepareModifications();
	if (result != B_OK) {
		fprintf(stderr, "Failed to prepare disk for modifications: %s\n",
			strerror(result));
		return result;
	}

	// 6. Validate creation parameters
	off_t validatedOffset = espOffset;
	off_t validatedSize = kESPSize;
	BString espName("ESP");

	result = targetDevice->ValidateCreateChild(&validatedOffset, &validatedSize,
		kESPTypeName, &espName, NULL);
	if (result != B_OK) {
		fprintf(stderr, "ESP creation validation failed: %s\n",
			strerror(result));
		targetDevice->CancelModifications();
		return result;
	}

	// 7. Create ESP partition
	BPartition* espPartition = NULL;
	result = targetDevice->CreateChild(validatedOffset, validatedSize,
		kESPTypeName, espName.String(), NULL, &espPartition);
	if (result != B_OK) {
		fprintf(stderr, "Failed to create ESP partition: %s\n",
			strerror(result));
		targetDevice->CancelModifications();
		return result;
	}

	// 8. Commit partition creation
	result = targetDevice->CommitModifications();
	if (result != B_OK) {
		fprintf(stderr, "Failed to commit ESP partition creation: %s\n",
			strerror(result));
		return result;
	}

	printf("ESP partition created: offset=%" B_PRIdOFF ", size=%" B_PRIdOFF
		" MB\n", validatedOffset, validatedSize / (1024 * 1024));

	// 9. Update disk information
	bool updated = false;
	targetDevice->Update(&updated);

	// 10. Find created partition and format as FAT32
	espPartition = _FindESPChild(targetDevice);
	if (espPartition == NULL) {
		fprintf(stderr, "ESP partition created but not found after update\n");
		return B_ERROR;
	}

	// 11. Format ESP as FAT32
	result = targetDevice->PrepareModifications();
	if (result != B_OK) {
		fprintf(stderr, "Failed to prepare disk for FAT32 formatting: %s\n",
			strerror(result));
		return result;
	}

	result = espPartition->Initialize(kPartitionTypeFAT32, "ESP", NULL);
	if (result != B_OK) {
		fprintf(stderr, "Failed to format ESP as FAT32: %s\n",
			strerror(result));
		targetDevice->CancelModifications();
		return result;
	}

	// 12. Commit formatting
	result = targetDevice->CommitModifications();
	if (result != B_OK) {
		fprintf(stderr, "Failed to commit FAT32 formatting: %s\n",
			strerror(result));
		return result;
	}

	printf("ESP partition created and formatted as FAT32 successfully:\n");
	printf("  Offset: %" B_PRIdOFF " bytes\n", validatedOffset);
	printf("  Size: %" B_PRIdOFF " MB\n", validatedSize / (1024 * 1024));

	_SetStatusMessage(B_TRANSLATE("EFI System Partition created."));
	return B_OK;
}


status_t
WorkerThread::_FindESPPartition(BPath& espMountPoint)
{
	// First: Search ALL partitions (including unmounted) for ESP by GPT type
	printf("Searching for ESP partition (mounted or unmounted)...\n");

	bool espFound = false;
	BDiskDevice device;

	ESPPartitionVisitor visitor(&espMountPoint, &espFound);
	fDDRoster.VisitEachPartition(&visitor, &device);

	if (espFound)
		return B_OK;

	// Second pass: Fallback to FAT partition with existing EFI directory
	// (for MBR or non-standard setups)
	printf("ESP not found by GUID, trying FAT + EFI directory fallback...\n");

	BVolumeRoster volumeRoster;
	BVolume volume;
	while (volumeRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsReadOnly())
			continue;

		BDirectory mountPoint;
		if (volume.GetRootDirectory(&mountPoint) != B_OK)
			continue;

		BEntry entry;
		mountPoint.GetEntry(&entry);
		BPath path;
		entry.GetPath(&path);

		BDiskDevice diskDevice;
		BPartition* partition = NULL;
		status_t result = fDDRoster.GetPartitionForPath(path.Path(),
			&diskDevice, &partition);
		if (result != B_OK || partition == NULL)
			continue;

		const char* contentType = partition->ContentType();
		if (contentType == NULL)
			continue;

		if (strcmp(contentType, kPartitionTypeFAT32) != 0
				&& strcmp(contentType, kPartitionTypeFAT16) != 0
				&& strcmp(contentType, kPartitionTypeFAT12) != 0) {
			continue;
		}

		BPath efiCheckPath(path.Path(), "EFI");
		if (efiCheckPath.InitCheck() != B_OK)
			continue;

		BEntry efiEntry(efiCheckPath.Path());
		if (!efiEntry.Exists()) {
			result = create_directory(efiCheckPath.Path(), 0755);
			if (result != B_OK && result != B_FILE_EXISTS)
				continue;
		}

		espMountPoint = path;
		printf("Found FAT partition with EFI directory (fallback) at: %s\n",
			espMountPoint.Path());
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
WorkerThread::_InstallEFIBootloader(const BPath& targetDirectory)
{
	_SetStatusMessage(B_TRANSLATE("Installing EFI bootloader."));

	BPath espPath;
	status_t result = _FindESPPartition(espPath);
	if (result != B_OK) {
		fprintf(stderr, "Warning: ESP partition not found or not mounted\n");
		fprintf(stderr, "Please ensure you have a FAT32 partition with GPT "
			"type 'EFI System' or manually mount an ESP.\n");
		return result;
	}

	// Source bootloader path
	BPath loaderSource(targetDirectory.Path(),
		"system/boot/efi/haiku_loader.efi");
	if (loaderSource.InitCheck() != B_OK)
		return loaderSource.InitCheck();

	BEntry sourceEntry(loaderSource.Path());
	if (!sourceEntry.Exists()) {
		fprintf(stderr, "Error: haiku_loader.efi not found at %s\n",
			loaderSource.Path());
		return B_ENTRY_NOT_FOUND;
	}

	// Create target directories: EFI/HAIKU and EFI/BOOT
	BPath haikuEfiDir(espPath.Path(), "EFI/HAIKU");
	if (haikuEfiDir.InitCheck() != B_OK)
		return haikuEfiDir.InitCheck();

	result = create_directory(haikuEfiDir.Path(), 0755);
	if (result != B_OK && result != B_FILE_EXISTS) {
		fprintf(stderr, "Error: Failed to create %s: %s\n",
			haikuEfiDir.Path(), strerror(result));
		return result;
	}

	BPath bootEfiDir(espPath.Path(), "EFI/BOOT");
	if (bootEfiDir.InitCheck() != B_OK)
		return bootEfiDir.InitCheck();

	result = create_directory(bootEfiDir.Path(), 0755);
	if (result != B_OK && result != B_FILE_EXISTS) {
		fprintf(stderr, "Error: Failed to create %s: %s\n",
			bootEfiDir.Path(), strerror(result));
		return result;
	}

	// Determine correct EFI binary name based on architecture
	const char* efiBootName;
#if defined(__x86_64__)
	efiBootName = "BOOTX64.EFI";
#elif defined(__i386__)
	efiBootName = "BOOTIA32.EFI";
#elif defined(__aarch64__) || defined(__ARM_ARCH_8__)
	efiBootName = "BOOTAA64.EFI";
#elif defined(__arm__)
	efiBootName = "BOOTARM.EFI";
#elif defined(__riscv) && (__riscv_xlen == 64)
	efiBootName = "BOOTRISCV64.EFI";
#else
	efiBootName = "BOOTX64.EFI";
	fprintf(stderr, "Warning: Unknown architecture, defaulting to "
		"BOOTX64.EFI\n");
#endif

	// Copy to EFI/HAIKU/haiku_loader.efi
	BPath haikuLoaderPath(haikuEfiDir.Path(), "haiku_loader.efi");
	if (haikuLoaderPath.InitCheck() != B_OK)
		return haikuLoaderPath.InitCheck();

	BEntry haikuLoaderEntry(haikuLoaderPath.Path());
	if (haikuLoaderEntry.Exists())
		haikuLoaderEntry.Remove();

	result = _CopyFile(loaderSource.Path(), haikuLoaderPath.Path());
	if (result != B_OK) {
		fprintf(stderr, "Error: Failed to copy bootloader to %s: %s\n",
			haikuLoaderPath.Path(), strerror(result));
		return result;
	}

	// Copy to fallback location EFI/BOOT/BOOT{ARCH}.EFI
	// This is essential for stubborn UEFI implementations (e.g., Lenovo M720Q)
	// that ignore boot entries and only look for the fallback bootloader
	BPath fallbackPath(bootEfiDir.Path(), efiBootName);
	if (fallbackPath.InitCheck() != B_OK)
		return fallbackPath.InitCheck();

	BEntry fallbackEntry(fallbackPath.Path());
	if (fallbackEntry.Exists())
		fallbackEntry.Remove();

	result = _CopyFile(loaderSource.Path(), fallbackPath.Path());
	if (result != B_OK) {
		fprintf(stderr, "Error: Failed to copy bootloader to %s: %s\n",
			fallbackPath.Path(), strerror(result));
		return result;
	}

	printf("EFI bootloader installed successfully:\n");
	printf("  - %s\n", haikuLoaderPath.Path());
	printf("  - %s (fallback for UEFI)\n", fallbackPath.Path());

	_SetStatusMessage(B_TRANSLATE("EFI bootloader installed."));

	return B_OK;
}


status_t
WorkerThread::_CopyFile(const char* source, const char* destination)
{
	BFile sourceFile(source, B_READ_ONLY);
	status_t result = sourceFile.InitCheck();
	if (result != B_OK) {
		fprintf(stderr, "Error: Failed to open source file %s: %s\n",
			source, strerror(result));
		return result;
	}

	BFile destFile(destination, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	result = destFile.InitCheck();
	if (result != B_OK) {
		fprintf(stderr, "Error: Failed to create destination file %s: %s\n",
			destination, strerror(result));
		return result;
	}

	off_t fileSize;
	result = sourceFile.GetSize(&fileSize);
	if (result != B_OK) {
		fprintf(stderr, "Error: Failed to get source file size: %s\n",
			strerror(result));
		return result;
	}

	// For small files, use stack buffer to avoid heap allocation overhead
	if (fileSize > 0 && fileSize <= kSmallFileCopyThreshold) {
		char stackBuffer[kSmallFileCopyThreshold];
		ssize_t bytesRead = sourceFile.Read(stackBuffer, fileSize);
		if (bytesRead != fileSize) {
			fprintf(stderr, "Error: Read failed during file copy\n");
			return bytesRead < 0 ? (status_t)bytesRead : B_IO_ERROR;
		}
		ssize_t bytesWritten = destFile.Write(stackBuffer, fileSize);
		if (bytesWritten != fileSize) {
			fprintf(stderr, "Error: Write failed during file copy\n");
			return bytesWritten < 0 ? (status_t)bytesWritten : B_IO_ERROR;
		}
		return B_OK;
	}

	// For larger files, use heap buffer with streaming copy
	char* buffer = new(std::nothrow) char[kFileCopyBufferSize];
	if (buffer == NULL)
		return B_NO_MEMORY;

	ssize_t bytesRead;
	while ((bytesRead = sourceFile.Read(buffer, kFileCopyBufferSize)) > 0) {
		ssize_t bytesWritten = destFile.Write(buffer, bytesRead);
		if (bytesWritten != bytesRead) {
			delete[] buffer;
			fprintf(stderr, "Error: Write failed during file copy\n");
			return B_IO_ERROR;
		}
	}

	delete[] buffer;

	if (bytesRead < 0) {
		fprintf(stderr, "Error: Read failed during file copy: %s\n",
			strerror(bytesRead));
		return (status_t)bytesRead;
	}

	return B_OK;
}


status_t
WorkerThread::_LaunchFinishScript(BPath& path)
{
	_SetStatusMessage(B_TRANSLATE("Finishing installation."));

	// Create cache/tmp directory
	BPath cacheTmpPath(path.Path(), "system/cache/tmp");
	status_t result = cacheTmpPath.InitCheck();
	if (result != B_OK)
		return result;

	result = create_directory(cacheTmpPath.Path(), 0755);
	if (result != B_OK && result != B_FILE_EXISTS)
		return result;

	// Create packages/administrative directory
	BPath adminPath(path.Path(), "system/packages/administrative");
	result = adminPath.InitCheck();
	if (result != B_OK)
		return result;

	result = create_directory(adminPath.Path(), 0755);
	if (result != B_OK && result != B_FILE_EXISTS)
		return result;

	// Create FirstBootProcessingNeeded marker file
	BPath firstBootPath(adminPath.Path(), "FirstBootProcessingNeeded");
	result = firstBootPath.InitCheck();
	if (result != B_OK)
		return result;

	BFile firstBootFile(firstBootPath.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	result = firstBootFile.InitCheck();
	if (result != B_OK)
		return result;

	const char* markerText = "First Boot written by Installer.\n";
	firstBootFile.Write(markerText, strlen(markerText));

	// Remove Installer link from Desktop
	BPath installerLinkPath(path.Path(), "home/Desktop/Installer");
	if (installerLinkPath.InitCheck() == B_OK) {
		BEntry installerEntry(installerLinkPath.Path());
		if (installerEntry.Exists())
			installerEntry.Remove();
	}

	return B_OK;
}


status_t
WorkerThread::_PerformInstall(partition_id sourcePartitionID,
	partition_id targetPartitionID)
{
	CALLED();

	BPath targetDirectory;
	BPath srcDirectory;
	BPath trashPath;
	BPath testPath;
	BDirectory targetDir;
	BDiskDevice device;
	BPartition* partition;
	BVolume targetVolume;
	status_t err = B_OK;
	int32 entries = 0;
	entry_ref testRef;

	if (sourcePartitionID < 0 || targetPartitionID < 0) {
		ERR("bad source or target partition ID\n");
		return _InstallationError(err);
	}

	err = _GetMountPoint(targetPartitionID, targetDirectory, &targetVolume);
	if (err != B_OK) {
		_SetStatusMessage(B_TRANSLATE("The disk can't be mounted. Please "
			"choose a different disk."));
		ERR("_GetMountPoint (target)");
		return _InstallationError(err);
	}

	if (fSpaceRequired > 0 && targetVolume.FreeBytes() < fSpaceRequired) {
		BAlert* alert = new BAlert("", B_TRANSLATE("The destination disk may "
			"not have enough space. Try choosing a different disk or choose "
			"to not install optional items."),
			B_TRANSLATE("Try installing anyway"), B_TRANSLATE("Cancel"), 0,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() != 0)
			return _InstallationError(err);
	}

	err = _GetMountPoint(sourcePartitionID, srcDirectory);
	if (err != B_OK) {
		_SetStatusMessage(B_TRANSLATE("The disk can't be mounted. Please "
			"choose a different disk."));
		ERR("_GetMountPoint (source)");
		return _InstallationError(err);
	}

	if (strcmp(srcDirectory.Path(), targetDirectory.Path()) == 0) {
		_SetStatusMessage(B_TRANSLATE("You can't install the contents of a "
			"disk onto itself. Please choose a different disk."));
		return _InstallationError(err);
	}

// check not installing on boot volume
	if (strncmp(kBootPath, targetDirectory.Path(), strlen(kBootPath)) == 0) {
		BString text(B_TRANSLATE("Are you sure you want to "
		"install onto the current boot disk? The %appname% will have to "
		"reboot your machine if you proceed."));
		text.ReplaceFirst("%appname%", B_TRANSLATE_SYSTEM_NAME("Installer"));
		BAlert* alert = new BAlert("", text, B_TRANSLATE("OK"),
			B_TRANSLATE("Cancel"), 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() != 0) {
			_SetStatusMessage("Installation stopped.");
			return _InstallationError(err);
		}
	}

	if (find_directory(B_TRASH_DIRECTORY, &trashPath, false,
			&targetVolume) == B_OK && targetDir.SetTo(trashPath.Path()) == B_OK) {
		while (targetDir.GetNextRef(&testRef) == B_OK) {
			entries++;
			break;
		}
	}

	targetDir.SetTo(targetDirectory.Path());

	while (entries == 0 && targetDir.GetNextRef(&testRef) == B_OK) {
		if (testPath.SetTo(&testRef) == B_OK && testPath != trashPath)
			entries++;
	}

	if (entries != 0) {
		BAlert* alert = new BAlert("", B_TRANSLATE("The target volume is not "
			"empty. If it already contains a Haiku installation, it will be "
			"overwritten. This will remove all installed software.\n\n"
			"If you want to upgrade your system without removing installed "
			"software, see the Haiku User Guide's topic on the application "
			"\"SoftwareUpdater\" for update instructions.\n\n"
			"Are you sure you want to continue the installation?"),
			B_TRANSLATE("Install anyway"), B_TRANSLATE("Cancel"), 0,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() != 0) {
			// TODO: Would be cool to offer the option here to clean additional
			// folders at the user's choice.
			return _InstallationError(B_CANCELED);
		}

		err = _PrepareCleanInstall(targetDirectory);
		if (err != B_OK)
			return _InstallationError(err);
	}

	// Begin actual installation

	ProgressReporter reporter(fOwner, new BMessage(MSG_STATUS_MESSAGE));
	EntryFilter entryFilter(srcDirectory.Path());
	CopyEngine engine(&reporter, &entryFilter);
	BList unzipEngines;

	// Create the default indices which should always be present on a proper
	// boot volume. We don't care if the source volume does not have them.
	// After all, the user might be re-installing to another drive and may
	// want problems fixed along the way...
	err = _CreateDefaultIndices(targetDirectory);
	if (err != B_OK)
		return _InstallationError(err);

	// Mirror all the indices which are present on the source volume onto
	// the target volume.
	err = _MirrorIndices(srcDirectory, targetDirectory);
	if (err != B_OK)
		return _InstallationError(err);

	// Let the engine collect information for the progress bar later on
	engine.ResetTargets(srcDirectory.Path());
	err = engine.CollectTargets(srcDirectory.Path(), fCancelSemaphore);
	if (err != B_OK)
		return _InstallationError(err);

	// Collect selected packages also
	if (fPackages) {
		int32 count = fPackages->CountItems();
		for (int32 i = 0; i < count; i++) {
			Package* p = static_cast<Package*>(fPackages->ItemAt(i));
			const BPath& pkgPath = p->Path();
			err = pkgPath.InitCheck();
			if (err != B_OK)
				return _InstallationError(err);
			err = engine.CollectTargets(pkgPath.Path(), fCancelSemaphore);
			if (err != B_OK)
				return _InstallationError(err);
		}
	}

	// collect information about all zip packages
	err = _ProcessZipPackages(srcDirectory.Path(), targetDirectory.Path(),
		&reporter, unzipEngines);
	if (err != B_OK)
		return _InstallationError(err);

	reporter.StartTimer();

	// copy source volume
	err = engine.Copy(srcDirectory.Path(), targetDirectory.Path(),
		fCancelSemaphore);
	if (err != B_OK)
		return _InstallationError(err);

	// copy selected packages
	if (fPackages) {
		int32 count = fPackages->CountItems();
		// FIXME: find_directory doesn't return the folder in the target volume,
		// so we are hard coding this for now.
		BPath targetPkgDir(targetDirectory.Path(), kSystemPackagesPath);
		err = targetPkgDir.InitCheck();
		if (err != B_OK)
			return _InstallationError(err);
		for (int32 i = 0; i < count; i++) {
			Package* p = static_cast<Package*>(fPackages->ItemAt(i));
			const BPath& pkgPath = p->Path();
			err = pkgPath.InitCheck();
			if (err != B_OK)
				return _InstallationError(err);
			BPath targetPath(targetPkgDir.Path(), pkgPath.Leaf());
			err = targetPath.InitCheck();
			if (err != B_OK)
				return _InstallationError(err);
			err = engine.Copy(pkgPath.Path(), targetPath.Path(),
				fCancelSemaphore);
			if (err != B_OK)
				return _InstallationError(err);
		}
	}

	// Extract all zip packages. If an error occured, delete the rest of
	// the engines, but stop extracting.
	for (int32 i = 0; i < unzipEngines.CountItems(); i++) {
		UnzipEngine* unzipEngine = reinterpret_cast<UnzipEngine*>(
			unzipEngines.ItemAtFast(i));
		if (err == B_OK)
			err = unzipEngine->UnzipPackage();
		delete unzipEngine;
	}
	if (err != B_OK)
		return _InstallationError(err);

	err = _WriteBootSector(targetDirectory);
	if (err != B_OK)
		return _InstallationError(err);

	// Create ESP partition if needed (before EFI bootloader installation)
	// This ensures UEFI systems have a proper ESP even if user didn't create one
	BDiskDevice targetDiskDevice;
	if (fDDRoster.GetPartitionWithID(targetPartitionID, &targetDiskDevice,
			&partition) == B_OK) {
		status_t espResult = _CreateESPIfNeeded(&targetDiskDevice);
		if (espResult != B_OK && espResult != B_DEVICE_FULL) {
			fprintf(stderr, "Warning: ESP creation failed: %s\n",
				strerror(espResult));
			// Continue anyway - might be BIOS system or ESP exists elsewhere
		}
	}

	// Install UEFI bootloader to ESP partition
	// This is critical for modern UEFI systems and fixes boot issues on
	// systems like Lenovo M720Q that require the fallback bootloader location.
	// Note: This may fail gracefully on BIOS-only systems, which is expected.
	err = _InstallEFIBootloader(targetDirectory);
	if (err != B_OK) {
		fprintf(stderr, "Warning: Failed to install EFI bootloader: %s\n",
			strerror(err));
		fprintf(stderr, "This is expected on BIOS-only systems.\n");
		fprintf(stderr, "If you're installing to a UEFI system, please "
			"ensure:\n");
		fprintf(stderr, "  1. You have a FAT32 partition with GPT type "
			"'EFI System'\n");
		fprintf(stderr, "  2. The ESP partition is mounted\n");
	}

	err = _LaunchFinishScript(targetDirectory);
	if (err != B_OK)
		return _InstallationError(err);

	fOwner.SendMessage(MSG_INSTALL_FINISHED);
	return B_OK;
}


status_t
WorkerThread::_PrepareCleanInstall(const BPath& targetDirectory) const
{
	// When a target volume has files (other than the trash), the /system
	// folder will be purged, except for the /system/settings subdirectory.
	BPath systemPath(targetDirectory.Path(), "system", true);
	status_t ret = systemPath.InitCheck();
	if (ret != B_OK)
		return ret;

	BEntry systemEntry(systemPath.Path());
	ret = systemEntry.InitCheck();
	if (ret != B_OK)
		return ret;
	if (!systemEntry.Exists())
		return B_OK;
	if (!systemEntry.IsDirectory())
		return systemEntry.Remove();

	BDirectory systemDirectory(&systemEntry);
	ret = systemDirectory.InitCheck();
	if (ret != B_OK)
		return ret;

	BEntry subEntry;
	char fileName[B_FILE_NAME_LENGTH];
	while (systemDirectory.GetNextEntry(&subEntry) == B_OK) {
		ret = subEntry.GetName(fileName);
		if (ret != B_OK)
			return ret;

		if (subEntry.IsDirectory() && strcmp(fileName, "settings") == 0)
			continue;
		else if (subEntry.IsDirectory()) {
			ret = CopyEngine::RemoveFolder(subEntry);
			if (ret != B_OK)
				return ret;
		} else {
			ret = subEntry.Remove();
			if (ret != B_OK)
				return ret;
		}
	}

	return B_OK;
}


status_t
WorkerThread::_InstallationError(status_t error)
{
	BMessage statusMessage(MSG_RESET);
	if (error == B_CANCELED)
		_SetStatusMessage(B_TRANSLATE("Installation canceled."));
	else
		statusMessage.AddInt32("error", error);
	ERR("_PerformInstall failed");
	fOwner.SendMessage(&statusMessage);
	return error;
}


status_t
WorkerThread::_MirrorIndices(const BPath& sourceDirectory,
	const BPath& targetDirectory) const
{
	dev_t sourceDevice = dev_for_path(sourceDirectory.Path());
	if (sourceDevice < 0)
		return (status_t)sourceDevice;
	dev_t targetDevice = dev_for_path(targetDirectory.Path());
	if (targetDevice < 0)
		return (status_t)targetDevice;
	DIR* indices = fs_open_index_dir(sourceDevice);
	if (indices == NULL) {
		printf("%s: fs_open_index_dir(): (%d) %s\n", sourceDirectory.Path(),
			errno, strerror(errno));
		// Opening the index directory will fail for example on ISO-Live
		// CDs. The default indices have already been created earlier, so
		// we simply bail.
		return B_OK;
	}
	while (dirent* index = fs_read_index_dir(indices)) {
		const char* name = index->d_name;

		// Quick first-character check before expensive strcmp calls
		if (name[0] == 'n' || name[0] == 's' || name[0] == 'l') {
			if (strcmp(name, "name") == 0
				|| strcmp(name, "size") == 0
				|| strcmp(name, "last_modified") == 0) {
				continue;
			}
		}

		index_info info;
		if (fs_stat_index(sourceDevice, name, &info) != B_OK) {
			printf("Failed to mirror index %s: fs_stat_index(): (%d) %s\n",
				name, errno, strerror(errno));
			continue;
		}

		uint32 flags = 0;
			// Flags are always 0 for the moment.
		if (fs_create_index(targetDevice, name, info.type, flags)
			!= B_OK) {
			if (errno == B_FILE_EXISTS)
				continue;
			printf("Failed to mirror index %s: fs_create_index(): (%d) %s\n",
				name, errno, strerror(errno));
			continue;
		}
	}
	fs_close_index_dir(indices);
	return B_OK;
}


status_t
WorkerThread::_CreateDefaultIndices(const BPath& targetDirectory) const
{
	dev_t targetDevice = dev_for_path(targetDirectory.Path());
	if (targetDevice < 0)
		return (status_t)targetDevice;

	struct IndexInfo {
		const char* name;
		uint32		type;
	};

	const IndexInfo defaultIndices[] = {
		{ "BEOS:APP_SIG", B_STRING_TYPE },
		{ "BEOS:LOCALE_LANGUAGE", B_STRING_TYPE },
		{ "BEOS:LOCALE_SIGNATURE", B_STRING_TYPE },
		{ "_trk/qrylastchange", B_INT32_TYPE },
		{ "_trk/recentQuery", B_INT32_TYPE },
		{ "be:deskbar_item_status", B_STRING_TYPE }
	};

	uint32 flags = 0;
		// Flags are always 0 for the moment.

	for (size_t i = 0; i < B_COUNT_OF(defaultIndices); i++) {
		const IndexInfo& info = defaultIndices[i];
		if (fs_create_index(targetDevice, info.name, info.type, flags)
			!= B_OK) {
			if (errno == B_FILE_EXISTS)
				continue;
			printf("Failed to create index %s: fs_create_index(): (%d) %s\n",
				info.name, errno, strerror(errno));
			return errno;
		}
	}

	return B_OK;
}


status_t
WorkerThread::_ProcessZipPackages(const char* sourcePath,
	const char* targetPath, ProgressReporter* reporter, BList& unzipEngines)
{
	// TODO: Put those in the optional packages list view
	// TODO: Implement mechanism to handle dependencies between these
	// packages. (Selecting one will auto-select others.)
	BPath pkgRootDir(sourcePath, kPackagesDirectoryPath);
	BDirectory directory(pkgRootDir.Path());
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;
		size_t nameLength = strlen(name);
		if (nameLength < 5)		// minimum: "x.zip"
			continue;
		if (strcasecmp(name + nameLength - 4, ".zip") != 0)
			continue;
		printf("found .zip package: %s\n", name);

		UnzipEngine* unzipEngine = new(std::nothrow) UnzipEngine(reporter,
			fCancelSemaphore);
		if (unzipEngine == NULL || !unzipEngines.AddItem(unzipEngine)) {
			delete unzipEngine;
			return B_NO_MEMORY;
		}
		BPath path;
		entry.GetPath(&path);
		status_t ret = unzipEngine->SetTo(path.Path(), targetPath);
		if (ret != B_OK)
			return ret;

		reporter->AddItems(unzipEngine->ItemsToUncompress(),
			unzipEngine->BytesToUncompress());
	}

	return B_OK;
}


void
WorkerThread::_SetStatusMessage(const char* status)
{
	BMessage msg(MSG_STATUS_MESSAGE);
	msg.AddString("status", status);
	fOwner.SendMessage(&msg);
}


static void
make_partition_label(BPartition* partition, char* label, char* menuLabel,
	bool showContentType, bool markBootDisk)
{
	char size[20];
	string_for_size(partition->Size(), size, sizeof(size));

	BPath path;
	partition->GetPath(&path);

	BString bootMark;
	if (markBootDisk) {
		bootMark.SetTo(B_TRANSLATE_COMMENT(" (boot disk)",
			"Marks EFI partitions on boot disk - preserve leading space"));
	}

	if (showContentType) {
		const char* type = partition->ContentType();
		if (type == NULL)
			type = B_TRANSLATE_COMMENT("Unknown type", "Partition content type");

		sprintf(label, "%s%s - %s [%s] (%s)",
			partition->ContentName().String(), bootMark.String(),
			size, path.Path(), type);
	} else {
		sprintf(label, "%s%s - %s [%s]",
			partition->ContentName().String(), bootMark.String(),
			size, path.Path());
	}

	sprintf(menuLabel, "%s%s - %s",
		partition->ContentName().String(), bootMark.String(), size);
}


// #pragma mark - SourceVisitor


SourceVisitor::SourceVisitor(BMenu* menu, off_t* maxSourceSize)
	:
	fMenu(menu),
	fMaxSourceSize(maxSourceSize)
{
}


bool
SourceVisitor::Visit(BDiskDevice* device)
{
	return Visit(device, 0);
}


bool
SourceVisitor::Visit(BPartition* partition, int32 level)
{
	if (partition->ContentType() == NULL)
		return false;

	bool isBootPartition = false;
	if (partition->IsMounted()) {
		BPath mountPoint;
		if (partition->GetMountPoint(&mountPoint) != B_OK)
			return false;
		isBootPartition = strcmp(kBootPath, mountPoint.Path()) == 0;
	}

	if (!isBootPartition
		&& strcmp(partition->ContentType(), kPartitionTypeBFS) != 0) {
		return false;
	}

	if (!isBootPartition && !_ContainsHaikuSystem(partition)) {
		printf("Skipping %s - no Haiku system found\n",
			partition->ContentName().String());
		return false;
	}

	if (fMaxSourceSize != NULL && partition->ContentSize() > *fMaxSourceSize)
		*fMaxSourceSize = partition->ContentSize();

	char label[255];
	char menuLabel[255];
	make_partition_label(partition, label, menuLabel, false, false);
	PartitionMenuItem* item = new PartitionMenuItem(partition->ContentName(),
		label, menuLabel, new BMessage(SOURCE_PARTITION), partition->ID());
	item->SetMarked(isBootPartition);
	fMenu->AddItem(item);
	return false;
}


/*static*/ bool
SourceVisitor::_ContainsHaikuSystem(BPartition* partition)
{
	if (!partition->IsMounted())
		return true;

	BPath mountPoint;
	if (partition->GetMountPoint(&mountPoint) != B_OK)
		return true;

	BPath systemPath(mountPoint.Path(), "system/packages");
	BEntry systemEntry(systemPath.Path());

	return systemEntry.Exists();
}


// #pragma mark - TargetVisitor


TargetVisitor::TargetVisitor(BMenu* menu, off_t minTargetSize)
	:
	fMenu(menu),
	fMinTargetSize(minTargetSize)
{
}


bool
TargetVisitor::Visit(BDiskDevice* device)
{
	if (device->IsReadOnlyMedia())
		return false;
	return Visit(device, 0);
}


bool
TargetVisitor::Visit(BPartition* partition, int32 level)
{
	if (partition->ContentSize() < fMinTargetSize)
		return false;

	if (partition->CountChildren() > 0)
		return false;

	// TODO: After running DriveSetup and doing another scan, it would
	// be great to pick the partition which just appeared!

	bool isBootPartition = false;
	if (partition->IsMounted()) {
		BPath mountPoint;
		partition->GetMountPoint(&mountPoint);
		isBootPartition = strcmp(kBootPath, mountPoint.Path()) == 0;
	}

	// Only writable non-boot BFS partitions are valid targets, but we want to
	// display the other partitions as well, to inform the user that they are
	// detected but somehow not appropriate.
	bool isValidTarget = isBootPartition == false
		&& !partition->IsReadOnly()
		&& partition->ContentType() != NULL
		&& strcmp(partition->ContentType(), kPartitionTypeBFS) == 0;

	char label[255];
	char menuLabel[255];
	make_partition_label(partition, label, menuLabel, !isValidTarget, false);
	PartitionMenuItem* item = new PartitionMenuItem(partition->ContentName(),
		label, menuLabel, new BMessage(TARGET_PARTITION), partition->ID());

	item->SetIsValidTarget(isValidTarget);

	fMenu->AddItem(item);
	return false;
}


// #pragma mark - EFIVisitor


EFIVisitor::EFIVisitor(BMenu* menu, partition_id bootId)
	:
	fMenu(menu),
	fBootId(bootId)
{
}


bool
EFIVisitor::Visit(BDiskDevice* device)
{
	if (device->IsReadOnlyMedia())
		return false;
	return Visit(device, 0);
}


bool
EFIVisitor::Visit(BPartition* partition, int32 level)
{
	if (partition->IsReadOnly()
		|| partition->ContentSize() < kESPSize
		|| partition->CountChildren() > 0
		|| partition->Type() == NULL
		|| strcmp(partition->Type(), kESPTypeName) != 0
		|| partition->ContentType() == NULL
		|| strcmp(partition->ContentType(), kPartitionTypeFAT32) != 0
		|| partition->Parent() == NULL
		|| partition->Parent()->ContentType() == NULL
		|| strcmp(partition->Parent()->ContentType(), kPartitionTypeEFI) != 0) {
		return false;
	}

	char label[255];
	char menuLabel[255];
	make_partition_label(partition, label, menuLabel, false,
		partition->Parent()->ID() == fBootId);
	BMessage* message = new BMessage(EFI_PARTITION);
	message->AddInt32("id", partition->ID());
	BMenuItem* item = new BMenuItem(label, message);
	fMenu->AddItem(item);
	return false;
}
