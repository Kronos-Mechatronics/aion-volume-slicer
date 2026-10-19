if(NOT DEFINED DEPS_PREFIX OR NOT DEFINED DESTDIR)
    message(FATAL_ERROR "DEPS_PREFIX and DESTDIR are required")
endif()

# Exclude boost from the copy, as it is also build separately and installed to the same destination
file(COPY "${DEPS_PREFIX}/"
    DESTINATION "${DESTDIR}"
    PATTERN "boost-1_83" EXCLUDE
    PATTERN "libboost_*" EXCLUDE
    PATTERN "cmake/boost_*" EXCLUDE
)
