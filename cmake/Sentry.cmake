# Optional integration of sentry-native (the official Sentry C++ SDK).
#
# Controlled by the CMake option MIB_USE_SENTRY (default ON). When enabled
# we use FetchContent to pull a pinned sentry-native release and build it
# in-tree. If the fetch fails (e.g. no network) we automatically fall back
# to local-only crash reporting and the CrashReporter still produces .dmp +
# .json sidecars via Windows MiniDumpWriteDump.
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

include(FetchContent)

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

FetchContent_Declare(
    sentry-native
    GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
    GIT_TAG        ${MIB_SENTRY_GIT_TAG}
    GIT_SHALLOW    TRUE
)

# Wrap fetch in a try-style block so missing-network builds still succeed
# with the local-only crash path.
set(_mib_sentry_fetch_ok TRUE)
FetchContent_GetProperties(sentry-native)
if(NOT sentry-native_POPULATED)
    message(STATUS "sentry-native: fetching ${MIB_SENTRY_GIT_TAG}...")
    FetchContent_Populate(sentry-native
        QUIET
        SOURCE_DIR     ${CMAKE_BINARY_DIR}/_deps/sentry-src
        BINARY_DIR     ${CMAKE_BINARY_DIR}/_deps/sentry-build
        SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/_deps/sentry-subbuild
    )
    if(NOT EXISTS "${sentry-native_SOURCE_DIR}/CMakeLists.txt")
        set(_mib_sentry_fetch_ok FALSE)
    endif()
endif()

if(_mib_sentry_fetch_ok)
    add_subdirectory(${sentry-native_SOURCE_DIR} ${sentry-native_BINARY_DIR} EXCLUDE_FROM_ALL)
    if(TARGET sentry::sentry)
        set(MIB_SENTRY_AVAILABLE ON)
        message(STATUS "sentry-native: enabled (target sentry::sentry, tag ${MIB_SENTRY_GIT_TAG})")
    else()
        message(WARNING "sentry-native: fetch succeeded but sentry::sentry target not found")
    endif()
else()
    message(WARNING "sentry-native: fetch failed; building with local-only crash reporting")
endif()
