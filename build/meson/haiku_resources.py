#!/usr/bin/env python3
"""
Haiku Resource Compiler Module for Meson
Handles compilation of .rdef files using Haiku's rc tool
"""

import os
from mesonbuild import mesonlib
from mesonbuild.interpreterbase import typed_pos_args, KwargInfo
from mesonbuild.modules import ModuleReturnValue, ExtensionModule

class HaikuResourcesModule(ExtensionModule):
    
    INFO = mesonlib.MachineChoice.HOST
    
    def __init__(self, interpreter):
        super().__init__(interpreter)
        self.methods.update({
            'compile_rdef': self.compile_rdef,
        })
    
    @typed_pos_args('haiku_resources.compile_rdef', str, str)
    def compile_rdef(self, state, args, kwargs):
        """Compile .rdef file to .rsrc using Haiku's rc compiler"""
        input_file = args[0]
        output_file = args[1]
        
        # Find the rc compiler in cross-tools
        haiku_root = os.environ.get('HAIKU_ROOT', '/home/ruslan/haiku')
        rc_compiler = os.path.join(haiku_root, 'generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-rc')
        
        # Check if rc exists, if not, try alternative paths
        if not os.path.exists(rc_compiler):
            # Try buildtools path
            rc_compiler = os.path.join(haiku_root, 'buildtools/bin/rc')
            if not os.path.exists(rc_compiler):
                # Try system rc (might not be cross-compiled though)
                rc_compiler = 'rc'
        
        # Create custom target for resource compilation
        target = self.interpreter.func_custom_target(None, [
            output_file
        ], {
            'input': [input_file],
            'output': [output_file],
            'command': [rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],
            'build_by_default': True
        })
        
        return ModuleReturnValue(target, [target])

def initialize(interpreter):
    return HaikuResourcesModule(interpreter)