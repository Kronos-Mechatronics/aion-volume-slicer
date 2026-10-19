if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED SOURCE_DIR OR NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "GIT_EXECUTABLE, SOURCE_DIR and PATCH_FILE are required")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE CHECK_RESULT
    OUTPUT_VARIABLE CHECK_OUTPUT
    ERROR_VARIABLE CHECK_ERROR
)

if(CHECK_RESULT EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE APPLY_RESULT
        OUTPUT_VARIABLE APPLY_OUTPUT
        ERROR_VARIABLE APPLY_ERROR
    )
    if(NOT APPLY_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to apply PrusaSlicer patch:\n${APPLY_ERROR}")
    endif()
elseif(CHECK_RESULT EQUAL 1)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE REVERSE_CHECK_RESULT
        OUTPUT_VARIABLE REVERSE_CHECK_OUTPUT
        ERROR_VARIABLE REVERSE_CHECK_ERROR
    )
    if(NOT REVERSE_CHECK_RESULT EQUAL 0)
        message(FATAL_ERROR "PrusaSlicer patch cannot be applied:\n${CHECK_ERROR}")
    endif()
else()
    message(FATAL_ERROR "Could not check PrusaSlicer patch:\n${CHECK_ERROR}")
endif()
