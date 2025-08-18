#!/bin/bash
# Script to embed resources into Haiku executable

set -e

if [ $# -ne 4 ]; then
    echo "Usage: $0 <input_exe> <input_rsrc> <output_exe> <xres_tool>"
    exit 1
fi

INPUT_EXE="$1"
INPUT_RSRC="$2" 
OUTPUT_EXE="$3"
XRES_TOOL="$4"

# Copy executable to output location
cp "$INPUT_EXE" "$OUTPUT_EXE"

# Make it executable
chmod +x "$OUTPUT_EXE"

# Embed resources
"$XRES_TOOL" -o "$OUTPUT_EXE" "$INPUT_RSRC"

echo "Successfully embedded resources into $OUTPUT_EXE"