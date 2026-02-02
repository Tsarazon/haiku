/*
 * Copyright 2008-2009, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 *  All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef COPY_ENGINE_H
#define COPY_ENGINE_H


#include <stdlib.h>

#include <Entry.h>
#include <File.h>
#include <Messenger.h>
#include <String.h>

#include "BlockingQueue.h"

class BFile;


// #pragma mark - ProgressReporter


class ProgressReporter {
public:
								ProgressReporter(const BMessenger& messenger,
									BMessage* message);
	virtual						~ProgressReporter();

			void				Reset();

			void				AddItems(int64 count, off_t bytes);

			void				StartTimer();

			void				ItemsWritten(uint64 items, off_t bytes,
									const char* itemName,
									const char* targetFolder);

private:
			void				_UpdateProgress(const char* itemName,
									const char* targetFolder);

private:
			bigtime_t			fStartTime;

			off_t				fBytesToWrite;
			off_t				fBytesWritten;

			int64				fItemsToWrite;
			uint64				fItemsWritten;

			BMessenger			fMessenger;
			BMessage*			fMessage;
};


// #pragma mark - CopyEngine


class CopyEngine {
public:
	class EntryFilter;

	static const int32 kBufferCount = 16;
	static const size_t kBufferSize = 1024 * 1024;

public:
								CopyEngine(ProgressReporter* reporter,
									EntryFilter* entryFilter);
	virtual						~CopyEngine();

			void				ResetTargets(const char* source);
			status_t			CollectTargets(const char* source,
									sem_id cancelSemaphore = -1);

			status_t			Copy(const char* source,
									const char* destination,
									sem_id cancelSemaphore = -1,
									bool copyAttributes = true);

	static	status_t			RemoveFolder(BEntry& entry);

private:
	struct Buffer {
								Buffer(BFile* file);
								~Buffer();

		BFile*					file;
		void*					buffer;
		size_t					size;
		size_t					validBytes;
		bool					deleteFile;
	};

private:
			status_t			_CollectCopyInfo(const char* source,
									sem_id cancelSemaphore, off_t& bytesToCopy,
									uint64& itemsToCopy);
			status_t			_Copy(BEntry& source, BEntry& destination,
									sem_id cancelSemaphore,
									bool copyAttributes);
			status_t			_CopyData(const BEntry& entry,
									const BEntry& destination,
									sem_id cancelSemaphore = -1);

			status_t			_CopyAttributes(const BEntry& source,
									BEntry& destination,
									const struct stat& sourceInfo) const;

			status_t			_RemoveExisting(BEntry& entry,
									const char* entryPath) const;

			bool				_IsCanceled(sem_id cancelSemaphore) const;

			const char*			_RelativeEntryPath(
									const char* absoluteSourcePath) const;

			void				_UpdateProgress();

	static	int32				_WriteThreadEntry(void* cookie);
			void				_WriteThread();

private:
	BlockingQueue<Buffer>		fBufferQueue;

			thread_id			fWriterThread;
	volatile bool				fQuitting;
	volatile status_t			fWriteError;

			BString				fAbsoluteSourcePath;

			off_t				fAddedBytesToProgress;
			int64				fAddedItemsToProgress;

			off_t				fBytesRead;
			off_t				fLastBytesRead;
			uint64				fItemsCopied;
			uint64				fLastItemsCopied;
			bigtime_t			fTimeRead;

			off_t				fBytesWritten;
			bigtime_t			fTimeWritten;

			const char*			fCurrentTargetFolder;
			const char*			fCurrentItem;

			ProgressReporter*	fProgressReporter;
			EntryFilter*		fEntryFilter;
};


class CopyEngine::EntryFilter {
public:
	virtual						~EntryFilter();

	virtual	bool				ShouldCopyEntry(const BEntry& entry,
									const char* path,
									const struct stat& statInfo) const = 0;
};


#endif // COPY_ENGINE_H
