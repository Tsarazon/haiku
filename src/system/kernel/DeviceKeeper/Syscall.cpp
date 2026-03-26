/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Syscall.h"
#include "DeviceKeeper.h"
#include "Node.h"

#include <string.h>

#include <kernel.h>
#include <kosm_handle.h>
#include <util/AutoLock.h>


// Helper: insert a DkNode into the calling process's handle table.
// Acquires one reference via KosmHandleTable::Insert.
static kosm_handle_t
insert_node_handle(DkNode* node)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return KOSM_HANDLE_INVALID;

	return table->Insert(node, KOSM_HANDLE_DK_NODE, KOSM_RIGHT_ALL);
}


// Helper: resolve a handle to DkNode*. Acquires one reference.
// Caller must ReleaseReference() when done.
static DkNode*
lookup_node(kosm_handle_t handle)
{
	KosmHandleTable* table = KosmHandleTable::TableForCurrent();
	if (table == NULL)
		return NULL;

	KernelReferenceable* object;
	status_t status = table->Lookup(handle, KOSM_HANDLE_DK_NODE,
		KOSM_RIGHT_READ, &object);
	if (status != B_OK)
		return NULL;

	return static_cast<DkNode*>(object);
}


kosm_handle_t
_user_kosm_dk_get_root(void)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return KOSM_HANDLE_INVALID;

	DkNode* root = keeper->RootNode();
	if (root == NULL)
		return KOSM_HANDLE_INVALID;

	return insert_node_handle(root);
}


kosm_handle_t
_user_kosm_dk_get_child(kosm_handle_t parent)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return KOSM_HANDLE_INVALID;

	DkNode* parentNode = lookup_node(parent);
	if (parentNode == NULL)
		return KOSM_HANDLE_INVALID;

	ReadLocker locker(keeper->TreeLock());

	DkNodeList::Iterator it = parentNode->Children().GetIterator();
	kosm_handle_t result = KOSM_HANDLE_INVALID;

	while (it.HasNext()) {
		DkNode* child = it.Next();
		if (child->IsRegistered()) {
			result = insert_node_handle(child);
			break;
		}
	}

	locker.Unlock();
	parentNode->ReleaseReference();

	return result;
}


kosm_handle_t
_user_kosm_dk_get_next_child(kosm_handle_t parent, kosm_handle_t previous)
{
	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return KOSM_HANDLE_INVALID;

	DkNode* parentNode = lookup_node(parent);
	if (parentNode == NULL)
		return KOSM_HANDLE_INVALID;

	DkNode* previousNode = NULL;
	if (previous != KOSM_HANDLE_INVALID) {
		previousNode = lookup_node(previous);
		if (previousNode == NULL) {
			parentNode->ReleaseReference();
			return KOSM_HANDLE_INVALID;
		}
	}

	ReadLocker locker(keeper->TreeLock());

	DkNodeList::Iterator it = parentNode->Children().GetIterator();
	bool pastPrevious = (previousNode == NULL);
	kosm_handle_t result = KOSM_HANDLE_INVALID;

	while (it.HasNext()) {
		DkNode* child = it.Next();
		if (!pastPrevious) {
			if (child == previousNode)
				pastPrevious = true;
			continue;
		}
		if (child->IsRegistered()) {
			result = insert_node_handle(child);
			break;
		}
	}

	locker.Unlock();
	parentNode->ReleaseReference();
	if (previousNode != NULL)
		previousNode->ReleaseReference();

	return result;
}


status_t
_user_kosm_dk_get_property(kosm_handle_t handle, const char* userName,
	kosm_dk_prop_value* userOutValue)
{
	if (!IS_USER_ADDRESS(userName) || !IS_USER_ADDRESS(userOutValue))
		return B_BAD_ADDRESS;

	char name[64];
	status_t status = user_strlcpy(name, userName, sizeof(name));
	if (status < B_OK)
		return status;

	DkNode* node = lookup_node(handle);
	if (node == NULL)
		return B_BAD_VALUE;

	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL) {
		node->ReleaseReference();
		return B_NO_INIT;
	}

	// Walk the parent chain under tree read lock.
	// Lock each node's mutex while checking and copying the property
	// value, so that concurrent SetAfterCommit cannot free a string
	// or raw buffer mid-copy.
	ReadLocker treeLocker(keeper->TreeLock());

	kosm_dk_prop_value val;
	memset(&val, 0, sizeof(val));
	status = B_NAME_NOT_FOUND;

	const DkNode* current = node;
	while (current != NULL) {
		MutexLocker nodeLocker(const_cast<DkNode*>(current)->NodeLock());
		const DkPropertyEntry* entry = current->Properties().Lookup(name);
		if (entry == NULL) {
			nodeLocker.Unlock();
			current = current->Parent();
			continue;
		}

		val.type = entry->type;
		val.size = 0;
		status = B_OK;

		switch (entry->type) {
			case B_UINT8_TYPE:
				val.value.ui8 = entry->value.ui8;
				val.size = sizeof(uint8);
				break;
			case B_UINT16_TYPE:
				val.value.ui16 = entry->value.ui16;
				val.size = sizeof(uint16);
				break;
			case B_UINT32_TYPE:
				val.value.ui32 = entry->value.ui32;
				val.size = sizeof(uint32);
				break;
			case B_UINT64_TYPE:
				val.value.ui64 = entry->value.ui64;
				val.size = sizeof(uint64);
				break;
			case B_STRING_TYPE:
			{
				const char* str = entry->value.string;
				if (str == NULL)
					str = "";
				size_t len = strlen(str);
				val.size = len + 1;
				if (len < KOSM_DK_PROP_STRING_MAX) {
					memcpy(val.value.string, str, len + 1);
				} else {
					memcpy(val.value.string, str,
						KOSM_DK_PROP_STRING_MAX - 1);
					val.value.string[KOSM_DK_PROP_STRING_MAX - 1] = '\0';
					status = B_BUFFER_OVERFLOW;
				}
				break;
			}
			default:
				status = B_BAD_TYPE;
				break;
		}
		break;
	}

	treeLocker.Unlock();
	node->ReleaseReference();

	if (status != B_OK && status != B_BUFFER_OVERFLOW)
		return status;

	status_t copyStatus = user_memcpy(userOutValue, &val, sizeof(val));
	if (copyStatus != B_OK)
		return copyStatus;

	return status;
}


status_t
_user_kosm_dk_find_node(const kosm_dk_match_rule* userRules,
	kosm_handle_t* userIterator)
{
	if (!IS_USER_ADDRESS(userRules) || !IS_USER_ADDRESS(userIterator))
		return B_BAD_ADDRESS;

	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL || keeper->RootNode() == NULL)
		return B_NO_INIT;

	// copy match rules from userland
	kosm_dk_match_rule uRules[KOSM_DK_MATCH_MAX_RULES];
	if (user_memcpy(uRules, userRules, sizeof(uRules)) != B_OK)
		return B_BAD_ADDRESS;

	// convert to kernel dk_match_rule (NULL-terminated)
	dk_match_rule kRules[KOSM_DK_MATCH_MAX_RULES + 1];
	int32 ruleCount = 0;

	for (int32 i = 0; i < KOSM_DK_MATCH_MAX_RULES; i++) {
		if (uRules[i].name[0] == '\0')
			break;
		uRules[i].name[sizeof(uRules[i].name) - 1] = '\0';
		kRules[ruleCount].name = uRules[i].name;
		kRules[ruleCount].type = uRules[i].type;

		switch (uRules[i].type) {
			case B_UINT8_TYPE:
				kRules[ruleCount].value.ui8 = uRules[i].value.ui8;
				break;
			case B_UINT16_TYPE:
				kRules[ruleCount].value.ui16 = uRules[i].value.ui16;
				break;
			case B_UINT32_TYPE:
				kRules[ruleCount].value.ui32 = uRules[i].value.ui32;
				break;
			case B_UINT64_TYPE:
				kRules[ruleCount].value.ui64 = uRules[i].value.ui64;
				break;
			case B_STRING_TYPE:
				uRules[i].value.string[sizeof(uRules[i].value.string) - 1]
					= '\0';
				kRules[ruleCount].value.string = uRules[i].value.string;
				break;
			default:
				return B_BAD_VALUE;
		}
		ruleCount++;
	}

	// NULL-name sentinel
	kRules[ruleCount].name = NULL;

	// read iterator handle from userland
	kosm_handle_t iterHandle;
	if (user_memcpy(&iterHandle, userIterator, sizeof(iterHandle)) != B_OK)
		return B_BAD_ADDRESS;

	// resolve previous node from iterator (NULL to start)
	DkNode* last = NULL;
	if (iterHandle != KOSM_HANDLE_INVALID) {
		last = lookup_node(iterHandle);
		if (last == NULL)
			return B_BAD_VALUE;
	}

	ReadLocker locker(keeper->TreeLock());

	DkNode* found = keeper->RootNode()->FindChildDeep(kRules, last);

	// insert_node_handle must happen under tree lock: FindChildDeep
	// returns a raw pointer without acquiring a reference, so the
	// node could be deleted after unlock if we don't pin it here.
	kosm_handle_t newHandle = KOSM_HANDLE_INVALID;
	if (found != NULL)
		newHandle = insert_node_handle(found);

	locker.Unlock();

	if (last != NULL)
		last->ReleaseReference();

	if (found == NULL) {
		kosm_handle_t invalid = KOSM_HANDLE_INVALID;
		user_memcpy(userIterator, &invalid, sizeof(invalid));
		return B_ENTRY_NOT_FOUND;
	}

	if (newHandle == KOSM_HANDLE_INVALID)
		return B_NO_MEMORY;

	return user_memcpy(userIterator, &newHandle, sizeof(newHandle));
}


status_t
_user_kosm_dk_get_node_info(kosm_handle_t handle,
	kosm_dk_node_info* userInfo)
{
	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	DkNode* node = lookup_node(handle);
	if (node == NULL)
		return B_BAD_VALUE;

	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL) {
		node->ReleaseReference();
		return B_NO_INIT;
	}

	kosm_dk_node_info info;
	memset(&info, 0, sizeof(info));

	// module name (immutable after creation, no lock needed)
	strlcpy(info.module_name, node->ModuleName(),
		sizeof(info.module_name));

	// flags (immutable after SetRegistered)
	info.flags = node->Flags();

	// driver state
	info.has_driver = node->IsAttached();

	// child count under tree read lock
	{
		ReadLocker treeLocker(keeper->TreeLock());
		uint32 count = 0;
		DkNodeList::Iterator it =
			node->Children().GetIterator();
		while (it.HasNext()) {
			DkNode* child = it.Next();
			if (child->IsRegistered())
				count++;
		}
		info.child_count = count;
	}

	// properties under node lock (string values may be mutable)
	{
		MutexLocker nodeLocker(node->NodeLock());

		info.property_count = node->Properties().CountProperties();

		node->Properties().GetStringCopy(KOSM_DEVICE_PRETTY_NAME,
			info.pretty_name, sizeof(info.pretty_name));

		node->Properties().GetStringCopy(KOSM_DEVICE_BUS,
			info.bus, sizeof(info.bus));

		// first published device path
		const char* path = node->FirstPublishedPath();
		if (path != NULL)
			strlcpy(info.device_path, path, sizeof(info.device_path));
	}

	node->ReleaseReference();

	return user_memcpy(userInfo, &info, sizeof(info));
}
