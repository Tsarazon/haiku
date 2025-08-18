/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Claude Code Assistant
 */

#ifndef ARM64_CONFIG_TEST_H
#define ARM64_CONFIG_TEST_H


#include <TestCase.h>


class ARM64ConfigTest : public BTestCase {
public:
							ARM64ConfigTest();
	virtual					~ARM64ConfigTest();
	
	// Test methods
	void					TestArchitectureDetection();
	void					TestCompilerFlags();
	void					TestABI();
	void					TestBuildSystemIntegration();
	void					TestPerformanceFeatures();
	
	static	void			AddTests(BTestSuite& suite);
};


#endif	// ARM64_CONFIG_TEST_H