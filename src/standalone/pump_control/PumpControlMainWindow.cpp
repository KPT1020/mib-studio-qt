#include "standalone/pump_control/PumpControlMainWindow.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>

#include "frontend/tabs/SyringePumpTab.h"

PumpControlMainWindow::PumpControlMainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(tr("Pump Control"));
    resize(900, 700);

    loadInitialConfig();

    pumpTab_ = new frontend::SyringePumpTab(
        pumpService_,
        []() { return QStringList{}; },
        this);
    setCentralWidget(pumpTab_);

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* exitAction = fileMenu->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
}

PumpControlMainWindow::~PumpControlMainWindow() = default;

void PumpControlMainWindow::loadInitialConfig() {
    if (pumpService_.pumpCount() == 0) {
        pumpService_.addPump(QStringLiteral("Pump 1"));
    }
}
