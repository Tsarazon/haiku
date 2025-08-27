#!/usr/bin/env python3
"""
Haiku Headers Rules for Meson
Port of JAM HeadersRules to provide include path management.
"""

from typing import List, Dict, Optional, Any, Union
from pathlib import Path

# ========== IMPORTS ACCORDING TO MESON_MIGRATION_STRATEGY.md ==========
# Specialized module HeadersRules imports:

# 1. Base components from HaikuCommon
try:
    from .HaikuCommon import get_architecture_config  # ArchitectureRules
    from .HaikuCommon import get_file_rules          # FileRules
except ImportError:
    # Fallback for standalone execution
    get_architecture_config = None
    get_file_rules = None

# 2. Direct import from BuildFeatures (specialized module)
try:
    # Note: use_build_feature_headers doesn't exist yet, need to create it
    from .BuildFeatures import BuildFeatureAttribute, get_available_features
except ImportError:
    BuildFeatureAttribute = None
    get_available_features = None

class HaikuHeadersRules:
    """Port of JAM HeadersRules providing include path management."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        self.subdir_sys_headers = []  # SUBDIRSYSHDRS equivalent
        self.subdir_headers = []      # SUBDIRHDRS equivalent
        
    def f_includes(self, dirs: List[str], option: str = "-I") -> List[str]:
        """
        Port of JAM FIncludes rule.
        
        Args:
            dirs: Include directories
            option: Include option (default: -I)
            
        Returns:
            List of formatted include arguments
        """
        return [f"{option}{dir}" for dir in dirs]
    
    def f_sys_includes(self, dirs: List[str], option: str = "-isystem") -> List[str]:
        """
        Port of JAM FSysIncludes rule.
        
        Args:
            dirs: System include directories  
            option: System include option (default: -isystem)
            
        Returns:
            List of formatted system include arguments
        """
        return [f"{option}{dir}" for dir in dirs]
    
    def subdir_sys_hdrs(self, dirs: List[str]) -> None:
        """
        Port of JAM SubDirSysHdrs rule.
        Adds directories to the system include search paths for current subdir.
        
        Args:
            dirs: Directories to add to system include paths
        """
        resolved_dirs = [str(Path(dir).resolve()) for dir in dirs]
        self.subdir_sys_headers.extend(resolved_dirs)
    
    def subdir_hdrs(self, dirs: List[str]) -> None:
        """
        Port of JAM SubDirHdrs rule.
        Adds directories to the include search paths for current subdir.
        
        Args:
            dirs: Directories to add to include paths
        """
        resolved_dirs = [str(Path(dir).resolve()) for dir in dirs]
        self.subdir_headers.extend(resolved_dirs)
    
    def object_sys_hdrs(self, sources: List[str], dirs: List[str]) -> Dict[str, List[str]]:
        """
        Port of JAM ObjectSysHdrs rule.
        Adds directories to the system include search paths for given sources.
        
        Args:
            sources: Source files or objects
            dirs: System include directories
            
        Returns:
            Dictionary with include configuration for sources
        """
        resolved_dirs = [str(Path(dir).resolve()) for dir in dirs]
        
        config = {}
        for source in sources:
            config[source] = {
                'system_include_directories': resolved_dirs,
                'cpp_args': self.f_sys_includes(resolved_dirs)
            }
            
        return config
    
    def object_hdrs(self, sources: List[str], dirs: List[str]) -> Dict[str, List[str]]:
        """
        Port of JAM ObjectHdrs rule.
        Adds directories to the include search paths for given sources.
        
        Args:
            sources: Source files or objects
            dirs: Include directories
            
        Returns:
            Dictionary with include configuration for sources
        """
        resolved_dirs = [str(Path(dir).resolve()) for dir in dirs]
        
        config = {}
        for source in sources:
            config[source] = {
                'include_directories': resolved_dirs,
                'cpp_args': self.f_includes(resolved_dirs)
            }
            
        return config
    
    def source_sys_hdrs(self, sources: List[str], dirs: List[str]) -> Dict[str, List[str]]:
        """
        Port of JAM SourceSysHdrs rule.
        Adds system header directories to source files.
        
        Args:
            sources: Source files
            dirs: System header directories
            
        Returns:
            Dictionary with system include configuration
        """
        return self.object_sys_hdrs(sources, dirs)
    
    def use_private_headers(self, sources: List[str], subsystems: List[str]) -> Dict[str, List[str]]:
        """
        Port of JAM UsePrivateHeaders rule equivalent.
        Adds private header paths for Haiku subsystems.
        
        Args:
            sources: Source files
            subsystems: List of subsystems (app, interface, kernel, etc.)
            
        Returns:
            Dictionary with private header configuration
        """
        # Map common Haiku subsystems to their private header directories
        haiku_root = Path('/home/ruslan/haiku')
        private_header_map = {
            'app': haiku_root / 'headers' / 'private' / 'app',
            'interface': haiku_root / 'headers' / 'private' / 'interface', 
            'kernel': haiku_root / 'headers' / 'private' / 'kernel',
            'locale': haiku_root / 'headers' / 'private' / 'locale',
            'print': haiku_root / 'headers' / 'private' / 'print',
            'shared': haiku_root / 'headers' / 'private' / 'shared',
            'storage': haiku_root / 'headers' / 'private' / 'storage',
            'support': haiku_root / 'headers' / 'private' / 'support',
            'system': haiku_root / 'headers' / 'private' / 'system',
            'binary_compatibility': haiku_root / 'headers' / 'private' / 'binary_compatibility',
            'input': haiku_root / 'headers' / 'private' / 'input',
            'tracker': haiku_root / 'headers' / 'private' / 'tracker'
        }
        
        private_dirs = []
        for subsystem in subsystems:
            if subsystem in private_header_map:
                private_dir = private_header_map[subsystem]
                if private_dir.exists():
                    private_dirs.append(str(private_dir))
        
        return self.object_sys_hdrs(sources, private_dirs)
    
    def get_all_include_directories(self) -> List[str]:
        """Get all configured include directories."""
        return self.subdir_headers + self.subdir_sys_headers
    
    def f_standard_os_headers(self) -> List[str]:
        """
        Port of JAM FStandardOSHeaders rule.
        Returns standard OS header directories.
        """
        haiku_root = Path('/home/ruslan/haiku')
        os_includes = [
            'add-ons', 'add-ons/file_system', 'add-ons/graphics',
            'add-ons/input_server', 'add-ons/registrar', 'add-ons/screen_saver',
            'add-ons/tracker', 'app', 'device', 'drivers', 'game', 'interface',
            'kernel', 'locale', 'media', 'mail', 'midi', 'midi2', 'net',
            'storage', 'support', 'translation'
        ]
        
        dirs = [str(haiku_root / 'headers' / 'os')]
        for include in os_includes:
            dirs.append(str(haiku_root / 'headers' / 'os' / include))
            
        return dirs
    
    def f_standard_headers(self, architecture: str = 'x86_64', language: str = 'C++') -> List[str]:
        """
        Port of JAM FStandardHeaders rule - **CRITICAL MISSING FUNCTION**
        This is what automatically adds ALL base Haiku headers including config!
        
        Args:
            architecture: Target architecture
            language: Programming language (C++ or C)
            
        Returns:
            List of all standard header directories in correct order
        """
        haiku_root = Path('/home/ruslan/haiku')
        headers = []
        
        # CRITICAL: Ordering here is important (JAM comment line 457-458)
        
        if language == 'C++':
            # C++ headers from cross-tools
            cross_tools_root = haiku_root / f'generated/cross-tools-{architecture}'
            cpp_headers = [
                cross_tools_root / f'{architecture}-unknown-haiku/include/c++/13.3.0',
                cross_tools_root / f'{architecture}-unknown-haiku/include/c++/13.3.0/{architecture}-unknown-haiku'
            ]
            for cpp_header in cpp_headers:
                if cpp_header.exists():
                    headers.append(str(cpp_header))
        
        # **CRITICAL**: Use posix headers directory (JAM line 467-468)
        headers.append(str(haiku_root / 'headers' / 'posix'))
        
        # GCC headers from cross-tools (JAM line 470-471)
        gcc_headers = haiku_root / f'generated/cross-tools-{architecture}/lib/gcc/{architecture}-unknown-haiku/13.3.0/include'
        if gcc_headers.exists():
            headers.append(str(gcc_headers))
        
        # **CRITICAL**: Use headers/config directory for HaikuConfig.h FIRST!
        # Must be BEFORE headers/build to avoid build vs system types conflict
        headers.append(str(haiku_root / 'headers' / 'config'))
        
        # **CRITICAL**: Use headers directory root (JAM line 473-474)
        # This allows includes like <posix/string.h> AND <config/HaikuConfig.h>!
        headers.append(str(haiku_root / 'headers'))
        
        # Public OS headers (JAM line 476-477)  
        headers.extend(self.f_standard_os_headers())
        
        # Private headers root (JAM line 479-480)
        headers.append(str(haiku_root / 'headers' / 'private'))
        
        return headers
    
    def use_headers(self, dirs: List[str], system: bool = True) -> None:
        """
        Port of JAM UseHeaders rule.
        
        Args:
            dirs: Header directories to add
            system: True for system headers, False for local headers
        """
        if system:
            self.subdir_sys_hdrs(dirs)
        else:
            self.subdir_hdrs(dirs)
    
    def configure_standard_headers(self, architecture: str = 'x86_64', language: str = 'C++') -> None:
        """
        Configure all standard Haiku headers automatically.
        This is the missing piece that JAM does automatically!
        """
        standard_headers = self.f_standard_headers(architecture, language)
        self.use_headers(standard_headers, system=True)
    
    def get_meson_include_config(self) -> Dict[str, Any]:
        """
        Generate Meson-compatible include directory configuration.
        
        Returns:
            Dictionary with Meson include configuration
        """
        return {
            'include_directories': self.subdir_headers,
            'system_include_directories': self.subdir_sys_headers,
            'cpp_args': (self.f_includes(self.subdir_headers) + 
                        self.f_sys_includes(self.subdir_sys_headers))
        }


def get_headers_rules(target_arch: str = 'x86_64') -> HaikuHeadersRules:
    """Get headers rules instance."""
    return HaikuHeadersRules(target_arch)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Headers Rules (JAM Port) ===")
    
    headers = get_headers_rules('x86_64')
    
    # Test SubDirSysHdrs
    headers.subdir_sys_hdrs(['src/system/kernel', 'headers/private/kernel'])
    print(f"System headers: {headers.subdir_sys_headers}")
    
    # Test UsePrivateHeaders equivalent
    sources = ['main.cpp', 'utils.cpp']
    private_config = headers.use_private_headers(sources, ['app', 'interface', 'kernel'])
    print(f"Private headers config: {len(private_config)} sources configured")
    
    # Test Meson config generation
    config = headers.get_meson_include_config()
    print(f"Meson config: {len(config['cpp_args'])} compiler args generated")
    
    # **TEST NEW CRITICAL FUNCTIONALITY**
    print("\n=== Testing FStandardHeaders (CRITICAL FIX) ===")
    headers2 = get_headers_rules('x86_64')
    headers2.configure_standard_headers('x86_64', 'C++')
    
    standard_config = headers2.get_meson_include_config()
    print(f"Standard headers configured: {len(standard_config['system_include_directories'])}")
    
    # Check if critical paths are included
    critical_paths = ['/home/ruslan/haiku/headers/config', '/home/ruslan/haiku/headers/posix']
    found_paths = 0
    for path in critical_paths:
        if path in standard_config['system_include_directories']:
            found_paths += 1
    
    print(f"Critical paths found: {found_paths}/{len(critical_paths)}")
    if found_paths == len(critical_paths):
        print("✅ CRITICAL FIX: HaikuConfig.h and types.h paths configured!")
    else:
        print("❌ CRITICAL PATHS MISSING!")
    
    print("✅ Headers Rules functionality verified")