/*
 * Copyright 2006 Haiku Inc. All rights reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DM_WRAPPER_H
#define DM_WRAPPER_H


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
status_t dm_get_node_info(kosm_handle_t node, kosm_dk_node_info *info);
void close_node(kosm_handle_t node);


#endif /* DM_WRAPPER_H */
