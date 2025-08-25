#!/usr/bin/env python3
"""
Haiku Build Features Module for Meson
Provides Jam-compatible BuildFeatureAttribute functionality for Meson builds.
This module bridges between Jam BuildFeatures concepts and modern build systems.
"""

import os
import sys
from pathlib import Path
from typing import List, Dict, Optional, Union

# Import detection functions from the detection module
try:
    from . import detect_build_features as detector
except ImportError:
    # Handle direct execution
    import detect_build_features as detector

# Jam BuildFeature definitions - matches existing Jam BuildFeatures
# These define how each package should be accessed in Jam-compatible way
JAM_BUILDFEATURE_DEFINITIONS = {
    'zlib': {
        'package_name': 'zlib',
        'library_names': ['libz.so', 'libz.a'],
        'header_subdir': '',  # Headers are in root of include dir
        'jam_name': 'zlib',
        'description': 'zlib compression library'
    },
    'zstd': {
        'package_name': 'zstd',
        'library_names': ['libzstd.so', 'libzstd.a'], 
        'header_subdir': '',
        'jam_name': 'zstd',
        'description': 'Zstandard compression library'
    },
    'icu': {
        'package_name': 'icu74',  # Use icu74 as it's what's actually available
        'library_names': ['libicudata.so', 'libicui18n.so', 'libicuio.so', 'libicuuc.so'],
        'header_subdir': 'unicode',
        'jam_name': 'icu',
        'description': 'International Components for Unicode'
    },
    'freetype': {
        'package_name': 'freetype',
        'library_names': ['libfreetype.so', 'libfreetype.a'],
        'header_subdir': 'freetype2',
        'jam_name': 'freetype',
        'description': 'FreeType font rendering library'
    },
    'libpng': {
        'package_name': 'libpng',
        'library_names': ['libpng.so', 'libpng.a'],
        'header_subdir': '',
        'jam_name': 'libpng',
        'description': 'PNG image library'
    }
}

def BuildFeatureAttribute(feature_name: str, attribute: str = 'libraries', 
                         architecture: str = 'x86_64') -> Optional[Union[str, List[str]]]:
    """
    Meson equivalent of Jam's BuildFeatureAttribute function.
    
    This function provides the same API as Jam's BuildFeatureAttribute, making
    it easy to port Jam build rules to Meson.
    
    Args:
        feature_name: Name of the feature (zlib, zstd, icu, freetype, etc.)
        attribute: What to return ('libraries', 'headers', 'lib_path', 'link_args')
        architecture: Target architecture (default: x86_64)
    
    Returns:
        Appropriate paths/libraries for the feature, or None if not available
    """
    
    # Ensure cross-tools compatibility first
    detector.ensure_cross_tools_compatibility(architecture)
    
    if feature_name not in JAM_BUILDFEATURE_DEFINITIONS:
        print(f"Warning: Unknown build feature '{feature_name}'")
        return None
    
    definition = JAM_BUILDFEATURE_DEFINITIONS[feature_name]
    package_name = definition['package_name']
    
    # Get package information from detector
    package_info = detector.get_package_info(package_name, architecture)
    if not package_info:
        return None
    
    if attribute == 'headers':
        headers_path = package_info['headers_path']
        if not headers_path:
            return None
        
        # Add subdir if specified
        header_subdir = definition.get('header_subdir', '')
        if header_subdir:
            full_path = Path(headers_path) / header_subdir
            return str(full_path) if full_path.exists() else headers_path
        return headers_path
        
    elif attribute == 'lib_path':
        return package_info['lib_path']
        
    elif attribute == 'libraries':
        lib_path = package_info['lib_path']
        if not lib_path:
            return None
        
        # Find actual library files
        lib_dir = Path(lib_path)
        found_libs = []
        
        for lib_name in definition['library_names']:
            lib_file = lib_dir / lib_name
            if lib_file.exists():
                found_libs.append(str(lib_file))
        
        return found_libs if found_libs else None
        
    elif attribute == 'link_args':
        lib_path = package_info['lib_path']
        if not lib_path:
            return []
        
        # Generate -L and -l flags for linking
        args = [f"-L{lib_path}"]
        
        # Add -l flags for each library
        for lib_name in definition['library_names']:
            lib_file = Path(lib_path) / lib_name
            if lib_file.exists():
                # Convert libname.so/.a to -lname
                if lib_name.startswith('lib') and (lib_name.endswith('.so') or lib_name.endswith('.a')):
                    if lib_name.endswith('.so'):
                        lib_base = lib_name[3:-3]  # Remove 'lib' prefix and '.so' suffix
                    else:
                        lib_base = lib_name[3:-2]  # Remove 'lib' prefix and '.a' suffix
                    args.append(f"-l{lib_base}")
        
        return args
        
    elif attribute == 'available':
        return package_info is not None
        
    elif attribute == 'version':
        return package_info.get('version', 'unknown')
        
    elif attribute == 'description':
        return definition.get('description', f'{feature_name} library')
    
    return None

def get_available_features(architecture: str = 'x86_64') -> List[str]:
    """
    Get list of available build features (Jam-compatible).
    
    Args:
        architecture: Target architecture
        
    Returns:
        List of available feature names
    """
    available = []
    for feature_name in JAM_BUILDFEATURE_DEFINITIONS.keys():
        if BuildFeatureAttribute(feature_name, 'available', architecture):
            available.append(feature_name)
    return available

def get_feature_info(feature_name: str, architecture: str = 'x86_64') -> Optional[Dict]:
    """
    Get comprehensive information about a build feature.
    
    Args:
        feature_name: Name of the feature
        architecture: Target architecture
        
    Returns:
        Dictionary with all feature information or None if not available
    """
    if not BuildFeatureAttribute(feature_name, 'available', architecture):
        return None
    
    return {
        'name': feature_name,
        'description': BuildFeatureAttribute(feature_name, 'description', architecture),
        'version': BuildFeatureAttribute(feature_name, 'version', architecture),
        'headers_path': BuildFeatureAttribute(feature_name, 'headers', architecture),
        'lib_path': BuildFeatureAttribute(feature_name, 'lib_path', architecture),
        'libraries': BuildFeatureAttribute(feature_name, 'libraries', architecture),
        'link_args': BuildFeatureAttribute(feature_name, 'link_args', architecture)
    }

def generate_meson_dependency(feature_name: str, architecture: str = 'x86_64') -> Optional[str]:
    """
    Generate Meson dependency declaration for a build feature.
    
    Args:
        feature_name: Name of the feature
        architecture: Target architecture
        
    Returns:
        Meson dependency declaration string or None if not available
    """
    info = get_feature_info(feature_name, architecture)
    if not info:
        return None
    
    # Generate Meson declare_dependency() call
    meson_dep = f"{feature_name}_dep = declare_dependency(\n"
    
    if info['headers_path']:
        meson_dep += f"  include_directories: ['{info['headers_path']}'],\n"
    
    if info['link_args']:
        link_args_str = "', '".join(info['link_args'])
        meson_dep += f"  link_args: ['{link_args_str}'],\n"
    
    meson_dep += f"  version: '{info['version']}'\n"
    meson_dep += ")"
    
    return meson_dep

def write_meson_dependencies_file(output_file: str = 'build_features.meson', 
                                 architecture: str = 'x86_64') -> bool:
    """
    Write a Meson file with all available build feature dependencies.
    
    Args:
        output_file: Path to output file
        architecture: Target architecture
        
    Returns:
        True if successful, False otherwise
    """
    try:
        available_features = get_available_features(architecture)
        
        with open(output_file, 'w') as f:
            f.write("# Auto-generated Haiku build feature dependencies\n")
            f.write(f"# Architecture: {architecture}\n")
            f.write("# Generated by BuildFeatures.py\n\n")
            
            for feature in available_features:
                dep = generate_meson_dependency(feature, architecture)
                if dep:
                    f.write(f"# {feature.upper()}\n")
                    f.write(dep)
                    f.write("\n\n")
        
        print(f"Generated {output_file} with {len(available_features)} features")
        return True
        
    except Exception as e:
        print(f"Error writing dependencies file: {e}")
        return False

# For backward compatibility with existing code
def build_feature_attribute(feature_name: str, attribute: str = 'libraries', 
                           architecture: str = 'x86_64') -> Optional[Union[str, List[str]]]:
    """Alias for BuildFeatureAttribute (backward compatibility)"""
    return BuildFeatureAttribute(feature_name, attribute, architecture)

# Test and example usage
if __name__ == '__main__':
    arch = 'x86_64'
    
    print("=== Haiku Build Features (Jam-compatible) ===")
    print(f"Architecture: {arch}")
    print()
    
    # Ensure cross-tools are compatible
    print("Ensuring cross-tools compatibility...")
    if detector.ensure_cross_tools_compatibility(arch):
        print("✅ Cross-tools are compatible")
    else:
        print("❌ Cross-tools compatibility issues")
    print()
    
    # Show available features
    available = get_available_features(arch)
    if not available:
        print("❌ No build features found!")
        print("Make sure build packages are extracted and cross-tools are built.")
        sys.exit(1)
    
    print(f"Available build features ({len(available)}):")
    for feature in available:
        info = get_feature_info(feature, arch)
        if info:
            print(f"  📦 {feature}: v{info['version']} - {info['description']}")
            print(f"     Headers: {info['headers_path']}")
            print(f"     Libraries: {len(info['libraries']) if info['libraries'] else 0} found")
    print()
    
    # Test BuildFeatureAttribute function (Jam-compatible API)
    print("=== Testing Jam-compatible API ===")
    for feature in available[:3]:  # Test first 3 features
        print(f"\n{feature.upper()}:")
        print(f"  Headers: {BuildFeatureAttribute(feature, 'headers', arch)}")
        print(f"  Libraries: {BuildFeatureAttribute(feature, 'libraries', arch)}")
        print(f"  Link args: {BuildFeatureAttribute(feature, 'link_args', arch)}")
    
    # Generate Meson dependencies file
    print("\n=== Generating Meson dependencies ===")
    if write_meson_dependencies_file('build_features_generated.meson', arch):
        print("✅ Generated build_features_generated.meson")
    else:
        print("❌ Failed to generate dependencies file")