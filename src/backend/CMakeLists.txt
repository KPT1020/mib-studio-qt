add_library(mib_backend STATIC
    AppBackend.cpp
    Tools.cpp
    EModulusLut.cpp
    services/Logger.cpp
    services/CrashReporter.cpp
    diagnostics/CrashStateMirror.cpp
    services/SqliteService.cpp
    services/CameraControlService.cpp
    services/Hdf5Service.cpp
    services/CaptureService.cpp
    services/RecorderService.cpp
    services/ProcessingService.cpp
    services/BatchMaskSources.cpp
    services/PlaybackService.cpp
    services/SyringePumpService.cpp
    services/TriggerService.cpp
    playback/FrameStore.cpp
    camera/common/EGrabberCamera.cpp
    camera/mock/MockCamera.cpp
)

if(MIB_HAS_EGRABBER)
    target_sources(mib_backend PRIVATE services/AutofocusService.cpp)
else()
    target_sources(mib_backend PRIVATE services/AutofocusService.stub.cpp)
endif()

if(MIB_HAS_ONNXRUNTIME)
    target_sources(mib_backend PRIVATE services/YoloService.cpp)
else()
    target_sources(mib_backend PRIVATE services/YoloService.stub.cpp)
endif()

set_target_properties(mib_backend PROPERTIES AUTOMOC ON)

target_compile_definitions(mib_backend PUBLIC
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
    MIB_HAS_EGRABBER=$<IF:$<BOOL:${MIB_HAS_EGRABBER}>,1,0>
    MIB_HAS_ONNXRUNTIME=$<IF:$<BOOL:${MIB_HAS_ONNXRUNTIME}>,1,0>
)

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

if(WIN32)
    target_link_libraries(mib_backend PUBLIC dbghelp)
endif()

if(MIB_SENTRY_AVAILABLE)
    target_link_libraries(mib_backend PUBLIC sentry::sentry)
    target_compile_definitions(mib_backend PUBLIC MIB_USE_SENTRY=1)
else()
    target_compile_definitions(mib_backend PUBLIC MIB_USE_SENTRY=0)
endif()

if(MIB_HAS_EGRABBER)
    target_link_libraries(mib_backend PUBLIC
        ${PROJECT_SOURCE_DIR}/include/Coremor/XMT_DLL_SER.lib
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

if(TARGET onnxruntime::onnxruntime)
    target_link_libraries(mib_backend PUBLIC onnxruntime::onnxruntime)
else()
    message(WARNING "onnxruntime::onnxruntime target not found - building with YoloService stub")
endif()

if(SQLite3_FOUND)
    target_link_libraries(mib_backend PUBLIC SQLite::SQLite3)
elseif(TARGET unofficial::sqlite3::sqlite3)
    target_link_libraries(mib_backend PUBLIC unofficial::sqlite3::sqlite3)
elseif(TARGET sqlite3)
    target_link_libraries(mib_backend PUBLIC sqlite3)
endif()

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
