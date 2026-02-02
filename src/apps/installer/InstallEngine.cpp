/*
 * Copyright 2008-2009, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2009-2010, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2014, Axel Dörfler, axeld@pinc-software.de
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "InstallEngine.h"

#include <new>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>

#include <Catalog.h>
#include <Directory.h>
#include <fs_attr.h>
#include <Locale.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <SymLink.h>

#include "Concurrency.h"


using std::nothrow;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstallProgress"


// ProgressReporter

ProgressReporter::ProgressReporter(const BMessenger& messenger,
		BMessage* message)
	:
	fStartTime(0),
	fBytesToWrite(0),
	fBytesWritten(0),
	fItemsToWrite(0),
	fItemsWritten(0),
	fMessenger(messenger),
	fMessage(message)
{
}


ProgressReporter::~ProgressReporter()
{
	delete fMessage;
}


void
ProgressReporter::Reset()
{
	fBytesToWrite = 0;
	fBytesWritten = 0;
	fItemsToWrite = 0;
	fItemsWritten = 0;

	if (fMessage) {
		BMessage message(*fMessage);
		message.AddString("status",
			B_TRANSLATE("Collecting copy information."));
		fMessenger.SendMessage(&message);
	}
}


void
ProgressReporter::AddItems(int64 count, off_t bytes)
{
	fBytesToWrite += bytes;
	fItemsToWrite += count;
}


void
ProgressReporter::StartTimer()
{
	fStartTime = system_time();

	printf("%" B_PRIdOFF " bytes to write in %" B_PRId64 " files\n",
		fBytesToWrite, fItemsToWrite);

	if (fMessage) {
		BMessage message(*fMessage);
		message.AddString("status", B_TRANSLATE("Performing installation."));
		fMessenger.SendMessage(&message);
	}
}


void
ProgressReporter::ItemsWritten(uint64 items, off_t bytes,
	const char* itemName, const char* targetFolder)
{
	fItemsWritten += items;
	fBytesWritten += bytes;

	_UpdateProgress(itemName, targetFolder);
}


void
ProgressReporter::_UpdateProgress(const char* itemName,
	const char* targetFolder)
{
	if (fMessage == NULL)
		return;

	BMessage message(*fMessage);
	float progress = 100.0 * fBytesWritten / fBytesToWrite;
	message.AddFloat("progress", progress);
	message.AddInt32("current", fItemsWritten);
	message.AddInt32("maximum", fItemsToWrite);
	message.AddString("item", itemName);
	message.AddString("folder", targetFolder);
	fMessenger.SendMessage(&message);
}


// CopyEngine::Buffer

CopyEngine::Buffer::Buffer(BFile* file)
	:
	file(file),
	buffer(malloc(kBufferSize)),
	size(kBufferSize),
	validBytes(0),
	deleteFile(false)
{
}


CopyEngine::Buffer::~Buffer()
{
	if (deleteFile)
		delete file;
	free(buffer);
}


// CopyEngine::EntryFilter

CopyEngine::EntryFilter::~EntryFilter()
{
}


// CopyEngine

CopyEngine::CopyEngine(ProgressReporter* reporter, EntryFilter* entryFilter)
	:
	fBufferQueue(),
	fWriterThread(-1),
	fQuitting(false),
	fWriteError(B_OK),
	fAbsoluteSourcePath(),
	fAddedBytesToProgress(0),
	fAddedItemsToProgress(0),
	fBytesRead(0),
	fLastBytesRead(0),
	fItemsCopied(0),
	fLastItemsCopied(0),
	fTimeRead(0),
	fBytesWritten(0),
	fTimeWritten(0),
	fCurrentTargetFolder(NULL),
	fCurrentItem(NULL),
	fProgressReporter(reporter),
	fEntryFilter(entryFilter)
{
	fWriterThread = spawn_thread(_WriteThreadEntry, "buffer writer",
		B_NORMAL_PRIORITY, this);

	if (fWriterThread >= B_OK)
		resume_thread(fWriterThread);

	struct rlimit rl;
	rl.rlim_cur = 512;
	rl.rlim_max = RLIM_SAVED_MAX;
	setrlimit(RLIMIT_NOFILE, &rl);
}


CopyEngine::~CopyEngine()
{
	fQuitting = true;

	const vector<Buffer*>* remaining = NULL;
	fBufferQueue.Close(false, &remaining);

	if (fWriterThread >= B_OK) {
		int32 exitValue;
		wait_for_thread(fWriterThread, &exitValue);
	}

	if (remaining != NULL) {
		for (size_t i = 0; i < remaining->size(); i++)
			delete (*remaining)[i];
	}
}


void
CopyEngine::ResetTargets(const char* source)
{
	if (fProgressReporter != NULL
		&& (fAddedBytesToProgress > 0 || fAddedItemsToProgress > 0)) {
		fProgressReporter->AddItems(-fAddedItemsToProgress,
			-fAddedBytesToProgress);
	}

	fAbsoluteSourcePath = source;

	fAddedBytesToProgress = 0;
	fAddedItemsToProgress = 0;
	fBytesRead = 0;
	fLastBytesRead = 0;
	fItemsCopied = 0;
	fLastItemsCopied = 0;
	fTimeRead = 0;
	fBytesWritten = 0;
	fTimeWritten = 0;
	fCurrentTargetFolder = NULL;
	fCurrentItem = NULL;
	fWriteError = B_OK;
}


status_t
CopyEngine::CollectTargets(const char* source, sem_id cancelSemaphore)
{
	off_t bytesToCopy = 0;
	uint64 itemsToCopy = 0;

	status_t ret = _CollectCopyInfo(source, cancelSemaphore, bytesToCopy,
		itemsToCopy);

	if (ret == B_OK && fProgressReporter != NULL) {
		fProgressReporter->AddItems(itemsToCopy, bytesToCopy);
		fAddedItemsToProgress += itemsToCopy;
		fAddedBytesToProgress += bytesToCopy;
	}

	return ret;
}


status_t
CopyEngine::Copy(const char* _source, const char* _destination,
	sem_id cancelSemaphore, bool copyAttributes)
{
	BEntry source(_source);
	status_t ret = source.InitCheck();
	if (ret != B_OK)
		return ret;

	BEntry destination(_destination);
	ret = destination.InitCheck();
	if (ret != B_OK)
		return ret;

	return _Copy(source, destination, cancelSemaphore, copyAttributes);
}


status_t
CopyEngine::RemoveFolder(BEntry& entry)
{
	BDirectory directory(&entry);
	status_t ret = directory.InitCheck();
	if (ret != B_OK)
		return ret;

	BEntry subEntry;
	while (directory.GetNextEntry(&subEntry) == B_OK) {
		if (subEntry.IsDirectory()) {
			ret = RemoveFolder(subEntry);
			if (ret != B_OK)
				return ret;
		} else {
			ret = subEntry.Remove();
			if (ret != B_OK)
				return ret;
		}
	}

	return entry.Remove();
}


bool
CopyEngine::_IsCanceled(sem_id cancelSemaphore) const
{
	if (cancelSemaphore < 0)
		return false;

	SemaphoreLocker lock(cancelSemaphore);
	return !lock.IsLocked();
}


status_t
CopyEngine::_RemoveExisting(BEntry& entry, const char* entryPath) const
{
	if (!entry.Exists())
		return B_OK;

	status_t ret;
	if (entry.IsDirectory())
		ret = RemoveFolder(entry);
	else
		ret = entry.Remove();

	if (ret != B_OK) {
		fprintf(stderr, "Failed to make room for entry '%s': %s\n",
			entryPath, strerror(ret));
	}

	return ret;
}


status_t
CopyEngine::_CopyAttributes(const BEntry& source, BEntry& destination,
	const struct stat& sourceInfo) const
{
	BNode sourceNode(&source);
	BNode targetNode(&destination);

	char attrName[B_ATTR_NAME_LENGTH];
	while (sourceNode.GetNextAttrName(attrName) == B_OK) {
		attr_info info;
		if (sourceNode.GetAttrInfo(attrName, &info) != B_OK)
			continue;

		const size_t bufferSize = 4096;
		uint8 buffer[bufferSize];
		off_t offset = 0;

		ssize_t read = sourceNode.ReadAttr(attrName, info.type,
			offset, buffer, std::min((off_t)bufferSize, info.size));

		while (read >= 0) {
			targetNode.WriteAttr(attrName, info.type, offset, buffer, read);
			offset += read;

			read = sourceNode.ReadAttr(attrName, info.type,
				offset, buffer, std::min((off_t)bufferSize, info.size - offset));

			if (read == 0)
				break;
		}
	}

	destination.SetPermissions(sourceInfo.st_mode);
	destination.SetOwner(sourceInfo.st_uid);
	destination.SetGroup(sourceInfo.st_gid);
	destination.SetModificationTime(sourceInfo.st_mtime);
	destination.SetCreationTime(sourceInfo.st_crtime);

	return B_OK;
}


status_t
CopyEngine::_CopyData(const BEntry& _source, const BEntry& _destination,
	sem_id cancelSemaphore)
{
	if (_IsCanceled(cancelSemaphore))
		return B_CANCELED;

	BFile source(&_source, B_READ_ONLY);
	status_t ret = source.InitCheck();
	if (ret < B_OK)
		return ret;

	BFile* destination = new (nothrow) BFile(&_destination,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	ret = destination->InitCheck();
	if (ret < B_OK) {
		delete destination;
		return ret;
	}

	int32 loopIteration = 0;

	while (true) {
		if (fWriteError != B_OK) {
			delete destination;
			return fWriteError;
		}

		if (fBufferQueue.Size() >= kBufferCount) {
			snooze(1000);
			continue;
		}

		Buffer* buffer = new (nothrow) Buffer(destination);
		if (buffer == NULL || buffer->buffer == NULL) {
			delete destination;
			delete buffer;
			fprintf(stderr, "reading loop: out of memory\n");
			return B_NO_MEMORY;
		}

		ssize_t read = source.Read(buffer->buffer, buffer->size);
		if (read < 0) {
			ret = (status_t)read;
			fprintf(stderr, "Failed to read data: %s\n", strerror(ret));
			delete buffer;
			delete destination;
			break;
		}

		fBytesRead += read;
		loopIteration += 1;
		if (loopIteration % 2 == 0)
			_UpdateProgress();

		buffer->deleteFile = (read == 0);
		buffer->validBytes = (read > 0) ? (size_t)read : 0;

		ret = fBufferQueue.Push(buffer);
		if (ret < B_OK) {
			buffer->deleteFile = false;
			delete buffer;
			delete destination;
			return ret;
		}

		if (read == 0)
			break;
	}

	return ret;
}


status_t
CopyEngine::_CollectCopyInfo(const char* _source, sem_id cancelSemaphore,
	off_t& bytesToCopy, uint64& itemsToCopy)
{
	BEntry source(_source);
	status_t ret = source.InitCheck();
	if (ret < B_OK)
		return ret;

	struct stat statInfo;
	ret = source.GetStat(&statInfo);
	if (ret < B_OK)
		return ret;

	if (_IsCanceled(cancelSemaphore))
		return B_CANCELED;

	if (fEntryFilter != NULL
		&& !fEntryFilter->ShouldCopyEntry(source,
			_RelativeEntryPath(_source), statInfo)) {
		return B_OK;
	}

	if (S_ISDIR(statInfo.st_mode)) {
		BDirectory srcFolder(&source);
		ret = srcFolder.InitCheck();
		if (ret < B_OK)
			return ret;

		BEntry entry;
		while (srcFolder.GetNextEntry(&entry) == B_OK) {
			BPath entryPath;
			ret = entry.GetPath(&entryPath);
			if (ret < B_OK)
				return ret;

			ret = _CollectCopyInfo(entryPath.Path(), cancelSemaphore,
				bytesToCopy, itemsToCopy);
			if (ret < B_OK)
				return ret;
		}
	} else if (!S_ISLNK(statInfo.st_mode)) {
		bytesToCopy += statInfo.st_size;
	}

	itemsToCopy++;
	return B_OK;
}


status_t
CopyEngine::_Copy(BEntry& source, BEntry& destination,
	sem_id cancelSemaphore, bool copyAttributes)
{
	struct stat sourceInfo;
	status_t ret = source.GetStat(&sourceInfo);
	if (ret != B_OK)
		return ret;

	if (_IsCanceled(cancelSemaphore))
		return B_CANCELED;

	BPath sourcePath(&source);
	ret = sourcePath.InitCheck();
	if (ret != B_OK)
		return ret;

	BPath destPath(&destination);
	ret = destPath.InitCheck();
	if (ret != B_OK)
		return ret;

	const char* relativeSourcePath = _RelativeEntryPath(sourcePath.Path());
	if (fEntryFilter != NULL
		&& !fEntryFilter->ShouldCopyEntry(source, relativeSourcePath, sourceInfo)) {
		return B_OK;
	}

	bool copyAttributesToTarget = copyAttributes;

	if (S_ISDIR(sourceInfo.st_mode)) {
		BDirectory sourceDirectory(&source);
		ret = sourceDirectory.InitCheck();
		if (ret != B_OK)
			return ret;

		if (destination.Exists()) {
			if (destination.IsDirectory()) {
				copyAttributesToTarget = false;
			} else {
				ret = destination.Remove();
				if (ret != B_OK) {
					fprintf(stderr, "Failed to make room for folder '%s': %s\n",
						sourcePath.Path(), strerror(ret));
					return ret;
				}
			}
		}

		ret = create_directory(destPath.Path(), 0777);
		if (ret != B_OK && ret != B_FILE_EXISTS) {
			fprintf(stderr, "Could not create '%s': %s\n",
				destPath.Path(), strerror(ret));
			return ret;
		}

		BDirectory destDirectory(&destination);
		ret = destDirectory.InitCheck();
		if (ret != B_OK)
			return ret;

		BEntry entry;
		while (sourceDirectory.GetNextEntry(&entry) == B_OK) {
			BEntry dest(&destDirectory, entry.Name());
			ret = dest.InitCheck();
			if (ret != B_OK)
				return ret;

			ret = _Copy(entry, dest, cancelSemaphore, copyAttributes);
			if (ret != B_OK)
				return ret;
		}
	} else {
		ret = _RemoveExisting(destination, sourcePath.Path());
		if (ret != B_OK)
			return ret;

		fItemsCopied++;

		BPath destDirectory;
		ret = destPath.GetParent(&destDirectory);
		if (ret != B_OK)
			return ret;

		fCurrentTargetFolder = destDirectory.Path();
		fCurrentItem = sourcePath.Leaf();
		_UpdateProgress();

		if (S_ISLNK(sourceInfo.st_mode)) {
			BSymLink srcLink(&source);
			ret = srcLink.InitCheck();
			if (ret != B_OK)
				return ret;

			char linkPath[B_PATH_NAME_LENGTH];
			ssize_t read = srcLink.ReadLink(linkPath, B_PATH_NAME_LENGTH - 1);
			if (read < 0)
				return (status_t)read;

			BDirectory dstFolder;
			ret = destination.GetParent(&dstFolder);
			if (ret != B_OK)
				return ret;

			ret = dstFolder.CreateSymLink(sourcePath.Leaf(), linkPath, NULL);
			if (ret != B_OK)
				return ret;
		} else {
			ret = _CopyData(source, destination);
			if (ret != B_OK)
				return ret;
		}
	}

	if (copyAttributesToTarget) {
		_CopyAttributes(source, destination, sourceInfo);
	}

	return B_OK;
}


const char*
CopyEngine::_RelativeEntryPath(const char* absoluteSourcePath) const
{
	if (strncmp(absoluteSourcePath, fAbsoluteSourcePath,
			fAbsoluteSourcePath.Length()) != 0) {
		return absoluteSourcePath;
	}

	const char* relativePath = absoluteSourcePath + fAbsoluteSourcePath.Length();
	return relativePath[0] == '/' ? relativePath + 1 : relativePath;
}


void
CopyEngine::_UpdateProgress()
{
	if (fProgressReporter == NULL)
		return;

	uint64 items = 0;
	if (fLastItemsCopied < fItemsCopied) {
		items = fItemsCopied - fLastItemsCopied;
		fLastItemsCopied = fItemsCopied;
	}

	off_t bytes = 0;
	if (fLastBytesRead < fBytesRead) {
		bytes = fBytesRead - fLastBytesRead;
		fLastBytesRead = fBytesRead;
	}

	fProgressReporter->ItemsWritten(items, bytes, fCurrentItem,
		fCurrentTargetFolder);
}


int32
CopyEngine::_WriteThreadEntry(void* cookie)
{
	CopyEngine* engine = (CopyEngine*)cookie;
	engine->_WriteThread();
	return B_OK;
}


void
CopyEngine::_WriteThread()
{
	const bigtime_t bufferWaitTimeout = 100000;

	while (!fQuitting) {
		bigtime_t now = system_time();

		Buffer* buffer = NULL;
		status_t ret = fBufferQueue.Pop(&buffer, bufferWaitTimeout);

		if (ret == B_TIMED_OUT)
			continue;

		if (ret == B_NO_INIT)
			return;

		if (ret != B_OK) {
			snooze(10000);
			continue;
		}

		if (!buffer->deleteFile) {
			ssize_t written = buffer->file->Write(buffer->buffer,
				buffer->validBytes);

			if (written != (ssize_t)buffer->validBytes) {
				status_t error = (written < 0) ? (status_t)written : B_IO_ERROR;
				fprintf(stderr, "Failed to write data: %s\n", strerror(error));
				fWriteError = error;
			}

			fBytesWritten += (written > 0) ? written : 0;
		}

		delete buffer;
		fTimeWritten += system_time() - now;
	}

	double megaBytes = (double)fBytesWritten / (1024 * 1024);
	double seconds = (double)fTimeWritten / 1000000;

	if (seconds > 0) {
		printf("%.2f MB written (%.2f MB/s)\n", megaBytes, megaBytes / seconds);
	}
}


// UnzipEngine

UnzipEngine::UnzipEngine(ProgressReporter* reporter, sem_id cancelSemaphore)
	:
	fPackage(""),
	fRetrievingListing(false),
	fBytesToUncompress(0),
	fBytesUncompressed(0),
	fLastBytesUncompressed(0),
	fItemsToUncompress(0),
	fItemsUncompressed(0),
	fLastItemsUncompressed(0),
	fProgressReporter(reporter),
	fCancelSemaphore(cancelSemaphore)
{
}


UnzipEngine::~UnzipEngine()
{
}


status_t
UnzipEngine::SetTo(const char* pathToPackage, const char* destinationFolder)
{
	fPackage = pathToPackage;
	fDestinationFolder = destinationFolder;

	fEntrySizeMap.Clear();

	fBytesToUncompress = 0;
	fBytesUncompressed = 0;
	fLastBytesUncompressed = 0;
	fItemsToUncompress = 0;
	fItemsUncompressed = 0;
	fLastItemsUncompressed = 0;

	BPrivate::BCommandPipe commandPipe;
	status_t ret = commandPipe.AddArg("unzip");
	if (ret == B_OK)
		ret = commandPipe.AddArg("-l");
	if (ret == B_OK)
		ret = commandPipe.AddArg(fPackage.String());
	if (ret != B_OK)
		return ret;

	FILE* stdOutAndErrPipe = NULL;
	thread_id unzipThread = commandPipe.PipeInto(&stdOutAndErrPipe);
	if (unzipThread < 0)
		return (status_t)unzipThread;

	fRetrievingListing = true;
	ret = commandPipe.ReadLines(stdOutAndErrPipe, this);
	fRetrievingListing = false;

	printf("%" B_PRIu64 " items in %" B_PRIdOFF " bytes\n", fItemsToUncompress,
		fBytesToUncompress);

	return ret;
}


status_t
UnzipEngine::UnzipPackage()
{
	if (fItemsToUncompress == 0)
		return B_NO_INIT;

	BPrivate::BCommandPipe commandPipe;
	status_t ret = commandPipe.AddArg("unzip");
	if (ret == B_OK)
		ret = commandPipe.AddArg("-o");
	if (ret == B_OK)
		ret = commandPipe.AddArg(fPackage.String());
	if (ret == B_OK)
		ret = commandPipe.AddArg("-d");
	if (ret == B_OK)
		ret = commandPipe.AddArg(fDestinationFolder.String());
	if (ret != B_OK) {
		fprintf(stderr, "Failed to construct argument list for unzip "
			"process: %s\n", strerror(ret));
		return ret;
	}

	FILE* stdOutAndErrPipe = NULL;
	thread_id unzipThread = commandPipe.PipeInto(&stdOutAndErrPipe);
	if (unzipThread < 0)
		return (status_t)unzipThread;

	ret = commandPipe.ReadLines(stdOutAndErrPipe, this);
	if (ret != B_OK) {
		fprintf(stderr, "Piping the unzip process failed: %s\n",
			strerror(ret));
		return ret;
	}

	BPath descriptionPath(fDestinationFolder.String(),
		".OptionalPackageDescription");
	ret = descriptionPath.InitCheck();
	if (ret != B_OK) {
		fprintf(stderr, "Failed to construct path to "
			".OptionalPackageDescription: %s\n", strerror(ret));
		return ret;
	}

	BEntry descriptionEntry(descriptionPath.Path());
	if (!descriptionEntry.Exists())
		return B_OK;

	BFile descriptionFile(&descriptionEntry, B_READ_ONLY);
	ret = descriptionFile.InitCheck();
	if (ret != B_OK) {
		fprintf(stderr, "Failed to construct file to "
			".OptionalPackageDescription: %s\n", strerror(ret));
		return ret;
	}

	BPath aboutSystemPath(fDestinationFolder.String(),
		"system/apps/AboutSystem");
	ret = aboutSystemPath.InitCheck();
	if (ret != B_OK) {
		fprintf(stderr, "Failed to construct path to AboutSystem: %s\n",
			strerror(ret));
		return ret;
	}

	BNode aboutSystemNode(aboutSystemPath.Path());
	ret = aboutSystemNode.InitCheck();
	if (ret != B_OK) {
		fprintf(stderr, "Failed to construct node to AboutSystem: %s\n",
			strerror(ret));
		return ret;
	}

	const char* kCopyrightsAttrName = "COPYRIGHTS";

	BString copyrightAttr;
	ret = aboutSystemNode.ReadAttrString(kCopyrightsAttrName, &copyrightAttr);
	if (ret != B_OK && ret != B_ENTRY_NOT_FOUND) {
		fprintf(stderr, "Failed to read current COPYRIGHTS attribute from "
			"AboutSystem: %s\n", strerror(ret));
		return ret;
	}

	size_t bufferSize = 2048;
	char buffer[bufferSize + 1];
	buffer[bufferSize] = '\0';
	while (true) {
		ssize_t read = descriptionFile.Read(buffer, bufferSize);
		if (read > 0) {
			int32 length = copyrightAttr.Length();
			if (read < (ssize_t)bufferSize)
				buffer[read] = '\0';
			int32 bufferLength = strlen(buffer);
			copyrightAttr << buffer;
			if (copyrightAttr.Length() != length + bufferLength) {
				fprintf(stderr, "Failed to append buffer to COPYRIGHTS "
					"attribute.\n");
				return B_NO_MEMORY;
			}
		} else
			break;
	}

	if (copyrightAttr[copyrightAttr.Length() - 1] != '\n')
		copyrightAttr << '\n' << '\n';
	else
		copyrightAttr << '\n';

	ret = aboutSystemNode.WriteAttrString(kCopyrightsAttrName, &copyrightAttr);
	if (ret != B_OK && ret != B_ENTRY_NOT_FOUND) {
		fprintf(stderr, "Failed to read current COPYRIGHTS attribute from "
			"AboutSystem: %s\n", strerror(ret));
		return ret;
	}

	descriptionFile.Unset();
	descriptionEntry.Remove();

	return B_OK;
}


bool
UnzipEngine::IsCanceled()
{
	if (fCancelSemaphore < 0)
		return false;

	SemaphoreLocker locker(fCancelSemaphore);
	return !locker.IsLocked();
}


status_t
UnzipEngine::ReadLine(const BString& line)
{
	if (fRetrievingListing)
		return _ReadLineListing(line);
	else
		return _ReadLineExtract(line);
}


status_t
UnzipEngine::_ReadLineListing(const BString& line)
{
	static const char* kListingFormat = "%llu  %s %s   %s\n";

	const char* string = line.String();
	while (string[0] == ' ')
		string++;

	uint64 bytes;
	char date[16];
	char time[16];
	char path[1024];
	if (sscanf(string, kListingFormat, &bytes, &date, &time, &path) == 4) {
		fBytesToUncompress += bytes;

		BString itemPath(path);
		BString itemName(path);
		int leafPos = itemPath.FindLast('/');
		if (leafPos >= 0)
			itemName = itemPath.String() + leafPos + 1;

		uint32 itemCount = 1;
		if (bytes == 0 && itemName.Length() == 0) {
			BPath destination(fDestinationFolder.String());
			if (destination.Append(itemPath.String()) == B_OK) {
				BEntry test(destination.Path());
				if (test.Exists() && test.IsDirectory()) {
					itemCount = 0;
				}
			}
		}

		fItemsToUncompress += itemCount;
		fEntrySizeMap.Put(itemName.String(), bytes);
	}

	return B_OK;
}


status_t
UnzipEngine::_ReadLineExtract(const BString& line)
{
	const char* kCreatingFormat = "   creating:";
	const char* kInflatingFormat = "  inflating:";
	const char* kLinkingFormat = "    linking:";
	if (line.FindFirst(kCreatingFormat) == 0
		|| line.FindFirst(kInflatingFormat) == 0
		|| line.FindFirst(kLinkingFormat) == 0) {

		fItemsUncompressed++;

		BString itemPath;

		int pos = line.FindLast(" -> ");
		if (pos > 0)
			line.CopyInto(itemPath, 13, pos - 13);
		else
			line.CopyInto(itemPath, 13, line.CountChars() - 14);

		itemPath.Trim();
		pos = itemPath.FindLast('/');
		BString itemName = itemPath.String() + pos + 1;
		itemPath.Truncate(pos);

		off_t bytes = 0;
		if (fEntrySizeMap.ContainsKey(itemName.String())) {
			bytes = fEntrySizeMap.Get(itemName.String());
			fBytesUncompressed += bytes;
		}

		_UpdateProgress(itemName.String(), itemPath.String());
	}

	return B_OK;
}


void
UnzipEngine::_UpdateProgress(const char* item, const char* targetFolder)
{
	if (fProgressReporter == NULL)
		return;

	uint64 items = 0;
	if (fLastItemsUncompressed < fItemsUncompressed) {
		items = fItemsUncompressed - fLastItemsUncompressed;
		fLastItemsUncompressed = fItemsUncompressed;
	}

	off_t bytes = 0;
	if (fLastBytesUncompressed < fBytesUncompressed) {
		bytes = fBytesUncompressed - fLastBytesUncompressed;
		fLastBytesUncompressed = fBytesUncompressed;
	}

	fProgressReporter->ItemsWritten(items, bytes, item, targetFolder);
}
