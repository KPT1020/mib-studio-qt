cmake_minimum_required(VERSION 3.20)
project(MIB_Studio_Qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Suppress character encoding warnings from third-party headers
if(MSVC)
    add_compile_options(/wd4828)
endif()

find_package(Qt6 COMPONENTS Widgets Gui Charts REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG QUIET)
find_package(OpenCV CONFIG REQUIRED)

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

# Include directories (EGrabber headers on Windows install path)
include_directories("C:/Program Files/Euresys/eGrabber/include")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/Coremor)

# Backend library (services, APIs)
add_library(mib_backend STATIC
    src/backend/AppBackend.cpp
    src/backend/Tools.cpp
    src/backend/services/Logger.cpp
    src/backend/services/SqliteService.cpp
    src/backend/services/CameraControlService.cpp
    src/backend/services/Hdf5Service.cpp
    src/backend/services/CaptureService.cpp
    src/backend/services/RecorderService.cpp
    src/backend/services/ProcessingService.cpp
    src/backend/services/PlaybackService.cpp
    src/backend/services/AutofocusService.cpp
    src/backend/playback/FrameStore.cpp
    src/camera/common/EGrabberCamera.cpp
    src/camera/mock/MockCamera.cpp
)

target_include_directories(mib_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Coremor
    "C:/Program Files/Euresys/eGrabber/include"
)

# Coremor library
set(COREMOR_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/Coremor)
target_link_libraries(mib_backend PUBLIC
    spdlog::spdlog
    Qt6::Gui
    ${COREMOR_LIB_DIR}/XMT_DLL_SER.lib
)

if(TARGET opencv_world)
    target_link_libraries(mib_backend PUBLIC opencv_world)
elseif(TARGET opencv_core)
    target_link_libraries(mib_backend PUBLIC opencv_core opencv_imgproc opencv_imgcodecs)
elseif(DEFINED OpenCV_LIBS)
    target_include_directories(mib_backend PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mib_backend PUBLIC ${OpenCV_LIBS})
endif()

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mib_backend PUBLIC nlohmann_json::nlohmann_json)
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
    # Fallback: manually link HDF5 libraries from vcpkg
    find_library(HDF5_LIB hdf5 PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib" NO_DEFAULT_PATH)
    find_library(HDF5_HL_LIB hdf5_hl PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib" NO_DEFAULT_PATH)
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

# Frontend application (Qt Widgets)
set(FRONTEND_COMMON_SOURCES
    src/frontend/MainWindow.cpp
    include/frontend/MainWindow.h
    src/frontend/HdfReviewTab.cpp
    include/frontend/HdfReviewTab.h
    src/frontend/PlaybackPanel.cpp
    include/frontend/PlaybackPanel.h
    src/frontend/BufferSaveDialog.cpp
    include/frontend/BufferSaveDialog.h
    resources/defaults.qrc
    src/frontend/ConnectTab.cpp
    include/frontend/ConnectTab.h
    src/frontend/PreviewPage.cpp
    include/frontend/PreviewPage.h
    src/frontend/ConfigTabs.cpp
    include/frontend/ConfigTabs.h
    src/frontend/NanopositionerTab.cpp
    include/frontend/NanopositionerTab.h
    src/frontend/ProcessingSettingsDialog.cpp
    include/frontend/ProcessingSettingsDialog.h
    src/frontend/FrameViewerDialog.cpp
    include/frontend/FrameViewerDialog.h
    src/frontend/MockConfigDialog.cpp
    include/frontend/MockConfigDialog.h
    src/frontend/ExperimentMonitoringTab.cpp
    include/frontend/ExperimentMonitoringTab.h
    src/frontend/HdfMetricsModel.cpp
    include/frontend/HdfMetricsModel.h
    src/frontend/MonitoringSettingsDialog.cpp
    include/frontend/MonitoringSettingsDialog.h
    src/frontend/JsonTableModel.cpp
    include/frontend/JsonTableModel.h
    src/frontend/JsonFlatten.cpp
    include/frontend/JsonFlatten.h
    src/frontend/AppConfigWatcher.cpp
    include/frontend/AppConfigWatcher.h
)

add_executable(mib_studio_qt
    src/frontend/main.cpp
    ${FRONTEND_COMMON_SOURCES}
)

add_executable(mock_studio_qt
    src/frontend/mock_main.cpp
    ${FRONTEND_COMMON_SOURCES}
)

target_include_directories(mib_studio_qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "C:/Program Files/Euresys/eGrabber/include"
)

target_include_directories(mock_studio_qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "C:/Program Files/Euresys/eGrabber/include"
)

target_link_libraries(mib_studio_qt PRIVATE
    Qt6::Widgets
    Qt6::Charts
    mib_backend
    spdlog::spdlog
)

target_link_libraries(mock_studio_qt PRIVATE
    Qt6::Widgets
    Qt6::Charts
    mib_backend
    spdlog::spdlog
)

# OpenCV linking (prefer imported targets; fallback to variables)
if(TARGET opencv_world)
    target_link_libraries(mib_studio_qt PRIVATE opencv_world)
    target_link_libraries(mock_studio_qt PRIVATE opencv_world)
elseif(TARGET opencv_core)
    target_link_libraries(mib_studio_qt PRIVATE opencv_core opencv_imgproc opencv_imgcodecs)
    target_link_libraries(mock_studio_qt PRIVATE opencv_core opencv_imgproc opencv_imgcodecs)
elseif(DEFINED OpenCV_LIBS)
    target_include_directories(mib_studio_qt PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mib_studio_qt PRIVATE ${OpenCV_LIBS})
    target_include_directories(mock_studio_qt PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mock_studio_qt PRIVATE ${OpenCV_LIBS})
endif()

# Qt deployment via windeployqt (vcpkg)
if(WIN32)
    set(VCPKG_BIN_DIR ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin)
    set(WINDEPLOYQT ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/qt6/bin/windeployqt.exe)

    # Discover extra third-party codec runtimes used by Qt imageformats plugins (copy after windeployqt)
    # This helps ensure qjpeg/qtiff/qwebp/qjp2 load on machines without vcpkg in PATH.
    set(_EXTRA_CODEC_PATTERNS
        "${VCPKG_BIN_DIR}/jpeg*.dll"
        "${VCPKG_BIN_DIR}/libjpeg*.dll"
        "${VCPKG_BIN_DIR}/tiff*.dll"
        "${VCPKG_BIN_DIR}/libtiff*.dll"
        "${VCPKG_BIN_DIR}/liblzma*.dll"
        "${VCPKG_BIN_DIR}/libwebp*.dll"
        "${VCPKG_BIN_DIR}/libwebpdemux*.dll"
        "${VCPKG_BIN_DIR}/libwebpmux*.dll"
        "${VCPKG_BIN_DIR}/libsharpyuv*.dll"
        "${VCPKG_BIN_DIR}/openjp*.dll"      # openjp2 for qjp2
        "${VCPKG_BIN_DIR}/zstd*.dll"
        "${VCPKG_BIN_DIR}/libdeflate*.dll"
    )
    set(_EXTRA_CODEC_DLLS "")
    foreach(_pat IN LISTS _EXTRA_CODEC_PATTERNS)
        file(GLOB _found "${_pat}")
        list(APPEND _EXTRA_CODEC_DLLS ${_found})
    endforeach()
    list(REMOVE_DUPLICATES _EXTRA_CODEC_DLLS)

    foreach(app_target IN ITEMS mib_studio_qt mock_studio_qt)
        add_custom_command(TARGET ${app_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env "PATH=${VCPKG_BIN_DIR};$ENV{PATH}"
                    "${WINDEPLOYQT}" --release "$<TARGET_FILE:${app_target}>"
            COMMENT "windeployqt (Release): ${app_target}"
        )
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
    endforeach()
endif()

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mib_studio_qt PRIVATE nlohmann_json::nlohmann_json)
    target_link_libraries(mock_studio_qt PRIVATE nlohmann_json::nlohmann_json)
endif()

# Console test: capture + processing (CPU-only)
add_executable(capture_processing_test
    src/tests/capture_processing_test.cpp
)

target_include_directories(capture_processing_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "C:/Program Files/Euresys/eGrabber/include"
)

target_link_libraries(capture_processing_test PRIVATE
    mib_backend
    spdlog::spdlog
)

# Windows installer packaging (InnoSetup)
if(WIN32)
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
        
        # InnoSetup script path
        set(INNOSETUP_SCRIPT "${CMAKE_SOURCE_DIR}/resources/installers/mib-studio-qt.iss")
        
        # Custom target to build installer
        add_custom_target(package_installer
            COMMAND ${CMAKE_COMMAND} -E echo "Building Windows installer..."
            COMMAND ${CMAKE_COMMAND} -E echo "Verifying Release build files..."
            COMMAND ${CMAKE_COMMAND} -E echo "Release directory: ${CMAKE_BINARY_DIR}/Release"
            # Verify main executables exist
            COMMAND ${CMAKE_COMMAND} -E echo "Checking for ${CMAKE_BINARY_DIR}/Release/mib_studio_qt.exe..."
            COMMAND ${CMAKE_COMMAND} -E echo "Checking for ${CMAKE_BINARY_DIR}/Release/mock_studio_qt.exe..."
            # Build installer
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling InnoSetup script..."
            COMMAND "${ISCC_EXE}" "${INNOSETUP_SCRIPT}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Building Windows installer with InnoSetup"
            DEPENDS mib_studio_qt mock_studio_qt
        )
        
        message(STATUS "InnoSetup found: ${ISCC_EXE}")
        message(STATUS "Installer target 'package_installer' available")
        message(STATUS "Run 'cmake --build build --target package_installer' to create installer")
    else()
        message(STATUS "InnoSetup not found - installer target will not be available")
        message(STATUS "Install InnoSetup from https://jrsoftware.org/isdl.php to enable installer generation")
    endif()
endif()
