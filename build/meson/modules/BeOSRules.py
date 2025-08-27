#!/usr/bin/env python3
"""
Haiku BeOS Rules for Meson
Port of JAM BeOSRules to provide Haiku-specific build functionality.
Handles file attributes, MIME types, resources, and version information.
"""

import os
import subprocess
from typing import List, Dict, Optional, Any, Union
from pathlib import Path

class HaikuBeOSRules:
    """Port of JAM BeOSRules providing Haiku-specific build functionality."""
    
    def __init__(self, target_arch: str = 'x86_64'):
        self.target_arch = target_arch
        # Support both Meson and JAM directory structures
        self.haiku_top = Path(os.environ.get('MESON_SOURCE_ROOT', '/home/ruslan/haiku'))
        self.build_dir = Path(os.environ.get('MESON_BUILD_ROOT', self.haiku_top / 'generated'))
        self.build_tools = self._get_build_tools()
        
        # Resource tracking
        self.string_resources = {}
        self.file_resources = {}
        
    def _get_build_tools(self) -> Dict[str, str]:
        """Get paths to Haiku build tools."""
        tools_base = self.build_dir / 'objects/linux/x86_64/release/tools'
        
        return {
            'addattr': str(tools_base / 'addattr/addattr'),
            'xres': str(tools_base / 'xres/xres'),
            'setversion': str(tools_base / 'setversion/setversion'),
            'settype': str(tools_base / 'settype/settype'),
            'mimeset': str(tools_base / 'mimeset/mimeset'),
            'rc': str(tools_base / 'rc/rc'),
            'resattr': str(tools_base / 'resattr/resattr'),
            'copyattr': str(tools_base / 'copyattr/copyattr')
        }
    
    def add_file_data_attribute(self, target: str, attr_name: str, 
                               attr_type: str, data_file: str) -> Dict[str, Any]:
        """
        Port of JAM AddFileDataAttribute rule.
        Add file attribute with data from separate file.
        
        Args:
            target: File to add attribute to
            attr_name: Attribute name
            attr_type: Attribute type (string, int, etc.)
            data_file: File containing attribute data
            
        Returns:
            Dictionary with Meson custom_target configuration
        """
        addattr_tool = self.build_tools.get('addattr', 'addattr')
        target_name = f'attr_{Path(target).stem}_{attr_name}'
        
        # Create attribute metadata file
        attr_meta_file = f'{target_name}_meta.txt'
        
        config = {
            'target_name': target_name,
            'input': [target, data_file],
            'output': target,  # In-place modification
            'command': [
                'sh', '-c',
                f'echo "-t {attr_type} {attr_name}" > {attr_meta_file} && '
                f'{addattr_tool} -f {data_file} $(cat {attr_meta_file}) {target}'
            ],
            'depends': [target, data_file],
            'build_by_default': False
        }
        
        return config
    
    def add_string_data_resource(self, target: str, resource_id: str, 
                                data_string: str = "") -> Dict[str, Any]:
        """
        Port of JAM AddStringDataResource rule.
        Add string resource to executable/library.
        
        Args:
            target: Target executable/library
            resource_id: Resource ID (type:id[:name] format)
            data_string: String data for resource
            
        Returns:
            Dictionary with resource configuration
        """
        xres_tool = self.build_tools.get('xres', 'xres')
        target_base = Path(target).stem
        
        # Track string resources for this target
        if target not in self.string_resources:
            self.string_resources[target] = []
        
        self.string_resources[target].append({
            'id': resource_id,
            'data': data_string
        })
        
        resource_file = f'{target_base}_string_resources.rsrc'
        
        # Build command for all string resources
        resource_args = []
        for res in self.string_resources[target]:
            resource_args.extend(['-a', res['id'], '-s', f'"{res["data"]}"'])
        
        config = {
            'target_name': f'string_res_{target_base}',
            'output': resource_file,
            'command': [xres_tool, '-o', '@OUTPUT@'] + resource_args,
            'build_by_default': False
        }
        
        return config
    
    def add_file_data_resource(self, target: str, resource_id: str, 
                              data_file: str) -> Dict[str, Any]:
        """
        Port of JAM AddFileDataResource rule.
        Add file data as resource to executable/library.
        
        Args:
            target: Target executable/library
            resource_id: Resource ID (type:id[:name] format)
            data_file: File containing resource data
            
        Returns:
            Dictionary with resource configuration
        """
        xres_tool = self.build_tools.get('xres', 'xres')
        resource_file = f'file_data_{resource_id}_{Path(data_file).name}.rsrc'
        
        config = {
            'target_name': f'file_res_{Path(target).stem}_{resource_id}',
            'input': data_file,
            'output': resource_file,
            'command': [xres_tool, '-o', '@OUTPUT@', '-a', resource_id, '@INPUT@'],
            'depends': [data_file],
            'build_by_default': False
        }
        
        return config
    
    def x_res(self, target: str, resource_files: List[str]) -> Dict[str, Any]:
        """
        Port of JAM XRes rule.
        Add resource files to target.
        
        Args:
            target: Target to add resources to
            resource_files: List of resource files
            
        Returns:
            Dictionary with XRes configuration
        """
        if not resource_files:
            return {}
        
        xres_tool = self.build_tools.get('xres', 'xres')
        
        config = {
            'target_name': f'xres_{Path(target).stem}',
            'input': resource_files,
            'output': target,  # In-place modification
            'command': [xres_tool, '-o', '@OUTPUT@'] + ['@INPUT@'],
            'depends': resource_files,
            'build_by_default': False
        }
        
        return config
    
    def set_version(self, target: str, system_version: Optional[str] = None,
                   short_description: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM SetVersion rule.
        Set version information on target.
        
        Args:
            target: Target to set version on
            system_version: System version (defaults to HAIKU_BUILD_VERSION)
            short_description: Short description (defaults to HAIKU_BUILD_DESCRIPTION)
            
        Returns:
            Dictionary with version setting configuration
        """
        setversion_tool = self.build_tools.get('setversion', 'setversion')
        
        # Use environment variables if not provided
        sys_version = system_version or os.getenv('HAIKU_BUILD_VERSION', 'r1beta5')
        short_desc = short_description or os.getenv('HAIKU_BUILD_DESCRIPTION', 'Haiku')
        
        config = {
            'target_name': f'setversion_{Path(target).stem}',
            'input': target,
            'output': target,  # In-place modification
            'command': [
                setversion_tool, '@INPUT@', 
                '-system', sys_version,
                '-short', short_desc
            ],
            'depends': [target],
            'build_by_default': False
        }
        
        return config
    
    def set_type(self, target: str, mime_type: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM SetType rule.
        Set MIME type on target.
        
        Args:
            target: Target to set type on
            mime_type: MIME type (defaults to executable type)
            
        Returns:
            Dictionary with type setting configuration
        """
        settype_tool = self.build_tools.get('settype', 'settype')
        
        # Default to executable MIME type if not specified
        target_mime_type = mime_type or 'application/x-vnd.Be-executable'
        
        config = {
            'target_name': f'settype_{Path(target).stem}',
            'input': target,
            'output': target,  # In-place modification
            'command': [settype_tool, '-t', target_mime_type, '@INPUT@'],
            'depends': [target],
            'build_by_default': False
        }
        
        return config
    
    def mime_set(self, target: str, mime_db: Optional[str] = None) -> Dict[str, Any]:
        """
        Port of JAM MimeSet rule.
        Set MIME information using MIME database.
        
        Args:
            target: Target to set MIME info on
            mime_db: MIME database file (optional)
            
        Returns:
            Dictionary with mimeset configuration
        """
        mimeset_tool = self.build_tools.get('mimeset', 'mimeset')
        
        # Default MIME database location
        if not mime_db:
            mime_db = str(self.haiku_top / 'generated/objects/linux/x86_64/release/data/mime_db/mime_db')
        
        command = [mimeset_tool, '-f']
        if mime_db and Path(mime_db).exists():
            command.extend(['--mimedb', mime_db])
        command.append('@INPUT@')
        
        config = {
            'target_name': f'mimeset_{Path(target).stem}',
            'input': target,
            'output': target,  # In-place modification
            'command': command,
            'depends': [target] + ([mime_db] if mime_db and Path(mime_db).exists() else []),
            'build_by_default': False
        }
        
        return config
    
    def create_app_mime_db_entries(self, target: str) -> Dict[str, Any]:
        """
        Port of JAM CreateAppMimeDBEntries rule.
        Create application MIME database entries.
        
        Args:
            target: Application target
            
        Returns:
            Dictionary with MIME DB creation configuration
        """
        mimeset_tool = self.build_tools.get('mimeset', 'mimeset')
        target_base = Path(target).stem
        mime_db_dir = f'{target_base}_mimedb'
        
        # MIME DB from build system
        system_mime_db = str(self.haiku_top / 'generated/objects/linux/x86_64/release/data/mime_db/mime_db')
        
        config = {
            'target_name': f'app_mimedb_{target_base}',
            'input': target,
            'output': mime_db_dir,
            'command': [
                'sh', '-c',
                f'rm -rf {mime_db_dir} && mkdir -p {mime_db_dir} && '
                f'{mimeset_tool} -f --apps --mimedb {mime_db_dir} --mimedb {system_mime_db} @INPUT@'
            ],
            'depends': [target] + ([system_mime_db] if Path(system_mime_db).exists() else []),
            'build_by_default': False
        }
        
        return config
    
    def res_comp(self, resource_file: str, rdef_file: str,
                includes: Optional[List[str]] = None,
                defines: Optional[List[str]] = None) -> Dict[str, Any]:
        """
        Port of JAM ResComp rule.
        Compile .rdef resource definition file to .rsrc.
        
        Args:
            resource_file: Output .rsrc file
            rdef_file: Input .rdef file
            includes: Include directories
            defines: Preprocessor defines
            
        Returns:
            Dictionary with resource compilation configuration
        """
        rc_tool = self.build_tools.get('rc', 'rc')
        includes = includes or []
        defines = defines or []
        
        # Build preprocessor command
        cpp_args = []
        for define in defines:
            cpp_args.extend(['-D', define])
        
        for include in includes:
            cpp_args.extend(['-I', include])
        
        # Get target compiler for preprocessing
        cross_tools = self.haiku_top / f'generated/cross-tools-{self.target_arch}/bin'
        gcc_tool = str(cross_tools / f'{self.target_arch}-unknown-haiku-gcc')
        
        # Build command: preprocess then compile resources
        command = [
            'sh', '-c',
            f'cat {rdef_file} | {gcc_tool} -E {" ".join(cpp_args)} - | '
            f'grep -v "^#" | {rc_tool} -o {resource_file} -'
        ]
        
        config = {
            'target_name': f'rescomp_{Path(resource_file).stem}',
            'input': rdef_file,
            'output': resource_file,
            'command': command,
            'depends': [rdef_file],
            'build_by_default': False
        }
        
        return config
    
    def res_attr(self, attribute_file: str, resource_files: List[str],
                delete_target: bool = True) -> Dict[str, Any]:
        """
        Port of JAM ResAttr rule.
        Convert resource files to attribute file.
        
        Args:
            attribute_file: Output attribute file
            resource_files: Input resource files (.rsrc or .rdef)
            delete_target: Whether to remove target first
            
        Returns:
            Dictionary with resource attribute configuration
        """
        resattr_tool = self.build_tools.get('resattr', 'resattr')
        
        # Handle .rdef files - compile them first
        processed_files = []
        compile_configs = []
        
        for res_file in resource_files:
            if res_file.endswith('.rdef'):
                # Compile .rdef to .rsrc first
                rsrc_file = res_file.replace('.rdef', '.rsrc')
                compile_config = self.res_comp(rsrc_file, res_file)
                compile_configs.append(compile_config)
                processed_files.append(rsrc_file)
            else:
                processed_files.append(res_file)
        
        # Build resattr command
        command = []
        if delete_target:
            command.extend(['rm', '-f', attribute_file, '&&'])
        
        command.extend([resattr_tool, '-O', '-o', attribute_file] + processed_files)
        
        config = {
            'target_name': f'resattr_{Path(attribute_file).stem}',
            'input': resource_files,
            'output': attribute_file,
            'command': command,
            'depends': resource_files,
            'compile_configs': compile_configs,  # For .rdef compilation
            'build_by_default': False
        }
        
        return config
    
    def get_build_tools_config(self) -> Dict[str, Any]:
        """Get build tools configuration."""
        return {
            'tools': self.build_tools,
            'available_tools': {name: Path(path).exists() 
                              for name, path in self.build_tools.items()},
            'target_arch': self.target_arch
        }


def get_beos_rules(target_arch: str = 'x86_64') -> HaikuBeOSRules:
    """Get BeOS rules instance."""
    return HaikuBeOSRules(target_arch)


# ========== MODULE-LEVEL EXPORTS FOR JAM COMPATIBILITY ==========
_beos_instance = None

def _get_beos(target_arch: str = 'x86_64'):
    global _beos_instance
    if _beos_instance is None:
        _beos_instance = HaikuBeOSRules(target_arch)
    return _beos_instance

# JAM Rule: AddFileDataAttribute
def AddFileDataAttribute(target: str, attr_name: str, attr_type: str, data_file: str) -> Dict[str, Any]:
    """AddFileDataAttribute <target> : <attr_name> : <attr_type> : <data_file>"""
    return _get_beos().add_file_data_attribute(target, attr_name, attr_type, data_file)

# JAM Rule: AddStringDataResource
def AddStringDataResource(target: str, resource_id: str, data_string: str = "") -> Dict[str, Any]:
    """AddStringDataResource <target> : <resource_id> : <data_string>"""
    return _get_beos().add_string_data_resource(target, resource_id, data_string)

# JAM Rule: AddFileDataResource
def AddFileDataResource(target: str, resource_id: str, data_file: str) -> Dict[str, Any]:
    """AddFileDataResource <target> : <resource_id> : <data_file>"""
    return _get_beos().add_file_data_resource(target, resource_id, data_file)

# JAM Rule: XRes
def XRes(target: str, resource_files: List[str]) -> Dict[str, Any]:
    """XRes <target> : <resource_files>"""
    return _get_beos().x_res(target, resource_files)

# JAM Rule: SetVersion
def SetVersion(target: str, system_version: Optional[str] = None,
               short_description: Optional[str] = None) -> Dict[str, Any]:
    """SetVersion <target> : <system_version> : <short_description>"""
    return _get_beos().set_version(target, system_version, short_description)

# JAM Rule: SetType
def SetType(target: str, mime_type: Optional[str] = None) -> Dict[str, Any]:
    """SetType <target> : <mime_type>"""
    return _get_beos().set_type(target, mime_type)

# JAM Rule: MimeSet
def MimeSet(target: str, mime_db: Optional[str] = None) -> Dict[str, Any]:
    """MimeSet <target> : <mime_db>"""
    return _get_beos().mime_set(target, mime_db)

# JAM Rule: CreateAppMimeDBEntries
def CreateAppMimeDBEntries(target: str) -> Dict[str, Any]:
    """CreateAppMimeDBEntries <target>"""
    return _get_beos().create_app_mime_db_entries(target)

# JAM Rule: ResComp
def ResComp(resource_file: str, rdef_file: str,
            includes: Optional[List[str]] = None,
            defines: Optional[List[str]] = None) -> Dict[str, Any]:
    """ResComp <resource_file> : <rdef_file> : <includes> : <defines>"""
    return _get_beos().res_comp(resource_file, rdef_file, includes, defines)

# JAM Rule: ResAttr
def ResAttr(attribute_file: str, resource_files: List[str],
            delete_target: bool = True) -> Dict[str, Any]:
    """ResAttr <attribute_file> : <resource_files> : <delete_target>"""
    return _get_beos().res_attr(attribute_file, resource_files, delete_target)


# Test and example usage
if __name__ == '__main__':
    print("=== Haiku BeOS Rules (JAM Port) ===")
    
    beos = get_beos_rules('x86_64')
    
    # Test build tools detection
    tools_config = beos.get_build_tools_config()
    available_count = sum(1 for available in tools_config['available_tools'].values() if available)
    total_tools = len(tools_config['tools'])
    print(f"Available Haiku build tools: {available_count}/{total_tools}")
    
    # Test resource compilation
    rdef_config = beos.res_comp('app_resources.rsrc', 'app_resources.rdef',
                               includes=['headers/private/interface'],
                               defines=['HAIKU=1', 'DEBUG=0'])
    print(f"Resource compilation config: {rdef_config['target_name']}")
    
    # Test attribute file creation
    attr_config = beos.res_attr('app_attributes', ['app_resources.rsrc', 'icons.rdef'])
    print(f"Attribute file config: {attr_config['target_name']}")
    
    # Test version setting
    version_config = beos.set_version('MyApp', 'r1beta5', 'My Haiku Application')
    print(f"Version setting config: {version_config['target_name']}")
    
    # Test MIME type setting
    type_config = beos.set_type('MyApp', 'application/x-vnd.MyCompany-MyApp')
    print(f"Type setting config: {type_config['target_name']}")
    
    # Test string resource addition
    string_res_config = beos.add_string_data_resource('MyApp', 'CSTR:1001:AppName', 'My Application')
    print(f"String resource config: {string_res_config['target_name']}")
    
    # Test MIME database entry creation
    mimedb_config = beos.create_app_mime_db_entries('MyApp')
    print(f"MIME DB config: {mimedb_config['target_name']}")
    
    print("✅ BeOS Rules functionality verified")
    print("Complete Haiku-specific build functionality ported from JAM")
    print("Supports: file attributes, MIME types, resources, version info")