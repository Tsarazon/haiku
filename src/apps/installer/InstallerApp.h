/*
 * Copyright 2005, Jérôme DUVAL. All rights reserved.
 * Copyright 2013, Jérôme DUVAL.
 * Distributed under the terms of the MIT License.
 */
#ifndef INSTALLER_APP_H
#define INSTALLER_APP_H

#include <Application.h>
#include <Catalog.h>
#include <SupportDefs.h>
#include <Window.h>

#include "InstallerWindow.h"


// Install progress messages
static const uint32 MSG_STATUS_MESSAGE = 'iSTM';
static const uint32 MSG_INSTALL_FINISHED = 'iIFN';
static const uint32 MSG_RESET = 'iRSI';
static const uint32 MSG_WRITE_BOOT_SECTOR = 'iWBS';

// Installation paths
static const char* const kPackagesDirectoryPath = "_packages_";
static const char* const kSourcesDirectoryPath = "_sources_";


class EULAWindow : public BWindow {
public:
								EULAWindow();
	virtual bool				QuitRequested();
};


class InstallerApp : public BApplication {
public:
								InstallerApp();

	virtual	void				MessageReceived(BMessage* message);

	virtual	void				ReadyToRun();
	virtual	void				Quit();

private:
			EULAWindow*			fEULAWindow;
};

#endif // INSTALLER_APP_H
