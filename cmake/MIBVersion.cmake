# Default version (fallback if git is not available or no tags exist)
set(DEFAULT_VERSION "1.0.2")

# Try to get version from git tags
set(GIT_VERSION "")
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        # Get the latest tag matching version pattern (vX.Y.Z or X.Y.Z)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "v[0-9]*" --match "[0-9]*"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_TAG_RESULT
        )

        if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG)
            # Extract version from tag (strip optional 'v' prefix)
            string(REGEX REPLACE "^v?([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" GIT_VERSION "${GIT_TAG}")
            # Verify it's a valid semantic version
            if(GIT_VERSION AND GIT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
                message(STATUS "Found git tag version: ${GIT_VERSION} (from tag: ${GIT_TAG})")
            else()
                set(GIT_VERSION "")
            endif()
        endif()
    endif()
endif()

# Use git version if available and newer than default, otherwise use default
if(GIT_VERSION)
    # Compare versions: if git version is newer, use it; otherwise use default
    if(GIT_VERSION VERSION_GREATER DEFAULT_VERSION OR GIT_VERSION VERSION_EQUAL DEFAULT_VERSION)
        set(PROJECT_VERSION "${GIT_VERSION}")
        message(STATUS "Using version from git tag: ${PROJECT_VERSION}")
    else()
        set(PROJECT_VERSION "${DEFAULT_VERSION}")
        message(STATUS "Using default version (git tag ${GIT_VERSION} is older): ${PROJECT_VERSION}")
    endif()
else()
    set(PROJECT_VERSION "${DEFAULT_VERSION}")
    message(STATUS "Using default version (git tag not available): ${PROJECT_VERSION}")
endif()
