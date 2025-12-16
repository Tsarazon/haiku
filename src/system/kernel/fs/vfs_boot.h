/*
 * Copyright 2007, Ingo Weinhold, bonefish@cs.tu-berlin.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _VFS_BOOT_H
#define _VFS_BOOT_H


#include <disk_device_manager/KDiskDevice.h>
#include <util/KMessage.h>


/*!	Abstract base class for boot device detection strategies.
	Different boot methods (hard disk, CD, network) implement
	specific device identification and partition sorting logic.
*/
class BootMethod {
public:
	BootMethod(const KMessage& bootVolume, int32 method);
	virtual ~BootMethod();

	/*!	Initialize the boot method. Called before device scanning.
		@return B_OK on success, error code otherwise
	*/
	virtual status_t Init();

	/*!	Check if a device matches the boot device identifier.
		@param device Device to check
		@param strict If true, perform strict matching (e.g., exact size check)
		@return true if device is the boot device
	*/
	virtual bool IsBootDevice(KDiskDevice* device, bool strict) = 0;

	/*!	Check if a partition could be the boot partition.
		@param partition Partition to check
		@param foundForSure Set to true if this is definitely the boot partition
		@return true if partition is a boot candidate
	*/
	virtual bool IsBootPartition(KPartition* partition, bool& foundForSure) = 0;

	/*!	Sort partitions by boot preference (most likely first).
		@param partitions Array of partition pointers
		@param count Number of partitions in array
	*/
	virtual void SortPartitions(KPartition** partitions, int32 count) = 0;

protected:
	const KMessage&	fBootVolume;	// Boot configuration from bootloader
	int32			fMethod;		// Boot method type (BOOT_METHOD_*)
};


#endif	// _VFS_BOOT_H
