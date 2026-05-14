/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <KosmOS.h>
#include <device_keeper_defs.h>
#include <syscalls.h>


kosm_handle_t
kosm_dk_get_root(void)
{
	return _kern_kosm_dk_get_root();
}


kosm_handle_t
kosm_dk_get_child(kosm_handle_t parent)
{
	return _kern_kosm_dk_get_child(parent);
}


kosm_handle_t
kosm_dk_get_next_child(kosm_handle_t parent, kosm_handle_t previous)
{
	return _kern_kosm_dk_get_next_child(parent, previous);
}


status_t
kosm_dk_get_property(kosm_handle_t node, const char *name,
	kosm_dk_prop_value *outValue)
{
	return _kern_kosm_dk_get_property(node, name, outValue);
}


status_t
kosm_dk_find_node(const kosm_dk_match_rule *rules,
	kosm_handle_t *iterator)
{
	return _kern_kosm_dk_find_node(rules, iterator);
}


status_t
kosm_dk_get_node_info(kosm_handle_t node, kosm_dk_node_info *info)
{
	return _kern_kosm_dk_get_node_info(node, info);
}
