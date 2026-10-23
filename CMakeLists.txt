cmake_minimum_required(VERSION 3.20)
# The version string is upstream's (xmake.lua set_version), so the Mac build reports what upstream's own build reports. It stays a literal here
# because the everyday launcher reads it from this line; the check below keeps the two equal.
project(TweakXL VERSION 1.11.4 LANGUAGES CXX)
file(READ "${CMAKE_SOURCE_DIR}/xmake.lua" _upstream_xmake)
string(REGEX MATCH "set_version\\(\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _upstream_version "${_upstream_xmake}")
if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "TweakXL ${PROJECT_VERSION} here, but xmake.lua says ${CMAKE_MATCH_1}: update the project() line")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(APPLE)
    message(STATUS "Building TweakXL for macOS")
    add_compile_definitions(RED4EXT_PLATFORM_MACOS)
    add_compile_definitions(RED4EXT_USE_FRIDA_GADGET)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(OUTPUT_NAME "TweakXL")
    
    # macOS linker flags for dylib
    # Note: -flat_namespace removed because it causes TweakXL to use RED4ext's
    # SDK symbols instead of its own modified versions
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -undefined dynamic_lookup")
else()
    message(STATUS "Building TweakXL for Windows")
    add_compile_definitions(WINVER=0x0601)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    add_compile_definitions(NOMINMAX)
    set(OUTPUT_NAME "TweakXL")
endif()

# Generate Project.hpp from template
set(NAME "TweakXL")
set(AUTHOR "psiberx")
set(VERSION "${PROJECT_VERSION}")
set(MODE "release")
set(DEBUG "")
configure_file(
    "${CMAKE_SOURCE_DIR}/config/Project.hpp.in"
    "${CMAKE_SOURCE_DIR}/src/App/Project.hpp"
)

# Vendor dependencies (header-only)
add_library(nameof INTERFACE)
target_include_directories(nameof INTERFACE "${CMAKE_SOURCE_DIR}/vendor/nameof/include")

add_library(semver INTERFACE)
target_include_directories(semver INTERFACE "${CMAKE_SOURCE_DIR}/vendor/semver/include")

add_library(pegtl INTERFACE)
target_include_directories(pegtl INTERFACE "${CMAKE_SOURCE_DIR}/vendor/pegtl/include")

# R23: the SDK rows TweakXL needs on top of the sibling SDK's Mac table (macos/MacSdkRows.inc) are injected into a generated copy of
# RED4ext/Relocation-inl.hpp, which comes first on the include path. The sibling SDK checkout is not touched.
if(APPLE)
    set(TWEAKXL_SDK_INCLUDE "${CMAKE_SOURCE_DIR}/../RED4ext.SDK-macos/include")
    if(DEFINED RED4EXT_SDK_DIR)
        set(TWEAKXL_SDK_INCLUDE "${RED4EXT_SDK_DIR}")
    endif()
    set(SDK_RELOC "${TWEAKXL_SDK_INCLUDE}/RED4ext/Relocation-inl.hpp")
    set(SDK_ROWS_INC "${CMAKE_SOURCE_DIR}/macos/MacSdkRows.inc")
    set(SDK_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/sdk-overlay")
    # Reproducible builds: no checkout path ends up in the dylib (__FILE__ in asserts and logs), so the same sources build to the same bytes
    # from any directory.
    get_filename_component(_sdk_root "${TWEAKXL_SDK_INCLUDE}/.." ABSOLUTE)
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${_sdk_root}=RED4ext.SDK-macos"
                        "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
    if(EXISTS "${SDK_RELOC}")
        file(READ "${SDK_RELOC}" SDK_RELOC_TEXT)
        file(READ "${SDK_ROWS_INC}" SDK_ROWS_TEXT)
        set(SDK_ROWS_MARKER "        { 4201325776, 0x109BAA8 },  // CGlobalFunction_ctor\n")
        string(FIND "${SDK_RELOC_TEXT}" "${SDK_ROWS_MARKER}" SDK_ROWS_POS)
        if(SDK_ROWS_POS EQUAL -1)
            message(FATAL_ERROR "R23: the anchor row (CGlobalFunction_ctor) is not in ${SDK_RELOC}. Update the anchor in CMakeLists.txt.")
        endif()
        string(REPLACE "${SDK_ROWS_MARKER}" "${SDK_ROWS_MARKER}${SDK_ROWS_TEXT}" SDK_RELOC_TEXT "${SDK_RELOC_TEXT}")
        file(WRITE "${SDK_OVERLAY}/RED4ext/Relocation-inl.hpp" "${SDK_RELOC_TEXT}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SDK_RELOC}" "${SDK_ROWS_INC}")
        message(STATUS "R23: SDK Mac table overlay written to ${SDK_OVERLAY}")
    else()
        message(FATAL_ERROR "R23: ${SDK_RELOC} not found; check out RED4ext.SDK-macos (mac-full) next to this repository or pass -DRED4EXT_SDK_DIR.")
    endif()
endif()

# RED4ext.SDK (header-only for macOS)
add_library(RED4ext.SDK INTERFACE)

# Prefer a workspace checkout of RED4ext.SDK (for the macOS port) when available,
# but keep the vendored SDK as a fallback for standalone builds.
set(RED4EXT_SDK_VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor/RED4ext.SDK/include")
# R23: the sibling checkout RED4ext.SDK-macos (branch mac-full) carries the Mac layouts, the lock protocol, the job queue and the vtable fixes that
# Codeware and ArchiveXL build against. -DRED4EXT_SDK_DIR=<include dir> picks another one.
if(DEFINED RED4EXT_SDK_DIR)
    set(RED4EXT_SDK_WORKSPACE_DIR "${RED4EXT_SDK_DIR}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../RED4ext.SDK-macos/include/RED4ext/RED4ext.hpp")
    set(RED4EXT_SDK_WORKSPACE_DIR "${CMAKE_SOURCE_DIR}/../RED4ext.SDK-macos/include")
else()
    set(RED4EXT_SDK_WORKSPACE_DIR "${CMAKE_SOURCE_DIR}/../RED4ext.SDK/include")
endif()

if(EXISTS "${RED4EXT_SDK_WORKSPACE_DIR}/RED4ext/RED4ext.hpp")
    message(STATUS "Using workspace RED4ext.SDK headers: ${RED4EXT_SDK_WORKSPACE_DIR}")
    target_include_directories(RED4ext.SDK INTERFACE "${RED4EXT_SDK_WORKSPACE_DIR}")
else()
    message(STATUS "Using vendored RED4ext.SDK headers: ${RED4EXT_SDK_VENDOR_DIR}")
endif()

target_include_directories(RED4ext.SDK INTERFACE "${RED4EXT_SDK_VENDOR_DIR}")
if(APPLE)
    target_include_directories(RED4ext.SDK BEFORE INTERFACE "${SDK_OVERLAY}")
endif()

# Platform-specific dependencies
if(NOT APPLE)
    # WIL (Windows only)
    add_library(wil INTERFACE)
    target_include_directories(wil INTERFACE "${CMAKE_SOURCE_DIR}/vendor/wil/include")
endif()

# Find external packages
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)

# TiltedCore and hopscotch - for macOS, we need to provide these
if(APPLE)
    # For macOS, we'll use standard allocator instead of TiltedPhoques
    # This is handled via platform abstraction in Stl.hpp
endif()

# Source files
set(TWEAKXL_SOURCES
    src/main.cpp
    src/pch.cpp
    src/rtti.cpp
    src/App/Application.cpp
    src/App/MacRegisterNatives.cpp
    src/App/Facade.cpp
    src/App/Stats/StatService.cpp
    src/App/Tweaks/Batch/TweakChangelog.cpp
    src/App/Tweaks/Batch/TweakChangeset.cpp
    src/App/Tweaks/Declarative/TweakImporter.cpp
    src/App/Tweaks/Declarative/TweakReader.cpp
    src/App/Tweaks/Declarative/Red/RedReader.cpp
    src/App/Tweaks/Declarative/Red/RedReader.Types.cpp
    src/App/Tweaks/Declarative/Red/RedReader.Values.cpp
    src/App/Tweaks/Declarative/Yaml/YamlReader.cpp
    src/App/Tweaks/Declarative/Yaml/YamlReader.Legacy.cpp
    src/App/Tweaks/Declarative/Yaml/YamlReader.Template.cpp
    src/App/Tweaks/Declarative/Yaml/YamlReader.Values.cpp
    src/App/Tweaks/Executable/TweakExecutor.cpp
    src/App/Tweaks/Executable/Scriptable/ScriptBatch.cpp
    src/App/Tweaks/Executable/Scriptable/ScriptInterface.cpp
    src/App/Tweaks/Executable/Scriptable/ScriptManager.cpp
    src/App/Tweaks/Metadata/MetadataExporter.cpp
    src/App/Tweaks/Metadata/MetadataImporter.cpp
    src/App/Tweaks/TweakService.cpp
    src/Red/TweakDB/Buffer.cpp
    src/Red/TweakDB/Manager.cpp
    src/Red/TweakDB/Reflection.cpp
    src/Red/TweakDB/Source/Parser.cpp
    lib/Core/Facades/Runtime.cpp
    lib/Core/Foundation/Application.cpp
    lib/Core/Foundation/RuntimeProvider.cpp
    lib/Core/Hooking/HookingAgent.cpp
    lib/Core/Hooking/HookingDriver.cpp
    lib/Core/Logging/LoggingAgent.cpp
    lib/Core/Logging/LoggingDriver.cpp
    lib/Core/Memory/AddressResolver.cpp
    lib/Core/Runtime/HostImage.cpp
    lib/Core/Runtime/ModuleImage.cpp
    lib/Core/Runtime/OwnerMutex.cpp
    lib/Support/RED4ext/RED4extProvider.cpp
    lib/Support/Spdlog/SpdlogProvider.cpp
    lib/Support/RedLib/RedLibProvider.cpp
)

# Platform-specific sources
if(APPLE)
    list(APPEND TWEAKXL_SOURCES
        lib/Support/macOS/TweakXLAddressResolver.cpp
        macos/MacLayoutGuard.cpp
    )
else()
    list(APPEND TWEAKXL_SOURCES
        lib/Support/MinHook/MinHookProvider.cpp
    )
endif()

# Create shared library
add_library(TweakXL SHARED ${TWEAKXL_SOURCES})

# Set output name
set_target_properties(TweakXL PROPERTIES OUTPUT_NAME ${OUTPUT_NAME})

if(APPLE)
    set_target_properties(TweakXL PROPERTIES
        SUFFIX ".dylib"
        PREFIX ""
    )
else()
    set_target_properties(TweakXL PROPERTIES
        SUFFIX ".dll"
        PREFIX ""
    )
endif()

# Include directories
target_include_directories(TweakXL PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib
)
if(APPLE)
    target_include_directories(TweakXL PRIVATE ${CMAKE_SOURCE_DIR}/macos)
endif()

# Link libraries
target_link_libraries(TweakXL PRIVATE
    RED4ext.SDK
    nameof
    semver
    pegtl
    spdlog::spdlog
    yaml-cpp
)

if(NOT APPLE)
    target_link_libraries(TweakXL PRIVATE
        wil
        Version
        User32
    )
endif()

# Precompiled header
target_precompile_headers(TweakXL PRIVATE src/pch.hpp)

# Compiler options
if(APPLE)
    target_compile_options(TweakXL PRIVATE
        -fno-rtti
        -fexceptions
        -Wno-deprecated-declarations
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )
else()
    target_compile_options(TweakXL PRIVATE
        /MP /GR- /EHsc
    )
endif()

if(APPLE)
    # R23: the plugin loads the copies of spdlog, fmt and yaml-cpp that sit next to it (@loader_path), and the build fails when one of its own
    # symbols is declared but not compiled in (-undefined dynamic_lookup would hide it until the first call).
    add_custom_command(TARGET TweakXL POST_BUILD
        COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/macos/fix-install-names.sh" "$<TARGET_FILE:TweakXL>"
        VERBATIM)
    add_custom_command(TARGET TweakXL POST_BUILD
        COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/macos/check-undefined.sh" "$<TARGET_FILE:TweakXL>"
        VERBATIM)
endif()

# Installation
install(TARGETS TweakXL
    LIBRARY DESTINATION red4ext/plugins
    RUNTIME DESTINATION red4ext/plugins
)

message(STATUS "TweakXL ${PROJECT_VERSION} configured")
