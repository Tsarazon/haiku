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
    
    # Try environment variable first
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
    haiku_root = get_haiku_root()
    return haiku_root / 'generated' / 'build_packages'

def get_cross_tools_dir(architecture: str = 'x86_64', jam_cross_tools_dir: str = '', 
                       external_cross_tools_dir: str = '', use_jam_cross: bool = True) -> Path:
    """
    Get cross-tools directory dynamically with support for Meson options.
    
    Args:
        architecture: Target architecture
        jam_cross_tools_dir: Override path for Jam cross-tools (from meson option)
        external_cross_tools_dir: Path to external cross-tools (from meson option)
        use_jam_cross: Whether to use Jam cross-tools (from meson option)
    
    Returns:
        Path to cross-tools directory
    """
    if not use_jam_cross and external_cross_tools_dir:
        # Use external cross-tools
        return Path(external_cross_tools_dir)
    
    if jam_cross_tools_dir:
        # Use explicitly specified Jam cross-tools directory
        return Path(jam_cross_tools_dir)
    
    # Default: auto-detect Jam cross-tools
    haiku_root = get_haiku_root()
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
        'cross_tools_compatible': ensure_cross_tools_compatibility(architecture)
    }
    
    # Add gcc_syslibs info
    syslibs = detect_gcc_syslibs(architecture)
    if syslibs:
        config['gcc_syslibs'] = {
            'runtime': str(syslibs['runtime']) if syslibs['runtime'] else None,
            'devel': str(syslibs['devel']) if syslibs['devel'] else None
        }
    
    return config

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
    
    args = parser.parse_args()
    
    # Handle cross-tools fix
    if args.fix_cross_tools:
        if ensure_cross_tools_compatibility(args.architecture):
            print("✅ Cross-tools compatibility ensured")
        else:
            print("❌ Failed to ensure cross-tools compatibility")
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

if __name__ == '__main__':
    main()