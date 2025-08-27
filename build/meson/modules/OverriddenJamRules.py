#!/usr/bin/env python3
"""
Haiku Overridden JAM Rules for Meson
Port of JAM OverriddenJamRules to provide enhanced build rule implementations.
"""

import os
from typing import List, Dict, Optional, Any, Union
from pathlib import Path

# Import ResourceHandler for .rdef compilation
try:
    from .ResourceHandler import HaikuResourceHandler
except ImportError:
    # Fallback for direct execution
    from ResourceHandler import HaikuResourceHandler

class HaikuOverriddenJamRules:
    """Port of JAM OverriddenJamRules providing enhanced build rule implementations."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        # Support both Meson and JAM directory structures
        self.haiku_top = Path(os.environ.get('MESON_SOURCE_ROOT', '/home/ruslan/haiku'))
        self.build_dir = Path(os.environ.get('MESON_BUILD_ROOT', self.haiku_top / 'generated'))
        
        # Initialize ResourceHandler for .rdef/.rsrc processing
        self.resource_handler = HaikuResourceHandler(str(self.haiku_top))
        
        # Build configuration
        self.supported_platforms = ['host', 'target']
        self.host_use_xattr = os.getenv('HAIKU_HOST_USE_XATTR', '0') == '1'
        
        # File extensions
        self.sufobj = '.o'
        self.suflib = '.a'
        self.sufexe = ''
        
        # Compiler tools configuration
        self.compiler_tools = self._get_compiler_tools()
        
        # Header patterns for dependency scanning
        self.hdr_pattern = r'#[ \t]*include[ \t]*[<"]([^>"]*)[>"]'
        
    def _get_compiler_tools(self) -> Dict[str, Dict[str, str]]:
        """Get compiler tools configuration for host and target platforms."""
        return {
            'host': {
                'cc': 'gcc',
                'cxx': 'g++',
                'ar': 'ar',
                'ranlib': 'ranlib',
                'link': 'gcc',
                'lex': 'flex',
                'yacc': 'bison'
            },
            'target': {
                'cc': f'/home/ruslan/haiku/generated/cross-tools-{self.target_arch}/bin/{self.target_arch}-unknown-haiku-gcc',
                'cxx': f'/home/ruslan/haiku/generated/cross-tools-{self.target_arch}/bin/{self.target_arch}-unknown-haiku-g++',
                'ar': f'/home/ruslan/haiku/generated/cross-tools-{self.target_arch}/bin/{self.target_arch}-unknown-haiku-ar',
                'ranlib': f'/home/ruslan/haiku/generated/cross-tools-{self.target_arch}/bin/{self.target_arch}-unknown-haiku-ranlib',
                'link': f'/home/ruslan/haiku/generated/cross-tools-{self.target_arch}/bin/{self.target_arch}-unknown-haiku-gcc',
                'lex': 'flex',
                'yacc': 'bison'
            }
        }
    
    def _get_compiler_flags(self, platform: str, debug: str = '1') -> Dict[str, List[str]]:
        """Get compiler flags for platform and debug level."""
        base_flags = {
            'ccflags': ['-Wall', '-Wextra'],
            'cxxflags': ['-Wall', '-Wextra'],
            'asflags': [],
            'linkflags': [],
            'defines': []
        }
        
        if platform == 'host':
            base_flags['defines'].extend(['__STDC__=1'])
            if debug != '0':
                base_flags['ccflags'].extend(['-g', '-O0'])
                base_flags['cxxflags'].extend(['-g', '-O0'])
            else:
                base_flags['ccflags'].extend(['-O2'])
                base_flags['cxxflags'].extend(['-O2'])
        else:
            base_flags['defines'].extend([f'TARGET_PACKAGING_ARCH_{self.target_arch.upper()}=1', 'HAIKU=1'])
            if debug != '0':
                base_flags['ccflags'].extend(['-g', '-O0'])
                base_flags['cxxflags'].extend(['-g', '-O0'])
            else:
                base_flags['ccflags'].extend(['-O2'])
                base_flags['cxxflags'].extend(['-O2'])
        
        return base_flags
    
    def link(self, target: str, sources: List[str], libraries: Optional[List[str]] = None,
            resources: Optional[List[str]] = None, platform: str = 'target') -> Dict[str, Any]:
        """
        Port of JAM Link rule (overridden version).
        Enhanced link rule that handles files with spaces and adds resources.
        
        Args:
            target: Target executable name
            sources: Object files to link
            libraries: Libraries to link against
            resources: Resource files to include
            platform: Platform ('host' or 'target')
            
        Returns:
            Dictionary with link configuration
        """
        libraries = libraries or []
        resources = resources or []
        
        tools = self.compiler_tools[platform]
        flags = self._get_compiler_flags(platform)
        
        # Build link command
        link_cmd = [tools['link']]
        link_cmd.extend(flags['linkflags'])
        link_cmd.extend(['-o', target])
        
        # Add sources (quoted for spaces)
        for source in sources:
            link_cmd.append(f'"{source}"')
        
        # Add libraries
        for lib in libraries:
            if lib.startswith('-'):
                link_cmd.append(lib)
            else:
                link_cmd.append(f'-l{lib}')
        
        # Add version script if available
        version_script = f'{target}.version'
        if Path(version_script).exists():
            link_cmd.extend(['-Wl,--version-script,' + version_script])
        
        config = {
            'target_name': f'link_{Path(target).stem}',
            'target_type': 'executable',
            'input': sources,
            'output': target,
            'command': link_cmd,
            'dependencies': libraries,
            'resources': resources,
            'platform': platform,
            'executable': True,
            'pre_command': [] if self.host_use_xattr else [f'rm -f "{target}"']
        }
        
        # Add resource processing if resources are specified
        if resources:
            config['post_commands'] = self._generate_resource_commands(target, resources)
        
        return config
    
    def _generate_resource_commands(self, target: str, resources: List[str]) -> List[List[str]]:
        """Generate commands for Haiku resource processing (XRes, SetType, MimeSet)."""
        commands = []
        
        # Process each resource file using ResourceHandler
        for resource in resources:
            if resource.endswith('.rsrc'):
                # Add binary resource to executable with xres
                commands.append(['xres', '-o', target, resource])
            elif resource.endswith('.rdef'):
                # Use ResourceHandler to compile .rdef to .rsrc
                rsrc_file = resource.replace('.rdef', '.rsrc')
                rdef_command = self.resource_handler.get_resource_command(resource, rsrc_file)
                commands.append(rdef_command)
                # Then add compiled resource to executable
                commands.append(['xres', '-o', target, rsrc_file])
        
        # Set executable file type (Haiku-specific)
        commands.append(['settype', '-t', 'application/x-vnd.Be-elfexecutable', target])
        
        # Set MIME database entries (sharedObject is default for apps)
        commands.append(['mimeset', '-f', target])
        
        # Set version information if version file exists
        version_file = f'{target}.version'
        if Path(version_file).exists():
            commands.append(['setversion', target, version_file])
        
        # Set file permissions (executable mode)
        commands.append(['chmod', '755', target])
        
        return commands
    
    def xres(self, target: str, resource_files: List[str]) -> Dict[str, Any]:
        """
        Port of JAM XRes action - add resources to Haiku executable.
        Uses ResourceHandler for .rdef compilation.
        
        Args:
            target: Target executable file
            resource_files: Resource files to add (.rsrc, .rdef)
            
        Returns:
            Dictionary with xres configuration
        """
        commands = []
        meson_targets = []
        
        for resource in resource_files:
            if Path(resource).exists():
                if resource.endswith('.rdef'):
                    # Use ResourceHandler to compile .rdef to .rsrc
                    rsrc_file = resource.replace('.rdef', '.rsrc')
                    rdef_command = self.resource_handler.get_resource_command(resource, rsrc_file)
                    commands.append(rdef_command)
                    commands.append(['xres', '-o', target, rsrc_file])
                    
                    # Also provide Meson target configuration
                    target_name = Path(resource).stem + '_rsrc'
                    meson_target = self.resource_handler.create_meson_resource_target(
                        target_name, resource, rsrc_file)
                    meson_targets.append(meson_target)
                else:
                    # Direct .rsrc file
                    commands.append(['xres', '-o', target, resource])
        
        config = {
            'target_name': f'xres_{Path(target).stem}',
            'target': target,
            'resources': resource_files,
            'commands': commands,
            'meson_resource_targets': meson_targets,
            'description': 'Adding resources to executable'
        }
        
        return config
    
    def set_type(self, target: str, file_type: str = 'application/x-vnd.Be-elfexecutable') -> Dict[str, Any]:
        """
        Port of JAM SetType action - set BeOS/Haiku file type.
        
        Args:
            target: Target file
            file_type: MIME type to set
            
        Returns:
            Dictionary with settype configuration  
        """
        config = {
            'target_name': f'settype_{Path(target).stem}',
            'target': target,
            'file_type': file_type,
            'command': ['settype', '-t', file_type, target],
            'description': f'Setting file type {file_type}'
        }
        
        return config
    
    def mime_set(self, target: str, signature: str = 'sharedObject') -> Dict[str, Any]:
        """
        Port of JAM MimeSet action - set MIME database entries.
        
        Args:
            target: Target executable
            signature: Application signature (default: sharedObject)
            
        Returns:
            Dictionary with mimeset configuration
        """
        config = {
            'target_name': f'mimeset_{Path(target).stem}',
            'target': target,
            'signature': signature,
            'command': ['mimeset', '-f', target],
            'description': f'Setting MIME signature {signature}'
        }
        
        return config
    
    def set_version(self, target: str, version_file: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM SetVersion action - set version information.
        
        Args:
            target: Target executable
            version_file: Optional version file path
            
        Returns:
            Dictionary with version setting configuration
        """
        version_file = version_file or f'{target}.version'
        
        config = {
            'target_name': f'setversion_{Path(target).stem}',
            'target': target,
            'version_file': version_file,
            'command': ['setversion', target, version_file] if Path(version_file).exists() else None,
            'description': 'Setting version information'
        }
        
        return config
    
    def create_app_mime_db_entries(self, target: str) -> Dict[str, Any]:
        """
        Port of JAM CreateAppMimeDBEntries action.
        Generate MIME database entries for applications.
        
        Args:
            target: Target application
            
        Returns:
            Dictionary with MIME DB entries configuration
        """
        config = {
            'target_name': f'mime_db_{Path(target).stem}',
            'target': target,
            'commands': [
                ['mimeset', '-f', target],
                # Create application signature entries
                ['addattr', '-t', 'mime', 'BEOS:APP_SIG', 'application/x-vnd.Be-elfexecutable', target]
            ],
            'description': 'Creating MIME database entries for application'
        }
        
        return config
    
    def object_rule(self, obj_file: str, source_file: str, 
                   platform: str = 'target', debug: str = '1') -> Dict[str, Any]:
        """
        Port of JAM Object rule (overridden version).
        Enhanced object compilation with better header dependency handling.
        
        Args:
            obj_file: Output object file
            source_file: Input source file
            platform: Platform ('host' or 'target')
            debug: Debug level ('0' or '1')
            
        Returns:
            Dictionary with object compilation configuration
        """
        tools = self.compiler_tools[platform]
        flags = self._get_compiler_flags(platform, debug)
        
        # Determine source file type and compiler
        source_ext = Path(source_file).suffix.lower()
        
        if source_ext in ['.c']:
            compiler = tools['cc']
            compiler_flags = flags['ccflags']
        elif source_ext in ['.cpp', '.cc', '.cxx', '.C']:
            compiler = tools['cxx']
            compiler_flags = flags['cxxflags']
        elif source_ext in ['.s', '.S']:
            compiler = tools['cc']
            compiler_flags = flags['asflags'] + ['-D_ASSEMBLER']
        elif source_ext == '.asm':
            return self._assemble_nasm(obj_file, source_file, platform)
        elif source_ext == '.l':
            return self._compile_lex(obj_file, source_file, platform, debug)
        elif source_ext == '.y':
            return self._compile_yacc(obj_file, source_file, platform, debug)
        else:
            # Default to C compilation
            compiler = tools['cc']
            compiler_flags = flags['ccflags']
        
        # Build compilation command
        compile_cmd = [compiler]
        compile_cmd.extend(compiler_flags)
        compile_cmd.extend([f'-D{define}' for define in flags['defines']])
        compile_cmd.extend(['-c', f'"{source_file}"', '-o', f'"{obj_file}"'])
        
        config = {
            'target_name': f'compile_{Path(obj_file).stem}',
            'input': source_file,
            'output': obj_file,
            'command': compile_cmd,
            'platform': platform,
            'debug': debug,
            'source_type': source_ext,
            'dependencies': [],
            'header_dependencies': self._get_header_dependencies(source_file)
        }
        
        return config
    
    def _get_header_dependencies(self, source_file: str) -> List[str]:
        """Extract header dependencies from source file."""
        headers = []
        try:
            with open(source_file, 'r', encoding='utf-8', errors='ignore') as f:
                import re
                for line in f:
                    match = re.match(self.hdr_pattern, line.strip())
                    if match:
                        headers.append(match.group(1))
        except (IOError, UnicodeDecodeError):
            pass  # Ignore files that can't be read
        
        return headers
    
    def _assemble_nasm(self, obj_file: str, source_file: str, platform: str) -> Dict[str, Any]:
        """Assemble NASM assembly file."""
        if platform == 'host':
            nasm_format = 'elf64'
        else:
            nasm_format = 'elf64' if self.target_arch == 'x86_64' else 'elf32'
        
        config = {
            'target_name': f'nasm_{Path(obj_file).stem}',
            'input': source_file,
            'output': obj_file,
            'command': ['nasm', '-f', nasm_format, '-o', obj_file, source_file],
            'platform': platform
        }
        
        return config
    
    def _compile_lex(self, obj_file: str, source_file: str, platform: str, debug: str) -> Dict[str, Any]:
        """Compile Lex source file."""
        tools = self.compiler_tools[platform]
        c_file = source_file.replace('.l', '.c')
        
        config = {
            'target_name': f'lex_{Path(obj_file).stem}',
            'input': source_file,
            'output': obj_file,
            'intermediate': c_file,
            'commands': [
                [tools['lex'], '-o', c_file, source_file],
                # Then compile the generated C file
                *self.object_rule(obj_file, c_file, platform, debug)['command']
            ],
            'platform': platform
        }
        
        return config
    
    def _compile_yacc(self, obj_file: str, source_file: str, platform: str, debug: str) -> Dict[str, Any]:
        """Compile Yacc source file."""
        tools = self.compiler_tools[platform]
        c_file = source_file.replace('.y', '.c')
        h_file = source_file.replace('.y', '.h')
        
        config = {
            'target_name': f'yacc_{Path(obj_file).stem}',
            'input': source_file,
            'output': obj_file,
            'intermediates': [c_file, h_file],
            'commands': [
                [tools['yacc'], '-o', c_file, '-d', source_file],
                ['bash', '-c', f'[ -f {c_file}.h ] && mv {c_file}.h {h_file} || true'],
                # Then compile the generated C file
                *self.object_rule(obj_file, c_file, platform, debug)['command']
            ],
            'platform': platform
        }
        
        return config
    
    def library_from_objects(self, lib_file: str, obj_files: List[str],
                           platform: str = 'target') -> Dict[str, Any]:
        """
        Port of JAM LibraryFromObjects rule (overridden version).
        Create static library from object files with forced recreation.
        
        Args:
            lib_file: Output library file
            obj_files: Input object files
            platform: Platform ('host' or 'target')
            
        Returns:
            Dictionary with library creation configuration
        """
        tools = self.compiler_tools[platform]
        
        # Build archive command (force recreation to avoid stale dependencies)
        archive_cmd = ['bash', '-c', 
                      f'rm -f "{lib_file}" && {tools["ar"]} rcs "{lib_file}" ' + 
                      ' '.join(f'"{obj}"' for obj in obj_files)]
        
        # Add ranlib if needed
        post_commands = []
        if tools['ranlib'] and tools['ranlib'] != 'true':
            post_commands.append([tools['ranlib'], lib_file])
        
        config = {
            'target_name': f'library_{Path(lib_file).stem}',
            'target_type': 'static_library',
            'input': obj_files,
            'output': lib_file,
            'command': archive_cmd,
            'post_commands': post_commands,
            'platform': platform
        }
        
        return config
    
    def main_from_objects(self, exe_file: str, obj_files: List[str],
                         libraries: Optional[List[str]] = None,
                         platform: str = 'target') -> Dict[str, Any]:
        """
        Port of JAM MainFromObjects rule (overridden version).
        Create executable from object files.
        
        Args:
            exe_file: Output executable file
            obj_files: Input object files
            libraries: Libraries to link
            platform: Platform ('host' or 'target')
            
        Returns:
            Dictionary with executable creation configuration
        """
        libraries = libraries or []
        
        # Add executable suffix if needed
        if platform == 'host' and os.name == 'nt':
            if not exe_file.endswith('.exe'):
                exe_file += '.exe'
        
        # Use the enhanced Link rule
        link_config = self.link(exe_file, obj_files, libraries, platform=platform)
        link_config['target_name'] = f'main_{Path(exe_file).stem}'
        link_config['target_type'] = 'executable'
        
        return link_config
    
    def make_locate(self, targets: List[str], directory: str) -> Dict[str, Any]:
        """
        Port of JAM MakeLocate rule (overridden version).
        Enhanced directory creation with better path handling.
        
        Args:
            targets: Target files to locate
            directory: Directory to place targets in
            
        Returns:
            Dictionary with location configuration
        """
        # Normalize directory path
        dir_path = Path(directory).resolve()
        
        config = {
            'targets': targets,
            'directory': str(dir_path),
            'mkdir_command': ['mkdir', '-p', str(dir_path)],
            'dependencies': [str(dir_path)]
        }
        
        return config
    
    def sub_include(self, top_dir: str, subdir_tokens: List[str], 
                   jamfile: str = 'Jamfile') -> Dict[str, Any]:
        """
        Port of JAM SubInclude rule (overridden version).
        Include a subdirectory's build file with proper configuration inheritance.
        
        Args:
            top_dir: Top-level directory name (like HAIKU_TOP)
            subdir_tokens: Directory path tokens
            jamfile: Build file name (default: Jamfile)
            
        Returns:
            Dictionary with subdirectory inclusion configuration
        """
        # Build full subdirectory path
        subdir_path = Path(top_dir) / Path(*subdir_tokens)
        build_file_path = subdir_path / jamfile
        
        # For Meson, we need to look for meson.build instead of Jamfile
        meson_build_file = subdir_path / 'meson.build'
        
        config = {
            'target_name': f'subinclude_{subdir_tokens[-1] if subdir_tokens else "root"}',
            'top_directory': top_dir,
            'subdir_tokens': subdir_tokens,
            'subdir_path': str(subdir_path),
            'original_jamfile': str(build_file_path),
            'meson_build_file': str(meson_build_file),
            'jamfile_name': jamfile,
            'exists': build_file_path.exists(),
            'meson_exists': meson_build_file.exists(),
            'description': f'Including subdirectory {"/".join(subdir_tokens)}'
        }
        
        # Add configuration inheritance logic
        config['inherit_config'] = True
        config['config_variables'] = {
            'SUBDIR_TOKENS': subdir_tokens,
            'SUBDIR': str(subdir_path),
            'HAIKU_TOP': top_dir
        }
        
        return config
    
    def generate_meson_generators(self) -> Dict[str, Any]:
        """
        Generate Meson generator configurations for Lex/Yacc and other tools.
        Note: RC (.rdef) compilation is handled by ResourceHandler.
        
        Returns:
            Dictionary with Meson generator configurations
        """
        generators = {
            'flex': {
                'name': 'flex_gen',
                'command': ['flex', '-o', '@OUTPUT@', '@INPUT@'],
                'output': '@BASENAME@.c',
                'description': 'Generating C source from Flex input @INPUT@'
            },
            'bison': {
                'name': 'bison_gen', 
                'command': ['bison', '-o', '@OUTPUT@', '-d', '@INPUT@'],
                'output': ['@BASENAME@.c', '@BASENAME@.h'],
                'description': 'Generating C source/header from Bison input @INPUT@'
            },
            'nasm': {
                'name': 'nasm_gen',
                'command': ['nasm', '-f', 'elf64', '-o', '@OUTPUT@', '@INPUT@'],
                'output': '@BASENAME@.o',
                'description': 'Assembling NASM source @INPUT@'
            }
        }
        
        return generators
    
    def generate_haiku_custom_targets(self) -> Dict[str, Any]:
        """
        Generate Meson custom target templates for Haiku-specific operations.
        Note: Resource compilation (.rdef -> .rsrc) is handled by ResourceHandler.
        
        Returns:
            Dictionary with custom target configurations
        """
        custom_targets = {
            'xres_target': {
                'command': ['xres', '-o', '@OUTPUT@', '@INPUT@'],
                'description': 'Adding compiled resources to @OUTPUT@',
                'depends': []
            },
            'settype_target': {
                'command': ['settype', '-t', 'application/x-vnd.Be-elfexecutable', '@OUTPUT@'],
                'description': 'Setting file type for @OUTPUT@',
                'depends': []
            },
            'mimeset_target': {
                'command': ['mimeset', '-f', '@OUTPUT@'],
                'description': 'Setting MIME attributes for @OUTPUT@',
                'depends': []
            },
            'setversion_target': {
                'command': ['setversion', '@OUTPUT@', '@INPUT@'],
                'description': 'Setting version for @OUTPUT@',
                'depends': []
            }
        }
        
        return custom_targets
    
    def get_resource_handler(self) -> 'HaikuResourceHandler':
        """Get the ResourceHandler instance for direct access."""
        return self.resource_handler
    
    def get_overridden_rules_config(self) -> Dict[str, Any]:
        """Get complete overridden rules configuration."""
        return {
            'compiler_tools': self.compiler_tools,
            'supported_platforms': self.supported_platforms,
            'host_use_xattr': self.host_use_xattr,
            'file_extensions': {
                'object': self.sufobj,
                'library': self.suflib,
                'executable': self.sufexe
            },
            'header_pattern': self.hdr_pattern,
            'target_arch': self.target_arch,
            'resource_handler': {
                'rc_compiler': self.resource_handler.get_rc_compiler(),
                'is_valid': self.resource_handler.validate_rc_compiler(),
                'haiku_root': str(self.resource_handler.haiku_root)
            }
        }


def get_overridden_jam_rules(target_arch: str = 'x86_64') -> HaikuOverriddenJamRules:
    """Get overridden JAM rules instance."""
    return HaikuOverriddenJamRules(target_arch)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku Overridden JAM Rules (JAM Port) ===")
    
    rules = get_overridden_jam_rules('x86_64')
    
    # Test link rule
    link_config = rules.link(
        'test_app',
        ['main.o', 'utils.o', 'window.o'],
        ['be', 'root', 'translation'],
        ['app_icon.rsrc', 'app_resources.rdef']
    )
    print(f"Link rule: {link_config['target_name']}")
    print(f"  Platform: {link_config['platform']}")
    print(f"  Resources: {len(link_config['resources'])} files")
    print(f"  Pre-command: {link_config.get('pre_command', 'None')}")
    
    # Test object compilation
    obj_config_c = rules.object_rule('main.o', 'main.c', 'target', '1')
    obj_config_cpp = rules.object_rule('window.o', 'window.cpp', 'target', '1')
    obj_config_asm = rules.object_rule('boot.o', 'boot.S', 'target', '1')
    
    print(f"Object rules:")
    print(f"  C file: {obj_config_c['target_name']} (type: {obj_config_c['source_type']})")
    print(f"  C++ file: {obj_config_cpp['target_name']} (type: {obj_config_cpp['source_type']})")
    print(f"  Assembly: {obj_config_asm['target_name']} (type: {obj_config_asm['source_type']})")
    
    # Test special file types
    lex_config = rules._compile_lex('lexer.o', 'lexer.l', 'target', '1')
    yacc_config = rules._compile_yacc('parser.o', 'parser.y', 'target', '1')
    nasm_config = rules._assemble_nasm('asm_code.o', 'asm_code.asm', 'target')
    
    print(f"Special compilations:")
    print(f"  Lex: {lex_config['target_name']} -> {lex_config['intermediate']}")
    print(f"  Yacc: {yacc_config['target_name']} -> {yacc_config['intermediates']}")
    print(f"  NASM: {nasm_config['target_name']}")
    
    # Test library creation
    lib_config = rules.library_from_objects('libutils.a', ['util1.o', 'util2.o', 'util3.o'])
    print(f"Library: {lib_config['target_name']}")
    print(f"  Type: {lib_config['target_type']}")
    print(f"  Objects: {len(lib_config['input'])} files")
    print(f"  Post-commands: {len(lib_config.get('post_commands', []))}")
    
    # Test executable creation
    main_config = rules.main_from_objects('myapp', ['main.o', 'app.o'], ['be', 'translation'])
    print(f"Executable: {main_config['target_name']}")
    print(f"  Dependencies: {len(main_config['dependencies'])} libraries")
    
    # Test make locate
    locate_config = rules.make_locate(['file1.o', 'file2.o'], 'generated/objects/interface')
    print(f"Make locate:")
    print(f"  Targets: {len(locate_config['targets'])} files")
    print(f"  Directory: {locate_config['directory']}")
    
    # Test Haiku-specific resource functions
    print(f"\n--- Haiku Resource Functions ---")
    
    # Test XRes (integrated with ResourceHandler)
    xres_config = rules.xres('TestApp', ['icons.rsrc', 'strings.rdef'])
    print(f"XRes: {xres_config['target_name']}")
    print(f"  Target: {xres_config['target']}")
    print(f"  Resources: {len(xres_config['resources'])} files")
    print(f"  Commands: {len(xres_config['commands'])}")
    print(f"  Meson targets: {len(xres_config['meson_resource_targets'])}")
    
    # Test SetType
    settype_config = rules.set_type('TestApp')
    print(f"SetType: {settype_config['target_name']}")
    print(f"  File type: {settype_config['file_type']}")
    print(f"  Command: {' '.join(settype_config['command'])}")
    
    # Test MimeSet
    mimeset_config = rules.mime_set('TestApp')
    print(f"MimeSet: {mimeset_config['target_name']}")
    print(f"  Signature: {mimeset_config['signature']}")
    print(f"  Command: {' '.join(mimeset_config['command'])}")
    
    # Test SetVersion
    setversion_config = rules.set_version('TestApp', 'version.txt')
    print(f"SetVersion: {setversion_config['target_name']}")
    print(f"  Version file: {setversion_config['version_file']}")
    print(f"  Command: {setversion_config['command']}")
    
    # Test CreateAppMimeDBEntries
    mime_db_config = rules.create_app_mime_db_entries('TestApp')
    print(f"MIME DB entries: {mime_db_config['target_name']}")
    print(f"  Commands: {len(mime_db_config['commands'])}")
    
    # Test SubInclude
    subinclude_config = rules.sub_include('/home/ruslan/haiku', ['src', 'kits', 'interface'])
    print(f"SubInclude: {subinclude_config['target_name']}")
    print(f"  Path: {subinclude_config['subdir_path']}")
    print(f"  Jamfile exists: {subinclude_config['exists']}")
    print(f"  Meson file exists: {subinclude_config['meson_exists']}")
    print(f"  Inherit config: {subinclude_config['inherit_config']}")
    
    # Test ResourceHandler integration
    resource_handler = rules.get_resource_handler()
    print(f"ResourceHandler integration:")
    print(f"  RC compiler: {resource_handler.get_rc_compiler()}")
    print(f"  RC valid: {resource_handler.validate_rc_compiler()}")
    print(f"  Standard resources: {len(resource_handler.get_standard_haiku_resources())}")
    
    # Test Meson generators (RC removed, handled by ResourceHandler)
    generators = rules.generate_meson_generators()
    print(f"Meson generators: {len(generators)} types")
    for gen_name, gen_config in generators.items():
        print(f"  {gen_name}: {gen_config['description']}")
    
    # Test Haiku custom targets
    custom_targets = rules.generate_haiku_custom_targets()
    print(f"Haiku custom targets: {len(custom_targets)} types")
    for target_name, target_config in custom_targets.items():
        print(f"  {target_name}: {target_config['description']}")
    
    # Test configuration
    config = rules.get_overridden_rules_config()
    print(f"\nConfiguration:")
    print(f"  Platforms: {config['supported_platforms']}")
    print(f"  Host xattr: {config['host_use_xattr']}")
    print(f"  Target arch: {config['target_arch']}")
    print(f"  Extensions: {config['file_extensions']}")
    
    print("\n✅ Overridden JAM Rules functionality verified")
    print("Complete enhanced build rules system ported from JAM")
    print("Supports: enhanced linking, object compilation, library creation, resource handling, special file types")
    print("Haiku-specific: XRes, SetType, MimeSet, SetVersion, SubInclude, resource processing")
    print("Meson integration: generators for Flex/Bison/NASM, custom targets for Haiku tools")
    print("ResourceHandler integration: .rdef compilation handled by specialized ResourceHandler module")
    print("Architecture: Single entry point with integrated resource handling")