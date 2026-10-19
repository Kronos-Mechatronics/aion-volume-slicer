if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED DESTDIR)
    message(FATAL_ERROR "SOURCE_DIR, BUILD_DIR and DESTDIR are required")
endif()

file(MAKE_DIRECTORY "${DESTDIR}/include/PrusaSlicer")
file(MAKE_DIRECTORY "${DESTDIR}/lib")

file(GLOB PRUSASLICER_LIBS
    "${BUILD_DIR}/src/libslic3r/Release/*.lib"
    "${BUILD_DIR}/src/clipper/Release/*.lib"
    "${BUILD_DIR}/src/libseqarrange/Release/*.lib"
)
file(GLOB_RECURSE BUNDLED_DEPS_LIBS
    "${BUILD_DIR}/bundled_deps/*.lib"
)
file(COPY ${PRUSASLICER_LIBS} ${BUNDLED_DEPS_LIBS} DESTINATION "${DESTDIR}/lib")

file(COPY "${SOURCE_DIR}/src/"
    DESTINATION "${DESTDIR}/include/PrusaSlicer/src"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)
file(COPY "${SOURCE_DIR}/bundled_deps/"
    DESTINATION "${DESTDIR}/include/PrusaSlicer/bundled_deps"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)
file(COPY "${BUILD_DIR}/src/libslic3r/libslic3r_version.h"
    DESTINATION "${DESTDIR}/include/PrusaSlicer/build/src/libslic3r"
)
