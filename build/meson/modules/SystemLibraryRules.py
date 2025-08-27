#!/usr/bin/env python3
"""
Haiku System Library Rules for Meson
Port of JAM SystemLibraryRules to provide system library paths via BuildFeatures.
"""

from typing import List, Optional, Dict, Any
try:
    from .BuildFeatures import get_build_features
except ImportError:
    import sys
    import os
    sys.path.append(os.path.dirname(__file__))
    from BuildFeatures import get_build_features

class SystemLibrary:
    """Haiku system library resolver using BuildFeatures."""
    
    def __init__(self, target_arch: str = 'x86_64', target_platform: str = 'haiku'):
        self.target_arch = target_arch
        self.target_platform = target_platform
        self.build_features = get_build_features()
        
    def _get_feature_attribute(self, feature: str, library: str, as_path: bool = False) -> Optional[str]:
        """Get library from BuildFeature (equivalent to BuildFeatureAttribute in JAM)."""
        # Use new JAM-compatible BuildFeatures module directly
        from BuildFeatures import BuildFeatureAttribute
        
        libraries = BuildFeatureAttribute(feature, 'libraries', self.target_arch)
        if not libraries:
            return None
            
        # Find matching library
        for lib_path in libraries:
            if library in lib_path:
                if as_path:
                    return lib_path  # Return full path
                else:
                    return library   # Return just library name
        
        return None
    
    def get_libstdcpp_for_image(self) -> List[str]:
        """Returns the c++-standard-library to be put onto the image."""
        # libstdc++.so comes with gcc_syslibs package, so no direct library
        return []
    
    def get_target_libstdcpp(self, as_path: bool = False) -> List[str]:
        """Returns the c++-standard-library for the target."""
        if self.target_platform in ['haiku', 'libbe_test']:
            lib = self._get_feature_attribute('gcc_syslibs', 'libstdc++.so', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_libsupcpp(self, as_path: bool = False) -> List[str]:
        """Returns the c++-support-library for the target (shared version)."""
        if self.target_platform == 'haiku':
            # For shared libraries, use shared libsupc++.so (provides __dso_handle)
            lib = self._get_feature_attribute('gcc_syslibs', 'libsupc++.so', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_static_libsupcpp(self, as_path: bool = False) -> List[str]:
        """Returns the static c++-support-library for the target."""
        if self.target_platform == 'haiku':
            lib = self._get_feature_attribute('gcc_syslibs_devel', 'libsupc++.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_kernel_libsupcpp(self, as_path: bool = False) -> List[str]:
        """Returns the static kernel c++-support-library for the target."""
        if self.target_platform == 'haiku':
            lib = self._get_feature_attribute('gcc_syslibs_devel', 'libsupc++-kernel.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_boot_libsupcpp(self, as_path: bool = False) -> List[str]:
        """Returns the static bootloader c++-support-library for the target."""
        if self.target_platform == 'haiku':
            if self.target_arch == 'x86_64':
                # Special handling for x86_64 boot (would need cross-compiler paths)
                # For now, fallback to kernel version
                lib = self._get_feature_attribute('gcc_syslibs_devel', 'libsupc++-kernel.a', as_path)
            elif self.target_arch == 'arm':
                lib = self._get_feature_attribute('gcc_syslibs_devel', 'libsupc++-boot.a', as_path)
            else:
                lib = self._get_feature_attribute('gcc_syslibs_devel', 'libsupc++-kernel.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_libgcc(self, as_path: bool = False) -> List[str]:
        """Returns the default libgcc(s) for the target (shared + static)."""
        if self.target_platform == 'haiku':
            libs = []
            # Shared libgcc_s first
            shared_lib = self._get_feature_attribute('gcc_syslibs', 'libgcc_s.so.1', as_path)
            if shared_lib:
                libs.append(shared_lib)
            # Static libgcc second
            static_lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc.a', as_path)
            if static_lib:
                libs.append(static_lib)
            return libs
        return []
    
    def get_target_static_libgcc(self, as_path: bool = False) -> List[str]:
        """Returns the static libgcc for the target."""
        if self.target_platform == 'haiku':
            lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_kernel_libgcc(self, as_path: bool = False) -> List[str]:
        """Returns the static kernel libgcc for the target.""" 
        if self.target_platform == 'haiku':
            lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc-kernel.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_boot_libgcc(self, architecture: str = None, as_path: bool = False) -> List[str]:
        """Returns the static bootloader libgcc for the target."""
        arch = architecture or self.target_arch
        if self.target_platform == 'haiku':
            if arch == 'x86_64':
                # Special handling for x86_64 boot (would need cross-compiler paths)
                # For now, fallback to kernel version
                lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc-kernel.a', as_path)
            elif arch == 'arm':
                lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc-boot.a', as_path)
            else:
                lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc-kernel.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_static_libgcceh(self, as_path: bool = False) -> List[str]:
        """Returns the static libgcc_eh for the target."""
        if self.target_platform == 'haiku':
            lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc_eh.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_target_kernel_libgcceh(self, as_path: bool = False) -> List[str]:
        """Returns the static kernel libgcc_eh for the target."""
        if self.target_platform == 'haiku':
            lib = self._get_feature_attribute('gcc_syslibs_devel', 'libgcc_eh-kernel.a', as_path)
            return [lib] if lib else []
        return []
    
    def get_cpp_header_directories(self, architecture: str = None) -> List[str]:
        """Returns the c++ header directories for the given architecture."""
        arch = architecture or self.target_arch
        
        if 'gcc_syslibs_devel' not in self.build_features:
            return []
            
        feature = self.build_features['gcc_syslibs_devel']
        if 'include_dirs' not in feature:
            return []
            
        base_dirs = feature['include_dirs']
        cpp_dirs = []
        
        for base_dir in base_dirs:
            if 'c++' in base_dir or 'include' in base_dir:
                cpp_dirs.extend([
                    base_dir,
                    f"{base_dir}/x86_64-unknown-haiku",  # HAIKU_GCC_MACHINE_<arch>
                    f"{base_dir}/backward",
                    f"{base_dir}/ext"
                ])
        
        return cpp_dirs
    
    def get_gcc_header_directories(self, architecture: str = None) -> List[str]:
        """Returns the gcc header directories for the given architecture."""
        arch = architecture or self.target_arch
        
        if 'gcc_syslibs_devel' not in self.build_features:
            return []
            
        feature = self.build_features['gcc_syslibs_devel']
        if 'include_dirs' not in feature:
            return []
            
        base_dirs = feature['include_dirs']
        gcc_dirs = []
        
        for base_dir in base_dirs:
            if 'gcc' in base_dir or base_dir.endswith('include'):
                gcc_dirs.extend([
                    f"{base_dir}/include",
                    f"{base_dir}/include-fixed"
                ])
        
        return gcc_dirs
    
    def get_system_library_config(self) -> Dict[str, Any]:
        """Generate complete system library configuration for Meson."""
        config = {
            'libstdc++': self.get_target_libstdcpp(as_path=True),
            'libsupc++': self.get_target_libsupcpp(as_path=True),
            'libsupc++_static': self.get_target_static_libsupcpp(as_path=True),
            'libgcc': self.get_target_libgcc(as_path=True),
            'libgcc_static': self.get_target_static_libgcc(as_path=True),
            'libgcc_eh_static': self.get_target_static_libgcceh(as_path=True),
            'cpp_header_dirs': self.get_cpp_header_directories(),
            'gcc_header_dirs': self.get_gcc_header_directories()
        }
        
        return config


def get_system_library(target_arch: str = 'x86_64', target_platform: str = 'haiku') -> SystemLibrary:
    """Get system library resolver instance."""
    return SystemLibrary(target_arch, target_platform)


# ========== MODULE-LEVEL EXPORTS FOR JAM COMPATIBILITY ==========
_syslib_instance = None

def _get_syslib(target_arch: str = 'x86_64', target_platform: str = 'haiku'):
    global _syslib_instance
    if _syslib_instance is None:
        _syslib_instance = SystemLibrary(target_arch, target_platform)
    return _syslib_instance

# JAM Rule: LibstdcppForImage
def LibstdcppForImage() -> List[str]:
    """LibstdcppForImage - Get C++ standard library for image."""
    return _get_syslib().get_libstdcpp_for_image()

# JAM Rule: TargetLibstdcpp
def TargetLibstdcpp(as_path: bool = False) -> List[str]:
    """TargetLibstdcpp - Get target C++ standard library."""
    return _get_syslib().get_target_libstdcpp(as_path)

# JAM Rule: TargetLibsupcpp
def TargetLibsupcpp(as_path: bool = False) -> List[str]:
    """TargetLibsupcpp - Get target C++ support library (shared)."""
    return _get_syslib().get_target_libsupcpp(as_path)

# JAM Rule: TargetStaticLibsupcpp
def TargetStaticLibsupcpp(as_path: bool = False) -> List[str]:
    """TargetStaticLibsupcpp - Get target static C++ support library."""
    return _get_syslib().get_target_static_libsupcpp(as_path)

# JAM Rule: TargetKernelLibsupcpp
def TargetKernelLibsupcpp(as_path: bool = False) -> List[str]:
    """TargetKernelLibsupcpp - Get target kernel C++ support library."""
    return _get_syslib().get_target_kernel_libsupcpp(as_path)

# JAM Rule: TargetBootLibsupcpp
def TargetBootLibsupcpp(as_path: bool = False) -> List[str]:
    """TargetBootLibsupcpp - Get target boot C++ support library."""
    return _get_syslib().get_target_boot_libsupcpp(as_path)

# JAM Rule: TargetLibgcc
def TargetLibgcc(as_path: bool = False) -> List[str]:
    """TargetLibgcc - Get target libgcc."""
    return _get_syslib().get_target_libgcc(as_path)

# JAM Rule: TargetStaticLibgcc
def TargetStaticLibgcc(as_path: bool = False) -> List[str]:
    """TargetStaticLibgcc - Get target static libgcc."""
    return _get_syslib().get_target_static_libgcc(as_path)

# JAM Rule: TargetKernelLibgcc
def TargetKernelLibgcc(as_path: bool = False) -> List[str]:
    """TargetKernelLibgcc - Get target kernel libgcc."""
    return _get_syslib().get_target_kernel_libgcc(as_path)

# JAM Rule: TargetBootLibgcc
def TargetBootLibgcc(architecture: str = None, as_path: bool = False) -> List[str]:
    """TargetBootLibgcc <architecture> - Get target boot libgcc."""
    return _get_syslib().get_target_boot_libgcc(architecture, as_path)

# JAM Rule: TargetStaticLibgcceh
def TargetStaticLibgcceh(as_path: bool = False) -> List[str]:
    """TargetStaticLibgcceh - Get target static libgcc_eh."""
    return _get_syslib().get_target_static_libgcceh(as_path)

# JAM Rule: TargetKernelLibgcceh
def TargetKernelLibgcceh(as_path: bool = False) -> List[str]:
    """TargetKernelLibgcceh - Get target kernel libgcc_eh."""
    return _get_syslib().get_target_kernel_libgcceh(as_path)

# JAM Rule: CppHeaderDirectories
def CppHeaderDirectories(architecture: str = None) -> List[str]:
    """CppHeaderDirectories <architecture> - Get C++ header directories."""
    return _get_syslib().get_cpp_header_directories(architecture)

# JAM Rule: GccHeaderDirectories
def GccHeaderDirectories(architecture: str = None) -> List[str]:
    """GccHeaderDirectories <architecture> - Get GCC header directories."""
    return _get_syslib().get_gcc_header_directories(architecture)


# Test/example usage
if __name__ == '__main__':
    print("=== System Library Configuration ===")
    syslib = get_system_library('x86_64', 'haiku')
    
    config = syslib.get_system_library_config()
    
    for key, value in config.items():
        print(f"{key}: {value}")