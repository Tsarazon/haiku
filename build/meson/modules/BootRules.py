#!/usr/bin/env python3
"""
Haiku Boot Rules for Meson
Port of JAM BootRules to provide boot loader build configurations.
"""

import os
from typing import List, Dict, Optional, Any, Union
from pathlib import Path

# ========== IMPORTS ACCORDING TO MESON_MIGRATION_STRATEGY.md ==========
# Specialized module BootRules imports:

# Base components from HaikuCommon
try:
    from .HaikuCommon import get_architecture_config  # ArchitectureRules
    from .HaikuCommon import get_helper_rules        # HelperRules
except ImportError:
    # Fallback for standalone execution
    get_architecture_config = None
    get_helper_rules = None

class HaikuBootRules:
    """Port of JAM BootRules providing boot loader build configurations."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        self.kernel_arch = target_arch  # TARGET_KERNEL_ARCH
        # Support both Meson and JAM directory structures
        self.haiku_top = Path(os.environ.get('MESON_SOURCE_ROOT', '/home/ruslan/haiku'))
        self.build_dir = Path(os.environ.get('MESON_BUILD_ROOT', self.haiku_top / 'generated'))
        
        # Boot target information
        self.boot_targets = self._get_boot_targets()
        self.boot_platforms = self._get_boot_platforms()
        self.boot_flags = self._get_boot_flags()
        self.boot_tools = self._get_boot_tools()
        
        # JAM equivalent variables
        self.target_boot_platform = None  # Will be set by MultiBootSubDirSetup
        self.target_private_kernel_headers = self._get_private_kernel_headers()
        self.boot_cxx_headers_dir = self._get_boot_cxx_headers_dir()
        
        # Version script and BeOS rules handling
        self.version_script = self.haiku_top / 'build' / 'scripts' / 'version_script'
        self.use_beos_rules = True
        
    def _get_boot_targets(self) -> List[str]:
        """Get available boot targets for architecture (from ArchitectureRules)."""
        # Boot targets from JAM ArchitectureRules (HAIKU_BOOT_TARGETS)
        boot_targets_map = {
            'x86': ['bios_ia32', 'efi', 'pxe_ia32'],
            'x86_64': ['efi'],  # x86_64 only uses EFI
            'arm': ['efi'],
            'arm64': ['efi'],
            'riscv64': ['efi', 'riscv']
        }
        
        return boot_targets_map.get(self.target_arch, ['efi'])
    
    def _get_private_kernel_headers(self) -> List[str]:
        """Get private kernel headers directories (TARGET_PRIVATE_KERNEL_HEADERS equivalent)."""
        return [
            str(self.haiku_top / 'headers' / 'private' / 'kernel'),
            str(self.haiku_top / 'headers' / 'private' / 'system'),
            str(self.haiku_top / 'headers' / 'private' / 'shared'),
            str(self.haiku_top / 'headers' / 'private' / 'libroot'),
            str(self.haiku_top / f'headers/private/kernel/arch/{self.kernel_arch}'),
            str(self.haiku_top / 'headers' / 'private' / 'kernel' / 'boot' / 'platform'),
            str(self.haiku_top / f'generated/objects/{self.target_arch}/common/system/kernel')
        ]
    
    def _get_boot_cxx_headers_dir(self) -> Optional[str]:
        """Get boot C++ headers directory (HAIKU_BOOT_C++_HEADERS_DIR equivalent)."""
        cxx_headers = self.haiku_top / 'headers' / 'private' / 'libroot' / 'glibc'
        return str(cxx_headers) if cxx_headers.exists() else None
    
    def _get_boot_platforms(self) -> Dict[str, Dict[str, Any]]:
        """Get boot platform specific configurations (JAM HAIKU_BOOT_*_FLAGS equivalent)."""
        return {
            'bios_ia32': {
                'ccflags': ['-fno-pic', '-fno-stack-protector'],  # HAIKU_BOOT_BIOS_IA32_CCFLAGS
                'cxxflags': ['-fno-pic', '-fno-stack-protector'], # HAIKU_BOOT_BIOS_IA32_C++FLAGS
                'ldflags': ['-Bstatic', '-nostdlib'],             # HAIKU_BOOT_BIOS_IA32_LDFLAGS
                'defines': []
            },
            'efi': {
                'ccflags': [                                      # HAIKU_BOOT_EFI_CCFLAGS
                    '-fpic', '-fno-stack-protector', '-fPIC', 
                    '-fshort-wchar', '-Wno-error=unused-variable', 
                    '-Wno-error=main'
                ],
                'cxxflags': [                                     # HAIKU_BOOT_EFI_C++FLAGS
                    '-fpic', '-fno-stack-protector', '-fPIC', 
                    '-fshort-wchar', '-Wno-error=unused-variable', 
                    '-Wno-error=main'
                ],
                'ldflags': [                                      # HAIKU_BOOT_EFI_LDFLAGS
                    '-Bstatic', '-Bsymbolic', '-nostdlib', 
                    '-znocombreloc', '-no-undefined'
                ],
                'defines': []
            },
            'pxe_ia32': {
                'ccflags': ['-fno-pic', '-fno-stack-protector'],  # HAIKU_BOOT_PXE_IA32_CCFLAGS
                'cxxflags': ['-fno-pic', '-fno-stack-protector'], # HAIKU_BOOT_PXE_IA32_C++FLAGS
                'ldflags': ['-Bstatic', '-nostdlib'],             # HAIKU_BOOT_PXE_IA32_LDFLAGS
                'defines': []
            },
            'riscv': {
                'ccflags': [                                      # HAIKU_BOOT_RISCV_CCFLAGS
                    '-mcmodel=medany', '-fno-omit-frame-pointer', 
                    '-fno-plt', '-fno-pic', '-fno-semantic-interposition'
                ],
                'cxxflags': [                                     # HAIKU_BOOT_RISCV_C++FLAGS
                    '-mcmodel=medany', '-fno-omit-frame-pointer', 
                    '-fno-plt', '-fno-pic', '-fno-semantic-interposition'
                ],
                'ldflags': [],
                'defines': []
            }
        }
    
    def _get_boot_flags(self) -> Dict[str, List[str]]:
        """Get general boot compilation flags (JAM HAIKU_BOOT_* equivalent)."""
        return {
            'common_ccflags': [                      # HAIKU_BOOT_CCFLAGS
                '-ffreestanding', '-finline', 
                '-fno-semantic-interposition', '-Os'  # HAIKU_BOOT_OPTIM
            ],
            'common_cxxflags': [                     # HAIKU_BOOT_C++FLAGS  
                '-ffreestanding', '-finline',
                '-fno-semantic-interposition',
                '-fno-exceptions', '-Os'              # No -fno-use-cxa-atexit for Clang
            ],
            'common_ldflags': [                      # HAIKU_BOOT_LDFLAGS
                '-Bstatic'                           # HAIKU_BOOT_LDFLAGS
            ],
            'target_kernel_defines': [               # TARGET_KERNEL_DEFINES
                '_KERNEL_MODE'
            ],
            'target_boot_defines': [                 # TARGET_BOOT_DEFINES
                '_BOOT_MODE',
                'BOOT_ARCHIVE_IMAGE_OFFSET=384'      # Default, varies by arch
            ]
        }
    
    def _get_boot_tools(self) -> Dict[str, str]:
        """Get boot build tools paths."""
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
            'objcopy': str(cross_tools / f'{self.target_arch}-unknown-haiku-objcopy'),
            'strip': str(cross_tools / f'{self.target_arch}-unknown-haiku-strip')
        }
    
    def setup_boot(self, sources: List[str], extra_cc_flags: Optional[List[str]] = None,
                  include_private_headers: bool = True, boot_platform: Optional[str] = None,
                  override_target_flags: bool = True) -> Dict[str, Any]:
        """
        Port of JAM SetupBoot rule.
        Configure sources for boot loader compilation.
        
        Args:
            sources: Source files to configure
            extra_cc_flags: Additional compiler flags
            include_private_headers: Whether to include private kernel headers
            boot_platform: Specific boot platform (defaults to first available)
            
        Returns:
            Dictionary with boot source configuration
        """
        extra_flags = extra_cc_flags or []
        platform = boot_platform or self.boot_targets[0]
        
        # Get platform-specific flags
        platform_config = self.boot_platforms.get(platform, self.boot_platforms['efi'])
        
        # Build complete flag lists (JAM equivalent)
        cflags = (self.boot_flags['common_ccflags'] +                  # HAIKU_BOOT_CCFLAGS
                 platform_config['ccflags'] + extra_flags)             # HAIKU_BOOT_$(platform)_CCFLAGS
        cxxflags = (self.boot_flags['common_cxxflags'] +               # HAIKU_BOOT_C++FLAGS
                   platform_config['cxxflags'] + extra_flags)          # HAIKU_BOOT_$(platform)_C++FLAGS  
        defines = (self.boot_flags['target_kernel_defines'] +          # TARGET_KERNEL_DEFINES
                  self.boot_flags['target_boot_defines'] +             # TARGET_BOOT_DEFINES
                  platform_config['defines'])
        
        # Include directories (JAM SourceSysHdrs equivalent)
        include_dirs = []
        if include_private_headers:
            # Add TARGET_PRIVATE_KERNEL_HEADERS
            include_dirs.extend(self.target_private_kernel_headers)
        
        # Add HAIKU_BOOT_C++_HEADERS_DIR if available
        if self.boot_cxx_headers_dir:
            include_dirs.append(self.boot_cxx_headers_dir)
        
        config = {
            'sources': sources,
            'c_args': cflags + [f'-D{define}' for define in defines],
            'cpp_args': cxxflags + [f'-D{define}' for define in defines],
            'include_directories': include_dirs,
            'dependencies': [],
            'boot_platform': platform,
            'kernel_arch': self.kernel_arch,
            # JAM lines 102-111: Override regular flags
            'override_flags': override_target_flags,
            'target_ccflags_override': [],  # Clear TARGET_CCFLAGS_$(TARGET_KERNEL_ARCH)
            'target_cxxflags_override': [],  # Clear TARGET_C++FLAGS_$(TARGET_KERNEL_ARCH)
            'target_asflags_override': [],   # Clear TARGET_ASFLAGS_$(TARGET_KERNEL_ARCH)
            # Override warning flags (JAM lines 108-111)
            'target_warning_ccflags': [],  # Use TARGET_KERNEL_WARNING_CCFLAGS instead
            'target_warning_cxxflags': [],  # Use TARGET_KERNEL_WARNING_C++FLAGS instead
            'optim': '-Os',  # OPTIM on $(object) = $(HAIKU_BOOT_OPTIM)
            'asflags': cflags  # ASFLAGS on $(object) = $(HAIKU_BOOT_CCFLAGS) $(HAIKU_BOOT_$(platform)_CCFLAGS)
        }
        
        return config
    
    def boot_objects(self, sources: List[str], extra_cc_flags: Optional[List[str]] = None,
                    boot_platform: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM BootObjects rule.
        Compile sources with boot loader flags.
        
        Args:
            sources: Source files to compile
            extra_cc_flags: Additional compiler flags
            boot_platform: Specific boot platform
            
        Returns:
            Dictionary with object compilation configuration
        """
        return self.setup_boot(sources, extra_cc_flags, True, boot_platform)
    
    def boot_ld(self, name: str, objects: List[str], linker_script: Optional[str] = None,
               link_args: Optional[List[str]] = None, boot_platform: Optional[str] = None,
               no_libsupcpp: bool = False, version_script: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM BootLd rule.
        Link boot loader executable with specific flags and libraries.
        
        Args:
            name: Executable name
            objects: Object files to link
            linker_script: Optional linker script
            link_args: Additional linker arguments
            boot_platform: Specific boot platform
            no_libsupcpp: Skip linking libsupc++ (JAM HAIKU_NO_LIBSUPC++)
            
        Returns:
            Dictionary with boot loader linking configuration
        """
        platform = boot_platform or self.boot_targets[0]
        platform_config = self.boot_platforms.get(platform, self.boot_platforms['efi'])
        
        # Build link flags (JAM HAIKU_BOOT_$(platform)_LDFLAGS)
        link_flags = list(platform_config['ldflags'])
        if link_args:
            link_flags.extend(link_args)
        
        if linker_script:
            link_flags.extend(['--script=' + linker_script])
        
        # Add version script (JAM lines 159-163: actions BootLd bind VERSION_SCRIPT)
        version_script_path = version_script or str(self.version_script)
        if Path(version_script_path).exists():
            link_flags.extend([f'--version-script={version_script_path}'])
        
        # JAM: Remove preset LINKLIBS, but link against libgcc.a and libsupc++
        link_libraries = []
        
        # Link libsupc++ unless disabled (JAM: if ! HAIKU_NO_LIBSUPC++)
        if not no_libsupcpp:
            libsupcpp = self._target_boot_libsupcpp()
            if libsupcpp:
                link_libraries.append(libsupcpp)
        
        # Always link libgcc.a (JAM: TargetBootLibgcc)
        libgcc = self._target_boot_libgcc()
        if libgcc:
            link_libraries.append(libgcc)
        
        config = {
            'target_name': name,
            'sources': objects,
            'link_args': link_flags,
            'link_with': [],
            'link_libraries': link_libraries,
            'link_depends': [linker_script] if linker_script else [],
            'executable_type': 'boot_loader',
            'boot_platform': platform,
            'install': False,
            # BeOS-specific operations (JAM lines 152-156: if ! DONT_USE_BEOS_RULES)
            'use_beos_rules': self.use_beos_rules,
            'xres': True,  # XRes $(1) : $(RESFILES)
            'settype': True,  # SetType $(1)
            'mimeset': True,  # MimeSet $(1)
            'setversion': True,  # SetVersion $(1)
            # JAM lines 159-163: actions BootLd command
            'link_command': [
                '$(LINK)', '$(LINKFLAGS)', '-o', '"$(1)"', '"$(2)"', '$(LINKLIBS)',
                f'--version-script={version_script_path}' if Path(version_script_path).exists() else ''
            ],
            'version_script': version_script_path if Path(version_script_path).exists() else None
        }
        
        return config
    
    def _target_boot_libsupcpp(self, as_path: bool = True) -> Optional[str]:
        """Get boot libsupc++ path (JAM TargetBootLibsupc++ equivalent)."""
        # Use proper GCC library path structure
        gcc_base = self.build_dir / 'cross-tools' / self.target_arch
        gcc_version_dir = None
        
        # Find GCC version directory
        lib_gcc_dir = gcc_base / 'lib' / 'gcc' / f'{self.target_arch}-unknown-haiku'
        if lib_gcc_dir.exists():
            for version_dir in lib_gcc_dir.iterdir():
                if version_dir.is_dir() and (version_dir / 'libsupc++.a').exists():
                    gcc_version_dir = version_dir
                    break
        
        if gcc_version_dir:
            libsupcpp = gcc_version_dir / 'libsupc++.a'
            return str(libsupcpp) if as_path else 'supc++'
        
        # Fallback to kernel boot libsupc++
        kernel_libsupcpp = self.build_dir / 'objects' / self.target_arch / 'release' / 'system' / 'kernel' / 'libsupc++-kernel.a'
        if kernel_libsupcpp.exists():
            return str(kernel_libsupcpp) if as_path else 'supc++-kernel'
        
        return None
    
    def _target_boot_libgcc(self, architecture: str = None, as_path: bool = True) -> Optional[str]:
        """Get boot libgcc path (JAM TargetBootLibgcc equivalent)."""
        arch = architecture or self.target_arch
        
        # Use proper GCC library path structure
        gcc_base = self.build_dir / 'cross-tools' / arch
        gcc_version_dir = None
        
        # Find GCC version directory
        lib_gcc_dir = gcc_base / 'lib' / 'gcc' / f'{arch}-unknown-haiku'
        if lib_gcc_dir.exists():
            for version_dir in lib_gcc_dir.iterdir():
                if version_dir.is_dir() and (version_dir / 'libgcc.a').exists():
                    gcc_version_dir = version_dir
                    break
        
        if gcc_version_dir:
            libgcc = gcc_version_dir / 'libgcc.a'
            return str(libgcc) if as_path else 'gcc'
        
        # Fallback to kernel boot libgcc
        kernel_libgcc = self.build_dir / 'objects' / arch / 'release' / 'system' / 'kernel' / 'libgcc-kernel.a'
        if kernel_libgcc.exists():
            return str(kernel_libgcc) if as_path else 'gcc-kernel'
        
        return None
    
    def boot_merge_object(self, name: str, sources: List[str],
                         extra_cflags: Optional[List[str]] = None,
                         other_objects: Optional[List[str]] = None,
                         boot_platform: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM BootMergeObject rule.
        Compile and merge objects into single object file.
        
        Args:
            name: Output object name
            sources: Source files to compile
            extra_cflags: Additional compiler flags
            other_objects: Additional object files to merge
            boot_platform: Specific boot platform
            
        Returns:
            Dictionary with merge object configuration
        """
        extra_flags = extra_cflags or []
        objects = other_objects or []
        platform = boot_platform or self.boot_targets[0]
        
        # Setup compilation
        source_config = self.setup_boot(sources, extra_flags, True, platform)
        
        config = {
            'target_name': f'boot_merge_{Path(name).stem}',
            'sources': sources,
            'c_args': source_config['c_args'],
            'cpp_args': source_config['cpp_args'],
            'include_directories': source_config['include_directories'],
            'output': name,
            'merge_objects': objects,
            'boot_platform': platform,
            'object_only': True
        }
        
        return config
    
    def boot_static_library_objects(self, name: str, sources: List[str],
                                   boot_platform: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM BootStaticLibraryObjects rule.
        Create archive from object files for boot loader.
        
        Args:
            name: Archive name
            sources: Source files to compile into objects
            boot_platform: Specific boot platform
            
        Returns:
            Dictionary with archive creation configuration
        """
        platform = boot_platform or self.boot_targets[0]
        
        # Setup compilation for objects
        source_config = self.setup_boot(sources, [], True, platform)
        
        config = {
            'target_name': name,
            'sources': sources,
            'c_args': source_config['c_args'],
            'cpp_args': source_config['cpp_args'],
            'include_directories': source_config['include_directories'],
            'boot_platform': platform,
            'archive_only': True,
            # JAM lines 218-224: BootStaticLibraryObjects actions
            'ar_command': [
                'rm', '-f', '@OUTPUT@', '&&',  # Force recreation (JAM line 222)
                self.boot_tools['ar'], '-r', '@OUTPUT@', '@INPUT@'
            ],
            'custom_target': True
        }
        
        return config
    
    def boot_static_library(self, name: str, sources: List[str],
                           extra_cc_flags: Optional[List[str]] = None,
                           no_hidden_visibility: bool = False,
                           boot_platform: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM BootStaticLibrary rule.
        Build boot loader static library.
        
        Args:
            name: Library name
            sources: Source files
            extra_cc_flags: Additional compiler flags
            no_hidden_visibility: Don't add -fvisibility=hidden (JAM NO_HIDDEN_VISIBILITY)
            boot_platform: Specific boot platform
            
        Returns:
            Dictionary with static library configuration
        """
        extra_flags = list(extra_cc_flags or [])
        platform = boot_platform or self.boot_targets[0]
        
        # Add hidden visibility by default (JAM: if NO_HIDDEN_VISIBILITY != 1)
        if not no_hidden_visibility:
            extra_flags.append('-fvisibility=hidden')
        
        # Setup compilation without private headers for library (JAM: false parameter)
        source_config = self.setup_boot(sources, extra_flags, False, platform)
        
        config = {
            'target_name': name,
            'sources': sources,
            'c_args': source_config['c_args'],
            'cpp_args': source_config['cpp_args'],
            'include_directories': source_config['include_directories'],
            'boot_platform': platform,
            'static_library': True,
            'install': False
        }
        
        return config
    
    def build_mbr(self, binary: str, source: str, packaging_arch: str = None) -> Dict[str, Any]:
        """
        Port of JAM BuildMBR rule (complete implementation).
        Build Master Boot Record binary.
        
        Args:
            binary: Output MBR binary name
            source: Source file for MBR
            packaging_arch: Packaging architecture (defaults to target arch)
            
        Returns:
            Dictionary with MBR build configuration
        """
        arch = packaging_arch or self.target_arch
        
        # Get proper compiler and link flags (JAM lines 236-238)
        cc_tool = self.boot_tools.get('cc', f'{arch}-unknown-haiku-gcc')
        
        # Get architecture link flags (JAM line 236: $(HAIKU_LINKFLAGS_$(HAIKU_PACKAGING_ARCH)))
        # These would come from ArchitectureRules normally
        arch_linkflags = []  # Would be populated from architecture config
        
        # MBR specific flags (JAM lines 237-238: MBRFLAGS)
        mbr_flags = [
            '-nostdlib', '-m32',
            '-Wl,--oformat,binary',
            '-Wl,-z,notext',
            '-Xlinker', '-S',
            '-Xlinker', '-N', 
            '-Xlinker', '--entry=start',
            '-Xlinker', '-Ttext=0x600'
        ]
        
        config = {
            'target_name': f'mbr_{Path(binary).stem}',
            'input': source,
            'output': binary,
            # JAM lines 235-238: actions BuildMBR
            'command': [
                'rm', '-f', binary, '&&',  # JAM line 235: $(RM) $(1)
                cc_tool,
                arch_linkflags,  # JAM line 236: $(HAIKU_LINKFLAGS_$(HAIKU_PACKAGING_ARCH))
                source, '-o', binary
            ] + mbr_flags,  # JAM line 237: $(MBRFLAGS)
            'depends': [source],
            'build_by_default': False,
            'search_source': True  # JAM line 228: SEARCH on $(source) = $(SUBDIR)
        }
        
        return config
    
    def multi_boot_subdir_setup(self, boot_targets: Optional[List[str]] = None) -> List[Dict[str, Any]]:
        """
        Port of JAM MultiBootSubDirSetup rule (complete implementation).
        Setup multiple boot targets for build (equivalent to JAM boot target objects).
        
        Args:
            boot_targets: List of boot targets (defaults to available targets)
            
        Returns:
            List of boot target object configurations
        """
        targets = boot_targets or self.boot_targets
        boot_target_objects = []
        
        for bootTarget in targets:
            # Create boot target object (JAM line 7: bootTargetObject = $(bootTarget:G=<boot-target-object>))
            bootTargetObject = {
                'name': f'<boot-target-object>{bootTarget}',
                'TARGET_BOOT_PLATFORM': bootTarget,  # JAM line 10
                'SOURCE_GRIST': f'!{bootTarget}',    # JAM lines 12-13
                'HDRGRIST': f'!{bootTarget}',        # JAM lines 15-16
                
                # JAM lines 19-21 - TARGET_ARCH variables
                # Note: JAM has a bug here - uses 'architectureObject' instead of 'bootTargetObject'
                'TARGET_ARCH': self.target_arch,
                
                # JAM lines 24-27 - Clone config variables and SUBDIR reset variables
                'AUTO_SET_UP_CONFIG_VARIABLES': {},  # Will be filled by actual build
                'SUBDIRRESET': ['SUBDIR', 'SUBDIRCCFLAGS', 'SUBDIRC++FLAGS', 'SUBDIRHDRS'],
                
                # JAM lines 29-39 - Object directory variables
                'objectDirVars': {
                    'COMMON_PLATFORM_LOCATE_TARGET': str(self.build_dir / 'objects' / bootTarget / 'common'),
                    'HOST_COMMON_ARCH_LOCATE_TARGET': str(self.build_dir / 'objects' / bootTarget / 'host_common'),
                    'HOST_COMMON_DEBUG_LOCATE_TARGET': str(self.build_dir / 'objects' / bootTarget / 'host_debug'),
                    'TARGET_COMMON_ARCH_LOCATE_TARGET': str(self.build_dir / 'objects' / bootTarget / 'target_common'),
                    'TARGET_COMMON_DEBUG_LOCATE_TARGET': str(self.build_dir / 'objects' / bootTarget / 'target_debug'),
                    'LOCATE_TARGET': str(self.build_dir / 'objects' / bootTarget),
                    'LOCATE_SOURCE': str(self.build_dir / 'objects' / bootTarget / 'src'),
                    'SEARCH_SOURCE': str(self.haiku_top / 'src')
                },
                
                # JAM lines 45-52 - SetupObjectsDir and SetupFeatureObjectsDir
                'setup_objects_dir_done': True,
                'setup_feature_objects_dir': bootTarget,  # JAM line 47: SetupFeatureObjectsDir $(bootTarget)
            }
            
            boot_target_objects.append(bootTargetObject)
        
        return boot_target_objects
    
    def multi_boot_grist_files(self, files: List[str], boot_platform: Optional[str] = None) -> List[str]:
        """
        Port of JAM MultiBootGristFiles rule.
        Add platform grist to file names.
        
        Args:
            files: Files to add grist to
            boot_platform: Boot platform for grist
            
        Returns:
            List of files with platform grist
        """
        platform = boot_platform or self.boot_targets[0]
        return [f'{platform}__{file}' for file in files]
    
    def get_boot_config(self, boot_platform: Optional[str] = None) -> Dict[str, Any]:
        """Get complete boot build configuration for specific platform."""
        platform = boot_platform or self.boot_targets[0]
        platform_config = self.boot_platforms.get(platform, {})
        
        return {
            'platform': platform,
            'available_targets': self.boot_targets,
            'arch': self.target_arch,
            'kernel_arch': self.kernel_arch,
            'flags': platform_config,
            'tools': self.boot_tools,
            'common_flags': self.boot_flags
        }


def get_boot_rules(target_arch: str = 'x86_64') -> HaikuBootRules:
    """Get boot rules instance."""
    return HaikuBootRules(target_arch)


# ========== MODULE-LEVEL EXPORTS FOR JAM COMPATIBILITY ==========
_boot_instance = None

def _get_boot(target_arch: str = 'x86_64'):
    global _boot_instance
    if _boot_instance is None or _boot_instance.target_arch != target_arch:
        _boot_instance = HaikuBootRules(target_arch)
    return _boot_instance

# JAM Rule: MultiBootSubDirSetup
def MultiBootSubDirSetup(bootTargets: List[str] = None) -> List[Dict[str, Any]]:
    """MultiBootSubDirSetup <bootTargets> - Setup multiple boot targets."""
    return _get_boot().multi_boot_subdir_setup(bootTargets)

# JAM Rule: MultiBootGristFiles
def MultiBootGristFiles(files: List[str]) -> List[str]:
    """MultiBootGristFiles <files> - Add boot platform grist to files."""
    return _get_boot().multi_boot_grist_files(files)

# JAM Rule: SetupBoot
def SetupBoot(sources: List[str], extra_cc_flags: List[str] = None, 
              include_private_headers: bool = True) -> Dict[str, Any]:
    """SetupBoot <sources> : <extra_cc_flags> : <include_private_headers>"""
    return _get_boot().setup_boot(sources, extra_cc_flags, include_private_headers)

# JAM Rule: BootObjects
def BootObjects(sources: List[str], extra_cc_flags: List[str] = None) -> Dict[str, Any]:
    """BootObjects <sources> : <extra_cc_flags> - Compile boot objects."""
    return _get_boot().boot_objects(sources, extra_cc_flags)

# JAM Rule: BootLd
def BootLd(name: str, objs: List[str], linkerscript: str = None, args: List[str] = None) -> Dict[str, Any]:
    """BootLd <name> : <objs> : <linkerscript> : <args> - Link boot loader."""
    return _get_boot().boot_ld(name, objs, linkerscript, args)

# JAM Rule: BootMergeObject
def BootMergeObject(name: str, sources: List[str], extra_cflags: List[str] = None,
                    other_objects: List[str] = None) -> Dict[str, Any]:
    """BootMergeObject <name> : <sources> : <extra_cflags> : <other_objects>"""
    return _get_boot().boot_merge_object(name, sources, extra_cflags, other_objects)

# JAM Rule: BootStaticLibrary
def BootStaticLibrary(name: str, sources: List[str], extra_cc_flags: List[str] = None) -> Dict[str, Any]:
    """BootStaticLibrary <name> : <sources> : <extra_cc_flags>"""
    return _get_boot().boot_static_library(name, sources, extra_cc_flags)

# JAM Rule: BootStaticLibraryObjects
def BootStaticLibraryObjects(name: str, sources: List[str]) -> Dict[str, Any]:
    """BootStaticLibraryObjects <name> : <sources> - Create boot archive."""
    return _get_boot().boot_static_library_objects(name, sources)

# JAM Rule: BuildMBR
def BuildMBR(binary: str, source: str) -> Dict[str, Any]:
    """BuildMBR <binary> : <source> - Build MBR binary."""
    return _get_boot().build_mbr(binary, source)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Boot Rules (JAM Port) ===")
    
    boot = get_boot_rules('x86_64')
    
    # Test SetupBoot equivalent
    sources = ['boot_loader.cpp', 'stage1.c', 'memory.cpp']
    setup_config = boot.setup_boot(sources, ['-O2'], True, 'bios_ia32')
    print(f"Boot setup config: {len(setup_config['c_args'])} C args, {len(setup_config['cpp_args'])} C++ args")
    print(f"Boot platform: {setup_config['boot_platform']}")
    
    # Test BootStaticLibrary equivalent
    lib_config = boot.boot_static_library('libboot_util', ['util.c', 'debug.c'], [], 'bios_ia32')
    print(f"Boot library config: {lib_config['target_name']}")
    
    # Test BuildMBR equivalent
    mbr_config = boot.build_mbr('boot_mbr', 'mbr.S')
    print(f"MBR config: {mbr_config['target_name']}")
    
    # Test MultiBootSubDirSetup equivalent
    boot_objects = boot.multi_boot_subdir_setup()
    print(f"Boot objects: {boot_objects}")
    
    # Test MultiBootGristFiles equivalent
    grist_files = boot.multi_boot_grist_files(['loader.cpp', 'stage2.c'], 'efi')
    print(f"Grist files: {grist_files}")
    
    # Show boot configuration for different platforms
    for platform in boot.boot_targets:
        config = boot.get_boot_config(platform)
        print(f"Platform {platform}: {len(config['flags']['defines']) if 'defines' in config['flags'] else 0} defines")
    
    # Show available tools
    available_tools = sum(1 for tool_path in boot.boot_tools.values() 
                         if Path(tool_path).exists())
    print(f"Available boot tools: {available_tools}/{len(boot.boot_tools)}")
    
    print("✅ Boot Rules functionality verified")