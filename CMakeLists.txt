# We work on version 4.2.0 as Aion is also build on this version
cmake_minimum_required(VERSION 4.2.0)
project("Aion-Volume-Slicer" LANGUAGES CXX VERSION 0.1.0)

set(VENDOR "Kronos Mechatronics GmbH")
set(VENDOR_URL "kronos-mct.com")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "Only MSVC is supported as it is required by Rhino")
endif()

# set platform. Only x64 is supported.
set(CMAKE_VS_PLATFORM_NAME "x64")
# enable folders in IDE
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# enable embedded debug symbols (-Z7)
# this must be explicitly set since cmake 3.25.0. At least for me it works without the flag, but compile time goes up a lot.
# info: https://cmake.org/cmake/help/latest/policy/CMP0141.html
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

# Define possible build configurations
set(CMAKE_CONFIGURATION_TYPES
    "Debug"
    "Release"
    CACHE STRING "" FORCE
)

# Version Number
set(FILE_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH}")
set(FILE_VERSION_STR "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(PLUGIN_UUID "5B0D10BD-DC1E-4E14-99A8-5961EBDAD014")
configure_file(version.h.in version.h)

## Rhino path
if(NOT DEFINED Rhino_DIR)
    if(DEFINED ENV{Rhino_DIR})
        set(Rhino_DIR $ENV{Rhino_DIR})
        message("Rhino_DIR set via ENV to: " ${Rhino_DIR})
    else()
        if(EXISTS "C:/Program Files/Rhino 8")
            set(Rhino_DIR "C:/Program Files/Rhino 8")
            message(STATUS "Rhino_DIR automagically detected to be ${Rhino_DIR}")
        else()
            message(FATAL_ERROR "Rhino_DIR is not defined, not set as ENV variable and not present at default location \"C:\\Program Files\\Rhino 8\". Try something like -DRhino_DIR=\"C:\\Program Files\\Rhino 8\" if Rhino is installed at a different location.")
        endif()
    endif()
endif()

# enable file level parallel build in visual studio (use all cores, significantly faster)
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    message("Enable file level parallel build for visual studio compiler")
    add_compile_definitions(/MP)
endif()

# this is required. If MultiThreadedDll is not set explicitely, cmake uses default value
# MultiThreadedDebugDLL which sets _DEBUG and that is not allowed by Rhino
# which requires a specific RHINO_DEBUG_PLUGIN flag (in stdafx.h) for precompiled header generation.
#set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
# changed to global variable, see https://cmake.org/cmake/help/latest/policy/CMP0091.html#policy:CMP0091
set(CMAKE_MSVC_RUNTIME_LIBRARY "")

# we simply include all headerfiles
file(GLOB Headerfiles "src/*.h")
source_group("Headerfiles" FILES ${Headerfiles})

# use all .cpp files in src/ 
file(GLOB Sourcefiles "src/*.cpp")
source_group("Sourcefiles" FILES ${Sourcefiles})

file(GLOB Resourcefiles "res/*")
source_group("Resourcefiles" FILES ${Resourcefiles})

# Import Aion-5X-SDK from the repository root.
set(AION_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Aion-5X-SDK" CACHE PATH "Path to the Aion-5X-SDK directory")
find_path(AION_SDK_INCLUDE_DIR
    NAMES AionSDK.h
    PATHS "${AION_SDK_DIR}"
    PATH_SUFFIXES include Include
    NO_DEFAULT_PATH
    REQUIRED
)
foreach(AION_SDK_HEADER IN ITEMS AionSDK.h CamBase.h Geometry5X.h)
    if(NOT EXISTS "${AION_SDK_INCLUDE_DIR}/${AION_SDK_HEADER}")
        message(FATAL_ERROR
            "Aion-5X-SDK is missing ${AION_SDK_HEADER} in ${AION_SDK_INCLUDE_DIR}"
        )
    endif()
endforeach()
find_library(AION_SDK_LIBRARY
    NAMES Aion-5X
    PATHS "${AION_SDK_DIR}/lib"
    NO_DEFAULT_PATH
    REQUIRED
)

# use an INTERFACE library, so we need to set compile configurations only once and apply
# them to every target later
add_library(config_target INTERFACE)

# build main plugin
add_library(${PROJECT_NAME} SHARED ${Sourcefiles} ${Resourcefiles} ${Headerfiles})
target_precompile_headers(${PROJECT_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/stdafx.h>" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${AION_SDK_INCLUDE_DIR}"
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    config_target
    ${EXTERNAL_LIBRARIES}
    "${AION_SDK_LIBRARY}"
)
# Add delay load option for Aion-5X.rhp as it is not loaded at plugin load time but will be loaded by rhino.
target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:Aion-5X.rhp")

add_library(libslic3r INTERFACE)

find_package(Boost CONFIG 1.83.0 EXACT REQUIRED COMPONENTS thread filesystem log nowide)

# Extract the directories of the prusaslicer build and include files
find_path(PRUSASLICER_INCLUDE_ROOT
    NAMES
        PrusaSlicer/src/libslic3r.h
        PrusaSlicer/build/src/libslic3r/libslic3r_version.h
    PATH_SUFFIXES include
    REQUIRED
)
find_path(PRUSASLICER_DEPS_INCLUDE_DIR
    NAMES cereal/cereal.hpp
    PATH_SUFFIXES include
    REQUIRED
)
find_path(PRUSASLICER_LIBRARY_DIR
    NAMES libslic3r.lib
    PATH_SUFFIXES lib
    REQUIRED
)

# Gathering of all needed prusaslicer libraries.
set(Slicerdeps_d
    "${PRUSASLICER_LIBRARY_DIR}/libslic3r.lib"
    "${PRUSASLICER_LIBRARY_DIR}/libslic3r_cgal.lib"
    "${PRUSASLICER_LIBRARY_DIR}/clipper.lib"
    "${PRUSASLICER_LIBRARY_DIR}/libseqarrange.lib"
    "${PRUSASLICER_LIBRARY_DIR}/admesh.lib"
    "${PRUSASLICER_LIBRARY_DIR}/glu-libtess.lib"
    "${PRUSASLICER_LIBRARY_DIR}/miniz_static.lib"
    "${PRUSASLICER_LIBRARY_DIR}/localesutils.lib"
    "${PRUSASLICER_LIBRARY_DIR}/qoi.lib"
    "${PRUSASLICER_LIBRARY_DIR}/semver.lib"
    "${PRUSASLICER_LIBRARY_DIR}/prusa_fdm_mixer.lib"
    "${PRUSASLICER_LIBRARY_DIR}/qhullstatic_r.lib"
    "${PRUSASLICER_LIBRARY_DIR}/bgcode_binarize.lib"
    "${PRUSASLICER_LIBRARY_DIR}/bgcode_core.lib"
    "${PRUSASLICER_LIBRARY_DIR}/heatshrink_dynalloc.lib"
    "${PRUSASLICER_LIBRARY_DIR}/libgmp-10.lib"
    "${PRUSASLICER_LIBRARY_DIR}/libmpfr-4.lib"
    "${PRUSASLICER_LIBRARY_DIR}/libpng16_static.lib"
    "${PRUSASLICER_LIBRARY_DIR}/libz3.lib"
    "${PRUSASLICER_LIBRARY_DIR}/qhullcpp.lib"
    "${PRUSASLICER_LIBRARY_DIR}/tbb12.lib"
    "${PRUSASLICER_LIBRARY_DIR}/tbb12_debug.lib"
    "${PRUSASLICER_LIBRARY_DIR}/tbbmalloc.lib"
    "${PRUSASLICER_LIBRARY_DIR}/zlib.lib"
    "${PRUSASLICER_LIBRARY_DIR}/jpeg-static.lib"
)

# Add the include directories to libslic3r target
target_include_directories(libslic3r INTERFACE
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/src"
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/src/libslic3r"
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/build/src/libslic3r" # includes generated libslic3r_version.h
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/bundled_deps/semver"
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/bundled_deps/localesutils"
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/bundled_deps/admesh"
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/bundled_deps/tcbspan"
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/bundled_deps/libigl"
    "${PRUSASLICER_INCLUDE_ROOT}/PrusaSlicer/bundled_deps/ankerl"
    "${PRUSASLICER_DEPS_INCLUDE_DIR}"
    "${PRUSASLICER_DEPS_INCLUDE_DIR}/LibBGCode"
    "${PRUSASLICER_DEPS_INCLUDE_DIR}/eigen3"
)

# Add the prusaslicer libraries to libslic3r
target_link_libraries(libslic3r INTERFACE Boost::thread Boost::filesystem Boost::log Boost::nowide ${Slicerdeps_d})
# Add the prusaslicer libraries to the main plugin target
target_link_libraries(${PROJECT_NAME} PRIVATE libslic3r)

# Find the prusaslicer dlls for runtime linking
find_file(PRUSASLICER_LIBGMP_DLL
    NAMES libgmp-10.dll
    PATH_SUFFIXES bin
    REQUIRED
)
find_file(PRUSASLICER_LIBMPFR_DLL
    NAMES libmpfr-4.dll
    PATH_SUFFIXES bin
    REQUIRED
)

# set a bunch of rhino-specific build parameters
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/rhino.cmake")

# copy dll files from external dependencies to build dir
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
    ${PRUSASLICER_LIBGMP_DLL}
    ${PRUSASLICER_LIBMPFR_DLL}
    "${CMAKE_CURRENT_SOURCE_DIR}/res/Aion_prusa_config.ini"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>
  COMMAND_EXPAND_LISTS
)

# add the cpack cmake file to create the installer
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cpack/cpack.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cpack/cpack.cmake")
endif()
