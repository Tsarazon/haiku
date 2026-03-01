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

static const FlagsTypeHandler::FlagInfo kKosmMutexFlagsInfo[] = {
	FLAG_INFO_ENTRY(KOSM_MUTEX_SHARED),
	FLAG_INFO_ENTRY(KOSM_MUTEX_RECURSIVE),
	FLAG_INFO_ENTRY(KOSM_MUTEX_PRIO_INHERIT),

	{ 0, NULL }
};

static const FlagsTypeHandler::FlagInfo kKosmMutexAcquireFlagsInfo[] = {
	FLAG_INFO_ENTRY(B_RELATIVE_TIMEOUT),
	FLAG_INFO_ENTRY(B_ABSOLUTE_TIMEOUT),
	FLAG_INFO_ENTRY(B_TIMEOUT_REAL_TIME_BASE),

	{ 0, NULL }
};

static FlagsTypeHandler::FlagsList kKosmMutexFlags;
static FlagsTypeHandler::FlagsList kKosmMutexAcquireFlags;


void
patch_kosm_mutex()
{
	for (int i = 0; kKosmMutexFlagsInfo[i].name != NULL; i++)
		kKosmMutexFlags.push_back(kKosmMutexFlagsInfo[i]);
	for (int i = 0; kKosmMutexAcquireFlagsInfo[i].name != NULL; i++)
		kKosmMutexAcquireFlags.push_back(kKosmMutexAcquireFlagsInfo[i]);

	Syscall *createMutex = get_syscall("_kern_kosm_create_mutex");
	createMutex->GetParameter("flags")->SetHandler(
		new FlagsTypeHandler(kKosmMutexFlags));

	Syscall *acquireEtc = get_syscall("_kern_kosm_acquire_mutex_etc");
	acquireEtc->GetParameter("flags")->SetHandler(
		new FlagsTypeHandler(kKosmMutexAcquireFlags));
}
