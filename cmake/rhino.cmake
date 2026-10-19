# definition of rhino specific properties

# Global compiler options
# enable C++ exception handling. Throws a warning if disabled.
set(CMAKE_CXX_FLAGS "/EHsc")
# remove default flags provided with CMake for MSVC
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_RELEASE "")


# include default MSVC and Rhino specific properties.
# CMake only allows a single .props file, so we combine all calls in cmake.props and include that.
set_target_properties(${PROJECT_NAME} PROPERTIES VS_USER_PROPS "${CMAKE_CURRENT_LIST_DIR}/cmake.props")

# configure as MFC DLL project
set_target_properties(${PROJECT_NAME} PROPERTIES VS_GLOBAL_KEYWORD "MFCDLLProj")
add_definitions(-D_AFXDLL)
set(CMAKE_MFC_FLAG 2) # https://cmake.org/cmake/help/latest/variable/CMAKE_MFC_FLAG.html

# change file extension to .rhp. This should be done by the rhino-shipped .props script,
# but somehow doesn't work, so we do it manually.
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".rhp")

# set Rhino.exe as debugger in VS solution
set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_COMMAND "${Rhino_DIR}/System/Rhino.exe"
        #VS_DEBUGGER_COMMAND_ARGUMENTS "$<TARGET_FILE:${PROJECT_NAME}>"
)

# Compile definitions
target_compile_definitions(config_target INTERFACE
    WIN64;
    _WINDOWS;
    NDEBUG;
    _USRDLL;
    UNICODE;
    _UNICODE;
    NOMINMAX;
    $<$<CONFIG:Debug>:
        RHINO_DEBUG_PLUGIN;
    >
)

# Compile and link options
# mostly just copied from the MSVC template provided by Rhino SDK
target_compile_options(config_target INTERFACE
    $<$<CONFIG:Debug>:
        /Od;
        /Z7;
    >
    $<$<CONFIG:Release>:
        /O2;
        /Oi;
        /Gy
    >
    /MD
    /sdl;
    /W4;
    ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
    ${DEFAULT_CXX_EXCEPTION_HANDLING}
)

target_link_options(config_target INTERFACE
    $<$<CONFIG:Debug>:
        /INCREMENTAL;
        /DEBUG
    >
    $<$<CONFIG:Release>:
        /INCREMENTAL:NO
    >
    /IGNORE:4042 # ignore "cmake_pch.obj : warning LNK4042: object specified more than once" warnings. I do not fully understand why they are thrown, but afaik the pch object is actually exactly used as intended and everythings works.
)

target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:WINDOWS)