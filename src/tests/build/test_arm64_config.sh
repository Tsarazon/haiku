#!/bin/bash

# ARM64 Build Configuration Test Script
# Copyright 2024 Haiku, Inc.
# Distributed under the terms of the MIT License.

echo "=== ARM64 Build Configuration Test ==="

# Check if we're in a Haiku build environment
if [ ! -f "../../../configure" ]; then
    echo "Error: This script should be run from the Haiku src/tests/build directory"
    echo "Current directory: $(pwd)"
    exit 1
fi

# Build configuration checks
echo "1. Checking build system configuration..."

# Check if ARM64 architecture is recognized in BuildConfig
if [ -f "../../../generated/build/BuildConfig" ]; then
    if grep -q "arm64\|aarch64" "../../../generated/build/BuildConfig"; then
        echo "✓ ARM64 architecture found in BuildConfig"
    else
        echo "✗ ARM64 architecture not found in BuildConfig"
    fi
else
    echo "! BuildConfig not found - run configure first"
fi

# Check if ARM64 cross-tools are available
echo "2. Checking ARM64 cross-compilation tools..."

if [ -d "../../../generated/cross-tools-aarch64" ]; then
    echo "✓ ARM64 cross-tools directory exists"
    
    if [ -x "../../../generated/cross-tools-aarch64/bin/aarch64-unknown-haiku-gcc" ]; then
        echo "✓ ARM64 GCC compiler found"
        
        # Test compiler flags
        echo "3. Testing ARM64 compiler flags..."
        GCC_PATH="../../../generated/cross-tools-aarch64/bin/aarch64-unknown-haiku-gcc"
        
        # Test march flag support
        if $GCC_PATH -march=armv8.6-a+crypto+lse2 -c -x c /dev/null -o /dev/null 2>/dev/null; then
            echo "✓ Advanced ARM64 march flags supported"
        else
            echo "✗ Advanced ARM64 march flags not supported"
        fi
        
        # Test other ARM64 flags
        if $GCC_PATH -mcmodel=small -c -x c /dev/null -o /dev/null 2>/dev/null; then
            echo "✓ ARM64 memory model flags supported"
        else
            echo "✗ ARM64 memory model flags not supported"  
        fi
        
        if $GCC_PATH -mbranch-protection=standard -c -x c /dev/null -o /dev/null 2>/dev/null; then
            echo "✓ ARM64 security flags supported"
        else
            echo "! ARM64 security flags may not be supported (older GCC version)"
        fi
        
    else
        echo "✗ ARM64 GCC compiler not found"
    fi
else
    echo "✗ ARM64 cross-tools not found"
    echo "  Run: ./configure --build-cross-tools aarch64 --cross-tools-source ../buildtools"
fi

# Check ArchitectureRules for ARM64 support
echo "4. Checking ArchitectureRules ARM64 configuration..."

if grep -q "case arm64" "../../../build/jam/ArchitectureRules"; then
    echo "✓ ARM64 case found in ArchitectureRules"
    
    if grep -A 20 "case arm64" "../../../build/jam/ArchitectureRules" | grep -q "armv8.6-a"; then
        echo "✓ Advanced ARM64 architecture flags configured"
    else
        echo "! Basic ARM64 architecture flags configured"
    fi
    
    if grep -A 20 "case arm64" "../../../build/jam/ArchitectureRules" | grep -q "mbranch-protection"; then
        echo "✓ ARM64 security features configured"
    else
        echo "! ARM64 security features not configured"
    fi
    
else
    echo "✗ ARM64 case not found in ArchitectureRules"
fi

# Check BuildSetup for ARM64 constants
echo "5. Checking BuildSetup ARM64 constants..."

if grep -q "HAIKU_ARCH_ARM64" "../../../build/jam/BuildSetup"; then
    echo "✓ HAIKU_ARCH_ARM64 constant defined"
else
    echo "✗ HAIKU_ARCH_ARM64 constant not found"
fi

echo "=== Test Complete ==="

# Try to build the test if possible
echo "6. Attempting to build ARM64 configuration test..."

if command -v jam >/dev/null 2>&1; then
    # Try to build the test
    if jam ARM64ConfigTestRunner 2>/dev/null; then
        echo "✓ ARM64 configuration test built successfully"
    else
        echo "! ARM64 configuration test build failed or not configured for this platform"
    fi
else
    echo "! Jam not found - cannot build test"
fi

echo "Done."