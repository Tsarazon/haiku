project('haiku-stdlib', 'c', 'cpp')

# Find the GCC syslibs packages dynamically  
build_packages_base = '../generated/build_packages'

# Find gcc_syslibs packages
gcc_syslibs_dirs = run_command('find', build_packages_base, '-maxdepth', '1', '-name', 'gcc_syslibs*', '-type', 'd', check: false)

if gcc_syslibs_dirs.returncode() == 0
  syslibs_paths = gcc_syslibs_dirs.stdout().strip().split('\n')
  
  # Find the runtime and devel packages
  runtime_pkg = ''
  devel_pkg = ''
  
  foreach path : syslibs_paths
    if path.contains('_devel')
      devel_pkg = path
    else
      runtime_pkg = path
    endif
  endforeach
  
  link_args = []
  if runtime_pkg != ''
    link_args += ['-L' + runtime_pkg + '/lib']
  endif
  if devel_pkg != ''
    link_args += ['-L' + devel_pkg + '/develop/lib']
  endif
  
  # Add Haiku runtime objects path
  link_args += ['-L../generated/objects/haiku/x86_64/release/system/libroot']
  
  # Declare the Haiku C standard library dependency
  haiku_c_stdlib = declare_dependency(
    link_args: link_args
  )
else
  # Fallback if packages not found
  haiku_c_stdlib = declare_dependency()
endif

# Declare Haiku C++ stdlib dependency (same as C for Haiku)
if gcc_syslibs_dirs.returncode() == 0
  haiku_cpp_stdlib = declare_dependency(
    link_args: link_args
  )
else
  haiku_cpp_stdlib = declare_dependency()
endif

# Note: overrides are handled in main meson.build to control timing