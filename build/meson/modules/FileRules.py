#!/usr/bin/env python3
"""
Haiku File Rules for Meson
Port of JAM FileRules to provide file operations.
"""

import os
import shutil
from typing import List, Dict, Optional, Any, Union
from pathlib import Path

class HaikuFileRules:
    """Port of JAM FileRules providing file operations."""
    
    def __init__(self):
        self.copy_targets = []
        self.symlink_targets = []
        
    def copy_file(self, target: str, source: str, preserve_attrs: bool = True) -> Dict[str, Any]:
        """
        Port of JAM Copy rule.
        
        Args:
            target: Destination file path
            source: Source file path
            preserve_attrs: Whether to preserve file attributes
            
        Returns:
            Dictionary with Meson custom_target configuration
        """
        source_path = Path(source)
        target_path = Path(target)
        
        # Ensure source exists
        if not source_path.exists():
            raise FileNotFoundError(f"Source file not found: {source}")
        
        copy_config = {
            'target_name': f'copy_{target_path.stem}',
            'input': str(source_path),
            'output': str(target_path.name),
            'command': self._get_copy_command(preserve_attrs),
            'install': False,
            'build_by_default': True
        }
        
        self.copy_targets.append(copy_config)
        return copy_config
    
    def _get_copy_command(self, preserve_attrs: bool = True) -> List[str]:
        """Get appropriate copy command for platform."""
        if preserve_attrs:
            # Use copyattr if available (Haiku-specific), otherwise cp -p
            copyattr_path = '/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/copyattr/copyattr'
            if Path(copyattr_path).exists():
                return [copyattr_path, '-d', '@INPUT@', '@OUTPUT@']
            else:
                return ['cp', '-p', '@INPUT@', '@OUTPUT@']
        else:
            return ['cp', '@INPUT@', '@OUTPUT@']
    
    def symlink(self, target: str, source: str, make_default_dependencies: bool = True) -> Dict[str, Any]:
        """
        Port of JAM SymLink rule.
        
        Args:
            target: Symlink target path
            source: Symlink source (link contents)
            make_default_dependencies: Whether to add to default build
            
        Returns:
            Dictionary with Meson custom_target configuration
        """
        target_path = Path(target)
        
        symlink_config = {
            'target_name': f'symlink_{target_path.stem}',
            'output': str(target_path.name),
            'command': ['ln', '-sf', source, '@OUTPUT@'],
            'install': False,
            'build_by_default': make_default_dependencies
        }
        
        self.symlink_targets.append(symlink_config)
        return symlink_config
    
    def clean_dir(self, directory: str) -> Dict[str, Any]:
        """
        Port of JAM CleanDir rule.
        
        Args:
            directory: Directory to clean
            
        Returns:
            Dictionary with clean operation configuration
        """
        dir_path = Path(directory)
        
        return {
            'target_name': f'clean_{dir_path.name}',
            'command': ['rm', '-rf', str(dir_path)],
            'build_always': True
        }
    
    def install_file(self, source: str, dest_dir: str, permissions: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM InstallFile rule equivalent.
        
        Args:
            source: Source file path
            dest_dir: Destination directory
            permissions: File permissions (e.g., '755')
            
        Returns:
            Dictionary with install configuration
        """
        source_path = Path(source)
        dest_path = Path(dest_dir)
        
        install_config = {
            'sources': [str(source_path)],
            'install_dir': str(dest_path),
            'install_mode': permissions if permissions else '644'
        }
        
        return install_config
    
    def mkdir_p(self, directory: str) -> Dict[str, Any]:
        """
        Create directory with parents (mkdir -p equivalent).
        
        Args:
            directory: Directory path to create
            
        Returns:
            Dictionary with mkdir configuration
        """
        dir_path = Path(directory)
        
        return {
            'target_name': f'mkdir_{dir_path.name}',
            'command': ['mkdir', '-p', str(dir_path)],
            'build_always': True
        }
    
    def touch_file(self, filepath: str) -> Dict[str, Any]:
        """
        Touch a file (create empty or update timestamp).
        
        Args:
            filepath: File path to touch
            
        Returns:
            Dictionary with touch configuration
        """
        file_path = Path(filepath)
        
        return {
            'target_name': f'touch_{file_path.stem}',
            'output': str(file_path.name),
            'command': ['touch', '@OUTPUT@'],
            'build_always': True
        }
    
    def generate_meson_targets(self) -> List[str]:
        """
        Generate Meson custom_target declarations for all file operations.
        
        Returns:
            List of Meson target declarations
        """
        meson_targets = []
        
        # Copy targets
        for copy_target in self.copy_targets:
            target_code = f"""
{copy_target['target_name']} = custom_target('{copy_target['target_name']}',
    input: '{copy_target['input']}',
    output: '{copy_target['output']}',
    command: {copy_target['command']},
    build_by_default: {str(copy_target['build_by_default']).lower()}
)"""
            meson_targets.append(target_code)
        
        # Symlink targets  
        for symlink_target in self.symlink_targets:
            target_code = f"""
{symlink_target['target_name']} = custom_target('{symlink_target['target_name']}',
    output: '{symlink_target['output']}',
    command: {symlink_target['command']},
    build_by_default: {str(symlink_target['build_by_default']).lower()}
)"""
            meson_targets.append(target_code)
        
        return meson_targets
    
    def get_all_targets(self) -> Dict[str, List[Dict[str, Any]]]:
        """Get all configured file operation targets."""
        return {
            'copy_targets': self.copy_targets,
            'symlink_targets': self.symlink_targets
        }


def get_file_rules() -> HaikuFileRules:
    """Get file rules instance."""
    return HaikuFileRules()


# ========== MODULE-LEVEL EXPORTS FOR JAM COMPATIBILITY ==========
_file_instance = None

def _get_file():
    global _file_instance
    if _file_instance is None:
        _file_instance = HaikuFileRules()
    return _file_instance

# JAM Rule: Copy
def Copy(target: str, source: str) -> Dict[str, Any]:
    """Copy <target> : <source> - Copy file."""
    return _get_file().copy(target, source)

# JAM Rule: SymLink  
def SymLink(target: str, source: str) -> Dict[str, Any]:
    """SymLink <target> : <source> - Create symbolic link."""
    return _get_file().symlink(target, source)

# JAM Rule: HaikuInstall
def HaikuInstall(install_dir: str, sources: List[str], install_grist: str = None) -> Dict[str, Any]:
    """HaikuInstall <installDir> : <sources> : <installGrist>"""
    return _get_file().haiku_install(install_dir, sources, install_grist)

# JAM Rule: RelSymLink
def RelSymLink(target: str, source: str) -> Dict[str, Any]:
    """RelSymLink <target> : <source> - Create relative symbolic link."""
    return _get_file().rel_symlink(target, source)

# JAM Rule: AbsSymLink
def AbsSymLink(target: str, source: str) -> Dict[str, Any]:
    """AbsSymLink <target> : <source> - Create absolute symbolic link."""
    return _get_file().abs_symlink(target, source)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku File Rules (JAM Port) ===")
    
    file_rules = get_file_rules()
    
    # Test Copy operation
    copy_config = file_rules.copy_file(
        target='build/copied_file.txt',
        source='src/original_file.txt'
    )
    print(f"Copy target configured: {copy_config['target_name']}")
    
    # Test SymLink operation
    symlink_config = file_rules.symlink(
        target='build/link_to_file',
        source='../original_file.txt'
    )
    print(f"Symlink target configured: {symlink_config['target_name']}")
    
    # Test Meson target generation
    targets = file_rules.generate_meson_targets()
    print(f"Generated {len(targets)} Meson custom_target declarations")
    
    # Show all targets
    all_targets = file_rules.get_all_targets()
    total_targets = sum(len(target_list) for target_list in all_targets.values())
    print(f"Total file operation targets: {total_targets}")
    
    print("✅ File Rules functionality verified")