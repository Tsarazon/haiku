/*
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2005, Jérôme DUVAL.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <Looper.h>
#include <Messenger.h>
#include <Partition.h>
#include <Volume.h>

class BList;
class BMenu;
class ProgressReporter;


// Partition size constants
static const off_t kMinTargetPartitionSize = 20 * 1024 * 1024;		// 20 MB
static const off_t kESPSize = 360 * 1024 * 1024;					// 360 MB


class WorkerThread : public BLooper {
public:
								WorkerThread(const BMessenger& owner);

	virtual	void				MessageReceived(BMessage* message);

			void 				InstallEFILoader(partition_id id, bool rename);

			void				ScanDisksPartitions(BMenu* srcMenu,
									BMenu* dstMenu, BMenu* EFIMenu);

			void				SetPackagesList(BList* list);
			void				SetSpaceRequired(off_t bytes)
									{ fSpaceRequired = bytes; };

			bool				Cancel();
			void				SetLock(sem_id cancelSemaphore)
									{ fCancelSemaphore = cancelSemaphore; }

			void				StartInstall(partition_id sourcePartitionID,
									partition_id targetPartitionID);
			void				WriteBootSector(BMenu* dstMenu);

private:
			status_t			_GetMountPoint(partition_id partitionID,
									BPath& mountPoint, BVolume* volume = NULL);

			status_t			_WriteBootSector(BPath& path);
			status_t			_CreateESPIfNeeded(BDiskDevice* targetDevice);
			status_t			_InstallEFIBootloader(
									const BPath& targetDirectory);
			status_t			_CopyFile(const char* source,
									const char* destination);
			status_t			_LaunchFinishScript(BPath& path);

			status_t			_PerformInstall(partition_id sourcePartitionID,
									partition_id targetPartitionID);
			status_t			_PrepareCleanInstall(
									const BPath& targetDirectory) const;
			status_t			_InstallationError(status_t error);
			status_t			_MirrorIndices(const BPath& srcDirectory,
									const BPath& targetDirectory) const;
			status_t			_CreateDefaultIndices(
									const BPath& targetDirectory) const;
			status_t			_ProcessZipPackages(const char* sourcePath,
									const char* targetPath,
									ProgressReporter* reporter,
									BList& unzipEngines);

			void				_SetStatusMessage(const char* status);

private:
			class EntryFilter;

private:
			BMessenger			fOwner;
			BDiskDeviceRoster	fDDRoster;
			BList*				fPackages;
			off_t				fSpaceRequired;
			sem_id				fCancelSemaphore;
};

#endif // WORKER_THREAD_H
