#!/usr/bin/env python3
"""
Haiku Resource Handler for Meson
Universal module for handling .rdef (Resource Definition Files) compilation
"""

import os
import sys
from pathlib import Path

class HaikuResourceHandler:
    """Handles Haiku .rdef resource file compilation"""
    
    def __init__(self, haiku_root=None):
        """Initialize the resource handler
        
        Args:
            haiku_root: Path to Haiku source root (auto-detected if None)
        """
        if haiku_root is None:
            haiku_root = os.environ.get('HAIKU_ROOT', '/home/ruslan/haiku')
        
        self.haiku_root = Path(haiku_root)
        self.rc_compiler = self._find_rc_compiler()
        
    def _find_rc_compiler(self):
        """Find the Haiku resource compiler (rc)"""
        # Try multiple possible locations for rc compiler
        possible_paths = [
            self.haiku_root / 'generated' / 'objects' / 'linux' / 'x86_64' / 'release' / 'tools' / 'rc' / 'rc',
            self.haiku_root / 'generated' / 'cross-tools-x86_64' / 'bin' / 'rc',
            self.haiku_root / 'buildtools' / 'bin' / 'rc'
        ]
        
        for path in possible_paths:
            if path.exists() and path.is_file():
                return str(path)
        
        # If not found, return default path (will cause error if not exists)
        return str(possible_paths[0])
    
    def get_rc_compiler(self):
        """Get the path to the rc compiler"""
        return self.rc_compiler
    
    def get_resource_command(self, input_file, output_file):
        """Get the command to compile a resource file
        
        Args:
            input_file: Path to .rdef input file
            output_file: Path to .rsrc output file
            
        Returns:
            List of command arguments for meson custom_target
        """
        return [self.rc_compiler, '-o', output_file, input_file]
    
    def create_meson_resource_target(self, target_name, rdef_file, rsrc_file=None):
        """Create Meson custom_target configuration for resource compilation
        
        Args:
            target_name: Name for the Meson target
            rdef_file: Input .rdef file path
            rsrc_file: Output .rsrc file path (auto-generated if None)
            
        Returns:
            Dictionary with Meson custom_target parameters
        """
        if rsrc_file is None:
            # Auto-generate output filename: input.rdef -> input.rsrc
            rdef_path = Path(rdef_file)
            rsrc_file = rdef_path.with_suffix('.rsrc').name
        
        return {
            'target_name': target_name,
            'input': rdef_file,
            'output': rsrc_file,
            'command': [self.rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],
            'build_by_default': True
        }
    
    def get_standard_haiku_resources(self):
        """Get standard Haiku libbe.so resource files
        
        Returns:
            List of resource target configurations
        """
        return [
            self.create_meson_resource_target(
                'libbe_version_rsrc',
                'libbe_version.rdef',
                'libbe_version.rsrc'
            ),
            self.create_meson_resource_target(
                'icons_rsrc', 
                'Icons.rdef',
                'Icons.rsrc'
            ),
            self.create_meson_resource_target(
                'country_flags_rsrc',
                'CountryFlags.rdef', 
                'CountryFlags.rsrc'
            ),
            self.create_meson_resource_target(
                'language_flags_rsrc',
                'LanguageFlags.rdef',
                'LanguageFlags.rsrc'
            )
        ]
    
    def validate_rc_compiler(self):
        """Validate that the rc compiler exists and is executable
        
        Returns:
            bool: True if rc compiler is valid
        """
        rc_path = Path(self.rc_compiler)
        return rc_path.exists() and rc_path.is_file()
    
    def get_meson_code_for_resources(self, resource_configs):
        """Generate Meson code for resource compilation
        
        Args:
            resource_configs: List of resource configurations
            
        Returns:
            String containing Meson build code
        """
        meson_code = []
        meson_code.append("# Resource compilation using ResourceHandler")
        meson_code.append(f"rc_compiler = '{self.rc_compiler}'")
        meson_code.append("")
        
        target_names = []
        for config in resource_configs:
            target_name = config['target_name']
            target_names.append(target_name)
            
            meson_code.append(f"{target_name} = custom_target('{target_name}',")
            meson_code.append(f"    input: '{config['input']}',")
            meson_code.append(f"    output: '{config['output']}',")
            meson_code.append(f"    command: [rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],")
            meson_code.append(f"    build_by_default: {str(config['build_by_default']).lower()}")
            meson_code.append(")")
            meson_code.append("")
        
        return '\n'.join(meson_code), target_names

def get_haiku_resource_handler():
    """Factory function to get a HaikuResourceHandler instance"""
    return HaikuResourceHandler()

def generate_meson_resources(resource_files, output_file=None):
    """Generate Meson resource compilation code
    
    Args:
        resource_files: List of .rdef files or resource configurations
        output_file: Optional file to write Meson code to
        
    Returns:
        Generated Meson code string
    """
    handler = HaikuResourceHandler()
    
    # Convert simple file list to full configurations
    if resource_files and isinstance(resource_files[0], str):
        # Simple file list
        configs = []
        for rdef_file in resource_files:
            rdef_path = Path(rdef_file)
            target_name = rdef_path.stem + '_rsrc'
            configs.append(handler.create_meson_resource_target(target_name, rdef_file))
        resource_files = configs
    
    meson_code, target_names = handler.get_meson_code_for_resources(resource_files)
    
    if output_file:
        with open(output_file, 'w') as f:
            f.write(meson_code)
        print(f"Generated Meson resource code in: {output_file}")
    
    return meson_code

# Command line interface
def main():
    """Command line interface for the resource handler"""
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python3 ResourceHandler.py validate - Check rc compiler")
        print("  python3 ResourceHandler.py standard - Generate standard libbe resources")
        print("  python3 ResourceHandler.py generate file1.rdef file2.rdef - Generate for specific files")
        sys.exit(1)
    
    command = sys.argv[1]
    handler = HaikuResourceHandler()
    
    if command == 'validate':
        if handler.validate_rc_compiler():
            print(f"✅ RC compiler found: {handler.get_rc_compiler()}")
            sys.exit(0)
        else:
            print(f"❌ RC compiler not found: {handler.get_rc_compiler()}")
            sys.exit(1)
            
    elif command == 'standard':
        configs = handler.get_standard_haiku_resources()
        meson_code, targets = handler.get_meson_code_for_resources(configs)
        print(meson_code)
        
    elif command == 'generate':
        if len(sys.argv) < 3:
            print("Error: No .rdef files specified")
            sys.exit(1)
        
        rdef_files = sys.argv[2:]
        meson_code = generate_meson_resources(rdef_files)
        print(meson_code)
        
    else:
        print(f"Unknown command: {command}")
        sys.exit(1)

if __name__ == '__main__':
    main()