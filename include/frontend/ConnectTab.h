#pragma once

#include <QWidget>

#include <vector>
#include <optional>

namespace backend { class AppBackend; }

class QListWidget;
class QPushButton;
class QLabel;

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
    QListWidget* deviceList_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QPushButton* mockBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

} // namespace frontend



