/*
 * Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <new>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <util/AutoLock.h>
#include <device_keeper.h>
#include <Drivers.h>

#include <team.h>

extern "C" {
#include <drivers/tty.h>
#include <tty_module.h>
}
#include "tty_private.h"


//#define PTY_TRACE
#ifdef PTY_TRACE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif

#define DRIVER_NAME "pty"


#define PTY_DRIVER_MODULE_NAME "drivers/pty/dk_driver_v1"

static dk_keeper_info* sDeviceKeeper;

// Singleton guard: the pty driver is a single-instance driver (it owns
// a fixed-size table of master/slave TTY slots and a global lock),
// so pty_probe must refuse to match more than one node at a time.
// Set true by a successful pty_attach, cleared by pty_detach.
static bool sPtyAttached = false;

tty_module_info *gTTYModule = NULL;

struct mutex gGlobalTTYLock;

static const uint32 kNumTTYs = 64;
// Index layout (2 * kNumTTYs + 3 slots):
//   [0, kNumTTYs)              — "pt/XY" master names (strdup'ed)
//   [kNumTTYs, 2 * kNumTTYs)   — "tt/XY" slave names (strdup'ed)
//   [2 * kNumTTYs]             — "ptmx" (string literal)
//   [2 * kNumTTYs + 1]         — "tty"  (string literal)
//   [2 * kNumTTYs + 2]         — NULL terminator
char *gDeviceNames[kNumTTYs * 2 + 3];

struct tty* gMasterTTYs[kNumTTYs];
struct tty* gSlaveTTYs[kNumTTYs];


static void
pty_free_device_names()
{
	// Only entries [0, 2 * kNumTTYs) were strdup'ed; the "ptmx"/"tty"
	// slots point at string literals and must not be freed.
	for (uint32 i = 0; i < 2 * kNumTTYs; i++) {
		free(gDeviceNames[i]);
		gDeviceNames[i] = NULL;
	}
	gDeviceNames[2 * kNumTTYs] = NULL;
	gDeviceNames[2 * kNumTTYs + 1] = NULL;
}


static status_t
pty_initialize()
{
	TRACE((DRIVER_NAME ": init pty\n"));

	mutex_init(&gGlobalTTYLock, "tty global");
	memset(gDeviceNames, 0, sizeof(gDeviceNames));
	memset(gMasterTTYs, 0, sizeof(gMasterTTYs));
	memset(gSlaveTTYs, 0, sizeof(gSlaveTTYs));

	char letter = 'p';
	int8 digit = 0;

	for (uint32 i = 0; i < kNumTTYs; i++) {
		char buffer[64];

		snprintf(buffer, sizeof(buffer), "pt/%c%x", letter, digit);
		gDeviceNames[i] = strdup(buffer);

		snprintf(buffer, sizeof(buffer), "tt/%c%x", letter, digit);
		gDeviceNames[i + kNumTTYs] = strdup(buffer);

		if (++digit > 15)
			digit = 0, letter++;

		if (gDeviceNames[i] == NULL || gDeviceNames[i + kNumTTYs] == NULL) {
			pty_free_device_names();
			mutex_destroy(&gGlobalTTYLock);
			return B_NO_MEMORY;
		}
	}

	gDeviceNames[2 * kNumTTYs] = (char *)"ptmx";
	gDeviceNames[2 * kNumTTYs + 1] = (char *)"tty";

	return B_OK;
}


static void
pty_uninitialize()
{
	TRACE((DRIVER_NAME ": uninit pty\n"));

	// Tear down any TTY pairs that are still allocated. TTYs are created
	// lazily in master_open, so slots may be NULL — that's fine. Pairs
	// must be destroyed together and in order (slave before master) to
	// match the construction order in master_open.
	//
	// NOTE: we skip pairs that still have live references (ref_count > 0).
	// In normal operation, the devfs layer closes all open cookies before
	// detach is called, so this should never happen; but on a surprise
	// unregister_node without device_removed unblocking I/O, a pty pair
	// could still be held by userspace. Leaking the pair is strictly
	// better than destroying it under a live reader/writer.
	mutex_lock(&gGlobalTTYLock);
	for (uint32 i = 0; i < kNumTTYs; i++) {
		if (gMasterTTYs[i] != NULL && gMasterTTYs[i]->ref_count != 0)
			continue;
		if (gSlaveTTYs[i] != NULL && gSlaveTTYs[i]->ref_count != 0)
			continue;

		if (gSlaveTTYs[i] != NULL) {
			gTTYModule->tty_destroy(gSlaveTTYs[i]);
			gSlaveTTYs[i] = NULL;
		}
		if (gMasterTTYs[i] != NULL) {
			gTTYModule->tty_destroy(gMasterTTYs[i]);
			gMasterTTYs[i] = NULL;
		}
	}
	mutex_unlock(&gGlobalTTYLock);

	pty_free_device_names();
	mutex_destroy(&gGlobalTTYLock);
}


static bool
is_master_device(const char* name)
{
	// pt/* and ptmx are master devices
	return strncmp(name, "pt/", 3) == 0 || strcmp(name, "ptmx") == 0;
}


//	#pragma mark - helpers


static int32
get_tty_index(const char *name)
{
	// device names follow this form: "pt/%c%x"
	int8 digit = name[4];
	if (digit >= 'a') {
		// hexadecimal digits
		digit -= 'a' - 10;
	} else
		digit -= '0';

	return (name[3] - 'p') * 16 + digit;
}


static int32
get_tty_index(struct tty *tty)
{
	int32 index = -1;
	for (uint32 i = 0; i < kNumTTYs; i++) {
		if (tty == gMasterTTYs[i] || tty == gSlaveTTYs[i]) {
			index = i;
			break;
		}
	}
	return index;
}


//	#pragma mark - device hooks


static bool
master_service(struct tty *tty, uint32 op, void *buffer, size_t length)
{
	// nothing here yet
	return false;
}


static bool
slave_service(struct tty *tty, uint32 op, void *buffer, size_t length)
{
	// nothing here yet
	return false;
}


static status_t
master_open(const char *name, uint32 flags, void **_cookie)
{
	bool findUnusedTTY = strcmp(name, "ptmx") == 0;

	int32 index = -1;
	if (!findUnusedTTY) {
		index = get_tty_index(name);
		if (index >= (int32)kNumTTYs)
			return B_ERROR;
	}

	TRACE(("pty_open: TTY index = %" B_PRId32 " (name = %s)\n", index, name));

	MutexLocker globalLocker(gGlobalTTYLock);

	if (findUnusedTTY) {
		for (index = 0; index < (int32)kNumTTYs; index++) {
			if (gMasterTTYs[index] == NULL)
				break;
		}
		if (index >= (int32)kNumTTYs)
			return ENOENT;
	} else if (gMasterTTYs[index] != NULL && gMasterTTYs[index]->ref_count != 0) {
		// we're already open!
		return B_BUSY;
	}

	status_t status = B_OK;

	if (gMasterTTYs[index] == NULL) {
		status = gTTYModule->tty_create(master_service, NULL, &gMasterTTYs[index]);
		if (status != B_OK)
			return status;
	}
	if (gSlaveTTYs[index] == NULL) {
		status = gTTYModule->tty_create(slave_service, gMasterTTYs[index], &gSlaveTTYs[index]);
		if (status != B_OK)
			return status;
	}

	tty_cookie *cookie;
	status = gTTYModule->tty_create_cookie(gMasterTTYs[index], gSlaveTTYs[index], flags, &cookie);
	if (status != B_OK)
		return status;

	*_cookie = cookie;
	return B_OK;
}


static status_t
slave_open(const char *name, uint32 flags, void **_cookie)
{
	// Get the tty index: Opening "/dev/tty" means opening the process'
	// controlling tty.
	int32 index = get_tty_index(name);
	if (strcmp(name, "tty") == 0) {
		struct tty *controllingTTY = (struct tty *)team_get_controlling_tty();
		if (controllingTTY == NULL)
			return B_NOT_ALLOWED;

		index = get_tty_index(controllingTTY);
		if (index < 0)
			return B_NOT_ALLOWED;
	} else {
		index = get_tty_index(name);
		if (index >= (int32)kNumTTYs)
			return B_ERROR;
	}

	TRACE(("slave_open: TTY index = %" B_PRId32 " (name = %s)\n", index,
		name));

	MutexLocker globalLocker(gGlobalTTYLock);

	// we may only be used if our master has already been opened
	if (gMasterTTYs[index] == NULL || gMasterTTYs[index]->open_count == 0
			|| gSlaveTTYs[index] == NULL) {
		return B_IO_ERROR;
	}

	bool makeControllingTTY = (flags & O_NOCTTY) == 0;
	pid_t processID = getpid();
	pid_t sessionID = getsid(processID);

	if (gSlaveTTYs[index]->open_count == 0) {
		// We only allow session leaders to open the tty initially.
		if (makeControllingTTY && processID != sessionID)
			return B_NOT_ALLOWED;
	} else if (makeControllingTTY) {
		// If already open, we allow only processes from the same session
		// to open the tty again while becoming controlling tty
		pid_t ttySession = gSlaveTTYs[index]->settings->session_id;
		if (ttySession >= 0) {
			makeControllingTTY = false;
		} else {
			// The tty is not associated with a session yet. The process needs
			// to be a session leader.
			if (makeControllingTTY && processID != sessionID)
				return B_NOT_ALLOWED;
		}
	}

	if (gSlaveTTYs[index]->open_count == 0) {
		gSlaveTTYs[index]->settings->session_id = -1;
		gSlaveTTYs[index]->settings->pgrp_id = -1;
	}

	tty_cookie *cookie;
	status_t status = gTTYModule->tty_create_cookie(gSlaveTTYs[index], gMasterTTYs[index], flags,
		&cookie);
	if (status != B_OK)
		return status;

	if (makeControllingTTY) {
		gSlaveTTYs[index]->settings->session_id = sessionID;
		gSlaveTTYs[index]->settings->pgrp_id = sessionID;
		team_set_controlling_tty(gSlaveTTYs[index]);
	}

	*_cookie = cookie;
	return B_OK;
}


static status_t
pty_close(void *_cookie)
{
	tty_cookie *cookie = (tty_cookie *)_cookie;

	TRACE(("pty_close: cookie %p\n", _cookie));

	MutexLocker globalLocker(gGlobalTTYLock);

	if (cookie->tty->is_master) {
		// close all connected slave cookies first
		while (tty_cookie *slave = cookie->other_tty->cookies.Head())
			gTTYModule->tty_close_cookie(slave);
	}

	gTTYModule->tty_close_cookie(cookie);

	return B_OK;
}


static status_t
pty_free_cookie(void *_cookie)
{
	// The TTY is already closed. We only have to free the cookie.
	tty_cookie *cookie = (tty_cookie *)_cookie;
	struct tty *tty = cookie->tty;

	MutexLocker globalLocker(gGlobalTTYLock);

	gTTYModule->tty_destroy_cookie(cookie);

	if (tty->ref_count == 0) {
		// We need to destroy both master and slave TTYs at the same time,
		// and in the proper order.
		int32 index = get_tty_index(tty);
		if (index < 0)
			return B_OK;

		if (gMasterTTYs[index]->ref_count == 0 && gSlaveTTYs[index]->ref_count == 0) {
			gTTYModule->tty_destroy(gSlaveTTYs[index]);
			gTTYModule->tty_destroy(gMasterTTYs[index]);
			gMasterTTYs[index] = gSlaveTTYs[index] = NULL;
		}
	}

	return B_OK;
}


static status_t
pty_ioctl(void *_cookie, uint32 op, void *buffer, size_t length)
{
	tty_cookie *cookie = (tty_cookie *)_cookie;

	struct tty* tty = cookie->tty;
	RecursiveLocker locker(tty->lock);

	TRACE(("pty_ioctl: cookie %p, op %" B_PRIu32 ", buffer %p, length %lu"
		"\n", _cookie, op, buffer, length));

	switch (op) {
		case B_IOCTL_GET_TTY_INDEX:
		{
			int32 ptyIndex = get_tty_index(cookie->tty);
			if (ptyIndex < 0)
				return B_BAD_VALUE;

			if (user_memcpy(buffer, &ptyIndex, sizeof(int32)) < B_OK)
				return B_BAD_ADDRESS;

			return B_OK;
		}

		case B_IOCTL_GRANT_TTY:
		{
			if (!cookie->tty->is_master)
				return B_BAD_VALUE;

			int32 ptyIndex = get_tty_index(cookie->tty);
			if (ptyIndex < 0)
				return B_BAD_VALUE;

			// get slave path
			char path[64];
			snprintf(path, sizeof(path), "/dev/%s",
				gDeviceNames[kNumTTYs + ptyIndex]);

			// set owner and permissions respectively
			if (chown(path, getuid(), getgid()) != 0
				|| chmod(path, S_IRUSR | S_IWUSR | S_IWGRP) != 0) {
				return errno;
			}

			return B_OK;
		}

		case 'pgid':				// BeOS
			op = TIOCSPGRP;

		case 'wsiz':				// BeOS
			op = TIOCSWINSZ;
			break;

		default:
			break;
	}

	return gTTYModule->tty_control(cookie, op, buffer, length);
}


static status_t
pty_read(void *_cookie, off_t offset, void *buffer, size_t *_length)
{
	tty_cookie *cookie = (tty_cookie *)_cookie;

	TRACE(("pty_read: cookie %p, offset %" B_PRIdOFF ", buffer %p, length "
		"%lu\n", _cookie, offset, buffer, *_length));

	status_t result = gTTYModule->tty_read(cookie, buffer, _length);

	TRACE(("pty_read done: cookie %p, result: %" B_PRIx32 ", length %lu\n",
		_cookie, result, *_length));

	return result;
}


static status_t
pty_write(void *_cookie, off_t offset, const void *buffer, size_t *_length)
{
	tty_cookie *cookie = (tty_cookie *)_cookie;

	TRACE(("pty_write: cookie %p, offset %" B_PRIdOFF ", buffer %p, length "
		"%lu\n", _cookie, offset, buffer, *_length));

	status_t result = gTTYModule->tty_write(cookie, buffer, _length);

	TRACE(("pty_write done: cookie %p, result: %" B_PRIx32 ", length %lu\n",
		_cookie, result, *_length));

	return result;
}


static status_t
pty_select(void *_cookie, uint8 event, selectsync *sync)
{
	tty_cookie *cookie = (tty_cookie *)_cookie;

	return gTTYModule->tty_select(cookie, event, 0, sync);
}


static status_t
pty_deselect(void *_cookie, uint8 event, selectsync *sync)
{
	tty_cookie *cookie = (tty_cookie *)_cookie;

	return gTTYModule->tty_deselect(cookie, event, sync);
}


//	#pragma mark - dk_device_ops / dk_driver_info


static status_t
pty_dk_open(void *driverCookie, const char *path, int openMode, void **_cookie)
{
	if (is_master_device(path))
		return master_open(path, openMode, _cookie);
	return slave_open(path, openMode, _cookie);
}


static dk_device_ops sDeviceOps = {
	pty_dk_open,
	pty_close,
	pty_free_cookie,
	pty_read,
	pty_write,
	NULL,	// io
	pty_ioctl,
	pty_select,
	pty_deselect,
	NULL,	// device_removed
};


static const dk_match_rule sPtyMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sPtyMatchDict = {
	sPtyMatchRules,
	0
};


static float
pty_probe(dk_node* node)
{
	if (sPtyAttached)
		return 0.0;
	return 0.01;
}


static status_t
pty_attach(dk_node* node, void** cookie)
{
	TRACE((DRIVER_NAME ": attach\n"));

	status_t status = pty_initialize();
	if (status != B_OK)
		return status;

	// Publish all PTY device paths into devfs. On any failure, roll back
	// devices already published so detach is not called with a partial
	// state (detach would happen anyway, but rollback keeps devfs clean
	// in case the framework does not call detach on attach failure).
	uint32 published = 0;
	for (uint32 i = 0; gDeviceNames[i] != NULL; i++) {
		status = sDeviceKeeper->publish_device(node, gDeviceNames[i],
			&sDeviceOps);
		if (status != B_OK) {
			for (uint32 j = 0; j < published; j++)
				sDeviceKeeper->unpublish_device(node, gDeviceNames[j]);
			pty_uninitialize();
			return status;
		}
		published++;
	}

	sPtyAttached = true;
	*cookie = node;
	return B_OK;
}


static void
pty_detach(void* _cookie)
{
	TRACE((DRIVER_NAME ": detach\n"));

	dk_node* node = (dk_node*)_cookie;

	// Unpublish all device paths so that no new opens can find the
	// devfs entries pointing at sDeviceOps after we tear down the TTY
	// tables below. Existing opens must already have been closed by the
	// devfs layer before detach is called.
	for (uint32 i = 0; gDeviceNames[i] != NULL; i++)
		sDeviceKeeper->unpublish_device(node, gDeviceNames[i]);

	pty_uninitialize();
	sPtyAttached = false;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ B_TTY_MODULE_NAME, (module_info**)&gTTYModule },
	{}
};

static dk_driver_info sPtyDriver = {
	.info	= { PTY_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sPtyMatchDict,
	.probe	= pty_probe,
	.attach	= pty_attach,
	.detach	= pty_detach,
	.ops	= &sDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sPtyDriver,
	NULL
};
