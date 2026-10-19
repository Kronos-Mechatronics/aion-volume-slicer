set(PRUSASLICER_VERSION version_2.9.6)
set(PRUSASLICER_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer-src")
set(PRUSASLICER_DEPS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer-deps-build")
set(PRUSASLICER_DEPS_PREFIX "${PRUSASLICER_DEPS_BUILD_DIR}/destdir/usr/local")
set(PRUSASLICER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/PrusaSlicer-build")
set(PRUSASLICER_PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/prusaslicer.patch")
set(PRUSASLICER_PATCH_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/apply_prusaslicer_patch.cmake")

# Download once and use the same checkout for both the dependency and main builds.
ExternalProject_Add(prusaslicer_source
    GIT_REPOSITORY https://github.com/prusa3d/PrusaSlicer.git
    GIT_TAG ${PRUSASLICER_VERSION}
    GIT_SHALLOW TRUE
    SOURCE_DIR ${PRUSASLICER_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

ExternalProject_Add_Step(prusaslicer_source apply_patch
    DEPENDEES download
    DEPENDS ${PRUSASLICER_PATCH_FILE} ${PRUSASLICER_PATCH_SCRIPT}
    COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DSOURCE_DIR=${PRUSASLICER_SOURCE_DIR}
        -DPATCH_FILE=${PRUSASLICER_PATCH_FILE}
        -P ${PRUSASLICER_PATCH_SCRIPT}
)

ExternalProject_Add(prusaslicer_deps
    SOURCE_DIR "${PRUSASLICER_SOURCE_DIR}/deps"
    BINARY_DIR ${PRUSASLICER_DEPS_BUILD_DIR}
    DOWNLOAD_COMMAND ""
    DEPENDS prusaslicer_source
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${PRUSASLICER_DEPS_PREFIX}
        -DCMAKE_PREFIX_PATH:PATH=${DESTDIR}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        "-DPrusaSlicer_deps_PACKAGE_EXCLUDES=(wxWidgets|OCCT|Catch2)"
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env CMAKE_POLICY_VERSION_MINIMUM=3.5
        ${CMAKE_COMMAND} --build .
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E env CMAKE_POLICY_VERSION_MINIMUM=3.5
        ${CMAKE_COMMAND} --build . --target install
)

ExternalProject_Add_Step(prusaslicer_deps install_to_destdir
    DEPENDEES install
    COMMAND ${CMAKE_COMMAND}
        -DDEPS_PREFIX=${PRUSASLICER_DEPS_PREFIX}
        -DDESTDIR=${DESTDIR}
        -P ${CMAKE_CURRENT_LIST_DIR}/install_prusaslicer_deps.cmake
)

ExternalProject_Add(prusaslicer
    SOURCE_DIR ${PRUSASLICER_SOURCE_DIR}
    BINARY_DIR ${PRUSASLICER_BUILD_DIR}
    DOWNLOAD_COMMAND ""
    DEPENDS prusaslicer_deps
    CMAKE_ARGS
        -DCMAKE_PREFIX_PATH:PATH=${PRUSASLICER_DEPS_PREFIX}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DSLIC3R_GUI=OFF
        -DSLIC3R_ENABLE_FORMAT_STEP=OFF
        -DSLIC3R_BUILD_TESTS=OFF
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env CMAKE_POLICY_VERSION_MINIMUM=3.5
        ${CMAKE_COMMAND} --build . --config Release
    INSTALL_COMMAND ""
)

ExternalProject_Add_Step(prusaslicer install_to_destdir
    DEPENDEES build
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PRUSASLICER_SOURCE_DIR}
        -DBUILD_DIR=${PRUSASLICER_BUILD_DIR}
        -DDESTDIR=${DESTDIR}
        -P ${CMAKE_CURRENT_LIST_DIR}/install_prusaslicer.cmake
)
