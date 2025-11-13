#pragma once

#include <QMainWindow>

class QLabel;
class QTimer;
class PlaybackPanel;
class QTabWidget;
class QSpinBox;
template<typename T> class QFutureWatcher;

namespace backend { class AppBackend; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(backend::AppBackend& backend, QWidget* parent = nullptr);

private slots:
    void onStartCapture();
    void onStopCapture();
    void onStartExperiment();
    void onStopExperiment();
    void onUpdateStats();

private:
    backend::AppBackend& backend_;
    QLabel* statusLabel_ = nullptr;
    QTimer* statsTimer_ = nullptr;
    PlaybackPanel* playbackPanel_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    uint64_t experimentStartTimeNs_{0};
    bool experimentActive_{false};
    bool flushInProgress_{false};
    QFutureWatcher<size_t>* flushWatcher_{nullptr};
    QSpinBox* invalidFrameSamplingSpinBox_{nullptr};
};
