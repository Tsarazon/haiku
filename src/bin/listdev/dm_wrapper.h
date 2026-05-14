/*
 * Copyright 2006-2012, Haiku, Inc. All Rights Reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DM_WRAPPER_H_
#define _DM_WRAPPER_H_


#include <KosmOS.h>
#include <device_keeper_defs.h>


status_t init_dm_wrapper(void);
status_t uninit_dm_wrapper(void);

status_t get_root(kosm_handle_t *cookie);
status_t get_child(kosm_handle_t parent, kosm_handle_t *child);
status_t get_next_child(kosm_handle_t parent, kosm_handle_t previous,
	kosm_handle_t *next);
status_t dm_get_property(kosm_handle_t node, const char* name,
	kosm_dk_prop_value *value);
void close_node(kosm_handle_t node);


#endif
