set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/wd4828)
endif()

set(MIB_ENABLE_HARDWARE_SDKS_DEFAULT OFF)
if(WIN32)
    set(MIB_ENABLE_HARDWARE_SDKS_DEFAULT ON)
endif()
option(MIB_ENABLE_HARDWARE_SDKS
    "Enable proprietary Windows hardware SDK integrations (EGrabber/Coremor)"
    ${MIB_ENABLE_HARDWARE_SDKS_DEFAULT})

set(MIB_ENABLE_WINDOWS_PACKAGING_DEFAULT OFF)
if(WIN32)
    set(MIB_ENABLE_WINDOWS_PACKAGING_DEFAULT ON)
endif()
option(MIB_ENABLE_WINDOWS_PACKAGING
    "Enable Windows runtime deployment and InnoSetup packaging targets"
    ${MIB_ENABLE_WINDOWS_PACKAGING_DEFAULT})

option(MIB_BUILD_BACKEND_ONLY
    "Build only backend targets (no frontend executables)"
    OFF)
option(MIB_BUILD_BRIDGE
    "Build frontend-neutral bridge API targets"
    ON)

if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Sentry)
