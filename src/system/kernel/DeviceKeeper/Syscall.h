/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_SYSCALL_H
#define DEVICE_KEEPER_SYSCALL_H

#include <KosmOS.h>
#include <device_keeper_defs.h>


#ifdef __cplusplus
extern "C" {
#endif


// Dedicated DeviceKeeper syscalls.
// Handles are kosm_handle_t, per-process via KosmHandleTable.
// Closed via generic _user_kosm_close_handle().
//
// Ownership rules:
//   - Every returned handle carries one reference. Caller must
//     close it via _user_kosm_close_handle() when done.
//   - _user_kosm_dk_find_node uses an iterator handle: caller
//     must close the previous iterator handle before or after
//     each call; the function does NOT close it automatically.

kosm_handle_t	_user_kosm_dk_get_root(void);

kosm_handle_t	_user_kosm_dk_get_child(kosm_handle_t parent);

kosm_handle_t	_user_kosm_dk_get_next_child(kosm_handle_t parent,
				kosm_handle_t previous);

// Recursive: walks parent chain if property not found locally.
status_t		_user_kosm_dk_get_property(kosm_handle_t node,
				const char* name,
				kosm_dk_prop_value* outValue);

status_t		_user_kosm_dk_find_node(
				const kosm_dk_match_rule* userRules,
				kosm_handle_t* iterator);


#ifdef __cplusplus
}
#endif


#endif // DEVICE_KEEPER_SYSCALL_H
