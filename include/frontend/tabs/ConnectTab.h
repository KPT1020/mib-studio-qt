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
    void noCamerasFound();

public slots:
    // On app launch: if exactly one camera is discoverable, auto-select it.
    // Emits:
    //  - connected() on auto-connect success
    //  - noCamerasFound() when discovery finds 0 cameras
    void tryAutoConnect();

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



