/*
 * Copyright 2007-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include "vfs_boot.h"

#include <stdio.h>
#include <strings.h>

#include <fs_info.h>
#include <OS.h>

#include <AutoDeleter.h>
#include <boot/kernel_args.h>
#include <directories.h>
#include <disk_device_manager/KDiskDevice.h>
#include <disk_device_manager/KDiskDeviceManager.h>
#include <disk_device_manager/KPartitionVisitor.h>
#include <DiskDeviceTypes.h>
#include <file_cache.h>
#include <fs/KPath.h>
#include <kmodule.h>
#include <syscalls.h>
#include <util/KMessage.h>
#include <util/Stack.h>
#include <vfs.h>

#include "vfs_net_boot.h"


//#define TRACE_VFS
#ifdef TRACE_VFS
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// Constants for boot device detection and retry logic
static const int32 kMaxRetryAttempts = 10;
static const bigtime_t kRetryDelayMicros = 1000000;  // 1 second
static const int32 kChecksumBufferSize = 512;
static const int32 kMaxPathLength = 256;
static const uint32 kMaxDeviceNameLength = 128;

// Boot device identification
typedef Stack<KPartition *> PartitionStack;

static struct {
	const char *path;
	const char *target;
} sPredefinedLinks[] = {
	{ kGlobalSystemDirectory,		kSystemDirectory },
	{ kGlobalBinDirectory,			kSystemBinDirectory },
	{ kGlobalEtcDirectory,			kSystemEtcDirectory },
	{ kGlobalTempDirectory,			kSystemTempDirectory },
	{ kGlobalVarDirectory,			kSystemVarDirectory },
	{ kGlobalPackageLinksDirectory,	kSystemPackageLinksDirectory },
	{NULL, NULL}
};

// Global boot device state - accessed only during boot in single-threaded context
dev_t gBootDevice = -1;
bool gReadOnlyBootDevice = false;


// Forward declarations
static uint32 compute_check_sum(KDiskDevice* device, off_t offset);
static bool validate_partition(KPartition* partition, KDiskDevice* device);


//	#pragma mark - Partition Comparison Functions


/*!	Compare partitions when no specific boot image was selected.
	Prefer partitions with names like "Haiku" or "System".
	Returns: positive if 'a' should come before 'b', negative if 'b' before 'a', 0 if equal.
*/
int
compare_image_boot(const void* _a, const void* _b)
{
	if (_a == NULL || _b == NULL)
		return 0;

	KPartition* a = *(KPartition**)_a;
	KPartition* b = *(KPartition**)_b;

	if (a == NULL || b == NULL)
		return 0;

	const char* aName = a->ContentName();
	const char* bName = b->ContentName();

	// Handle NULL names - partitions with names come first
	if (aName != NULL && bName == NULL)
		return 1;
	if (aName == NULL && bName != NULL)
		return -1;
	if (aName == NULL && bName == NULL)
		return 0;

	// Prefer "Haiku" over other names
	bool aIsHaiku = strcasecmp(aName, "Haiku") == 0;
	bool bIsHaiku = strcasecmp(bName, "Haiku") == 0;
	if (aIsHaiku && !bIsHaiku)
		return 1;
	if (!aIsHaiku && bIsHaiku)
		return -1;

	// Prefer names starting with "System"
	bool aIsSystem = strncmp(aName, "System", 6) == 0;
	bool bIsSystem = strncmp(bName, "System", 6) == 0;
	if (aIsSystem && !bIsSystem)
		return 1;
	if (!aIsSystem && bIsSystem)
		return -1;

	// Fall back to alphabetical comparison
	return strcasecmp(aName, bName);
}


/*!	Compare partitions when booted from CD.
	Prefer CD partitions (data sessions) over other media.
	Falls back to compare_image_boot() for non-CD partitions.
*/
static int
compare_cd_boot(const void* _a, const void* _b)
{
	if (_a == NULL || _b == NULL)
		return 0;

	KPartition* a = *(KPartition**)_a;
	KPartition* b = *(KPartition**)_b;

	if (a == NULL || b == NULL)
		return 0;

	const char* aType = a->Type();
	const char* bType = b->Type();

	bool aIsCD = (aType != NULL && strcmp(aType, kPartitionTypeDataSession) == 0);
	bool bIsCD = (bType != NULL && strcmp(bType, kPartitionTypeDataSession) == 0);

	// Prefer CD partitions
	int compare = (int)aIsCD - (int)bIsCD;
	if (compare != 0)
		return compare;

	return compare_image_boot(_a, _b);
}


//	#pragma mark - Partition Validation


/*!	Validates that a partition has sane values.
	Checks offset, size, and block size for corruption or invalid data.
*/
static bool
validate_partition(KPartition* partition, KDiskDevice* device)
{
	if (partition == NULL || device == NULL) {
		dprintf("validate_partition: NULL partition or device\n");
		return false;
	}

	// Check basic sanity
	if (partition->Offset() < 0 || partition->Size() <= 0) {
		dprintf("validate_partition: invalid offset (%" B_PRIdOFF
			") or size (%" B_PRIdOFF ")\n",
			partition->Offset(), partition->Size());
		return false;
	}

	// Check partition doesn't exceed device (overflow-safe)
	if (partition->Offset() > device->Size()) {
		dprintf("validate_partition: partition offset %" B_PRIdOFF
			" exceeds device size %" B_PRIdOFF "\n",
			partition->Offset(), device->Size());
		return false;
	}

	if (partition->Size() > device->Size() - partition->Offset()) {
		dprintf("validate_partition: partition extends beyond device "
			"(offset %" B_PRIdOFF " + size %" B_PRIdOFF
			" > device %" B_PRIdOFF ")\n",
			partition->Offset(), partition->Size(), device->Size());
		return false;
	}

	// Check for suspiciously large block sizes (corrupted partition table)
	if (partition->BlockSize() > 1024 * 1024) {
		dprintf("validate_partition: suspicious block size %u\n",
			partition->BlockSize());
		return false;
	}

	return true;
}


//	#pragma mark - Checksum Computation


/*!	Computes a checksum for a block at the specified offset.
	The checksum is the sum of all data interpreted as uint32 values.
	This must use the same method as in boot loader platform code
	(e.g., boot/platform/bios_ia32/devices.cpp).
*/
static uint32
compute_check_sum(KDiskDevice* device, off_t offset)
{
	if (device == NULL) {
		dprintf("compute_check_sum: NULL device\n");
		return 0;
	}

	char buffer[kChecksumBufferSize];
	ssize_t bytesRead = read_pos(device->FD(), offset, buffer, sizeof(buffer));
	if (bytesRead < B_OK) {
		TRACE(("compute_check_sum: read error at offset %lld: %s\n",
			offset, strerror(bytesRead)));
		return 0;
	}

	// Zero remaining buffer if partial read
	if (bytesRead < (ssize_t)sizeof(buffer))
		memset(buffer + bytesRead, 0, sizeof(buffer) - bytesRead);

	// Calculate checksum with overflow protection
	uint32* array = (uint32*)buffer;
	uint32 sum = 0;
	uint32 elementCount = (bytesRead + sizeof(uint32) - 1) / sizeof(uint32);

	for (uint32 i = 0; i < elementCount; i++) {
		uint32 oldSum = sum;
		sum += array[i];
		// Detect overflow (not critical for checksum, but good to track)
		if (sum < oldSum) {
			TRACE(("compute_check_sum: overflow at element %u\n", i));
		}
	}

	return sum;
}


//	#pragma mark - BootMethod Base Class


BootMethod::BootMethod(const KMessage& bootVolume, int32 method)
	:
	fBootVolume(bootVolume),
	fMethod(method)
{
}


BootMethod::~BootMethod()
{
}


status_t
BootMethod::Init()
{
	return B_OK;
}


//	#pragma mark - DiskBootMethod Implementation


class DiskBootMethod : public BootMethod {
public:
	DiskBootMethod(const KMessage& bootVolume, int32 method)
		: BootMethod(bootVolume, method)
	{
	}

	virtual bool IsBootDevice(KDiskDevice* device, bool strict);
	virtual bool IsBootPartition(KPartition* partition, bool& foundForSure);
	virtual void SortPartitions(KPartition** partitions, int32 count);
};


bool
DiskBootMethod::IsBootDevice(KDiskDevice* device, bool strict)
{
	if (device == NULL) {
		dprintf("DiskBootMethod::IsBootDevice: NULL device\n");
		return false;
	}

	disk_identifier* disk;
	int32 diskIdentifierSize;
	status_t status = fBootVolume.FindData(BOOT_VOLUME_DISK_IDENTIFIER,
		B_RAW_TYPE, (const void**)&disk, &diskIdentifierSize);
	if (status != B_OK) {
		dprintf("DiskBootMethod::IsBootDevice: no disk identifier in boot volume\n");
		return false;
	}

	if (diskIdentifierSize < (int32)sizeof(disk_identifier)) {
		dprintf("DiskBootMethod::IsBootDevice: disk identifier size too small: %d\n",
			diskIdentifierSize);
		return false;
	}

	TRACE(("boot device: bus %" B_PRId32 ", device %" B_PRId32 "\n",
		disk->bus_type, disk->device_type));

	// CD boots only from removable media
	if (fMethod == BOOT_METHOD_CD && !device->IsRemovable())
		return false;

	// Bus-specific identification
	switch (disk->bus_type) {
		case PCI_BUS:
		case LEGACY_BUS:
			// TODO: Implement PCI/legacy bus device identification
			// Requires device_node access for proper matching
			break;

		case UNKNOWN_BUS:
			// No bus-specific checks needed
			break;

		default:
			TRACE(("IsBootDevice: unknown bus type %d\n", disk->bus_type));
			break;
	}

	// Device-specific identification
	switch (disk->device_type) {
		case UNKNOWN_DEVICE:
			// Validate size if in strict mode
			if (strict && device->Size() != disk->device.unknown.size) {
				TRACE(("IsBootDevice: size mismatch: device %lld != boot %lld\n",
					device->Size(), disk->device.unknown.size));
				return false;
			}

			// Skip checksum validation for CD (unreliable data at boot time)
			if (fMethod == BOOT_METHOD_CD)
				break;

			// Verify checksums
			for (int32 i = 0; i < NUM_DISK_CHECK_SUMS; i++) {
				if (disk->device.unknown.check_sums[i].offset == -1)
					continue;

				off_t offset = disk->device.unknown.check_sums[i].offset;

				// Validate offset is within device bounds
				if (offset < 0 || offset >= device->Size()
					|| offset > device->Size() - kChecksumBufferSize) {
					TRACE(("IsBootDevice: invalid checksum offset %lld "
						"(device size %lld)\n", offset, device->Size()));
					return false;
				}

				uint32 expectedSum = disk->device.unknown.check_sums[i].sum;
				uint32 actualSum = compute_check_sum(device, offset);

				if (actualSum != expectedSum) {
					TRACE(("IsBootDevice: checksum mismatch at offset %lld: "
						"expected %u, got %u\n", offset, expectedSum, actualSum));
					return false;
				}
			}
			break;

		case ATA_DEVICE:
		case ATAPI_DEVICE:
		case SCSI_DEVICE:
		case USB_DEVICE:
		case FIREWIRE_DEVICE:
		case FIBRE_DEVICE:
			// TODO: Implement specific device type matching
			// Requires additional device information from firmware
			TRACE(("IsBootDevice: device type %d not fully implemented\n",
				disk->device_type));
			break;

		default:
			dprintf("IsBootDevice: unknown device type %d\n", disk->device_type);
			return false;
	}

	return true;
}


bool
DiskBootMethod::IsBootPartition(KPartition* partition, bool& foundForSure)
{
	if (partition == NULL) {
		dprintf("DiskBootMethod::IsBootPartition: NULL partition\n");
		return false;
	}

	foundForSure = false;

	off_t bootPartitionOffset = fBootVolume.GetInt64(
		BOOT_VOLUME_PARTITION_OFFSET, 0);

	if (!fBootVolume.GetBool(BOOT_VOLUME_BOOTED_FROM_IMAGE, false)) {
		// Simple case: boot from selected device
		if (partition->Offset() == bootPartitionOffset) {
			dprintf("Identified boot partition by offset match: %" B_PRIdOFF "\n",
				bootPartitionOffset);
			foundForSure = true;
			return true;
		}
		return false;
	}

	// Booted from image - need special handling
	if (fMethod == BOOT_METHOD_CD) {
		// Check for anyboot CD
		KDiskDevice* device = partition->Device();
		if (device != NULL && IsBootDevice(device, false)
			&& bootPartitionOffset == 0
			&& partition->Parent() == device) {
			
			const char* deviceContentType = device->ContentType();
			const char* partitionContentType = partition->ContentType();

			// Anyboot: Intel partition table on CD with BFS partition
			if (deviceContentType != NULL && partitionContentType != NULL
				&& strcmp(deviceContentType, kPartitionTypeIntel) == 0
				&& strcmp(partitionContentType, kPartitionTypeBFS) == 0) {
				dprintf("Identified anyboot CD\n");
				foundForSure = true;
				return true;
			}
		}

		// For user-selected CD boot, ignore non-session partitions
		if (fBootVolume.GetBool(BOOT_VOLUME_USER_SELECTED, false)) {
			const char* type = partition->Type();
			if (type == NULL || strcmp(type, kPartitionTypeDataSession) != 0) {
				return false;
			}
		}
	}

	// Accept any BFS or ISO9660 partition when booted from image
	const char* contentType = partition->ContentType();
	if (contentType != NULL
		&& (strcmp(contentType, kPartitionTypeBFS) == 0
			|| strcmp(contentType, kPartitionTypeISO9660) == 0)) {
		TRACE(("IsBootPartition: accepting partition with type %s\n", contentType));
		return true;
	}

	return false;
}


void
DiskBootMethod::SortPartitions(KPartition** partitions, int32 count)
{
	if (partitions == NULL || count <= 0)
		return;

	qsort(partitions, count, sizeof(KPartition*),
		fMethod == BOOT_METHOD_CD ? compare_cd_boot : compare_image_boot);
}


//	#pragma mark - Boot Partition Discovery


/*!	Identifies potential boot partitions and adds them to the stack.
	The most likely boot partition is placed on top.
	
	@param bootVolume Boot volume configuration from bootloader
	@param partitions Output stack of potential boot partitions
	@return B_OK on success, error code otherwise
*/
static status_t
get_boot_partitions(KMessage& bootVolume, PartitionStack& partitions)
{
	dprintf("get_boot_partitions: boot volume message:\n");
	bootVolume.Dump(&dprintf);

	// Determine boot method from bootloader
	int32 bootMethodType = bootVolume.GetInt32(BOOT_METHOD, BOOT_METHOD_DEFAULT);
	dprintf("get_boot_partitions: boot method type: %" B_PRId32 "\n",
		bootMethodType);

	// Create appropriate boot method handler
	BootMethod* bootMethod = NULL;
	switch (bootMethodType) {
		case BOOT_METHOD_NET:
			bootMethod = new(nothrow) NetBootMethod(bootVolume, bootMethodType);
			break;

		case BOOT_METHOD_HARD_DISK:
		case BOOT_METHOD_CD:
		default:
			bootMethod = new(nothrow) DiskBootMethod(bootVolume, bootMethodType);
			break;
	}

	if (bootMethod == NULL) {
		dprintf("get_boot_partitions: failed to allocate boot method\n");
		return B_NO_MEMORY;
	}

	// Ensure bootMethod is always deleted on all code paths
	ObjectDeleter<BootMethod> bootMethodDeleter(bootMethod);

	status_t status = bootMethod->Init();
	if (status != B_OK) {
		dprintf("get_boot_partitions: boot method init failed: %s\n",
			strerror(status));
		return status;
	}

	// Initialize disk device manager
	status = KDiskDeviceManager::CreateDefault();
	if (status != B_OK) {
		dprintf("get_boot_partitions: failed to create device manager: %s\n",
			strerror(status));
		return status;
	}

	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (manager == NULL) {
		dprintf("get_boot_partitions: device manager is NULL\n");
		return B_ERROR;
	}

	// Scan for disk systems (BFS, FAT, ISO9660, etc.) before devices
	dprintf("Scanning for disk systems...\n");
	manager->RescanDiskSystems();

	// Perform initial device scan
	status = manager->InitialDeviceScan();
	if (status != B_OK) {
		dprintf("get_boot_partitions: InitialDeviceScan failed: %s\n",
			strerror(status));
		// Continue despite errors - some partitions may still be usable
	}

#if KDEBUG
	// Dump detected devices for debugging
	KDiskDevice *device;
	int32 cookie = 0;
	while ((device = manager->NextDevice(&cookie)) != NULL) {
		device->Dump(true, 0);
	}
#endif

	// Visitor to find boot partitions
	struct BootPartitionVisitor : KPartitionVisitor {
		BootPartitionVisitor(BootMethod* method, PartitionStack &stack)
			: fPartitions(stack),
			  fBootMethod(method)
		{
		}

		virtual bool VisitPre(KPartition *partition)
		{
			if (partition == NULL)
				return false;

			if (!partition->ContainsFileSystem())
				return false;

			// Validate partition sanity before considering it
			KDiskDevice* device = partition->Device();
			if (device == NULL) {
				TRACE(("BootPartitionVisitor: partition has NULL device\n"));
				return false;
			}

			if (!validate_partition(partition, device)) {
				TRACE(("BootPartitionVisitor: partition failed validation\n"));
				return false;
			}

			bool foundForSure = false;
			if (fBootMethod->IsBootPartition(partition, foundForSure)) {
				status_t status = fPartitions.Push(partition);
				if (status != B_OK) {
					dprintf("BootPartitionVisitor: failed to push partition\n");
				}
			}

			// Stop searching if found definitive match
			return foundForSure;
		}

	private:
		PartitionStack	&fPartitions;
		BootMethod*		fBootMethod;
	} visitor(bootMethod, partitions);

	// Device detection with retry for slow USB devices
	bool strict = true;
	int32 retryCount = 0;

	while (true) {
		// Scan all devices for boot partitions
		KDiskDevice *device;
		int32 cookie = 0;
		while ((device = manager->NextDevice(&cookie)) != NULL) {
			if (!bootMethod->IsBootDevice(device, strict))
				continue;

			// Visit partitions on this device
			if (device->VisitEachDescendant(&visitor) != NULL) {
				// Found definitive boot partition, stop searching
				break;
			}
		}

		if (!partitions.IsEmpty()) {
			dprintf("Found boot partition(s) on attempt %d\n", retryCount + 1);
			break;
		}

		// Retry logic for USB and slow devices
		if (!strict) {
			// Already tried non-strict mode
			if (retryCount >= kMaxRetryAttempts) {
				dprintf("get_boot_partitions: max retries exceeded\n");
				break;
			}

			// Wait and rescan for USB devices
			retryCount++;
			dprintf("Boot partition not found, waiting %" B_PRId64 " ms "
				"(retry %d/%d)...\n",
				kRetryDelayMicros / 1000, retryCount, kMaxRetryAttempts);
			
			snooze(kRetryDelayMicros);
			
			// Rescan disk systems and devices
			manager->RescanDiskSystems();
			status = manager->InitialDeviceScan();
			if (status != B_OK) {
				dprintf("get_boot_partitions: rescan failed: %s\n",
					strerror(status));
			}
			continue;
		}

		// First retry: relax matching criteria
		dprintf("get_boot_partitions: trying non-strict mode\n");
		strict = false;
	}

	// Sort partitions by preference
	if (!partitions.IsEmpty()
		&& !bootVolume.GetBool(BOOT_VOLUME_USER_SELECTED, false)) {
		bootMethod->SortPartitions(partitions.Array(), partitions.CountItems());
	}

	// bootMethod will be automatically deleted by ObjectDeleter
	return B_OK;
}


//	#pragma mark - VFS Bootstrap


status_t
vfs_bootstrap_file_systems(void)
{
	// Mount root filesystem
	status_t status = _kern_mount("/", NULL, "rootfs", 0, NULL, 0);
	if (status < B_OK) {
		panic("vfs_bootstrap_file_systems: failed to mount rootfs: %s\n",
			strerror(status));
	}

	_kern_setcwd(-1, "/");

	// Mount devfs
	status = _kern_create_dir(-1, "/dev", 0755);
	if (status < B_OK && status != B_FILE_EXISTS) {
		panic("vfs_bootstrap_file_systems: failed to create /dev: %s\n",
			strerror(status));
	}

	status = _kern_mount("/dev", NULL, "devfs", 0, NULL, 0);
	if (status < B_OK) {
		panic("vfs_bootstrap_file_systems: failed to mount devfs: %s\n",
			strerror(status));
	}

	// Create boot volume mount point
	status = _kern_create_dir(-1, "/boot", 0755);
	if (status < B_OK && status != B_FILE_EXISTS) {
		dprintf("vfs_bootstrap_file_systems: warning: failed to create /boot: %s\n",
			strerror(status));
	}

	// Create predefined symbolic links
	for (int32 i = 0; sPredefinedLinks[i].path != NULL; i++) {
		_kern_create_symlink(-1, sPredefinedLinks[i].path,
			sPredefinedLinks[i].target, 0777);
		// Ignore errors - links may already exist or be unsupported
	}

	return B_OK;
}


void
vfs_mount_boot_file_system(kernel_args* args)
{
	if (args == NULL) {
		panic("vfs_mount_boot_file_system: NULL kernel_args\n");
	}

	// Parse boot volume information from bootloader
	KMessage bootVolume;
	status_t status = bootVolume.SetTo(args->boot_volume, args->boot_volume_size);
	if (status != B_OK) {
		panic("vfs_mount_boot_file_system: invalid boot volume message: %s\n",
			strerror(status));
	}

	// Find potential boot partitions
	PartitionStack partitions;
	status = get_boot_partitions(bootVolume, partitions);
	if (status < B_OK) {
		panic("vfs_mount_boot_file_system: get_boot_partitions failed: %s\n",
			strerror(status));
	}

	int32 partitionCount = partitions.CountItems();
	dprintf("vfs_mount_boot_file_system: Found %" B_PRId32 " potential boot partition(s)\n",
		partitionCount);

	if (partitions.IsEmpty()) {
		// Dump diagnostic information
		dprintf("\n=== BOOT VOLUME INFO ===\n");
		bootVolume.Dump(&dprintf);
		
		dprintf("\n=== ALL DETECTED DEVICES ===\n");
		KDiskDeviceManager *manager = KDiskDeviceManager::Default();
		if (manager != NULL) {
			KDiskDevice *device;
			int32 cookie = 0;
			int deviceCount = 0;
			while ((device = manager->NextDevice(&cookie)) != NULL) {
				dprintf("Device %d:\n", deviceCount++);
				device->Dump(true, 0);
			}
			if (deviceCount == 0) {
				dprintf("No devices detected by disk device manager\n");
			}
		} else {
			dprintf("Disk device manager not available\n");
		}
		
		panic("vfs_mount_boot_file_system: no boot partitions found\n");
	}

	// Try to mount each partition until one succeeds
	dev_t bootDevice = -1;
	KPartition* bootPartition;
	
	while (partitions.Pop(&bootPartition)) {
		if (bootPartition == NULL) {
			dprintf("vfs_mount_boot_file_system: warning: NULL partition in stack\n");
			continue;
		}

		KPath path;
		if (bootPartition->GetPath(&path) != B_OK) {
			dprintf("vfs_mount_boot_file_system: failed to get partition path\n");
			continue;
		}

		const char* fsName = NULL;
		bool readOnly = false;

		// Determine filesystem and mount options
		const char* contentType = bootPartition->ContentType();
		if (contentType == NULL) {
			dprintf("vfs_mount_boot_file_system: partition has NULL content type\n");
			continue;
		}

		if (strcmp(contentType, kPartitionTypeISO9660) == 0) {
			// ISO9660 with write overlay for modifications
			fsName = "iso9660:write_overlay:attribute_overlay";
			readOnly = true;
		} else if (bootPartition->IsReadOnly()
			&& strcmp(contentType, kPartitionTypeBFS) == 0) {
			// Read-only BFS with write overlay
			fsName = "bfs:write_overlay";
			readOnly = true;
		}

		TRACE(("trying to mount boot partition: %s\n", path.Path()));

		bootDevice = _kern_mount("/boot", path.Path(), fsName, 0, NULL, 0);
		if (bootDevice >= 0) {
			dprintf("Mounted boot partition: %s\n", path.Path());
			gReadOnlyBootDevice = readOnly;
			break;
		}

		dprintf("Failed to mount %s: %s\n", path.Path(), strerror(bootDevice));
	}

	if (bootDevice < B_OK) {
		panic("vfs_mount_boot_file_system: could not mount any boot device\n");
	}

	// Create symbolic link for volume name
	fs_info info;
	if (_kern_read_fs_info(bootDevice, &info) == B_OK) {
		char linkPath[B_FILE_NAME_LENGTH + 2];
		int result = snprintf(linkPath, sizeof(linkPath), "/%s", info.volume_name);
		if (result > 0 && result < (int)sizeof(linkPath)) {
			_kern_create_symlink(-1, linkPath, "/boot", 0777);
		} else {
			dprintf("vfs_mount_boot_file_system: volume name too long\n");
		}
	}

	// Mount packagefs if needed
	struct stat st;
	bool isPackaged = bootVolume.GetBool(BOOT_VOLUME_PACKAGED, false);
	bool hasPackages = (bootVolume.GetBool(BOOT_VOLUME_BOOTED_FROM_IMAGE, false)
		&& lstat(kSystemPackagesDirectory, &st) == 0);

	if (isPackaged || hasPackages) {
		static const char* const kPackageFSName = "packagefs";

		// Mount system packagefs
		char arguments[kMaxPathLength];
		strlcpy(arguments, "packages /boot/system/packages; type system",
			sizeof(arguments));

		const char* stateName = bootVolume.GetString(BOOT_VOLUME_PACKAGES_STATE, NULL);
		if (stateName != NULL) {
			strlcat(arguments, "; state ", sizeof(arguments));
			strlcat(arguments, stateName, sizeof(arguments));
		}

		dev_t packageMount = _kern_mount("/boot/system", NULL, kPackageFSName,
			0, arguments, 0);
		if (packageMount < 0) {
			panic("vfs_mount_boot_file_system: failed to mount system packagefs: %s\n",
				strerror(packageMount));
		}

		// Mount home packagefs (non-fatal if this fails)
		packageMount = _kern_mount("/boot/home/config", NULL, kPackageFSName, 0,
			"packages /boot/home/config/packages; type home", 0);
		if (packageMount < 0) {
			dprintf("vfs_mount_boot_file_system: failed to mount home packagefs: %s\n",
				strerror(packageMount));
		}
	}

	// Boot volume is ready - set global state
	gBootDevice = bootDevice;

	// Initialize post-boot-device module system
	int32 bootMethodType = bootVolume.GetInt32(BOOT_METHOD, BOOT_METHOD_DEFAULT);
	bool bootingFromBootLoaderVolume = (bootMethodType == BOOT_METHOD_HARD_DISK
		|| bootMethodType == BOOT_METHOD_CD);
	
	status = module_init_post_boot_device(bootingFromBootLoaderVolume);
	if (status != B_OK) {
		dprintf("vfs_mount_boot_file_system: module_init_post_boot_device failed: %s\n",
			strerror(status));
	}

	// Initialize file cache
	status = file_cache_init_post_boot_device();
	if (status != B_OK) {
		dprintf("vfs_mount_boot_file_system: file_cache_init failed: %s\n",
			strerror(status));
	}

	// Start monitoring for disk changes
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (manager != NULL) {
		manager->RescanDiskSystems();
		manager->StartMonitoring();
	}
}
