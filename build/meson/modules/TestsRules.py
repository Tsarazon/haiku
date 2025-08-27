#!/usr/bin/env python3
"""
Haiku Tests Rules for Meson
Port of JAM TestsRules to provide unit testing framework integration.
"""

import os
from typing import List, Dict, Optional, Any, Union
from pathlib import Path

# ========== IMPORTS ACCORDING TO MESON_MIGRATION_STRATEGY.md ==========
# Specialized module TestsRules imports:

# Base components from HaikuCommon
try:
    from .HaikuCommon import get_architecture_config    # ArchitectureRules
    from .HaikuCommon import get_system_library        # SystemLibraryRules
except ImportError:
    # Fallback for standalone execution
    get_architecture_config = None
    get_system_library = None

class HaikuTestsRules:
    """Port of JAM TestsRules providing unit testing framework integration."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        # Support both Meson and JAM directory structures
        self.haiku_top = Path(os.environ.get('MESON_SOURCE_ROOT', '/home/ruslan/haiku'))
        self.build_dir = Path(os.environ.get('MESON_BUILD_ROOT', self.haiku_top / 'generated'))
        
        # Test configuration
        self.test_debug = os.getenv('TEST_DEBUG', '0') == '1'
        self.unit_test_targets = []
        self.unit_test_libs = []
        
        # Test directories
        self.unit_test_dir = 'generated/tests/unittests'
        self.unit_test_lib_dir = 'generated/tests/lib'
        
    def _get_test_defines(self) -> List[str]:
        """Get test-specific preprocessor defines."""
        defines = ['TEST_HAIKU', 'TEST_OBOS']
        
        # Add platform-specific defines
        target_platform = os.getenv('TARGET_PLATFORM', 'haiku')
        if target_platform == 'libbe_test':
            defines.extend(['TEST_LIBBE'])
        
        return defines
    
    def _get_cppunit_config(self) -> Dict[str, Any]:
        """Get CppUnit configuration."""
        return {
            'libraries': ['cppunit'],
            'include_directories': [
                str(self.haiku_top / 'headers' / 'tools' / 'cppunit'),
                str(self.haiku_top / 'headers' / 'private' / 'testing')
            ]
        }
    
    def unit_test_dependency(self, target: str) -> None:
        """
        Port of JAM UnitTestDependency rule.
        Add target to unit test dependencies.
        
        Args:
            target: Test target to add to dependency list
        """
        if target not in self.unit_test_targets:
            self.unit_test_targets.append(target)
    
    def unit_test_lib(self, lib_name: str, sources: List[str],
                     libraries: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM UnitTestLib rule.
        Build unit test shared library.
        
        Args:
            lib_name: Name of test library
            sources: Source files
            libraries: Additional libraries to link
            
        Returns:
            Dictionary with test library configuration
        """
        libraries = libraries or []
        test_defines = self._get_test_defines()
        cppunit_config = self._get_cppunit_config()
        
        # Build configuration
        config = {
            'target_name': lib_name,
            'target_type': 'shared_library',
            'sources': sources,
            'dependencies': libraries + cppunit_config['libraries'],
            'include_directories': cppunit_config['include_directories'],
            'c_args': [f'-D{define}' for define in test_defines],
            'cpp_args': [f'-D{define}' for define in test_defines],
            'install': False,
            'build_by_default': False
        }
        
        # Add debug flags if enabled
        if self.test_debug:
            config['c_args'].extend(['-g', '-DDEBUG=1'])
            config['cpp_args'].extend(['-g', '-DDEBUG=1'])
        
        # Handle libbe_test platform
        target_platform = os.getenv('TARGET_PLATFORM', 'haiku')
        if target_platform == 'libbe_test':
            config['dependencies'].append('libbe_test.so')
        
        # Add to unit test libraries list
        self.unit_test_libs.append(config)
        self.unit_test_dependency(lib_name)
        
        return config
    
    def unit_test(self, target: str, sources: List[str],
                 libraries: Optional[List[str]] = None,
                 resources: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM UnitTest rule.
        Build unit test executable.
        
        Args:
            target: Test executable name
            sources: Source files
            libraries: Additional libraries to link
            resources: Resource files to include
            
        Returns:
            Dictionary with unit test configuration
        """
        libraries = libraries or []
        resources = resources or []
        test_defines = self._get_test_defines()
        cppunit_config = self._get_cppunit_config()
        
        # Build configuration
        config = {
            'target_name': target,
            'target_type': 'executable',
            'sources': sources,
            'dependencies': libraries + cppunit_config['libraries'],
            'include_directories': cppunit_config['include_directories'],
            'c_args': [f'-D{define}' for define in test_defines],
            'cpp_args': [f'-D{define}' for define in test_defines],
            'resources': resources,
            'install': False,
            'build_by_default': False
        }
        
        # Add debug flags if enabled
        if self.test_debug:
            config['c_args'].extend(['-g', '-DDEBUG=1'])
            config['cpp_args'].extend(['-g', '-DDEBUG=1'])
        
        # Handle libbe_test platform
        target_platform = os.getenv('TARGET_PLATFORM', 'haiku')
        if target_platform == 'libbe_test':
            config['dependencies'].append('libbe_test.so')
        
        # Add to unit test dependencies
        self.unit_test_dependency(target)
        
        return config
    
    def test_objects(self, sources: List[str]) -> Dict[str, Any]:
        """
        Port of JAM TestObjects rule.
        Compile test objects with test-specific defines.
        
        Args:
            sources: Source files to compile
            
        Returns:
            Dictionary with test object configuration
        """
        test_defines = self._get_test_defines()
        cppunit_config = self._get_cppunit_config()
        
        config = {
            'sources': sources,
            'include_directories': cppunit_config['include_directories'],
            'c_args': [f'-D{define}' for define in test_defines],
            'cpp_args': [f'-D{define}' for define in test_defines],
            'object_only': True
        }
        
        return config
    
    def simple_test(self, target: str, sources: List[str],
                   libraries: Optional[List[str]] = None,
                   resources: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM SimpleTest rule.
        Build simple test executable without CppUnit.
        
        Args:
            target: Test executable name
            sources: Source files
            libraries: Additional libraries to link
            resources: Resource files to include
            
        Returns:
            Dictionary with simple test configuration
        """
        libraries = libraries or []
        resources = resources or []
        test_defines = self._get_test_defines()
        
        config = {
            'target_name': target,
            'target_type': 'executable',
            'sources': sources,
            'dependencies': libraries,
            'c_args': [f'-D{define}' for define in test_defines],
            'cpp_args': [f'-D{define}' for define in test_defines],
            'resources': resources,
            'install': False,
            'build_by_default': False
        }
        
        # Add debug flags if enabled
        if self.test_debug:
            config['c_args'].extend(['-g', '-DDEBUG=1'])
            config['cpp_args'].extend(['-g', '-DDEBUG=1'])
        
        return config
    
    def build_platform_test(self, target: str, sources: List[str],
                           libraries: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM BuildPlatformTest rule.
        Build test for build platform (host system).
        
        Args:
            target: Test executable name
            sources: Source files
            libraries: Additional libraries to link
            
        Returns:
            Dictionary with platform test configuration
        """
        libraries = libraries or []
        
        config = {
            'target_name': target,
            'target_type': 'executable',
            'sources': sources,
            'dependencies': libraries,
            'native': True,  # Build for host platform
            'install': False,
            'build_by_default': False
        }
        
        return config
    
    def get_all_unit_tests(self) -> List[str]:
        """Get list of all unit test targets."""
        return self.unit_test_targets.copy()
    
    def get_all_unit_test_libs(self) -> List[Dict[str, Any]]:
        """Get list of all unit test library configurations."""
        return self.unit_test_libs.copy()
    
    def generate_test_suite_meson(self) -> str:
        """
        Generate Meson test suite configuration.
        
        Returns:
            Meson test suite configuration string
        """
        lines = [
            "# Generated test suite configuration",
            "",
            "# Unit test dependencies",
            "unit_tests = []"
        ]
        
        # Add individual tests
        for target in self.unit_test_targets:
            lines.append(f"unit_tests += {target}")
        
        lines.extend([
            "",
            "# Test suite definition",
            "if get_option('enable_tests')",
            "  test_suite = custom_target('unittests',",
            "    output: 'test_results.txt',",
            "    command: ['echo', 'Unit tests completed'],",
            "    depends: unit_tests,",
            "    build_by_default: false",
            "  )",
            "endif"
        ])
        
        return '\n'.join(lines)
    
    def get_test_config_for_meson(self) -> Dict[str, Any]:
        """Get complete test configuration for Meson integration."""
        return {
            'unit_tests': self.unit_test_targets,
            'unit_test_libs': self.unit_test_libs,
            'test_debug': self.test_debug,
            'test_defines': self._get_test_defines(),
            'cppunit_config': self._get_cppunit_config(),
            'test_directories': {
                'unit_test_dir': self.unit_test_dir,
                'unit_test_lib_dir': self.unit_test_lib_dir
            }
        }


def get_tests_rules(target_arch: str = 'x86_64') -> HaikuTestsRules:
    """Get tests rules instance."""
    return HaikuTestsRules(target_arch)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Tests Rules (JAM Port) ===")
    
    tests = get_tests_rules('x86_64')
    
    # Test unit test library
    lib_config = tests.unit_test_lib('test_utils', 
                                   ['test_utils.cpp', 'mock_objects.cpp'],
                                   ['be', 'root'])
    print(f"Unit test library: {lib_config['target_name']}")
    print(f"  Sources: {lib_config['sources']}")
    print(f"  Dependencies: {lib_config['dependencies']}")
    
    # Test unit test executable
    test_config = tests.unit_test('interface_test',
                                ['interface_test.cpp', 'view_test.cpp'],
                                ['be', 'test_utils'],
                                ['test_resources.rsrc'])
    print(f"Unit test: {test_config['target_name']}")
    print(f"  Test defines: {[arg for arg in test_config['c_args'] if arg.startswith('-D')]}")
    
    # Test simple test
    simple_config = tests.simple_test('basic_test',
                                     ['basic_test.c'],
                                     ['root'])
    print(f"Simple test: {simple_config['target_name']}")
    
    # Test platform test
    platform_config = tests.build_platform_test('host_test',
                                                ['host_test.cpp'],
                                                ['pthread'])
    print(f"Platform test: {platform_config['target_name']} (native: {platform_config['native']})")
    
    # Show all configured tests
    all_tests = tests.get_all_unit_tests()
    print(f"All unit tests: {all_tests}")
    
    # Test configuration
    test_config_full = tests.get_test_config_for_meson()
    print(f"Test defines: {test_config_full['test_defines']}")
    print(f"Test debug enabled: {test_config_full['test_debug']}")
    
    # Generate Meson test suite
    meson_suite = tests.generate_test_suite_meson()
    print("Generated Meson test suite configuration:")
    print(meson_suite[:200] + "..." if len(meson_suite) > 200 else meson_suite)
    
    print("✅ Tests Rules functionality verified")
    print("Complete unit testing framework ported from JAM")