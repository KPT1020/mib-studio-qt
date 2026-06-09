# Optional sentry-native integration (CMake-managed clone).
include(Sentry)

set(MIB_QT_COMPONENTS Core Gui SerialPort)
if(NOT MIB_BUILD_BACKEND_ONLY)
    list(APPEND MIB_QT_COMPONENTS Widgets Charts Network Concurrent)
endif()
find_package(Qt6 COMPONENTS ${MIB_QT_COMPONENTS} REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG QUIET)
find_package(OpenCV CONFIG REQUIRED)
find_package(onnxruntime CONFIG QUIET)

# Hardware SDKs (EGrabber/Coremor) are Windows-only in this project. Non-Windows
# builds compile against service stubs so Linux can be used for fast local
# verification without installing proprietary SDKs.
set(MIB_HAS_EGRABBER OFF)
if(WIN32 AND MIB_ENABLE_HARDWARE_SDKS)
    set(MIB_HAS_EGRABBER ON)
endif()
message(STATUS "Hardware SDK integrations: ${MIB_HAS_EGRABBER}")

set(MIB_HAS_ONNXRUNTIME OFF)
if(TARGET onnxruntime::onnxruntime)
    set(MIB_HAS_ONNXRUNTIME ON)
endif()

# Prefer official SQLite3 package; fallback to unofficial config if needed.
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_package(unofficial-sqlite3 CONFIG QUIET)
endif()

# HDF5 - try both CONFIG and MODULE mode.
find_package(HDF5 CONFIG QUIET)
if(NOT HDF5_FOUND)
    find_package(HDF5 MODULE QUIET)
endif()
