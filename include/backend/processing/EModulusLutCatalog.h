#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>

namespace backend {

class EModulusLutCatalog final {
public:
    struct Manifest {
        int manifestSchemaVersion = 0;
        QString lutId;
        QString displayName;
        QString revision;
        QUrl downloadUrl;
        QString sha256;
        qint64 sizeBytes = -1;
        QDateTime publishedAt;
        QString appMinVersion;
        QString appMaxVersion;
    };

    struct ManagedLutInfo {
        QString sourceType;
        QString lutId;
        QString displayName;
        QString revision;
        QString manifestUrl;
        QString downloadUrl;
        QString localPath;
        QString sha256;
        QString checksumStatus;
        QString note;
        QDateTime publishedAt;
        qint64 sizeBytes = -1;
        bool remoteUpdated = false;
        bool usedBundledFallback = false;
    };

    static QString defaultManifestUrl(const QString& channel = QStringLiteral("stable"));
    QUrl manifestUrlFromEnvOrDefault(const QString& channel = QStringLiteral("stable")) const;

    std::optional<Manifest> fetchManifest(const QUrl& url, QString* errorOut = nullptr) const;

    bool ensureManagedLut(const QString& bundledPath,
                          QString* resolvedPathOut,
                          ManagedLutInfo* infoOut = nullptr,
                          QString* errorOut = nullptr) const;

    static QString localCacheDir();
    static QString localLutPath();
    static QString localMetadataPath();

private:
    struct LocalMetadata {
        int schemaVersion = 1;
        QString sourceType;
        QString lutId;
        QString displayName;
        QString revision;
        QString manifestUrl;
        QString downloadUrl;
        QString sha256;
        QString checksumStatus;
        QString localPath;
        QDateTime publishedAt;
        QDateTime lastCheckedUtc;
        QDateTime lastUpdatedUtc;
        qint64 sizeBytes = -1;
    };

    static QByteArray sha256Hex(const QByteArray& bytes);
    static bool isCompatibleWithCurrentApp(const Manifest& manifest);
    static std::optional<QByteArray> readUrlBytes(const QUrl& url, QString* errorOut);
    static std::optional<QByteArray> readFileBytes(const QString& path, QString* errorOut);
    static bool writeFileBytesAtomic(const QString& path, const QByteArray& bytes, QString* errorOut);
    static std::optional<QByteArray> computeFileSha256(const QString& path, QString* errorOut);
    static std::optional<Manifest> manifestFromJson(const QJsonObject& obj, QString* errorOut);
    static QJsonObject manifestToJson(const Manifest& manifest, const LocalMetadata& metadata);
    static std::optional<LocalMetadata> metadataFromJson(const QJsonObject& obj);
    static bool writeMetadataFile(const QString& path, const LocalMetadata& metadata, QString* errorOut);
    static std::optional<LocalMetadata> readMetadataFile(const QString& path, QString* errorOut);
    static LocalMetadata metadataFromManifest(const Manifest& manifest,
                                              const QString& sourceType,
                                              const QString& localPath,
                                              const QString& checksumStatus);
};

} // namespace backend
