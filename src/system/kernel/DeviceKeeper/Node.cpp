/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Node.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <KernelExport.h>
#include <util/AutoLock.h>


DkNode::DkNode(const char* moduleName)
	:
	fParent(NULL),
	fModuleName(NULL),
	fFlags(0),
	fRegistered(false),
	fKeepLoaded(false),
	fDriver(NULL),
	fDriverCookie(NULL),
	fAttaching(false),
	fPowerState(kPowerActive),
	fPublishedPathCount(0)
{
	mutex_init(&fLock, "dk node");
	fAttachCondition.Init(this, "dk node attach");
	if (moduleName != NULL)
		fModuleName = strdup(moduleName);
}


DkNode::~DkNode()
{
	ASSERT(fParent == NULL);

	// children should have been detached and unregistered before
	// reaching here, but clean up defensively
	while (DkNode* child = fChildren.RemoveHead()) {
		child->fParent = NULL;
		child->Release();
	}

	ReleaseAllResources();
	ClearPublishedPaths();
	ClearWatchers();
	free(fModuleName);
	mutex_destroy(&fLock);
}


status_t
DkNode::InitCheck() const
{
	if (fModuleName == NULL)
		return B_NO_MEMORY;
	return B_OK;
}


void
DkNode::AddChild(DkNode* child)
{
	child->fParent = this;

	// Insert sorted by priority (highest first) so that more specific
	// drivers are probed before bus enumerators. This mirrors Haiku's
	// device_node::AddChild and ensures stable device naming.
	int32 priority = child->Priority();

	DkNode* before = NULL;
	DoublyLinkedList<DkNode>::Iterator it = fChildren.GetIterator();
	while (it.HasNext()) {
		DkNode* sibling = it.Next();
		if (sibling->Priority() < priority) {
			before = sibling;
			break;
		}
	}

	fChildren.InsertBefore(before, child);
	Acquire();
}


void
DkNode::RemoveChild(DkNode* child)
{
	child->fParent = NULL;
	fChildren.Remove(child);
	Release();
}


DkNode*
DkNode::FindChild(const dk_match_rule* attrs) const
{
	DoublyLinkedList<DkNode>::ConstIterator it = fChildren.GetIterator();
	while (it.HasNext()) {
		DkNode* child = it.Next();
		if (!child->IsRegistered())
			continue;
		if (child->Properties().Matches(attrs))
			return child;
	}
	return NULL;
}


const DkPropertyEntry*
DkNode::LookupRecursive(const char* name) const
{
	const DkNode* node = this;
	while (node != NULL) {
		const DkPropertyEntry* entry = node->Properties().Lookup(name);
		if (entry != NULL)
			return entry;
		node = node->Parent();
	}
	return NULL;
}


DkNode*
DkNode::FindChildDeep(const dk_match_rule* attrs, DkNode* after) const
{
	DkNode* result = NULL;
	bool pastAfter = (after == NULL);
	_FindDeepHelper(const_cast<DkNode*>(this), attrs, after, &pastAfter,
		&result);
	return result;
}


/*static*/ bool
DkNode::_FindDeepHelper(DkNode* root, const dk_match_rule* attrs,
	DkNode* after, bool* pastAfter, DkNode** _result)
{
	DkNodeList::Iterator it = root->Children().GetIterator();
	while (it.HasNext()) {
		DkNode* child = it.Next();

		// Identity check must precede IsRegistered(): if the after-node
		// was unregistered between two iterative calls, we still need
		// to advance pastAfter so the search can continue from here.
		if (child == after) {
			*pastAfter = true;
			// In DFS order, after's children come immediately after
			// after itself — they are valid candidates. Recurse into
			// the subtree before moving to the next sibling.
			if (_FindDeepHelper(child, attrs, after, pastAfter, _result))
				return true;
			continue;
		}

		if (!child->IsRegistered())
			continue;

		if (*pastAfter && child->Properties().Matches(attrs)) {
			*_result = child;
			return true;
		}

		if (_FindDeepHelper(child, attrs, after, pastAfter, _result))
			return true;
	}
	return false;
}


void
DkNode::ReleaseAllResources()
{
	while (DkOwnedResource* entry = fResources.RemoveHead())
		delete entry;
}


void
DkNode::SetRegistered(bool registered)
{
	fRegistered = registered;
	if (registered) {
		fProperties.GetUInt32(KOSM_DEVICE_FLAGS, &fFlags);
		fKeepLoaded = (fFlags & KOSM_KEEP_DRIVER_LOADED) != 0;
	}
}


int32
DkNode::Priority() const
{
	return (fFlags & KOSM_FIND_MULTIPLE_CHILDREN) != 0 ? 0 : 100;
}


void
DkNode::LastReferenceReleased()
{
	delete this;
}


void
DkNode::SetDriver(dk_driver_info* driver, void* cookie)
{
	fDriver = driver;
	fDriverCookie = cookie;
}


void
DkNode::ClearDriver()
{
	fDriver = NULL;
	fDriverCookie = NULL;
}


void
DkNode::SetAttaching(bool attaching)
{
	// caller must hold node lock
	fAttaching = attaching;
	if (!attaching)
		fAttachCondition.NotifyAll();
}


void
DkNode::WaitAttachDone()
{
	MutexLocker locker(&fLock);
	while (fAttaching)
		fAttachCondition.Wait(&fLock);
}


void
DkNode::SetPowerState(power_state state)
{
	fPowerState = state;
}


status_t
DkNode::AddPublishedPath(const char* path)
{
	DkPublishedPathEntry* entry = new(std::nothrow) DkPublishedPathEntry(path);
	if (entry == NULL || entry->path == NULL) {
		delete entry;
		return B_NO_MEMORY;
	}

	fPublishedPaths.Add(entry);
	fPublishedPathCount++;
	return B_OK;
}


void
DkNode::RemovePublishedPath(const char* path)
{
	DkPublishedPathList::Iterator it = fPublishedPaths.GetIterator();
	while (it.HasNext()) {
		DkPublishedPathEntry* entry = it.Next();
		if (strcmp(entry->path, path) == 0) {
			it.Remove();
			fPublishedPathCount--;
			delete entry;
			return;
		}
	}
}


const char*
DkNode::FirstPublishedPath() const
{
	DkPublishedPathEntry* entry = fPublishedPaths.Head();
	if (entry == NULL)
		return NULL;
	return entry->path;
}


int32
DkNode::CountPublishedPaths() const
{
	return fPublishedPathCount;
}


void
DkNode::ClearPublishedPaths()
{
	while (DkPublishedPathEntry* entry = fPublishedPaths.RemoveHead())
		delete entry;
	fPublishedPathCount = 0;
}


status_t
DkNode::AddWatcher(uint32 events, dk_node_callback callback, void* cookie)
{
	if (callback == NULL || events == 0)
		return B_BAD_VALUE;

	DkWatchEntry* entry = new(std::nothrow) DkWatchEntry();
	if (entry == NULL)
		return B_NO_MEMORY;

	entry->events = events;
	entry->callback = callback;
	entry->cookie = cookie;
	fWatchers.Add(entry);
	return B_OK;
}


status_t
DkNode::RemoveWatcher(dk_node_callback callback, void* cookie)
{
	DkWatchList::Iterator it = fWatchers.GetIterator();
	while (it.HasNext()) {
		DkWatchEntry* entry = it.Next();
		if (entry->callback == callback && entry->cookie == cookie) {
			it.Remove();
			delete entry;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


void
DkNode::NotifyWatchers(uint32 event, dk_node* eventNode, const char* path)
{
	// Snapshot matching watchers under node lock, then invoke
	// callbacks without the lock held so that callbacks may safely
	// call watch_node/unwatch_node on this node (re-entrant safe).
	struct Pending { dk_node_callback cb; void* cookie; };

	static const int32 kStackMax = 8;
	Pending stackBuf[kStackMax];

	MutexLocker locker(&fLock);

	int32 count = 0;
	DkWatchList::Iterator it = fWatchers.GetIterator();
	while (it.HasNext()) {
		DkWatchEntry* entry = it.Next();
		if ((entry->events & event) != 0)
			count++;
	}

	if (count == 0)
		return;

	Pending* pending = count <= kStackMax
		? stackBuf
		: (Pending*)malloc(count * sizeof(Pending));
	if (pending == NULL)
		return;

	int32 index = 0;
	it = fWatchers.GetIterator();
	while (it.HasNext() && index < count) {
		DkWatchEntry* entry = it.Next();
		if ((entry->events & event) != 0) {
			pending[index].cb = entry->callback;
			pending[index].cookie = entry->cookie;
			index++;
		}
	}

	locker.Unlock();

	for (int32 i = 0; i < index; i++)
		pending[i].cb(pending[i].cookie, event, eventNode, path);

	if (pending != stackBuf)
		free(pending);
}


void
DkNode::ClearWatchers()
{
	while (DkWatchEntry* entry = fWatchers.RemoveHead())
		delete entry;
}


// DkPublishedPathEntry

DkPublishedPathEntry::DkPublishedPathEntry(const char* _path)
	:
	path(strdup(_path))
{
}


DkPublishedPathEntry::~DkPublishedPathEntry()
{
	free(path);
}


// DkPropertyEntry

DkPropertyEntry::DkPropertyEntry()
	:
	name(NULL),
	type(0),
	hash_link(NULL),
	insert_next(NULL)
{
	memset(&value, 0, sizeof(value));
}


DkPropertyEntry::~DkPropertyEntry()
{
	_Free();
}


void
DkPropertyEntry::_Free()
{
	free(name);
	name = NULL;

	switch (type) {
		case B_STRING_TYPE:
			free(value.string);
			value.string = NULL;
			break;
		case B_RAW_TYPE:
			free(value.raw.data);
			value.raw.data = NULL;
			value.raw.length = 0;
			break;
		case KOSM_STRINGLIST_TYPE:
			if (value.stringlist.items != NULL) {
				for (uint32 i = 0; i < value.stringlist.count; i++)
					free(value.stringlist.items[i]);
				free(value.stringlist.items);
				value.stringlist.items = NULL;
				value.stringlist.count = 0;
			}
			break;
		default:
			break;
	}

	type = 0;
}


status_t
DkPropertyEntry::SetFrom(const dk_property& prop)
{
	_Free();

	name = strdup(prop.name);
	if (name == NULL)
		return B_NO_MEMORY;

	type = prop.type;

	switch (type) {
		case B_UINT8_TYPE:
			value.ui8 = prop.value.ui8;
			break;
		case B_UINT16_TYPE:
			value.ui16 = prop.value.ui16;
			break;
		case B_UINT32_TYPE:
			value.ui32 = prop.value.ui32;
			break;
		case B_UINT64_TYPE:
			value.ui64 = prop.value.ui64;
			break;
		case B_STRING_TYPE:
			if (prop.value.string != NULL) {
				value.string = strdup(prop.value.string);
				if (value.string == NULL) {
					_Free();
					return B_NO_MEMORY;
				}
			}
			break;
		case B_RAW_TYPE:
			if (prop.value.raw.data != NULL && prop.value.raw.length > 0) {
				value.raw.data = malloc(prop.value.raw.length);
				if (value.raw.data == NULL) {
					_Free();
					return B_NO_MEMORY;
				}
				memcpy(value.raw.data, prop.value.raw.data,
					prop.value.raw.length);
				value.raw.length = prop.value.raw.length;
			}
			break;
		case KOSM_STRINGLIST_TYPE:
		{
			uint32 count = prop.value.stringlist.count;
			if (count == 0 || prop.value.stringlist.items == NULL)
				break;
			value.stringlist.items = (char**)malloc(count * sizeof(char*));
			if (value.stringlist.items == NULL) {
				_Free();
				return B_NO_MEMORY;
			}
			value.stringlist.count = 0;
			for (uint32 i = 0; i < count; i++) {
				value.stringlist.items[i] = strdup(
					prop.value.stringlist.items[i]);
				if (value.stringlist.items[i] == NULL) {
					_Free();
					return B_NO_MEMORY;
				}
				value.stringlist.count++;
			}
			break;
		}
		default:
			_Free();
			return B_BAD_VALUE;
	}

	return B_OK;
}


// DkPropertyHashDefinition

size_t
DkPropertyHashDefinition::HashKey(const char* key) const
{
	uint32 hash = 5381;
	for (const char* p = key; *p != '\0'; p++)
		hash = hash * 33 + (uint8)*p;
	return hash;
}


size_t
DkPropertyHashDefinition::Hash(const DkPropertyEntry* entry) const
{
	return HashKey(entry->name);
}


bool
DkPropertyHashDefinition::Compare(const char* key,
	const DkPropertyEntry* entry) const
{
	return strcmp(key, entry->name) == 0;
}


DkPropertyEntry*&
DkPropertyHashDefinition::GetLink(DkPropertyEntry* entry) const
{
	return entry->hash_link;
}


// DkPropertyStore

DkPropertyStore::DkPropertyStore()
	:
	fCommitted(false),
	fInsertHead(NULL),
	fInsertTail(NULL)
{
}


DkPropertyStore::~DkPropertyStore()
{
	DkPropertyEntry* entry = fTable.Clear(true);
	while (entry != NULL) {
		DkPropertyEntry* next = entry->hash_link;
		delete entry;
		entry = next;
	}
}


status_t
DkPropertyStore::Init(uint32 capacity)
{
	return fTable.Init(capacity);
}


status_t
DkPropertyStore::Put(const dk_property& property)
{
	if (fCommitted)
		return B_NOT_ALLOWED;
	if (property.name == NULL)
		return B_BAD_VALUE;

	// replace existing
	DkPropertyEntry* existing = fTable.Lookup(property.name);
	if (existing != NULL) {
		_InsertListRemove(existing);
		fTable.Remove(existing);
		delete existing;
	}

	DkPropertyEntry* entry = new(std::nothrow) DkPropertyEntry();
	if (entry == NULL)
		return B_NO_MEMORY;

	status_t status = entry->SetFrom(property);
	if (status != B_OK) {
		delete entry;
		return status;
	}

	status = fTable.Insert(entry);
	if (status != B_OK) {
		delete entry;
		return status;
	}

	_InsertListAppend(entry);
	return B_OK;
}


status_t
DkPropertyStore::PutUInt8(const char* name, uint8 value)
{
	dk_property prop = {};
	prop.name = name;
	prop.type = B_UINT8_TYPE;
	prop.value.ui8 = value;
	return Put(prop);
}


status_t
DkPropertyStore::PutUInt16(const char* name, uint16 value)
{
	dk_property prop = {};
	prop.name = name;
	prop.type = B_UINT16_TYPE;
	prop.value.ui16 = value;
	return Put(prop);
}


status_t
DkPropertyStore::PutUInt32(const char* name, uint32 value)
{
	dk_property prop = {};
	prop.name = name;
	prop.type = B_UINT32_TYPE;
	prop.value.ui32 = value;
	return Put(prop);
}


status_t
DkPropertyStore::PutUInt64(const char* name, uint64 value)
{
	dk_property prop = {};
	prop.name = name;
	prop.type = B_UINT64_TYPE;
	prop.value.ui64 = value;
	return Put(prop);
}


status_t
DkPropertyStore::PutString(const char* name, const char* value)
{
	dk_property prop = {};
	prop.name = name;
	prop.type = B_STRING_TYPE;
	prop.value.string = value;
	return Put(prop);
}


status_t
DkPropertyStore::PutStringList(const char* name, const char** items,
	uint32 count)
{
	dk_property prop = {};
	prop.name = name;
	prop.type = KOSM_STRINGLIST_TYPE;
	prop.value.stringlist.items = items;
	prop.value.stringlist.count = count;
	return Put(prop);
}


status_t
DkPropertyStore::PutRaw(const char* name, const void* data, size_t length)
{
	dk_property prop = {};
	prop.name = name;
	prop.type = B_RAW_TYPE;
	prop.value.raw.data = data;
	prop.value.raw.length = length;
	return Put(prop);
}


status_t
DkPropertyStore::PutFromArray(const dk_property* properties)
{
	if (properties == NULL)
		return B_OK;

	for (; properties->name != NULL; properties++) {
		status_t status = Put(*properties);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


void
DkPropertyStore::Commit()
{
	fCommitted = true;
}


status_t
DkPropertyStore::SetAfterCommit(const dk_property& property)
{
	if (!fCommitted)
		return B_NOT_ALLOWED;
	if (property.name == NULL)
		return B_BAD_VALUE;

	DkPropertyEntry* existing = fTable.Lookup(property.name);
	if (existing != NULL) {
		// validate new type
		switch (property.type) {
			case B_UINT8_TYPE:
			case B_UINT16_TYPE:
			case B_UINT32_TYPE:
			case B_UINT64_TYPE:
			case B_STRING_TYPE:
			case B_RAW_TYPE:
			case KOSM_STRINGLIST_TYPE:
				break;
			default:
				return B_BAD_TYPE;
		}

		// allocate new heap data before freeing old (fail-safe)
		char* newStr = NULL;
		void* newRawData = NULL;
		size_t newRawLen = 0;
		char** newListItems = NULL;
		uint32 newListCount = 0;

		switch (property.type) {
			case B_STRING_TYPE:
				newStr = strdup(
					property.value.string != NULL ? property.value.string : "");
				if (newStr == NULL)
					return B_NO_MEMORY;
				break;
			case B_RAW_TYPE:
				if (property.value.raw.data != NULL
					&& property.value.raw.length > 0) {
					newRawData = malloc(property.value.raw.length);
					if (newRawData == NULL)
						return B_NO_MEMORY;
					memcpy(newRawData, property.value.raw.data,
						property.value.raw.length);
					newRawLen = property.value.raw.length;
				}
				break;
			case KOSM_STRINGLIST_TYPE:
			{
				uint32 count = property.value.stringlist.count;
				if (count > 0 && property.value.stringlist.items != NULL) {
					newListItems = (char**)malloc(count * sizeof(char*));
					if (newListItems == NULL)
						return B_NO_MEMORY;
					for (uint32 i = 0; i < count; i++) {
						newListItems[i] = strdup(
							property.value.stringlist.items[i]);
						if (newListItems[i] == NULL) {
							for (uint32 j = 0; j < i; j++)
								free(newListItems[j]);
							free(newListItems);
							return B_NO_MEMORY;
						}
						newListCount++;
					}
				}
				break;
			}
			default:
				break;
		}

		// free old heap value based on old type
		switch (existing->type) {
			case B_STRING_TYPE:
				free(existing->value.string);
				break;
			case B_RAW_TYPE:
				free(existing->value.raw.data);
				break;
			case KOSM_STRINGLIST_TYPE:
				if (existing->value.stringlist.items != NULL) {
					for (uint32 i = 0; i < existing->value.stringlist.count; i++)
						free(existing->value.stringlist.items[i]);
					free(existing->value.stringlist.items);
				}
				break;
			default:
				break;
		}

		// write new value
		memset(&existing->value, 0, sizeof(existing->value));
		existing->type = property.type;

		switch (property.type) {
			case B_UINT8_TYPE:
				existing->value.ui8 = property.value.ui8;
				break;
			case B_UINT16_TYPE:
				existing->value.ui16 = property.value.ui16;
				break;
			case B_UINT32_TYPE:
				existing->value.ui32 = property.value.ui32;
				break;
			case B_UINT64_TYPE:
				existing->value.ui64 = property.value.ui64;
				break;
			case B_STRING_TYPE:
				existing->value.string = newStr;
				break;
			case B_RAW_TYPE:
				existing->value.raw.data = newRawData;
				existing->value.raw.length = newRawLen;
				break;
			case KOSM_STRINGLIST_TYPE:
				existing->value.stringlist.items = newListItems;
				existing->value.stringlist.count = newListCount;
				break;
			default:
				break;
		}

		return B_OK;
	}

	// new entry: allocate and insert
	DkPropertyEntry* entry = new(std::nothrow) DkPropertyEntry();
	if (entry == NULL)
		return B_NO_MEMORY;

	status_t status = entry->SetFrom(property);
	if (status != B_OK) {
		delete entry;
		return status;
	}

	status = fTable.Insert(entry);
	if (status != B_OK) {
		delete entry;
		return status;
	}

	_InsertListAppend(entry);
	return B_OK;
}


const DkPropertyEntry*
DkPropertyStore::Lookup(const char* name) const
{
	ASSERT(fCommitted);
	return fTable.Lookup(name);
}


status_t
DkPropertyStore::GetUInt8(const char* name, uint8* _value) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT8_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui8;
	return B_OK;
}


status_t
DkPropertyStore::GetUInt16(const char* name, uint16* _value) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT16_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui16;
	return B_OK;
}


status_t
DkPropertyStore::GetUInt32(const char* name, uint32* _value) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT32_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui32;
	return B_OK;
}


status_t
DkPropertyStore::GetUInt64(const char* name, uint64* _value) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_UINT64_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.ui64;
	return B_OK;
}


status_t
DkPropertyStore::GetString(const char* name, const char** _value) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_STRING_TYPE)
		return B_BAD_TYPE;
	*_value = entry->value.string;
	return B_OK;
}


status_t
DkPropertyStore::GetRaw(const char* name, const void** _data,
	size_t* _length) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_RAW_TYPE)
		return B_BAD_TYPE;
	if (_data != NULL)
		*_data = entry->value.raw.data;
	if (_length != NULL)
		*_length = entry->value.raw.length;
	return B_OK;
}


status_t
DkPropertyStore::GetStringCopy(const char* name, char* buffer,
	size_t bufferSize) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_STRING_TYPE)
		return B_BAD_TYPE;

	const char* str = entry->value.string;
	if (str == NULL)
		str = "";

	size_t len = strlen(str);
	if (len >= bufferSize) {
		if (bufferSize > 0) {
			memcpy(buffer, str, bufferSize - 1);
			buffer[bufferSize - 1] = '\0';
		}
		return B_BUFFER_OVERFLOW;
	}

	memcpy(buffer, str, len + 1);
	return B_OK;
}


status_t
DkPropertyStore::GetRawCopy(const char* name, void* buffer,
	size_t bufferSize, size_t* _copiedSize) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL)
		return B_NAME_NOT_FOUND;
	if (entry->type != B_RAW_TYPE)
		return B_BAD_TYPE;

	size_t dataLen = entry->value.raw.length;
	size_t toCopy = 0;
	if (entry->value.raw.data != NULL)
		toCopy = dataLen < bufferSize ? dataLen : bufferSize;
	if (toCopy > 0)
		memcpy(buffer, entry->value.raw.data, toCopy);
	if (_copiedSize != NULL)
		*_copiedSize = toCopy;

	return dataLen > bufferSize ? B_BUFFER_OVERFLOW : B_OK;
}


bool
DkPropertyStore::ContainsInStringList(const char* name,
	const char* value) const
{
	const DkPropertyEntry* entry = Lookup(name);
	if (entry == NULL || entry->type != KOSM_STRINGLIST_TYPE)
		return false;

	for (uint32 i = 0; i < entry->value.stringlist.count; i++) {
		if (strcmp(entry->value.stringlist.items[i], value) == 0)
			return true;
	}
	return false;
}


bool
DkPropertyStore::Matches(const dk_match_rule* rules) const
{
	if (rules == NULL)
		return true;

	for (; rules->name != NULL; rules++) {
		const DkPropertyEntry* entry = Lookup(rules->name);
		if (entry == NULL)
			return false;

		if (entry->type != rules->type) {
			// string rule matches against stringlist
			if (entry->type == KOSM_STRINGLIST_TYPE
				&& rules->type == B_STRING_TYPE) {
				if (!ContainsInStringList(rules->name, rules->value.string))
					return false;
				continue;
			}
			return false;
		}

		switch (rules->type) {
			case B_UINT8_TYPE:
				if (entry->value.ui8 != rules->value.ui8)
					return false;
				break;
			case B_UINT16_TYPE:
				if (entry->value.ui16 != rules->value.ui16)
					return false;
				break;
			case B_UINT32_TYPE:
				if (entry->value.ui32 != rules->value.ui32)
					return false;
				break;
			case B_UINT64_TYPE:
				if (entry->value.ui64 != rules->value.ui64)
					return false;
				break;
			case B_STRING_TYPE:
				if (entry->value.string == NULL || rules->value.string == NULL)
					return entry->value.string == rules->value.string;
				if (strcmp(entry->value.string, rules->value.string) != 0)
					return false;
				break;
			default:
				return false;
		}
	}

	return true;
}


uint32
DkPropertyStore::CountProperties() const
{
	return fTable.CountElements();
}


status_t
DkPropertyStore::GetNext(DkPropertyEntry** _entry) const
{
	ASSERT(fCommitted);

	DkPropertyEntry* next;
	if (*_entry == NULL)
		next = fInsertHead;
	else
		next = (*_entry)->insert_next;

	if (next != NULL) {
		*_entry = next;
		return B_OK;
	}

	*_entry = NULL;
	return B_ENTRY_NOT_FOUND;
}


void
DkPropertyStore::_InsertListAppend(DkPropertyEntry* entry)
{
	entry->insert_next = NULL;
	if (fInsertTail != NULL)
		fInsertTail->insert_next = entry;
	else
		fInsertHead = entry;
	fInsertTail = entry;
}


void
DkPropertyStore::_InsertListRemove(DkPropertyEntry* entry)
{
	if (entry == fInsertHead) {
		fInsertHead = entry->insert_next;
		if (fInsertTail == entry)
			fInsertTail = NULL;
	} else {
		DkPropertyEntry* prev = fInsertHead;
		while (prev != NULL && prev->insert_next != entry)
			prev = prev->insert_next;
		if (prev != NULL) {
			prev->insert_next = entry->insert_next;
			if (fInsertTail == entry)
				fInsertTail = prev;
		}
	}
	entry->insert_next = NULL;
}
