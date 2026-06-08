cmake_minimum_required(VERSION 3.21)

# Default version (fallback if git is not available or no tags exist)
set(DEFAULT_VERSION "1.0.1")

set(GIT_VERSION "")
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "v[0-9]*" --match "[0-9]*"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_TAG_RESULT
        )

        if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG)
            string(REGEX REPLACE "^v?([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" GIT_VERSION "${GIT_TAG}")
            if(GIT_VERSION AND GIT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
                message(STATUS "Found git tag version: ${GIT_VERSION} (from tag: ${GIT_TAG})")
            else()
                set(GIT_VERSION "")
            endif()
        endif()
    endif()
endif()

if(GIT_VERSION)
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

project(MIB_Studio_Qt VERSION ${PROJECT_VERSION} LANGUAGES CXX)

include(cmake/options.cmake)
include(cmake/dependencies.cmake)

add_subdirectory(src/backend)

if(MIB_BUILD_BRIDGE)
    add_subdirectory(src/bridge)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(NOT MIB_BUILD_BACKEND_ONLY)
    add_subdirectory(src/frontend/qt)
endif()
