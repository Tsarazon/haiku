/*
 * Copyright 2008 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "PathList.h"

#include <new>
#include <stdlib.h>
#include <string.h>


struct PathList::path_entry {
	path_entry(const char* _path)
		: path{strdup(_path)}, ref_count{1}
	{
	}

	~path_entry() noexcept
	{
		free(const_cast<char*>(path));
	}

	const char* path;
	int32 ref_count;
};


PathList::PathList()
	:
	fPaths{10}
{
}


PathList::~PathList()
{
}


bool
PathList::HasPath(const char* path, int32* _index) const
{
	for (auto i = fPaths.CountItems(); i-- > 0;) {
		if (!strcmp(fPaths.ItemAt(i)->path, path)) {
			if (_index != nullptr)
				*_index = i;
			return true;
		}
	}

	return false;
}


status_t
PathList::AddPath(const char* path)
{
	if (path == nullptr)
		return B_BAD_VALUE;

	int32 index;
	if (HasPath(path, &index)) {
		fPaths.ItemAt(index)->ref_count++;
		return B_OK;
	}

	auto* entry = new(std::nothrow) path_entry(path);
	if (entry == nullptr || entry->path == nullptr || !fPaths.AddItem(entry)) {
		delete entry;
		return B_NO_MEMORY;
	}

	return B_OK;
}


status_t
PathList::RemovePath(const char* path)
{
	int32 index;
	if (!HasPath(path, &index))
		return B_ENTRY_NOT_FOUND;

	if (--fPaths.ItemAt(index)->ref_count == 0)
		delete fPaths.RemoveItemAt(index);

	return B_OK;
}


int32
PathList::CountPaths() const
{
	return fPaths.CountItems();
}


const char*
PathList::PathAt(int32 index) const
{
	auto* entry = fPaths.ItemAt(index);
	if (entry == nullptr)
		return nullptr;

	return entry->path;
}
