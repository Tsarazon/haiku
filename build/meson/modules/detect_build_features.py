#!/usr/bin/env python3
"""
Haiku Build Feature Detection Module
This script auto-detects available build packages, cross-tools, and system configuration.
It serves as the foundation for build system discovery.
"""

import os
import sys
import json
import subprocess
from pathlib import Path
from typing import Dict, Optional, List, Tuple

# Cache for expensive operations
_cache = {}

def get_haiku_root() -> Path:
    """Dynamically find Haiku root directory (with caching)"""
    if 'haiku_root' in _cache:
        return _cache['haiku_root']
    
    # Try Meson source root first
    if 'MESON_SOURCE_ROOT' in os.environ:
        root = Path(os.environ['MESON_SOURCE_ROOT'])
        _cache['haiku_root'] = root
        return root
    
    # Try environment variable
    if 'HAIKU_ROOT' in os.environ:
        root = Path(os.environ['HAIKU_ROOT'])
        _cache['haiku_root'] = root
        return root
    
    # Try to find it relative to this script
    script_path = Path(__file__).resolve()
    current = script_path.parent
    
    # Go up directories looking for Haiku root indicators
    while current != current.parent:
        if (current / 'src').exists() and (current / 'headers').exists():
            _cache['haiku_root'] = current
            return current
        current = current.parent
    
    # Fallback to current working directory
    cwd = Path.cwd()
    _cache['haiku_root'] = cwd
    return cwd

def get_build_packages_dir() -> Path:
    """Get build packages directory dynamically"""
    # Try Meson build root first
    if 'MESON_BUILD_ROOT' in os.environ:
        meson_packages = Path(os.environ['MESON_BUILD_ROOT']) / 'build_packages'
        if meson_packages.exists():
            return meson_packages
    
    # Fallback to JAM structure
    haiku_root = get_haiku_root()
    return haiku_root / 'generated' / 'build_packages'

def get_cross_tools_dir(architecture: str = 'x86_64', jam_cross_tools_dir: str = '', 
                       external_cross_tools_dir: str = '', use_jam_cross: bool = None) -> Path:
    """
    Get cross-tools directory dynamically with support for Meson options.
    
    Args:
        architecture: Target architecture
        jam_cross_tools_dir: Override path for Jam cross-tools (from meson option)
        external_cross_tools_dir: Path to external cross-tools (from meson option)
        use_jam_cross: Whether to use Jam cross-tools (auto-detect if None)
    
    Returns:
        Path to cross-tools directory
    """
    haiku_root = get_haiku_root()
    
    # Auto-detect use_jam_cross if not specified
    if use_jam_cross is None:
        # Priority 1: Check if Meson builddir cross-tools exist AND are functional
        meson_builddir_cross = haiku_root / 'builddir' / f'cross-tools-{architecture}'
        meson_gcc = meson_builddir_cross / 'bin' / f'{architecture}-unknown-haiku-gcc'
        if meson_builddir_cross.exists() and meson_gcc.exists() and meson_gcc.is_file():
            use_jam_cross = False
        else:
            # Priority 2: Check if Jam cross-tools exist AND are functional
            jam_cross = haiku_root / 'generated' / f'cross-tools-{architecture}'
            jam_gcc = jam_cross / 'bin' / f'{architecture}-unknown-haiku-gcc'
            use_jam_cross = jam_cross.exists() and jam_gcc.exists() and jam_gcc.is_file()
    
    if not use_jam_cross and external_cross_tools_dir:
        # Use external cross-tools
        return Path(external_cross_tools_dir)
    
    if jam_cross_tools_dir:
        # Use explicitly specified Jam cross-tools directory
        return Path(jam_cross_tools_dir)
    
    if not use_jam_cross:
        # Prefer Meson build locations
        # Try Meson build root first
        if 'MESON_BUILD_ROOT' in os.environ:
            meson_cross = Path(os.environ['MESON_BUILD_ROOT']) / f'cross-tools-{architecture}'
            if meson_cross.exists():
                return meson_cross
        
        # Try builddir (Meson style)
        meson_builddir_cross = haiku_root / 'builddir' / f'cross-tools-{architecture}'
        if meson_builddir_cross.exists():
            return meson_builddir_cross
    
    # Default: fallback to Jam cross-tools
    return haiku_root / 'generated' / f'cross-tools-{architecture}'

def ensure_cross_tools_compatibility(architecture: str = 'x86_64') -> bool:
    """
    Ensure cross-tools are compatible with Meson by creating necessary symlinks.
    This fixes the libstdc++ detection issue.
    
    Returns:
        True if compatibility is ensured, False if there was an error
    """
    try:
        cross_tools_dir = get_cross_tools_dir(architecture)
        lib_dir = cross_tools_dir / f'{architecture}-unknown-haiku' / 'lib'
        
        if not lib_dir.exists():
            print(f"Warning: Cross-tools lib directory not found: {lib_dir}")
            return False
        
        # Fix libstdc++ symlink for Meson
        static_lib = lib_dir / 'libstdc++-static.a'
        regular_lib = lib_dir / 'libstdc++.a'
        
        if static_lib.exists() and not regular_lib.exists():
            regular_lib.symlink_to('libstdc++-static.a')
            print(f"Created symlink: {regular_lib} -> libstdc++-static.a")
            return True
        elif regular_lib.exists():
            # Already compatible
            return True
        else:
            print(f"Warning: libstdc++-static.a not found in {lib_dir}")
            return False
            
    except Exception as e:
        print(f"Error ensuring cross-tools compatibility: {e}")
        return False

def find_package_version(package_base_name: str, architecture: str = 'x86_64') -> Optional[str]:
    """
    Find the actual package directory for a given package base name.
    Returns the full directory name or None if not found.
    """
    cache_key = f'package_{package_base_name}_{architecture}'
    if cache_key in _cache:
        return _cache[cache_key]
    
    base_dir = get_build_packages_dir()
    
    if not base_dir.exists():
        return None
    
    # Look for packages matching pattern: package_name-version-architecture
    for entry in os.listdir(base_dir):
        if entry.startswith(f"{package_base_name}-") and entry.endswith(f"-{architecture}"):
            _cache[cache_key] = entry
            return entry
    
    return None

def get_package_info(package_name: str, architecture: str = 'x86_64') -> Optional[Dict]:
    """
    Get comprehensive package information including paths and version.
    
    Returns:
        Dictionary with package_dir, lib_path, headers_path, version, or None if not found
    """
    package_dir = find_package_version(package_name, architecture)
    if not package_dir:
        return None
    
    base_path = get_build_packages_dir() / package_dir
    lib_path = base_path / "develop" / "lib"
    headers_path = base_path / "develop" / "headers"
    
    # Extract version from package directory name
    version = None
    if '-' in package_dir:
        parts = package_dir.split('-')
        if len(parts) >= 2:
            version = '-'.join(parts[1:-1])  # Everything between name and architecture
    
    info = {
        'package_dir': package_dir,
        'base_path': str(base_path),
        'lib_path': str(lib_path) if lib_path.exists() else None,
        'headers_path': str(headers_path) if headers_path.exists() else None,
        'version': version,
        'exists': base_path.exists()
    }
    
    return info if info['exists'] else None

def detect_cross_compiler(architecture: str = 'x86_64') -> Optional[Dict[str, str]]:
    """
    Detect cross-compiler binaries and their versions.
    
    Returns:
        Dictionary mapping tool names to their paths, or None if not found
    """
    cache_key = f'cross_compiler_{architecture}'
    if cache_key in _cache:
        return _cache[cache_key]
    
    cross_tools_dir = get_cross_tools_dir(architecture)
    bin_dir = cross_tools_dir / 'bin'
    
    if not bin_dir.exists():
        return None
    
    prefix = f'{architecture}-unknown-haiku'
    binaries = {}
    
    for tool in ['gcc', 'g++', 'ar', 'strip', 'objcopy', 'ld', 'nm', 'ranlib', 'objdump']:
        tool_path = bin_dir / f'{prefix}-{tool}'
        if tool_path.exists():
            binaries[tool] = str(tool_path)
    
    # Get GCC version if available
    if 'gcc' in binaries:
        try:
            result = subprocess.run([binaries['gcc'], '-dumpversion'], 
                                  capture_output=True, text=True, check=True)
            binaries['gcc_version'] = result.stdout.strip()
        except subprocess.SubprocessError:
            pass
    
    _cache[cache_key] = binaries if binaries else None
    return binaries

def detect_gcc_syslibs(architecture: str = 'x86_64') -> Optional[Dict]:
    """
    Detect GCC syslibs packages (runtime and development).
    
    Returns:
        Dictionary with 'runtime' and 'devel' package paths
    """
    base_dir = get_build_packages_dir()
    
    if not base_dir.exists():
        return None
    
    syslibs = {'runtime': None, 'devel': None}
    
    for entry in base_dir.iterdir():
        if entry.is_dir() and entry.name.startswith('gcc_syslibs'):
            if entry.name.endswith(f'-{architecture}'):
                if '_devel' in entry.name:
                    syslibs['devel'] = entry
                else:
                    syslibs['runtime'] = entry
    
    return syslibs if (syslibs['runtime'] or syslibs['devel']) else None

def get_all_packages(architecture: str = 'x86_64') -> Dict[str, Dict]:
    """
    Detect all available build packages.
    
    Returns:
        Dictionary mapping package names to their info
    """
    # Standard packages to look for
    standard_packages = [
        'zlib', 'zstd', 'icu74', 'icu', 'freetype', 'libpng', 
        'jpeg', 'tiff', 'libwebp', 'curl', 'openssl'
    ]
    
    packages = {}
    for pkg in standard_packages:
        info = get_package_info(pkg, architecture)
        if info:
            packages[pkg] = info
    
    return packages

def get_haiku_build_info() -> Dict[str, str]:
    """
    Get Haiku build information (JAM BuildSetup integration).
    
    Returns:
        Dictionary with build version, revision, and description
    """
    haiku_root = get_haiku_root()
    build_info = {
        'haiku_version': 'R1/development',
        'haiku_revision': 'unknown',
        'build_description': 'Unknown Build',
        'build_date': 'unknown',
        'distro_compatibility': 'default'
    }
    
    try:
        # Try to get git revision
        result = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], 
                              cwd=haiku_root, capture_output=True, text=True, check=True)
        build_info['haiku_revision'] = result.stdout.strip()
        
        # Try to get build date from git
        result = subprocess.run(['git', 'log', '-1', '--format=%ci'], 
                              cwd=haiku_root, capture_output=True, text=True, check=True)
        build_info['build_date'] = result.stdout.strip()
        
        # Generate build description
        build_info['build_description'] = f"Haiku {build_info['haiku_version']} (hrev{build_info['haiku_revision']})"
        
    except (subprocess.SubprocessError, FileNotFoundError):
        # Fallback if git is not available
        pass
    
    return build_info

def detect_host_platform() -> Dict[str, str]:
    """
    Detect host platform information (JAM BuildSetup lines 231-280).
    
    Returns:
        Dictionary with host platform details
    """
    import platform
    
    # Map Python platform to JAM HOST_PLATFORM equivalents
    system = platform.system().lower()
    machine = platform.machine().lower()
    
    # JAM HOST_PLATFORM mapping
    if system == 'linux':
        host_platform = 'linux'
    elif system == 'darwin':
        host_platform = 'darwin'
    elif system == 'freebsd':
        host_platform = 'freebsd'
    elif system.startswith('cygwin'):
        host_platform = 'cygwin'
    else:
        host_platform = 'unknown'
    
    # Map machine architecture
    if machine in ['x86_64', 'amd64']:
        host_cpu = 'x86_64'
        host_arch = 'x86_64'
    elif machine in ['i386', 'i586', 'i686']:
        host_cpu = 'x86'
        host_arch = 'x86'
    elif machine.startswith('arm'):
        if '64' in machine:
            host_cpu = 'arm64'
            host_arch = 'arm64'
        else:
            host_cpu = 'arm'
            host_arch = 'arm'
    elif machine == 'riscv64':
        host_cpu = 'riscv64'
        host_arch = 'riscv64'
    else:
        host_cpu = 'unknown'
        host_arch = 'unknown'
    
    return {
        'host_platform': host_platform,
        'host_cpu': host_cpu,
        'host_arch': host_arch,
        'host_os': system,
        'host_machine': machine
    }

def generate_meson_config(architecture: str = 'x86_64') -> Dict:
    """
    Generate complete Meson configuration as a dictionary.
    
    Returns:
        Dictionary with all build configuration info
    """
    config = {
        'haiku_root': str(get_haiku_root()),
        'architecture': architecture,
        'cross_tools_dir': str(get_cross_tools_dir(architecture)),
        'build_packages_dir': str(get_build_packages_dir()),
        'cross_compiler': detect_cross_compiler(architecture),
        'gcc_syslibs': {},
        'packages': get_all_packages(architecture),
        'cross_tools_compatible': ensure_cross_tools_compatibility(architecture),
        'build_info': get_haiku_build_info(),
        'host_platform': detect_host_platform()
    }
    
    # Add gcc_syslibs info
    syslibs = detect_gcc_syslibs(architecture)
    if syslibs:
        config['gcc_syslibs'] = {
            'runtime': str(syslibs['runtime']) if syslibs['runtime'] else None,
            'devel': str(syslibs['devel']) if syslibs['devel'] else None
        }
    
    return config

def generate_meson_cross_file(architecture: str = 'x86_64', output_file: str = None) -> str:
    """
    Generate Meson cross-compilation file using all integrated modules.
    
    Args:
        architecture: Target architecture
        output_file: Output file path (auto-generated if None)
        
    Returns:
        Path to generated cross-compilation file
    """
    if output_file is None:
        haiku_root = get_haiku_root()
        output_file = haiku_root / 'build' / 'meson' / f'haiku-{architecture}-cross-dynamic.ini'
    
    # Get configuration from all modules
    config = generate_meson_config(architecture)
    cross_compiler = config['cross_compiler']
    build_info = config['build_info']
    
    if not cross_compiler:
        raise RuntimeError(f"Cross-compiler not found for {architecture}")
    
    # Import ArchitectureRules for build settings
    try:
        from . import ArchitectureRules as arch_rules
        arch_config = arch_rules.ArchitectureConfig(architecture)
        build_flags = arch_config.get_build_flags()
        image_defaults = arch_config.get_image_defaults()
    except ImportError:
        import ArchitectureRules as arch_rules
        arch_config = arch_rules.ArchitectureConfig(architecture)
        build_flags = arch_config.get_build_flags()
        image_defaults = arch_config.get_image_defaults()
    
    # Get architecture-specific settings
    cpu_family_map = {
        'x86_64': 'x86_64',
        'x86': 'x86',
        'arm': 'arm',
        'arm64': 'aarch64',
        'riscv64': 'riscv64'
    }
    
    cpu_family = cpu_family_map.get(architecture, architecture)
    endian = 'little' if architecture != 'sparc' else 'big'  # Most Haiku archs are little-endian
    
    # Get paths
    haiku_root = get_haiku_root()
    cross_tools_dir = get_cross_tools_dir(architecture)
    build_packages_dir = get_build_packages_dir()
    
    # Build the cross-compilation file content
    content = [
        "# Haiku Cross-Compilation File",
        f"# Auto-generated by detect_build_features.py from integrated JAM modules",
        f"# Architecture: {architecture}",
        f"# Build Description: {build_info['build_description']}",
        f"# Generated: {build_info['build_date']}",
        "",
        "[binaries]"
    ]
    
    # Add compiler binaries
    prefix = f'{architecture}-unknown-haiku'
    
    # Handle special cases for C and C++ compilers
    if 'gcc' in cross_compiler:
        content.append(f"c = '{cross_compiler['gcc']}'")
    if 'g++' in cross_compiler:
        content.append(f"cpp = '{cross_compiler['g++']}'")
    
    # Add other tools
    for tool in ['ar', 'strip', 'objcopy', 'ld', 'nm', 'ranlib', 'objdump']:
        if tool in cross_compiler:
            content.append(f"{tool} = '{cross_compiler[tool]}'")
    
    content.extend([
        "",
        "[host_machine]",
        "system = 'haiku'",
        f"cpu_family = '{cpu_family}'",
        f"cpu = '{architecture}'",
        f"endian = '{endian}'",
        ""
    ])
    
    # Built-in options section
    content.append("[built-in options]")
    
    # Get library paths from gcc_syslibs
    gcc_syslibs = config.get('gcc_syslibs', {})
    lib_paths = []
    include_paths = []
    
    # Cross-tools lib path
    cross_lib_path = cross_tools_dir / f'{prefix}/lib'
    if cross_lib_path.exists():
        lib_paths.append(f"-L{cross_lib_path}")
    
    # GCC syslibs paths
    if gcc_syslibs.get('runtime'):
        runtime_lib = Path(gcc_syslibs['runtime']) / 'lib'
        if runtime_lib.exists():
            lib_paths.append(f"-L{runtime_lib}")
    
    if gcc_syslibs.get('devel'):
        devel_lib = Path(gcc_syslibs['devel']) / 'develop/lib'
        devel_headers = Path(gcc_syslibs['devel']) / 'develop/headers'
        if devel_lib.exists():
            lib_paths.append(f"-L{devel_lib}")
        if devel_headers.exists():
            include_paths.append(f"-I{devel_headers}")
    
    # System headers
    system_headers = haiku_root / 'headers'
    posix_headers = haiku_root / 'headers/posix'
    if system_headers.exists():
        include_paths.extend([f"-isystem {posix_headers}", f"-isystem {system_headers}"])
    
    # Cross-tools system headers (for stdlib.h compatibility)
    # Try multiple possible locations for system headers
    cross_include_paths = [
        cross_tools_dir / f'{prefix}/sys-include',
        cross_tools_dir / f'{prefix}/include',
        cross_tools_dir / 'lib/gcc' / f'{prefix}' / '13.3.0' / 'include'
    ]
    
    for cross_include in cross_include_paths:
        if cross_include.exists():
            include_paths.append(f"-isystem {cross_include}")
    
    # C++ link args
    cpp_link_args = [f"'-B', '{haiku_root}/builddir/src/system/glue'"]
    cpp_link_args.extend([f"'{path}'" for path in lib_paths])
    
    # Add libstdc++ static linking for cross-compilation
    libstdcpp_static = cross_lib_path / 'libstdc++-static.a'
    if libstdcpp_static.exists():
        cpp_link_args.append(f"'{libstdcpp_static}'")
    cpp_link_args.append("'-lsupc++'")
    
    content.append(f"cpp_link_args = [{', '.join(cpp_link_args)}]")
    
    # C link args (simpler)
    c_link_args = [f"'-B', '{haiku_root}/builddir/src/system/glue'"]
    content.append(f"c_link_args = [{', '.join(c_link_args)}]")
    
    # Compiler args
    cpp_args = [f"'{path}'" for path in include_paths]
    content.append(f"cpp_args = [{', '.join(cpp_args)}]")
    content.append(f"c_args = [{', '.join(cpp_args)}]")
    
    # Standards
    content.extend([
        "cpp_std = 'c++17'",
        "c_std = 'c11'",
        f"debug = {'true' if build_flags['debug'] > 0 else 'false'}",
        ""
    ])
    
    # Properties section
    content.extend([
        "[properties]",
        "skip_sanity_check = true",
        "needs_exe_wrapper = false",
        "has_function_printf = true",
        "has_function_hfkerhisadf = false",
        "# Force stdlib identification to bypass Meson detection",
        f"sys_root = '{haiku_root}'",
        "# Bypass exe wrapper requirement", 
        "exe_wrapper = ['true']",
        ""
    ])
    
    # Add build feature information as comments
    available_features = []
    try:
        from . import BuildFeatures as bf
        available_features = bf.get_available_features(architecture)
    except ImportError:
        import BuildFeatures as bf
        available_features = bf.get_available_features(architecture)
    
    if available_features:
        content.extend([
            "# Available Build Features:",
            f"# {', '.join(available_features)}",
            ""
        ])
    
    # Write the file
    output_path = Path(output_file)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        f.write('\n'.join(content))
    
    return str(output_path)

def generate_cross_files_for_profiles(architectures: List[str] = None, profiles: List[str] = None) -> Dict[str, str]:
    """
    Generate cross-compilation files for multiple architectures and profiles.
    Integration with CommandLineArguments build profiles.
    
    Args:
        architectures: List of target architectures (default: ['x86_64'])
        profiles: List of build profiles to add as comments (default: all major profiles)
        
    Returns:
        Dictionary mapping architecture to generated cross-file path
    """
    if architectures is None:
        architectures = ['x86_64']
    
    if profiles is None:
        profiles = ['nightly-anyboot', 'release-cd', 'bootstrap-raw', 'minimum-vmware']
    
    generated_files = {}
    
    for arch in architectures:
        try:
            # Generate profile information as comments
            profile_info = []
            profile_info.append("# Build Profile Information:")
            
            try:
                # Import CommandLineArguments to get profile data
                import importlib.util
                from pathlib import Path
                
                module_path = Path(__file__).parent / "CommandLineArguments.py"
                spec = importlib.util.spec_from_file_location("CommandLineArguments", module_path)
                cmd_args = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(cmd_args)
                
                for profile in profiles:
                    processor = cmd_args.HaikuCommandLineArguments()
                    processor.build_profile = profile
                    settings = processor.get_profile_settings()
                    packages = processor.get_profile_packages()
                    
                    profile_info.append(f"# {profile}:")
                    profile_info.append(f"#   Build Type: {settings.get('build_type')}")
                    profile_info.append(f"#   Image Size: {settings.get('image_size')} MB" if settings.get('image_size') else "#   Image Size: default")
                    profile_info.append(f"#   System Packages: {len(packages.get('system', []))}")
                    profile_info.append(f"#   Optional Packages: {len(packages.get('optional', []))}")
                    
            except ImportError:
                profile_info.append("# Profile information unavailable")
            
            # Generate base cross-file
            cross_file = generate_meson_cross_file(arch)
            
            # Read the file and add profile information
            with open(cross_file, 'r') as f:
                content = f.read()
            
            # Insert profile information before the build features
            if "# Available Build Features:" in content:
                content = content.replace("# Available Build Features:", 
                                        '\n'.join(profile_info) + "\n#\n# Available Build Features:")
            else:
                content += '\n' + '\n'.join(profile_info) + '\n'
            
            # Write back with profile info
            with open(cross_file, 'w') as f:
                f.write(content)
            
            generated_files[arch] = cross_file
            print(f"✅ Generated {arch} cross-file with profile info: {cross_file}")
            
        except Exception as e:
            print(f"❌ Failed to generate cross-file for {arch}: {e}")
            continue
    
    return generated_files

def main():
    """Main function for command-line usage"""
    import argparse
    parser = argparse.ArgumentParser(description='Detect Haiku build features and configuration')
    parser.add_argument('--architecture', default='x86_64',
                       help='Target architecture (default: x86_64)')
    parser.add_argument('--json', action='store_true',
                       help='Output as JSON')
    parser.add_argument('--packages-only', action='store_true',
                       help='Show only package information')
    parser.add_argument('--fix-cross-tools', action='store_true',
                       help='Fix cross-tools compatibility for Meson')
    parser.add_argument('--generate-cross-file', action='store_true',
                       help='Generate Meson cross-compilation file')
    parser.add_argument('--output-cross-file', type=str,
                       help='Output path for cross-compilation file')
    parser.add_argument('--generate-all-cross-files', action='store_true',
                       help='Generate cross-compilation files with profile info')
    
    args = parser.parse_args()
    
    # Handle cross-tools fix
    if args.fix_cross_tools:
        if ensure_cross_tools_compatibility(args.architecture):
            print("✅ Cross-tools compatibility ensured")
        else:
            print("❌ Failed to ensure cross-tools compatibility")
            sys.exit(1)
        return
    
    # Handle cross-file generation
    if args.generate_cross_file:
        try:
            output_file = generate_meson_cross_file(args.architecture, args.output_cross_file)
            print(f"✅ Generated Meson cross-compilation file: {output_file}")
            
            # Also show some info about what was generated
            config = generate_meson_config(args.architecture)
            print(f"🔧 Cross-compiler: {config['cross_compiler'].get('gcc_version', 'unknown')} GCC")
            print(f"📦 Build packages: {len(config['packages'])} available")
            # Get available features
            try:
                import BuildFeatures as bf
                available_features = bf.get_available_features(args.architecture)
                print(f"🎯 Build features: {len(available_features)} enabled")
            except ImportError:
                print("🎯 Build features: detection unavailable")
            print(f"📝 Build info: {config['build_info']['build_description']}")
        except Exception as e:
            print(f"❌ Failed to generate cross-compilation file: {e}")
            sys.exit(1)
        return
    
    # Handle all cross-files generation
    if args.generate_all_cross_files:
        try:
            generated_files = generate_cross_files_for_profiles([args.architecture])
            print(f"🎉 Generated cross-compilation files with complete profile integration!")
            for arch, path in generated_files.items():
                print(f"   {arch}: {path}")
        except Exception as e:
            print(f"❌ Failed to generate cross-compilation files: {e}")
            sys.exit(1)
        return
    
    # Generate configuration
    if args.json:
        config = generate_meson_config(args.architecture)
        print(json.dumps(config, indent=2))
    elif args.packages_only:
        packages = get_all_packages(args.architecture)
        print("# Auto-detected build packages")
        print(f"# Architecture: {args.architecture}")
        print()
        
        for name, info in packages.items():
            print(f"# {name.upper()} v{info.get('version', 'unknown')}")
            if info['lib_path']:
                print(f"{name}_lib_path = '{info['lib_path']}'")
            if info['headers_path']:
                print(f"{name}_headers_path = '{info['headers_path']}'")
            print(f"{name}_available = true")
            print()
        
        if not packages:
            print("# No packages found!")
    else:
        # Default output - human readable
        config = generate_meson_config(args.architecture)
        print(f"=== Haiku Build Configuration ===")
        print(f"Haiku Root: {config['haiku_root']}")
        print(f"Architecture: {config['architecture']}")
        print(f"Cross-tools: {config['cross_tools_dir']}")
        print(f"Cross-tools Compatible: {'✅' if config['cross_tools_compatible'] else '❌'}")
        print()
        
        if config['cross_compiler']:
            print("Cross-Compiler:")
            gcc_version = config['cross_compiler'].get('gcc_version', 'unknown')
            print(f"  GCC Version: {gcc_version}")
            print(f"  Tools: {', '.join(config['cross_compiler'].keys())}")
        else:
            print("Cross-Compiler: NOT FOUND")
        print()
        
        if config['packages']:
            print(f"Packages ({len(config['packages'])} found):")
            for name, info in config['packages'].items():
                version = info.get('version', 'unknown')
                print(f"  {name}: v{version}")
        else:
            print("Packages: NONE FOUND")

# Cross-tools build integration functions
def build_cross_tools_with_meson_wrapper(architecture='x86_64', buildtools_dir='buildtools', jobs=0):
    """
    Build cross-tools using Meson wrapper approach.
    This function integrates cross-tools build into the Meson system.
    """
    import subprocess
    import multiprocessing
    
    haiku_root = get_haiku_root()
    
    # Determine paths
    if isinstance(buildtools_dir, str):
        buildtools_path = Path(buildtools_dir)
        if not buildtools_path.is_absolute():
            buildtools_path = haiku_root / buildtools_path
    else:
        buildtools_path = Path(buildtools_dir)
    
    # Output directory - use builddir for Meson-style build
    cross_tools_dir = haiku_root / 'builddir' / f'cross-tools-{architecture}'
    
    # Auto-detect jobs if not specified
    if jobs <= 0:
        jobs = multiprocessing.cpu_count()
    
    # Build command
    build_script = haiku_root / 'build' / 'scripts' / 'build_cross_tools_gcc4'
    target_machine = f'{architecture}-unknown-haiku'
    
    command = [
        str(build_script),
        target_machine,
        str(haiku_root),
        str(buildtools_path),
        str(cross_tools_dir),
        f'-j{jobs}'
    ]
    
    print(f"Building cross-tools for {architecture}...")
    print(f"Command: {' '.join(command)}")
    print(f"Output directory: {cross_tools_dir}")
    
    try:
        # Run the build process
        result = subprocess.run(command, 
                              cwd=haiku_root,
                              check=True,
                              text=True,
                              capture_output=False)  # Let output go to console
        
        print(f"✅ Cross-tools build completed successfully!")
        return cross_tools_dir
        
    except subprocess.CalledProcessError as e:
        print(f"❌ Cross-tools build failed with exit code {e.returncode}")
        raise
    except Exception as e:
        print(f"❌ Cross-tools build failed: {e}")
        raise

def ensure_cross_tools_exist(architecture='x86_64', use_meson_wrapper=False, **kwargs):
    """
    Ensure cross-tools exist for the specified architecture.
    Build them if they don't exist using the specified method.
    """
    # First try to find existing cross-tools
    try:
        cross_tools = find_cross_tools_directory(architecture)
        if cross_tools and cross_tools.exists():
            # Verify they work
            gcc_path = cross_tools / 'bin' / f'{architecture}-unknown-haiku-gcc'
            if gcc_path.exists():
                print(f"✅ Using existing cross-tools: {cross_tools}")
                return cross_tools
    except Exception as e:
        print(f"Warning: Could not verify existing cross-tools: {e}")
    
    # Need to build cross-tools
    print(f"Cross-tools not found for {architecture}, building...")
    
    if use_meson_wrapper:
        return build_cross_tools_with_meson_wrapper(architecture, **kwargs)
    else:
        print("❌ Traditional cross-tools build not implemented in this function")
        print("💡 Use --build-cross-tools option with configure script, or set use_meson_wrapper=True")
        raise RuntimeError("Cross-tools not available and traditional build not configured")

def get_cross_tools_build_options():
    """
    Get cross-tools build options from Meson.
    This function is called from main meson.build.
    """
    try:
        # In Meson context, we can access options
        # This will be called from meson.build with proper context
        return {
            'enabled': True,  # Will be replaced by get_option() call
            'buildtools_dir': 'buildtools',
            'jobs': 0
        }
    except:
        # Fallback for standalone execution
        return {
            'enabled': False,
            'buildtools_dir': 'buildtools', 
            'jobs': 0
        }

if __name__ == '__main__':
    main()