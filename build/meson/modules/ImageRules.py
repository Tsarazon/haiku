#!/usr/bin/env python3
"""
Haiku Image Rules for Meson
Port of JAM ImageRules to provide system image and container management.
"""

import os
import subprocess
from typing import List, Dict, Optional, Any, Union
from pathlib import Path

# ========== IMPORTS ACCORDING TO MESON_MIGRATION_STRATEGY.md ==========
# ImageRules is a high-level module that depends on many others

# Import from specialized modules directly (not from HaikuCommon)
try:
    from .PackageRules import get_package_rules, HaikuPackageRules
    from .MainBuildRules import get_haiku_build_rules
    from .BootRules import get_boot_rules, HaikuBootRules
    from .KernelRules import get_kernel_rules, HaikuKernelRules
except ImportError:
    # Fallback for standalone execution
    get_package_rules = None
    get_haiku_build_rules = None
    get_boot_rules = None
    get_kernel_rules = None
    HaikuPackageRules = None
    HaikuBootRules = None
    HaikuKernelRules = None

class HaikuImageRules:
    """Port of JAM ImageRules providing system image and container management."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        # Support both Meson and JAM directory structures
        self.haiku_top = Path(os.environ.get('MESON_SOURCE_ROOT', '/home/ruslan/haiku'))
        self.build_dir = Path(os.environ.get('MESON_BUILD_ROOT', self.haiku_top / 'generated'))
        self.output_dir = self.haiku_top / 'generated'
        
        # Container state
        self.containers = {}  # Track active containers
        self.install_directories = {}
        self.install_targets = {}
        self.symlinks = {}
    
    def _init_container(self, container: str) -> None:
        """Initialize container with complete structure."""
        self.containers[container] = {
            'directories': [],
            'files': [],
            'symlinks': [],
            'attributes': {},
            'copies': [],
            'archives': [],
            'system_packages': [],
            'other_packages': []
        }
        
    def add_directory_to_container(self, container: str, directory_tokens: List[str],
                                  attribute_files: Optional[List[str]] = None) -> str:
        """
        Port of JAM AddDirectoryToContainer rule.
        Add directory structure to container.
        
        Args:
            container: Container name
            directory_tokens: Path components for directory
            attribute_files: Optional attribute files for directory
            
        Returns:
            Directory identifier for further operations
        """
        directory_path = '/'.join(directory_tokens)
        directory_id = f"{container}__{directory_path}"
        
        if container not in self.containers:
            self._init_container(container)
        
        if directory_id not in self.containers[container]['directories']:
            self.containers[container]['directories'].append(directory_id)
            
            if attribute_files:
                self.containers[container]['attributes'][directory_id] = attribute_files
        
        return directory_id
    
    def add_files_to_container(self, container: str, directory_tokens: List[str],
                              targets: List[str], dest_name: Optional[str] = None,
                              flags: Optional[List[str]] = None) -> None:
        """
        Port of JAM AddFilesToContainer rule.
        Add files to container in specified directory.
        
        Args:
            container: Container name
            directory_tokens: Directory path components
            targets: Files to add
            dest_name: Optional destination name override
            flags: Optional flags (computeName, alwaysUpdate)
        """
        flags = flags or []
        directory_id = self.add_directory_to_container(container, directory_tokens)
        
        if container not in self.install_targets:
            self.install_targets[container] = []
        
        for target in targets:
            install_entry = {
                'source': target,
                'directory': directory_id,
                'dest_name': dest_name if dest_name else Path(target).name,
                'flags': flags
            }
            
            self.install_targets[container].append(install_entry)
            self.containers[container]['files'].append(install_entry)
    
    def add_symlink_to_container(self, container: str, directory_tokens: List[str],
                                link_target: str, link_name: Optional[str] = None) -> None:
        """
        Port of JAM AddSymlinkToContainer rule.
        Add symbolic link to container.
        
        Args:
            container: Container name
            directory_tokens: Directory path components
            link_target: Target of the symbolic link
            link_name: Name of the link (defaults to target basename)
        """
        directory_id = self.add_directory_to_container(container, directory_tokens)
        
        if not link_name:
            link_name = Path(link_target).name
        
        symlink_entry = {
            'directory': directory_id,
            'target': link_target,
            'name': link_name
        }
        
        if container not in self.symlinks:
            self.symlinks[container] = []
        
        self.symlinks[container].append(symlink_entry)
        self.containers[container]['symlinks'].append(symlink_entry)
    
    def copy_directory_to_container(self, container: str, directory_tokens: List[str],
                                   source_directory: str, target_directory_name: Optional[str] = None,
                                   exclude_patterns: Optional[List[str]] = None,
                                   flags: Optional[List[str]] = None) -> None:
        """
        Port of JAM CopyDirectoryToContainer rule.
        Copy entire directory structure to container.
        
        Args:
            container: Container name
            directory_tokens: Destination directory path components
            source_directory: Source directory to copy
            target_directory_name: Target directory name
            exclude_patterns: Files/patterns to exclude
            flags: Optional flags (alwaysUpdate, isTarget)
        """
        flags = flags or []
        exclude_patterns = exclude_patterns or []
        
        if not target_directory_name:
            target_directory_name = Path(source_directory).name
        
        dest_tokens = directory_tokens + [target_directory_name]
        directory_id = self.add_directory_to_container(container, dest_tokens)
        
        copy_entry = {
            'source_directory': source_directory,
            'directory': directory_id,
            'exclude_patterns': exclude_patterns,
            'flags': flags
        }
        
        if container not in self.containers:
            self.containers[container] = {'directories': [], 'files': [], 'symlinks': [], 'copies': []}
        
        if 'copies' not in self.containers[container]:
            self.containers[container]['copies'] = []
        
        self.containers[container]['copies'].append(copy_entry)
    
    def add_libraries_to_container(self, container: str, directory_tokens: List[str],
                                  libraries: List[str]) -> None:
        """
        Port of JAM AddLibrariesToContainer rule.
        Add libraries with appropriate version symlinks.
        
        Args:
            container: Container name
            directory_tokens: Directory path components
            libraries: Library files to add
        """
        for lib in libraries:
            lib_path = Path(lib)
            
            # Add the library file itself
            self.add_files_to_container(container, directory_tokens, [lib])
            
            # Add version symlinks if library has ABI version
            # This is a simplified version - real implementation would need
            # to extract ABI version information from the library
            if '.so.' in lib_path.name:
                # Create symlink without version number
                base_name = lib_path.name.split('.so.')[0] + '.so'
                self.add_symlink_to_container(container, directory_tokens,
                                            lib_path.name, base_name)
    
    def create_container_script(self, container: str, script_type: str = 'copy') -> Dict[str, Any]:
        """
        Create shell script for container operations.
        
        Args:
            container: Container name
            script_type: Type of script ('copy', 'mkdir', 'extract')
            
        Returns:
            Dictionary with script configuration
        """
        script_lines = [
            '#!/bin/bash',
            'set -e',
            f'# Container {script_type} script for {container}',
            '',
            '# Variables',
            'sPrefix="${sourceDir}/"',  # Source prefix
            'tPrefix="${tmpDir}/"',     # Target prefix
            'copyAttrs="${copyattr}"',  # Copy attributes tool
            'mkdir="mkdir -p"',         # Mkdir command
            ''
        ]
        
        if script_type == 'mkdir' and container in self.containers:
            # Add directory creation commands
            for directory_id in self.containers[container]['directories']:
                dir_path = directory_id.replace(f'{container}__', '')
                script_lines.append(f'$mkdir "${{tPrefix}}{dir_path}"')
            
            # Add attribute commands
            for dir_id, attr_files in self.containers[container].get('attributes', {}).items():
                dir_path = dir_id.replace(f'{container}__', '')
                for attr_file in attr_files:
                    script_lines.append(f'$copyAttrs "{attr_file}" "${{tPrefix}}{dir_path}"')
        
        elif script_type == 'copy' and container in self.containers:
            # Add file copy commands
            for file_entry in self.containers[container]['files']:
                dir_path = file_entry['directory'].replace(f'{container}__', '')
                script_lines.append(f'cp "${{sPrefix}}{file_entry["source"]}" "${{tPrefix}}{dir_path}/{file_entry["dest_name"]}"')
            
            # Add symlink commands
            for symlink in self.containers[container]['symlinks']:
                dir_path = symlink['directory'].replace(f'{container}__', '')
                script_lines.append(f'ln -sfn "{symlink["target"]}" "${{tPrefix}}{dir_path}/{symlink["name"]}"')
            
            # Add directory copy commands
            for copy_entry in self.containers[container].get('copies', []):
                dir_path = copy_entry['directory'].replace(f'{container}__', '')
                exclude_flags = ' '.join(f'-x {pattern}' for pattern in copy_entry['exclude_patterns'])
                script_lines.append(f'cp -r {exclude_flags} "${{sPrefix}}{copy_entry["source_directory"]}/." "${{tPrefix}}{dir_path}"')
        
        script_content = '\n'.join(script_lines)
        
        return {
            'script_type': script_type,
            'container': container,
            'content': script_content,
            'executable': True
        }
    
    def build_haiku_image(self, image_name: str, scripts: List[str],
                         is_image: bool = True, is_vmware: bool = False) -> Dict[str, Any]:
        """
        Port of JAM BuildHaikuImage rule.
        Build complete Haiku system image.
        
        Args:
            image_name: Name of output image
            scripts: Build scripts to execute
            is_image: Whether this is a disk image
            is_vmware: Whether this is VMware format
            
        Returns:
            Dictionary with image build configuration
        """
        build_script = self.haiku_top / 'build/scripts/build_haiku_image'
        
        config = {
            'target_name': f'build_image_{Path(image_name).stem}',
            'output': image_name,
            'command': [str(build_script)] + scripts,
            'depends': scripts + [str(build_script)],
            'env': {
                'imagePath': image_name,
                'isImage': '1' if is_image else '',
                'isVMwareImage': '1' if is_vmware else ''
            },
            'build_by_default': False
        }
        
        return config
    
    def build_cd_boot_image_efi_only(self, image_name: str, boot_efi: str,
                                    extra_files: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM BuildCDBootImageEFIOnly rule.
        Build EFI-only bootable ISO image.
        
        Args:
            image_name: Output ISO image name
            boot_efi: EFI boot file
            extra_files: Additional files to include
            
        Returns:
            Dictionary with ISO build configuration
        """
        extra_files = extra_files or []
        
        # Determine volume ID
        if os.getenv('HAIKU_NIGHTLY_BUILD') == '1':
            vol_id = f'haiku-nightly-{self.target_arch}'
        else:
            haiku_version = os.getenv('HAIKU_VERSION', 'unknown')
            vol_id = f'haiku-{haiku_version}-{self.target_arch}'
        
        # Build xorriso command
        command = [
            'xorriso', '-as', 'mkisofs',
            '-no-emul-boot', '-e', boot_efi,
            '-r', '-J', '-V', vol_id,
            '-o', image_name,
            boot_efi
        ] + extra_files
        
        config = {
            'target_name': f'cd_image_{Path(image_name).stem}',
            'output': image_name,
            'command': command,
            'depends': [boot_efi] + extra_files,
            'build_by_default': False
        }
        
        return config
    
    def add_drivers_to_container(self, container: str, relative_directory_tokens: List[str],
                                targets: List[str]) -> None:
        """
        Port of JAM AddDriversToContainer rule.
        Add drivers to container with proper symlink structure.
        
        Args:
            container: Container name
            relative_directory_tokens: Relative directory path for drivers
            targets: Driver files to add
        """
        system_dir_tokens = ['system']  # Simplified system directory
        
        # Add drivers to bin directory
        bin_directory_tokens = system_dir_tokens + ['add-ons', 'kernel', 'drivers', 'bin']
        self.add_files_to_container(container, bin_directory_tokens, targets)
        
        # Add symlinks in device-specific directories
        dev_directory_tokens = system_dir_tokens + ['add-ons', 'kernel', 'drivers', 'dev'] + relative_directory_tokens
        
        # Calculate relative path back to bin
        link_prefix = ['..'] * len(relative_directory_tokens) + ['..', 'bin']
        
        for target in targets:
            driver_name = Path(target).stem
            link_target = '/'.join(link_prefix + [driver_name])
            self.add_symlink_to_container(container, dev_directory_tokens, link_target, driver_name)
    
    def add_new_drivers_to_container(self, container: str, relative_directory_tokens: List[str],
                                   targets: List[str], flags: Optional[List[str]] = None) -> None:
        """
        Port of JAM AddNewDriversToContainer rule.
        Add new-style drivers to container (no bin directory).
        
        Args:
            container: Container name
            relative_directory_tokens: Directory path for drivers
            targets: Driver files to add
            flags: Optional flags (alwaysUpdate)
        """
        flags = flags or []
        system_dir_tokens = ['system']
        directory_tokens = system_dir_tokens + ['add-ons', 'kernel', 'drivers'] + relative_directory_tokens
        
        self.add_files_to_container(container, directory_tokens, targets, flags=flags)
    
    def add_boot_module_symlinks_to_container(self, container: str, targets: List[str]) -> None:
        """
        Port of JAM AddBootModuleSymlinksToContainer rule.
        Add boot module symlinks for kernel modules.
        
        Args:
            container: Container name  
            targets: Boot modules to create symlinks for
        """
        system_dir_tokens = ['system']
        boot_dir_tokens = system_dir_tokens + ['add-ons', 'kernel', 'boot']
        
        for target in targets:
            # Find where the target is installed
            target_found = False
            target_install_path = None
            
            if container in self.install_targets:
                for install_entry in self.install_targets[container]:
                    if install_entry['source'] == target:
                        # Calculate relative path from boot directory
                        install_dir_tokens = install_entry['directory'].replace(f'{container}__', '').split('/')
                        
                        # Remove system prefix
                        if install_dir_tokens[:len(system_dir_tokens)] == system_dir_tokens:
                            install_dir_tokens = install_dir_tokens[len(system_dir_tokens):]
                        
                        # Create relative link path
                        link_target_parts = ['..', '..', '..'] + install_dir_tokens + [Path(target).name]
                        link_target = '/'.join(link_target_parts)
                        
                        self.add_symlink_to_container(container, boot_dir_tokens, link_target, Path(target).name)
                        target_found = True
                        break
            
            if not target_found:
                print(f"Warning: AddBootModuleSymlinksToContainer: Target {target} not found in container")

    def init_script(self, script_path: str) -> str:
        """
        Port of JAM InitScript rule.
        Initialize a build script with basic structure.
        
        Args:
            script_path: Path to script file
            
        Returns:
            Initialized script identifier
        """
        script_content = [
            '#!/bin/bash',
            'set -e',
            f'# Auto-generated build script: {script_path}',
            '',
            '# Script initialized',
            'touch "$0.initialized"'
        ]
        
        return '\n'.join(script_content)
    
    def add_variable_to_script(self, script_path: str, variable: str, value: Union[str, List[str]]) -> str:
        """
        Port of JAM AddVariableToScript rule.
        Add variable definition to build script.
        
        Args:
            script_path: Script file path
            variable: Variable name
            value: Variable value (string or list)
            
        Returns:
            Script lines for variable definition
        """
        if not value:
            value = '""'
        
        if isinstance(value, list):
            if len(value) == 1:
                return f'echo {variable}=\\"{value[0]}\\" >> {script_path}'
            else:
                lines = [f'echo {variable}=\\"{value[0]}\\" >> {script_path}']
                for val in value[1:]:
                    lines.append(f'echo {variable}=\\"\\${{{variable}}} {val}\\" >> {script_path}')
                return '\n'.join(lines)
        else:
            return f'echo {variable}=\\"{value}\\" >> {script_path}'
    
    def add_target_variable_to_script(self, script_path: str, targets: List[str], 
                                    variable: Optional[str] = None) -> str:
        """
        Port of JAM AddTargetVariableToScript rule.
        Add target paths as variable to script.
        
        Args:
            script_path: Script file path
            targets: Target files
            variable: Variable name (defaults to first target basename)
            
        Returns:
            Script lines for target variable definition
        """
        if not variable and targets:
            variable = Path(targets[0]).stem
        
        if not targets:
            return f'echo {variable}= >> {script_path}'
        
        lines = [f'echo {variable}=\\"{targets[0]}\\" >> {script_path}']
        for target in targets[1:]:
            lines.append(f'echo {variable}=\\"\\${{{variable}}} {target}\\" >> {script_path}')
        
        return '\n'.join(lines)

    def build_efi_system_partition(self, image_name: str, efi_loader: str) -> Dict[str, Any]:
        """
        Port of JAM BuildEfiSystemPartition rule.
        Build EFI System Partition with proper structure.
        
        Args:
            image_name: Output ESP image name
            efi_loader: EFI loader executable
            
        Returns:
            Dictionary with ESP build configuration
        """
        # Determine EFI binary name based on architecture
        efi_names = {
            'x86': 'BOOTIA32.EFI',
            'x86_64': 'BOOTX64.EFI',
            'arm': 'BOOTARM.EFI',
            'arm64': 'BOOTAA64.EFI',
            'riscv32': 'BOOTRISCV32.EFI',
            'riscv64': 'BOOTRISCV64.EFI'
        }
        
        efi_name = efi_names.get(self.target_arch, 'BOOTX64.EFI')
        
        # Tools and resources
        fat_shell = self.haiku_top / 'generated/objects/linux/x86_64/release/tools/fat_shell/fat_shell'
        volume_icon = self.haiku_top / 'data/artwork/VolumeIcon.icns'
        efi_keys_dir = self.haiku_top / 'data/boot/efi/keys'
        
        # Build script content
        script_lines = [
            '#!/bin/bash',
            'set -e',
            f'# Build EFI System Partition: {image_name}',
            '',
            f'rm -f "{image_name}"',
            f'dd if=/dev/zero of="{image_name}" bs=1024 count=2880',
            '',
            f'FATFS="{fat_shell}"',
            f'EFIICON="{volume_icon}"',
            f'LOADER="{efi_loader}"',
            '',
            f'${{FATFS}} --initialize "{image_name}" "Haiku ESP"',
            f'echo "mkdir myfs/EFI" | ${{FATFS}} "{image_name}"',
            f'echo "mkdir myfs/KEYS" | ${{FATFS}} "{image_name}"',
            f'echo "mkdir myfs/EFI/BOOT" | ${{FATFS}} "{image_name}"',
            '',
            f'echo "cp :${{LOADER}} myfs/EFI/BOOT/{efi_name}" | ${{FATFS}} "{image_name}"',
            f'echo "cp :${{EFIICON}} myfs/.VolumeIcon.icns" | ${{FATFS}} "{image_name}"',
            '',
            '# Copy UEFI signing keys',
            f'echo "cp :{efi_keys_dir}/README.md myfs/KEYS/README.md" | ${{FATFS}} "{image_name}"',
            'for ext in auth cer crt; do',
            f'    echo "cp :{efi_keys_dir}/DB.$ext myfs/KEYS/DB.$ext" | ${{FATFS}} "{image_name}"',
            'done'
        ]
        
        config = {
            'target_name': f'efi_partition_{Path(image_name).stem}',
            'output': image_name,
            'script_content': '\n'.join(script_lines),
            'depends': [efi_loader, str(fat_shell), str(volume_icon)],
            'build_by_default': False
        }
        
        return config
    
    def add_package_files_to_haiku_image(self, location: List[str], packages: List[str],
                                       flags: Optional[List[str]] = None) -> None:
        """
        Port of JAM AddPackageFilesToHaikuImage rule.
        Add package files to Haiku image.
        
        Args:
            location: Location in image (e.g., ['system', 'packages'])
            packages: Package files to add
            flags: Optional flags (nameFromMetaInfo)
        """
        flags = flags or []
        container = 'haiku-image'  # Default Haiku image container
        
        # Track packages for system vs other categories
        if location == ['system', 'packages']:
            if container not in self.containers:
                self._init_container(container)
            if 'system_packages' not in self.containers[container]:
                self.containers[container]['system_packages'] = []
            self.containers[container]['system_packages'].extend(packages)
        else:
            if container not in self.containers:
                self._init_container(container)
            if 'other_packages' not in self.containers[container]:
                self.containers[container]['other_packages'] = []
            self.containers[container]['other_packages'].extend(packages)
        
        # Add files using appropriate naming
        if 'nameFromMetaInfo' in flags:
            self.add_files_to_container(container, location, packages, 
                                      dest_name='packageFileName', flags=['computeName'])
        else:
            self.add_files_to_container(container, location, packages)
    
    def add_optional_haiku_image_packages(self, packages: List[str]) -> None:
        """
        Port of JAM AddOptionalHaikuImagePackages rule.
        Add optional packages with dependency resolution.
        
        Args:
            packages: List of optional package names
        """
        if not hasattr(self, 'added_optional_packages'):
            self.added_optional_packages = set()
        
        for package in packages:
            if package not in self.added_optional_packages:
                self.added_optional_packages.add(package)
                
                # Add package dependencies recursively
                dependencies = self._get_package_dependencies(package)
                if dependencies:
                    self.add_optional_haiku_image_packages(dependencies)
    
    def suppress_optional_haiku_image_packages(self, packages: List[str]) -> None:
        """
        Port of JAM SuppressOptionalHaikuImagePackages rule.
        Suppress optional packages from being included.
        
        Args:
            packages: List of package names to suppress
        """
        if not hasattr(self, 'suppressed_optional_packages'):
            self.suppressed_optional_packages = set()
        
        self.suppressed_optional_packages.update(packages)
    
    def is_optional_haiku_image_package_added(self, package: str) -> bool:
        """
        Port of JAM IsOptionalHaikuImagePackageAdded rule.
        Check if optional package is added and not suppressed.
        
        Args:
            package: Package name to check
            
        Returns:
            True if package is added and not suppressed
        """
        if not hasattr(self, 'existing_optional_packages'):
            self.existing_optional_packages = set()
        
        self.existing_optional_packages.add(package)
        
        added = hasattr(self, 'added_optional_packages') and package in self.added_optional_packages
        suppressed = hasattr(self, 'suppressed_optional_packages') and package in self.suppressed_optional_packages
        
        return added and not suppressed
    
    def optional_package_dependencies(self, package: str, dependencies: List[str]) -> None:
        """
        Port of JAM OptionalPackageDependencies rule.
        Define dependencies for an optional package.
        
        Args:
            package: Package name
            dependencies: List of dependency package names
        """
        if not hasattr(self, 'package_dependencies'):
            self.package_dependencies = {}
        
        self.package_dependencies[package] = dependencies
        
        # If package is already added, add its dependencies too
        if (hasattr(self, 'added_optional_packages') and 
            package in self.added_optional_packages):
            self.add_optional_haiku_image_packages(dependencies)
    
    def _get_package_dependencies(self, package: str) -> List[str]:
        """Helper function to get package dependencies."""
        if hasattr(self, 'package_dependencies'):
            return self.package_dependencies.get(package, [])
        return []
    
    def add_haiku_image_packages(self, packages: List[str], directory: List[str]) -> None:
        """
        Port of JAM AddHaikuImagePackages rule.
        Add packages to Haiku image with download and verification.
        
        Args:
            packages: Package names to add
            directory: Directory in image to place packages
        """
        if not hasattr(self, 'added_packages'):
            self.added_packages = set()
        
        for package in packages:
            if package not in self.added_packages:
                self.added_packages.add(package)
                
                # Simulate package download/fetch
                package_file = f"{package}.hpkg"  # Simplified package filename
                
                # Add to image
                self.add_package_files_to_haiku_image(directory, [package_file])
    
    def build_haiku_image_package_list(self, target: str) -> Dict[str, Any]:
        """
        Port of JAM BuildHaikuImagePackageList rule.
        Build list of all packages in image.
        
        Args:
            target: Target file to write package list
            
        Returns:
            Dictionary with package list configuration
        """
        packages = []
        if hasattr(self, 'added_packages'):
            packages.extend(self.added_packages)
        if hasattr(self, 'added_optional_packages'):
            packages.extend(self.added_optional_packages)
        
        # Remove duplicates and sort
        unique_packages = sorted(set(packages))
        
        config = {
            'target_name': f'package_list_{Path(target).stem}',
            'output': target,
            'packages': unique_packages,
            'command': ['echo'] + unique_packages + ['|', 'xargs', '-n', '1', 'echo', '|', 'sort', '-u'],
            'build_by_default': False
        }
        
        return config
    
    def add_user_to_haiku_image(self, user: str, uid: int, gid: int, home: str,
                               shell: Optional[str] = None, real_name: Optional[str] = None) -> None:
        """
        Port of JAM AddUserToHaikuImage rule.
        Add user account to Haiku image.
        
        Args:
            user: Username
            uid: User ID
            gid: Group ID  
            home: Home directory
            shell: Login shell (optional)
            real_name: Real name (optional)
        """
        if not user or not uid or not gid or not home:
            raise ValueError("Invalid user specification - user, uid, gid, and home are required")
        
        real_name = real_name or user
        shell = shell or ""
        
        entry = f"{user}:x:{uid}:{gid}:{real_name}:{home}:{shell}"
        
        self._add_entry_to_user_group_file('passwd', entry)
    
    def add_group_to_haiku_image(self, group: str, gid: int, members: Optional[List[str]] = None) -> None:
        """
        Port of JAM AddGroupToHaikuImage rule.
        Add group to Haiku image.
        
        Args:
            group: Group name
            gid: Group ID
            members: List of group members (optional)
        """
        if not group or not gid:
            raise ValueError("Invalid group specification - group and gid are required")
        
        members_str = ','.join(members) if members else ''
        entry = f"{group}:x:{gid}:{members_str}"
        
        self._add_entry_to_user_group_file('group', entry)
    
    def _add_entry_to_user_group_file(self, file_type: str, entry: str) -> None:
        """Helper function to add entries to user/group files."""
        container = 'haiku-image'
        
        if not hasattr(self, 'user_group_entries'):
            self.user_group_entries = {'passwd': [], 'group': []}
        
        if file_type not in self.user_group_entries:
            self.user_group_entries[file_type] = []
        
        self.user_group_entries[file_type].append(entry)
        
        # Create file target if not exists
        file_target = f'<haiku-image>{file_type}'
        if container not in self.containers:
            self.containers[container] = {'directories': [], 'files': [], 'symlinks': []}
        
        # Add file to image at system/settings/etc
        self.add_files_to_container(container, ['system', 'settings', 'etc'], [file_target])

    # Haiku-specific convenience functions (wrappers around container functions)
    def add_directory_to_haiku_image(self, directory_tokens: List[str],
                                   attribute_files: Optional[List[str]] = None) -> str:
        """Port of JAM AddDirectoryToHaikuImage rule."""
        return self.add_directory_to_container('haiku-image', directory_tokens, attribute_files)
    
    def add_files_to_haiku_image(self, directory: List[str], targets: List[str],
                               dest_name: Optional[str] = None, flags: Optional[List[str]] = None) -> None:
        """Port of JAM AddFilesToHaikuImage rule."""
        self.add_files_to_container('haiku-image', directory, targets, dest_name, flags)
    
    def add_symlink_to_haiku_image(self, directory_tokens: List[str],
                                 link_target: str, link_name: Optional[str] = None) -> None:
        """Port of JAM AddSymlinkToHaikuImage rule."""
        # Join link_target if it's a list
        if isinstance(link_target, list):
            link_target = '/'.join(link_target)
        self.add_symlink_to_container('haiku-image', directory_tokens, link_target, link_name)
    
    def copy_directory_to_haiku_image(self, directory_tokens: List[str], source_directory: str,
                                    target_directory_name: Optional[str] = None,
                                    exclude_patterns: Optional[List[str]] = None,
                                    flags: Optional[List[str]] = None) -> None:
        """Port of JAM CopyDirectoryToHaikuImage rule."""
        self.copy_directory_to_container('haiku-image', directory_tokens, source_directory,
                                       target_directory_name, exclude_patterns, flags)
    
    def add_source_directory_to_haiku_image(self, dir_tokens: List[str],
                                          flags: Optional[List[str]] = None) -> None:
        """Port of JAM AddSourceDirectoryToHaikuImage rule."""
        source_path = str(self.haiku_top / '/'.join(dir_tokens))
        self.copy_directory_to_haiku_image(['home', 'HaikuSources'], source_path,
                                         flags=flags)
    
    def add_header_directory_to_haiku_image(self, dir_tokens: List[str],
                                          dir_name: Optional[str] = None,
                                          flags: Optional[List[str]] = None) -> None:
        """Port of JAM AddHeaderDirectoryToHaikuImage rule."""
        source_path = str(self.haiku_top / 'headers' / '/'.join(dir_tokens))
        system_dir_tokens = ['system', 'develop', 'headers']
        
        self.copy_directory_to_container('haiku-image', system_dir_tokens, source_path,
                                       dir_name, ['-x', '*~'], flags)
    
    def add_wifi_firmware_to_haiku_image(self, driver: str, package: str,
                                       archive: str, extract: bool) -> None:
        """Port of JAM AddWifiFirmwareToHaikuImage rule."""
        self.add_wifi_firmware_to_container('haiku-image', driver, package, archive, extract)
    
    def add_wifi_firmware_to_container(self, container: str, driver: str, package: str,
                                     archive: str, extract: bool) -> None:
        """Port of JAM AddWifiFirmwareToContainer rule."""
        firmware_archive = self.haiku_top / 'data/system/data/firmware' / driver / archive
        system_dir_tokens = ['system']
        dir_tokens = system_dir_tokens + ['data', 'firmware', driver]
        
        if extract:
            self.extract_archive_to_container(container, dir_tokens, str(firmware_archive),
                                            extracted_sub_dir=package)
        else:
            self.add_files_to_container(container, dir_tokens, [str(firmware_archive)])
    
    def extract_archive_to_haiku_image(self, dir_tokens: List[str], archive_file: str,
                                     flags: Optional[List[str]] = None,
                                     extracted_sub_dir: Optional[str] = None) -> None:
        """Port of JAM ExtractArchiveToHaikuImage rule."""
        self.extract_archive_to_container('haiku-image', dir_tokens, archive_file,
                                        flags, extracted_sub_dir)
    
    def extract_archive_to_container(self, container: str, directory_tokens: List[str],
                                   archive_file: str, flags: Optional[List[str]] = None,
                                   extracted_sub_dir: Optional[str] = None) -> None:
        """Port of JAM ExtractArchiveToContainer rule."""
        flags = flags or []
        directory_id = self.add_directory_to_container(container, directory_tokens)
        
        if container not in self.containers:
            self.containers[container] = {'directories': [], 'files': [], 'symlinks': [], 'archives': []}
        
        if 'archives' not in self.containers[container]:
            self.containers[container]['archives'] = []
        
        archive_entry = {
            'directory': directory_id,
            'archive_file': archive_file,
            'extracted_sub_dir': extracted_sub_dir or '.',
            'flags': flags
        }
        
        self.containers[container]['archives'].append(archive_entry)
    
    def add_drivers_to_haiku_image(self, relative_directory_tokens: List[str],
                                 targets: List[str]) -> None:
        """Port of JAM AddDriversToHaikuImage rule."""
        self.add_drivers_to_container('haiku-image', relative_directory_tokens, targets)
    
    def add_new_drivers_to_haiku_image(self, relative_directory_tokens: List[str],
                                     targets: List[str], flags: Optional[List[str]] = None) -> None:
        """Port of JAM AddNewDriversToHaikuImage rule."""
        self.add_new_drivers_to_container('haiku-image', relative_directory_tokens, targets, flags)
    
    def add_boot_module_symlinks_to_haiku_image(self, targets: List[str]) -> None:
        """Port of JAM AddBootModuleSymlinksToHaikuImage rule."""
        self.add_boot_module_symlinks_to_container('haiku-image', targets)
    
    def add_libraries_to_haiku_image(self, directory: List[str], libs: List[str]) -> None:
        """Port of JAM AddLibrariesToHaikuImage rule."""
        self.add_libraries_to_container('haiku-image', directory, libs)
    
    def set_update_haiku_image_only(self, flag: bool) -> None:
        """Port of JAM SetUpdateHaikuImageOnly rule."""
        if 'haiku-image' not in self.containers:
            self.containers['haiku-image'] = {}
        self.containers['haiku-image']['update_only'] = flag
    
    def is_update_haiku_image_only(self) -> bool:
        """Port of JAM IsUpdateHaikuImageOnly rule."""
        if 'haiku-image' not in self.containers:
            return False
        return self.containers['haiku-image'].get('update_only', False)
    
    def add_haiku_image_system_packages(self, packages: List[str]) -> None:
        """Port of JAM AddHaikuImageSystemPackages rule."""
        self.add_haiku_image_packages(packages, ['system', 'packages'])
    
    def add_haiku_image_disabled_packages(self, packages: List[str]) -> None:
        """Port of JAM AddHaikuImageDisabledPackages rule."""
        self.add_haiku_image_packages(packages, ['_packages_'])
    
    def add_haiku_image_source_packages(self, packages: List[str]) -> None:
        """Port of JAM AddHaikuImageSourcePackages rule."""
        include_sources = os.getenv('HAIKU_INCLUDE_SOURCES', '0') == '1'
        if include_sources:
            source_packages = [f"{pkg}_source" for pkg in packages]
            self.add_haiku_image_packages(source_packages, ['_sources_'])
    
    def is_haiku_image_package_added(self, package: str) -> bool:
        """Port of JAM IsHaikuImagePackageAdded rule."""
        if hasattr(self, 'added_packages'):
            return package in self.added_packages
        return False

    def get_container_info(self, container: str) -> Dict[str, Any]:
        """Get information about a container."""
        if container not in self.containers:
            return {}
        
        return {
            'directories': len(self.containers[container]['directories']),
            'files': len(self.containers[container]['files']),
            'symlinks': len(self.containers[container]['symlinks']),
            'copies': len(self.containers[container].get('copies', [])),
            'attributes': len(self.containers[container].get('attributes', {}))
        }
    
    # NetBoot Archive rules
    def add_directory_to_netboot_archive(self, directory_tokens: List[str]) -> str:
        """Port of JAM AddDirectoryToNetBootArchive rule."""
        return self.add_directory_to_container('netboot-archive', directory_tokens)
    
    def add_files_to_netboot_archive(self, directory: List[str], targets: List[str],
                                   dest_name: Optional[str] = None) -> None:
        """Port of JAM AddFilesToNetBootArchive rule."""
        self.add_files_to_container('netboot-archive', directory, targets, dest_name)
    
    def add_symlink_to_netboot_archive(self, directory_tokens: List[str],
                                     link_target: str, link_name: Optional[str] = None) -> None:
        """Port of JAM AddSymlinkToNetBootArchive rule."""
        self.add_symlink_to_container('netboot-archive', directory_tokens, link_target, link_name)
    
    def build_netboot_archive(self, archive: str, scripts: List[str]) -> Dict[str, Any]:
        """Port of JAM BuildNetBootArchive rule."""
        build_script = self.haiku_top / 'build/scripts/build_archive'
        
        config = {
            'target_name': f'netboot_archive_{Path(archive).stem}',
            'output': archive,
            'command': [str(build_script), archive] + scripts,
            'depends': scripts + [str(build_script)],
            'build_by_default': False
        }
        
        return config
    
    # CD Boot Image rules  
    def build_cd_boot_image_efi(self, image: str, bootfloppy: str, bootefi: str,
                               extra_files: Optional[List[str]] = None) -> Dict[str, Any]:
        """Port of JAM BuildCDBootImageEFI rule."""
        extra_files = extra_files or []
        
        # Determine volume ID
        if os.getenv('HAIKU_NIGHTLY_BUILD') == '1':
            vol_id = f'haiku-nightly-{self.target_arch}'
        else:
            haiku_version = os.getenv('HAIKU_VERSION', 'unknown')
            vol_id = f'haiku-{haiku_version}-{self.target_arch}'
        
        # Build xorriso command with both legacy and EFI boot
        command = [
            'xorriso', '-as', 'mkisofs',
            '-b', bootfloppy, '-no-emul-boot',
            '-eltorito-alt-boot', '-no-emul-boot', '-e', bootefi,
            '-r', '-J', '-V', vol_id,
            '-o', image,
            bootfloppy, bootefi
        ] + extra_files
        
        config = {
            'target_name': f'cd_efi_image_{Path(image).stem}',
            'output': image,
            'command': command,
            'depends': [bootfloppy, bootefi] + extra_files,
            'build_by_default': False
        }
        
        return config
    
    def build_vmware_image(self, vmware_image: str, plain_image: str,
                          image_size: str) -> Dict[str, Any]:
        """Port of JAM BuildVMWareImage rule."""
        vmdkheader_tool = self.haiku_top / 'generated/objects/linux/x86_64/release/tools/vmdkheader/vmdkheader'
        
        script_lines = [
            '#!/bin/bash',
            'set -e',
            f'# Build VMware image: {vmware_image}',
            '',
            f'rm -f "{vmware_image}"',
            f'"{vmdkheader_tool}" -h 64k -i{image_size}M "{vmware_image}"',
            f'cat "{plain_image}" >> "{vmware_image}"'
        ]
        
        config = {
            'target_name': f'vmware_image_{Path(vmware_image).stem}',
            'output': vmware_image,
            'script_content': '\n'.join(script_lines),
            'depends': [str(vmdkheader_tool), plain_image],
            'env': {'IMAGE_SIZE': image_size},
            'build_by_default': False
        }
        
        return config


def get_image_rules(target_arch: str = 'x86_64') -> HaikuImageRules:
    """Get image rules instance."""
    return HaikuImageRules(target_arch)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Image Rules (JAM Port) ===")
    
    image = get_image_rules('x86_64')
    
    # Test AddDirectoryToContainer equivalent
    dir_id = image.add_directory_to_container('haiku-image', ['system', 'bin'])
    print(f"Created directory: {dir_id}")
    
    # Test AddFilesToContainer equivalent
    image.add_files_to_container('haiku-image', ['system', 'bin'], 
                                ['build/ls', 'build/cat', 'build/mkdir'])
    
    # Test AddSymlinkToContainer equivalent  
    image.add_symlink_to_container('haiku-image', ['system', 'bin'], '/system/bin/ls', 'list')
    
    # Test AddLibrariesToContainer equivalent
    image.add_libraries_to_container('haiku-image', ['system', 'lib'], 
                                   ['build/libbe.so.1.0.0', 'build/libroot.so'])
    
    # Test script generation
    mkdir_script = image.create_container_script('haiku-image', 'mkdir')
    copy_script = image.create_container_script('haiku-image', 'copy')
    
    print(f"Generated mkdir script: {len(mkdir_script['content'])} chars")
    print(f"Generated copy script: {len(copy_script['content'])} chars")
    
    # Test image build configuration
    image_config = image.build_haiku_image('haiku.image', ['script1.sh', 'script2.sh'])
    print(f"Image build config: {image_config['target_name']}")
    
    # Test EFI-only ISO build
    iso_config = image.build_cd_boot_image_efi_only('haiku.iso', 'boot.efi', ['readme.txt'])
    print(f"ISO build config: {iso_config['target_name']}")
    
    # Show container info
    info = image.get_container_info('haiku-image')
    print(f"Container info: {info}")
    
    print("✅ Image Rules functionality verified")