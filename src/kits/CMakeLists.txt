# Haiku libbe.so - Complete CMake Build System
# This builds libbe.so with cross-compiler exactly matching Jam build
#
# Usage:
#   cmake -B ../../cmake_generated .   # Clean build directory in main Haiku folder
#   cmake -B ../../cmake_generated -DHAIKU_ROOT=/path/to/haiku .
#   cmake -B ../../cmake_generated -DHAIKU_ARCH=riscv64 .
#
# Configurable variables:
#   HAIKU_ROOT        - Path to Haiku source tree (auto-detected)
#   HAIKU_ARCH        - Target architecture (default: x86_64)  
#   HAIKU_TARGET      - Target triplet (default: ${HAIKU_ARCH}-unknown-haiku)
#   HAIKU_*_PACKAGE   - Package names for zlib, zstd, ICU, GCC

cmake_minimum_required(VERSION 3.16)

# Detect Haiku root directory automatically (can be overridden)
if(NOT DEFINED HAIKU_ROOT)
    get_filename_component(HAIKU_ROOT "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
endif()

# Configure target architecture (can be overridden)
if(NOT DEFINED HAIKU_ARCH)
    set(HAIKU_ARCH "x86_64")
endif()

# Configure target triplet (can be overridden)  
if(NOT DEFINED HAIKU_TARGET)
    set(HAIKU_TARGET "${HAIKU_ARCH}-unknown-haiku")
endif()

message(STATUS "Using Haiku root: ${HAIKU_ROOT}")
message(STATUS "Using Haiku architecture: ${HAIKU_ARCH}")
message(STATUS "Using Haiku target: ${HAIKU_TARGET}")

# Use cross-compiler BEFORE project() call
set(CMAKE_C_COMPILER ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/bin/${HAIKU_TARGET}-gcc CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/bin/${HAIKU_TARGET}-g++ CACHE FILEPATH "" FORCE)

# Set sysroot for cross-compilation
set(CMAKE_SYSROOT ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/sysroot)

# Configure cross-compilation paths
set(CMAKE_FIND_ROOT_PATH ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Skip CMake's automatic compiler testing for cross-compilation
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

project(libbe LANGUAGES C CXX)

message(STATUS "=== Building libbe.so with CMake Cross-Compiler ===")

# CMake modules path (simplified for testing)
# list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../build/cmake/modules)

# Build package headers (exactly matching Jam build system approach)
# This matches the exact include paths used by Haiku's Jam build
set(HAIKU_BUILD_PACKAGES_BASE "${HAIKU_ROOT}/generated/build_packages")

# Package versions (can be overridden via -D flags)
# Example: cmake -DHAIKU_ROOT=/path/to/haiku -DHAIKU_ARCH=riscv64 ...
if(NOT DEFINED HAIKU_ZLIB_PACKAGE)
    set(HAIKU_ZLIB_PACKAGE "zlib-1.3.1-3-${HAIKU_ARCH}")
endif()
if(NOT DEFINED HAIKU_ZSTD_PACKAGE)
    set(HAIKU_ZSTD_PACKAGE "zstd-1.5.6-1-${HAIKU_ARCH}")
endif()
if(NOT DEFINED HAIKU_ICU_PACKAGE)
    set(HAIKU_ICU_PACKAGE "icu74-74.1-3-${HAIKU_ARCH}")
endif()
if(NOT DEFINED HAIKU_GCC_PACKAGE)
    set(HAIKU_GCC_PACKAGE "gcc_syslibs_devel-13.3.0_2023_08_10-1-${HAIKU_ARCH}")
endif()

# Add the same include directories that Jam uses (from debug output analysis)
include_directories(SYSTEM
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_ZLIB_PACKAGE}/develop/headers
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_ZSTD_PACKAGE}/develop/headers  
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_ICU_PACKAGE}/develop/headers
)

set(ICU_FOUND TRUE)
set(ZLIB_FOUND TRUE) 
set(ZSTD_FOUND TRUE)

# Include headers exactly like Jam build (complete mapping from haiku-headers-mapping.md)
# Order matters: build packages first, then private, then public
include_directories(SYSTEM
    # Private headers (highest precedence for compression algorithm headers)
    ${HAIKU_ROOT}/headers/private/app
    ${HAIKU_ROOT}/headers/private/interface
    ${HAIKU_ROOT}/headers/private/input
    ${HAIKU_ROOT}/headers/private/kernel
    ${HAIKU_ROOT}/headers/private/libroot
    ${HAIKU_ROOT}/headers/private/locale
    ${HAIKU_ROOT}/headers/private/media
    ${HAIKU_ROOT}/headers/private/print
    ${HAIKU_ROOT}/headers/private/shared
    ${HAIKU_ROOT}/headers/private/support    # ZlibCompressionAlgorithm.h here
    ${HAIKU_ROOT}/headers/private/storage
    ${HAIKU_ROOT}/headers/private/tracker
    ${HAIKU_ROOT}/headers/private
    ${HAIKU_ROOT}/headers/private/system
    ${HAIKU_ROOT}/headers/private/system/arch/${HAIKU_ARCH}
    
    # Build headers (include redirectors)
    ${HAIKU_ROOT}/headers/build/private/support
    
    # GCC C++ headers from build packages
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++/${HAIKU_TARGET}
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++/backward
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++/ext
    
    # Compatibility headers
    ${HAIKU_ROOT}/headers/glibc
    ${HAIKU_ROOT}/headers/posix
    
    # GCC built-in headers  
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/gcc/include
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/gcc/include-fixed
    
    # Public OS headers (last to avoid conflicts)
    ${HAIKU_ROOT}/headers
    ${HAIKU_ROOT}/headers/os
    ${HAIKU_ROOT}/headers/os/add-ons
    ${HAIKU_ROOT}/headers/os/add-ons/file_system
    ${HAIKU_ROOT}/headers/os/add-ons/graphics
    ${HAIKU_ROOT}/headers/os/add-ons/input_server
    ${HAIKU_ROOT}/headers/os/add-ons/registrar
    ${HAIKU_ROOT}/headers/os/add-ons/screen_saver
    ${HAIKU_ROOT}/headers/os/add-ons/tracker
    ${HAIKU_ROOT}/headers/os/app
    ${HAIKU_ROOT}/headers/os/device
    ${HAIKU_ROOT}/headers/os/drivers
    ${HAIKU_ROOT}/headers/os/game
    ${HAIKU_ROOT}/headers/os/interface
    ${HAIKU_ROOT}/headers/os/kernel
    ${HAIKU_ROOT}/headers/os/locale
    ${HAIKU_ROOT}/headers/os/media
    ${HAIKU_ROOT}/headers/os/mail
    ${HAIKU_ROOT}/headers/os/midi
    ${HAIKU_ROOT}/headers/os/midi2
    ${HAIKU_ROOT}/headers/os/net
    ${HAIKU_ROOT}/headers/os/storage
    ${HAIKU_ROOT}/headers/os/support
    ${HAIKU_ROOT}/headers/os/translation
    
    # Library headers
    ${HAIKU_ROOT}/headers/libs/agg
    ${HAIKU_ROOT}/headers/libs/icon
    
    # Build configuration
    ${HAIKU_ROOT}/build/config_headers
    ${HAIKU_ROOT}/src/kits/tracker
)

# Find optional dependencies using CMake (for cross-compilation these may not be found)
find_package(ZLIB QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ICU QUIET icu-uc icu-io)
endif()

# App Kit sources (exactly from app/Jamfile)
set(APP_KIT_SOURCES
    app/AppDefs.cpp
    app/AppMisc.cpp
    app/AppServerLink.cpp
    app/Application.cpp
    app/Clipboard.cpp
    app/Cursor.cpp
    app/DesktopLink.cpp
    app/DirectMessageTarget.cpp
    app/Handler.cpp
    app/InitTerminateLibBe.cpp
    app/Invoker.cpp
    app/Key.cpp
    app/KeyStore.cpp
    app/LaunchRoster.cpp
    app/LinkReceiver.cpp
    app/LinkSender.cpp
    app/Looper.cpp
    app/LooperList.cpp
    app/Message.cpp
    app/MessageAdapter.cpp
    app/MessageFilter.cpp
    app/MessageQueue.cpp
    app/MessageRunner.cpp
    app/MessageUtils.cpp
    app/Messenger.cpp
    app/Notification.cpp
    app/PortLink.cpp
    app/PropertyInfo.cpp
    app/RegistrarDefs.cpp
    app/Roster.cpp
    app/RosterPrivate.cpp
    app/Server.cpp
    app/ServerLink.cpp
    app/ServerMemoryAllocator.cpp
    app/TokenSpace.cpp
    app/TypeConstants.cpp
)

# Interface Kit sources (exactly from interface/Jamfile)
set(INTERFACE_KIT_SOURCES
    interface/AboutWindow.cpp
    interface/AbstractLayout.cpp
    interface/AbstractLayoutItem.cpp
    interface/AbstractSpinner.cpp
    interface/AffineTransform.cpp
    interface/Alert.cpp
    interface/Alignment.cpp
    interface/BMCPrivate.cpp
    interface/Bitmap.cpp
    interface/Box.cpp
    interface/Button.cpp
    interface/CardLayout.cpp
    interface/CardView.cpp
    interface/ChannelControl.cpp
    interface/ChannelSlider.cpp
    interface/CheckBox.cpp
    interface/ColorControl.cpp
    interface/ColorConversion.cpp
    interface/ColorMenuItem.cpp
    interface/Control.cpp
    interface/ControlLook.cpp
    interface/DecimalSpinner.cpp
    interface/DecorInfo.cpp
    interface/Deskbar.cpp
    interface/Dragger.cpp
    interface/Font.cpp
    interface/Gradient.cpp
    interface/GradientConic.cpp
    interface/GradientDiamond.cpp
    interface/GradientLinear.cpp
    interface/GradientRadial.cpp
    interface/GradientRadialFocus.cpp
    interface/GraphicsDefs.cpp
    interface/GridLayout.cpp
    interface/GridLayoutBuilder.cpp
    interface/GridView.cpp
    interface/GroupLayout.cpp
    interface/GroupLayoutBuilder.cpp
    interface/GroupView.cpp
    interface/HaikuControlLook.cpp
    interface/Icon.cpp
    interface/Input.cpp
    interface/InterfaceDefs.cpp
    interface/Layout.cpp
    interface/LayoutContext.cpp
    interface/LayoutItem.cpp
    interface/LayoutUtils.cpp
    interface/ListItem.cpp
    interface/ListView.cpp
    interface/Menu.cpp
    interface/MenuBar.cpp
    interface/MenuField.cpp
    interface/MenuItem.cpp
    interface/MenuItemPrivate.cpp
    interface/MenuPrivate.cpp
    interface/MenuWindow.cpp
    interface/OptionControl.cpp
    interface/OptionPopUp.cpp
    interface/OutlineListView.cpp
    interface/Picture.cpp
    interface/PictureButton.cpp
    interface/PictureDataWriter.cpp
    interface/PicturePlayer.cpp
    interface/Point.cpp
    interface/Polygon.cpp
    interface/PopUpMenu.cpp
    interface/PrintJob.cpp
    interface/PrivateScreen.cpp
    interface/RadioButton.cpp
    interface/Rect.cpp
    interface/Region.cpp
    interface/RegionSupport.cpp
    interface/Screen.cpp
    interface/ScrollBar.cpp
    interface/ScrollView.cpp
    interface/SeparatorItem.cpp
    interface/SeparatorView.cpp
    interface/Shape.cpp
    interface/Shelf.cpp
    interface/Size.cpp
    interface/Slider.cpp
    interface/SpaceLayoutItem.cpp
    interface/Spinner.cpp
    interface/SplitLayout.cpp
    interface/SplitLayoutBuilder.cpp
    interface/SplitView.cpp
    interface/StatusBar.cpp
    interface/StringItem.cpp
    interface/StringView.cpp
    interface/TabView.cpp
    interface/TextControl.cpp
    interface/TextInput.cpp
    interface/TextView.cpp
    interface/ToolTip.cpp
    interface/ToolTipManager.cpp
    interface/TwoDimensionalLayout.cpp
    interface/View.cpp
    interface/ViewLayoutItem.cpp
    interface/ViewPort.cpp
    interface/Window.cpp
    interface/WindowStack.cpp
    interface/ZombieReplicantView.cpp
    interface/layouter/CollapsingLayouter.cpp
    interface/layouter/ComplexLayouter.cpp
    interface/layouter/LayoutOptimizer.cpp
    interface/layouter/Layouter.cpp
    interface/layouter/OneElementLayouter.cpp
    interface/layouter/SimpleLayouter.cpp
    interface/textview_support/InlineInput.cpp
    interface/textview_support/LineBuffer.cpp
    interface/textview_support/StyleBuffer.cpp
    interface/textview_support/TextGapBuffer.cpp
    interface/textview_support/UndoBuffer.cpp
    interface/textview_support/WidthBuffer.cpp
)

# Locale Kit sources (exactly from locale/Jamfile)
set(LOCALE_KIT_SOURCES
    locale/Catalog.cpp
    locale/CatalogData.cpp
    locale/Collator.cpp
    locale/Country.cpp
    locale/DateFormat.cpp
    locale/DateTimeFormat.cpp
    locale/DefaultCatalog.cpp
    locale/DurationFormat.cpp
    locale/EditableCatalog.cpp
    locale/Format.cpp
    locale/FormattingConventions.cpp
    locale/HashMapCatalog.cpp
    locale/InitLocaleKit.cpp
    locale/Language.cpp
    locale/Locale.cpp
    locale/LocaleRoster.cpp
    locale/LocaleRosterData.cpp
    locale/MutableLocaleRoster.cpp
    locale/NumberFormat.cpp
    locale/RelativeDateTimeFormat.cpp
    locale/StringFormat.cpp
    locale/TextEncoding.cpp
    locale/TimeFormat.cpp
    locale/TimeUnitFormat.cpp
    locale/TimeZone.cpp
    locale/UnicodeChar.cpp
)

# Storage Kit sources (exactly from storage/Jamfile)
set(STORAGE_KIT_SOURCES
    storage/AppFileInfo.cpp
    storage/CopyEngine.cpp
    storage/Directory.cpp
    storage/DriverSettings.cpp
    storage/Entry.cpp
    storage/EntryList.cpp
    storage/EntryOperationEngineBase.cpp
    storage/FdIO.cpp
    storage/File.cpp
    storage/FileDescriptorIO.cpp
    storage/FileIO.cpp
    storage/FindDirectory.cpp
    storage/MergedDirectory.cpp
    storage/Mime.cpp
    storage/MimeType.cpp
    storage/Node.cpp
    storage/NodeInfo.cpp
    storage/NodeMonitor.cpp
    storage/OffsetFile.cpp
    storage/Path.cpp
    storage/PathFinder.cpp
    storage/PathMonitor.cpp
    storage/Query.cpp
    storage/QueryPredicate.cpp
    storage/RemoveEngine.cpp
    storage/ResourceFile.cpp
    storage/ResourceItem.cpp
    storage/ResourceStrings.cpp
    storage/Resources.cpp
    storage/ResourcesContainer.cpp
    storage/Statable.cpp
    storage/SymLink.cpp
    storage/Volume.cpp
    storage/VolumeRoster.cpp
    storage/disk_device/DiskDevice.cpp
    storage/disk_device/DiskDeviceJob.cpp
    storage/disk_device/DiskDeviceJobGenerator.cpp
    storage/disk_device/DiskDeviceJobQueue.cpp
    storage/disk_device/DiskDeviceList.cpp
    storage/disk_device/DiskDevicePrivate.cpp
    storage/disk_device/DiskDeviceRoster.cpp
    storage/disk_device/DiskDeviceTypes.cpp
    storage/disk_device/DiskDeviceVisitor.cpp
    storage/disk_device/DiskSystem.cpp
    storage/disk_device/DiskSystemAddOn.cpp
    storage/disk_device/DiskSystemAddOnManager.cpp
    storage/disk_device/MutablePartition.cpp
    storage/disk_device/Partition.cpp
    storage/disk_device/PartitionDelegate.cpp
    storage/disk_device/PartitionParameterEditor.cpp
    storage/disk_device/PartitionReference.cpp
    storage/disk_device/PartitioningInfo.cpp
    storage/disk_device/jobs/CreateChildJob.cpp
    storage/disk_device/jobs/DefragmentJob.cpp
    storage/disk_device/jobs/DeleteChildJob.cpp
    storage/disk_device/jobs/InitializeJob.cpp
    storage/disk_device/jobs/MoveJob.cpp
    storage/disk_device/jobs/RepairJob.cpp
    storage/disk_device/jobs/ResizeJob.cpp
    storage/disk_device/jobs/SetStringJob.cpp
    storage/disk_device/jobs/UninitializeJob.cpp
    storage/mime/AppMetaMimeCreator.cpp
    storage/mime/AssociatedTypes.cpp
    storage/mime/Database.cpp
    storage/mime/DatabaseDirectory.cpp
    storage/mime/DatabaseLocation.cpp
    storage/mime/InstalledTypes.cpp
    storage/mime/MimeEntryProcessor.cpp
    storage/mime/MimeInfoUpdater.cpp
    storage/mime/MimeSniffer.cpp
    storage/mime/MimeSnifferAddon.cpp
    storage/mime/MimeSnifferAddonManager.cpp
    storage/mime/SnifferRules.cpp
    storage/mime/Supertype.cpp
    storage/mime/SupportingApps.cpp
    storage/mime/TextSnifferAddon.cpp
    storage/mime/database_support.cpp
    storage/sniffer/CharStream.cpp
    storage/sniffer/DisjList.cpp
    storage/sniffer/Err.cpp
    storage/sniffer/Parser.cpp
    storage/sniffer/Pattern.cpp
    storage/sniffer/PatternList.cpp
    storage/sniffer/RPattern.cpp
    storage/sniffer/RPatternList.cpp
    storage/sniffer/Range.cpp
    storage/sniffer/Rule.cpp
    storage/storage_support.cpp
)

# Support Kit sources (exactly from support/Jamfile)
set(SUPPORT_KIT_SOURCES
    support/Architecture.cpp
    support/Archivable.cpp
    support/ArchivingManagers.cpp
    support/Base64.cpp
    support/Beep.cpp
    support/BlockCache.cpp
    support/BufferIO.cpp
    support/BufferedDataIO.cpp
    support/ByteOrder.cpp
    support/CompressionAlgorithm.cpp
    support/DataIO.cpp
    support/DataPositionIOWrapper.cpp
    support/DateTime.cpp
    support/Flattenable.cpp
    support/Job.cpp
    support/JobQueue.cpp
    support/List.cpp
    support/Locker.cpp
    support/PointerList.cpp
    support/Referenceable.cpp
    support/StopWatch.cpp
    support/String.cpp
    support/StringList.cpp
    support/Url.cpp
    support/Uuid.cpp
    support/ZlibCompressionAlgorithm.cpp
    support/ZstdCompressionAlgorithm.cpp
)

# Create app_kit.o as object library (matches Jam MergeObject)
add_library(app_kit_obj OBJECT ${APP_KIT_SOURCES})

# Add compiler flags exactly matching Jam build
target_compile_definitions(app_kit_obj PRIVATE ZSTD_ENABLED)
target_compile_options(app_kit_obj PRIVATE 
    -O2
    -fno-strict-aliasing
)

# Link dependencies if found
if(ZLIB_FOUND)
    target_link_libraries(app_kit_obj PRIVATE ZLIB::ZLIB)
endif()

if(ICU_FOUND)
    target_include_directories(app_kit_obj PRIVATE ${ICU_INCLUDE_DIRS})
    target_compile_options(app_kit_obj PRIVATE ${ICU_CFLAGS_OTHER})
endif()

# Set cross-compiler properties
set_target_properties(app_kit_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME app_kit
)

# Create app_kit.o exactly like Jam build (MergeObject)
add_custom_command(
    OUTPUT ${HAIKU_KITS_DIR}/app_kit.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:app_kit_obj> -o ${HAIKU_KITS_DIR}/app_kit.o
    DEPENDS app_kit_obj
    COMMENT "Merging objects into app_kit.o (matching Jam MergeObject)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(app_kit ALL DEPENDS ${HAIKU_KITS_DIR}/app_kit.o)

# Create interface_kit.o as object library (matches Jam MergeObject)
add_library(interface_kit_obj OBJECT ${INTERFACE_KIT_SOURCES})

# Add interface_kit specific includes
target_include_directories(interface_kit_obj PRIVATE
    ${CMAKE_SOURCE_DIR}/interface/textview_support
    ${CMAKE_SOURCE_DIR}/interface/layouter
)

# Add compiler flags exactly matching Jam build  
target_compile_definitions(interface_kit_obj PRIVATE ZSTD_ENABLED)
target_compile_options(interface_kit_obj PRIVATE 
    -O2
    -fno-strict-aliasing
)

# Link dependencies if found
if(ZLIB_FOUND)
    target_link_libraries(interface_kit_obj PRIVATE ZLIB::ZLIB)
endif()

if(ICU_FOUND)
    target_include_directories(interface_kit_obj PRIVATE ${ICU_INCLUDE_DIRS})
    target_compile_options(interface_kit_obj PRIVATE ${ICU_CFLAGS_OTHER})
endif()

# Set cross-compiler properties
set_target_properties(interface_kit_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME interface_kit
)

# Create interface_kit.o exactly like Jam build (MergeObject)
add_custom_command(
    OUTPUT ${HAIKU_KITS_DIR}/interface_kit.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:interface_kit_obj> -o ${HAIKU_KITS_DIR}/interface_kit.o
    DEPENDS interface_kit_obj
    COMMENT "Merging objects into interface_kit.o (matching Jam MergeObject)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(interface_kit ALL DEPENDS ${HAIKU_KITS_DIR}/interface_kit.o)

# Create locale_kit.o as object library (matches Jam MergeObject)
add_library(locale_kit_obj OBJECT ${LOCALE_KIT_SOURCES})

# Add compiler flags matching Jam build (with symbol visibility control)
target_compile_definitions(locale_kit_obj PRIVATE ZSTD_ENABLED)
target_compile_options(locale_kit_obj PRIVATE 
    -O2
    -fno-strict-aliasing
)

# Headers are now available globally from build packages, no target-specific linking needed

# Set cross-compiler properties
set_target_properties(locale_kit_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME locale_kit
)

# Create locale_kit.o exactly like Jam build (MergeObject)
add_custom_command(
    OUTPUT ${HAIKU_KITS_DIR}/locale_kit.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:locale_kit_obj> -o ${HAIKU_KITS_DIR}/locale_kit.o
    DEPENDS locale_kit_obj
    COMMENT "Merging objects into locale_kit.o (matching Jam MergeObject)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(locale_kit ALL DEPENDS ${HAIKU_KITS_DIR}/locale_kit.o)

# Create storage_kit.o as object library (matches Jam MergeObject)
add_library(storage_kit_obj OBJECT ${STORAGE_KIT_SOURCES})

# Add storage_kit specific includes
target_include_directories(storage_kit_obj PRIVATE
    ${CMAKE_SOURCE_DIR}/storage/disk_device
    ${CMAKE_SOURCE_DIR}/storage/disk_device/jobs
    ${CMAKE_SOURCE_DIR}/storage/mime
    ${CMAKE_SOURCE_DIR}/storage/sniffer
)

# Add compiler flags to prevent local template instantiation like Jam
target_compile_definitions(storage_kit_obj PRIVATE ZSTD_ENABLED)
target_compile_options(storage_kit_obj PRIVATE 
    -O2
    -fno-strict-aliasing
    -fno-implicit-templates
    -fno-implicit-inline-templates
)

# Link dependencies if found
if(ZLIB_FOUND)
    target_link_libraries(storage_kit_obj PRIVATE ZLIB::ZLIB)
endif()

if(ICU_FOUND)
    target_include_directories(storage_kit_obj PRIVATE ${ICU_INCLUDE_DIRS})
    target_compile_options(storage_kit_obj PRIVATE ${ICU_CFLAGS_OTHER})
endif()

# Set cross-compiler properties
set_target_properties(storage_kit_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME storage_kit
)

# Create storage_kit.o exactly like Jam build (MergeObject)
add_custom_command(
    OUTPUT ${HAIKU_KITS_DIR}/storage_kit.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:storage_kit_obj> -o ${HAIKU_KITS_DIR}/storage_kit.o
    DEPENDS storage_kit_obj
    COMMENT "Merging objects into storage_kit.o (matching Jam MergeObject)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(storage_kit ALL DEPENDS ${HAIKU_KITS_DIR}/storage_kit.o)

# Create support_kit.o as object library (matches Jam MergeObject)
add_library(support_kit_obj OBJECT ${SUPPORT_KIT_SOURCES})

# Add compiler flags exactly matching Jam build
target_compile_definitions(support_kit_obj PRIVATE ZSTD_ENABLED)
target_compile_options(support_kit_obj PRIVATE 
    -O2
    -fno-strict-aliasing
)

# Headers are now available globally from build packages, no target-specific linking needed

# Set cross-compiler properties
set_target_properties(support_kit_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME support_kit
)

# Create support_kit.o exactly like Jam build (MergeObject)
add_custom_command(
    OUTPUT ${HAIKU_KITS_DIR}/support_kit.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:support_kit_obj> -o ${HAIKU_KITS_DIR}/support_kit.o
    DEPENDS support_kit_obj
    COMMENT "Merging objects into support_kit.o (matching Jam MergeObject)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(support_kit ALL DEPENDS ${HAIKU_KITS_DIR}/support_kit.o)

# Create libshared.a static library (from shared/Jamfile)
set(SHARED_KIT_SOURCES
    shared/AboutMenuItem.cpp
    shared/ArgumentVector.cpp
    shared/AttributeUtilities.cpp
    shared/BarberPole.cpp
    shared/BitmapButton.cpp
    shared/CalendarView.cpp
    shared/ColorItem.cpp
    shared/ColorListView.cpp
    shared/ColorPreview.cpp
    shared/ColorQuantizer.cpp
    shared/CommandPipe.cpp
    shared/DateTimeEdit.cpp
    shared/DragTrackingFilter.cpp
    shared/DriverSettingsMessageAdapter.cpp
    shared/HSL.cpp
    shared/HashString.cpp
    shared/IconButton.cpp
    shared/IconView.cpp
    shared/JsonWriter.cpp
    shared/JsonEventListener.cpp
    shared/JsonMessageWriter.cpp
    shared/JsonTextWriter.cpp
    shared/JsonEvent.cpp
    shared/Json.cpp
    shared/Keymap.cpp
    shared/LongAndDragTrackingFilter.cpp
    shared/md5.cpp
    shared/MemoryRingIO.cpp
    shared/MessageBuilder.cpp
    shared/NaturalCompare.cpp
    shared/PromptWindow.cpp
    shared/QueryFile.cpp
    shared/RegExp.cpp
    shared/RWLocker.cpp
    shared/RWLockManager.cpp
    shared/SettingsHandler.cpp
    shared/SettingsMessage.cpp
    shared/ShakeTrackingFilter.cpp
    shared/StatusView.cpp
    shared/StringForRate.cpp
    shared/StringForSize.cpp
    shared/StripeView.cpp
    shared/TextTable.cpp
    shared/Thread.cpp
    shared/ToolBar.cpp
    shared/Variant.cpp
    shared/WeakReferenceable.cpp
)

add_library(shared_kit STATIC ${SHARED_KIT_SOURCES})

# Add compiler flags and includes matching shared/Jamfile
target_compile_options(shared_kit PRIVATE 
    -O2
    -fno-strict-aliasing
)

# Add special include directories needed by shared kit (from shared/Jamfile)
target_include_directories(shared_kit PRIVATE
    ${CMAKE_SOURCE_DIR}  # For tracker/MimeTypes.h and other kits headers
    ${HAIKU_GENERATED_DIRS}/servers/input  # For SystemKeymap.h
)

# Set cross-compiler properties
set_target_properties(shared_kit PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "shared"
)

# Create the complete libbe.so shared library by linking all kit objects
# This matches the original Jam SharedLibrary rule that creates libbe.so
add_library(libbe SHARED 
    $<TARGET_OBJECTS:app_kit_obj>
    $<TARGET_OBJECTS:interface_kit_obj>  
    $<TARGET_OBJECTS:locale_kit_obj>
    $<TARGET_OBJECTS:storage_kit_obj>
    $<TARGET_OBJECTS:support_kit_obj>
)

# Set the exact library name to match Haiku's libbe.so
set_target_properties(libbe PROPERTIES
    OUTPUT_NAME "be"
    SOVERSION 1
    VERSION 1.0.0
    POSITION_INDEPENDENT_CODE ON
    LINKER_LANGUAGE CXX
    CXX_STANDARD_LIBRARIES ""  # Don't link standard C++ library automatically
)

# Add explicit path to libstdc++ from build packages (runtime package)  
# Extract runtime package name from devel package name
string(REPLACE "_devel" "" HAIKU_GCC_RUNTIME_PACKAGE "${HAIKU_GCC_PACKAGE}")

# Find the actual libstdc++ file (avoid hardcoding version)
file(GLOB LIBSTDCXX_FILES "${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_RUNTIME_PACKAGE}/lib/libstdc++.so.*")
list(FILTER LIBSTDCXX_FILES EXCLUDE REGEX "libstdc\\+\\+\\.so$")  # Exclude the main symlink
if(LIBSTDCXX_FILES)
    list(GET LIBSTDCXX_FILES 0 LIBSTDCXX_PATH)
    message(STATUS "Found libstdc++: ${LIBSTDCXX_PATH}")
    # Use linker options to avoid automatic -lstdc++ and add version script
    target_link_options(libbe PRIVATE 
        "-nostdlib++"
        "-Wl,--version-script=${CMAKE_SOURCE_DIR}/libbe_versions"
    )
    target_link_libraries(libbe PRIVATE 
        ${LIBSTDCXX_PATH}
        shared_kit
    )
else()
    message(WARNING "Could not find libstdc++ in ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_RUNTIME_PACKAGE}/lib/")
    # Fallback to system library  
    target_link_options(libbe PRIVATE "-Wl,--version-script=${CMAKE_SOURCE_DIR}/libbe_versions")
    target_link_libraries(libbe PRIVATE 
        stdc++
        shared_kit
    )
endif()

# Make sure all kit objects are built before creating libbe.so
add_dependencies(libbe app_kit interface_kit locale_kit storage_kit support_kit shared_kit)

message(STATUS "libbe.so CMake cross-compilation configured:")
message(STATUS "  All kits: support_kit app_kit interface_kit locale_kit storage_kit")
message(STATUS "  Cross-compiler: x86_64-unknown-haiku-gcc")
message(STATUS "  Output: Complete libbe.so shared library")
message(STATUS "  Dependencies: libroot, zlib, zstd, ICU")