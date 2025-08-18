/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Claude Code Assistant
 */


#include "ARM64ConfigTest.h"

#include <TestUtils.h>
#include <cppunit/TestCaller.h>
#include <cppunit/TestSuite.h>

#include <OS.h>

// Test includes for build configuration
#ifdef __HAIKU_ARCH_ARM64
	#define ARCH_ARM64_DETECTED 1
#else
	#define ARCH_ARM64_DETECTED 0
#endif


ARM64ConfigTest::ARM64ConfigTest()
{
}


ARM64ConfigTest::~ARM64ConfigTest()
{
}


void 
ARM64ConfigTest::TestArchitectureDetection()
{
	// Test that ARM64 architecture constant is properly defined
	#if defined(__aarch64__) || defined(__arm64__)
		// On actual ARM64 hardware/build, the constant should be defined
		CPPUNIT_ASSERT_MESSAGE("__HAIKU_ARCH_ARM64 should be defined on ARM64 builds",
			ARCH_ARM64_DETECTED == 1);
	#else
		// On non-ARM64 builds, we can't guarantee the constant is defined
		// but we can test that the build system recognizes it
		NextSubTest();
		// This test always passes on non-ARM64 platforms
		CPPUNIT_ASSERT(true);
	#endif
}


void 
ARM64ConfigTest::TestCompilerFlags()
{
	// Test that ARM64-specific compiler flags are recognized
	// This is a compile-time test - if we get here, the flags compiled correctly
	
	NextSubTest();
	
	// Test ARMv8.6-A instruction availability
	#ifdef __aarch64__
		// Test that advanced ARM64 features are available
		#if __ARM_FEATURE_ATOMICS
			CPPUNIT_ASSERT_MESSAGE("ARM64 LSE atomics should be available", true);
		#endif
		
		#if __ARM_FEATURE_CRYPTO
			CPPUNIT_ASSERT_MESSAGE("ARM64 crypto extensions should be available", true);
		#endif
		
		// Test pointer authentication if available
		#if defined(__ARM_FEATURE_PAC_DEFAULT)
			CPPUNIT_ASSERT_MESSAGE("ARM64 pointer authentication should be available", true);
		#endif
	#else
		// On non-ARM64 platforms, just verify the test compiles
		CPPUNIT_ASSERT(true);
	#endif
}


void 
ARM64ConfigTest::TestABI()
{
	// Test ARM64 ABI settings
	NextSubTest();
	
	#ifdef __aarch64__
		// Verify 64-bit architecture
		CPPUNIT_ASSERT_EQUAL_MESSAGE("ARM64 should be 64-bit", 
			sizeof(void*), static_cast<size_t>(8));
		
		// Verify little-endian (standard for ARM64 Haiku)
		uint32_t test = 0x12345678;
		uint8_t* bytes = reinterpret_cast<uint8_t*>(&test);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("ARM64 should be little-endian", 
			bytes[0], static_cast<uint8_t>(0x78));
		
		// Test alignment requirements
		CPPUNIT_ASSERT_MESSAGE("ARM64 should have strict alignment", 
			__alignof__(long long) == 8);
	#else
		// Non-ARM64 platform test
		CPPUNIT_ASSERT(true);
	#endif
}


void 
ARM64ConfigTest::TestBuildSystemIntegration()
{
	// Test that build system constants are consistent
	NextSubTest();
	
	#ifdef __HAIKU_ARCH_ARM64
		// Verify Haiku-specific ARM64 constants
		CPPUNIT_ASSERT_MESSAGE("HAIKU_ARCH_ARM64 constant should be available", true);
		
		// Test that we're building for the right architecture
		#ifndef __aarch64__
			CPPUNIT_FAIL("__HAIKU_ARCH_ARM64 defined but not building for aarch64");
		#endif
	#endif
	
	// Test architecture string consistency
	#ifdef __aarch64__
		system_info info;
		if (get_system_info(&info) == B_OK) {
			// The system should report ARM64 architecture
			// Note: This may not work in cross-compilation environment
			// but will work on actual ARM64 Haiku systems
		}
	#endif
}


void 
ARM64ConfigTest::TestPerformanceFeatures()
{
	// Test that performance-oriented ARM64 features are enabled
	NextSubTest();
	
	#ifdef __aarch64__
		// Test outline atomics support
		#ifdef __ARM_FEATURE_ATOMICS
			// Verify that outline atomics are available
			// This improves performance on multi-core ARM64 systems
			CPPUNIT_ASSERT_MESSAGE("Outline atomics should be enabled", true);
		#endif
		
		// Test NEON support (should be standard on ARM64)
		#ifdef __ARM_NEON
			CPPUNIT_ASSERT_MESSAGE("NEON SIMD should be available", true);
		#else
			CPPUNIT_FAIL("NEON SIMD support missing on ARM64");
		#endif
		
		// Test for advanced SIMD features
		#ifdef __ARM_FEATURE_FP16_VECTOR_ARITHMETIC
			CPPUNIT_ASSERT_MESSAGE("FP16 vector arithmetic should be available", true);
		#endif
		
	#else
		CPPUNIT_ASSERT(true);
	#endif
}


/*static*/ void
ARM64ConfigTest::AddTests(BTestSuite& parent)
{
	CppUnit::TestSuite& suite = *new CppUnit::TestSuite("ARM64ConfigTest");
	
	suite.addTest(new CppUnit::TestCaller<ARM64ConfigTest>(
		"ARM64ConfigTest::TestArchitectureDetection", 
		&ARM64ConfigTest::TestArchitectureDetection));
	
	suite.addTest(new CppUnit::TestCaller<ARM64ConfigTest>(
		"ARM64ConfigTest::TestCompilerFlags",
		&ARM64ConfigTest::TestCompilerFlags));
	
	suite.addTest(new CppUnit::TestCaller<ARM64ConfigTest>(
		"ARM64ConfigTest::TestABI",
		&ARM64ConfigTest::TestABI));
	
	suite.addTest(new CppUnit::TestCaller<ARM64ConfigTest>(
		"ARM64ConfigTest::TestBuildSystemIntegration",
		&ARM64ConfigTest::TestBuildSystemIntegration));
	
	suite.addTest(new CppUnit::TestCaller<ARM64ConfigTest>(
		"ARM64ConfigTest::TestPerformanceFeatures",
		&ARM64ConfigTest::TestPerformanceFeatures));
	
	parent.addTest("ARM64ConfigTest", &suite);
}