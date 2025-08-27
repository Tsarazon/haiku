#!/usr/bin/env python3
"""
HaikuPackages Module for Haiku Build System
Defines standard Haiku packages and their configurations.
Based on JAM HaikuPackages rules.
"""

import os
from typing import Dict, List, Any, Optional

def get_haiku_packages(architecture: str = 'x86_64') -> Dict[str, Dict]:
    """
    Get all standard Haiku packages definitions.
    
    Returns:
        Dictionary of package configurations
    """
    packages = {}
    
    # System packages
    packages['haiku'] = {
        'name': 'haiku',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'Haiku operating system',
        'description': 'The Haiku operating system',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku={architecture}',
            'cmd:sh',
            'cmd:bash'
        ],
        'requires': []
    }
    
    packages['haiku_devel'] = {
        'name': 'haiku_devel',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'Haiku development files',
        'description': 'Headers and libraries for developing Haiku applications',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku_devel={architecture}'
        ],
        'requires': [
            f'haiku={architecture}'
        ]
    }
    
    packages['haiku_loader'] = {
        'name': 'haiku_loader',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'Haiku boot loader',
        'description': 'Boot loader for the Haiku operating system',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku_loader={architecture}'
        ],
        'requires': []
    }
    
    # Network kit packages
    packages['haiku_netsurf'] = {
        'name': 'haiku_netsurf',
        'version': '3.10',
        'architecture': architecture,
        'summary': 'NetSurf web browser',
        'description': 'Small and fast web browser',
        'packager': 'The Haiku build system',
        'vendor': 'NetSurf Project',
        'licenses': ['MIT', 'GPL v2'],
        'urls': ['https://www.netsurf-browser.org'],
        'provides': [
            f'haiku_netsurf={architecture}',
            'app:NetSurf'
        ],
        'requires': [
            f'haiku={architecture}'
        ]
    }
    
    packages['haiku_webpositive'] = {
        'name': 'haiku_webpositive',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'WebPositive web browser',
        'description': 'Haiku native web browser',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku_webpositive={architecture}',
            'app:WebPositive'
        ],
        'requires': [
            f'haiku={architecture}'
        ]
    }
    
    # Development packages
    packages['haiku_debugger'] = {
        'name': 'haiku_debugger',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'Haiku Debugger',
        'description': 'Graphical debugger for Haiku',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku_debugger={architecture}',
            'app:Debugger',
            'cmd:debug_server'
        ],
        'requires': [
            f'haiku={architecture}',
            f'haiku_devel={architecture}'
        ]
    }
    
    # Media packages
    packages['ffmpeg'] = {
        'name': 'ffmpeg',
        'version': '4.4.1',
        'architecture': architecture,
        'summary': 'Audio and video recording, conversion, and streaming library',
        'description': 'FFmpeg is a complete solution to record, convert and stream audio and video',
        'packager': 'The Haiku build system',
        'vendor': 'FFmpeg Project',
        'licenses': ['LGPL v2.1', 'GPL v2'],
        'urls': ['https://ffmpeg.org'],
        'provides': [
            f'ffmpeg={architecture}',
            'cmd:ffmpeg',
            'cmd:ffplay',
            'cmd:ffprobe'
        ],
        'requires': [
            f'haiku={architecture}'
        ]
    }
    
    # Utilities
    packages['haiku_extras'] = {
        'name': 'haiku_extras',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'Haiku extras',
        'description': 'Additional Haiku applications and utilities',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku_extras={architecture}'
        ],
        'requires': [
            f'haiku={architecture}'
        ]
    }
    
    packages['haiku_welcome'] = {
        'name': 'haiku_welcome',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'Haiku Welcome application',
        'description': 'Welcome and quick tour application for new users',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku_welcome={architecture}',
            'app:Welcome'
        ],
        'requires': [
            f'haiku={architecture}'
        ]
    }
    
    packages['haiku_userguide'] = {
        'name': 'haiku_userguide',
        'version': 'R1~alpha4',
        'architecture': architecture,
        'summary': 'Haiku User Guide',
        'description': 'Documentation for the Haiku operating system',
        'packager': 'The Haiku build system',
        'vendor': 'Haiku Project',
        'licenses': ['MIT'],
        'urls': ['https://www.haiku-os.org'],
        'provides': [
            f'haiku_userguide={architecture}'
        ],
        'requires': [
            f'haiku={architecture}'
        ]
    }
    
    return packages


def get_package_info(package_name: str, architecture: str = 'x86_64') -> Optional[Dict]:
    """
    Get information for a specific package.
    
    Args:
        package_name: Name of the package
        architecture: Target architecture
        
    Returns:
        Package configuration or None
    """
    packages = get_haiku_packages(architecture)
    return packages.get(package_name)


def get_package_dependencies(package_name: str, architecture: str = 'x86_64', 
                            recursive: bool = False) -> List[str]:
    """
    Get dependencies for a package.
    
    Args:
        package_name: Name of the package
        architecture: Target architecture
        recursive: Include transitive dependencies
        
    Returns:
        List of dependency package names
    """
    package = get_package_info(package_name, architecture)
    if not package:
        return []
    
    deps = package.get('requires', [])
    
    if recursive:
        all_deps = set(deps)
        to_process = list(deps)
        
        while to_process:
            dep = to_process.pop(0)
            # Extract package name from requirement
            dep_name = dep.split('=')[0] if '=' in dep else dep
            
            sub_deps = get_package_dependencies(dep_name, architecture, False)
            for sub_dep in sub_deps:
                if sub_dep not in all_deps:
                    all_deps.add(sub_dep)
                    to_process.append(sub_dep)
        
        return list(all_deps)
    
    return deps


def get_package_provides(package_name: str, architecture: str = 'x86_64') -> List[str]:
    """
    Get what a package provides.
    
    Args:
        package_name: Name of the package
        architecture: Target architecture
        
    Returns:
        List of provided features
    """
    package = get_package_info(package_name, architecture)
    if not package:
        return []
    
    return package.get('provides', [])


def get_packages_by_category(category: str, architecture: str = 'x86_64') -> List[str]:
    """
    Get packages in a specific category.
    
    Args:
        category: Category name (system, development, media, network, etc.)
        architecture: Target architecture
        
    Returns:
        List of package names
    """
    categories = {
        'system': ['haiku', 'haiku_loader'],
        'development': ['haiku_devel', 'haiku_debugger'],
        'network': ['haiku_netsurf', 'haiku_webpositive'],
        'media': ['ffmpeg'],
        'utilities': ['haiku_extras', 'haiku_welcome', 'haiku_userguide']
    }
    
    return categories.get(category, [])


def get_core_packages(architecture: str = 'x86_64') -> List[str]:
    """
    Get list of core Haiku packages.
    
    Args:
        architecture: Target architecture
        
    Returns:
        List of core package names
    """
    return [
        'haiku',
        'haiku_devel',
        'haiku_loader',
        'haiku_debugger',
        'haiku_welcome',
        'haiku_userguide'
    ]


def get_optional_packages(architecture: str = 'x86_64') -> List[str]:
    """
    Get list of optional Haiku packages.
    
    Args:
        architecture: Target architecture
        
    Returns:
        List of optional package names
    """
    core = set(get_core_packages(architecture))
    all_packages = set(get_haiku_packages(architecture).keys())
    return list(all_packages - core)


def create_package_spec(package_name: str, install_items: List[Dict], 
                       architecture: str = 'x86_64') -> Dict:
    """
    Create a complete package specification for building.
    
    Args:
        package_name: Name of the package
        install_items: List of items to install
        architecture: Target architecture
        
    Returns:
        Complete package specification
    """
    package_info = get_package_info(package_name, architecture)
    if not package_info:
        package_info = {
            'name': package_name,
            'version': '1.0',
            'architecture': architecture,
            'summary': f'{package_name} package',
            'description': f'Haiku {package_name} package',
            'packager': 'The Haiku build system',
            'vendor': 'Haiku Project',
            'licenses': ['MIT'],
            'urls': ['https://www.haiku-os.org'],
            'provides': [f'{package_name}={architecture}'],
            'requires': []
        }
    
    return {
        'package_info': package_info,
        'install_items': install_items,
        'architecture': architecture,
        'compression_level': 9,
        'output_dir': f'/home/ruslan/haiku/generated/packages/{architecture}'
    }


def validate_package_spec(spec: Dict) -> bool:
    """
    Validate a package specification.
    
    Args:
        spec: Package specification
        
    Returns:
        True if valid
    """
    required_fields = ['package_info', 'install_items', 'architecture']
    
    for field in required_fields:
        if field not in spec:
            print(f"Missing required field: {field}")
            return False
    
    package_info = spec['package_info']
    required_info = ['name', 'version', 'architecture', 'summary', 'description']
    
    for field in required_info:
        if field not in package_info:
            print(f"Missing required package info field: {field}")
            return False
    
    return True


# Module exports
def get_all_packages(architecture: str = 'x86_64') -> List[str]:
    """Get all package names."""
    return list(get_haiku_packages(architecture).keys())


def get_package_version(package_name: str, architecture: str = 'x86_64') -> Optional[str]:
    """Get package version."""
    package = get_package_info(package_name, architecture)
    return package.get('version') if package else None


# Test functionality
if __name__ == '__main__':
    arch = 'x86_64'
    
    print("=== Haiku Packages ===")
    print(f"Architecture: {arch}")
    print()
    
    # List all packages
    all_packages = get_haiku_packages(arch)
    print(f"Total packages: {len(all_packages)}")
    print()
    
    # Show packages by category
    for category in ['system', 'development', 'network', 'media', 'utilities']:
        packages = get_packages_by_category(category, arch)
        print(f"{category.capitalize()}: {', '.join(packages)}")
    
    print()
    
    # Show core vs optional
    core = get_core_packages(arch)
    optional = get_optional_packages(arch)
    print(f"Core packages ({len(core)}): {', '.join(core[:3])}...")
    print(f"Optional packages ({len(optional)}): {', '.join(optional[:3])}...")
    
    print()
    
    # Test package info
    test_package = 'haiku_devel'
    info = get_package_info(test_package, arch)
    if info:
        print(f"Package: {test_package}")
        print(f"  Version: {info['version']}")
        print(f"  Summary: {info['summary']}")
        print(f"  Requires: {', '.join(info['requires']) if info['requires'] else 'none'}")
        print(f"  Provides: {', '.join(info['provides'][:2])}...")