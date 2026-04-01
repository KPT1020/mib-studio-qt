#pragma once

#include <QDialog>

namespace backend { class AppBackend; }
namespace Ui { class SyringePumpSettingsDialog; }

class SyringePumpSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SyringePumpSettingsDialog(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~SyringePumpSettingsDialog();

private slots:
    void onApply();
    void onRefreshPorts();

private:
    void loadConfig();
    void saveConfig();
    QString configPath() const;
    void populateComPorts();

    Ui::SyringePumpSettingsDialog* ui;
    backend::AppBackend& backend_;
};
