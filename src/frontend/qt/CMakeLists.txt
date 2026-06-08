file(GLOB_RECURSE MIB_FRONTEND_CPP CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/frontend/*.cpp"
)
file(GLOB_RECURSE MIB_FRONTEND_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/frontend/*.h"
)
file(GLOB MIB_FRONTEND_UI CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/resources/ui/*.ui"
)

set(MIB_STUDIO_QT_SOURCES
    ${MIB_FRONTEND_CPP}
    ${MIB_FRONTEND_HEADERS}
    ${MIB_FRONTEND_UI}
    "${PROJECT_SOURCE_DIR}/resources/defaults.qrc"
)

if(WIN32)
    list(APPEND MIB_STUDIO_QT_SOURCES "${PROJECT_SOURCE_DIR}/resources/app_icon.rc")
    add_executable(mib_studio_qt WIN32 ${MIB_STUDIO_QT_SOURCES})
else()
    add_executable(mib_studio_qt ${MIB_STUDIO_QT_SOURCES})
endif()

set_target_properties(mib_studio_qt PROPERTIES
    AUTOUIC_SEARCH_PATHS "${PROJECT_SOURCE_DIR}/resources/ui"
)

target_include_directories(mib_studio_qt PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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

target_compile_definitions(mib_studio_qt PRIVATE
    MIB_STUDIO_QT_VERSION="${PROJECT_VERSION}"
)

if(TARGET opencv_world)
    target_link_libraries(mib_studio_qt PRIVATE opencv_world)
elseif(TARGET opencv_core)
    target_link_libraries(mib_studio_qt PRIVATE opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio)
elseif(DEFINED OpenCV_LIBS)
    target_include_directories(mib_studio_qt PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(mib_studio_qt PRIVATE ${OpenCV_LIBS})
endif()

include(${PROJECT_SOURCE_DIR}/cmake/packaging.cmake)
