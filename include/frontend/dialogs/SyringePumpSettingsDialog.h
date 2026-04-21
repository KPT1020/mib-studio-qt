#pragma once

#include <QDialog>

#include <functional>

namespace backend::services {
class SyringePumpService;
}
namespace Ui {
class SyringePumpSettingsDialog;
}

class SyringePumpSettingsDialog : public QDialog {
    Q_OBJECT
public:
    using ReservedPortNamesProvider = std::function<QStringList()>;

    explicit SyringePumpSettingsDialog(backend::services::SyringePumpService& pumpService,
                                       ReservedPortNamesProvider reservedPortNamesProvider = {},
                                       QWidget* parent = nullptr);
    ~SyringePumpSettingsDialog();

private slots:
    void onApply();
    void onRefreshPorts();

private:
    QString configPath() const;
    QString normalizedPort(const QString& value) const;
    void ensureTwoPumps();
    void loadConfig();
    void saveConfig();
    void populateComPorts();

    Ui::SyringePumpSettingsDialog* ui;
    backend::services::SyringePumpService& pumpService_;
    ReservedPortNamesProvider reservedPortNamesProvider_;
};
