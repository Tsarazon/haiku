/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2005-2008, Jérôme DUVAL.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "WorkerThread.h"

#include <errno.h>
#include <new>
#include <stdio.h>

#include <set>
#include <string>
#include <strings.h>

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Directory.h>
#include <DiskDeviceVisitor.h>
#include <DiskDeviceTypes.h>
#include <FindDirectory.h>
#include <fs_index.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <String.h>
#include <VolumeRoster.h>

#include "AutoLocker.h"
#include "CopyEngine.h"
#include "InstallerDefs.h"
#include "PackageViews.h"
#include "PartitionMenuItem.h"
#include "ProgressReporter.h"
#include "StringForSize.h"
#include "UnzipEngine.h"


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

const char BOOT_PATH[] = "/boot";

const uint32 MSG_START_INSTALLING = 'eSRT';


class SourceVisitor : public BDiskDeviceVisitor {
public:
	SourceVisitor(BMenu* menu);
	virtual bool Visit(BDiskDevice* device);
	virtual bool Visit(BPartition* partition, int32 level);

private:
	BMenu* fMenu;
};


class TargetVisitor : public BDiskDeviceVisitor {
public:
	TargetVisitor(BMenu* menu);
	virtual bool Visit(BDiskDevice* device);
	virtual bool Visit(BPartition* partition, int32 level);

private:
	BMenu* fMenu;
};


// #pragma mark - WorkerThread


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
			_PerformInstall(message->GetInt32("source", -1),
				message->GetInt32("target", -1));
			break;

		case MSG_WRITE_BOOT_SECTOR:
		{
			int32 id;
			if (message->FindInt32("id", &id) != B_OK) {
				_SetStatusMessage(B_TRANSLATE("Boot sector not written "
					"because of an internal error."));
				break;
			}

			// TODO: Refactor with _PerformInstall()
			BPath targetDirectory;
			BDiskDevice device;
			BPartition* partition;

			if (fDDRoster.GetPartitionWithID(id, &device, &partition) == B_OK) {
				if (!partition->IsMounted()) {
					if (partition->Mount() < B_OK) {
						_SetStatusMessage(B_TRANSLATE("The partition can't be "
							"mounted. Please choose a different partition."));
						break;
					}
				}
				if (partition->GetMountPoint(&targetDirectory) != B_OK) {
					_SetStatusMessage(B_TRANSLATE("The mount point could not "
						"be retrieved."));
					break;
				}
			} else if (fDDRoster.GetDeviceWithID(id, &device) == B_OK) {
				if (!device.IsMounted()) {
					if (device.Mount() < B_OK) {
						_SetStatusMessage(B_TRANSLATE("The disk can't be "
							"mounted. Please choose a different disk."));
						break;
					}
				}
				if (device.GetMountPoint(&targetDirectory) != B_OK) {
					_SetStatusMessage(B_TRANSLATE("The mount point could not "
						"be retrieved."));
					break;
				}
			}

			if (_WriteBootSector(targetDirectory) != B_OK) {
				_SetStatusMessage(
					B_TRANSLATE("Error writing boot sector."));
				break;
			}
			_SetStatusMessage(
				B_TRANSLATE("Boot sector successfully written."));
		}
		default:
			BLooper::MessageReceived(message);
	}
}




void
WorkerThread::ScanDisksPartitions(BMenu *srcMenu, BMenu *targetMenu)
{
	// NOTE: This is actually executed in the window thread.
	BDiskDevice device;
	BPartition *partition = NULL;

	SourceVisitor srcVisitor(srcMenu);
	fDDRoster.VisitEachMountedPartition(&srcVisitor, &device, &partition);

	TargetVisitor targetVisitor(targetMenu);
	fDDRoster.VisitEachPartition(&targetVisitor, &device, &partition);
}


void
WorkerThread::SetPackagesList(BList *list)
{
	// Executed in window thread.
	BAutolock _(this);

	delete fPackages;
	fPackages = list;
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
	// Executed in window thread.
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


// #pragma mark -


status_t
WorkerThread::_WriteBootSector(BPath &path)
{
	BPath bootPath;
	find_directory(B_BEOS_BOOT_DIRECTORY, &bootPath);
	BString command;
	command.SetToFormat("makebootable \"%s\"", path.Path());
	_SetStatusMessage(B_TRANSLATE("Writing bootsector."));
	return system(command.String());
}


status_t
WorkerThread::_InstallEFIBootloader(const BPath& targetDirectory)
{
	_SetStatusMessage(B_TRANSLATE("Installing EFI bootloader."));

	// ESP GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
	const char* kESPTypeString = "EFI system data";

	BPath espPath;
	bool espFound = false;

	// First: Search ALL partitions (including unmounted) for ESP by GPT type
	printf("Searching for ESP partition (mounted or unmounted)...\n");

	BDiskDevice device;
	int32 cookie = 0;
	while (fDDRoster.GetNextDevice(&device, &cookie) == B_OK) {
		// Visit all partitions on this device
		struct ESPVisitor : public BDiskDeviceVisitor {
			const char* fESPType;
			BDiskDeviceRoster* fRoster;
			BPath* fEspPath;
			bool* fFound;

			ESPVisitor(const char* espType, BDiskDeviceRoster* roster,
				BPath* path, bool* found)
				: fESPType(espType), fRoster(roster),
				  fEspPath(path), fFound(found) {}

			virtual bool Visit(BPartition* partition, int32 level)
			{
				if (*fFound)
					return true; // Already found, stop

				const char* type = partition->Type();
				if (type == NULL || strcmp(type, fESPType) != 0)
					return false; // Not ESP, continue

				printf("Found ESP partition: %s\n", partition->Name());

				// Check if already mounted
				if (partition->IsMounted()) {
					BPath mountPoint;
					if (partition->GetMountPoint(&mountPoint) == B_OK) {
						*fEspPath = mountPoint;
						*fFound = true;
						printf("ESP already mounted at: %s\n", mountPoint.Path());
						return true;
					}
				}

				// Not mounted - try to mount it
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

				return false; // Continue searching
			}
		} visitor(kESPTypeString, &fDDRoster, &espPath, &espFound);

		device.VisitEachDescendant(&visitor);
		if (espFound)
			break;
	}
	
	// Second pass: Fallback to FAT partition with existing EFI directory
	// (for MBR or non-standard setups)
	if (!espFound) {
		printf("ESP not found by GUID, falling back to FAT + EFI directory detection...\n");
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

			// Check partition type - must be FAT for ESP
			BDiskDevice device;
			BPartition* partition = NULL;
			status_t result = fDDRoster.GetPartitionForPath(path.Path(),
				&device, &partition);
			if (result == B_OK && partition != NULL) {
				const char* contentType = partition->ContentType();
				// Only accept FAT partitions as potential ESP
				if (contentType == NULL ||
					(strcmp(contentType, kPartitionTypeFAT32) != 0 &&
					 strcmp(contentType, kPartitionTypeFAT16) != 0 &&
					 strcmp(contentType, kPartitionTypeFAT12) != 0)) {
					continue;
				}
			} else {
				// Can't determine partition type, skip
				continue;
			}

			// Check if this FAT partition has an existing EFI directory
			BPath efiCheckPath(path.Path(), "EFI");
			BEntry efiEntry(efiCheckPath.Path());

			if (!efiEntry.Exists()) {
				// No existing EFI directory - create it only on FAT partitions
				result = create_directory(efiCheckPath.Path(), 0755);
				if (result != B_OK && result != B_FILE_EXISTS) {
					continue;
				}
			}

			espPath = path;
			espFound = true;
			printf("Found FAT partition with EFI directory (fallback) at: %s\n",
				espPath.Path());
			break;
		}
	}
	
	if (!espFound) {
		fprintf(stderr, "Warning: ESP partition not found or not mounted\n");
		fprintf(stderr, "Please ensure you have a FAT32 partition with GPT type "
			"'EFI System' or manually mount an ESP.\n");
		return B_ERROR;
	}
	
	// Source bootloader path
	BPath loaderSource(targetDirectory.Path(), 
		"system/boot/efi/haiku_loader.efi");
	BEntry sourceEntry(loaderSource.Path());
	if (!sourceEntry.Exists()) {
		fprintf(stderr, "Error: haiku_loader.efi not found at %s\n", 
			loaderSource.Path());
		return B_ENTRY_NOT_FOUND;
	}
	
	// Create target directories: EFI/HAIKU and EFI/BOOT
	BPath haikuEfiDir(espPath.Path(), "EFI/HAIKU");
	status_t result = create_directory(haikuEfiDir.Path(), 0755);
	if (result != B_OK && result != B_FILE_EXISTS) {
		fprintf(stderr, "Error: Failed to create %s: %s\n",
			haikuEfiDir.Path(), strerror(result));
		return result;
	}
	
	BPath bootEfiDir(espPath.Path(), "EFI/BOOT");
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
		efiBootName = "BOOTX64.EFI"; // default to x86_64
		fprintf(stderr, "Warning: Unknown architecture, defaulting to BOOTX64.EFI\n");
	#endif
	
	// Copy to EFI/HAIKU/haiku_loader.efi
	BPath haikuLoaderPath(haikuEfiDir.Path(), "haiku_loader.efi");
	BEntry haikuLoaderEntry(haikuLoaderPath.Path());
	if (haikuLoaderEntry.Exists())
		haikuLoaderEntry.Remove();
	
	result = _CopyFile(loaderSource.Path(), haikuLoaderPath.Path());
	if (result != B_OK) {
		fprintf(stderr, "Error: Failed to copy bootloader to %s: %s\n",
			haikuLoaderPath.Path(), strerror(result));
		return result;
	}
	
	// CRITICAL: Copy to fallback location EFI/BOOT/BOOT{ARCH}.EFI
	// This is essential for stubborn UEFI implementations (e.g., Lenovo M720Q)
	// that ignore boot entries and only look for the fallback bootloader
	BPath fallbackPath(bootEfiDir.Path(), efiBootName);
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
	// Simple file copy helper for EFI bootloader installation
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
	
	// Copy in 64KB chunks for better performance
	const size_t kBufferSize = 65536;
	char* buffer = new(std::nothrow) char[kBufferSize];
	if (buffer == NULL)
		return B_NO_MEMORY;
	
	ssize_t bytesRead;
	while ((bytesRead = sourceFile.Read(buffer, kBufferSize)) > 0) {
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
WorkerThread::_LaunchFinishScript(BPath &path)
{
	_SetStatusMessage(B_TRANSLATE("Finishing installation."));

	BString command;
	command.SetToFormat("mkdir -p \"%s/system/cache/tmp\"", path.Path());
	if (system(command.String()) != 0)
		return B_ERROR;
	command.SetToFormat("mkdir -p \"%s/system/packages/administrative\"",
		path.Path());
	if (system(command.String()) != 0)
		return B_ERROR;

	// Ask for first boot processing of all the packages copied into the new
	// installation, since by just copying them the normal package processing
	// isn't done.  package_daemon will detect the magic file and do it.
	command.SetToFormat("echo 'First Boot written by Installer.' > "
		"\"%s/system/packages/administrative/FirstBootProcessingNeeded\"",
		path.Path());
	if (system(command.String()) != 0)
		return B_ERROR;

	command.SetToFormat("rm -f \"%s/home/Desktop/Installer\"", path.Path());
	return system(command.String());
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
	const char* mountError = B_TRANSLATE("The disk can't be mounted. Please "
		"choose a different disk.");

	if (sourcePartitionID < 0 || targetPartitionID < 0) {
		ERR("bad source or target partition ID\n");
		return _InstallationError(err);
	}

	// check if target is initialized
	// ask if init or mount as is
	if (fDDRoster.GetPartitionWithID(targetPartitionID, &device,
			&partition) == B_OK) {
		if (!partition->IsMounted()) {
			if ((err = partition->Mount()) < B_OK) {
				_SetStatusMessage(mountError);
				ERR("BPartition::Mount");
				return _InstallationError(err);
			}
		}
		if ((err = partition->GetVolume(&targetVolume)) != B_OK) {
			ERR("BPartition::GetVolume");
			return _InstallationError(err);
		}
		if ((err = partition->GetMountPoint(&targetDirectory)) != B_OK) {
			ERR("BPartition::GetMountPoint");
			return _InstallationError(err);
		}
	} else if (fDDRoster.GetDeviceWithID(targetPartitionID, &device) == B_OK) {
		if (!device.IsMounted()) {
			if ((err = device.Mount()) < B_OK) {
				_SetStatusMessage(mountError);
				ERR("BDiskDevice::Mount");
				return _InstallationError(err);
			}
		}
		if ((err = device.GetVolume(&targetVolume)) != B_OK) {
			ERR("BDiskDevice::GetVolume");
			return _InstallationError(err);
		}
		if ((err = device.GetMountPoint(&targetDirectory)) != B_OK) {
			ERR("BDiskDevice::GetMountPoint");
			return _InstallationError(err);
		}
	} else
		return _InstallationError(err);  // shouldn't happen

	// check if target has enough space
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

	if (fDDRoster.GetPartitionWithID(sourcePartitionID, &device, &partition)
			== B_OK) {
		if ((err = partition->GetMountPoint(&srcDirectory)) != B_OK) {
			ERR("BPartition::GetMountPoint");
			return _InstallationError(err);
		}
	} else if (fDDRoster.GetDeviceWithID(sourcePartitionID, &device) == B_OK) {
		if ((err = device.GetMountPoint(&srcDirectory)) != B_OK) {
			ERR("BDiskDevice::GetMountPoint");
			return _InstallationError(err);
		}
	} else
		return _InstallationError(err); // shouldn't happen

	// check not installing on itself
	if (strcmp(srcDirectory.Path(), targetDirectory.Path()) == 0) {
		_SetStatusMessage(B_TRANSLATE("You can't install the contents of a "
			"disk onto itself. Please choose a different disk."));
		return _InstallationError(err);
	}

	// check not installing on boot volume
	if (strncmp(BOOT_PATH, targetDirectory.Path(), strlen(BOOT_PATH)) == 0) {
		BAlert* alert = new BAlert("", B_TRANSLATE("Are you sure you want to "
			"install onto the current boot disk? The Installer will have to "
			"reboot your machine if you proceed."), B_TRANSLATE("OK"),
			B_TRANSLATE("Cancel"), 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() != 0) {
			_SetStatusMessage("Installation stopped.");
			return _InstallationError(err);
		}
	}

	// check if target volume's trash dir has anything in it
	// (target volume w/ only an empty trash dir is considered
	// an empty volume)
	if (find_directory(B_TRASH_DIRECTORY, &trashPath, false,
		&targetVolume) == B_OK && targetDir.SetTo(trashPath.Path()) == B_OK) {
			while (targetDir.GetNextRef(&testRef) == B_OK) {
				// Something in the Trash
				entries++;
				break;
			}
	}

	targetDir.SetTo(targetDirectory.Path());

	// check if target volume otherwise has any entries
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
			Package *p = static_cast<Package*>(fPackages->ItemAt(i));
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
		BPath targetPkgDir(targetDirectory.Path(), "system/packages");
		err = targetPkgDir.InitCheck();
		if (err != B_OK)
			return _InstallationError(err);
		for (int32 i = 0; i < count; i++) {
			Package *p = static_cast<Package*>(fPackages->ItemAt(i));
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
		UnzipEngine* engine = reinterpret_cast<UnzipEngine*>(
			unzipEngines.ItemAtFast(i));
		if (err == B_OK)
			err = engine->UnzipPackage();
		delete engine;
	}
	if (err != B_OK)
		return _InstallationError(err);

	err = _WriteBootSector(targetDirectory);
	if (err != B_OK)
		return _InstallationError(err);

	// Install UEFI bootloader to ESP partition
	// This is critical for modern UEFI systems and fixes boot issues on
	// systems like Lenovo M720Q that require the fallback bootloader location.
	// Note: This may fail gracefully on BIOS-only systems, which is expected.
	err = _InstallEFIBootloader(targetDirectory);
	if (err != B_OK) {
		// Log warning but don't fail installation
		// System might be BIOS-only or ESP not properly set up
		fprintf(stderr, "Warning: Failed to install EFI bootloader: %s\n", 
			strerror(err));
		fprintf(stderr, "This is expected on BIOS-only systems.\n");
		fprintf(stderr, "If you're installing to a UEFI system, please ensure:\n");
		fprintf(stderr, "  1. You have a FAT32 partition with GPT type 'EFI System'\n");
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
		// target does not exist, done
		return B_OK;
	if (!systemEntry.IsDirectory())
		// the system entry is a file or a symlink
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

		if (subEntry.IsDirectory() && strcmp(fileName, "settings") == 0) {
			// Keep the settings folder
			continue;
		} else if (subEntry.IsDirectory()) {
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
		if (strcmp(index->d_name, "name") == 0
			|| strcmp(index->d_name, "size") == 0
			|| strcmp(index->d_name, "last_modified") == 0) {
			continue;
		}

		index_info info;
		if (fs_stat_index(sourceDevice, index->d_name, &info) != B_OK) {
			printf("Failed to mirror index %s: fs_stat_index(): (%d) %s\n",
				index->d_name, errno, strerror(errno));
			continue;
		}

		uint32 flags = 0;
			// Flags are always 0 for the moment.
		if (fs_create_index(targetDevice, index->d_name, info.type, flags)
			!= B_OK) {
			if (errno == B_FILE_EXISTS)
				continue;
			printf("Failed to mirror index %s: fs_create_index(): (%d) %s\n",
				index->d_name, errno, strerror(errno));
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
		uint32_t	type;
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

	for (uint32 i = 0; i < sizeof(defaultIndices) / sizeof(IndexInfo); i++) {
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
		int nameLength = strlen(name);
		if (nameLength <= 0)
			continue;
		char* nameExtension = name + nameLength - 4;
		if (strcasecmp(nameExtension, ".zip") != 0)
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
WorkerThread::_SetStatusMessage(const char *status)
{
	BMessage msg(MSG_STATUS_MESSAGE);
	msg.AddString("status", status);
	fOwner.SendMessage(&msg);
}


static void
make_partition_label(BPartition* partition, char* label, char* menuLabel,
	bool showContentType)
{
	char size[20];
	string_for_size(partition->Size(), size, sizeof(size));

	BPath path;
	partition->GetPath(&path);

	if (showContentType) {
		const char* type = partition->ContentType();
		if (type == NULL)
			type = B_TRANSLATE_COMMENT("Unknown Type", "Partition content type");

		sprintf(label, "%s - %s [%s] (%s)", partition->ContentName().String(), size,
			path.Path(), type);
	} else {
		sprintf(label, "%s - %s [%s]", partition->ContentName().String(), size,
			path.Path());
	}

	sprintf(menuLabel, "%s - %s", partition->ContentName().String(), size);
}


// #pragma mark - SourceVisitor


SourceVisitor::SourceVisitor(BMenu *menu)
	: fMenu(menu)
{
}

bool
SourceVisitor::Visit(BDiskDevice *device)
{
	return Visit(device, 0);
}


bool
SourceVisitor::Visit(BPartition *partition, int32 level)
{
	BPath path;

	if (partition->ContentType() == NULL)
		return false;

	bool isBootPartition = false;
	if (partition->IsMounted()) {
		BPath mountPoint;
		if (partition->GetMountPoint(&mountPoint) != B_OK)
			return false;
		isBootPartition = strcmp(BOOT_PATH, mountPoint.Path()) == 0;
	}

	if (!isBootPartition
		&& strcmp(partition->ContentType(), kPartitionTypeBFS) != 0) {
		// Except only BFS partitions, except this is the boot partition
		// (ISO9660 with write overlay for example).
		return false;
	}

	// TODO: We could probably check if this volume contains
	// the Haiku kernel or something. Does it make sense to "install"
	// from your BFS volume containing the music collection?
	// TODO: Then the check for BFS could also be removed above.

	char label[255];
	char menuLabel[255];
	make_partition_label(partition, label, menuLabel, false);
	PartitionMenuItem* item = new PartitionMenuItem(partition->ContentName(),
		label, menuLabel, new BMessage(SOURCE_PARTITION), partition->ID());
	item->SetMarked(isBootPartition);
	fMenu->AddItem(item);
	return false;
}


// #pragma mark - TargetVisitor


TargetVisitor::TargetVisitor(BMenu *menu)
	: fMenu(menu)
{
}


bool
TargetVisitor::Visit(BDiskDevice *device)
{
	if (device->IsReadOnlyMedia())
		return false;
	return Visit(device, 0);
}


bool
TargetVisitor::Visit(BPartition *partition, int32 level)
{
	if (partition->ContentSize() < 20 * 1024 * 1024) {
		// reject partitions which are too small anyway
		// TODO: Could depend on the source size
		return false;
	}

	if (partition->CountChildren() > 0) {
		// Looks like an extended partition, or the device itself.
		// Do not accept this as target...
		return false;
	}

	// TODO: After running DriveSetup and doing another scan, it would
	// be great to pick the partition which just appeared!

	bool isBootPartition = false;
	if (partition->IsMounted()) {
		BPath mountPoint;
		partition->GetMountPoint(&mountPoint);
		isBootPartition = strcmp(BOOT_PATH, mountPoint.Path()) == 0;
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
	make_partition_label(partition, label, menuLabel, !isValidTarget);
	PartitionMenuItem* item = new PartitionMenuItem(partition->ContentName(),
		label, menuLabel, new BMessage(TARGET_PARTITION), partition->ID());

	item->SetIsValidTarget(isValidTarget);


	fMenu->AddItem(item);
	return false;
}

