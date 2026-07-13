#pragma once

#include <QDialog>
#include <QVector>

#include "frontend/utils/ProcessingCoreCatalog.h"

class QComboBox;
class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QPushButton;

namespace backend { class AppBackend; }

namespace frontend {

class ProcessingCoreDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ProcessingCoreDialog(backend::AppBackend& backend, QWidget* parent = nullptr);

    // Called once during desktop startup, before capture/realtime begins.
    static bool restorePersistedCore(backend::AppBackend& backend, QString* error = nullptr);

private slots:
    void reload();
    void prepareAndActivateSelected();
    void updateButtons();

private:
    int selectedVersionIndex() const;
    void loadCanonicalActive(const QString& channel);
    void populate();
    void setBusy(bool busy, const QString& message = {});
    void downloadAndActivate(
        const processingcorecatalog::VersionEntry& version,
        const processingcorecatalog::NativePluginEntry& plugin,
        const QByteArray& manifestSha256Hex);

    backend::AppBackend& backend_;
    QNetworkAccessManager* network_{nullptr};
    QComboBox* channelBox_{nullptr};
    QListWidget* versions_{nullptr};
    QLabel* activeLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QPushButton* prepareButton_{nullptr};
    QPushButton* refreshButton_{nullptr};
    processingcorecatalog::ParseResult catalog_;
    bool busy_{false};
};

} // namespace frontend
