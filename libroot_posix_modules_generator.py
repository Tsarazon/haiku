#!/usr/bin/env python3
"""
Haiku libroot POSIX Modules Meson Generator with Custom Modules Integration
Automatically creates meson.build files for all POSIX modules in libroot
Integrates custom Meson modules for build features, resources, and cross-tools detection.
Based on jamfile_to_meson_converter_v4_enhanced.py patterns
"""

import os
import sys
from pathlib import Path
from typing import Dict, List, Set, Optional, Tuple
import re

# Import custom Meson modules
sys.path.insert(0, str(Path(__file__).parent / 'build' / 'meson' / 'modules'))
try:
    import detect_build_features as detector
    import BuildFeatures as build_features
    import HaikuCommon as haiku_common
    import ResourceHandler as resource_handler
    CUSTOM_MODULES_AVAILABLE = True
    print("✅ Custom Meson modules loaded successfully")
except ImportError as e:
    print(f"⚠️  Custom modules not found: {e}")
    print("   Continuing with basic functionality...")
    CUSTOM_MODULES_AVAILABLE = False

class LibrootPosixModulesGenerator:
    """Generate meson.build files for all POSIX modules in libroot with custom modules integration"""
    
    def __init__(self, libroot_path: str = "/home/ruslan/haiku/src/system/libroot", architecture: str = 'x86_64'):
        self.libroot_path = Path(libroot_path)
        self.posix_path = self.libroot_path / "posix"
        self.modules_info: Dict[str, Dict] = {}
        self.architecture = architecture
        
        # Initialize custom modules integration
        self.build_features = {}
        self.resource_handler = None
        if CUSTOM_MODULES_AVAILABLE:
            self._initialize_custom_modules()
        
        # Critical modules that must be included (matching Jam build)
        self.critical_modules = {
            'malloc', 'string', 'stdio', 'stdlib', 'pthread', 'signal', 
            'time', 'sys', 'unistd', 'wchar', 'locale', 'crypt',
            'glibc', 'musl'
        }
        
        # Standard compiler arguments for all POSIX modules
        self.standard_c_args = [
            '-fno-builtin',
            '-fno-strict-aliasing', 
            '-D_GNU_SOURCE',
            '-D_DEFAULT_SOURCE',
            '-DBUILDING_LIBROOT=1',
            # Architecture flag will be added dynamically
        ]
        
        self.standard_cpp_args = self.standard_c_args.copy()
    
    def _initialize_custom_modules(self) -> None:
        """Initialize custom Meson modules integration"""
        try:
            # Initialize build features detection
            print(f"🔧 Initializing build features for {self.architecture}...")
            self.build_features = haiku_common.get_build_feature_dependencies(self.architecture)
            
            # Initialize resource handler
            self.resource_handler = resource_handler.HaikuResourceHandler()
            
            # Ensure cross-tools compatibility
            if detector.ensure_cross_tools_compatibility(self.architecture):
                print(f"✅ Cross-tools compatibility ensured for {self.architecture}")
            else:
                print(f"⚠️  Cross-tools compatibility issues for {self.architecture}")
                
            print(f"📦 Available build features: {list(self.build_features.keys())}")
            
        except Exception as e:
            print(f"⚠️  Error initializing custom modules: {e}")
            print("   Continuing with basic functionality...")
    
    def discover_posix_modules(self) -> Dict[str, Dict]:
        """Discover all POSIX modules and their source files"""
        print(f"🔍 Discovering POSIX modules in {self.posix_path}")
        
        if not self.posix_path.exists():
            raise FileNotFoundError(f"POSIX directory not found: {self.posix_path}")
        
        modules = {}
        skipped_items = []
        
        # Scan all directories in POSIX
        try:
            for item in self.posix_path.iterdir():
                # Skip non-directories and special directories
                if not item.is_dir() or item.name in ['arch', 'CVS', '.git', '__pycache__']:
                    skipped_items.append(item.name)
                    continue
                    
                module_name = item.name
                print(f"  📁 Found module: {module_name}")
                
                module_info = {
                    'name': module_name,
                    'path': item,
                    'sources': [],
                    'arch_sources': [],
                    'jamfile_exists': (item / 'Jamfile').exists(),
                    'cmake_exists': (item / 'CMakeLists.txt').exists(),
                    'meson_exists': (item / 'meson.build').exists(),
                    'is_critical': module_name in self.critical_modules,
                    'special_flags': [],
                    'special_includes': []
                }
                
                try:
                    # Discover source files
                    module_info['sources'] = self._discover_sources(item)
                    
                    # Check for architecture-specific sources
                    arch_dir = item / 'arch'
                    if arch_dir.exists():
                        arch_x86_64_dir = arch_dir / 'x86_64'
                        if arch_x86_64_dir.exists():
                            module_info['arch_sources'] = self._discover_sources(arch_x86_64_dir)
                    
                    # Analyze module for special requirements
                    self._analyze_module_requirements(module_info)
                    
                    # Only add modules that have sources or are explicitly needed
                    if module_info['sources'] or module_info['arch_sources'] or module_info['is_critical']:
                        modules[module_name] = module_info
                    else:
                        print(f"    ⏭️  Skipping {module_name} (no sources found)")
                        
                except Exception as e:
                    print(f"    ❌ Error processing {module_name}: {e}")
                    continue
                    
        except PermissionError as e:
            print(f"❌ Permission error accessing {self.posix_path}: {e}")
            raise
        
        if skipped_items:
            print(f"  ⏭️  Skipped items: {', '.join(skipped_items)}")
            
        print(f"  ✅ Discovered {len(modules)} POSIX modules")
        return modules
    
    def _discover_sources(self, directory: Path) -> List[str]:
        """Discover source files in a directory using pathlib.rglob()"""
        sources = []
        
        # Use rglob for better file discovery (non-recursive in module dirs)
        source_patterns = ['*.c', '*.cpp', '*.S', '*.asm']
        
        for pattern in source_patterns:
            for file_path in directory.glob(pattern):
                if file_path.is_file():
                    # Make path relative to the module directory
                    relative_path = file_path.relative_to(directory)
                    sources.append(str(relative_path))
        
        return sorted(sources)
    
    def _analyze_module_requirements(self, module_info: Dict) -> None:
        """Analyze module for special compilation requirements"""
        module_name = module_info['name']
        
        # Special requirements based on module analysis
        if module_name == 'malloc':
            module_info['special_includes'] = [
                '../../../../../headers/compatibility/bsd'  # for sys/queue.h
            ]
            module_info['special_flags'] = ['-DNBBY=8']  # for OpenBSD malloc
            
        elif module_name == 'string':
            module_info['special_includes'] = [
                '../../../../../headers/private/libroot/locale'  # for LocaleBackend.h
            ]
            
        elif module_name == 'stdio':
            # stdio might need special handling for I/O
            pass
            
        elif module_name == 'pthread':
            # pthread needs thread-specific flags
            module_info['special_flags'] = ['-D_REENTRANT']
            
        elif module_name == 'locale':
            module_info['special_includes'] = [
                '../../../../../headers/private/libroot/locale'
            ]
            
        elif module_name in ['glibc', 'musl']:
            # These are compatibility layers
            module_info['special_flags'] = ['-D_GNU_SOURCE']
    
    def generate_module_meson_build(self, module_info: Dict) -> str:
        """Generate meson.build content for a single module with custom modules integration"""
        module_name = module_info['name']
        sources = module_info['sources']
        arch_sources = module_info['arch_sources']
        
        lines = [
            f"# Haiku libroot POSIX {module_name} implementation",
            f"# Auto-generated from directory structure analysis with custom modules integration",
            f"",
            f"message('Building libroot POSIX {module_name} layer...')",
            f"",
        ]
        
        # Add custom modules integration if available
        if CUSTOM_MODULES_AVAILABLE:
            lines.extend(self._generate_custom_modules_integration(module_info))
        
        # Architecture detection for arch-specific sources
        if arch_sources:
            lines.extend([
                "# Architecture detection",
                "target_arch = get_option('target_arch')",
                "",
            ])
        
        # Sources definition
        if sources:
            lines.extend([
                f"# {module_name} implementation sources",
                f"posix_{module_name}_sources = files([",
            ])
            
            for source in sources:
                lines.append(f"    '{source}',")
                
            lines.extend([
                "])",
                "",
            ])
        
        # Architecture-specific sources
        if arch_sources:
            lines.extend([
                f"# Architecture-specific {module_name} sources",
                f"if target_arch == 'x86_64'",
                f"    posix_{module_name}_arch_sources = files([",
            ])
            
            for arch_source in arch_sources:
                lines.append(f"        'arch/x86_64/{arch_source}',")
                
            lines.extend([
                f"    ])",
                f"else",
                f"    posix_{module_name}_arch_sources = []",
                f"endif",
                f"",
            ])
        
        # Build library
        all_sources = []
        if sources:
            all_sources.append(f"posix_{module_name}_sources")
        if arch_sources:
            all_sources.append(f"posix_{module_name}_arch_sources")
        
        if all_sources:
            lines.extend([
                f"# Build {module_name} object library",
                f"posix_{module_name}_lib = static_library('posix_{module_name}',",
            ])
            
            # Add sources
            for source_var in all_sources:
                lines.append(f"    {source_var},")
            
            # Include directories
            lines.append(f"    ")
            lines.append(f"    include_directories: [")
            lines.append(f"        haiku_config['get_all_include_dirs'],")
            
            # Add special includes
            for special_include in module_info['special_includes']:
                lines.append(f"        include_directories('{special_include}'),")
            
            # Build feature includes are added via c_args instead of include_directories 
            # to avoid absolute path issues in Meson
            
            lines.append(f"    ],")
            lines.append(f"    ")
            
            # C arguments
            c_args = self.standard_c_args.copy()
            c_args.extend(module_info['special_flags'])
            
            # Add build feature arguments if available
            if CUSTOM_MODULES_AVAILABLE:
                build_feature_args = self._get_build_feature_args(module_info)
                c_args.extend(build_feature_args)
            
            lines.append(f"    c_args: [")
            for arg in c_args:
                lines.append(f"        '{arg}',")
            # Add architecture flag dynamically
            lines.append(f"        '-DARCH_' + target_arch,")
            lines.append(f"    ],")
            lines.append(f"    ")
            
            # CPP arguments
            lines.append(f"    cpp_args: [")
            for arg in c_args:  # Use same as C args
                lines.append(f"        '{arg}',")
            # Add architecture flag dynamically
            lines.append(f"        '-DARCH_' + target_arch,")
            lines.append(f"    ],")
            lines.append(f"    ")
            
            lines.extend([
                f"    install: false",
                f")",
                f"",
            ])
            
            # Export dependency
            lines.extend([
                f"# Export for parent",
                f"posix_{module_name}_dep = declare_dependency(",
                f"    link_with: posix_{module_name}_lib",
                f")",
                f"",
            ])
        
        lines.extend([
            f"message('libroot POSIX {module_name} layer configured successfully')",
        ])
        
        return '\n'.join(lines)
    
    def _generate_custom_modules_integration(self, module_info: Dict) -> List[str]:
        """Generate custom modules integration code for a module"""
        lines = []
        module_name = module_info['name']
        
        # Add build features detection only if module actually needs external dependencies
        needed_features = self._get_needed_features_for_module(module_name)
        if needed_features and self.build_features:
            available_features = []
            for feature in needed_features:
                if feature in self.build_features and self.build_features[feature].get('available', False):
                    available_features.append(feature)
            
            if available_features:
                lines.extend([
                    f"# Build features integration for {module_name}",
                    "build_features_available = ["
                ])
                
                for feature in available_features:
                    info = self.build_features[feature]
                    lines.append(f"    '{feature}',  # {info.get('headers', 'N/A')}")
                
                lines.extend([
                    "]",
                    f"message('{module_name} using build features:', build_features_available)",
                    ""
                ])
            else:
                lines.extend([
                    f"# No external build features needed for {module_name}",
                    ""
                ])
        
        # Add resource handling if module has .rdef files
        module_dir = module_info['path']
        rdef_files = list(module_dir.glob('*.rdef'))
        if rdef_files and self.resource_handler and self.resource_handler.validate_rc_compiler():
            lines.extend([
                "# Resource compilation using ResourceHandler",
                f"rc_compiler = '{self.resource_handler.get_rc_compiler()}'",
                ""
            ])
            
            for rdef_file in rdef_files:
                target_name = f"{module_name}_{rdef_file.stem}_rsrc"
                lines.extend([
                    f"{target_name} = custom_target('{target_name}',",
                    f"    input: '{rdef_file.name}',",
                    f"    output: '{rdef_file.stem}.rsrc',",
                    f"    command: [rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],",
                    f"    build_by_default: true",
                    f")",
                    ""
                ])
        
        return lines
    
    def _get_needed_features_for_module(self, module_name: str) -> List[str]:
        """Determine which build features a module actually needs based on dependency analysis"""
        # Based on comprehensive POSIX dependencies analysis (POSIX_DEPENDENCIES_ANALYSIS_FIXED.md)
        module_feature_map = {
            # Modules with actual external dependencies found:
            'crypt': [],        # crypt has own crypto implementation, no external deps needed
            'glibc': ['icu'],   # glibc compatibility layer may use ICU for locale features
            'malloc': ['icu'],  # malloc might use ICU for debugging/error messages
            'musl': [],         # musl compatibility layer, no external deps
            'stdlib': ['freetype'],  # stdlib may use freetype for some operations
            'time': ['icu'],    # time functions may use ICU for locale-specific formatting
            'unistd': ['icu'],  # unistd functions may use ICU for locale features
            
            # All other modules are clean (no external dependencies):
            'libstdthreads': [],
            'locale': [],       # surprisingly, locale module itself is clean!
            'pthread': [],
            'signal': [],
            'stdio': [],
            'string': [],
            'sys': [],
            'wchar': [],
        }
        
        return module_feature_map.get(module_name, [])
    
    def _get_build_feature_includes(self, module_info: Dict) -> List[str]:
        """Get additional include directories from build features"""
        includes = []
        module_name = module_info['name']
        
        # Only add includes for features this module actually needs
        needed_features = self._get_needed_features_for_module(module_name)
        for feature in needed_features:
            if feature in self.build_features and self.build_features[feature].get('available', False):
                feature_headers = build_features.BuildFeatureAttribute(feature, 'headers', self.architecture)
                if feature_headers:
                    includes.append(feature_headers)
                
        return includes
    
    def _get_build_feature_args(self, module_info: Dict) -> List[str]:
        """Get additional compiler arguments from build features"""
        args = []
        module_name = module_info['name']
        
        # Only add arguments for features this module actually needs
        needed_features = self._get_needed_features_for_module(module_name)
        for feature in needed_features:
            if feature in self.build_features and self.build_features[feature].get('available', False):
                # Add feature-specific define
                args.append(f'-DHAVE_{feature.upper()}=1')
                
                # Add include path via -I flag to avoid absolute path issues
                feature_headers = build_features.BuildFeatureAttribute(feature, 'headers', self.architecture)
                if feature_headers:
                    args.append(f'-I{feature_headers}')
                
        return args
    
    def update_main_posix_meson_build(self, modules: Dict[str, Dict]) -> str:
        """Update the main POSIX meson.build to include all discovered modules"""
        
        # Read existing content if it exists
        main_posix_build = self.posix_path / 'meson.build'
        existing_content = ""
        if main_posix_build.exists():
            with open(main_posix_build, 'r') as f:
                existing_content = f.read()
        
        # Find where to insert subdir() calls
        lines = existing_content.split('\n') if existing_content else []
        
        # Create new content with all modules
        new_lines = []
        found_subdirs = False
        
        for line in lines:
            new_lines.append(line)
            
            # After architecture-specific subdirectory, add our modules
            if "subdir('arch/' + target_arch)" in line:
                found_subdirs = True
                new_lines.append("")
                new_lines.append("# Essential C library function subdirectories (auto-generated)")
                
                # Add critical modules first
                for module_name in sorted(self.critical_modules):
                    if module_name in modules:
                        new_lines.append(f"subdir('{module_name}')   # {modules[module_name]['name']}")
                
                # Add remaining modules
                for module_name in sorted(modules.keys()):
                    if module_name not in self.critical_modules:
                        new_lines.append(f"subdir('{module_name}')   # {modules[module_name]['name']}")
                
                # Skip any existing subdir lines for modules
                continue
            
            # Skip old module subdir lines (both with and without comments)
            if any(f"subdir('{mod}')" in line for mod in modules.keys()):
                continue
        
        # If we didn't find the arch subdir, add modules at the end of subdirs
        if not found_subdirs and modules:
            # Find a good insertion point
            insert_pos = -1
            for i, line in enumerate(new_lines):
                if line.strip().startswith("# Build POSIX layer") or line.strip().startswith("posix_main_lib"):
                    insert_pos = i
                    break
            
            if insert_pos > 0:
                module_lines = [
                    "",
                    "# Essential C library function subdirectories (auto-generated)",
                ]
                
                for module_name in sorted(self.critical_modules):
                    if module_name in modules:
                        module_lines.append(f"subdir('{module_name}')   # {modules[module_name]['name']}")
                
                for module_name in sorted(modules.keys()):
                    if module_name not in self.critical_modules:
                        module_lines.append(f"subdir('{module_name}')   # {modules[module_name]['name']}")
                        
                module_lines.append("")
                
                # Insert the module lines
                new_lines = new_lines[:insert_pos] + module_lines + new_lines[insert_pos:]
        
        return '\n'.join(new_lines)
    
    def update_main_libroot_dependencies(self, modules: Dict[str, Dict]) -> str:
        """Generate dependency list for main libroot meson.build"""
        deps = []
        
        # Standard dependencies
        deps.extend([
            'os_main_dep,',
            'os_arch_dep,',
            'posix_main_dep,',
            'posix_arch_dep,'
        ])
        
        # Add module dependencies
        for module_name in sorted(modules.keys()):
            deps.append(f'posix_{module_name}_dep,')
        
        return '\n        '.join(deps)
    
    def generate_all_modules(self) -> Tuple[Dict[str, str], List[str]]:
        """Generate meson.build files for all discovered modules"""
        print("🚀 Starting POSIX modules generation...")
        
        # Discover modules
        modules = self.discover_posix_modules()
        
        if not modules:
            print("❌ No POSIX modules found!")
            return {}, []
        
        print(f"📋 Generating meson.build files for {len(modules)} modules:")
        for name, info in modules.items():
            status = "🔥 CRITICAL" if info['is_critical'] else "📦 OPTIONAL"
            sources_count = len(info['sources']) + len(info['arch_sources'])
            print(f"  {status} {name:<12} ({sources_count} sources)")
        
        # Generate files
        files_to_write = {}
        generated_modules = []
        
        for module_name, module_info in modules.items():
            if module_info['sources'] or module_info['arch_sources']:
                module_dir = module_info['path']
                meson_build_path = module_dir / 'meson.build'
                
                content = self.generate_module_meson_build(module_info)
                files_to_write[str(meson_build_path)] = content
                generated_modules.append(module_name)
        
        # Update main POSIX meson.build
        main_posix_content = self.update_main_posix_meson_build(modules)
        files_to_write[str(self.posix_path / 'meson.build')] = main_posix_content
        
        print(f"✅ Generated {len(files_to_write)} meson.build files")
        print(f"🎯 Dependency list for libroot:")
        print("    dependencies: [")
        print("        " + self.update_main_libroot_dependencies(modules))
        print("    ],")
        
        return files_to_write, generated_modules
    
    def write_files(self, files_dict: Dict[str, str], force: bool = False) -> Tuple[List[str], List[str]]:
        """Write generated files"""
        written = []
        skipped = []
        
        for file_path, content in files_dict.items():
            path = Path(file_path)
            
            if path.exists() and not force:
                print(f"⚠️  File exists: {path.name}")
                choice = input(f"   Overwrite? [y/N]: ").strip().lower()
                if choice not in ['y', 'yes']:
                    skipped.append(str(path))
                    continue
            
            try:
                with open(path, 'w', encoding='utf-8') as f:
                    f.write(content)
                written.append(str(path))
                print(f"✅ Generated: {path.name}")
            except Exception as e:
                print(f"❌ Error writing {path}: {e}")
        
        return written, skipped


def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Haiku libroot POSIX Modules Meson Generator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
FEATURES:
- Auto-discovers all POSIX modules in src/system/libroot/posix/
- Generates proper meson.build files for each module
- Handles architecture-specific sources (x86_64)
- Includes special compiler flags for modules like malloc, string
- Updates main POSIX meson.build with all discovered modules

Examples:
  %(prog)s --generate-all
  %(prog)s --generate-all --force
  %(prog)s --dry-run
        """
    )
    
    parser.add_argument('--libroot-path',
                       default='/home/ruslan/haiku/src/system/libroot',
                       help='Path to libroot directory')
    parser.add_argument('--architecture', '--arch',
                       default='x86_64',
                       help='Target architecture (default: x86_64)')
    parser.add_argument('--generate-all',
                       action='store_true',
                       help='Generate meson.build files for all modules')
    parser.add_argument('--force',
                       action='store_true', 
                       help='Force overwrite existing files')
    parser.add_argument('--dry-run',
                       action='store_true',
                       help='Show what would be generated without writing files')
    
    args = parser.parse_args()
    
    print("🚀 Haiku libroot POSIX Modules Meson Generator")
    print(f"📁 libroot path: {args.libroot_path}")
    print(f"🏗️  Architecture: {args.architecture}")
    if CUSTOM_MODULES_AVAILABLE:
        print("🔧 Custom modules: ENABLED")
    else:
        print("⚠️  Custom modules: DISABLED")
    print("=" * 60)
    
    generator = LibrootPosixModulesGenerator(args.libroot_path, args.architecture)
    
    if args.generate_all or args.dry_run:
        files_dict, modules = generator.generate_all_modules()
        
        if args.dry_run:
            print(f"\n🔍 [DRY RUN] Would generate {len(files_dict)} files:")
            for file_path in sorted(files_dict.keys()):
                path = Path(file_path)
                size = len(files_dict[file_path])
                print(f"  📄 {path.name:<25} ({size:>5} bytes)")
        else:
            print(f"\n📝 Writing {len(files_dict)} files...")
            written, skipped = generator.write_files(files_dict, args.force)
            
            print(f"\n✅ Generation Complete:")
            print(f"  📄 Generated: {len(written)} files")
            print(f"  ⏭️  Skipped: {len(skipped)} files")
            print(f"  🎯 Modules: {', '.join(sorted(modules))}")
            
            if written:
                print(f"\n🔧 Next steps:")
                print(f"  1. Update main libroot meson.build dependencies:")
                print(f"     Add the generated dependency variables to the dependencies list")
                print(f"  2. Test compilation: meson compile -C builddir root")
                print(f"  3. If issues occur, check module-specific requirements")
    else:
        print("❗ No action specified. Use --generate-all or --dry-run")
        parser.print_help()


if __name__ == "__main__":
    main()