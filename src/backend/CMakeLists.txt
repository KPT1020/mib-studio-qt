include(MIBLinkHelpers)

add_library(mib_backend STATIC
    ${PROJECT_SOURCE_DIR}/src/backend/AppBackend.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/BackgroundCaptureNotifier.cpp
    ${PROJECT_SOURCE_DIR}/include/backend/BackgroundCaptureNotifier.h
    ${PROJECT_SOURCE_DIR}/src/backend/Tools.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/EModulusLut.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/Logger.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/CrashReporter.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/diagnostics/CrashStateMirror.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/SqliteService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/CameraControlService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/Hdf5Service.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/CaptureService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/RecorderService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/ProcessingService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/BatchMaskSources.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/PlaybackService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/SyringePumpService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/services/TriggerService.cpp
    ${PROJECT_SOURCE_DIR}/src/backend/playback/FrameStore.cpp
    ${PROJECT_SOURCE_DIR}/src/camera/common/EGrabberCamera.cpp
    ${PROJECT_SOURCE_DIR}/src/camera/mock/MockCamera.cpp
)

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
