#pragma once

#include <QMainWindow>

class QLabel;
class QTimer;
class PlaybackPanel;
class QTabWidget;

namespace backend { class AppBackend; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(backend::AppBackend& backend, QWidget* parent = nullptr);

private slots:
    void onStartCapture();
    void onStopCapture();
    void onUpdateStats();

private:
    backend::AppBackend& backend_;
    QLabel* statusLabel_ = nullptr;
    QTimer* statsTimer_ = nullptr;
    PlaybackPanel* playbackPanel_ = nullptr;
    QTabWidget* tabs_ = nullptr;
};
