#!/usr/bin/env python3
"""
Jamfile to Meson Converter for Haiku OS - Fixed Modern Architecture
Converts Haiku Jamfiles to modern Meson build files following 2025 best practices.

Fixed issues:
- No cross-file string interpolation problems
- No dependency redefinition conflicts
- Uses modern meson.project_source_root() instead of deprecated meson.source_root()
- Proper path handling for Linux
- Cross-compiler validation
- Correct include_directories usage
"""

import os
import re
import sys
import json
from pathlib import Path
from typing import Dict, List, Set, Tuple, Optional, Any
from dataclasses import dataclass, field

@dataclass
class KitInfo:
    """Data class representing a Haiku kit build configuration"""
    name: str
    directory: str
    sources: List[str] = field(default_factory=list)
    private_headers: List[str] = field(default_factory=list)
    library_headers: List[str] = field(default_factory=list)
    search_directories: List[str] = field(default_factory=list)
    defines: List[str] = field(default_factory=list)
    build_features: List[str] = field(default_factory=list)
    dependencies: List[str] = field(default_factory=list)
    static_libraries: List[Dict[str, Any]] = field(default_factory=list)
    conditional_defines: Dict[str, str] = field(default_factory=dict)
    shared_sources: List[str] = field(default_factory=list)

class FixedJamfileToMesonConverter:
    """Fixed modern Jamfile to Meson converter following 2025 best practices"""
    
    def __init__(self, base_dir: str = "/home/ruslan/haiku/src/kits"):
        self.base_dir = Path(base_dir)
        self.haiku_root = self._find_haiku_root()
        self.kits: Dict[str, KitInfo] = {}
        self.global_private_headers: Set[str] = set()
        self.global_library_headers: Set[str] = set()
        
    def _find_haiku_root(self) -> Path:
        """Automatically find Haiku root directory"""
        current = self.base_dir
        while current.parent != current:  # Not at filesystem root
            if (current / 'Jamfile').exists() and 'haiku' in current.name.lower():
                return current
            current = current.parent
        # Fallback: assume standard structure
        return self.base_dir.parent.parent.parent

    def _get_cpu_family(self, arch: str) -> str:
        """Get CPU family from architecture string"""
        if arch == 'x86_64':
            return 'x86_64'
        elif arch.startswith('riscv'):
            return 'riscv64'
        elif arch.startswith('arm'):
            return 'aarch64'
        else:
            return arch.split('_')[0] if '_' in arch else arch

    def parse_jamfile(self, jamfile_path: Path) -> Optional[KitInfo]:
        """Parse a Jamfile and extract kit information using modern parsing"""
        try:
            with open(jamfile_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            print(f"Error reading {jamfile_path}: {e}")
            return None
        
        kit_directory = jamfile_path.parent.name
        
        # Extract MergeObject to determine kit name
        merge_match = re.search(r'MergeObject\s+<[^>]*>(\w+_kit)\.o\s*:\s*(.*?)(?::|;)', content, re.DOTALL)
        if not merge_match:
            print(f"No MergeObject found in {jamfile_path}")
            return None
            
        kit_name = merge_match.group(1)
        kit = KitInfo(name=kit_name, directory=kit_directory)
        
        # Extract sources
        sources_text = merge_match.group(2)
        
        # Handle variable references
        if '$(sources)' in sources_text or '$sources' in sources_text:
            sources_var_match = re.search(r'local\s+sources\s*=\s*(.*?)\s*;', content, re.DOTALL)
            if sources_var_match:
                sources_text = sources_var_match.group(1)
        
        # Parse sources
        kit.sources = self._extract_cpp_sources(sources_text)
        
        # Extract shared sources referenced in kit
        shared_sources = [src for src in kit.sources if '../shared/' in src]
        kit.shared_sources = shared_sources
        kit.sources = [src for src in kit.sources if '../shared/' not in src]
        
        # Extract headers
        kit.private_headers = self._extract_headers(content, r'UsePrivateHeaders\s+([^;]+);')
        kit.library_headers = self._extract_headers(content, r'UseLibraryHeaders\s+([^;]+);')
        
        # Extract search directories  
        kit.search_directories = self._extract_search_directories(content)
        
        # Extract build features
        kit.build_features = re.findall(r'UseBuildFeatureHeaders\s+(\w+)', content)
        
        # Extract defines
        kit.defines = re.findall(r'SubDirC\+\+Flags\s+-D(\w+)', content)
        
        # Extract conditional defines
        kit.conditional_defines = self._extract_conditional_defines(content)
        
        # Extract dependencies
        kit.dependencies = re.findall(r'<[^>]*>(\w+\.a)', sources_text)
        
        # Extract static libraries
        kit.static_libraries = self._extract_static_libraries(content)
        
        return kit
    
    def _extract_cpp_sources(self, sources_text: str) -> List[str]:
        """Extract .cpp files from sources text"""
        sources = []
        for line in sources_text.split('\n'):
            line = re.sub(r'#.*$', '', line).strip()
            if line and not line.startswith('#') and not line.startswith(':'):
                cpp_files = re.findall(r'(\S+\.cpp)', line)
                sources.extend(cpp_files)
        return sources
    
    def _extract_headers(self, content: str, pattern: str) -> List[str]:
        """Extract header list from Jamfile"""
        match = re.search(pattern, content)
        return match.group(1).strip().split() if match else []
    
    def _extract_search_directories(self, content: str) -> List[str]:
        """Extract SEARCH_SOURCE directories"""
        search_dirs = []
        
        # SEARCH_SOURCE patterns
        matches = re.findall(r'SEARCH_SOURCE\s*\+=\s*\[(.*?)\]', content, re.DOTALL)
        for match in matches:
            if '$(SUBDIR)' in match:
                subdir_match = re.search(r'\$\(SUBDIR\)\s+(\w+)(?:\s+(\w+))?', match)
                if subdir_match:
                    dir1 = subdir_match.group(1)
                    dir2 = subdir_match.group(2)
                    search_dirs.append(f"{dir1}/{dir2}" if dir2 else dir1)
        
        # UseHeaders patterns (like tracker)
        if re.search(r'UseHeaders.*tracker', content, re.IGNORECASE):
            search_dirs.append('tracker')
            
        return search_dirs
    
    def _extract_conditional_defines(self, content: str) -> Dict[str, str]:
        """Extract conditional compilation defines"""
        conditionals = {}
        matches = re.findall(
            r'if \$\((\w+)\).*?FDefines\s+(\w+)', 
            content, re.DOTALL
        )
        for condition, define in matches:
            conditionals[condition.lower()] = define
        return conditionals
    
    def _extract_static_libraries(self, content: str) -> List[Dict[str, Any]]:
        """Extract StaticLibrary definitions"""
        libraries = []
        matches = re.findall(
            r'StaticLibrary\s+\[[^\]]+(\w+\.a)\]\s*:\s*(.*?)\s*;', 
            content, re.DOTALL
        )
        for lib_name, lib_sources in matches:
            libraries.append({
                'name': lib_name.replace('.a', ''),
                'sources': self._extract_cpp_sources(lib_sources)
            })
        return libraries

    def scan_kits(self):
        """Scan all kit directories for Jamfiles"""
        print(f"Scanning for kits in {self.base_dir}")
        
        for item in self.base_dir.iterdir():
            if not item.is_dir():
                continue
                
            jamfile = item / 'Jamfile'
            if not jamfile.exists():
                continue
                
            print(f"Processing {jamfile}")
            kit = self.parse_jamfile(jamfile)
            if kit:
                self.kits[kit.name] = kit
                self.global_private_headers.update(kit.private_headers)
                self.global_library_headers.update(kit.library_headers)
                print(f"  - Found kit: {kit.name} ({len(kit.sources)} sources)")

    def generate_cross_file(self, arch: str = 'x86_64') -> str:
        """Generate proper Meson cross-compilation file - FIXED"""
        target_triple = f"{arch}-unknown-haiku"
        cross_tools_dir = self.haiku_root / f"generated/cross-tools-{arch}"
        cpu_family = self._get_cpu_family(arch)
        
        return f"""# Haiku {arch} cross-compilation file for Meson
# Auto-generated by fixed modern Jamfile converter

[constants]
arch = '{arch}'
target = '{target_triple}'

[binaries]
c = '{cross_tools_dir}/bin/{target_triple}-gcc'
cpp = '{cross_tools_dir}/bin/{target_triple}-g++'
ar = '{cross_tools_dir}/bin/{target_triple}-ar'
strip = '{cross_tools_dir}/bin/{target_triple}-strip'
ld = '{cross_tools_dir}/bin/{target_triple}-ld'
objcopy = '{cross_tools_dir}/bin/{target_triple}-objcopy'
objdump = '{cross_tools_dir}/bin/{target_triple}-objdump'
nm = '{cross_tools_dir}/bin/{target_triple}-nm'
ranlib = '{cross_tools_dir}/bin/{target_triple}-ranlib'
readelf = '{cross_tools_dir}/bin/{target_triple}-readelf'
pkg-config = '{cross_tools_dir}/bin/{target_triple}-pkg-config'

[built-in options]
c_std = 'gnu11'
cpp_std = 'gnu++17'
warning_level = '1'
optimization = '2'
debug = false
default_library = 'static'
strip = true
b_lto = false
b_pgo = 'off'

[properties]
sys_root = '{cross_tools_dir}/sysroot'
pkg_config_libdir = '{cross_tools_dir}/sysroot/boot/system/develop/lib/{arch}/pkgconfig'

# Haiku-specific properties
haiku_arch = '{arch}'
haiku_target = '{target_triple}'
haiku_generated = '{self.haiku_root}/generated'

[host_machine]
system = 'haiku'
cpu_family = '{cpu_family}'
cpu = '{arch}'
endian = 'little'

[target_machine]
system = 'haiku'
cpu_family = '{cpu_family}'
cpu = '{arch}'
endian = 'little'
"""

    def generate_meson_options(self) -> str:
        """Generate modern meson_options.txt"""
        return """# Haiku build system options

option('haiku_arch', 
       type: 'string', 
       value: 'x86_64',
       description: 'Target Haiku architecture')

option('haiku_target',
       type: 'string', 
       value: 'x86_64-unknown-haiku',
       description: 'Target triplet for cross-compilation')

# Development options (matching Jam conditional defines)
option('run_without_registrar', 
       type: 'boolean', 
       value: false,
       description: 'Development: run without registrar')

option('run_without_app_server', 
       type: 'boolean', 
       value: false,
       description: 'Development: run without app server')

# Feature options
option('enable_zstd', 
       type: 'boolean', 
       value: true,
       description: 'Enable ZSTD compression support')

option('kit_output_format',
       type: 'combo',
       choices: ['object', 'static', 'both'],
       value: 'object',
       description: 'Kit output format (object files, static libs, or both)')
"""

    def generate_dependency_wraps(self) -> Dict[str, str]:
        """Generate modern wrap files with proper versions"""
        wraps = {}
        
        # ICU wrap - modern version
        wraps['icu.wrap'] = """[wrap-file]
source_url = https://github.com/unicode-org/icu/releases/download/release-74-2/icu4c-74_2-src.tgz
source_filename = icu4c-74_2-src.tgz
source_hash = 68db082212a96d6f53e35d60f47d38b962e9f9d207a74cfac78029ae8ff5e08c
directory = icu
patch_directory = icu

[provide]
dependency_names = icu-uc, icu-i18n, icu-io, icu-data
"""

        # zlib wrap - stable version
        wraps['zlib.wrap'] = """[wrap-file]
source_url = https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
source_filename = zlib-1.3.1.tar.gz
source_hash = 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
directory = zlib-1.3.1

[provide]
dependency_names = zlib
"""

        # zstd wrap - latest stable
        wraps['zstd.wrap'] = """[wrap-file]
source_url = https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
source_filename = zstd-1.5.6.tar.gz
source_hash = 8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1
directory = zstd-1.5.6

[provide]
dependency_names = libzstd
"""
        
        return wraps

    def generate_kit_meson_build(self, kit: KitInfo) -> str:
        """Generate modern meson.build for individual kit - FIXED"""
        lines = []
        
        # Header
        lines.extend([
            f"# Haiku {kit.name.replace('_', ' ').title()}",
            f"# Modern Meson build - converted from {kit.directory}/Jamfile",
            ""
        ])
        
        # Sources
        lines.append(f"{kit.name}_sources = files([")
        
        # Main sources
        for source in sorted(kit.sources):
            lines.append(f"  '{source}',")
        
        # Sources from search directories
        for search_dir in kit.search_directories:
            search_path = self.base_dir / kit.directory / search_dir
            if search_path.exists():
                for cpp_file in sorted(search_path.glob("*.cpp")):
                    lines.append(f"  '{search_dir}/{cpp_file.name}',")
        
        # Shared sources
        for shared_src in kit.shared_sources:
            lines.append(f"  '{shared_src}',")
            
        lines.extend(["])", ""])
        
        # Include directories (kit-specific)
        if kit.search_directories:
            lines.append(f"{kit.name}_inc = include_directories([")
            for search_dir in kit.search_directories:
                lines.append(f"  '{search_dir}',")
            lines.extend(["])", ""])
        
        # Kit dependencies - FIXED: reference global deps, don't redefine
        kit_deps = []
        if 'zlib' in kit.build_features:
            kit_deps.append("zlib_dep")
        if 'zstd' in kit.build_features:
            kit_deps.append("zstd_dep")  
        if 'icu' in kit.build_features:
            kit_deps.append("icu_dep")
            
        if kit_deps:
            lines.append(f"# Kit-specific dependencies (reference globals)")
            lines.append(f"{kit.name}_deps = [")
            for dep in kit_deps:
                lines.append(f"  {dep},")
            lines.append("]")
            lines.append("")
        
        # Compile arguments (target-specific, not global)
        compile_args = []
        
        # Base defines
        compile_args.append("'-DHAIKU_TARGET_PLATFORM_HAIKU'")
        if 'zstd' in kit.build_features:
            compile_args.append("'-DZSTD_ENABLED'")
        for define in kit.defines:
            compile_args.append(f"'-D{define}'")
        
        # Conditional defines (modern approach)
        conditional_args = []
        for condition, define in kit.conditional_defines.items():
            conditional_args.append(f"get_option('{condition}') ? ['-D{define}'] : []")
        
        if compile_args or conditional_args:
            lines.append(f"# Compile arguments for {kit.name}")
            lines.append(f"{kit.name}_args = [")
            for arg in compile_args:
                lines.append(f"  {arg},")
            lines.append("]")
            
            if conditional_args:
                lines.append(f"# Add conditional arguments")
                for cond_arg in conditional_args:
                    lines.append(f"{kit.name}_args += {cond_arg}")
            lines.append("")
        
        # Modern static library (not object library + custom target)
        lines.append(f"# {kit.name.replace('_', ' ').title()} library")
        
        lib_args = [f"{kit.name}_sources"]
        
        if kit.search_directories:
            lib_args.append(f"include_directories: [{kit.name}_inc, haiku_private_inc]")
        else:
            lib_args.append("include_directories: [haiku_private_inc]")
            
        if kit_deps:
            lib_args.append(f"dependencies: {kit.name}_deps")
            
        if compile_args or conditional_args:
            lib_args.append(f"cpp_args: {kit.name}_args + common_cpp_args")
        else:
            lib_args.append("cpp_args: common_cpp_args")
        
        lib_args.extend([
            "pic: true",
            "install: false"
        ])
        
        lib_args_str = ',\n  '.join(lib_args)
        lines.extend([
            f"{kit.name}_lib = static_library('{kit.name}',",
            f"  {lib_args_str}",
            ")",
            ""
        ])
        
        # Object file extraction for Jam compatibility
        if_object_format = "get_option('kit_output_format')"
        lines.extend([
            f"# Create .o file for Jam compatibility (conditional)",
            f"if {if_object_format} == 'object' or {if_object_format} == 'both'",
            f"  {kit.name}_obj = custom_target('{kit.name}.o',",
            f"    input: {kit.name}_lib.extract_all_objects(recursive: true),",
            f"    output: '{kit.name}.o',",
            f"    command: [cross_ld, '-r', '@INPUT@', '-o', '@OUTPUT@'],",
            f"    build_by_default: true,",
            f"    install: true,",
            f"    install_dir: get_option('libdir') / 'haiku-kits' / haiku_arch",
            f"  )",
            f"endif",
            ""
        ])
        
        # Static libraries within kit
        for static_lib in kit.static_libraries:
            lib_name = static_lib['name']
            lib_sources = static_lib['sources']
            
            lines.extend([
                f"# {lib_name} static library",
                f"{lib_name}_lib = static_library('{lib_name}',",
                f"  files([",
            ])
            for source in lib_sources:
                lines.append(f"    '{source}',")
            lines.extend([
                "  ]),",
                f"  include_directories: [haiku_private_inc],",
                f"  cpp_args: common_cpp_args,",
                "  install: false",
                ")",
                ""
            ])
        
        # Export for use by other kits
        lines.extend([
            f"# Export {kit.name} for other components",
            f"{kit.name}_dep = declare_dependency(",
            f"  link_with: {kit.name}_lib,",
            f"  include_directories: [haiku_private_inc]",
            ")",
            ""
        ])
        
        return '\n'.join(lines)

    def generate_main_meson_build(self) -> str:
        """Generate modern main meson.build - FIXED"""
        lines = []
        
        # Project declaration
        lines.extend([
            "# Haiku Kits - Fixed Modern Meson Build System",
            "# Auto-generated with best practices for 2025",
            "",
            "project('haiku_kits',",
            "  ['c', 'cpp'],",
            "  version: '1.0.0',",
            "  license: 'MIT',",
            "  meson_version: '>=1.0.0',",
            "  default_options: [",
            "    'warning_level=1',",
            "    'optimization=2',", 
            "    'cpp_std=gnu++17',",
            "    'c_std=gnu11',",
            "    'default_library=static',",
            "    'strip=true'",
            "  ]",
            ")",
            ""
        ])
        
        # Build configuration
        lines.extend([
            "# Build configuration",
            "haiku_arch = get_option('haiku_arch')",
            "haiku_target = get_option('haiku_target')",
            "",
            "message('Building Haiku kits for @0@ (@1@)'.format(haiku_arch, haiku_target))",
            ""
        ])
        
        # Cross-compiler setup - FIXED
        lines.extend([
            "# Cross-compiler setup with validation",
            "cross_cc = meson.get_compiler('c')",
            "cross_cxx = meson.get_compiler('cpp')",
            "cross_ld = find_program('ld', required: true)",
            "",
            "# Validate cross-compiler is working",
            "if not cross_cc.has_header('stdio.h')",
            "  error('Cross-compiler cannot find basic headers - check cross-file')",
            "endif",
            ""
        ])
        
        # Global include directories - FIXED: use meson.project_source_root()
        lines.extend([
            "# Global include directories - FIXED path handling",
            "fs = import('fs')",
            "haiku_root = meson.project_source_root() / '..' / '..' / '..'",
            "",
            "# Validate haiku root exists",
            "if not fs.exists(haiku_root / 'headers')",
            "  error('Haiku headers not found at: ' + haiku_root)",
            "endif",
            "",
            "haiku_public_inc = include_directories([",
            "  haiku_root + '/headers',",
            "  haiku_root + '/headers/os',",
            "  haiku_root + '/headers/os/kernel',",
            "  haiku_root + '/headers/os/support',",
            "  haiku_root + '/headers/os/app',",
            "  haiku_root + '/headers/os/interface',",
            "  haiku_root + '/headers/os/locale',",
            "  haiku_root + '/headers/os/storage',",
            "  haiku_root + '/headers/posix',",
            "])",
            "",
        ])
        
        # Private headers - FIXED: separate system from non-system
        lines.append("haiku_private_inc = include_directories([")
        for header in sorted(self.global_private_headers):
            lines.append(f"  haiku_root + '/headers/private/{header}',")
        lines.append("  haiku_root + '/headers/private',")
        
        # Library headers  
        for header in sorted(self.global_library_headers):
            lines.append(f"  haiku_root + '/headers/libs/{header}',")
            
        lines.extend([
            "  haiku_root + '/build/config_headers',",
            "  haiku_root + '/src/kits/tracker',",
            "])",
            ""
        ])
        
        # Common compile arguments (target-specific)
        lines.extend([
            "# Common compile arguments (applied per-target, not globally)",
            "common_cpp_args = [",
            "  '-fno-strict-aliasing',",
            "]",
            "",
        ])
        
        # Dependencies (modern approach with fallbacks)
        lines.extend([
            "# Global dependencies with subproject fallbacks",
            "zlib_dep = dependency('zlib',",
            "  required: false,",
            "  fallback: ['zlib', 'zlib_dep']",
            ")",
            "",
            "zstd_dep = dependency('libzstd',", 
            "  required: get_option('enable_zstd'),",
            "  fallback: ['zstd', 'zstd_dep']",
            ")",
            "",
            "icu_dep = dependency('icu-uc',",
            "  required: false,",
            "  fallback: ['icu', 'icu_uc_dep']",
            ")",
            ""
        ])
        
        # Kit subdirectories
        lines.extend([
            "# Kit subdirectories (order matters for dependencies)",
        ])
        
        # Sort kits by dependencies (simple heuristic)
        kit_order = sorted(self.kits.keys())
        for kit_name in kit_order:
            kit = self.kits[kit_name]
            lines.append(f"subdir('{kit.directory}')  # {kit.name}")
        
        lines.extend([
            "",
            "# Build summary",
            "summary_info = {",
            "  'Architecture': haiku_arch,",
            "  'Target': haiku_target,",
            "  'Source root': meson.project_source_root(),",
        ])
        
        for kit_name in kit_order:
            kit = self.kits[kit_name]
            lines.append(f"  '{kit.name.replace('_', ' ').title()}': '{len(kit.sources)} sources',")
        
        lines.extend([
            "  'Zlib': zlib_dep.found(),",
            "  'Zstd': zstd_dep.found(),", 
            "  'ICU': icu_dep.found(),",
            "}",
            "",
            "summary(summary_info, section: 'Haiku Kits Build')",
        ])
        
        return '\n'.join(lines)

    def generate_all_files(self, output_dir: str = None) -> Dict[str, str]:
        """Generate all modern Meson files"""
        if output_dir is None:
            output_dir = str(self.base_dir)
            
        output_path = Path(output_dir)
        files = {}
        
        # Main build file
        files[str(output_path / 'meson.build')] = self.generate_main_meson_build()
        
        # Options file
        files[str(output_path / 'meson_options.txt')] = self.generate_meson_options()
        
        # Cross-compilation files
        for arch in ['x86_64', 'riscv64', 'arm64']:
            cross_file = output_path / f'cross-{arch}.ini'
            files[str(cross_file)] = self.generate_cross_file(arch)
        
        # Wrap files
        subprojects_dir = output_path / 'subprojects'
        for wrap_name, wrap_content in self.generate_dependency_wraps().items():
            files[str(subprojects_dir / wrap_name)] = wrap_content
        
        # Individual kit build files
        for kit_name, kit in self.kits.items():
            kit_file = output_path / kit.directory / 'meson.build'
            files[str(kit_file)] = self.generate_kit_meson_build(kit)
        
        return files

    def write_files(self, files_dict: Dict[str, str], force: bool = False) -> Tuple[List[str], List[str]]:
        """Write files with confirmation"""
        written = []
        skipped = []
        
        for file_path, content in files_dict.items():
            path = Path(file_path)
            path.parent.mkdir(parents=True, exist_ok=True)
            
            if path.exists() and not force:
                choice = input(f"Overwrite {path.name}? [y/N]: ").strip().lower()
                if choice not in ['y', 'yes']:
                    skipped.append(str(path))
                    continue
            
            try:
                with open(path, 'w', encoding='utf-8') as f:
                    f.write(content)
                written.append(str(path))
                print(f"✓ Generated: {path}")
            except Exception as e:
                print(f"✗ Error writing {path}: {e}")
        
        return written, skipped

def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Fixed modern Jamfile to Meson converter for Haiku OS',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --base-dir /path/to/haiku/src/kits
  %(prog)s --dry-run --arch riscv64
  %(prog)s --force --output-dir /tmp/meson-build
        """
    )
    
    parser.add_argument('--base-dir', 
                       default='/home/ruslan/haiku/src/kits',
                       help='Base directory containing kit Jamfiles')
    parser.add_argument('--output-dir', 
                       help='Output directory (default: same as base-dir)')
    parser.add_argument('--arch', 
                       default='x86_64',
                       choices=['x86_64', 'riscv64', 'arm64'],
                       help='Primary target architecture')
    parser.add_argument('--force', 
                       action='store_true',
                       help='Force overwrite existing files')
    parser.add_argument('--dry-run', 
                       action='store_true',
                       help='Show what would be generated')
    
    args = parser.parse_args()
    
    print("🔧 Fixed Modern Jamfile to Meson Converter for Haiku OS")
    print(f"📁 Base directory: {args.base_dir}")
    print(f"🎯 Target architecture: {args.arch}")
    print("=" * 60)
    
    converter = FixedJamfileToMesonConverter(args.base_dir)
    converter.scan_kits()
    
    print(f"\n📊 Conversion Summary:")
    print(f"  - Discovered kits: {len(converter.kits)}")
    
    total_sources = sum(len(kit.sources) for kit in converter.kits.values())
    print(f"  - Total sources: {total_sources}")
    print(f"  - Private headers: {len(converter.global_private_headers)}")
    print(f"  - Library headers: {len(converter.global_library_headers)}")
    
    print(f"\n📋 Kit Details:")
    for kit_name, kit in sorted(converter.kits.items()):
        features = []
        if kit.build_features:
            features.extend(kit.build_features)
        if kit.conditional_defines:
            features.extend(kit.conditional_defines.keys())
        feature_str = f" [{', '.join(features)}]" if features else ""
        
        print(f"  - {kit.name:<15}: {len(kit.sources):>3} sources in {kit.directory}/{feature_str}")
    
    # Generate files
    output_dir = args.output_dir or args.base_dir
    files_dict = converter.generate_all_files(output_dir)
    
    if args.dry_run:
        print(f"\n🔍 [DRY RUN] Would generate {len(files_dict)} files:")
        for file_path in sorted(files_dict.keys()):
            path = Path(file_path)
            size = len(files_dict[file_path])
            print(f"  - {path.name:<20} ({size:>4} bytes) -> {path.parent}")
    else:
        print(f"\n🚀 Generating {len(files_dict)} fixed modern Meson files...")
        written, skipped = converter.write_files(files_dict, args.force)
        
        print(f"\n✅ Generation Complete:")
        print(f"  - Generated: {len(written)} files")
        print(f"  - Skipped: {len(skipped)} files")
        
        if written:
            print(f"\n🏗️  Build Instructions:")
            print(f"  # Configure")
            print(f"  meson setup builddir --cross-file cross-{args.arch}.ini")
            print(f"  # Build")  
            print(f"  meson compile -C builddir")
            print(f"  # Reconfigure architecture")
            print(f"  meson configure builddir -Dhaiku_arch=riscv64")
            print(f"  # Install (if needed)")
            print(f"  meson install -C builddir")

if __name__ == "__main__":
    main()