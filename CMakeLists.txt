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

find_package(Qt6 COMPONENTS Widgets Gui REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG QUIET)

# Prefer official SQLite3 package; fallback to unofficial config if needed
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_package(unofficial-sqlite3 CONFIG QUIET)
endif()

# HDF5 via config mode (vcpkg provides imported targets)
find_package(HDF5 CONFIG QUIET)

# Include directories (EGrabber headers on Windows install path)
include_directories("C:/Program Files/Euresys/eGrabber/include")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Backend library (services, APIs)
add_library(mib_backend STATIC
    src/backend/AppBackend.cpp
    src/backend/Tools.cpp
    src/backend/services/Logger.cpp
    src/backend/services/SqliteService.cpp
    src/backend/services/Hdf5Service.cpp
    src/backend/services/CaptureService.cpp
    src/backend/services/RecorderService.cpp
    src/backend/services/ProcessingService.cpp
    src/backend/services/PlaybackService.cpp
    src/backend/playback/FrameStore.cpp
    src/camera/common/EGrabberCamera.cpp
    src/camera/mock/MockCamera.cpp
)

target_include_directories(mib_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "C:/Program Files/Euresys/eGrabber/include"
)

target_link_libraries(mib_backend PUBLIC
    spdlog::spdlog
    Qt6::Gui
)

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

# HDF5 linking (prefer C++ component if available)
if(TARGET hdf5::hdf5_cpp-shared)
    target_link_libraries(mib_backend PUBLIC hdf5::hdf5_cpp-shared)
elseif(TARGET hdf5::hdf5-cpp)
    target_link_libraries(mib_backend PUBLIC hdf5::hdf5-cpp)
elseif(TARGET HDF5::HDF5)
    target_link_libraries(mib_backend PUBLIC HDF5::HDF5)
endif()

# Frontend application (Qt Widgets)
set(FRONTEND_COMMON_SOURCES
    src/frontend/MainWindow.cpp
    include/frontend/MainWindow.h
    src/frontend/PlaybackPanel.cpp
    include/frontend/PlaybackPanel.h
)

add_executable(mib_studio_qt
    src/frontend/main.cpp
    ${FRONTEND_COMMON_SOURCES}
)

add_executable(mock_studio_qt
    src/frontend/mock_main.cpp
    src/frontend/MockConfigDialog.cpp
    include/frontend/MockConfigDialog.h
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
    mib_backend
    spdlog::spdlog
)

target_link_libraries(mock_studio_qt PRIVATE
    Qt6::Widgets
    mib_backend
    spdlog::spdlog
)

# Qt deployment: copy platform plugins to build directory
if(WIN32)
    # Use the correct vcpkg installed path for Qt plugins
    set(VCPKG_BIN_DIR ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin)
    set(QT_PLATFORMS_PATH ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins/platforms)
    set(QT_IMAGEFORMATS_PATH ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins/imageformats)
    
    # Add custom target to deploy Qt plugins
    foreach(app_target IN ITEMS mib_studio_qt mock_studio_qt)
        add_custom_command(TARGET ${app_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${app_target}>/platforms
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${app_target}>/imageformats
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${QT_PLATFORMS_PATH}/qwindows.dll
                $<TARGET_FILE_DIR:${app_target}>/platforms/
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${QT_IMAGEFORMATS_PATH}
                $<TARGET_FILE_DIR:${app_target}>/imageformats
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/jpeg62.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/tiff.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/liblzma.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/jasper.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/libwebp.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/libwebpdemux.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/libwebpmux.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_BIN_DIR}/libsharpyuv.dll $<TARGET_FILE_DIR:${app_target}>/
            COMMENT "Deploying Qt plugins (platforms/imageformats) and codec runtime deps (${app_target})"
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
