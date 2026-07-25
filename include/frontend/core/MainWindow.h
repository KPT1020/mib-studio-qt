#pragma once

#include <QMainWindow>

#include <string>

namespace backend::services::checks { struct PreflightFacts; }

class QLabel;
class QTimer;
class PlaybackPanel;
class QTabWidget;
class QSplitter;
class QSpinBox;
class QAction;
class QPushButton;
class QCloseEvent;
template<typename T> class QFutureWatcher;

namespace backend { class AppBackend; }
namespace frontend { class OverviewTab; }
namespace frontend { class ConnectTab; }
namespace frontend { class AutoUpdater; }
namespace frontend { class SidebarWidget; }
namespace frontend { class DeviceInitManager; }
namespace frontend { class WorkflowStageBar; }
namespace frontend { class HdfReviewTab; }
namespace frontend { class ChecklistPanel; }
namespace frontend { class ConfigTabs; }
namespace frontend { class ContextBar; }
namespace frontend { class RunDashboardStrip; }
namespace frontend { class ExperimentMonitoringTab; }
namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~MainWindow();

    // UX-9: Service/Commissioning mode. Operator mode is the default every
    // session; entering interactively requires confirmation, an active
    // experiment forces Operator mode, and the banner stays visible while
    // enabled. Q_INVOKABLE so the screenshot tour can drive the state.
    Q_INVOKABLE void setCommissioningMode(bool enabled, bool confirmed);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStartCapture();
    void onStopCapture();
    void onStartExperiment();
    void onStopExperiment();
    void onUpdateStats();
    void onTabChanged(int index);
    void onNoCamerasFound();

private:
    void updateExperimentButtonStates();
    void updateTabStates();
    // Collect authoritative backend facts and re-render the workflow stage
    // bar (UX-1). Called on the workflow timer and after state-changing slots.
    void refreshWorkflowState();
    // Probe the experiment data folder (writability + free space) at a low
    // cadence; feeds the preflight checklist and workflow facts (UX-3).
    void probeStorage();
    void handlePreflightRecovery(const QString& checkId);
    // Shared fact collection for the preflight checklist and readiness gate.
    backend::services::checks::PreflightFacts collectPreflightFacts() const;
    void handleContextSegment(const QString& segmentId);
    void startExperimentServices();
    void stopExperimentServices();
    void setupCornerWidgets();
    void setupSidebar();
    
    Ui::MainWindow* ui;
    backend::AppBackend& backend_;
    QLabel* statusLabel_ = nullptr;
    QLabel* processingCoreLabel_ = nullptr;
    QTimer* statsTimer_ = nullptr;
    PlaybackPanel* playbackPanel_ = nullptr;
    QTabWidget* experimentTabs_ = nullptr;
    frontend::ConnectTab* connectTab_ = nullptr;
    frontend::OverviewTab* overviewTab_ = nullptr;
    frontend::HdfReviewTab* hdfReviewTab_ = nullptr;
    frontend::WorkflowStageBar* workflowBar_ = nullptr;
    frontend::ChecklistPanel* preflightPanel_ = nullptr;
    frontend::ChecklistPanel* alignmentPanel_ = nullptr;
    frontend::ContextBar* contextBar_ = nullptr;
    frontend::RunDashboardStrip* dashboardStrip_ = nullptr;
    frontend::ConfigTabs* configTabs_ = nullptr;
    frontend::ExperimentMonitoringTab* monitoringTab_ = nullptr;
    QWidget* commissioningBanner_ = nullptr;
    QAction* commissioningAct_ = nullptr;
    bool commissioningMode_{false};
    QTimer* workflowTimer_ = nullptr;
    QAction* processingCoreAct_ = nullptr;
    frontend::AutoUpdater* updater_ = nullptr;
    frontend::SidebarWidget* sidebarWidget_ = nullptr;
    frontend::DeviceInitManager* initManager_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    uint64_t experimentStartTimeNs_{0};
    bool experimentActive_{false};
    bool experimentServicesActive_{false};
    bool noCamerasFound_{false};
    bool experimentCompleted_{false};
    bool lastExperimentSaveOk_{true};
    // Cached Experiment Profile status (UX-2), refreshed via
    // ConfigTabs::profileStatusChanged — never re-scanned on the UI timer.
    bool profileSelected_{false};
    bool profileDirty_{false};
    bool profileIncompatible_{false};
    QString profileName_;
    // Apply & Verify transaction state (UX-5), invalidated on profile
    // change, config change, or camera reconnect.
    bool profileApplied_{false};
    bool profileVerified_{false};
    // Readiness/override provenance captured at start, written to the HDF5
    // file at stop (UX-6).
    std::string pendingReadinessJson_;
    // Cached storage probe (UX-3)
    bool storageKnown_{false};
    bool storageWritable_{true};
    double storageFreeGb_{0.0};
    int storageProbeTick_{0};
    int lastRecommendedStage_{0};
    bool flushInProgress_{false};
    bool restoreRealtimeModeAfterExperiment_{false};
    int realtimeModeBeforeExperiment_{0};
    QFutureWatcher<size_t>* flushWatcher_{nullptr};
    QAction* startExperimentAct_ = nullptr;
    QAction* stopExperimentAct_ = nullptr;
    QPushButton* startExperimentBtn_ = nullptr;
    QPushButton* stopExperimentBtn_ = nullptr;
    QLabel* experimentIndicator_ = nullptr;
    QLabel* roiLabel_ = nullptr;
    QPushButton* startCameraBtn_ = nullptr;
    QPushButton* stopCameraBtn_ = nullptr;
};
