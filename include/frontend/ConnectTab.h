#pragma once

#include <QWidget>

#include <vector>
#include <optional>

namespace backend { class AppBackend; }

class QListWidget;
class QPushButton;
class QLabel;
class QTabWidget;

namespace frontend {

class ConnectTab : public QWidget {
    Q_OBJECT
public:
    explicit ConnectTab(backend::AppBackend& backend, QWidget* parent = nullptr);

signals:
    void connected();

private slots:
    void onRefresh();
    void onConnect();
    void onConfigureMock();

private:
    void populateDevices();

    backend::AppBackend& backend_;
    QTabWidget* tabWidget_ = nullptr;
    QListWidget* framegrabberList_ = nullptr;
    QListWidget* cameraList_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QPushButton* mockBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

} // namespace frontend



