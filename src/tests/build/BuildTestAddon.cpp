/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Claude Code Assistant
 */


#include <TestSuite.h>
#include <TestSuiteAddon.h>

#include "ARM64ConfigTest.h"


BTestSuite*
getTestSuite()
{
	BTestSuite* suite = new BTestSuite("BuildConfiguration");
	
	// Add ARM64 configuration tests
	ARM64ConfigTest::AddTests(*suite);
	
	return suite;
}