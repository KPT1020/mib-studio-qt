cmake_minimum_required(VERSION 3.21)

# Default version (fallback if git is not available or no tags exist)
set(DEFAULT_VERSION "1.0.1")

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

project(MIB_Studio_Qt VERSION ${PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Suppress character encoding warnings from third-party headers
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

# Preserve PDBs and address-to-source mapping for Release builds so that
# minidumps from end-user crashes can be symbolicated. /Zi generates the
# .pdb; /DEBUG instructs the linker to emit and keep it; /OPT:REF /OPT:ICF
# restore Release optimizations that /DEBUG would otherwise disable.
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# Optional sentry-native integration (CMake-managed clone).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
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

# Prefer official SQLite3 package; fallback to unofficial config if needed
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_package(unofficial-sqlite3 CONFIG QUIET)
endif()

# HDF5 - try both CONFIG and MODULE mode
find_package(HDF5 CONFIG QUIET)
if(NOT HDF5_FOUND)
    find_package(HDF5 MODULE QUIET)
endif()

include(CTest)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MIB_HAS_EGRABBER)
    include_directories("C:/Program Files/Euresys/eGrabber/include")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/Coremor)
endif()

# Backend library (services, APIs)
add_library(mib_backend STATIC
    src/backend/AppBackend.cpp
    src/backend/BackgroundCaptureNotifier.cpp
    include/backend/BackgroundCaptureNotifier.h
    src/backend/Tools.cpp
    src/backend/EModulusLut.cpp
    src/backend/services/Logger.cpp
    src/backend/services/CrashReporter.cpp
    src/backend/diagnostics/CrashStateMirror.cpp
    src/backend/services/SqliteService.cpp
    src/backend/services/CameraControlService.cpp
    src/backend/services/Hdf5Service.cpp
    src/backend/services/CaptureService.cpp
    src/backend/services/RecorderService.cpp
    src/backend/services/ProcessingService.cpp
    src/backend/services/BatchMaskSources.cpp
    src/backend/services/PlaybackService.cpp
    src/backend/services/SyringePumpService.cpp
    src/backend/services/TriggerService.cpp
    src/backend/playback/FrameStore.cpp
    src/camera/common/EGrabberCamera.cpp
    src/camera/mock/MockCamera.cpp
)
if(MIB_HAS_EGRABBER)
    target_sources(mib_backend PRIVATE
        src/backend/services/AutofocusService.cpp
    )
else()
    target_sources(mib_backend PRIVATE
        src/backend/services/AutofocusService.stub.cpp
    )
endif()
if(MIB_HAS_ONNXRUNTIME)
    target_sources(mib_backend PRIVATE
        src/backend/services/YoloService.cpp
    )
else()
    target_sources(mib_backend PRIVATE
        src/backend/services/YoloService.stub.cpp
    )
endif()

# Explicitly enable AUTOMOC for this target to ensure MOC files are generated
set_target_properties(mib_backend PROPERTIES AUTOMOC ON)
target_compile_definitions(mib_backend PUBLIC
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
)
if(MIB_HAS_EGRABBER)
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_EGRABBER=1)
else()
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_EGRABBER=0)
endif()
if(MIB_HAS_ONNXRUNTIME)
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_ONNXRUNTIME=1)
else()
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_ONNXRUNTIME=0)
endif()

target_include_directories(mib_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MIB_HAS_EGRABBER)
    target_include_directories(mib_backend PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Coremor
        "C:/Program Files/Euresys/eGrabber/include"
    )
endif()

# Coremor library
set(COREMOR_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/Coremor)
target_link_libraries(mib_backend PUBLIC
    spdlog::spdlog
    Qt6::Core
    Qt6::Gui
    Qt6::SerialPort
)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mib_backend PUBLIC nlohmann_json::nlohmann_json)
endif()

# CrashReporter requires dbghelp on Windows for MiniDumpWriteDump.
if(WIN32)
    target_link_libraries(mib_backend PUBLIC dbghelp)
endif()

# Wire optional sentry-native. When MIB_USE_SENTRY=OFF (or the fetch failed)
# MIB_SENTRY_AVAILABLE stays false and CrashReporter falls back to the
# local-only path.
if(MIB_SENTRY_AVAILABLE)
    target_link_libraries(mib_backend PUBLIC sentry::sentry)
    target_compile_definitions(mib_backend PUBLIC MIB_USE_SENTRY=1)
else()
    target_compile_definitions(mib_backend PUBLIC MIB_USE_SENTRY=0)
endif()
if(MIB_HAS_EGRABBER)
    target_link_libraries(mib_backend PUBLIC
        ${COREMOR_LIB_DIR}/XMT_DLL_SER.lib
    )
endif()

if(TARGET opencv_world)
    target_link_libraries(mib_backend PUBLIC opencv_world)
elseif(TARGET opencv_core)
    target_link_libraries(mib_backend PUBLIC opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio)
elseif(DEFINED OpenCV_LIBS)
    target_include_directories(mib_backend PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mib_backend PUBLIC ${OpenCV_LIBS})
endif()

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mib_backend PUBLIC nlohmann_json::nlohmann_json)
endif()

# ONNX Runtime linking
if(TARGET onnxruntime::onnxruntime)
    target_link_libraries(mib_backend PUBLIC onnxruntime::onnxruntime)
else()
    message(WARNING "onnxruntime::onnxruntime target not found - building with YoloService stub")
endif()

# SQLite linking (whichever is available)
if(SQLite3_FOUND)
    target_link_libraries(mib_backend PUBLIC SQLite::SQLite3)
elseif(TARGET unofficial::sqlite3::sqlite3)
    target_link_libraries(mib_backend PUBLIC unofficial::sqlite3::sqlite3)
elseif(TARGET sqlite3)
    target_link_libraries(mib_backend PUBLIC sqlite3)
endif()

# HDF5 linking - using C API (hdf5.lib and hdf5_hl.lib)
# Try imported targets first, then fallback to manual linking
if(TARGET hdf5::hdf5-shared)
    target_link_libraries(mib_backend PUBLIC hdf5::hdf5-shared)
elseif(TARGET hdf5::hdf5)
    target_link_libraries(mib_backend PUBLIC hdf5::hdf5)
elseif(TARGET hdf5::hdf5_cpp-shared)
    target_link_libraries(mib_backend PUBLIC hdf5::hdf5_cpp-shared)
elseif(TARGET hdf5::hdf5-cpp)
    target_link_libraries(mib_backend PUBLIC hdf5::hdf5-cpp)
elseif(TARGET HDF5::HDF5)
    target_link_libraries(mib_backend PUBLIC HDF5::HDF5)
else()
    # Fallback: manually link HDF5 libraries from Conan
    # Check common Conan install locations
    set(CONAN_LIB_PATHS
        "${CMAKE_BINARY_DIR}/conan"
        "${CMAKE_PREFIX_PATH}"
    )
    find_library(HDF5_LIB hdf5 PATHS ${CONAN_LIB_PATHS} PATH_SUFFIXES lib NO_DEFAULT_PATH)
    find_library(HDF5_HL_LIB hdf5_hl PATHS ${CONAN_LIB_PATHS} PATH_SUFFIXES lib NO_DEFAULT_PATH)
    if(HDF5_LIB AND HDF5_HL_LIB)
        target_link_libraries(mib_backend PUBLIC ${HDF5_LIB} ${HDF5_HL_LIB})
        message(STATUS "Linked HDF5 libraries manually: ${HDF5_LIB} ${HDF5_HL_LIB}")
    elseif(HDF5_FOUND)
        # Use variables from find_package if available
        if(HDF5_INCLUDE_DIRS)
            target_include_directories(mib_backend PUBLIC ${HDF5_INCLUDE_DIRS})
        endif()
        if(HDF5_LIBRARIES)
            target_link_libraries(mib_backend PUBLIC ${HDF5_LIBRARIES})
        endif()
    else()
        message(WARNING "HDF5 libraries not found - HDF5 functionality will not be available")
    endif()
endif()

# Backend smoke test (no frontend dependencies).
if(BUILD_TESTING)
    add_executable(mib_backend_smoke_test
        src/tests/backend_smoke_test.cpp
    )
    target_link_libraries(mib_backend_smoke_test PRIVATE mib_backend)
    add_test(NAME backend.smoke COMMAND mib_backend_smoke_test)
    set_tests_properties(backend.smoke PROPERTIES LABELS "backend")

    add_executable(processing_batch_pipeline_test
        src/tests/processing_batch_pipeline_test.cpp
    )
    target_link_libraries(processing_batch_pipeline_test PRIVATE mib_backend)
    add_test(NAME backend.processing_batch_pipeline COMMAND processing_batch_pipeline_test)
    set_tests_properties(backend.processing_batch_pipeline PROPERTIES LABELS "backend")

    add_executable(kin6_batch_pipeline_evidence
        src/tests/kin6_batch_pipeline_evidence.cpp
    )
    target_link_libraries(kin6_batch_pipeline_evidence PRIVATE mib_backend)

    add_executable(kin7_empty_frame_evidence
        src/tests/kin7_empty_frame_evidence.cpp
    )
    target_link_libraries(kin7_empty_frame_evidence PRIVATE mib_backend)

    add_executable(kin6_mib_app_capture_proof
        src/tests/kin6_mib_app_capture_proof.cpp
    )
    target_link_libraries(kin6_mib_app_capture_proof PRIVATE mib_backend)
    add_test(NAME backend.kin6_mib_app_capture_proof COMMAND kin6_mib_app_capture_proof)
    set_tests_properties(backend.kin6_mib_app_capture_proof PROPERTIES LABELS "backend")

    add_executable(mib_processing_multi_object_test
        src/tests/processing_multi_object_test.cpp
    )
    target_link_libraries(mib_processing_multi_object_test PRIVATE mib_backend)
    add_test(NAME backend.processing_multi_object COMMAND mib_processing_multi_object_test)
    set_tests_properties(backend.processing_multi_object PROPERTIES LABELS "backend")
endif()

if(NOT MIB_BUILD_BACKEND_ONLY)
# Frontend application (Qt Widgets)
set(FRONTEND_COMMON_SOURCES
    # Core
    src/frontend/core/MainWindow.cpp
    include/frontend/core/MainWindow.h
    resources/ui/MainWindow.ui
    # Tabs
    src/frontend/tabs/ConnectTab.cpp
    include/frontend/tabs/ConnectTab.h
    resources/ui/ConnectTab.ui
    src/frontend/tabs/ConfigTabs.cpp
    include/frontend/tabs/ConfigTabs.h
    src/frontend/tabs/ExperimentMonitoringTab.cpp
    include/frontend/tabs/ExperimentMonitoringTab.h
    src/frontend/widgets/ZoomableChartView.cpp
    include/frontend/widgets/ZoomableChartView.h
    resources/ui/ExperimentMonitoringTab.ui
    src/frontend/tabs/HdfReviewTab.cpp
    include/frontend/tabs/HdfReviewTab.h
    resources/ui/HdfReviewTab.ui
    src/frontend/tabs/NanopositionerTab.cpp
    include/frontend/tabs/NanopositionerTab.h
    resources/ui/NanopositionerTab.ui
    src/frontend/tabs/SyringePumpTab.cpp
    include/frontend/tabs/SyringePumpTab.h
    resources/ui/SyringePumpTab.ui
    src/frontend/tabs/OverviewTab.cpp
    include/frontend/tabs/OverviewTab.h
    resources/ui/OverviewTab.ui
    src/frontend/tabs/PreviewPage.cpp
    include/frontend/tabs/PreviewPage.h
    resources/ui/PreviewPage.ui
    # Dialogs
    src/frontend/dialogs/BatchMaskDialog.cpp
    include/frontend/dialogs/BatchMaskDialog.h
    src/frontend/dialogs/BufferSaveDialog.cpp
    include/frontend/dialogs/BufferSaveDialog.h
    resources/ui/BufferSaveDialog.ui
    src/frontend/dialogs/ConversionFactorDialog.cpp
    include/frontend/dialogs/ConversionFactorDialog.h
    resources/ui/ConversionFactorDialog.ui
    src/frontend/dialogs/FrameViewerDialog.cpp
    include/frontend/dialogs/FrameViewerDialog.h
    resources/ui/FrameViewerDialog.ui
    src/frontend/dialogs/MockConfigDialog.cpp
    include/frontend/dialogs/MockConfigDialog.h
    resources/ui/MockConfigDialog.ui
    src/frontend/dialogs/MonitoringSettingsDialog.cpp
    include/frontend/dialogs/MonitoringSettingsDialog.h
    resources/ui/MonitoringSettingsDialog.ui
    src/frontend/dialogs/ProcessingSettingsDialog.cpp
    include/frontend/dialogs/ProcessingSettingsDialog.h
    resources/ui/ProcessingSettingsDialog.ui
    src/frontend/dialogs/SyringePumpSettingsDialog.cpp
    include/frontend/dialogs/SyringePumpSettingsDialog.h
    resources/ui/SyringePumpSettingsDialog.ui
    # Controllers
    src/frontend/controllers/CameraController.cpp
    include/frontend/controllers/CameraController.h
    src/frontend/controllers/ExperimentController.cpp
    include/frontend/controllers/ExperimentController.h
    # Utils
    src/frontend/utils/ConfigPathManager.cpp
    include/frontend/utils/ConfigPathManager.h
    src/frontend/utils/OverlayRenderer.cpp
    include/frontend/utils/OverlayRenderer.h
    src/frontend/utils/EgrabberConfigParser.cpp
    include/frontend/utils/EgrabberConfigParser.h
    src/frontend/utils/FileIOUtils.cpp
    include/frontend/utils/FileIOUtils.h
    src/frontend/utils/JsonFlatten.cpp
    include/frontend/utils/JsonFlatten.h
    src/frontend/utils/RoiManager.cpp
    include/frontend/utils/RoiManager.h
    src/frontend/utils/RoiDrawCanvas.cpp
    include/frontend/utils/RoiDrawCanvas.h
    src/frontend/utils/SimpleImageCanvas.cpp
    include/frontend/utils/SimpleImageCanvas.h
    src/frontend/utils/StatsDisplayManager.cpp
    include/frontend/utils/StatsDisplayManager.h
    src/frontend/utils/StatisticsPanel.cpp
    include/frontend/utils/StatisticsPanel.h
    src/frontend/utils/BackgroundPreviewWidget.cpp
    include/frontend/utils/BackgroundPreviewWidget.h
    src/frontend/utils/SidebarWidget.cpp
    include/frontend/utils/SidebarWidget.h
    # Models
    src/frontend/models/HdfMetricsModel.cpp
    include/frontend/models/HdfMetricsModel.h
    src/frontend/models/JsonTableModel.cpp
    include/frontend/models/JsonTableModel.h
    # System
    src/frontend/system/AppConfigWatcher.cpp
    include/frontend/system/AppConfigWatcher.h
    src/frontend/system/AutoUpdater.cpp
    include/frontend/system/AutoUpdater.h
    src/frontend/system/DeviceInitManager.cpp
    include/frontend/system/DeviceInitManager.h
    src/frontend/system/PlaybackPanel.cpp
    include/frontend/system/PlaybackPanel.h
    # Resources
    resources/defaults.qrc
)

set(MIB_STUDIO_QT_EXTRA_SOURCES)
if(WIN32)
    list(APPEND MIB_STUDIO_QT_EXTRA_SOURCES resources/app_icon.rc)
    add_executable(mib_studio_qt WIN32
        src/frontend/core/main.cpp
        ${FRONTEND_COMMON_SOURCES}
        ${MIB_STUDIO_QT_EXTRA_SOURCES}
    )
else()
    add_executable(mib_studio_qt
        src/frontend/core/main.cpp
        ${FRONTEND_COMMON_SOURCES}
        ${MIB_STUDIO_QT_EXTRA_SOURCES}
    )
endif()

set_target_properties(mib_studio_qt PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui"
)

target_include_directories(mib_studio_qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MIB_HAS_EGRABBER)
    target_include_directories(mib_studio_qt PRIVATE
        "C:/Program Files/Euresys/eGrabber/include"
    )
endif()

target_link_libraries(mib_studio_qt PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::Network
    Qt6::Concurrent
    mib_backend
    spdlog::spdlog
)

# App version exported from CMake (used for update checks, About dialogs, etc.)
target_compile_definitions(mib_studio_qt PRIVATE
    MIB_STUDIO_QT_VERSION="${PROJECT_VERSION}"
)

# OpenCV linking (prefer imported targets; fallback to variables)
if(TARGET opencv_world)
    target_link_libraries(mib_studio_qt PRIVATE opencv_world)
elseif(TARGET opencv_core)
    target_link_libraries(mib_studio_qt PRIVATE opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio)
elseif(DEFINED OpenCV_LIBS)
    target_include_directories(mib_studio_qt PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mib_studio_qt PRIVATE ${OpenCV_LIBS})
endif()

# Qt deployment via windeployqt (Conan)
if(WIN32 AND MIB_ENABLE_WINDOWS_PACKAGING)
    # Find Qt6 installation path from Conan
    # Qt6_DIR is set by Conan's CMakeDeps generator
    if(DEFINED Qt6_DIR)
        get_filename_component(QT6_ROOT "${Qt6_DIR}/../.." ABSOLUTE)
    elseif(TARGET Qt6::qmake)
        get_target_property(QT6_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        if(QT6_QMAKE_EXECUTABLE)
            get_filename_component(QT6_ROOT "${QT6_QMAKE_EXECUTABLE}/../.." ABSOLUTE)
        endif()
    endif()
    
    # Fallback to CMAKE_PREFIX_PATH if Qt6_DIR not found
    if(NOT DEFINED QT6_ROOT)
        set(QT6_ROOT "${CMAKE_PREFIX_PATH}")
    endif()
    
    # Set Qt bin directories (Conan typically installs in CMAKE_PREFIX_PATH)
    # For multi-config builds, use imported targets first (they handle Debug/Release automatically)
    set(CONAN_QT_BIN_DIR "")
    
    # Try to get path from Qt6::Core imported target (handles multi-config properly)
    if(TARGET Qt6::Core)
        # Try Release location first (more common for deployment)
        get_target_property(_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_RELEASE)
        if(_QT_CORE_LOCATION)
            get_filename_component(CONAN_QT_BIN_DIR "${_QT_CORE_LOCATION}" DIRECTORY)
            get_filename_component(QT6_ROOT "${CONAN_QT_BIN_DIR}" DIRECTORY)
        else()
            # Try Debug location as fallback
            get_target_property(_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_DEBUG)
            if(_QT_CORE_LOCATION)
                get_filename_component(CONAN_QT_BIN_DIR "${_QT_CORE_LOCATION}" DIRECTORY)
                get_filename_component(QT6_ROOT "${CONAN_QT_BIN_DIR}" DIRECTORY)
            endif()
        endif()
    endif()
    
    # Fallback: search CMAKE_PREFIX_PATH (may only have paths from last conan install)
    if(NOT CONAN_QT_BIN_DIR)
        foreach(_path IN LISTS CMAKE_PREFIX_PATH)
            if(EXISTS "${_path}/Qt6Core.dll" OR EXISTS "${_path}/Qt6Cored.dll")
                set(CONAN_QT_BIN_DIR "${_path}")
                get_filename_component(QT6_ROOT "${_path}" DIRECTORY)
                break()
            endif()
        endforeach()
    endif()
    
    # Final fallback: use computed path if not found
    if(NOT CONAN_QT_BIN_DIR)
        if(DEFINED QT6_ROOT)
            set(CONAN_QT_BIN_DIR "${QT6_ROOT}/bin")
        else()
            message(WARNING "Could not determine Qt bin directory - Qt deployment may fail")
        endif()
    endif()
    
    set(CONAN_QT_DEBUG_BIN_DIR "${CONAN_QT_BIN_DIR}")  # Debug and Release DLLs in same directory
    set(CONAN_QT_TOOLS_DIR "${QT6_ROOT}/tools/qt6/bin")

    # Find windeployqt: prefer the exact Conan package trees (CMakeDeps sets qt_PACKAGE_FOLDER_*).
    # A cached find_program result can keep pointing at an older qt/*/p folder after `conan install`,
    # which then mismatches the Qt DLLs we link (different package IDs).
    unset(WINDEPLOYQT_RELEASE CACHE)
    unset(WINDEPLOYQT_DEBUG CACHE)
    set(_WINDEPLOYQT_RELEASE_CAND "")
    set(_WINDEPLOYQT_DEBUG_CAND "")
    if(DEFINED qt_PACKAGE_FOLDER_RELEASE AND EXISTS "${qt_PACKAGE_FOLDER_RELEASE}/bin/windeployqt.exe")
        set(_WINDEPLOYQT_RELEASE_CAND "${qt_PACKAGE_FOLDER_RELEASE}/bin/windeployqt.exe")
    endif()
    if(DEFINED qt_PACKAGE_FOLDER_DEBUG AND EXISTS "${qt_PACKAGE_FOLDER_DEBUG}/bin/windeployqt.exe")
        set(_WINDEPLOYQT_DEBUG_CAND "${qt_PACKAGE_FOLDER_DEBUG}/bin/windeployqt.exe")
    endif()
    if(_WINDEPLOYQT_RELEASE_CAND)
        set(WINDEPLOYQT_RELEASE "${_WINDEPLOYQT_RELEASE_CAND}" CACHE FILEPATH "windeployqt for Release deployment" FORCE)
    else()
        find_program(WINDEPLOYQT_RELEASE windeployqt.exe
            PATHS "${CONAN_QT_TOOLS_DIR}" "${CONAN_QT_BIN_DIR}"
            NO_DEFAULT_PATH
        )
        if(NOT WINDEPLOYQT_RELEASE)
            find_program(WINDEPLOYQT_RELEASE windeployqt.exe
                PATHS "${CONAN_QT_BIN_DIR}" "${CMAKE_PREFIX_PATH}/bin"
            )
        endif()
    endif()
    if(_WINDEPLOYQT_DEBUG_CAND)
        set(WINDEPLOYQT_DEBUG "${_WINDEPLOYQT_DEBUG_CAND}" CACHE FILEPATH "windeployqt for Debug deployment" FORCE)
    else()
        find_program(WINDEPLOYQT_DEBUG windeployqt.debug.bat
            PATHS "${CONAN_QT_TOOLS_DIR}" "${CONAN_QT_BIN_DIR}"
            NO_DEFAULT_PATH
        )
    endif()
    
    # Discover extra third-party codec runtimes used by Qt imageformats plugins (copy after windeployqt)
    # This helps ensure qjpeg/qtiff/qwebp/qjp2 load on machines without Conan packages in PATH.
    set(CONAN_SEARCH_PATHS
        "${CONAN_QT_BIN_DIR}"
        "${CMAKE_PREFIX_PATH}/bin"
        "${CMAKE_BINARY_DIR}/conan/bin"
    )
    set(_EXTRA_CODEC_PATTERNS
        # Release + Debug bins, so Debug runs work too
    )
    foreach(_search_path IN LISTS CONAN_SEARCH_PATHS)
        list(APPEND _EXTRA_CODEC_PATTERNS
            "${_search_path}/jpeg*.dll"
            "${_search_path}/libjpeg*.dll"
            "${_search_path}/tiff*.dll"
            "${_search_path}/libtiff*.dll"
            "${_search_path}/liblzma*.dll"
            "${_search_path}/libwebp*.dll"
            "${_search_path}/libwebpdemux*.dll"
            "${_search_path}/libwebpmux*.dll"
            "${_search_path}/libsharpyuv*.dll"
            "${_search_path}/openjp*.dll"      # openjp2 for qjp2
            "${_search_path}/zstd*.dll"
            "${_search_path}/libdeflate*.dll"
        )
    endforeach()
    set(_EXTRA_CODEC_DLLS "")
    foreach(_pat IN LISTS _EXTRA_CODEC_PATTERNS)
        file(GLOB _found "${_pat}")
        list(APPEND _EXTRA_CODEC_DLLS ${_found})
    endforeach()
    list(REMOVE_DUPLICATES _EXTRA_CODEC_DLLS)

    foreach(app_target IN ITEMS mib_studio_qt)
        # Use different windeployqt for Debug vs Release
        if(WINDEPLOYQT_RELEASE)
            # Check if Debug and Release Qt DLLs exist
            # For multi-config, Debug and Release packages may be in different Conan directories
            set(_QT_DEBUG_DLL_FOUND FALSE)
            set(_QT_RELEASE_DLL_FOUND FALSE)
            set(_QT_DEBUG_BIN_DIR "")
            set(_QT_RELEASE_BIN_DIR "")

            # Prefer CMakeDeps package roots (unique per build type). Avoids cache-wide globs picking a
            # random Qt6Core.dll when several Conan package folders exist.
            if(DEFINED qt_PACKAGE_FOLDER_DEBUG AND EXISTS "${qt_PACKAGE_FOLDER_DEBUG}/bin/Qt6Cored.dll")
                set(_QT_DEBUG_BIN_DIR "${qt_PACKAGE_FOLDER_DEBUG}/bin")
                set(_QT_DEBUG_DLL_FOUND TRUE)
            endif()
            if(DEFINED qt_PACKAGE_FOLDER_RELEASE AND EXISTS "${qt_PACKAGE_FOLDER_RELEASE}/bin/Qt6Core.dll")
                set(_QT_RELEASE_BIN_DIR "${qt_PACKAGE_FOLDER_RELEASE}/bin")
                set(_QT_RELEASE_DLL_FOUND TRUE)
            endif()

            # Check Debug / Release locations using imported target (non-Conan or unusual layouts)
            if(TARGET Qt6::Core)
                if(NOT _QT_DEBUG_DLL_FOUND)
                    get_target_property(_QT_CORE_LOCATION_DEBUG Qt6::Core IMPORTED_LOCATION_DEBUG)
                    if(_QT_CORE_LOCATION_DEBUG)
                        get_filename_component(_QT_DEBUG_BIN_DIR "${_QT_CORE_LOCATION_DEBUG}" DIRECTORY)
                        if(EXISTS "${_QT_DEBUG_BIN_DIR}/Qt6Cored.dll")
                            set(_QT_DEBUG_DLL_FOUND TRUE)
                        endif()
                    else()
                        get_target_property(_QT_CORE_IMPLIB_DEBUG Qt6::Core IMPORTED_IMPLIB_DEBUG)
                        if(_QT_CORE_IMPLIB_DEBUG)
                            get_filename_component(_QT_DEBUG_LIB_DIR "${_QT_CORE_IMPLIB_DEBUG}" DIRECTORY)
                            get_filename_component(_QT_DEBUG_PREFIX "${_QT_DEBUG_LIB_DIR}" DIRECTORY)
                            set(_QT_DEBUG_BIN_DIR "${_QT_DEBUG_PREFIX}/bin")
                            if(EXISTS "${_QT_DEBUG_BIN_DIR}/Qt6Cored.dll")
                                set(_QT_DEBUG_DLL_FOUND TRUE)
                            endif()
                        endif()
                    endif()
                endif()
                if(NOT _QT_RELEASE_DLL_FOUND)
                    get_target_property(_QT_CORE_LOCATION_RELEASE Qt6::Core IMPORTED_LOCATION_RELEASE)
                    if(_QT_CORE_LOCATION_RELEASE)
                        get_filename_component(_QT_RELEASE_BIN_DIR "${_QT_CORE_LOCATION_RELEASE}" DIRECTORY)
                        if(EXISTS "${_QT_RELEASE_BIN_DIR}/Qt6Core.dll")
                            set(_QT_RELEASE_DLL_FOUND TRUE)
                        endif()
                    else()
                        get_target_property(_QT_CORE_IMPLIB_RELEASE Qt6::Core IMPORTED_IMPLIB_RELEASE)
                        if(_QT_CORE_IMPLIB_RELEASE)
                            get_filename_component(_QT_RELEASE_LIB_DIR "${_QT_CORE_IMPLIB_RELEASE}" DIRECTORY)
                            get_filename_component(_QT_RELEASE_PREFIX "${_QT_RELEASE_LIB_DIR}" DIRECTORY)
                            set(_QT_RELEASE_BIN_DIR "${_QT_RELEASE_PREFIX}/bin")
                            if(EXISTS "${_QT_RELEASE_BIN_DIR}/Qt6Core.dll")
                                set(_QT_RELEASE_DLL_FOUND TRUE)
                            endif()
                        endif()
                    endif()
                endif()
            endif()
            
            # Fallback: check CONAN_QT_BIN_DIR (may only have one build_type)
            if(CONAN_QT_BIN_DIR)
                if(NOT _QT_DEBUG_DLL_FOUND AND EXISTS "${CONAN_QT_BIN_DIR}/Qt6Cored.dll")
                    set(_QT_DEBUG_DLL_FOUND TRUE)
                    set(_QT_DEBUG_BIN_DIR "${CONAN_QT_BIN_DIR}")
                endif()
                if(NOT _QT_RELEASE_DLL_FOUND AND EXISTS "${CONAN_QT_BIN_DIR}/Qt6Core.dll")
                    set(_QT_RELEASE_DLL_FOUND TRUE)
                    set(_QT_RELEASE_BIN_DIR "${CONAN_QT_BIN_DIR}")
                endif()
            endif()

            # Last resort: search Conan cache (non-deterministic; only when still unknown)
            if(NOT _QT_DEBUG_DLL_FOUND OR NOT _QT_RELEASE_DLL_FOUND)
                set(_CONAN_CACHE_ROOT "$ENV{USERPROFILE}/.conan2")
                if(EXISTS "${_CONAN_CACHE_ROOT}")
                    if(NOT _QT_DEBUG_DLL_FOUND)
                        file(GLOB _qt_debug_dll_candidates "${_CONAN_CACHE_ROOT}/p/b/*/p/bin/Qt6Cored.dll")
                        list(LENGTH _qt_debug_dll_candidates _qt_debug_count)
                        if(_qt_debug_count GREATER 0)
                            list(SORT _qt_debug_dll_candidates)
                            list(GET _qt_debug_dll_candidates 0 _qt_debug_dll)
                            get_filename_component(_QT_DEBUG_BIN_DIR "${_qt_debug_dll}" DIRECTORY)
                            set(_QT_DEBUG_DLL_FOUND TRUE)
                        endif()
                    endif()
                    if(NOT _QT_RELEASE_DLL_FOUND)
                        file(GLOB _qt_release_dll_candidates "${_CONAN_CACHE_ROOT}/p/b/*/p/bin/Qt6Core.dll")
                        list(LENGTH _qt_release_dll_candidates _qt_release_count)
                        if(_qt_release_count GREATER 0)
                            list(SORT _qt_release_dll_candidates)
                            list(GET _qt_release_dll_candidates 0 _qt_release_dll)
                            get_filename_component(_QT_RELEASE_BIN_DIR "${_qt_release_dll}" DIRECTORY)
                            set(_QT_RELEASE_DLL_FOUND TRUE)
                        endif()
                    endif()
                endif()
            endif()
            
            # Warn if required DLLs are missing (for multi-config, both should be available)
            if(NOT _QT_DEBUG_DLL_FOUND)
                if(_QT_DEBUG_BIN_DIR)
                    message(WARNING "Qt Debug DLLs not found in ${_QT_DEBUG_BIN_DIR}")
                else()
                    message(WARNING "Qt Debug DLLs not found (Debug package location not determined)")
                endif()
                message(WARNING "  Debug builds may fail. Install Debug packages:")
                message(WARNING "  conan install . --output-folder=build --build=missing -s build_type=Debug")
            endif()
            if(NOT _QT_RELEASE_DLL_FOUND)
                if(_QT_RELEASE_BIN_DIR)
                    message(WARNING "Qt Release DLLs not found in ${_QT_RELEASE_BIN_DIR}")
                else()
                    message(WARNING "Qt Release DLLs not found (Release package location not determined)")
                endif()
                message(WARNING "  Release builds may fail. Install Release packages:")
                message(WARNING "  conan install . --output-folder=build --build=missing -s build_type=Release")
            endif()
            
            # Use config-specific deployment flags and paths
            set(_WINDEPLOYQT_DEBUG_EXE "")
            if(_QT_DEBUG_BIN_DIR)
                if(EXISTS "${_QT_DEBUG_BIN_DIR}/windeployqt.debug.bat")
                    set(_WINDEPLOYQT_DEBUG_EXE "${_QT_DEBUG_BIN_DIR}/windeployqt.debug.bat")
                elseif(EXISTS "${_QT_DEBUG_BIN_DIR}/windeployqt.exe")
                    set(_WINDEPLOYQT_DEBUG_EXE "${_QT_DEBUG_BIN_DIR}/windeployqt.exe")
                endif()
            endif()

            if(_WINDEPLOYQT_DEBUG_EXE)
                set(_WINDEPLOYQT_EXE "$<IF:$<STREQUAL:$<CONFIG>,Debug>,${_WINDEPLOYQT_DEBUG_EXE},${WINDEPLOYQT_RELEASE}>")
            elseif(WINDEPLOYQT_DEBUG)
                set(_WINDEPLOYQT_EXE "$<IF:$<STREQUAL:$<CONFIG>,Debug>,${WINDEPLOYQT_DEBUG},${WINDEPLOYQT_RELEASE}>")
            else()
                # Use windeployqt.exe with --debug/--release flag if windeployqt.debug.bat doesn't exist
                set(_WINDEPLOYQT_EXE "${WINDEPLOYQT_RELEASE}")
            endif()
            set(_WINDEPLOYQT_FLAG "$<IF:$<STREQUAL:$<CONFIG>,Debug>,--debug,--release>")

            # Use appropriate bin directory for PATH based on configuration
            # For Debug builds, prefer Debug bin dir; for Release, prefer Release bin dir
            set(_WINDEPLOYQT_PATH "")
            if(_QT_DEBUG_BIN_DIR AND _QT_RELEASE_BIN_DIR)
                # Both found - use generator expression to select based on config
                set(_WINDEPLOYQT_PATH "$<IF:$<STREQUAL:$<CONFIG>,Debug>,${_QT_DEBUG_BIN_DIR},${_QT_RELEASE_BIN_DIR}>")
            elseif(_QT_RELEASE_BIN_DIR)
                set(_WINDEPLOYQT_PATH "${_QT_RELEASE_BIN_DIR}")
            elseif(_QT_DEBUG_BIN_DIR)
                set(_WINDEPLOYQT_PATH "${_QT_DEBUG_BIN_DIR}")
            elseif(CONAN_QT_BIN_DIR)
                set(_WINDEPLOYQT_PATH "${CONAN_QT_BIN_DIR}")
            endif()
            
            add_custom_command(TARGET ${app_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E env "PATH=${_WINDEPLOYQT_PATH};$ENV{PATH}"
                        "${_WINDEPLOYQT_EXE}"
                        "${_WINDEPLOYQT_FLAG}"
                        --compiler-runtime
                        "$<TARGET_FILE:${app_target}>"
                COMMENT "windeployqt ($<CONFIG>): ${app_target}"
            )
        else()
            message(WARNING "windeployqt not found - Qt deployment will be skipped")
        endif()
        # Copy extra codec/runtime DLLs if present
        foreach(_dll IN LISTS _EXTRA_CODEC_DLLS)
            add_custom_command(TARGET ${app_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:${app_target}>/"
                VERBATIM
            )
        endforeach()
        # Copy Coremor DLL
        add_custom_command(TARGET ${app_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/include/Coremor/XMT_DLL_SER.dll"
                    "$<TARGET_FILE_DIR:${app_target}>/"
            COMMENT "Copy Coremor DLL: ${app_target}"
        )
        # Copy Conan package DLLs (OpenCV, HDF5, SQLite3, etc.)
        add_custom_command(TARGET ${app_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${app_target}>
                    $<TARGET_FILE_DIR:${app_target}>
            COMMAND_EXPAND_LISTS
            COMMENT "Copying Conan package DLLs for ${app_target}"
        )
        # Copy resources/models directory (YOLO model files)
        add_custom_command(TARGET ${app_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${app_target}>/resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/models/yolo11n-seg.onnx"
                    "$<TARGET_FILE_DIR:${app_target}>/resources/models/"
            COMMENT "Copying YOLO model files for ${app_target}"
        )

        # crashpad_handler.exe is required next to the application when
        # sentry-native is built with the Crashpad backend (the default on
        # Windows). Without it Sentry falls back to in-process capture and
        # cannot collect dumps for non-recoverable failures.
        if(MIB_SENTRY_AVAILABLE AND TARGET crashpad_handler)
            add_custom_command(TARGET ${app_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:crashpad_handler>"
                        "$<TARGET_FILE_DIR:${app_target}>/"
                COMMENT "Copying crashpad_handler.exe for ${app_target}"
            )
        endif()
    endforeach()
endif()

# Ensure the .pdb for the application ends up next to the .exe so it can
# be archived per release for later symbolication of remote crashes.
if(MSVC)
    set_target_properties(mib_studio_qt PROPERTIES
        PDB_NAME              "mib_studio_qt"
        PDB_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/$<CONFIG>"
        COMPILE_PDB_NAME      "mib_studio_qt"
        COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
endif()

# Windows installer packaging (InnoSetup)
if(WIN32 AND MIB_ENABLE_WINDOWS_PACKAGING)
    # Find InnoSetup compiler
    find_program(ISCC_EXE
        NAMES ISCC.exe
        PATHS
            "C:/Program Files (x86)/Inno Setup 6"
            "C:/Program Files/Inno Setup 6"
            "$ENV{ProgramFiles}/Inno Setup 6"
        DOC "InnoSetup Compiler"
    )
    
    if(ISCC_EXE)
        # Create dist directory
        set(INSTALLER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/dist")
        file(MAKE_DIRECTORY "${INSTALLER_OUTPUT_DIR}")

        # InnoSetup script paths
        set(INNOSETUP_SCRIPT "${CMAKE_SOURCE_DIR}/resources/installers/mib-studio-qt.iss")
        set(INNOSETUP_UPDATE_SCRIPT "${CMAKE_SOURCE_DIR}/resources/installers/mib-studio-qt-update.iss")

        # Sentry DSN / environment passed through to InnoSetup so installed
        # builds get MIB_SENTRY_DSN / MIB_CRASH_ENV as system env vars. Empty
        # by default — release CI passes the real value via -D on configure.
        set(MIB_SENTRY_DSN "" CACHE STRING
            "Sentry DSN baked into the installer (HKLM env var)")
        set(MIB_SENTRY_ENVIRONMENT "production" CACHE STRING
            "Sentry environment tag baked into the installer")

        set(_ISCC_SENTRY_FLAGS)
        if(MIB_SENTRY_DSN)
            list(APPEND _ISCC_SENTRY_FLAGS "/DSentryDSN=${MIB_SENTRY_DSN}")
            list(APPEND _ISCC_SENTRY_FLAGS "/DSentryEnvironment=${MIB_SENTRY_ENVIRONMENT}")
        endif()

        # Custom target to build full installer
        add_custom_target(package_installer
            COMMAND ${CMAKE_COMMAND} -E echo "Building Windows installer..."
            COMMAND ${CMAKE_COMMAND} -E echo "Verifying Release build files..."
            COMMAND ${CMAKE_COMMAND} -E echo "Release directory: ${CMAKE_BINARY_DIR}/Release"
            # Verify main executables exist
            COMMAND ${CMAKE_COMMAND} -E echo "Checking for ${CMAKE_BINARY_DIR}/Release/mib_studio_qt.exe..."
            # Build installer
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling InnoSetup script..."
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" ${_ISCC_SENTRY_FLAGS} "/O${INSTALLER_OUTPUT_DIR}" "${INNOSETUP_SCRIPT}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Building Windows installer with InnoSetup"
            DEPENDS mib_studio_qt
        )

        # Custom target to build update package (app files only, no eGrabber/VC++)
        add_custom_target(package_installer_update
            COMMAND ${CMAKE_COMMAND} -E echo "Building Windows update package..."
            COMMAND ${CMAKE_COMMAND} -E echo "Verifying Release build files..."
            COMMAND ${CMAKE_COMMAND} -E echo "Release directory: ${CMAKE_BINARY_DIR}/Release"
            # Verify main executables exist
            COMMAND ${CMAKE_COMMAND} -E echo "Checking for ${CMAKE_BINARY_DIR}/Release/mib_studio_qt.exe..."
            # Build update package
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling InnoSetup update script..."
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" ${_ISCC_SENTRY_FLAGS} "/O${INSTALLER_OUTPUT_DIR}" "${INNOSETUP_UPDATE_SCRIPT}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Building Windows update package with InnoSetup"
            DEPENDS mib_studio_qt
        )
        
        message(STATUS "InnoSetup found: ${ISCC_EXE}")
        message(STATUS "Installer target 'package_installer' available (full installer)")
        message(STATUS "Installer target 'package_installer_update' available (update package)")
        message(STATUS "Run 'cmake --build build --target package_installer' to create full installer")
        message(STATUS "Run 'cmake --build build --target package_installer_update' to create update package")
    else()
        message(STATUS "InnoSetup not found - installer target will not be available")
        message(STATUS "Install InnoSetup from https://jrsoftware.org/isdl.php to enable installer generation")
    endif()
endif()
endif()
