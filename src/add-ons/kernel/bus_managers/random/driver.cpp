/*
 * Copyright 2002-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval, korli@berlios.de
 *		Axel Dörfler, axeld@pinc-software.de
 *		David Reid
 */


#include <stdio.h>

#include <device_keeper.h>
#include <Drivers.h>
#include <generic_syscall.h>
#include <kernel.h>
#include <malloc.h>
#include <random_defs.h>
#include <string.h>
#include <util/AutoLock.h>

#include "yarrow_rng.h"


//#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x...) dprintf("random: " x)
#else
#	define TRACE(x...) ;
#endif
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define	RANDOM_DRIVER_MODULE_NAME "bus_managers/random/dk_driver_v1"


static mutex sRandomLock;
static void *sRandomCookie;
static dk_keeper_info* gDeviceKeeper;


typedef struct {
	dk_node*	node;
} random_driver_info;


static status_t
random_queue_randomness(uint64 value)
{
	MutexLocker locker(&sRandomLock);
	RANDOM_ENQUEUE(value);
	return B_OK;
}


//	#pragma mark - dk_device_ops


static status_t
random_open(void *deviceCookie, const char *name, int flags, void **cookie)
{
	TRACE("open(\"%s\")\n", name);
	return B_OK;
}


static status_t
random_read(void *cookie, off_t position, void *_buffer, size_t *_numBytes)
{
	TRACE("read(%lld,, %ld)\n", position, *_numBytes);

	MutexLocker locker(&sRandomLock);
	return RANDOM_READ(sRandomCookie, _buffer, _numBytes);
}


static status_t
random_write(void *cookie, off_t position, const void *buffer, size_t *_numBytes)
{
	TRACE("write(%lld,, %ld)\n", position, *_numBytes);
	MutexLocker locker(&sRandomLock);
	return RANDOM_WRITE(sRandomCookie, buffer, _numBytes);
}


static status_t
random_control(void *cookie, uint32 op, void *arg, size_t length)
{
	TRACE("ioctl(%ld)\n", op);
	return B_ERROR;
}


static status_t
random_generic_syscall(const char* subsystem, uint32 function, void* buffer,
	size_t bufferSize)
{
	switch (function) {
		case RANDOM_GET_ENTROPY:
		{
			random_get_entropy_args args;
			if (bufferSize != sizeof(args) || !IS_USER_ADDRESS(buffer))
				return B_BAD_VALUE;

			if (user_memcpy(&args, buffer, sizeof(args)) != B_OK)
				return B_BAD_ADDRESS;
			if (!IS_USER_ADDRESS(args.buffer))
				return B_BAD_ADDRESS;

			status_t result = random_read(NULL, 0, args.buffer, &args.length);
			if (result < 0)
				return result;

			return user_memcpy(buffer, &args, sizeof(args));
		}
	}
	return B_BAD_HANDLER;
}


static status_t
random_close(void *cookie)
{
	TRACE("close()\n");
	return B_OK;
}


static status_t
random_free(void *cookie)
{
	TRACE("free()\n");
	return B_OK;
}


static status_t
random_select(void *cookie, uint8 event, selectsync *sync)
{
	TRACE("select()\n");

	if (event == B_SELECT_READ) {
		notify_select_event(sync, event);
	} else if (event == B_SELECT_WRITE) {
		notify_select_event(sync, event);
	}
	return B_OK;
}


static status_t
random_deselect(void *cookie, uint8 event, selectsync *sync)
{
	TRACE("deselect()\n");
	return B_OK;
}


static dk_device_ops sRandomDeviceOps = {
	random_open,
	random_close,
	random_free,
	random_read,
	random_write,
	NULL,	// io
	random_control,
	random_select,
	random_deselect,
	NULL,	// device_removed
};


//	#pragma mark - dk_driver_info


static const dk_match_rule sRandomMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "root"),
	DK_MATCH_END
};

static const dk_match_dict sRandomMatchDict = {
	sRandomMatchRules,
	0
};


static status_t
random_attach(dk_node* node, void** cookie)
{
	CALLED();

	random_driver_info* info
		= (random_driver_info*)malloc(sizeof(random_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	mutex_init(&sRandomLock, "/dev/random lock");
	RANDOM_INIT();

	memset(info, 0, sizeof(*info));
	info->node = node;

	register_generic_syscall(RANDOM_SYSCALLS, random_generic_syscall, 1, 0);

	status_t status = gDeviceKeeper->publish_device(node, "random",
		&sRandomDeviceOps);
	if (status == B_OK) {
		gDeviceKeeper->publish_device(node, "urandom", &sRandomDeviceOps);
	}

	*cookie = info;
	return B_OK;
}


static void
random_detach(void* _cookie)
{
	CALLED();

	unregister_generic_syscall(RANDOM_SYSCALLS, 1);

	RANDOM_UNINIT();

	mutex_destroy(&sRandomLock);

	random_driver_info* info = (random_driver_info*)_cookie;
	free(info);
}


//	#pragma mark - callback module


static status_t
random_controller_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
	}
	return B_ERROR;
}


// Plain kernel module retrieved by RNG controller drivers via
// get_module(RANDOM_FOR_CONTROLLER_MODULE_NAME). Controllers call
// queue_randomness() to push entropy into the pool.
static random_for_controller_interface sRandomForControllerModule = {
	{
		RANDOM_FOR_CONTROLLER_MODULE_NAME,
		0,
		random_controller_std_ops
	},
	random_queue_randomness,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};


static dk_driver_info sRandomDriver = {
	.info		= { RANDOM_DRIVER_MODULE_NAME, 0, NULL },
	.match		= &sRandomMatchDict,
	.attach		= random_attach,
	.detach		= random_detach,
	.ops		= &sRandomDeviceOps,
	.node_flags	= KOSM_KEEP_DRIVER_LOADED,
};


module_info* modules[] = {
	(module_info*)&sRandomDriver,
	(module_info*)&sRandomForControllerModule,
	NULL
};
