cmake_minimum_required(VERSION 3.21)

# Default version (fallback if git is not available or no tags exist)
set(DEFAULT_VERSION "0.2.1")

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

find_package(Qt6 COMPONENTS Core Widgets Gui Charts Network REQUIRED)
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
    src/frontend/OverviewTab.cpp
    include/frontend/OverviewTab.h
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
    src/frontend/ConversionFactorDialog.cpp
    include/frontend/ConversionFactorDialog.h
    src/frontend/JsonTableModel.cpp
    include/frontend/JsonTableModel.h
    src/frontend/JsonFlatten.cpp
    include/frontend/JsonFlatten.h
    src/frontend/AppConfigWatcher.cpp
    include/frontend/AppConfigWatcher.h
    src/frontend/AutoUpdater.cpp
    include/frontend/AutoUpdater.h
)

add_executable(mib_studio_qt WIN32
    src/frontend/main.cpp
    ${FRONTEND_COMMON_SOURCES}
    resources/app_icon.rc
)

target_include_directories(mib_studio_qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "C:/Program Files/Euresys/eGrabber/include"
)

target_link_libraries(mib_studio_qt PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::Network
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
    target_link_libraries(mib_studio_qt PRIVATE opencv_core opencv_imgproc opencv_imgcodecs)
elseif(DEFINED OpenCV_LIBS)
    target_include_directories(mib_studio_qt PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mib_studio_qt PRIVATE ${OpenCV_LIBS})
endif()

# Qt deployment via windeployqt (Conan)
if(WIN32)
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
    
    # Find windeployqt
    find_program(WINDEPLOYQT_RELEASE windeployqt.exe
        PATHS "${CONAN_QT_TOOLS_DIR}" "${CONAN_QT_BIN_DIR}"
        NO_DEFAULT_PATH
    )
    find_program(WINDEPLOYQT_DEBUG windeployqt.debug.bat
        PATHS "${CONAN_QT_TOOLS_DIR}" "${CONAN_QT_BIN_DIR}"
        NO_DEFAULT_PATH
    )
    
    # If not found in tools, try bin directory
    if(NOT WINDEPLOYQT_RELEASE)
        find_program(WINDEPLOYQT_RELEASE windeployqt.exe
            PATHS "${CONAN_QT_BIN_DIR}" "${CMAKE_PREFIX_PATH}/bin"
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
            
            # Check Debug package location using imported target
            if(TARGET Qt6::Core)
                get_target_property(_QT_CORE_LOCATION_DEBUG Qt6::Core IMPORTED_LOCATION_DEBUG)
                if(_QT_CORE_LOCATION_DEBUG)
                    get_filename_component(_QT_DEBUG_BIN_DIR "${_QT_CORE_LOCATION_DEBUG}" DIRECTORY)
                    if(EXISTS "${_QT_DEBUG_BIN_DIR}/Qt6Cored.dll")
                        set(_QT_DEBUG_DLL_FOUND TRUE)
                    endif()
                else()
                    # Fallback: derive bin dir from debug import library
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
                
                # Check Release package location using imported target
                get_target_property(_QT_CORE_LOCATION_RELEASE Qt6::Core IMPORTED_LOCATION_RELEASE)
                if(_QT_CORE_LOCATION_RELEASE)
                    get_filename_component(_QT_RELEASE_BIN_DIR "${_QT_CORE_LOCATION_RELEASE}" DIRECTORY)
                    if(EXISTS "${_QT_RELEASE_BIN_DIR}/Qt6Core.dll")
                        set(_QT_RELEASE_DLL_FOUND TRUE)
                    endif()
                else()
                    # Fallback: derive bin dir from release import library
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

            # Last resort: search Conan cache for Qt6 debug/release DLLs
            if(NOT _QT_DEBUG_BIN_DIR OR NOT _QT_RELEASE_BIN_DIR)
                set(_CONAN_CACHE_ROOT "$ENV{USERPROFILE}/.conan2")
                if(EXISTS "${_CONAN_CACHE_ROOT}")
                    if(NOT _QT_DEBUG_BIN_DIR)
                        file(GLOB _qt_debug_dll_candidates "${_CONAN_CACHE_ROOT}/p/b/*/p/bin/Qt6Cored.dll")
                        list(LENGTH _qt_debug_dll_candidates _qt_debug_count)
                        if(_qt_debug_count GREATER 0)
                            list(GET _qt_debug_dll_candidates 0 _qt_debug_dll)
                            get_filename_component(_QT_DEBUG_BIN_DIR "${_qt_debug_dll}" DIRECTORY)
                            set(_QT_DEBUG_DLL_FOUND TRUE)
                        endif()
                    endif()
                    if(NOT _QT_RELEASE_BIN_DIR)
                        file(GLOB _qt_release_dll_candidates "${_CONAN_CACHE_ROOT}/p/b/*/p/bin/Qt6Core.dll")
                        list(LENGTH _qt_release_dll_candidates _qt_release_count)
                        if(_qt_release_count GREATER 0)
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
    endforeach()
endif()

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mib_studio_qt PRIVATE nlohmann_json::nlohmann_json)
endif()

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
        
        # InnoSetup script paths
        set(INNOSETUP_SCRIPT "${CMAKE_SOURCE_DIR}/resources/installers/mib-studio-qt.iss")
        set(INNOSETUP_UPDATE_SCRIPT "${CMAKE_SOURCE_DIR}/resources/installers/mib-studio-qt-update.iss")
        
        # Custom target to build full installer
        add_custom_target(package_installer
            COMMAND ${CMAKE_COMMAND} -E echo "Building Windows installer..."
            COMMAND ${CMAKE_COMMAND} -E echo "Verifying Release build files..."
            COMMAND ${CMAKE_COMMAND} -E echo "Release directory: ${CMAKE_BINARY_DIR}/Release"
            # Verify main executables exist
            COMMAND ${CMAKE_COMMAND} -E echo "Checking for ${CMAKE_BINARY_DIR}/Release/mib_studio_qt.exe..."
            # Build installer
            COMMAND ${CMAKE_COMMAND} -E echo "Compiling InnoSetup script..."
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" "${INNOSETUP_SCRIPT}"
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
            COMMAND "${ISCC_EXE}" "/DAppVersion=${PROJECT_VERSION}" "${INNOSETUP_UPDATE_SCRIPT}"
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
