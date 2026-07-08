include(MIBLinkHelpers)
include(MIBWindowsDeployment)

set(FRONTEND_COMMON_SOURCES
    # Core
    ${PROJECT_SOURCE_DIR}/src/frontend/qt/BackgroundFrameQtAdapter.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/qt/BackgroundFrameQtAdapter.h
    ${PROJECT_SOURCE_DIR}/src/frontend/core/MainWindow.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/core/MainWindow.h
    ${PROJECT_SOURCE_DIR}/resources/ui/MainWindow.ui
    # Tabs
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/ConnectTab.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/ConnectTab.h
    ${PROJECT_SOURCE_DIR}/resources/ui/ConnectTab.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/ConfigTabs.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/ConfigTabs.h
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/ExperimentMonitoringTab.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/ExperimentMonitoringTab.h
    ${PROJECT_SOURCE_DIR}/src/frontend/widgets/ZoomableChartView.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/widgets/ZoomableChartView.h
    ${PROJECT_SOURCE_DIR}/resources/ui/ExperimentMonitoringTab.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/HdfReviewTab.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/HdfReviewTab.h
    ${PROJECT_SOURCE_DIR}/resources/ui/HdfReviewTab.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/NanopositionerTab.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/NanopositionerTab.h
    ${PROJECT_SOURCE_DIR}/resources/ui/NanopositionerTab.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/SyringePumpTab.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/SyringePumpTab.h
    ${PROJECT_SOURCE_DIR}/resources/ui/SyringePumpTab.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/OverviewTab.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/OverviewTab.h
    ${PROJECT_SOURCE_DIR}/resources/ui/OverviewTab.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/tabs/PreviewPage.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/tabs/PreviewPage.h
    ${PROJECT_SOURCE_DIR}/resources/ui/PreviewPage.ui
    # Dialogs
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/BatchMaskDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/BatchMaskDialog.h
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/BufferSaveDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/BufferSaveDialog.h
    ${PROJECT_SOURCE_DIR}/resources/ui/BufferSaveDialog.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/ConversionFactorDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/ConversionFactorDialog.h
    ${PROJECT_SOURCE_DIR}/resources/ui/ConversionFactorDialog.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/FrameViewerDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/FrameViewerDialog.h
    ${PROJECT_SOURCE_DIR}/resources/ui/FrameViewerDialog.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/MockConfigDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/MockConfigDialog.h
    ${PROJECT_SOURCE_DIR}/resources/ui/MockConfigDialog.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/MonitoringSettingsDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/MonitoringSettingsDialog.h
    ${PROJECT_SOURCE_DIR}/resources/ui/MonitoringSettingsDialog.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/ProcessingSettingsDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/ProcessingSettingsDialog.h
    ${PROJECT_SOURCE_DIR}/resources/ui/ProcessingSettingsDialog.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/SyringePumpSettingsDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/SyringePumpSettingsDialog.h
    ${PROJECT_SOURCE_DIR}/resources/ui/SyringePumpSettingsDialog.ui
    ${PROJECT_SOURCE_DIR}/src/frontend/dialogs/SoftwareUpdatesDialog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/dialogs/SoftwareUpdatesDialog.h
    # Controllers
    ${PROJECT_SOURCE_DIR}/src/frontend/controllers/CameraController.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/controllers/CameraController.h
    ${PROJECT_SOURCE_DIR}/src/frontend/controllers/ExperimentController.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/controllers/ExperimentController.h
    # Utils
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/ConfigPathManager.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/ConfigPathManager.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/OverlayRenderer.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/OverlayRenderer.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/EgrabberConfigParser.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/EgrabberConfigParser.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/FileIOUtils.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/FileIOUtils.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/JsonFlatten.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/JsonFlatten.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/JsonConfigMerge.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/JsonConfigMerge.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/ProfileConfigReview.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/ProfileConfigReview.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/UpdateCatalog.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/UpdateCatalog.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/HdfReviewExportPaths.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/HdfReviewExportPaths.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/RoiManager.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/RoiManager.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/RoiDrawCanvas.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/RoiDrawCanvas.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/SimpleImageCanvas.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/SimpleImageCanvas.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/StatsDisplayManager.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/StatsDisplayManager.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/StatisticsPanel.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/StatisticsPanel.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/BackgroundPreviewWidget.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/BackgroundPreviewWidget.h
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/SidebarWidget.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/utils/SidebarWidget.h
    # Models
    ${PROJECT_SOURCE_DIR}/src/frontend/models/HdfMetricsModel.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/models/HdfMetricsModel.h
    ${PROJECT_SOURCE_DIR}/src/frontend/models/JsonTableModel.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/models/JsonTableModel.h
    # System
    ${PROJECT_SOURCE_DIR}/src/frontend/system/AppConfigWatcher.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/system/AppConfigWatcher.h
    ${PROJECT_SOURCE_DIR}/src/frontend/system/AutoUpdater.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/system/AutoUpdater.h
    ${PROJECT_SOURCE_DIR}/src/frontend/system/DeviceInitManager.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/system/DeviceInitManager.h
    ${PROJECT_SOURCE_DIR}/src/frontend/system/ProfileManager.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/system/ProfileManager.h
    ${PROJECT_SOURCE_DIR}/src/frontend/system/PlaybackPanel.cpp
    ${PROJECT_SOURCE_DIR}/include/frontend/system/PlaybackPanel.h
    # Resources
    ${PROJECT_SOURCE_DIR}/resources/defaults.qrc
)

set(MIB_STUDIO_QT_EXTRA_SOURCES)
if(WIN32)
    list(APPEND MIB_STUDIO_QT_EXTRA_SOURCES ${PROJECT_SOURCE_DIR}/resources/app_icon.rc)
    add_executable(mib_studio_qt WIN32
        ${PROJECT_SOURCE_DIR}/src/frontend/core/main.cpp
        ${FRONTEND_COMMON_SOURCES}
        ${MIB_STUDIO_QT_EXTRA_SOURCES}
    )
else()
    add_executable(mib_studio_qt
        ${PROJECT_SOURCE_DIR}/src/frontend/core/main.cpp
        ${FRONTEND_COMMON_SOURCES}
        ${MIB_STUDIO_QT_EXTRA_SOURCES}
    )
endif()

set_target_properties(mib_studio_qt PROPERTIES
    AUTOUIC_SEARCH_PATHS "${PROJECT_SOURCE_DIR}/resources/ui"
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
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

# App version exported from CMake (used for update checks, About dialogs, etc.).
# _FULL retains any pre-release suffix (e.g. 1.0.4-beta.1) so the app knows its
# channel; the plain one stays numeric for anything needing strict X.Y.Z.
target_compile_definitions(mib_studio_qt PRIVATE
    MIB_STUDIO_QT_VERSION="${PROJECT_VERSION}"
    MIB_STUDIO_QT_VERSION_FULL="${PROJECT_VERSION_FULL}"
)

mib_link_opencv(mib_studio_qt PRIVATE)
mib_configure_windows_deployment(mib_studio_qt)

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
