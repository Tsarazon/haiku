/*
 * Copyright 2015, Hamish Morrison, hamishm53@gmail.com.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_SELECT_OPS_H
#define _KERNEL_SELECT_OPS_H

#include <kosm_ray.h>

struct select_ops {
	status_t (*select)(int32 object, struct select_info* info, bool kernel);
	status_t (*deselect)(int32 object, struct select_info* info, bool kernel);
};


/*!	Wrapper for kosm_select_ray that resolves userspace handles.
	For kernel callers, object is already an internal ID.
	For userspace callers, object is a per-process handle that must
	be translated to a kernel-internal ray ID.
	The resolved ID is stored in info->resolved_object for deselect.
*/
static inline status_t
kosm_ray_select_op(int32 object, struct select_info* info, bool kernel)
{
	int32 internalId = object;
	if (!kernel) {
		internalId = kosm_ray_handle_to_id((kosm_handle_t)object);
		if (internalId < 0)
			return internalId;
	}
	info->resolved_object = internalId;
	return kosm_select_ray(internalId, info, kernel);
}


/*!	Wrapper for kosm_deselect_ray that uses the resolved internal ID.
	For kernel callers, object is the internal ID (pass through).
	For userspace callers, the internal ID was stored in
	info->resolved_object during the select call.
*/
static inline status_t
kosm_ray_deselect_op(int32 object, struct select_info* info, bool kernel)
{
	int32 id = kernel ? object : info->resolved_object;
	return kosm_deselect_ray(id, info, kernel);
}


static const select_ops kSelectOps[] = {
	// B_OBJECT_TYPE_FD
	{
		select_fd,
		deselect_fd
	},

	// B_OBJECT_TYPE_SEMAPHORE
	{
		select_sem,
		deselect_sem
	},

	// B_OBJECT_TYPE_PORT
	{
		select_port,
		deselect_port
	},

	// B_OBJECT_TYPE_THREAD
	{
		select_thread,
		deselect_thread
	},

	// B_OBJECT_TYPE_KOSM_RAY
	{
		kosm_ray_select_op,
		kosm_ray_deselect_op
	}
};


static inline status_t
select_object(uint32 type, int32 object, struct select_info* sync, bool kernel)
{
	if (type >= B_COUNT_OF(kSelectOps))
		return B_BAD_VALUE;
	return kSelectOps[type].select(object, sync, kernel);
}


static inline status_t
deselect_object(uint32 type, int32 object, struct select_info* sync, bool kernel)
{
	if (type >= B_COUNT_OF(kSelectOps))
		return B_BAD_VALUE;
	return kSelectOps[type].deselect(object, sync, kernel);
}


#endif
