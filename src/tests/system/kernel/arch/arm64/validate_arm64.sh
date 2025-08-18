#!/bin/bash
#
# ARM64 Kernel Architecture Validation Script
# Copyright 2024 Haiku, Inc. All rights reserved.
# Distributed under the terms of the MIT License.
#

echo "=== ARM64 Kernel Architecture Validation ==="
echo "Validating ARM64 kernel components compilation..."
echo

# Check if we're in the right directory
if [ ! -f "arch_test.cpp" ]; then
    echo "ERROR: Must be run from the ARM64 test directory"
    exit 1
fi

# Check for required header files
echo "Checking ARM64 header files..."
HEADERS=(
    "../../../../headers/private/system/arch/aarch64/arch_config.h"
    "../../../../headers/private/kernel/arch/arm64/arch_cpu.h"
    "../../../../headers/private/kernel/arch/arm64/arch_thread_types.h"
)

for header in "${HEADERS[@]}"; do
    if [ -f "$header" ]; then
        echo "  ✓ Found: $(basename $header)"
    else
        echo "  ✗ Missing: $(basename $header)"
        echo "    Expected at: $header"
    fi
done

# Check for ARM64 kernel source files
echo
echo "Checking ARM64 kernel source files..."
ARM64_SOURCES=(
    "../../kernel/arch/arm64/arch_asm.S"
    "../../kernel/arch/arm64/arch_cpu.cpp"
    "../../kernel/arch/arm64/arch_thread.cpp"
    "../../kernel/arch/arm64/Jamfile"
)

for source in "${ARM64_SOURCES[@]}"; do
    if [ -f "$source" ]; then
        echo "  ✓ Found: $(basename $source)"
    else
        echo "  ✗ Missing: $(basename $source)"
    fi
done

# Try to compile the test (syntax check)
echo
echo "Performing syntax validation..."
echo "Compiling arch_test.cpp..."

# Use a basic compile test (adjust compiler path as needed)
if command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    echo "  Using cross-compiler: aarch64-linux-gnu-g++"
    aarch64-linux-gnu-g++ -std=c++17 -c arch_test.cpp -I../../../../headers/private/kernel -I../../../../headers/private/system -o /tmp/arm64_test.o 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "  ✓ ARM64 test compilation: PASSED"
        rm -f /tmp/arm64_test.o
    else
        echo "  ✗ ARM64 test compilation: FAILED"
    fi
elif command -v g++ >/dev/null 2>&1; then
    echo "  Using host compiler: g++ (architecture-independent check)"
    g++ -std=c++17 -c arch_test.cpp -I../../../../headers/private/kernel -I../../../../headers/private/system -DHOST_COMPILE_TEST -o /tmp/arm64_test.o 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "  ✓ Host test compilation: PASSED"
        rm -f /tmp/arm64_test.o
    else
        echo "  ✗ Host test compilation: FAILED"
    fi
else
    echo "  ! No suitable compiler found for validation"
fi

# Check Jamfile syntax
echo
echo "Validating Jamfile..."
if [ -f "Jamfile" ]; then
    echo "  ✓ Jamfile exists"
    
    # Basic Jamfile validation
    if grep -q "arm64_arch_test" Jamfile; then
        echo "  ✓ Test target defined in Jamfile"
    else
        echo "  ✗ Test target not found in Jamfile"
    fi
    
    if grep -q "UsePrivateKernelHeaders" Jamfile; then
        echo "  ✓ Kernel headers included in Jamfile"
    else
        echo "  ✗ Kernel headers not included in Jamfile"
    fi
else
    echo "  ✗ Jamfile missing"
fi

echo
echo "=== ARM64 Validation Summary ==="
echo "Check complete. Review any errors above."
echo "This test validates that ARM64 kernel components are"
echo "properly structured for compilation in the Haiku build system."
echo