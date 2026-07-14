include(MIBLinkHelpers)

# The Python wheel version is the processing-core release identity. Keep a
# configure-time fallback so source archives without bindings still build;
# the consistency test/bump tool guards the literals in release branches.
set(MIB_PROCESSING_CORE_VERSION "")
if(EXISTS "${PROJECT_SOURCE_DIR}/bindings/python/pyproject.toml")
    file(READ "${PROJECT_SOURCE_DIR}/bindings/python/pyproject.toml" _mib_processing_pyproject)
    string(REGEX MATCH "version[ \t]*=[ \t]*\"([^\"]+)\"" _mib_processing_version_match
                 "${_mib_processing_pyproject}")
    set(MIB_PROCESSING_CORE_VERSION "${CMAKE_MATCH_1}")
endif()
if(MIB_PROCESSING_CORE_VERSION STREQUAL "")
    set(MIB_PROCESSING_CORE_VERSION "0.1.0")
endif()

# mib_processing: the Qt-free deformability-cytometry processing core plus
# its I/O adapters (HDF5 read/write, frame buffering). Depends only on
# OpenCV, HDF5, spdlog, and the standard library -- no Qt, no camera SDKs,
# no Sentry/CrashReporter -- so it can be built and consumed (e.g. via
# Python bindings) without a Qt toolchain. See
# docs/gold_standard_metrics.md ("Portable Processing Contract") and
# knowledge_map/services/ProcessingService.md. mib_backend links this
# target rather than compiling these sources itself, so the desktop app and
# any portable consumer share one implementation.
add_library(mib_processing STATIC
    ${PROJECT_SOURCE_DIR}/src/backend/processing/EModulusLut.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/processing/BundledProcessingKernel.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/processing/ProcessingCoreLoader.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/processing/ProcessingCoreCache.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/processing/ProcessingCoreSha256.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/processing/ProcessingService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/processing/BatchMaskSources.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/recording/Hdf5Service.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/diagnostics/CrashStateMirror.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/playback/FrameStore.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/app/Tools.cpp
)

# No Q_OBJECT usage in this target, and it must not require the Qt moc
# toolchain to build standalone (e.g. for a Qt-less Python wheel build --
# see the Biowork portability epic #220). POSITION_INDEPENDENT_CODE is
# required because bindings/python/CMakeLists.txt links this static library
# into a shared pybind11 extension module (.so); a non-PIC static archive
# cannot be linked into a shared object.
set_target_properties(mib_processing PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(mib_processing PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(mib_processing PUBLIC
    spdlog::spdlog
    ${CMAKE_DL_LIBS}
)

target_compile_definitions(mib_processing PUBLIC
    MIB_PROCESSING_CORE_VERSION="${MIB_PROCESSING_CORE_VERSION}"
)
if(WIN32)
    target_link_libraries(mib_processing PUBLIC wintrust crypt32)
endif()

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mib_processing PUBLIC nlohmann_json::nlohmann_json)
endif()

mib_link_opencv(mib_processing PUBLIC)
mib_link_hdf5(mib_processing PUBLIC)

# A wheel build needs only the portable library above. Stopping here keeps Qt,
# camera services, desktop backends, and the native Windows module out of the
# manylinux configuration and makes its dependency surface auditable.
if(MIB_BUILD_PROCESSING_ONLY)
    return()
endif()

# Native hot-swap artifact. It deliberately links only the mask kernel and
# OpenCV/spdlog, not Qt/HDF5 or the desktop service graph. Release CI signs the
# Windows DLL before the registry publisher hashes it.
add_library(mib_processing_core MODULE
    ${PROJECT_SOURCE_DIR}/src/backend/processing/BundledProcessingKernel.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/processing/ProcessingCorePlugin.cpp
)
target_include_directories(mib_processing_core PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(mib_processing_core PRIVATE spdlog::spdlog)
mib_link_opencv(mib_processing_core PRIVATE)
target_compile_definitions(mib_processing_core PRIVATE
    MIB_PROCESSING_CORE_PLUGIN_EXPORTS=1
    MIB_PROCESSING_CORE_VERSION="${MIB_PROCESSING_CORE_VERSION}"
)
set_target_properties(mib_processing_core PROPERTIES
    PREFIX ""
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    POSITION_INDEPENDENT_CODE ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>"
)
if(WIN32)
    set(_mib_processing_native_basename
        "mib_processing_core-${MIB_PROCESSING_CORE_VERSION}-windows_x86_64")
    set_target_properties(mib_processing_core PROPERTIES
        OUTPUT_NAME "${_mib_processing_native_basename}")
    if(MSVC)
        set(_mib_processing_runtime_fingerprint
            "windows-x86_64-msvc${MSVC_VERSION}-md-cxx17")
    else()
        set(_mib_processing_runtime_fingerprint "windows-x86_64-unknown-cxx17")
    endif()
    file(GENERATE
        OUTPUT "${PROJECT_BINARY_DIR}/$<CONFIG>/${_mib_processing_native_basename}.json"
        CONTENT "{\n  \"schema_version\": 1,\n  \"version\": \"${MIB_PROCESSING_CORE_VERSION}\",\n  \"filename\": \"$<TARGET_FILE_NAME:mib_processing_core>\",\n  \"os\": \"windows\",\n  \"arch\": \"x86_64\",\n  \"engine_abi_version\": 1,\n  \"contract_version\": 1,\n  \"entrypoint\": \"mib_processing_get_api\",\n  \"runtime_fingerprint\": \"${_mib_processing_runtime_fingerprint}\",\n  \"app_min_version\": \"${MIB_PROCESSING_CORE_APP_MIN_VERSION}\",\n  \"app_max_version\": \"${MIB_PROCESSING_CORE_APP_MAX_VERSION}\",\n  \"signing\": {\"required\": true, \"format\": \"authenticode\"}\n}\n")
endif()

add_library(mib_backend STATIC
    ${PROJECT_SOURCE_DIR}/src/backend/app/AppBackend.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/app/BackendFacade.cpp
    ${PROJECT_SOURCE_DIR}/include/backend/app/BackendFacade.h
    ${PROJECT_SOURCE_DIR}/include/backend/app/BackgroundFrame.h
    ${PROJECT_SOURCE_DIR}/src/backend/processing/EModulusLutCatalog.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/Logger.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/CrashReporter.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/database/SqliteService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/CameraControlService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/CaptureService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/recording/RecorderService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/playback/PlaybackService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/SyringePumpService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/TriggerService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/camera/egrabber/EGrabberCamera.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/camera/mindvision/MindVisionCamera.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/camera/mock/MockCamera.cpp
)

target_link_libraries(mib_backend PUBLIC mib_processing)

if(MIB_HAS_EGRABBER)
    target_sources(mib_backend PRIVATE
        ${PROJECT_SOURCE_DIR}/src/backend/services/AutofocusService.cpp
    )
else()
    target_sources(mib_backend PRIVATE
        ${PROJECT_SOURCE_DIR}/src/backend/services/AutofocusService.stub.cpp
    )
endif()

if(MIB_HAS_ONNXRUNTIME)
    target_sources(mib_backend PRIVATE
        ${PROJECT_SOURCE_DIR}/src/backend/services/YoloService.cpp
    )
else()
    target_sources(mib_backend PRIVATE
        ${PROJECT_SOURCE_DIR}/src/backend/services/YoloService.stub.cpp
    )
endif()

# Explicitly enable AUTOMOC for this target to ensure MOC files are generated.
set_target_properties(mib_backend PROPERTIES
    AUTOMOC ON
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

target_compile_definitions(mib_backend PUBLIC
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
)

if(MIB_HAS_EGRABBER)
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_EGRABBER=1)
else()
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_EGRABBER=0)
endif()

if(MIB_HAS_MINDVISION)
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_MINDVISION=1)
else()
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_MINDVISION=0)
endif()

if(MIB_HAS_ONNXRUNTIME)
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_ONNXRUNTIME=1)
else()
    target_compile_definitions(mib_backend PUBLIC MIB_HAS_ONNXRUNTIME=0)
endif()

target_include_directories(mib_backend PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
if(MIB_HAS_EGRABBER)
    target_include_directories(mib_backend PUBLIC
        ${PROJECT_SOURCE_DIR}/include/Coremor
        "C:/Program Files/Euresys/eGrabber/include"
    )
endif()
if(MIB_HAS_MINDVISION)
    target_include_directories(mib_backend PUBLIC
        ${MIB_MINDVISION_INCLUDE_DIR}
    )
endif()

target_link_libraries(mib_backend PUBLIC
    spdlog::spdlog
    Qt6::Core
    Qt6::Gui
    Qt6::Network
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
    set(COREMOR_LIB_DIR ${PROJECT_SOURCE_DIR}/include/Coremor)
    target_link_libraries(mib_backend PUBLIC
        ${COREMOR_LIB_DIR}/XMT_DLL_SER.lib
    )
endif()

mib_link_opencv(mib_backend PUBLIC)
mib_link_sqlite(mib_backend PUBLIC)
mib_link_hdf5(mib_backend PUBLIC)

# ONNX Runtime linking.
if(TARGET onnxruntime::onnxruntime)
    target_link_libraries(mib_backend PUBLIC onnxruntime::onnxruntime)
else()
    message(WARNING "onnxruntime::onnxruntime target not found - building with YoloService stub")
endif()
