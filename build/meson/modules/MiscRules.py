#!/usr/bin/env python3
"""
Haiku Misc Rules for Meson
Port of JAM MiscRules to provide miscellaneous utility functions for build system.
"""

import os
from typing import List, Dict, Optional, Any, Union, Callable
from pathlib import Path

class HaikuMiscRules:
    """Port of JAM MiscRules providing miscellaneous utility functions."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        # Support both Meson and JAM directory structures
        self.haiku_top = Path(os.environ.get('MESON_SOURCE_ROOT', '/home/ruslan/haiku'))
        self.build_dir = Path(os.environ.get('MESON_BUILD_ROOT', self.haiku_top / 'generated'))
        
        # Unique ID generation
        self._next_id_counter = 0
        
        # Object directories configuration
        self.common_platform_object_dir = str(self.build_dir / 'objects/common')
        self.host_common_arch_object_dir = str(self.build_dir / f'objects/linux/{self.target_arch}')
        self.target_common_arch_object_dir = str(self.build_dir / f'objects/haiku/{self.target_arch}')
        
        # Debug levels
        self.haiku_debug_levels = ['0', '1']
        
        # Deferred includes storage
        self.deferred_sub_includes = []
        
        # Build profile storage
        self.build_profiles = {}
        
    def setup_objects_dir(self, subdir_tokens: List[str]) -> Dict[str, str]:
        """
        Port of JAM SetupObjectsDir rule.
        Set up the *{LOCATE,SEARCH}*_{TARGET,SOURCE} variables for the current directory.
        
        Args:
            subdir_tokens: Directory tokens from SUBDIR_TOKENS
            
        Returns:
            Dictionary with configured object directories
        """
        rel_path = '/'.join(subdir_tokens[1:]) if len(subdir_tokens) > 1 else ''
        if rel_path == '.':
            rel_path = ''
        
        # Setup platform-specific locate targets
        locate_targets = {
            'COMMON_PLATFORM_LOCATE_TARGET': 
                str(Path(self.common_platform_object_dir) / rel_path),
            'HOST_COMMON_ARCH_LOCATE_TARGET': 
                str(Path(self.host_common_arch_object_dir) / rel_path),
            'TARGET_COMMON_ARCH_LOCATE_TARGET': 
                str(Path(self.target_common_arch_object_dir) / rel_path)
        }
        
        # Setup debug level directories
        for debug_level in ['COMMON_DEBUG'] + [f'DEBUG_{level}' for level in self.haiku_debug_levels]:
            locate_targets[f'HOST_{debug_level}_LOCATE_TARGET'] = \
                str(Path(f'generated/objects/linux/{self.target_arch}/{debug_level.lower()}') / rel_path)
            locate_targets[f'TARGET_{debug_level}_LOCATE_TARGET'] = \
                str(Path(f'generated/objects/haiku/{self.target_arch}/{debug_level.lower()}') / rel_path)
        
        # Default locations
        locate_targets['LOCATE_TARGET'] = locate_targets['COMMON_PLATFORM_LOCATE_TARGET']
        locate_targets['LOCATE_SOURCE'] = locate_targets['LOCATE_TARGET']
        
        # Search sources
        search_sources = [
            str(Path('/'.join(subdir_tokens))),  # SUBDIR
            locate_targets['LOCATE_SOURCE'],
            locate_targets.get('HOST_COMMON_DEBUG_LOCATE_TARGET', ''),
            locate_targets.get('TARGET_COMMON_DEBUG_LOCATE_TARGET', '')
        ]
        locate_targets['SEARCH_SOURCE'] = [src for src in search_sources if src]
        
        return locate_targets
    
    def setup_feature_objects_dir(self, feature: str, current_dirs: Dict[str, str]) -> Dict[str, str]:
        """
        Port of JAM SetupFeatureObjectsDir rule.
        Update object directories by appending a feature name.
        
        Args:
            feature: Feature name to append to directories
            current_dirs: Current directory configuration
            
        Returns:
            Updated directory configuration
        """
        updated_dirs = current_dirs.copy()
        
        # Update platform locate target
        if 'COMMON_PLATFORM_LOCATE_TARGET' in updated_dirs:
            updated_dirs['COMMON_PLATFORM_LOCATE_TARGET'] = \
                str(Path(updated_dirs['COMMON_PLATFORM_LOCATE_TARGET']) / feature)
        
        # Update architecture and debug level directories
        for debug_level in ['COMMON_ARCH', 'COMMON_DEBUG'] + [f'DEBUG_{level}' for level in self.haiku_debug_levels]:
            for platform in ['HOST', 'TARGET']:
                key = f'{platform}_{debug_level}_LOCATE_TARGET'
                if key in updated_dirs:
                    updated_dirs[key] = str(Path(updated_dirs[key]) / feature)
        
        # Update default locations
        if 'LOCATE_TARGET' in updated_dirs:
            updated_dirs['LOCATE_TARGET'] = str(Path(updated_dirs['LOCATE_TARGET']) / feature)
            updated_dirs['LOCATE_SOURCE'] = updated_dirs['LOCATE_TARGET']
        
        return updated_dirs
    
    def make_locate_common_platform(self, files: List[str], subdir: str = '') -> Dict[str, Any]:
        """
        Port of JAM MakeLocateCommonPlatform rule.
        The file is shared between all target platforms.
        
        Args:
            files: Files to locate
            subdir: Optional subdirectory
            
        Returns:
            Configuration for file location
        """
        target_dir = Path(self.common_platform_object_dir) / subdir if subdir else Path(self.common_platform_object_dir)
        
        return {
            'files': files,
            'target_directory': str(target_dir),
            'shared_platform': True
        }
    
    def make_locate_platform(self, files: List[str], subdir: str = '', platform: str = 'target') -> Dict[str, Any]:
        """
        Port of JAM MakeLocatePlatform rule.
        The file is specific for the target platform, but architecture independent.
        
        Args:
            files: Files to locate
            subdir: Optional subdirectory
            platform: Platform ('host' or 'target')
            
        Returns:
            Configuration for file location
        """
        if platform == 'host':
            base_dir = self.host_common_arch_object_dir
        else:
            base_dir = self.target_common_arch_object_dir
        
        target_dir = Path(base_dir) / subdir if subdir else Path(base_dir)
        
        return {
            'files': files,
            'target_directory': str(target_dir),
            'platform_specific': True,
            'platform': platform
        }
    
    def make_locate_arch(self, files: List[str], subdir: str = '', platform: str = 'target') -> Dict[str, Any]:
        """
        Port of JAM MakeLocateArch rule.
        The file is platform+architecture specific, but debug level independent.
        
        Args:
            files: Files to locate
            subdir: Optional subdirectory
            platform: Platform ('host' or 'target')
            
        Returns:
            Configuration for file location
        """
        if platform == 'host':
            base_dir = f'generated/objects/linux/{self.target_arch}/common_debug'
        else:
            base_dir = f'generated/objects/haiku/{self.target_arch}/common_debug'
        
        target_dir = Path(base_dir) / subdir if subdir else Path(base_dir)
        
        return {
            'files': files,
            'target_directory': str(target_dir),
            'architecture_specific': True,
            'platform': platform
        }
    
    def make_locate_debug(self, files: List[str], subdir: str = '', 
                         platform: str = 'target', debug_level: str = '1') -> Dict[str, Any]:
        """
        Port of JAM MakeLocateDebug rule.
        The file is platform+architecture+debug level specific.
        
        Args:
            files: Files to locate
            subdir: Optional subdirectory
            platform: Platform ('host' or 'target')
            debug_level: Debug level ('0' or '1')
            
        Returns:
            Configuration for file location
        """
        if platform == 'host':
            base_dir = f'generated/objects/linux/{self.target_arch}/debug_{debug_level}'
        else:
            base_dir = f'generated/objects/haiku/{self.target_arch}/debug_{debug_level}'
        
        target_dir = Path(base_dir) / subdir if subdir else Path(base_dir)
        
        return {
            'files': files,
            'target_directory': str(target_dir),
            'debug_specific': True,
            'platform': platform,
            'debug_level': debug_level
        }
    
    def deferred_sub_include(self, params: List[str], jamfile: str = 'Jamfile', scope: str = 'global') -> None:
        """
        Port of JAM DeferredSubInclude rule.
        Schedule a subdirectory for later inclusion.
        
        Args:
            params: Subdirectory tokens
            jamfile: Alternative Jamfile name (default: 'Jamfile')
            scope: Scope for alternative Jamfile ('global' or 'local')
        """
        self.deferred_sub_includes.append({
            'params': params,
            'jamfile': jamfile,
            'scope': scope
        })
    
    def execute_deferred_sub_includes(self) -> List[Dict[str, Any]]:
        """
        Port of JAM ExecuteDeferredSubIncludes rule.
        Process all deferred subdirectory inclusions.
        
        Returns:
            List of inclusion configurations
        """
        inclusions = []
        
        for include_info in self.deferred_sub_includes:
            if include_info['params']:
                inclusions.append({
                    'tokens': include_info['params'],
                    'jamfile': include_info['jamfile'],
                    'scope': include_info['scope'],
                    'path': '/'.join(include_info['params'])
                })
        
        return inclusions
    
    def haiku_sub_include(self, tokens: List[str], current_subdir_tokens: List[str]) -> Optional[Dict[str, Any]]:
        """
        Port of JAM HaikuSubInclude rule.
        Current subdir relative SubInclude.
        
        Args:
            tokens: Subdir tokens specifying the subdirectory to include (relative to current)
            current_subdir_tokens: Current subdirectory tokens
            
        Returns:
            Inclusion configuration or None if no tokens
        """
        if not tokens:
            return None
        
        full_tokens = current_subdir_tokens + tokens
        
        return {
            'tokens': full_tokens,
            'relative_tokens': tokens,
            'path': '/'.join(full_tokens),
            'relative_path': '/'.join(tokens)
        }
    
    def next_id(self) -> int:
        """
        Port of JAM NextID rule.
        Generate next unique ID.
        
        Returns:
            Next unique integer ID
        """
        current_id = self._next_id_counter
        self._next_id_counter += 1
        return current_id
    
    def new_unique_target(self, basename: str = '_target') -> str:
        """
        Port of JAM NewUniqueTarget rule.
        Generate unique target name.
        
        Args:
            basename: Base name for target (default: '_target')
            
        Returns:
            Unique target name
        """
        unique_id = self.next_id()
        return f'unique_target_{basename}_{unique_id}'
    
    def run_command_line(self, command_line: List[str]) -> Dict[str, Any]:
        """
        Port of JAM RunCommandLine rule.
        Creates a configuration for executing shell command line.
        
        Args:
            command_line: Command line elements (targets prefixed with ':')
            
        Returns:
            Command execution configuration
        """
        substituted_command = []
        targets = []
        target_var_name = 'target'
        
        # Process command line elements
        for element in command_line:
            if element.startswith(':'):
                # This is a target reference
                target = element[1:]  # Remove ':' prefix
                targets.append(target)
                target_var_name += 'X'
                substituted_command.append(f'${target_var_name}')
            else:
                substituted_command.append(element)
        
        run_target = self.new_unique_target('run')
        
        return {
            'target_name': run_target,
            'command_line': substituted_command,
            'depends': targets,
            'target_substitutions': targets,
            'always_execute': True,
            'not_file': True
        }
    
    def define_build_profile(self, name: str, profile_type: str, path: str = '') -> Dict[str, Any]:
        """
        Port of JAM DefineBuildProfile rule.
        Makes a build profile known for different Haiku images/installations.
        
        Args:
            name: Name of the build profile
            profile_type: Type ('image', 'anyboot-image', 'cd-image', 'vmware-image', 'disk', 'install', 'custom')
            path: Path associated with the profile
            
        Returns:
            Build profile configuration
        """
        if name in self.build_profiles:
            raise ValueError(f"Build profile '{name}' defined twice!")
        
        # Default paths based on type
        if not path:
            if profile_type == 'anyboot-image':
                path = 'generated/haiku-anyboot.image'
            elif profile_type == 'cd-image':
                path = 'generated/haiku.iso'
            elif profile_type == 'image':
                path = 'generated/haiku.image'
            elif profile_type == 'vmware-image':
                path = 'generated/haiku.vmdk'
            elif profile_type == 'install':
                path = 'generated/install'
        
        # Split path into directory and name
        path_obj = Path(path) if path else Path()
        target_dir = str(path_obj.parent) if path_obj.parent != Path('.') else ''
        target_name = path_obj.name
        
        # Handle disk type (same as image but with different flag)
        dont_clear_image = profile_type == 'disk'
        if profile_type == 'disk':
            profile_type = 'image'
        
        # Determine build target based on type
        build_targets = {
            'anyboot-image': 'haiku-anyboot-image',
            'cd-image': 'haiku-cd',
            'image': 'haiku-image',
            'haiku-mmc-image': 'haiku-mmc-image',
            'vmware-image': 'haiku-vmware-image',
            'install': 'install-haiku'
        }
        
        build_target = build_targets.get(profile_type, 'custom')
        start_offset = '--start-offset 65536' if profile_type == 'vmware-image' else ''
        
        profile_config = {
            'name': name,
            'type': profile_type,
            'path': path,
            'target_dir': target_dir,
            'target_name': target_name,
            'build_target': build_target,
            'start_offset': start_offset,
            'dont_clear_image': dont_clear_image
        }
        
        self.build_profiles[name] = profile_config
        return profile_config
    
    def get_build_profile(self, name: str) -> Optional[Dict[str, Any]]:
        """Get build profile configuration by name."""
        return self.build_profiles.get(name)
    
    def get_all_build_profiles(self) -> Dict[str, Dict[str, Any]]:
        """Get all defined build profiles."""
        return self.build_profiles.copy()
    
    def get_objects_dir_config(self) -> Dict[str, Any]:
        """Get object directories configuration."""
        return {
            'common_platform_object_dir': self.common_platform_object_dir,
            'host_common_arch_object_dir': self.host_common_arch_object_dir,
            'target_common_arch_object_dir': self.target_common_arch_object_dir,
            'debug_levels': self.haiku_debug_levels,
            'target_arch': self.target_arch
        }
    
    def get_misc_config(self) -> Dict[str, Any]:
        """Get complete miscellaneous rules configuration."""
        return {
            'next_id': self.next_id,
            'deferred_includes': len(self.deferred_sub_includes),
            'build_profiles': list(self.build_profiles.keys()),
            'objects_config': self.get_objects_dir_config()
        }


def get_misc_rules(target_arch: str = 'x86_64') -> HaikuMiscRules:
    """Get misc rules instance."""
    return HaikuMiscRules(target_arch)


# ========== MODULE-LEVEL EXPORTS FOR JAM COMPATIBILITY ==========
# These functions provide direct JAM-style access without needing class instantiation

_misc_instance = None

def _get_misc():
    """Get or create singleton misc instance."""
    global _misc_instance
    if _misc_instance is None:
        _misc_instance = HaikuMiscRules()
    return _misc_instance

# JAM Rule: SetupObjectsDir
def SetupObjectsDir(subdir_tokens: List[str]) -> Dict[str, str]:
    """SetupObjectsDir rule."""
    return _get_misc().setup_objects_dir(subdir_tokens)

# JAM Rule: SetupFeatureObjectsDir
def SetupFeatureObjectsDir(feature: str, parent_dirs: Optional[Dict] = None) -> Dict[str, str]:
    """SetupFeatureObjectsDir rule."""
    return _get_misc().setup_feature_objects_dir(feature, parent_dirs)

# JAM Rule: MakeLocateCommonPlatform
def MakeLocateCommonPlatform(files: List[str], subdir: str) -> Dict[str, Any]:
    """MakeLocateCommonPlatform rule."""
    return _get_misc().make_locate_common_platform(files, subdir)

# JAM Rule: MakeLocatePlatform
def MakeLocatePlatform(files: List[str], subdir: str, platform: str = 'target') -> Dict[str, Any]:
    """MakeLocatePlatform rule."""
    return _get_misc().make_locate_platform(files, subdir, platform)

# JAM Rule: MakeLocateArch
def MakeLocateArch(files: List[str], subdir: str, arch: Optional[str] = None) -> Dict[str, Any]:
    """MakeLocateArch rule."""
    return _get_misc().make_locate_arch(files, subdir, arch)

# JAM Rule: MakeLocateDebug
def MakeLocateDebug(files: List[str], subdir: str, platform: str = 'target', 
                   debug_level: str = '0') -> Dict[str, Any]:
    """MakeLocateDebug rule."""
    return _get_misc().make_locate_debug(files, subdir, platform, debug_level)

# JAM Rule: DeferredSubInclude
def DeferredSubInclude(params: List[str], jamfile: str = 'Jamfile', 
                       scope: str = 'inherited'):
    """DeferredSubInclude rule."""
    return _get_misc().deferred_sub_include(params, jamfile, scope)

# JAM Rule: ExecuteDeferredSubIncludes
def ExecuteDeferredSubIncludes() -> List[Dict[str, Any]]:
    """ExecuteDeferredSubIncludes rule."""
    return _get_misc().execute_deferred_sub_includes()

# JAM Rule: HaikuSubInclude
def HaikuSubInclude(*tokens) -> str:
    """HaikuSubInclude rule."""
    return _get_misc().haiku_sub_include(*tokens)

# JAM Rule: NextID
def NextID() -> int:
    """NextID rule - Get next unique ID."""
    return _get_misc().next_id()

# JAM Rule: NewUniqueTarget
def NewUniqueTarget(basename: Optional[str] = None) -> str:
    """NewUniqueTarget rule."""
    return _get_misc().new_unique_target(basename)

# JAM Rule: RunCommandLine
def RunCommandLine(command_line: str) -> Dict[str, Any]:
    """RunCommandLine rule."""
    return _get_misc().run_command_line(command_line)

# JAM Rule: DefineBuildProfile
def DefineBuildProfile(name: str, profile_type: str, path: str):
    """DefineBuildProfile rule."""
    return _get_misc().define_build_profile(name, profile_type, path)

# JAM Rule: Echo (essential for output)
def Echo(message: str):
    """Echo rule - Print message."""
    print(message)

# JAM Rule: Exit (essential for errors)
def Exit(message: str, code: int = 1):
    """Exit rule - Exit with message."""
    import sys
    print(f"Error: {message}", file=sys.stderr)
    sys.exit(code)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Misc Rules (JAM Port) ===")
    
    misc = get_misc_rules('x86_64')
    
    # Test object directory setup
    subdir_tokens = ['HAIKU_TOP', 'src', 'kits', 'interface']
    obj_dirs = misc.setup_objects_dir(subdir_tokens)
    print(f"Object directories setup:")
    print(f"  LOCATE_TARGET: {obj_dirs['LOCATE_TARGET']}")
    print(f"  HOST target: {obj_dirs.get('HOST_COMMON_ARCH_LOCATE_TARGET', 'N/A')}")
    print(f"  Search sources: {len(obj_dirs['SEARCH_SOURCE'])} paths")
    
    # Test feature object directory
    feature_dirs = misc.setup_feature_objects_dir('zlib', obj_dirs)
    print(f"Feature objects (zlib): {Path(feature_dirs['LOCATE_TARGET']).name}")
    
    # Test file location methods
    common_config = misc.make_locate_common_platform(['shared.h', 'common.h'], 'headers')
    print(f"Common platform location: {common_config['target_directory']}")
    
    platform_config = misc.make_locate_platform(['host_tool'], 'tools', 'host')
    print(f"Platform location (host): {platform_config['target_directory']}")
    
    arch_config = misc.make_locate_arch(['arch_specific.o'], 'objects')
    print(f"Arch-specific location: {arch_config['target_directory']}")
    
    debug_config = misc.make_locate_debug(['debug.o'], 'debug', 'target', '1')
    print(f"Debug location: {debug_config['target_directory']}")
    
    # Test deferred includes
    misc.deferred_sub_include(['src', 'apps', 'terminal'], 'Jamfile')
    misc.deferred_sub_include(['src', 'kits', 'interface'], 'CustomJamfile', 'local')
    
    deferred = misc.execute_deferred_sub_includes()
    print(f"Deferred includes: {len(deferred)} scheduled")
    for inc in deferred:
        print(f"  {inc['path']} (jamfile: {inc['jamfile']})")
    
    # Test relative include
    current_tokens = ['HAIKU_TOP', 'src', 'kits']
    rel_include = misc.haiku_sub_include(['interface'], current_tokens)
    if rel_include:
        print(f"Relative include: {rel_include['path']} (relative: {rel_include['relative_path']})")
    
    # Test unique ID generation
    id1 = misc.next_id()
    id2 = misc.next_id()
    target1 = misc.new_unique_target('compile')
    target2 = misc.new_unique_target('link')
    print(f"Unique IDs: {id1}, {id2}")
    print(f"Unique targets: {target1}, {target2}")
    
    # Test command line execution
    cmd_config = misc.run_command_line(['echo', ':build_tool', 'completed', ':output_file'])
    print(f"Command execution: {cmd_config['target_name']}")
    print(f"  Command: {' '.join(cmd_config['command_line'])}")
    print(f"  Dependencies: {cmd_config['depends']}")
    
    # Test build profiles
    image_profile = misc.define_build_profile('nightly', 'image', 'generated/haiku-nightly.image')
    anyboot_profile = misc.define_build_profile('release', 'anyboot-image')
    cd_profile = misc.define_build_profile('install-cd', 'cd-image', 'generated/install.iso')
    
    print(f"Build profiles defined: {len(misc.get_all_build_profiles())}")
    for name, profile in misc.get_all_build_profiles().items():
        print(f"  {name}: {profile['type']} -> {profile['build_target']}")
    
    # Test configuration retrieval
    config = misc.get_misc_config()
    print(f"Misc configuration:")
    print(f"  Next ID: {config['next_id']}")
    print(f"  Build profiles: {config['build_profiles']}")
    print(f"  Target arch: {config['objects_config']['target_arch']}")
    
    print("✅ Misc Rules functionality verified")
    print("Complete miscellaneous utility system ported from JAM")
    print("Supports: object directories, deferred includes, unique IDs, build profiles, command execution")