# CMake Test Application for Haiku Build System

# Include Haiku modules
include(HaikuCommon)
include(HaikuResources)

# Create test application (temporarily without resources)
haiku_application(CMakeTest
    SOURCES main.cpp
    LIBRARIES be
)

# TODO: Enable resources once rc tool is built
# RESOURCES CMakeTest.rdef
# haiku_set_mime_signature(CMakeTest "application/x-vnd.Haiku-CMakeTest")
# haiku_add_app_flags(CMakeTest B_SINGLE_LAUNCH)
# haiku_add_version_info(CMakeTest 
#     1 0 0 
#     B_APPV_FINAL 1
#     "CMakeTest" 
#     "Haiku CMake Build System Test"
# )