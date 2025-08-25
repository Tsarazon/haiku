#!/usr/bin/env python3
"""
Jamfile to CMake Converter for Haiku OS
Automatically converts Haiku Jamfiles to CMake build files for kit objects.
"""

import os
import re
import sys
from pathlib import Path

class JamfileToCMakeConverter:
    def __init__(self, base_dir="/home/ruslan/haiku/src/kits"):
        self.base_dir = Path(base_dir)
        self.kit_sources = {}
        self.private_headers = set()
        self.library_headers = set()
        
    def parse_jamfile(self, jamfile_path):
        """Parse a Jamfile and extract MergeObject information"""
        with open(jamfile_path, 'r') as f:
            content = f.read()
        
        kit_info = {
            'sources': [],
            'private_headers': [],
            'library_headers': [],
            'defines': [],
            'search_source': []
        }
        
        # Extract UsePrivateHeaders
        private_headers_match = re.search(r'UsePrivateHeaders\s+([^;]+);', content)
        if private_headers_match:
            headers = private_headers_match.group(1).strip().split()
            kit_info['private_headers'] = headers
            
        # Extract UseLibraryHeaders
        library_headers_match = re.search(r'UseLibraryHeaders\s+([^;]+);', content)
        if library_headers_match:
            headers = library_headers_match.group(1).strip().split()
            kit_info['library_headers'] = headers
            
        # Extract SEARCH_SOURCE
        search_source_matches = re.findall(r'SEARCH_SOURCE\s*\+=.*?\[(.*?)\]', content, re.DOTALL)
        for match in search_source_matches:
            # Extract directory from FDirName - handle both $(SUBDIR) and specific paths
            if '$(SUBDIR)' in match:
                # Extract relative directory after $(SUBDIR)
                subdir_match = re.search(r'\$\(SUBDIR\)\s+(\w+)(?:\s+(\w+))?', match)
                if subdir_match:
                    dir1 = subdir_match.group(1)
                    dir2 = subdir_match.group(2)
                    if dir2:
                        kit_info['search_source'].append(f"{dir1}/{dir2}")
                    else:
                        kit_info['search_source'].append(dir1)
            else:
                # Handle absolute paths
                dir_match = re.search(r'FDirName.*?(\w+)', match)
                if dir_match:
                    kit_info['search_source'].append(dir_match.group(1))
        
        # Extract MergeObject sources
        merge_match = re.search(r'MergeObject\s+<[^>]*>(\w+_kit)\.o\s*:\s*(.*?)\s*;', content, re.DOTALL)
        if merge_match:
            kit_name = merge_match.group(1)
            sources_text = merge_match.group(2)
            
            # Check if sources are referenced by variable (like $(sources) in locale)
            if '$(sources)' in sources_text or '$sources' in sources_text:
                # Extract sources from variable definition
                sources_var_match = re.search(r'local\s+sources\s*=\s*(.*?)\s*;', content, re.DOTALL)
                if sources_var_match:
                    sources_text = sources_var_match.group(1)
            
            # Clean up sources - remove comments and extra whitespace
            sources_lines = sources_text.split('\n')
            sources = []
            for line in sources_lines:
                # Remove comments
                line = re.sub(r'#.*$', '', line)
                line = line.strip()
                if line and not line.startswith('#'):
                    # Extract .cpp files
                    cpp_files = re.findall(r'(\w+\.cpp)', line)
                    sources.extend(cpp_files)
            
            kit_info['sources'] = sources
            return kit_name, kit_info
            
        return None, kit_info
    
    def scan_kits(self):
        """Scan all kit directories for Jamfiles"""
        kit_dirs = ['app', 'interface', 'locale', 'storage', 'support']
        
        for kit_dir in kit_dirs:
            jamfile_path = self.base_dir / kit_dir / 'Jamfile'
            if jamfile_path.exists():
                print(f"Processing {jamfile_path}")
                kit_name, kit_info = self.parse_jamfile(jamfile_path)
                if kit_name:
                    self.kit_sources[kit_name] = kit_info
                    self.kit_sources[kit_name]['kit_dir'] = kit_dir
                    
                    # Update global headers
                    self.private_headers.update(kit_info['private_headers'])
                    self.library_headers.update(kit_info['library_headers'])
    
    def generate_cmake_sources(self):
        """Generate CMake source lists for all kits"""
        cmake_content = []
        
        for kit_name, kit_info in self.kit_sources.items():
            kit_dir = kit_info['kit_dir']
            sources = kit_info['sources']
            search_dirs = kit_info['search_source']
            
            # Generate source list
            cmake_content.append(f"# {kit_name.replace('_', ' ').title()} sources (exactly from {kit_dir}/Jamfile)")
            cmake_content.append(f"set({kit_name.upper()}_SOURCES")
            
            # Collect all sources and deduplicate
            all_sources = []
            
            # Main sources
            for source in sources:
                all_sources.append(f"{kit_dir}/{source}")
            
            # Additional search directories
            for search_dir in search_dirs:
                search_path = self.base_dir / kit_dir / search_dir
                if search_path.exists():
                    for cpp_file in search_path.glob("*.cpp"):
                        rel_path = f"{kit_dir}/{search_dir}/{cpp_file.name}"
                        all_sources.append(rel_path)
                else:
                    # Check if it's already a relative path from kit root
                    alt_search_path = self.base_dir / kit_dir / search_dir.split('/')[-1]
                    if alt_search_path.exists():
                        for cpp_file in alt_search_path.glob("*.cpp"):
                            rel_path = f"{kit_dir}/{search_dir.split('/')[-1]}/{cpp_file.name}"
                            all_sources.append(rel_path)
            
            # Deduplicate and sort sources
            unique_sources = sorted(list(set(all_sources)))
            
            # Filter out sources that don't exist
            existing_sources = []
            for source in unique_sources:
                source_path = self.base_dir / source
                if source_path.exists():
                    existing_sources.append(source)
                else:
                    print(f"Warning: Source file not found: {source}")
            
            # Add to CMake content
            for source in existing_sources:
                cmake_content.append(f"    {source}")
            
            cmake_content.append(")")
            cmake_content.append("")
        
        return '\n'.join(cmake_content)
    
    def generate_cmake_includes(self):
        """Generate CMake include directories"""
        cmake_content = []
        
        cmake_content.append("# Include headers exactly like Jam build")
        cmake_content.append("include_directories(SYSTEM")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/os")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/os/kernel")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/os/support")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/os/app")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/os/interface")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/os/locale")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/os/storage")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/posix")
        
        # Add private headers
        for header in sorted(self.private_headers):
            cmake_content.append(f"    ${{CMAKE_SOURCE_DIR}}/../../headers/private/{header}")
        
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../headers/private")
        
        # Add library headers
        for header in sorted(self.library_headers):
            cmake_content.append(f"    ${{CMAKE_SOURCE_DIR}}/../../headers/libs/{header}")
        
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../build/config_headers")
        cmake_content.append("    ${CMAKE_SOURCE_DIR}/../../src/kits/tracker")
        cmake_content.append(")")
        cmake_content.append("")
        
        return '\n'.join(cmake_content)
    
    def generate_cmake_targets(self):
        """Generate CMake targets for all kits"""
        cmake_content = []
        
        for kit_name, kit_info in self.kit_sources.items():
            kit_dir = kit_info['kit_dir']
            
            cmake_content.append(f"# Create {kit_name}.o as object library (matches Jam MergeObject)")
            cmake_content.append(f"add_library({kit_name}_obj OBJECT ${{{kit_name.upper()}_SOURCES}})")
            cmake_content.append("")
            
            # Add kit-specific includes if needed
            search_dirs = kit_info['search_source']
            if search_dirs:
                cmake_content.append(f"# Add {kit_name} specific includes")
                cmake_content.append(f"target_include_directories({kit_name}_obj PRIVATE")
                for search_dir in search_dirs:
                    cmake_content.append(f"    ${{CMAKE_SOURCE_DIR}}/{kit_dir}/{search_dir}")
                cmake_content.append(")")
                cmake_content.append("")
            
            # Add compiler flags
            cmake_content.append(f"# Add compiler flags like Jam build (including optimization)")
            cmake_content.append(f"target_compile_definitions({kit_name}_obj PRIVATE ZSTD_ENABLED)")
            cmake_content.append(f"target_compile_options({kit_name}_obj PRIVATE -O2)")
            cmake_content.append("")
            
            # Dependencies
            cmake_content.append(f"# Link dependencies if found")
            cmake_content.append(f"if(ZLIB_FOUND)")
            cmake_content.append(f"    target_link_libraries({kit_name}_obj PRIVATE ZLIB::ZLIB)")
            cmake_content.append(f"endif()")
            cmake_content.append("")
            cmake_content.append(f"if(ICU_FOUND)")
            cmake_content.append(f"    target_include_directories({kit_name}_obj PRIVATE ${{ICU_INCLUDE_DIRS}})")
            cmake_content.append(f"    target_compile_options({kit_name}_obj PRIVATE ${{ICU_CFLAGS_OTHER}})")
            cmake_content.append(f"endif()")
            cmake_content.append("")
            
            # Properties
            cmake_content.append(f"# Set cross-compiler properties")
            cmake_content.append(f"set_target_properties({kit_name}_obj PROPERTIES")
            cmake_content.append(f"    POSITION_INDEPENDENT_CODE ON")
            cmake_content.append(f"    OUTPUT_NAME {kit_name}")
            cmake_content.append(f")")
            cmake_content.append("")
            
            # Custom command
            cmake_content.append(f"# Create {kit_name}.o exactly like Jam build (MergeObject)")
            cmake_content.append(f"add_custom_command(")
            cmake_content.append(f"    OUTPUT ${{CMAKE_BINARY_DIR}}/{kit_name}.o")
            cmake_content.append(f"    COMMAND ${{CMAKE_LINKER}} -r $<TARGET_OBJECTS:{kit_name}_obj> -o ${{CMAKE_BINARY_DIR}}/{kit_name}.o")
            cmake_content.append(f"    DEPENDS {kit_name}_obj")
            cmake_content.append(f'    COMMENT "Merging objects into {kit_name}.o (matching Jam MergeObject)"')
            cmake_content.append(f"    COMMAND_EXPAND_LISTS")
            cmake_content.append(f"    VERBATIM")
            cmake_content.append(f")")
            cmake_content.append("")
            cmake_content.append(f"add_custom_target({kit_name} ALL DEPENDS ${{CMAKE_BINARY_DIR}}/{kit_name}.o)")
            cmake_content.append("")
        
        return '\n'.join(cmake_content)
    
    def generate_complete_cmake(self):
        """Generate complete CMakeLists.txt"""
        cmake_content = []
        
        # Header
        cmake_content.append("# Haiku libbe.so - Complete CMake Build System")
        cmake_content.append("# This builds libbe.so with cross-compiler exactly matching Jam build")
        cmake_content.append("")
        cmake_content.append("cmake_minimum_required(VERSION 3.16)")
        cmake_content.append("")
        cmake_content.append("# Use cross-compiler BEFORE project() call")
        cmake_content.append("set(CMAKE_C_COMPILER /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc CACHE FILEPATH \"\" FORCE)")
        cmake_content.append("set(CMAKE_CXX_COMPILER /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++ CACHE FILEPATH \"\" FORCE)")
        cmake_content.append("")
        cmake_content.append("# Set sysroot for cross-compilation")
        cmake_content.append("set(CMAKE_SYSROOT /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot)")
        cmake_content.append("")
        cmake_content.append("# Configure cross-compilation paths")
        cmake_content.append("set(CMAKE_FIND_ROOT_PATH /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot)")
        cmake_content.append("set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)")
        cmake_content.append("set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)")
        cmake_content.append("set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)")
        cmake_content.append("")
        cmake_content.append("# Skip CMake's automatic compiler testing for cross-compilation")
        cmake_content.append("set(CMAKE_C_COMPILER_WORKS TRUE)")
        cmake_content.append("set(CMAKE_CXX_COMPILER_WORKS TRUE)")
        cmake_content.append("")
        cmake_content.append("project(libbe LANGUAGES C CXX)")
        cmake_content.append("")
        cmake_content.append("message(STATUS \"=== Building libbe.so with CMake Cross-Compiler ===\")")
        cmake_content.append("")
        cmake_content.append("# Add Haiku CMake modules to path")
        cmake_content.append("list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../build/cmake/modules)")
        cmake_content.append("")
        
        # Includes
        cmake_content.append(self.generate_cmake_includes())
        
        # Dependencies
        cmake_content.append("# Find optional dependencies using CMake (for cross-compilation these may not be found)")
        cmake_content.append("find_package(ZLIB QUIET)")
        cmake_content.append("find_package(PkgConfig QUIET)")
        cmake_content.append("if(PkgConfig_FOUND)")
        cmake_content.append("    pkg_check_modules(ICU QUIET icu-uc icu-io)")
        cmake_content.append("endif()")
        cmake_content.append("")
        
        # Sources
        cmake_content.append(self.generate_cmake_sources())
        
        # Targets
        cmake_content.append(self.generate_cmake_targets())
        
        # Footer
        cmake_content.append("message(STATUS \"libbe.so CMake cross-compilation configured:\")")
        cmake_content.append("message(STATUS \"  All kits: support_kit app_kit interface_kit locale_kit storage_kit\")")
        cmake_content.append("message(STATUS \"  Cross-compiler: x86_64-unknown-haiku-gcc\")")
        cmake_content.append("message(STATUS \"  Output: All kit objects for libbe.so\")")
        cmake_content.append("message(STATUS \"  Install to: cross-compiler sysroot\")")
        
        return '\n'.join(cmake_content)

def main():
    if len(sys.argv) > 1:
        base_dir = sys.argv[1]
    else:
        base_dir = "/home/ruslan/haiku/src/kits"
    
    print(f"Jamfile to CMake Converter")
    print(f"Base directory: {base_dir}")
    print("=" * 50)
    
    converter = JamfileToCMakeConverter(base_dir)
    converter.scan_kits()
    
    print(f"\nFound {len(converter.kit_sources)} kits:")
    for kit_name, kit_info in converter.kit_sources.items():
        print(f"  - {kit_name}: {len(kit_info['sources'])} sources in {kit_info['kit_dir']}/")
    
    print(f"\nPrivate headers: {sorted(converter.private_headers)}")
    print(f"Library headers: {sorted(converter.library_headers)}")
    
    # Generate complete CMakeLists.txt
    cmake_content = converter.generate_complete_cmake()
    
    output_file = Path(base_dir) / "CMakeLists_generated.txt"
    with open(output_file, 'w') as f:
        f.write(cmake_content)
    
    print(f"\nGenerated CMakeLists.txt written to: {output_file}")
    print("You can review and replace the existing CMakeLists.txt with this generated version.")

if __name__ == "__main__":
    main()