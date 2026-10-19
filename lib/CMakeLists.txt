# this will download and build all dependencies listed in the /lib subdirectory.

cmake_minimum_required(VERSION 4.2.0 FATAL_ERROR)
project(Aion-slicer-deps)

include(ExternalProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "Only MSVC is supported as it is required by Rhino")
endif()

# set platform. Only x64 is supported.
set(CMAKE_VS_PLATFORM_NAME "x64")

# Define possible build configurations
# actually, we always need to build Release libs, since RhinoPlugins must be build and linked against release libs (see comment below).
# we still allow "Debug" configuration, as it is the default, but it has no effect. External libs are always build as Release.
set(CMAKE_CONFIGURATION_TYPES
    "Debug"
    "Release"
    CACHE STRING "" FORCE
)


#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
set(CMAKE_MSVC_RUNTIME_LIBRARY "")

set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir" CACHE PATH "Destination directory")
set(DEP_DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE PATH "Path for downloaded source packages.")

message(STATUS "Installing libraries to DESTDIR: ${DESTDIR}")
message(STATUS "Dowload dir for source packages: ${DEP_DOWNLOAD_DIR}")

find_package(Git REQUIRED)

# function to download and build a single library.
# this is called from the projectname.cmake files in each lib/xxx directory.
function(Aion_add_cmake_project projectname)
    cmake_parse_arguments(P_ARGS "" "INSTALL_DIR;BUILD_COMMAND;INSTALL_COMMAND" "CMAKE_ARGS" ${ARGN})

    # build release library
    ExternalProject_Add(
        dep_${projectname}
        EXCLUDE_FROM_ALL    ON
        INSTALL_DIR         ${DESTDIR}
        DOWNLOAD_DIR        ${DEP_DOWNLOAD_DIR}/${projectname}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:STRING=${DESTDIR}
            -DCMAKE_MODULE_PATH:STRING=${PROJECT_SOURCE_DIR}/../cmake/modules
            -DCMAKE_PREFIX_PATH:STRING=${DESTDIR}
            -DCMAKE_DEBUG_POSTFIX:STRING=d
            -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}
            -DCMAKE_TOOLCHAIN_FILE:STRING=${CMAKE_TOOLCHAIN_FILE}
            -DBUILD_SHARED_LIBS:BOOL=OFF
            ${P_ARGS_CMAKE_ARGS}
        ${P_ARGS_UNPARSED_ARGUMENTS}
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release
        INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release
    )

    # IMPORTANT: we don't do that. Rhino plugins are always build in release mode, but with /Zi and /DEBUG flags turned on
    # the reason seems to be, that Rhino itself is only shipped as release build.
    # Documentation is here: https://developer.rhino3d.com/guides/cpp/your-first-plugin-windows/#testing
    # So if we build external libs in debug mode, VS tries to link the static plugin library against
    # debug libraries, resulting in an
    # "_ITERATOR_DEBUG_LEVEL': value '2' doesn't match value '0'"
    # error.

    # and build debug library as well
    #ExternalProject_Get_Property(dep_${projectname} BINARY_DIR)
    #ExternalProject_Add_Step(dep_${projectname} build_debug
    #    DEPENDEES build
    #    DEPENDERS install
    #    COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
    #    WORKING_DIRECTORY "${BINARY_DIR}"
    #)

endfunction(Aion_add_cmake_project)

include(boost/boost.cmake)
include(prusaslicer/prusaslicer.cmake)

set(_dep_list
    dep_boost
    prusaslicer
    )

add_custom_target(deps ALL DEPENDS ${_dep_list})