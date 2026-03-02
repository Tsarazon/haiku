/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <string.h>

#include <KosmOS.h>

#include "strace.h"
#include "Context.h"
#include "TypeHandler.h"


#define FLAG_INFO_ENTRY(name) \
	{ name, #name }

static const FlagsTypeHandler::FlagInfo kKosmRayWaitSignalsInfo[] = {
	FLAG_INFO_ENTRY(KOSM_RAY_READABLE),
	FLAG_INFO_ENTRY(KOSM_RAY_WRITABLE),
	FLAG_INFO_ENTRY(KOSM_RAY_PEER_CLOSED),

	{ 0, NULL }
};

static const FlagsTypeHandler::FlagInfo kKosmRayReadWriteFlagsInfo[] = {
	FLAG_INFO_ENTRY(KOSM_RAY_PEEK),
	FLAG_INFO_ENTRY(KOSM_RAY_COPY_HANDLES),
	FLAG_INFO_ENTRY(B_RELATIVE_TIMEOUT),
	FLAG_INFO_ENTRY(B_ABSOLUTE_TIMEOUT),

	{ 0, NULL }
};

static const FlagsTypeHandler::FlagInfo kKosmHandleTypeInfo[] = {
	FLAG_INFO_ENTRY(KOSM_HANDLE_RAY),
	FLAG_INFO_ENTRY(KOSM_HANDLE_MUTEX),
	FLAG_INFO_ENTRY(KOSM_HANDLE_AREA),
	FLAG_INFO_ENTRY(KOSM_HANDLE_SEM),
	FLAG_INFO_ENTRY(KOSM_HANDLE_FD),

	{ 0, NULL }
};

static FlagsTypeHandler::FlagsList kKosmRayWaitSignals;
static FlagsTypeHandler::FlagsList kKosmRayReadWriteFlags;
static FlagsTypeHandler::FlagsList kKosmHandleTypes;


void
patch_kosm_ray()
{
	for (int i = 0; kKosmRayWaitSignalsInfo[i].name != NULL; i++)
		kKosmRayWaitSignals.push_back(kKosmRayWaitSignalsInfo[i]);
	for (int i = 0; kKosmRayReadWriteFlagsInfo[i].name != NULL; i++)
		kKosmRayReadWriteFlags.push_back(kKosmRayReadWriteFlagsInfo[i]);
	for (int i = 0; kKosmHandleTypeInfo[i].name != NULL; i++)
		kKosmHandleTypes.push_back(kKosmHandleTypeInfo[i]);

	Syscall *rayWrite = get_syscall("_kern_kosm_ray_write");
	rayWrite->GetParameter("flags")->SetHandler(
		new FlagsTypeHandler(kKosmRayReadWriteFlags));

	Syscall *rayWriteEtc = get_syscall("_kern_kosm_ray_write_etc");
	rayWriteEtc->GetParameter("flags")->SetHandler(
		new FlagsTypeHandler(kKosmRayReadWriteFlags));

	Syscall *rayRead = get_syscall("_kern_kosm_ray_read");
	rayRead->GetParameter("flags")->SetHandler(
		new FlagsTypeHandler(kKosmRayReadWriteFlags));

	Syscall *rayReadEtc = get_syscall("_kern_kosm_ray_read_etc");
	rayReadEtc->GetParameter("flags")->SetHandler(
		new FlagsTypeHandler(kKosmRayReadWriteFlags));

	Syscall *rayWait = get_syscall("_kern_kosm_ray_wait");
	rayWait->GetParameter("signals")->SetHandler(
		new FlagsTypeHandler(kKosmRayWaitSignals));
	rayWait->GetParameter("flags")->SetHandler(
		new FlagsTypeHandler(kKosmRayReadWriteFlags));
}
