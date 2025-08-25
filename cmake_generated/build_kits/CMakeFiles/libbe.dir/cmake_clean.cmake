file(REMOVE_RECURSE
  ".1"
  "libbe.pdb"
  "libbe.so"
  "libbe.so.1"
  "libbe.so.1.0.0"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/libbe.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
