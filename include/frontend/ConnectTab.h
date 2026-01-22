#pragma once

#include <QWidget>

#include <vector>
#include <optional>

namespace backend { class AppBackend; }
namespace Ui { class ConnectTab; }

namespace frontend {

class ConnectTab : public QWidget {
    Q_OBJECT
public:
    explicit ConnectTab(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~ConnectTab();

signals:
    void connected();

private slots:
    void onRefresh();
    void onConnect();
    void onConfigureMock();

private:
    void populateDevices();

    Ui::ConnectTab* ui;
    backend::AppBackend& backend_;
};

} // namespace frontend



