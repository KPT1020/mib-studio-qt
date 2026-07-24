#pragma once

#include <QMainWindow>

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
namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~MainWindow();

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
    QTimer* workflowTimer_ = nullptr;
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
