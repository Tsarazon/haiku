#!/bin/bash

# Build Haiku cross-compilation tools
set -e

ARCH=${1:-x86_64}
JOBS=${2:-$(nproc)}

echo "Building cross-tools for architecture: $ARCH with $JOBS parallel jobs"

# Set paths
HAIKU_ROOT=$(pwd)
BUILDTOOLS_DIR="$HAIKU_ROOT/buildtools"
OUTPUT_DIR="$HAIKU_ROOT/generated/cross-tools-$ARCH"

# Create output directory
mkdir -p "$OUTPUT_DIR"
mkdir -p "$HAIKU_ROOT/generated/cross-tools-$ARCH-build"

echo "Buildtools directory: $BUILDTOOLS_DIR"
echo "Output directory: $OUTPUT_DIR"

cd "$HAIKU_ROOT/generated/cross-tools-$ARCH-build"

# Configure buildtools
echo "Configuring buildtools..."
"$BUILDTOOLS_DIR/configure" \
    --prefix="$OUTPUT_DIR" \
    --target="$ARCH-unknown-haiku" \
    --disable-nls \
    --disable-shared \
    --enable-languages=c,c++ \
    --with-gnu-as \
    --with-gnu-ld

echo "Building cross-tools (this may take a while)..."
make -j$JOBS

echo "Installing cross-tools..."
make install

echo "Cross-tools built successfully!"
echo "Compiler location: $OUTPUT_DIR/bin/$ARCH-unknown-haiku-gcc"

# Test the compiler
if [ -f "$OUTPUT_DIR/bin/$ARCH-unknown-haiku-gcc" ]; then
    echo "Testing compiler..."
    "$OUTPUT_DIR/bin/$ARCH-unknown-haiku-gcc" --version
else
    echo "ERROR: Cross-compiler not found!"
    exit 1
fi