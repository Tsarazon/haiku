#!/usr/bin/env python3
"""
Haiku Kernel Rules for Meson
Port of JAM KernelRules to provide kernel build configurations.
"""

import os
from typing import List, Dict, Optional, Any, Union
from pathlib import Path

# ========== IMPORTS ACCORDING TO MESON_MIGRATION_STRATEGY.md ==========
# Specialized module KernelRules imports:

# 1. Base components from HaikuCommon
try:
    from .HaikuCommon import get_architecture_config  # ArchitectureRules
except ImportError:
    # Fallback for standalone execution
    get_architecture_config = None

# 2. Direct import from BootRules (specialized module)
try:
    from .BootRules import HaikuBootRules
    # Get boot_ld and boot_objects functions
    def get_boot_ld():
        boot_rules = HaikuBootRules()
        return boot_rules.boot_ld
    
    def get_boot_objects():
        boot_rules = HaikuBootRules()
        return boot_rules.boot_objects
except ImportError:
    get_boot_ld = None
    get_boot_objects = None

class HaikuKernelRules:
    """Port of JAM KernelRules providing kernel build configurations."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        # Support both Meson and JAM directory structures
        self.haiku_top = Path(os.environ.get('MESON_SOURCE_ROOT', '/home/ruslan/haiku'))
        self.build_dir = Path(os.environ.get('MESON_BUILD_ROOT', self.haiku_top / 'generated'))
        self.kernel_arch = target_arch  # TARGET_KERNEL_ARCH
        
        # Kernel-specific paths and flags
        self.kernel_headers = self._get_kernel_private_headers()
        self.kernel_flags = self._get_kernel_flags()
        self.kernel_tools = self._get_kernel_tools()
        
    def _get_kernel_private_headers(self) -> List[str]:
        """Get private kernel header directories."""
        return [
            str(self.haiku_top / 'headers' / 'private' / 'kernel'),
            str(self.haiku_top / 'headers' / 'private' / 'system'),
            str(self.haiku_top / 'headers' / 'private' / 'shared'),
        ]
    
    def _get_kernel_flags(self) -> Dict[str, List[str]]:
        """Get kernel-specific compilation flags."""
        return {
            'cflags': [
                '-fno-pic', '-fno-stack-protector', 
                '-fno-stack-clash-protection',
                '-mno-red-zone', '-mcmodel=kernel',
                '-mno-sse', '-mno-mmx', '-mno-sse2', '-mno-3dnow',
                '-fno-asynchronous-unwind-tables'
            ],
            'cxxflags': [
                '-fno-pic', '-fno-stack-protector',
                '-fno-stack-clash-protection', 
                '-mno-red-zone', '-mcmodel=kernel',
                '-mno-sse', '-mno-mmx', '-mno-sse2', '-mno-3dnow',
                '-fno-exceptions', '-fno-rtti',
                '-fno-asynchronous-unwind-tables'
            ],
            'defines': [
                '_KERNEL_MODE=1',
                '_KERNEL_=1',
                'IN_KERNEL=1'
            ],
            'warning_cflags': ['-Wall', '-Wno-multichar'],
            'warning_cxxflags': ['-Wall', '-Wno-multichar', '-Wsign-compare']
        }
    
    def _get_kernel_tools(self) -> Dict[str, str]:
        """Get kernel build tools paths."""
        tools_base = self.haiku_top / 'generated/objects/linux/x86_64/release/tools'
        # Use smart cross-tools detection (prioritizes builddir over generated)
        try:
            from .detect_build_features import get_cross_tools_dir
            cross_tools = get_cross_tools_dir(self.target_arch) / 'bin'
        except ImportError:
            # Fallback to direct import
            try:
                from detect_build_features import get_cross_tools_dir
                cross_tools = get_cross_tools_dir(self.target_arch) / 'bin'
            except ImportError:
                # Ultimate fallback
                cross_tools = self.haiku_top / f'generated/cross-tools-{self.target_arch}/bin'
        
        return {
            'cc': str(cross_tools / f'{self.target_arch}-unknown-haiku-gcc'),
            'cxx': str(cross_tools / f'{self.target_arch}-unknown-haiku-g++'),
            'ld': str(cross_tools / f'{self.target_arch}-unknown-haiku-ld'),
            'ar': str(cross_tools / f'{self.target_arch}-unknown-haiku-ar'),
            'elfedit': str(tools_base / 'elfedit/elfedit'),
            'copyattr': str(tools_base / 'copyattr/copyattr')
        }
    
    def setup_kernel(self, sources: List[str], extra_cc_flags: Optional[List[str]] = None,
                    include_private_headers: bool = True) -> Dict[str, Any]:
        """
        Port of JAM SetupKernel rule.
        Configure sources for kernel compilation.
        
        Args:
            sources: Source files to configure
            extra_cc_flags: Additional compiler flags
            include_private_headers: Whether to include private kernel headers
            
        Returns:
            Dictionary with kernel source configuration
        """
        extra_flags = extra_cc_flags or []
        
        # Build complete flag lists
        cflags = self.kernel_flags['cflags'] + extra_flags
        cxxflags = self.kernel_flags['cxxflags'] + extra_flags
        defines = self.kernel_flags['defines']
        
        # Include directories
        include_dirs = []
        if include_private_headers:
            include_dirs.extend(self.kernel_headers)
        
        config = {
            'sources': sources,
            'c_args': cflags + [f'-D{define}' for define in defines],
            'cpp_args': cxxflags + [f'-D{define}' for define in defines],
            'include_directories': include_dirs,
            'dependencies': [],
            'kernel_arch': self.kernel_arch
        }
        
        return config
    
    def kernel_objects(self, sources: List[str], extra_cc_flags: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM KernelObjects rule.
        Compile sources with kernel flags.
        
        Args:
            sources: Source files to compile
            extra_cc_flags: Additional compiler flags
            
        Returns:
            Dictionary with object compilation configuration
        """
        return self.setup_kernel(sources, extra_cc_flags, True)
    
    def kernel_ld(self, name: str, objects: List[str], linker_script: Optional[str] = None,
                 link_args: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM KernelLd rule.
        Link kernel executable with specific flags.
        
        Args:
            name: Executable name
            objects: Object files to link
            linker_script: Optional linker script
            link_args: Additional linker arguments
            
        Returns:
            Dictionary with kernel linking configuration
        """
        link_flags = link_args or []
        
        if linker_script:
            link_flags.extend(['--script=' + linker_script])
        
        # Standard kernel linking flags
        link_flags.extend([
            '-nostdlib', '-static',
            '-Wl,--no-undefined',
            '-Wl,--build-id=none'
        ])
        
        config = {
            'target_name': name,
            'sources': objects,
            'link_args': link_flags,
            'link_with': [],
            'link_depends': [linker_script] if linker_script else [],
            'executable_type': 'kernel',
            'install': False
        }
        
        return config
    
    def kernel_so(self, target: str, source: str) -> Dict[str, Any]:
        """
        Port of JAM KernelSo rule.
        Create kernel shared object with proper ELF type.
        
        Args:
            target: Target file name
            source: Source file
            
        Returns:
            Dictionary with kernel SO configuration
        """
        copyattr_tool = self.kernel_tools.get('copyattr', 'cp')
        elfedit_tool = self.kernel_tools.get('elfedit', 'elfedit')
        
        commands = [
            f'{copyattr_tool} --data {source} {target}',
            f'{elfedit_tool} --output-type dyn {target}'
        ]
        
        config = {
            'target_name': f'kernel_so_{Path(target).stem}',
            'input': source,
            'output': target,
            'command': ' && '.join(commands),
            'depends': [source],
            'build_by_default': True
        }
        
        return config
    
    def kernel_addon(self, name: str, sources: List[str], 
                    static_libraries: Optional[List[str]] = None,
                    resources: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM KernelAddon rule.
        Build kernel addon (loadable kernel module).
        
        Args:
            name: Addon name
            sources: Source files
            static_libraries: Static libraries to link
            resources: Resource files to include
            
        Returns:
            Dictionary with kernel addon configuration
        """
        libs = static_libraries or []
        resources = resources or []
        
        # Kernel addon specific linking flags
        link_flags = [
            '-shared', '-nostdlib',
            '-Xlinker', '--no-undefined',
            '-Xlinker', f'-soname={name}'
        ]
        
        # Setup source compilation
        source_config = self.setup_kernel(sources, [], False)
        
        config = {
            'target_name': name,
            'sources': sources,
            'c_args': source_config['c_args'],
            'cpp_args': source_config['cpp_args'], 
            'include_directories': source_config['include_directories'],
            'link_args': link_flags,
            'link_with': libs,
            'resources': resources,
            'shared_module': True,
            'kernel_arch': self.kernel_arch
        }
        
        return config
    
    def kernel_merge_object(self, name: str, sources: List[str],
                           extra_cflags: Optional[List[str]] = None,
                           other_objects: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM KernelMergeObject rule.
        Compile and merge objects into single object file.
        
        Args:
            name: Output object name
            sources: Source files to compile
            extra_cflags: Additional compiler flags
            other_objects: Additional object files to merge
            
        Returns:
            Dictionary with merge object configuration
        """
        extra_flags = extra_cflags or []
        objects = other_objects or []
        
        # Setup compilation
        source_config = self.setup_kernel(sources, extra_flags)
        
        config = {
            'target_name': f'merge_{Path(name).stem}',
            'sources': sources,
            'c_args': source_config['c_args'],
            'cpp_args': source_config['cpp_args'],
            'include_directories': source_config['include_directories'],
            'output': name,
            'merge_objects': objects,
            'object_only': True
        }
        
        return config
    
    def kernel_static_library(self, name: str, sources: List[str],
                             extra_cc_flags: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM KernelStaticLibrary rule.
        Build kernel static library.
        
        Args:
            name: Library name
            sources: Source files
            extra_cc_flags: Additional compiler flags
            
        Returns:
            Dictionary with static library configuration
        """
        extra_flags = extra_cc_flags or []
        
        # Add hidden visibility by default
        extra_flags.append('-fvisibility=hidden')
        
        # Setup compilation without private headers for library
        source_config = self.setup_kernel(sources, extra_flags, False)
        
        config = {
            'target_name': name,
            'sources': sources,
            'c_args': source_config['c_args'],
            'cpp_args': source_config['cpp_args'],
            'include_directories': source_config['include_directories'],
            'static_library': True,
            'install': False
        }
        
        return config
    
    def kernel_static_library_objects(self, name: str, objects: List[str]) -> Dict[str, Any]:
        """
        Port of JAM KernelStaticLibraryObjects rule.
        Create static library archive from object files using kernel AR tool.
        
        Args:
            name: Library name
            objects: Object files to archive
            
        Returns:
            Dictionary with static library objects configuration
        """
        ar_tool = self.kernel_tools.get('ar', 'ar')
        
        # Force recreation of archive to avoid stale dependencies
        object_args = ' '.join(f'"{obj}"' for obj in objects)
        commands = [
            f'rm -f "{name}"',
            f'{ar_tool} -r "{name}" {object_args}'
        ]
        
        config = {
            'target_name': f'kernel_lib_objects_{Path(name).stem}',
            'output': name,
            'sources': objects,  # Object files as sources
            'command': ' && '.join(commands),
            'depends': objects,
            'kernel_arch': self.kernel_arch,
            'build_by_default': True
        }
        
        return config
    
    def get_kernel_config(self) -> Dict[str, Any]:
        """Get complete kernel build configuration."""
        return {
            'arch': self.kernel_arch,
            'flags': self.kernel_flags,
            'tools': self.kernel_tools,
            'headers': self.kernel_headers,
            'defines': self.kernel_flags['defines']
        }


def get_kernel_rules(target_arch: str = 'x86_64') -> HaikuKernelRules:
    """Get kernel rules instance."""
    return HaikuKernelRules(target_arch)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Kernel Rules (JAM Port) ===")
    
    kernel = get_kernel_rules('x86_64')
    
    # Test SetupKernel equivalent
    sources = ['kernel_main.c', 'vm.cpp', 'scheduler.c']
    setup_config = kernel.setup_kernel(sources, ['-O2'], True)
    print(f"Kernel setup config: {len(setup_config['c_args'])} C args, {len(setup_config['cpp_args'])} C++ args")
    
    # Test KernelAddon equivalent
    addon_config = kernel.kernel_addon('test_driver', ['driver.c'], ['libutil.a'])
    print(f"Kernel addon config: {addon_config['target_name']}")
    
    # Test KernelStaticLibrary equivalent
    lib_config = kernel.kernel_static_library('libkernel_util', ['util.c', 'debug.c'])
    print(f"Kernel library config: {lib_config['target_name']}")
    
    # Show kernel configuration
    config = kernel.get_kernel_config()
    available_tools = sum(1 for tool_path in config['tools'].values() 
                         if Path(tool_path).exists())
    print(f"Available kernel tools: {available_tools}/{len(config['tools'])}")
    
    print("✅ Kernel Rules functionality verified")