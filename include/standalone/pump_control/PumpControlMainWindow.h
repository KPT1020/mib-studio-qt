#pragma once

#include <QMainWindow>

#include "backend/services/SyringePumpService.h"

namespace frontend {
class SyringePumpTab;
}

class PumpControlMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PumpControlMainWindow(QWidget* parent = nullptr);
    ~PumpControlMainWindow() override;

private:
    void loadInitialConfig();
    void setupUi();

    backend::services::SyringePumpService pumpService_;
    frontend::SyringePumpTab* pumpTab_{nullptr};
};
