/*
 * Copyright 2006-2012, Haiku, Inc. All Rights Reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <syscalls.h>

#include "dm_wrapper.h"


status_t init_dm_wrapper(void)
{
	return B_OK;
}


status_t uninit_dm_wrapper(void)
{
	return B_OK;
}


status_t
get_root(kosm_handle_t *cookie)
{
	kosm_handle_t h = _kern_kosm_dk_get_root();
	if (h == KOSM_HANDLE_INVALID)
		return B_ERROR;
	*cookie = h;
	return B_OK;
}


status_t
get_child(kosm_handle_t parent, kosm_handle_t *child)
{
	kosm_handle_t h = _kern_kosm_dk_get_child(parent);
	if (h == KOSM_HANDLE_INVALID)
		return B_ENTRY_NOT_FOUND;
	*child = h;
	return B_OK;
}


status_t
get_next_child(kosm_handle_t parent, kosm_handle_t previous,
	kosm_handle_t *next)
{
	kosm_handle_t h = _kern_kosm_dk_get_next_child(parent, previous);
	if (h == KOSM_HANDLE_INVALID)
		return B_ENTRY_NOT_FOUND;
	*next = h;
	return B_OK;
}


status_t
dm_get_property(kosm_handle_t node, const char* name,
	kosm_dk_prop_value *value)
{
	return _kern_kosm_dk_get_property(node, name, value);
}


void
close_node(kosm_handle_t node)
{
	_kern_kosm_close_handle(node);
}
