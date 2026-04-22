#include "standalone/pump_control/PumpControlMainWindow.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QStringList>

#include "frontend/dialogs/SyringePumpSettingsDialog.h"
#include "frontend/tabs/SyringePumpTab.h"

namespace standalone::pump_control {

PumpControlMainWindow::PumpControlMainWindow(QWidget* parent)
    : QMainWindow(parent),
      service_(std::make_unique<backend::services::SyringePumpService>())
{
    setWindowTitle(tr("Pump Control"));

    tab_ = new frontend::SyringePumpTab(*service_, this);
    // Fresh installs default to a single pump.
    tab_->ensureDefaultPumps({QStringLiteral("Pump 1")});
    setCentralWidget(tab_);

    buildMenus();
    statusBar()->showMessage(tr("Ready"));
    resize(720, 520);
}

PumpControlMainWindow::~PumpControlMainWindow() = default;

void PumpControlMainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* exitAct = fileMenu->addAction(tr("E&xit"));
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* settingsMenu = menuBar()->addMenu(tr("&Settings"));
    auto* pumpSettingsAct = settingsMenu->addAction(tr("&Pump Settings…"));
    connect(pumpSettingsAct, &QAction::triggered, this, [this]() {
        SyringePumpSettingsDialog dlg(
            *service_,
            [] { return QStringList{}; },   // no external reservations in standalone
            this);
        // When the dialog adds/removes pumps, keep the tab in sync.
        connect(&dlg, &SyringePumpSettingsDialog::pumpsChanged, tab_,
                &frontend::SyringePumpTab::syncRowsWithService);
        dlg.exec();
        // Persist any settings-dialog changes that didn't pass through the tab.
        tab_->syncRowsWithService();
        tab_->saveConfig();
    });
}

} // namespace standalone::pump_control
