#pragma once

#include <QWidget>

#include "backend/services/SyringePumpService.h"

#include <vector>

class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace frontend {

class PumpRowWidget;

// Tab hosting zero-to-many syringe pumps. Users add or remove pumps at
// runtime; each pump is represented by a PumpRowWidget. Per-pump "live"
// settings (flow rate / unit / direction) and connection parameters
// (port name / baud / address / syringe volume) are persisted to the
// shared config.json.
class SyringePumpTab : public QWidget {
    Q_OBJECT
public:
    using PumpHandle = backend::services::SyringePumpService::PumpHandle;

    explicit SyringePumpTab(backend::services::SyringePumpService& service,
                            QWidget* parent = nullptr);
    ~SyringePumpTab() override;

    // Reload rows from the underlying service (e.g. after the settings
    // dialog added/removed pumps). Keeps live-settings values when the
    // handle already had a row.
    void syncRowsWithService();

    // Add pumps with the given names if the underlying service is empty
    // (e.g. first run with no config). No-op when pumps already exist.
    void ensureDefaultPumps(const QStringList& defaultNames);

    void saveConfig();

public slots:
    void loadConfig();

private slots:
    void onAddPumpClicked();
    void onRemovePump(PumpHandle handle);
    void onUpdateStatus();
    void onLiveSettingsChanged(PumpHandle handle);
    void onNameChanged(PumpHandle handle, const QString& name);

private:
    QString configPath() const;
    PumpRowWidget* findRow(PumpHandle handle) const;
    void addRowWidget(PumpHandle handle);
    void removeRowWidget(PumpHandle handle);

    backend::services::SyringePumpService& service_;
    std::vector<PumpRowWidget*> rows_;

    QVBoxLayout* pumpsLayout_{nullptr};
    QScrollArea* scroll_{nullptr};
    QPushButton* addBtn_{nullptr};
    QLabel* countLabel_{nullptr};
    QTimer* statusTimer_{nullptr};
};

} // namespace frontend
