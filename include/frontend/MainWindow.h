#pragma once

#include <QMainWindow>

class QLabel;
class QTimer;
class PlaybackPanel;
class QTabWidget;
class QSpinBox;
class QAction;
class QPushButton;
class QCloseEvent;
template<typename T> class QFutureWatcher;

namespace backend { class AppBackend; }
namespace frontend { class OverviewTab; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(backend::AppBackend& backend, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStartCapture();
    void onStopCapture();
    void onStartExperiment();
    void onStopExperiment();
    void onUpdateStats();
    void onTabChanged(int index);

private:
    void updateExperimentButtonStates();
    void updateTabStates();
    void startExperimentServices();
    void stopExperimentServices();
    backend::AppBackend& backend_;
    QLabel* statusLabel_ = nullptr;
    QTimer* statsTimer_ = nullptr;
    PlaybackPanel* playbackPanel_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTabWidget* experimentTabs_ = nullptr;
    frontend::OverviewTab* overviewTab_ = nullptr;
    uint64_t experimentStartTimeNs_{0};
    bool experimentActive_{false};
    bool experimentServicesActive_{false};
    bool flushInProgress_{false};
    QFutureWatcher<size_t>* flushWatcher_{nullptr};
    QAction* startExperimentAct_ = nullptr;
    QAction* stopExperimentAct_ = nullptr;
    QPushButton* startExperimentBtn_ = nullptr;
    QPushButton* stopExperimentBtn_ = nullptr;
    QLabel* experimentIndicator_ = nullptr;
    QPushButton* startCameraBtn_ = nullptr;
    QPushButton* stopCameraBtn_ = nullptr;
};
