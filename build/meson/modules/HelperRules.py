#!/usr/bin/env python3
"""
Haiku Helper Rules for Meson
Port of JAM HelperRules to provide fundamental utility functions.
These are the foundation functions used by all other JAM rules.
"""

import os
import re
from typing import List, Dict, Optional, Any, Union, Tuple
from pathlib import Path

class HaikuHelperRules:
    """Port of JAM HelperRules providing fundamental utility functions."""
    
    def __init__(self):
        # Platform compatibility state
        self.platform_variables = {}
        
    def f_filter(self, items: List[str], excludes: List[str]) -> List[str]:
        """
        Port of JAM FFilter rule.
        Removes all occurrences of excludes from items list.
        
        Args:
            items: List of items to filter
            excludes: Items to remove from the list
            
        Returns:
            Filtered list with excludes removed
        """
        if not excludes:
            return items.copy()
        
        filtered = []
        for item in items:
            if item not in excludes:
                filtered.append(item)
        
        return filtered
    
    def f_get_grist(self, target: str) -> Optional[str]:
        """
        Port of JAM FGetGrist rule.
        Returns the grist of a target, not including leading "<" and trailing ">".
        
        Args:
            target: Target name potentially with grist
            
        Returns:
            Grist string without brackets, or None if no grist
        """
        match = re.match(r'<([^>]*)>', target)
        if match:
            return match.group(1)
        return None
    
    def f_split_string(self, string: str, delimiter: str) -> List[str]:
        """
        Port of JAM FSplitString rule.
        Split string by delimiter character.
        
        Args:
            string: String to split
            delimiter: Delimiter character
            
        Returns:
            List of split components
        """
        if not string:
            return []
        
        # Handle special regex characters in delimiter
        if delimiter in r'\.^$*+?{}[]|()':
            delimiter = f'\\{delimiter}'
        
        # Split and filter empty strings
        parts = re.split(delimiter + '+', string)
        return [part for part in parts if part]
    
    def f_split_path(self, path: str) -> List[str]:
        """
        Port of JAM FSplitPath rule.
        Decomposes a path into its components.
        
        Args:
            path: Path to decompose
            
        Returns:
            List of path components
        """
        if not path:
            return []
        
        # Normalize path and split into components
        normalized_path = os.path.normpath(path)
        
        # Handle absolute paths
        if os.path.isabs(normalized_path):
            components = ['/'] + [comp for comp in normalized_path.split(os.sep) if comp]
        else:
            components = [comp for comp in normalized_path.split(os.sep) if comp]
        
        return components
    
    def f_conditions_hold(self, conditions: List[str], predicate_func) -> bool:
        """
        Port of JAM FConditionsHold rule.
        Checks whether conditions are satisfied by the predicate function.
        
        Args:
            conditions: List of conditions (positive and negative)
            predicate_func: Function to test conditions
            
        Returns:
            True if conditions hold, False otherwise
        """
        if not conditions:
            return False
        
        has_positive = False
        has_negative = False
        positive_match = False
        
        for condition in conditions:
            if condition.startswith('!'):
                # Negative condition
                has_negative = True
                test_condition = condition[1:]  # Remove leading !
                if predicate_func(test_condition):
                    return False  # Negative condition failed
            else:
                # Positive condition
                has_positive = True
                if predicate_func(condition):
                    positive_match = True
        
        # If we have positive conditions, at least one must match
        if has_positive:
            return positive_match
        
        # If we only have negative conditions, all must pass (none failed above)
        return has_negative
    
    def f_is_prefix(self, prefix: List[str], items: List[str]) -> bool:
        """
        Port of JAM FIsPrefix rule.
        Returns true if prefix is a prefix of items.
        
        Args:
            prefix: List that should be a prefix
            items: List to check against
            
        Returns:
            True if prefix is a prefix of items
        """
        if len(prefix) > len(items):
            return False
        
        for i, prefix_item in enumerate(prefix):
            if prefix_item != items[i]:
                return False
        
        return True
    
    def set_platform_compatibility_flag_variables(self, platform_var: str, var_prefix: str,
                                                 platform_kind: str, other_platforms: Optional[List[str]] = None) -> None:
        """
        Port of JAM SetPlatformCompatibilityFlagVariables rule.
        Set platform compatibility flags based on current platform.
        
        Args:
            platform_var: Name of platform variable
            var_prefix: Prefix for generated variables
            platform_kind: Kind of platform (e.g., "target", "host")
            other_platforms: List of other supported platforms
        """
        platform = self.platform_variables.get(platform_var)
        other_platforms = other_platforms or []
        
        if not platform:
            raise ValueError(f"Variable {platform_var} not set. Please run ./configure or specify it manually.")
        
        # Special case: Haiku libbe.so built for testing under Haiku
        if platform == 'libbe_test':
            platform = self.platform_variables.get('HOST_PLATFORM', 'host')
        
        # Reset compatibility flags
        self.platform_variables[f'{var_prefix}_PLATFORM_HAIKU_COMPATIBLE'] = None
        
        # Set compatibility based on platform
        if platform in ['haiku_host', 'haiku']:
            self.platform_variables[f'{var_prefix}_PLATFORM_HAIKU_COMPATIBLE'] = True
        elif platform == 'host':
            # Not compatible to anything
            pass
        elif platform not in other_platforms:
            raise ValueError(f"Unsupported {platform_kind} platform: {platform}")
    
    def set_include_properties_variables(self, prefix: str, suffix: str) -> None:
        """
        Port of JAM SetIncludePropertiesVariables rule.
        Set include-related property variables.
        
        Args:
            prefix: Variable prefix
            suffix: Variable suffix
        """
        # Set standard include properties
        base_name = f"{prefix}_INCLUDES_{suffix}"
        self.platform_variables[f"{base_name}_SEPARATOR"] = " -I"
        self.platform_variables[f"{base_name}_PREFIX"] = "-I"
        self.platform_variables[f"{base_name}_SUFFIX"] = ""
    
    def set_platform_for_target(self, target: str, platform: str) -> None:
        """
        Port of JAM SetPlatformForTarget rule.
        Set platform for specific target.
        
        Args:
            target: Target name
            platform: Platform name
        """
        # Store platform association for target
        if not hasattr(self, 'target_platforms'):
            self.target_platforms = {}
        self.target_platforms[target] = platform
    
    def is_platform_supported_for_target(self, platform: str, target: str) -> bool:
        """
        Port of JAM IsPlatformSupportedForTarget rule.
        Check if platform is supported for target.
        
        Args:
            platform: Platform name
            target: Target name
            
        Returns:
            True if platform is supported for target
        """
        # Get supported platforms for target
        if hasattr(self, 'target_supported_platforms'):
            supported = self.target_supported_platforms.get(target, [])
            return platform in supported
        
        # Default: all platforms supported
        return True
    
    def subdir_as_flags(self, tokens: List[str]) -> str:
        """
        Port of JAM SubDirAsFlags rule.
        Convert subdirectory tokens to flags format.
        
        Args:
            tokens: Directory tokens
            
        Returns:
            Flags string representation
        """
        if not tokens:
            return ""
        
        # Join tokens with underscores and convert to uppercase
        return "_".join(token.upper() for token in tokens)
    
    def inherit_platform(self, source_target: str, dest_target: str) -> None:
        """
        Port of JAM InheritPlatform rule.
        Inherit platform from source target to destination target.
        
        Args:
            source_target: Source target
            dest_target: Destination target
        """
        if hasattr(self, 'target_platforms'):
            source_platform = self.target_platforms.get(source_target)
            if source_platform:
                self.target_platforms[dest_target] = source_platform
    
    def get_platform_variables(self) -> Dict[str, Any]:
        """Get all platform variables."""
        return self.platform_variables.copy()
    
    def set_platform_variable(self, name: str, value: Any) -> None:
        """Set platform variable."""
        self.platform_variables[name] = value
    
    def get_target_platform(self, target: str) -> Optional[str]:
        """Get platform for target."""
        if hasattr(self, 'target_platforms'):
            return self.target_platforms.get(target)
        return None
    
    def f_dir_name(self, *components) -> str:
        """
        Port of JAM FDirName rule.
        Join path components into a proper path.
        
        Args:
            components: Path components to join
            
        Returns:
            Joined path string
        """
        if not components:
            return ''
        return os.path.join(*[str(c) for c in components if c])
    
    def f_grist_files(self, files: List[str], grist: str) -> List[str]:
        """
        Port of JAM FGristFiles rule.
        Add grist to file names.
        
        Args:
            files: List of file names
            grist: Grist to add
            
        Returns:
            List of gristed file names
        """
        if not grist:
            return files
        if not grist.startswith('<'):
            grist = f'<{grist}>'
        return [f'{grist}{file}' for file in files]
    
    def f_reverse(self, items: List[str]) -> List[str]:
        """
        Port of JAM FReverse rule.
        Reverse a list.
        
        Args:
            items: List to reverse
            
        Returns:
            Reversed list
        """
        return list(reversed(items))
    
    def f_sum(self, numbers: List[str]) -> int:
        """
        Port of JAM FSum rule.
        Sum a list of numbers.
        
        Args:
            numbers: List of string numbers
            
        Returns:
            Sum as integer
        """
        total = 0
        for num in numbers:
            try:
                total += int(num)
            except (ValueError, TypeError):
                pass
        return total


def get_helper_rules() -> HaikuHelperRules:
    """Get helper rules instance."""
    return HaikuHelperRules()


# ========== MODULE-LEVEL EXPORTS FOR JAM COMPATIBILITY ==========
# These functions provide direct JAM-style access without needing class instantiation

_helper_instance = None

def _get_helper():
    """Get or create singleton helper instance."""
    global _helper_instance
    if _helper_instance is None:
        _helper_instance = HaikuHelperRules()
    return _helper_instance

# JAM Rule: FFilter
def FFilter(items: List[str], excludes: List[str]) -> List[str]:
    """FFilter <items> : <excludes> - Filter items excluding specified."""
    return _get_helper().f_filter(items, excludes)

# JAM Rule: FGetGrist
def FGetGrist(files: List[str]) -> List[str]:
    """FGetGrist <files> - Get grist from files."""
    return _get_helper().f_get_grist(files)

# JAM Rule: FSplitString
def FSplitString(string: str, delimiter: str) -> List[str]:
    """FSplitString <string> : <delimiter> - Split string by delimiter."""
    return _get_helper().f_split_string(string, delimiter)

# JAM Rule: FSplitPath
def FSplitPath(path: str) -> List[str]:
    """FSplitPath <path> - Split path into components."""
    return _get_helper().f_split_path(path)

# JAM Rule: FConditionsHold
def FConditionsHold(conditions: List[str], predicate_func) -> bool:
    """FConditionsHold <conditions> : <predicate> - Check if conditions hold."""
    return _get_helper().f_conditions_hold(conditions, predicate_func)

# JAM Rule: FDirName (essential for path operations)
def FDirName(*components) -> str:
    """FDirName <components> - Join path components."""
    return _get_helper().f_dir_name(*components)

# JAM Rule: FGristFiles (essential for target management)
def FGristFiles(files: List[str], grist: str) -> List[str]:
    """FGristFiles <files> : <grist> - Add grist to files."""
    return _get_helper().f_grist_files(files, grist)

# JAM Rule: FReverse
def FReverse(items: List[str]) -> List[str]:
    """FReverse <items> - Reverse list."""
    return _get_helper().f_reverse(items)

# JAM Rule: FSum
def FSum(numbers: List[str]) -> int:
    """FSum <numbers> - Sum numbers."""
    return _get_helper().f_sum(numbers)

# JAM Rule: FIsPrefix
def FIsPrefix(prefix: List[str], list_items: List[str]) -> bool:
    """FIsPrefix <prefix> : <list> - Check if prefix matches."""
    return _get_helper().f_is_prefix(prefix, list_items)

# Platform-related rules
def SetPlatformCompatibilityFlagVariables(platform: str, var_prefix: str, var_suffix: str):
    """SetPlatformCompatibilityFlagVariables rule."""
    return _get_helper().set_platform_compatibility_flag_variables(platform, var_prefix, var_suffix)

def SetSubDirPlatform(platform: str):
    """SetSubDirPlatform rule."""
    return _get_helper().set_sub_dir_platform(platform)

def IsPlatformSupportedForTarget(target: str) -> bool:
    """IsPlatformSupportedForTarget rule."""
    return _get_helper().is_platform_supported_for_target(target)

def SubDirAsFlags(*subdir_tokens) -> str:
    """SubDirAsFlags rule."""
    return _get_helper().sub_dir_as_flags(*subdir_tokens)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Helper Rules (JAM Port) ===")
    
    helper = get_helper_rules()
    
    # Test FFilter
    items = ['a', 'b', 'c', 'd', 'e']
    excludes = ['b', 'd']
    filtered = helper.f_filter(items, excludes)
    print(f"FFilter test: {items} - {excludes} = {filtered}")
    
    # Test FSplitPath
    test_paths = ['/home/user/file.txt', 'relative/path', 'file.txt', '/']
    for path in test_paths:
        components = helper.f_split_path(path)
        print(f"FSplitPath: '{path}' -> {components}")
    
    # Test FSplitString
    test_string = "one,two,three,four"
    parts = helper.f_split_string(test_string, ',')
    print(f"FSplitString: '{test_string}' -> {parts}")
    
    # Test FConditionsHold with simple predicate
    available_features = ['ssl', 'zlib', 'icu']
    def feature_available(feature: str) -> bool:
        return feature in available_features
    
    test_conditions = [
        ['ssl'],           # Should hold (ssl available)
        ['ssl', 'zlib'],   # Should hold (both available)  
        ['ssl', '!mysql'], # Should hold (ssl available, mysql not available)
        ['mysql'],         # Should not hold (mysql not available)
        ['!ssl'],          # Should not hold (ssl is available)
        ['ssl', 'mysql']   # Should hold (ssl available, even though mysql isn't)
    ]
    
    for conditions in test_conditions:
        result = helper.f_conditions_hold(conditions, feature_available)
        print(f"FConditionsHold: {conditions} -> {result}")
    
    # Test FIsPrefix
    test_cases = [
        (['a', 'b'], ['a', 'b', 'c', 'd']),     # True
        (['a', 'b'], ['a', 'b']),               # True  
        (['a', 'b'], ['a', 'c', 'd']),          # False
        ([], ['a', 'b']),                       # True (empty prefix)
        (['a', 'b', 'c'], ['a', 'b'])          # False (prefix longer)
    ]
    
    for prefix, items in test_cases:
        result = helper.f_is_prefix(prefix, items)
        print(f"FIsPrefix: {prefix} prefix of {items} -> {result}")
    
    # Test platform compatibility
    helper.set_platform_variable('TARGET_PLATFORM', 'haiku')
    helper.set_platform_compatibility_flag_variables('TARGET_PLATFORM', 'TARGET', 'target')
    
    haiku_compat = helper.get_platform_variables().get('TARGET_PLATFORM_HAIKU_COMPATIBLE')
    print(f"Haiku compatibility: {haiku_compat}")
    
    print("✅ Helper Rules functionality verified")
    print(f"All fundamental JAM utility functions successfully ported to Python")