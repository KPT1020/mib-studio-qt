# Optional integration of sentry-native (the official Sentry C++ SDK).
#
# Controlled by the CMake option MIB_USE_SENTRY (default ON). When enabled
# we clone a pinned sentry-native release and build it in-tree. If the fetch
# fails (e.g. no network) we automatically fall back to local-only crash
# reporting and the CrashReporter still produces .dmp + .json sidecars via
# Windows MiniDumpWriteDump.
#
# To explicitly disable: configure with -DMIB_USE_SENTRY=OFF.
# To pin a different version: pass -DMIB_SENTRY_GIT_TAG=<tag>.

option(MIB_USE_SENTRY "Build with sentry-native crash reporting" ON)
set(MIB_SENTRY_GIT_TAG "0.7.20" CACHE STRING "sentry-native release tag to fetch")

set(MIB_SENTRY_AVAILABLE OFF)

if(NOT MIB_USE_SENTRY)
    message(STATUS "sentry-native: disabled by MIB_USE_SENTRY=OFF")
    return()
endif()

# Configure sentry-native build options BEFORE declaring/fetching so they
# stick when MakeAvailable runs.
set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(SENTRY_BACKEND "crashpad" CACHE STRING "" FORCE)
    set(SENTRY_TRANSPORT "winhttp" CACHE STRING "" FORCE)
else()
    set(SENTRY_BACKEND "inproc" CACHE STRING "" FORCE)
    set(SENTRY_TRANSPORT "curl" CACHE STRING "" FORCE)
endif()

set(_mib_sentry_source_dir "${CMAKE_BINARY_DIR}/_deps/sentry-src")
set(_mib_sentry_binary_dir "${CMAKE_BINARY_DIR}/_deps/sentry-build")

if(NOT EXISTS "${_mib_sentry_source_dir}/CMakeLists.txt")
    find_package(Git QUIET)
    if(GIT_FOUND)
        message(STATUS "sentry-native: fetching ${MIB_SENTRY_GIT_TAG}...")
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} clone
                --depth 1
                --recurse-submodules
                --shallow-submodules
                --branch ${MIB_SENTRY_GIT_TAG}
                https://github.com/getsentry/sentry-native.git
                ${_mib_sentry_source_dir}
            RESULT_VARIABLE _mib_sentry_clone_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT _mib_sentry_clone_result EQUAL 0)
            message(WARNING "sentry-native: fetch failed; building with local-only crash reporting")
        endif()
    else()
        message(WARNING "sentry-native: git not found; building with local-only crash reporting")
    endif()
endif()

if(EXISTS "${_mib_sentry_source_dir}/CMakeLists.txt"
   AND WIN32
   AND NOT EXISTS "${_mib_sentry_source_dir}/external/crashpad/CMakeLists.txt")
    find_package(Git QUIET)
    if(GIT_FOUND)
        message(STATUS "sentry-native: fetching Crashpad submodule...")
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} -C ${_mib_sentry_source_dir}
                submodule update --init --recursive --depth 1
            RESULT_VARIABLE _mib_sentry_submodule_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT _mib_sentry_submodule_result EQUAL 0)
            message(WARNING "sentry-native: Crashpad submodule fetch failed; building with local-only crash reporting")
        endif()
    endif()
endif()

set(_mib_sentry_ready OFF)
if(EXISTS "${_mib_sentry_source_dir}/CMakeLists.txt")
    set(_mib_sentry_ready ON)
    if(WIN32 AND NOT EXISTS "${_mib_sentry_source_dir}/external/crashpad/CMakeLists.txt")
        set(_mib_sentry_ready OFF)
        message(WARNING "sentry-native: Crashpad sources missing; building with local-only crash reporting")
    endif()
endif()

if(_mib_sentry_ready)
    add_subdirectory(${_mib_sentry_source_dir} ${_mib_sentry_binary_dir} EXCLUDE_FROM_ALL)
    if(TARGET sentry::sentry)
        set(MIB_SENTRY_AVAILABLE ON)
        message(STATUS "sentry-native: enabled (target sentry::sentry, tag ${MIB_SENTRY_GIT_TAG})")
    else()
        message(WARNING "sentry-native: fetch succeeded but sentry::sentry target not found")
    endif()
else()
    message(WARNING "sentry-native: fetch failed; building with local-only crash reporting")
endif()
