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

set(MIB_ENABLE_MINDVISION_DEFAULT OFF)
option(MIB_ENABLE_MINDVISION
    "Enable MindVision camera SDK integration (Windows only, requires external SDK)"
    ${MIB_ENABLE_MINDVISION_DEFAULT})

option(MIB_BUILD_BACKEND_ONLY
    "Build only backend targets (no frontend executables)"
    OFF)

option(MIB_BUILD_PYTHON_BINDINGS
    "Build the pybind11 Python bindings for mib_processing (bindings/python/)"
    OFF)
