/*
 * Copyright 2013, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef INSTALLER_DEFS_H
#define INSTALLER_DEFS_H


#include <SupportDefs.h>


static const uint32 MSG_STATUS_MESSAGE = 'iSTM';
static const uint32 MSG_INSTALL_FINISHED = 'iIFN';
static const uint32 MSG_RESET = 'iRSI';
static const uint32 MSG_WRITE_BOOT_SECTOR = 'iWBS';

static const char* const kPackagesDirectoryPath = "_packages_";
static const char* const kSourcesDirectoryPath = "_sources_";


#endif	// INSTALLER_DEFS_H
