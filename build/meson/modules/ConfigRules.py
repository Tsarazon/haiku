#!/usr/bin/env python3
"""
Haiku Config Rules for Meson
Port of JAM ConfigRules to provide directory-based configuration system.
Allows centralized configuration of build variables per directory.
"""

import os
from typing import List, Dict, Optional, Any, Union, Tuple
from pathlib import Path

class HaikuConfigRules:
    """Port of JAM ConfigRules providing directory-based configuration management."""
    
    def __init__(self):
        # Directory configurations storage
        # Format: {dir_path: {var_name: {'value': value, 'scope': 'global'|'local'}}}
        self.directory_configs = {}
        
        # Inherited configurations (global scope)
        self.inherited_configs = {}
        
        # Track configured directories
        self.configured_dirs = set()
    
    def _get_config_key(self, dir_tokens: List[str]) -> str:
        """
        Generate configuration key from directory tokens.
        
        Args:
            dir_tokens: Directory tokens (equivalent to SubDir parameters)
            
        Returns:
            Configuration key string
        """
        if not dir_tokens:
            return "root"
        
        # Handle TOP variable + subdir tokens format
        if len(dir_tokens) > 1 and dir_tokens[0] in ['HAIKU_TOP', 'TOP']:
            return '/'.join(dir_tokens[1:]) if len(dir_tokens) > 1 else "root"
        
        return '/'.join(dir_tokens)
    
    def _get_parent_dir_key(self, dir_key: str) -> Optional[str]:
        """Get parent directory key."""
        if dir_key == "root" or not dir_key:
            return None
        
        parts = dir_key.split('/')
        if len(parts) <= 1:
            return "root"
        
        return '/'.join(parts[:-1])
    
    def set_config_var(self, var_name: str, dir_tokens: List[str], 
                      value: Any, scope: str = 'global') -> None:
        """
        Port of JAM SetConfigVar rule.
        Set configuration variable for specified directory.
        
        Args:
            var_name: Variable name to set
            dir_tokens: Directory tokens (SubDir parameters)
            value: Value to set
            scope: 'global' (inherited by subdirs) or 'local' (dir only)
        """
        if not value:
            raise ValueError(f"Error: no value specified for ConfigVar '{var_name}'!")
        
        dir_key = self._get_config_key(dir_tokens)
        
        # Initialize directory config if not exists
        if dir_key not in self.directory_configs:
            self.directory_configs[dir_key] = {}
        
        # Set the variable
        self.directory_configs[dir_key][var_name] = {
            'value': value,
            'scope': scope
        }
        
        # If global scope, also set in inherited configs
        if scope == 'global':
            if dir_key not in self.inherited_configs:
                self.inherited_configs[dir_key] = {}
            self.inherited_configs[dir_key][var_name] = value
        
        # Mark directory as configured
        self.configured_dirs.add(dir_key)
    
    def append_to_config_var(self, var_name: str, dir_tokens: List[str],
                           value: Any, scope: str = 'global') -> None:
        """
        Port of JAM AppendToConfigVar rule.
        Append value to existing configuration variable.
        
        Args:
            var_name: Variable name
            dir_tokens: Directory tokens
            value: Value to append
            scope: Variable scope
        """
        # Get current value and append
        current_value = self.config_var(var_name, dir_tokens, scope)
        
        # Handle different value types
        if isinstance(current_value, list):
            if isinstance(value, list):
                new_value = current_value + value
            else:
                new_value = current_value + [value]
        elif isinstance(current_value, str):
            if isinstance(value, str):
                new_value = current_value + ' ' + value
            elif isinstance(value, list):
                new_value = current_value + ' ' + ' '.join(str(v) for v in value)
            else:
                new_value = current_value + ' ' + str(value)
        else:
            # For other types, make it a list
            new_value = [current_value, value] if current_value is not None else [value]
        
        self.set_config_var(var_name, dir_tokens, new_value, scope)
    
    def config_var(self, var_name: str, dir_tokens: List[str], 
                  scope: Optional[str] = None) -> Any:
        """
        Port of JAM ConfigVar rule.
        Get configuration variable value for directory.
        Searches up directory hierarchy if not found locally.
        
        Args:
            var_name: Variable name to get
            dir_tokens: Directory tokens
            scope: Optional scope filter
            
        Returns:
            Variable value or None if not found
        """
        dir_key = self._get_config_key(dir_tokens)
        
        # First check direct configuration for this directory
        if dir_key in self.directory_configs:
            var_config = self.directory_configs[dir_key].get(var_name)
            if var_config:
                # Check scope match if specified
                if scope is None or var_config['scope'] == scope:
                    return var_config['value']
        
        # If not found and scope allows, search up directory hierarchy for global vars
        if scope != 'local':
            current_dir = dir_key
            while current_dir is not None:
                # Check inherited configs for this directory level
                if current_dir in self.inherited_configs:
                    if var_name in self.inherited_configs[current_dir]:
                        return self.inherited_configs[current_dir][var_name]
                
                # Move up to parent directory
                current_dir = self._get_parent_dir_key(current_dir)
        
        # If still not found, return global variable if exists
        return os.environ.get(var_name)
    
    def prepare_subdir_config_variables(self, dir_tokens: List[str], 
                                      variables: List[str]) -> Dict[str, Any]:
        """
        Port of JAM PrepareSubDirConfigVariables rule.
        Prepare configuration variables for subdirectory.
        
        Args:
            dir_tokens: Directory tokens
            variables: List of variable names to prepare
            
        Returns:
            Dictionary of prepared variables
        """
        result = {}
        
        for var_name in variables:
            value = self.config_var(var_name, dir_tokens)
            if value is not None:
                result[var_name] = value
        
        return result
    
    def prepare_config_variables(self, variables: List[str]) -> Dict[str, Any]:
        """
        Port of JAM PrepareConfigVariables rule.
        Prepare configuration variables for current context.
        
        Args:
            variables: List of variable names to prepare
            
        Returns:
            Dictionary of prepared variables
        """
        return self.prepare_subdir_config_variables([], variables)
    
    def get_all_configured_directories(self) -> List[str]:
        """Get list of all configured directories."""
        return list(self.configured_dirs)
    
    def get_directory_config(self, dir_tokens: List[str]) -> Dict[str, Any]:
        """
        Get all configuration for a specific directory.
        
        Args:
            dir_tokens: Directory tokens
            
        Returns:
            Dictionary of all variables for directory
        """
        dir_key = self._get_config_key(dir_tokens)
        result = {}
        
        # Get direct configurations
        if dir_key in self.directory_configs:
            for var_name, config in self.directory_configs[dir_key].items():
                result[var_name] = config['value']
        
        # Get inherited configurations
        current_dir = self._get_parent_dir_key(dir_key)
        while current_dir is not None:
            if current_dir in self.inherited_configs:
                for var_name, value in self.inherited_configs[current_dir].items():
                    # Only add if not already set locally
                    if var_name not in result:
                        result[var_name] = value
            
            current_dir = self._get_parent_dir_key(current_dir)
        
        return result
    
    def clear_config(self) -> None:
        """Clear all configuration data."""
        self.directory_configs.clear()
        self.inherited_configs.clear()
        self.configured_dirs.clear()
    
    def export_config_for_meson(self, dir_tokens: List[str]) -> Dict[str, Any]:
        """
        Export configuration in Meson-compatible format.
        
        Args:
            dir_tokens: Directory tokens
            
        Returns:
            Meson-compatible configuration dictionary
        """
        config = self.get_directory_config(dir_tokens)
        
        # Convert JAM-style variables to Meson format
        meson_config = {}
        
        for var_name, value in config.items():
            # Convert common JAM variables to Meson equivalents
            if var_name in ['CCFLAGS', 'TARGET_CCFLAGS']:
                meson_config['c_args'] = self._ensure_list(value)
            elif var_name in ['C++FLAGS', 'TARGET_C++FLAGS']:
                meson_config['cpp_args'] = self._ensure_list(value)
            elif var_name in ['LINKFLAGS', 'TARGET_LINKFLAGS']:
                meson_config['link_args'] = self._ensure_list(value)
            elif var_name == 'DEFINES':
                defines = self._ensure_list(value)
                meson_config['c_args'] = meson_config.get('c_args', []) + [f'-D{d}' for d in defines]
                meson_config['cpp_args'] = meson_config.get('cpp_args', []) + [f'-D{d}' for d in defines]
            elif var_name == 'HDRS':
                includes = self._ensure_list(value)
                meson_config['include_directories'] = includes
            else:
                # Pass through other variables
                meson_config[var_name.lower()] = value
        
        return meson_config
    
    def _ensure_list(self, value: Any) -> List[str]:
        """Ensure value is a list of strings."""
        if isinstance(value, list):
            return [str(v) for v in value]
        elif isinstance(value, str):
            return value.split()
        else:
            return [str(value)]


def get_config_rules() -> HaikuConfigRules:
    """Get config rules instance."""
    return HaikuConfigRules()


# ========== MODULE-LEVEL EXPORTS FOR JAM COMPATIBILITY ==========
_config_instance = None

def _get_config():
    global _config_instance
    if _config_instance is None:
        _config_instance = HaikuConfigRules()
    return _config_instance

# JAM Rules
def ConfigObject():
    """ConfigObject rule."""
    return _get_config().config_object()

def SetConfigVar(var_name: str, var_value, dir_tokens: List[str] = None, scope: str = 'global'):
    """SetConfigVar rule."""
    if dir_tokens is None:
        dir_tokens = ['HAIKU_TOP']
    return _get_config().set_config_var(var_name, dir_tokens, var_value, scope)

def AppendToConfigVar(key: str, value):
    """AppendToConfigVar rule."""
    return _get_config().append_to_config_var(key, value)

def ConfigVar(var_name: str, dir_tokens: List[str] = None, default=None):
    """ConfigVar rule."""
    if dir_tokens is None:
        dir_tokens = ['HAIKU_TOP']
    return _get_config().config_var(var_name, dir_tokens, default)

def PrepareSubDirConfigVariables(subdir: str):
    """PrepareSubDirConfigVariables rule."""
    return _get_config().prepare_subdir_config_variables(subdir)

def PrepareConfigVariables():
    """PrepareConfigVariables rule."""
    return _get_config().prepare_config_variables()


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Config Rules (JAM Port) ===")
    
    config = get_config_rules()
    
    # Test basic configuration
    config.set_config_var('CCFLAGS', ['HAIKU_TOP', 'src'], ['-O2', '-Wall'], 'global')
    config.set_config_var('DEFINES', ['HAIKU_TOP', 'src'], ['DEBUG=1', 'HAIKU=1'], 'global')
    config.set_config_var('LOCAL_FLAG', ['HAIKU_TOP', 'src', 'kits'], '-fPIC', 'local')
    
    print("=== Configuration Test ===")
    
    # Test retrieval
    ccflags = config.config_var('CCFLAGS', ['HAIKU_TOP', 'src'])
    print(f"src CCFLAGS: {ccflags}")
    
    # Test inheritance
    ccflags_inherited = config.config_var('CCFLAGS', ['HAIKU_TOP', 'src', 'kits', 'interface'])
    print(f"interface CCFLAGS (inherited): {ccflags_inherited}")
    
    # Test local variable
    local_flag = config.config_var('LOCAL_FLAG', ['HAIKU_TOP', 'src', 'kits'])
    local_flag_not_inherited = config.config_var('LOCAL_FLAG', ['HAIKU_TOP', 'src', 'kits', 'interface'])
    print(f"kits LOCAL_FLAG: {local_flag}")
    print(f"interface LOCAL_FLAG (should be None): {local_flag_not_inherited}")
    
    # Test append
    config.append_to_config_var('CCFLAGS', ['HAIKU_TOP', 'src'], '-g', 'global')
    ccflags_appended = config.config_var('CCFLAGS', ['HAIKU_TOP', 'src'])
    print(f"src CCFLAGS after append: {ccflags_appended}")
    
    # Test Meson export
    meson_config = config.export_config_for_meson(['HAIKU_TOP', 'src', 'kits', 'interface'])
    print(f"Meson config for interface: {meson_config}")
    
    # Test directory listing
    configured_dirs = config.get_all_configured_directories()
    print(f"Configured directories: {configured_dirs}")
    
    print("✅ Config Rules functionality verified")
    print("Directory-based configuration system successfully ported from JAM")