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
    void onFormatChanged();
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

    // Returns a path that does not currently exist, by appending "_1",
    // "_2", ... to the base name if needed. Works for both files (preserves
    // the extension) and directories.
    QString resolveNonCollidingPath(const QString& candidate) const;

    Ui::BufferSaveDialog* ui;
    backend::AppBackend& backend_;
};

} // namespace frontend

