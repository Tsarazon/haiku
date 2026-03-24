/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 *
 * Shared kernel/userland definitions for DeviceKeeper syscalls.
 */
#ifndef _SYSTEM_DEVICE_KEEPER_DEFS_H
#define _SYSTEM_DEVICE_KEEPER_DEFS_H

#include <KosmOS.h>
#include <TypeConstants.h>


// Handle type tag for DkNode objects in KosmHandleTable
#define KOSM_HANDLE_DK_NODE			0x10

// Property value limits
#define KOSM_DK_PROP_STRING_MAX		256

// Match rule limits
#define KOSM_DK_MATCH_NAME_MAX		64
#define KOSM_DK_MATCH_STRING_MAX	64
#define KOSM_DK_MATCH_MAX_RULES		8


// Property value returned by _user_kosm_dk_get_property
typedef struct kosm_dk_prop_value {
	uint32		type;
	uint32		size;
	union {
		uint8	ui8;
		uint16	ui16;
		uint32	ui32;
		uint64	ui64;
		char	string[KOSM_DK_PROP_STRING_MAX];
	} value;
} kosm_dk_prop_value;


// Userland match rule for _user_kosm_dk_find_node.
// Array is terminated by name[0] == '\0'.
typedef struct kosm_dk_match_rule {
	char		name[KOSM_DK_MATCH_NAME_MAX];
	type_code	type;
	union {
		uint8	ui8;
		uint16	ui16;
		uint32	ui32;
		uint64	ui64;
		char	string[KOSM_DK_MATCH_STRING_MAX];
	} value;
} kosm_dk_match_rule;


#endif // _SYSTEM_DEVICE_KEEPER_DEFS_H
