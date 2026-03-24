#pragma once

#include <optional>

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QUrl>

class QWidget;
class QNetworkAccessManager;

namespace frontend {

class AutoUpdater final : public QObject {
    Q_OBJECT
public:
    explicit AutoUpdater(QWidget* uiParent, QObject* parent = nullptr);

    // If interactive=true, user-facing dialogs are shown even for "no update" / errors.
    // If interactive=false, only prompts when an update is available (quiet check).
    void checkForUpdates(bool interactive);

private:
    struct Manifest {
        QString versionString;
        QUrl installerUrl;
        QByteArray installerSha256Hex; // hex bytes, lowercased
        qint64 installerSizeBytes{-1};
        QUrl releaseNotesUrl;
    };

    QUrl manifestUrlFromEnvOrDefault() const;
    void startManifestRequest(const QUrl& url, bool interactive);
    std::optional<Manifest> parseManifest(const QByteArray& jsonBytes, QString* errorOut) const;
    void startInstallerDownload(const Manifest& manifest, bool interactive);

    bool verifyDownloadedInstaller(const QString& path, const Manifest& manifest, QString* errorOut) const;
    bool launchInstallerElevated(const QString& installerPath, QString* errorOut) const;

    void infoBox(const QString& title, const QString& msg, bool interactive) const;
    void errorBox(const QString& title, const QString& msg, bool interactive) const;

    QPointer<QWidget> uiParent_;
    QNetworkAccessManager* net_{nullptr};
    bool busy_{false};
};

} // namespace frontend

