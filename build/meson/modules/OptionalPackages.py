#!/usr/bin/env python3
"""
OptionalPackages Module for Haiku Build System
Defines optional packages that can be included in Haiku builds.
Based on JAM OptionalPackages rules.
"""

import os
from typing import Dict, List, Any, Optional

def get_optional_packages(architecture: str = 'x86_64') -> Dict[str, Dict]:
    """
    Get all optional packages definitions.
    
    Returns:
        Dictionary of optional package configurations
    """
    packages = {}
    
    # Development tools
    packages['gcc'] = {
        'name': 'gcc',
        'version': '13.2.0',
        'architecture': architecture,
        'summary': 'GNU Compiler Collection',
        'description': 'C, C++, and other language compilers',
        'category': 'development',
        'provides': ['cmd:gcc', 'cmd:g++', 'cmd:cpp'],
        'requires': ['haiku', 'haiku_devel'],
        'url': 'https://gcc.gnu.org'
    }
    
    packages['git'] = {
        'name': 'git',
        'version': '2.42.0',
        'architecture': architecture,
        'summary': 'Fast, scalable, distributed revision control system',
        'description': 'Git is a free and open source distributed version control system',
        'category': 'development',
        'provides': ['cmd:git'],
        'requires': ['haiku'],
        'url': 'https://git-scm.com'
    }
    
    packages['python3'] = {
        'name': 'python3',
        'version': '3.11.5',
        'architecture': architecture,
        'summary': 'Interpreted, interactive, object-oriented programming language',
        'description': 'Python is a programming language that lets you work quickly',
        'category': 'development',
        'provides': ['cmd:python3', 'cmd:pip3'],
        'requires': ['haiku'],
        'url': 'https://www.python.org'
    }
    
    packages['cmake'] = {
        'name': 'cmake',
        'version': '3.27.6',
        'architecture': architecture,
        'summary': 'Cross-platform make system',
        'description': 'CMake is used to control the software compilation process',
        'category': 'development',
        'provides': ['cmd:cmake', 'cmd:ctest', 'cmd:cpack'],
        'requires': ['haiku'],
        'url': 'https://cmake.org'
    }
    
    packages['ninja'] = {
        'name': 'ninja',
        'version': '1.11.1',
        'architecture': architecture,
        'summary': 'Small build system with focus on speed',
        'description': 'Ninja is a small build system designed for speed',
        'category': 'development',
        'provides': ['cmd:ninja'],
        'requires': ['haiku'],
        'url': 'https://ninja-build.org'
    }
    
    # Text editors
    packages['vim'] = {
        'name': 'vim',
        'version': '9.0.1894',
        'architecture': architecture,
        'summary': 'Vi IMproved text editor',
        'description': 'Vim is a highly configurable text editor',
        'category': 'editors',
        'provides': ['cmd:vim', 'cmd:vi'],
        'requires': ['haiku'],
        'url': 'https://www.vim.org'
    }
    
    packages['nano'] = {
        'name': 'nano',
        'version': '7.2',
        'architecture': architecture,
        'summary': 'Simple text editor',
        'description': 'GNU nano is a small and simple text editor',
        'category': 'editors',
        'provides': ['cmd:nano'],
        'requires': ['haiku'],
        'url': 'https://www.nano-editor.org'
    }
    
    packages['emacs'] = {
        'name': 'emacs',
        'version': '29.1',
        'architecture': architecture,
        'summary': 'GNU Emacs extensible text editor',
        'description': 'Emacs is the extensible, customizable text editor',
        'category': 'editors',
        'provides': ['cmd:emacs'],
        'requires': ['haiku'],
        'url': 'https://www.gnu.org/software/emacs'
    }
    
    # Media applications
    packages['vlc'] = {
        'name': 'vlc',
        'version': '3.0.18',
        'architecture': architecture,
        'summary': 'Cross-platform multimedia player',
        'description': 'VLC is a free and open source cross-platform multimedia player',
        'category': 'media',
        'provides': ['app:VLC', 'cmd:vlc'],
        'requires': ['haiku', 'ffmpeg'],
        'url': 'https://www.videolan.org/vlc'
    }
    
    packages['audacity'] = {
        'name': 'audacity',
        'version': '3.3.3',
        'architecture': architecture,
        'summary': 'Free audio editor and recorder',
        'description': 'Audacity is an easy-to-use audio editor and recorder',
        'category': 'media',
        'provides': ['app:Audacity'],
        'requires': ['haiku'],
        'url': 'https://www.audacityteam.org'
    }
    
    packages['gimp'] = {
        'name': 'gimp',
        'version': '2.10.34',
        'architecture': architecture,
        'summary': 'GNU Image Manipulation Program',
        'description': 'GIMP is a cross-platform image editor',
        'category': 'media',
        'provides': ['app:GIMP', 'cmd:gimp'],
        'requires': ['haiku'],
        'url': 'https://www.gimp.org'
    }
    
    # Network applications
    packages['openssh'] = {
        'name': 'openssh',
        'version': '9.4p1',
        'architecture': architecture,
        'summary': 'Secure shell client and server',
        'description': 'OpenSSH is the premier connectivity tool for remote login',
        'category': 'network',
        'provides': ['cmd:ssh', 'cmd:sshd', 'cmd:scp', 'cmd:sftp'],
        'requires': ['haiku'],
        'url': 'https://www.openssh.com'
    }
    
    packages['wget'] = {
        'name': 'wget',
        'version': '1.21.4',
        'architecture': architecture,
        'summary': 'Tool for downloading files from the web',
        'description': 'GNU Wget is a free utility for downloading files from the Web',
        'category': 'network',
        'provides': ['cmd:wget'],
        'requires': ['haiku'],
        'url': 'https://www.gnu.org/software/wget'
    }
    
    packages['curl'] = {
        'name': 'curl',
        'version': '8.3.0',
        'architecture': architecture,
        'summary': 'Command line tool for transferring data with URLs',
        'description': 'curl is used in command lines or scripts to transfer data',
        'category': 'network',
        'provides': ['cmd:curl'],
        'requires': ['haiku'],
        'url': 'https://curl.se'
    }
    
    # System utilities
    packages['htop'] = {
        'name': 'htop',
        'version': '3.2.2',
        'architecture': architecture,
        'summary': 'Interactive process viewer',
        'description': 'htop is an interactive process viewer',
        'category': 'utilities',
        'provides': ['cmd:htop'],
        'requires': ['haiku'],
        'url': 'https://htop.dev'
    }
    
    packages['tmux'] = {
        'name': 'tmux',
        'version': '3.3a',
        'architecture': architecture,
        'summary': 'Terminal multiplexer',
        'description': 'tmux enables multiple terminals in a single screen',
        'category': 'utilities',
        'provides': ['cmd:tmux'],
        'requires': ['haiku'],
        'url': 'https://tmux.github.io'
    }
    
    packages['zip'] = {
        'name': 'zip',
        'version': '3.0',
        'architecture': architecture,
        'summary': 'Compression and file packaging utility',
        'description': 'Info-ZIP compression utility',
        'category': 'utilities',
        'provides': ['cmd:zip', 'cmd:zipcloak', 'cmd:zipnote', 'cmd:zipsplit'],
        'requires': ['haiku'],
        'url': 'http://www.info-zip.org/Zip.html'
    }
    
    packages['unzip'] = {
        'name': 'unzip',
        'version': '6.0',
        'architecture': architecture,
        'summary': 'Extraction utility for ZIP archives',
        'description': 'Info-ZIP extraction utility',
        'category': 'utilities',
        'provides': ['cmd:unzip', 'cmd:funzip', 'cmd:unzipsfx'],
        'requires': ['haiku'],
        'url': 'http://www.info-zip.org/UnZip.html'
    }
    
    # Libraries
    packages['boost'] = {
        'name': 'boost',
        'version': '1.83.0',
        'architecture': architecture,
        'summary': 'Portable C++ source libraries',
        'description': 'Boost provides free peer-reviewed portable C++ source libraries',
        'category': 'libraries',
        'provides': ['lib:libboost_system', 'lib:libboost_filesystem'],
        'requires': ['haiku'],
        'url': 'https://www.boost.org'
    }
    
    packages['qt6'] = {
        'name': 'qt6',
        'version': '6.5.2',
        'architecture': architecture,
        'summary': 'Cross-platform application framework',
        'description': 'Qt is a cross-platform application development framework',
        'category': 'libraries',
        'provides': ['lib:libQt6Core', 'lib:libQt6Gui', 'lib:libQt6Widgets'],
        'requires': ['haiku'],
        'url': 'https://www.qt.io'
    }
    
    packages['sdl2'] = {
        'name': 'sdl2',
        'version': '2.28.3',
        'architecture': architecture,
        'summary': 'Simple DirectMedia Layer',
        'description': 'SDL is a cross-platform development library',
        'category': 'libraries',
        'provides': ['lib:libSDL2'],
        'requires': ['haiku'],
        'url': 'https://www.libsdl.org'
    }
    
    return packages


def get_packages_by_category(category: str, architecture: str = 'x86_64') -> List[str]:
    """
    Get optional packages in a specific category.
    
    Args:
        category: Category name (development, editors, media, network, utilities, libraries)
        architecture: Target architecture
        
    Returns:
        List of package names
    """
    packages = get_optional_packages(architecture)
    result = []
    
    for name, info in packages.items():
        if info.get('category') == category:
            result.append(name)
    
    return result


def get_package_info(package_name: str, architecture: str = 'x86_64') -> Optional[Dict]:
    """
    Get information for a specific optional package.
    
    Args:
        package_name: Name of the package
        architecture: Target architecture
        
    Returns:
        Package configuration or None
    """
    packages = get_optional_packages(architecture)
    return packages.get(package_name)


def get_development_packages(architecture: str = 'x86_64') -> List[str]:
    """Get all development-related optional packages."""
    return get_packages_by_category('development', architecture)


def get_media_packages(architecture: str = 'x86_64') -> List[str]:
    """Get all media-related optional packages."""
    return get_packages_by_category('media', architecture)


def get_network_packages(architecture: str = 'x86_64') -> List[str]:
    """Get all network-related optional packages."""
    return get_packages_by_category('network', architecture)


def get_recommended_packages(architecture: str = 'x86_64') -> List[str]:
    """
    Get recommended optional packages for a typical installation.
    
    Args:
        architecture: Target architecture
        
    Returns:
        List of recommended package names
    """
    return [
        'git',
        'python3',
        'vim',
        'openssh',
        'wget',
        'curl',
        'htop',
        'zip',
        'unzip'
    ]


def get_minimal_packages(architecture: str = 'x86_64') -> List[str]:
    """
    Get minimal set of optional packages.
    
    Args:
        architecture: Target architecture
        
    Returns:
        List of minimal package names
    """
    return [
        'vim',
        'openssh',
        'wget'
    ]


def get_package_dependencies(package_name: str, architecture: str = 'x86_64') -> List[str]:
    """
    Get dependencies for an optional package.
    
    Args:
        package_name: Name of the package
        architecture: Target architecture
        
    Returns:
        List of required packages
    """
    package = get_package_info(package_name, architecture)
    if not package:
        return []
    
    return package.get('requires', [])


def check_package_availability(package_name: str, architecture: str = 'x86_64') -> bool:
    """
    Check if an optional package is available.
    
    Args:
        package_name: Name of the package
        architecture: Target architecture
        
    Returns:
        True if package is available
    """
    return package_name in get_optional_packages(architecture)


def create_optional_package_list(profile: str = 'recommended', 
                                architecture: str = 'x86_64') -> List[str]:
    """
    Create a list of optional packages based on profile.
    
    Args:
        profile: Installation profile (minimal, recommended, full)
        architecture: Target architecture
        
    Returns:
        List of package names
    """
    if profile == 'minimal':
        return get_minimal_packages(architecture)
    elif profile == 'recommended':
        return get_recommended_packages(architecture)
    elif profile == 'full':
        return list(get_optional_packages(architecture).keys())
    elif profile == 'development':
        return get_development_packages(architecture)
    else:
        return []


def get_package_size_estimate(package_name: str, architecture: str = 'x86_64') -> int:
    """
    Get estimated size in MB for a package.
    
    Args:
        package_name: Name of the package
        architecture: Target architecture
        
    Returns:
        Estimated size in MB
    """
    # Rough estimates for common packages
    size_estimates = {
        'gcc': 150,
        'git': 25,
        'python3': 45,
        'cmake': 35,
        'vim': 15,
        'emacs': 85,
        'vlc': 65,
        'gimp': 95,
        'qt6': 250,
        'boost': 120,
        'openssh': 5,
        'wget': 2,
        'curl': 3,
        'htop': 1,
        'zip': 1,
        'unzip': 1
    }
    
    return size_estimates.get(package_name, 10)  # Default 10 MB


def calculate_total_size(packages: List[str], architecture: str = 'x86_64') -> int:
    """
    Calculate total estimated size for a list of packages.
    
    Args:
        packages: List of package names
        architecture: Target architecture
        
    Returns:
        Total size in MB
    """
    total = 0
    for package in packages:
        total += get_package_size_estimate(package, architecture)
    return total


# Module exports
def get_all_optional_packages(architecture: str = 'x86_64') -> List[str]:
    """Get all optional package names."""
    return list(get_optional_packages(architecture).keys())


def get_categories() -> List[str]:
    """Get all available categories."""
    return ['development', 'editors', 'media', 'network', 'utilities', 'libraries']


# Test functionality
if __name__ == '__main__':
    arch = 'x86_64'
    
    print("=== Optional Packages ===")
    print(f"Architecture: {arch}")
    print()
    
    # Show packages by category
    for category in get_categories():
        packages = get_packages_by_category(category, arch)
        print(f"{category.capitalize()} ({len(packages)}): {', '.join(packages)}")
    
    print()
    
    # Show profiles
    for profile in ['minimal', 'recommended', 'development']:
        packages = create_optional_package_list(profile, arch)
        size = calculate_total_size(packages, arch)
        print(f"{profile.capitalize()} profile ({len(packages)} packages, ~{size} MB):")
        print(f"  {', '.join(packages[:5])}{'...' if len(packages) > 5 else ''}")
    
    print()
    
    # Test package info
    test_package = 'git'
    info = get_package_info(test_package, arch)
    if info:
        print(f"Package: {test_package}")
        print(f"  Version: {info['version']}")
        print(f"  Category: {info['category']}")
        print(f"  Summary: {info['summary']}")
        print(f"  Size: ~{get_package_size_estimate(test_package, arch)} MB")
        print(f"  Requires: {', '.join(info['requires'])}")