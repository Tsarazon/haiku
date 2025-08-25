#!/usr/bin/env python3
import os
import sys
from BuildFeatures import build_feature_attribute, get_available_features
from ResourceHandler import HaikuResourceHandler

def get_build_feature_dependencies(architecture='x86_64'):
    """Get dynamic dependencies based on available build packages"""
    deps = {}
    
    for feature in ['zlib', 'zstd', 'icu', 'freetype', 'libpng']:
        link_args = build_feature_attribute(feature, 'link_args', architecture)
        headers = build_feature_attribute(feature, 'headers', architecture)
        
        if link_args and headers:
            deps[feature] = {
                'link_args': link_args,
                'headers': headers,
                'available': True
            }
        else:
            deps[feature] = {'available': False}
    
    return deps

def get_haiku_resource_handler():
    """Get a configured HaikuResourceHandler instance"""
    return HaikuResourceHandler()

def get_rc_compiler_path():
    """Get the path to the rc compiler using ResourceHandler"""
    handler = HaikuResourceHandler()
    if handler.validate_rc_compiler():
        return handler.get_rc_compiler()
    return None

def get_standard_libbe_resources():
    """Get standard libbe.so resource configurations"""
    handler = HaikuResourceHandler()
    return handler.get_standard_haiku_resources()

def generate_resource_meson_code(resource_files):
    """Generate Meson code for resource compilation"""
    handler = HaikuResourceHandler()
    if isinstance(resource_files, str):
        # Single file
        resource_files = [resource_files]
    
    # Convert to configurations if needed
    configs = []
    for rdef_file in resource_files:
        if isinstance(rdef_file, str):
            from pathlib import Path
            rdef_path = Path(rdef_file)
            target_name = rdef_path.stem + '_rsrc'
            configs.append(handler.create_meson_resource_target(target_name, rdef_file))
        else:
            configs.append(rdef_file)
    
    return handler.get_meson_code_for_resources(configs)

def main():
    haiku_root = os.environ.get('HAIKU_ROOT', '/home/ruslan/haiku')
    if not os.path.exists(os.path.join(haiku_root, 'src')):
        print(f"Error: Invalid Haiku root: {haiku_root}", file=sys.stderr)
        sys.exit(1)
    
    # Validate ResourceHandler
    handler = HaikuResourceHandler()
    if handler.validate_rc_compiler():
        print(f"✅ RC compiler found: {handler.get_rc_compiler()}")
    else:
        print(f"⚠️  RC compiler not found: {handler.get_rc_compiler()}")
    
    print("Haiku configuration initialized successfully")
    return 0

if __name__ == '__main__':
    sys.exit(main())