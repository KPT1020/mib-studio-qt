#pragma once

#include <QMainWindow>

#include "backend/services/SyringePumpService.h"

#include <memory>

namespace frontend { class SyringePumpTab; }

namespace standalone::pump_control {

// Thin QMainWindow for the standalone pump_control app. Owns the
// SyringePumpService directly — no AppBackend, no camera, no HDF5.
class PumpControlMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PumpControlMainWindow(QWidget* parent = nullptr);
    ~PumpControlMainWindow() override;

private:
    void buildMenus();

    std::unique_ptr<backend::services::SyringePumpService> service_;
    frontend::SyringePumpTab* tab_{nullptr};
};

} // namespace standalone::pump_control
