#pragma once

#include <QDialog>

namespace backend { class AppBackend; }
namespace Ui { class BufferSaveDialog; }

namespace frontend {

class BufferSaveDialog : public QDialog {
    Q_OBJECT
public:
    explicit BufferSaveDialog(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~BufferSaveDialog();

private slots:
    void onBrowseDirectory();
    void onRangeModeChanged();
    void onRefreshRanges();
    void onApplyResize();
    void onSaveFrames();
    void onDialogAccepted();

private:
    void updateAvailableRanges();
    void updateUIState();
    bool validateInputs();
    QString formatTimestamp(uint64_t timestampNs) const;
    void updateMemoryDisplay();
    QString formatMemoryBytes(uint64_t bytes) const;

    Ui::BufferSaveDialog* ui;
    backend::AppBackend& backend_;
};

} // namespace frontend

