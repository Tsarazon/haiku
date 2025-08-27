#!/usr/bin/env python3
"""
Extract assembly offsets from generated assembly file.
This is equivalent to JAM's CreateAsmStructOffsetsHeader rule.
"""

import sys
import re

def extract_offsets(asm_file, output_file):
    """Extract offset definitions from assembly file and generate header."""
    
    with open(asm_file, 'r') as f:
        asm_content = f.read()
    
    # Pattern to match offset definitions in assembly
    # Looking for patterns like:
    # .set SIZEOF_type, value
    # .set OFFSET_field, value
    pattern = r'\.set\s+(\w+),\s*(-?\d+)'
    
    matches = re.findall(pattern, asm_content)
    
    with open(output_file, 'w') as f:
        f.write("/* Auto-generated file - do not edit */\n")
        f.write("#ifndef _SYSCALL_TYPES_SIZES_H\n")
        f.write("#define _SYSCALL_TYPES_SIZES_H\n\n")
        
        for name, value in matches:
            f.write(f"#define {name} {value}\n")
        
        f.write("\n#endif /* _SYSCALL_TYPES_SIZES_H */\n")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.s> <output.h>")
        sys.exit(1)
    
    extract_offsets(sys.argv[1], sys.argv[2])