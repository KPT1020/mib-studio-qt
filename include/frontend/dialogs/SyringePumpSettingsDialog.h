#pragma once

#include <QDialog>
#include <QStringList>

#include "backend/services/SyringePumpService.h"

#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

// Per-pump configuration dialog (COM port, baud, address, syringe volume/
// unit, editable name). Users can also add or remove pumps from here; each
// row auto-selects a distinct COM port by default.
class SyringePumpSettingsDialog : public QDialog {
    Q_OBJECT
public:
    using PumpHandle = backend::services::SyringePumpService::PumpHandle;

    // `externallyReservedPortsFn` lets callers exclude extra port names
    // (e.g. autofocus's COM port in the main MIB Studio app). May be
    // null/empty for standalone use.
    SyringePumpSettingsDialog(
        backend::services::SyringePumpService& service,
        std::function<QStringList()> externallyReservedPortsFn,
        QWidget* parent = nullptr);
    ~SyringePumpSettingsDialog() override;

signals:
    // Emitted whenever pumps were added, removed, or reconfigured in the
    // dialog — lets the hosting tab rebuild its rows.
    void pumpsChanged();

private slots:
    void onApply();
    void onAccept();
    void onAddPump();
    void onRefresh();

private:
    struct Row {
        PumpHandle handle;
        QLineEdit* nameEdit{nullptr};
        QComboBox* portCombo{nullptr};
        QSpinBox*  baudSpin{nullptr};
        QSpinBox*  addressSpin{nullptr};
        QDoubleSpinBox* syringeVolSpin{nullptr};
        QComboBox* syringeUnitCombo{nullptr};
        QPushButton* removeBtn{nullptr};
        QWidget* frame{nullptr};  // the container widget for this row
    };

    void buildUi();
    void appendRowForExistingPump(PumpHandle id);
    void appendRowForNewPump();
    void removeRow(PumpHandle id);
    void refreshAllPortCombos();
    QStringList currentlyChosenPorts(PumpHandle exclude) const;
    QString firstFreePort(const QStringList& exclude) const;

    QStringList reservedFor(PumpHandle row) const;

    backend::services::SyringePumpService& service_;
    std::function<QStringList()> externallyReservedPortsFn_;
    std::vector<Row> rows_;

    QVBoxLayout* rowsLayout_{nullptr};
    QPushButton* addBtn_{nullptr};
    QPushButton* refreshBtn_{nullptr};
};
