#include "backend/processing/EModulusLutCatalog.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>

#include <spdlog/spdlog.h>

namespace {
constexpr int kNetworkTimeoutMs = 8000;
constexpr char kLutFileName[] = "scaled_isoelastic_data_LUT_6.16-4.24.txt";
constexpr char kLutManifestEnv[] = "MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL";
constexpr char kLutCacheDirEnv[] = "MIB_STUDIO_EMODULUS_LUT_CACHE_DIR";
constexpr char kDefaultChannel[] = "stable";

QDateTime parseIsoDateTime(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return {};
    }
    QDateTime dt = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(text, Qt::ISODate);
    }
    return dt;
}

QString appVersionString() {
    return QCoreApplication::applicationVersion().trimmed();
}

QByteArray normalizedHex(const QString& hex) {
    QByteArray out = hex.trimmed().toUtf8();
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'F') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}
} // namespace

namespace backend {

QString EModulusLutCatalog::defaultManifestUrl(const QString& channel) {
    const QString trimmed = channel.trimmed().isEmpty() ? QString::fromLatin1(kDefaultChannel) : channel.trimmed();
    return QStringLiteral("https://updates.yofo.bio/%1/emodulus-lut/latest.json").arg(trimmed);
}

QUrl EModulusLutCatalog::manifestUrlFromEnvOrDefault(const QString& channel) const {
    const QString override = qEnvironmentVariable(kLutManifestEnv);
    if (!override.trimmed().isEmpty()) {
        const QUrl url(override.trimmed());
        if (url.isValid() && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("file"))) {
            return url;
        }
        SPDLOG_WARN("EModulusLutCatalog: ignoring invalid {}='{}'", kLutManifestEnv, override.toStdString());
    }
    return QUrl(defaultManifestUrl(channel));
}

QString EModulusLutCatalog::localCacheDir() {
    const QString override = qEnvironmentVariable(kLutCacheDirEnv);
    if (!override.trimmed().isEmpty()) {
        return QDir(override.trimmed()).absolutePath();
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.trimmed().isEmpty()) {
        base = QDir::homePath();
    }
    return QDir(base).absoluteFilePath(QStringLiteral("isoelastic_curve"));
}

QString EModulusLutCatalog::localLutPath() {
    return QDir(localCacheDir()).absoluteFilePath(QString::fromLatin1(kLutFileName));
}

QString EModulusLutCatalog::localMetadataPath() {
    return QDir(localCacheDir()).absoluteFilePath(QString::fromLatin1(kLutFileName) + QStringLiteral(".meta.json"));
}

QByteArray EModulusLutCatalog::sha256Hex(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool EModulusLutCatalog::isCompatibleWithCurrentApp(const Manifest& manifest) {
    const QVersionNumber current = QVersionNumber::fromString(appVersionString());
    if (current.isNull()) {
        return true;
    }

    if (!manifest.appMinVersion.trimmed().isEmpty()) {
        const QVersionNumber minVersion = QVersionNumber::fromString(manifest.appMinVersion);
        if (!minVersion.isNull() && QVersionNumber::compare(current, minVersion) < 0) {
            return false;
        }
    }

    if (!manifest.appMaxVersion.trimmed().isEmpty()) {
        const QVersionNumber maxVersion = QVersionNumber::fromString(manifest.appMaxVersion);
        if (!maxVersion.isNull() && QVersionNumber::compare(current, maxVersion) > 0) {
            return false;
        }
    }

    return true;
}

std::optional<QByteArray> EModulusLutCatalog::readFileBytes(const QString& path, QString* errorOut) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to open %1: %2").arg(path, file.errorString());
        }
        return std::nullopt;
    }
    return file.readAll();
}

std::optional<QByteArray> EModulusLutCatalog::readUrlBytes(const QUrl& url, QString* errorOut) {
    if (!url.isValid()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid URL: %1").arg(url.toString());
        }
        return std::nullopt;
    }

    if (url.scheme() == QStringLiteral("file")) {
        return readFileBytes(url.toLocalFile(), errorOut);
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kNetworkTimeoutMs);

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(kNetworkTimeoutMs);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();
    timeout.stop();

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    const QString errorString = reply->errorString();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        if (errorOut) {
            QString detail = errorString;
            if (httpStatus > 0) {
                detail += QStringLiteral(" (HTTP %1)").arg(httpStatus);
            }
            if (!body.isEmpty()) {
                detail += QStringLiteral("\n%1").arg(QString::fromUtf8(body.left(500)));
            }
            *errorOut = detail;
        }
        return std::nullopt;
    }

    return body;
}

std::optional<QByteArray> EModulusLutCatalog::computeFileSha256(const QString& path, QString* errorOut) {
    const auto bytes = readFileBytes(path, errorOut);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    return sha256Hex(*bytes);
}

std::optional<EModulusLutCatalog::Manifest> EModulusLutCatalog::manifestFromJson(const QJsonObject& obj, QString* errorOut) {
    Manifest manifest;
    manifest.manifestSchemaVersion = obj.value(QStringLiteral("manifest_schema_version")).toInt(0);
    manifest.lutId = obj.value(QStringLiteral("lut_id")).toString().trimmed();
    manifest.displayName = obj.value(QStringLiteral("display_name")).toString();
    manifest.revision = obj.value(QStringLiteral("revision")).toString().trimmed();
    manifest.downloadUrl = QUrl(obj.value(QStringLiteral("download_url")).toString());
    manifest.sha256 = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    manifest.sizeBytes = static_cast<qint64>(obj.value(QStringLiteral("size_bytes")).toVariant().toLongLong());
    const QString publishedAt = obj.value(QStringLiteral("published_at")).toString();
    manifest.publishedAt = parseIsoDateTime(publishedAt);
    manifest.appMinVersion = obj.value(QStringLiteral("app_min_version")).toString();
    manifest.appMaxVersion = obj.value(QStringLiteral("app_max_version")).toString();

    if (manifest.manifestSchemaVersion <= 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("Manifest is missing manifest_schema_version.");
        }
        return std::nullopt;
    }
    if (manifest.lutId.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Manifest is missing lut_id.");
        }
        return std::nullopt;
    }
    if (!manifest.downloadUrl.isValid() || (manifest.downloadUrl.scheme() != QStringLiteral("https") && manifest.downloadUrl.scheme() != QStringLiteral("http") && manifest.downloadUrl.scheme() != QStringLiteral("file"))) {
        if (errorOut) {
            *errorOut = QStringLiteral("Manifest download_url must be HTTP(S) or file://.");
        }
        return std::nullopt;
    }
    if (manifest.revision.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Manifest is missing revision.");
        }
        return std::nullopt;
    }
    if (manifest.sha256.size() != 64) {
        if (errorOut) {
            *errorOut = QStringLiteral("Manifest sha256 must be a 64-character hex string.");
        }
        return std::nullopt;
    }
    if (!isCompatibleWithCurrentApp(manifest)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Manifest is incompatible with this app version (%1).").arg(appVersionString());
        }
        return std::nullopt;
    }

    return manifest;
}

QJsonObject EModulusLutCatalog::manifestToJson(const Manifest& manifest, const LocalMetadata& metadata) {
    QJsonObject obj;
    obj.insert(QStringLiteral("manifest_schema_version"), manifest.manifestSchemaVersion);
    obj.insert(QStringLiteral("lut_id"), manifest.lutId);
    obj.insert(QStringLiteral("display_name"), manifest.displayName);
    obj.insert(QStringLiteral("revision"), manifest.revision);
    obj.insert(QStringLiteral("download_url"), manifest.downloadUrl.toString());
    obj.insert(QStringLiteral("sha256"), manifest.sha256);
    obj.insert(QStringLiteral("size_bytes"), static_cast<qint64>(manifest.sizeBytes));
    if (manifest.publishedAt.isValid()) {
        obj.insert(QStringLiteral("published_at"), manifest.publishedAt.toString(Qt::ISODateWithMs));
    } else {
        obj.insert(QStringLiteral("published_at"), QJsonValue::Null);
    }
    obj.insert(QStringLiteral("app_min_version"), manifest.appMinVersion);
    obj.insert(QStringLiteral("app_max_version"), manifest.appMaxVersion);

    QJsonObject cache;
    cache.insert(QStringLiteral("schema_version"), metadata.schemaVersion);
    cache.insert(QStringLiteral("source_type"), metadata.sourceType);
    cache.insert(QStringLiteral("lut_id"), metadata.lutId);
    cache.insert(QStringLiteral("display_name"), metadata.displayName);
    cache.insert(QStringLiteral("revision"), metadata.revision);
    cache.insert(QStringLiteral("manifest_url"), metadata.manifestUrl);
    cache.insert(QStringLiteral("download_url"), metadata.downloadUrl);
    cache.insert(QStringLiteral("sha256"), metadata.sha256);
    cache.insert(QStringLiteral("checksum_status"), metadata.checksumStatus);
    cache.insert(QStringLiteral("local_path"), metadata.localPath);
    cache.insert(QStringLiteral("size_bytes"), static_cast<qint64>(metadata.sizeBytes));
    if (metadata.publishedAt.isValid()) {
        cache.insert(QStringLiteral("published_at"), metadata.publishedAt.toString(Qt::ISODateWithMs));
    } else {
        cache.insert(QStringLiteral("published_at"), QJsonValue::Null);
    }
    if (metadata.lastCheckedUtc.isValid()) {
        cache.insert(QStringLiteral("last_checked_utc"), metadata.lastCheckedUtc.toString(Qt::ISODateWithMs));
    } else {
        cache.insert(QStringLiteral("last_checked_utc"), QJsonValue::Null);
    }
    if (metadata.lastUpdatedUtc.isValid()) {
        cache.insert(QStringLiteral("last_updated_utc"), metadata.lastUpdatedUtc.toString(Qt::ISODateWithMs));
    } else {
        cache.insert(QStringLiteral("last_updated_utc"), QJsonValue::Null);
    }
    obj.insert(QStringLiteral("cache"), cache);
    return obj;
}

std::optional<EModulusLutCatalog::LocalMetadata> EModulusLutCatalog::metadataFromJson(const QJsonObject& obj) {
    LocalMetadata metadata;
    metadata.schemaVersion = obj.value(QStringLiteral("schema_version")).toInt(0);
    metadata.sourceType = obj.value(QStringLiteral("source_type")).toString();
    metadata.lutId = obj.value(QStringLiteral("lut_id")).toString();
    metadata.displayName = obj.value(QStringLiteral("display_name")).toString();
    metadata.revision = obj.value(QStringLiteral("revision")).toString();
    metadata.manifestUrl = obj.value(QStringLiteral("manifest_url")).toString();
    metadata.downloadUrl = obj.value(QStringLiteral("download_url")).toString();
    metadata.sha256 = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    metadata.checksumStatus = obj.value(QStringLiteral("checksum_status")).toString();
    metadata.localPath = obj.value(QStringLiteral("local_path")).toString();
    metadata.sizeBytes = obj.value(QStringLiteral("size_bytes")).toVariant().toLongLong();
    metadata.publishedAt = parseIsoDateTime(obj.value(QStringLiteral("published_at")).toString());
    metadata.lastCheckedUtc = parseIsoDateTime(obj.value(QStringLiteral("last_checked_utc")).toString());
    metadata.lastUpdatedUtc = parseIsoDateTime(obj.value(QStringLiteral("last_updated_utc")).toString());
    if (metadata.schemaVersion <= 0) {
        return std::nullopt;
    }
    return metadata;
}

bool EModulusLutCatalog::writeFileBytesAtomic(const QString& path, const QByteArray& bytes, QString* errorOut) {
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to open %1 for write: %2").arg(path, file.errorString());
        }
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to write %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    if (!file.commit()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to commit %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

bool EModulusLutCatalog::writeMetadataFile(const QString& path, const LocalMetadata& metadata, QString* errorOut) {
    QJsonObject cache;
    cache.insert(QStringLiteral("schema_version"), metadata.schemaVersion);
    cache.insert(QStringLiteral("source_type"), metadata.sourceType);
    cache.insert(QStringLiteral("lut_id"), metadata.lutId);
    cache.insert(QStringLiteral("display_name"), metadata.displayName);
    cache.insert(QStringLiteral("revision"), metadata.revision);
    cache.insert(QStringLiteral("manifest_url"), metadata.manifestUrl);
    cache.insert(QStringLiteral("download_url"), metadata.downloadUrl);
    cache.insert(QStringLiteral("sha256"), metadata.sha256);
    cache.insert(QStringLiteral("checksum_status"), metadata.checksumStatus);
    cache.insert(QStringLiteral("local_path"), metadata.localPath);
    cache.insert(QStringLiteral("size_bytes"), static_cast<qint64>(metadata.sizeBytes));
    if (metadata.publishedAt.isValid()) {
        cache.insert(QStringLiteral("published_at"), metadata.publishedAt.toString(Qt::ISODateWithMs));
    } else {
        cache.insert(QStringLiteral("published_at"), QJsonValue::Null);
    }
    if (metadata.lastCheckedUtc.isValid()) {
        cache.insert(QStringLiteral("last_checked_utc"), metadata.lastCheckedUtc.toString(Qt::ISODateWithMs));
    } else {
        cache.insert(QStringLiteral("last_checked_utc"), QJsonValue::Null);
    }
    if (metadata.lastUpdatedUtc.isValid()) {
        cache.insert(QStringLiteral("last_updated_utc"), metadata.lastUpdatedUtc.toString(Qt::ISODateWithMs));
    } else {
        cache.insert(QStringLiteral("last_updated_utc"), QJsonValue::Null);
    }
    const QByteArray payload = QJsonDocument(cache).toJson(QJsonDocument::Indented);
    return writeFileBytesAtomic(path, payload, errorOut);
}

std::optional<EModulusLutCatalog::LocalMetadata> EModulusLutCatalog::readMetadataFile(const QString& path, QString* errorOut) {
    const auto bytes = readFileBytes(path, errorOut);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid LUT metadata JSON in %1: %2").arg(path, parseError.errorString());
        }
        return std::nullopt;
    }
    return metadataFromJson(doc.object());
}

EModulusLutCatalog::LocalMetadata EModulusLutCatalog::metadataFromManifest(const Manifest& manifest,
                                                                           const QString& sourceType,
                                                                           const QString& localPath,
                                                                           const QString& checksumStatus) {
    LocalMetadata metadata;
    metadata.schemaVersion = 1;
    metadata.sourceType = sourceType;
    metadata.lutId = manifest.lutId;
    metadata.displayName = manifest.displayName;
    metadata.revision = manifest.revision;
    metadata.manifestUrl = QString();
    metadata.downloadUrl = manifest.downloadUrl.toString();
    metadata.sha256 = manifest.sha256;
    metadata.checksumStatus = checksumStatus;
    metadata.localPath = localPath;
    metadata.publishedAt = manifest.publishedAt;
    metadata.lastCheckedUtc = QDateTime::currentDateTimeUtc();
    metadata.lastUpdatedUtc = QDateTime::currentDateTimeUtc();
    metadata.sizeBytes = manifest.sizeBytes;
    return metadata;
}

std::optional<EModulusLutCatalog::Manifest> EModulusLutCatalog::fetchManifest(const QUrl& url, QString* errorOut) const {
    const auto bytes = readUrlBytes(url, errorOut);
    if (!bytes.has_value()) {
        return std::nullopt;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid LUT manifest JSON: %1").arg(parseError.errorString());
        }
        return std::nullopt;
    }

    auto manifest = manifestFromJson(doc.object(), errorOut);
    if (!manifest.has_value()) {
        return std::nullopt;
    }
    if (manifest->manifestSchemaVersion <= 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("Manifest is missing manifest_schema_version.");
        }
        return std::nullopt;
    }
    return manifest;
}

bool EModulusLutCatalog::ensureManagedLut(const QString& bundledPath,
                                          QString* resolvedPathOut,
                                          ManagedLutInfo* infoOut,
                                          QString* errorOut) const {
    const QString cacheDir = localCacheDir();
    if (!QDir().mkpath(cacheDir)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to create LUT cache directory: %1").arg(cacheDir);
        }
        return false;
    }

    const QString managedPath = localLutPath();
    const QString metadataPath = localMetadataPath();
    ManagedLutInfo info;
    info.sourceType = QStringLiteral("bundled-fallback");
    info.localPath = managedPath;

    QString localSha;
    if (QFileInfo::exists(managedPath)) {
        QString localReadError;
        const auto sha = computeFileSha256(managedPath, &localReadError);
        if (sha.has_value()) {
            localSha = QString::fromLatin1(*sha);
            info.checksumStatus = QStringLiteral("verified");
        } else {
            info.checksumStatus = QStringLiteral("unknown");
            if (!localReadError.isEmpty()) {
                SPDLOG_WARN("EModulusLutCatalog: could not hash local cache {}: {}", managedPath.toStdString(), localReadError.toStdString());
            }
        }
    }

    std::optional<LocalMetadata> cachedMetadata = readMetadataFile(metadataPath, nullptr);
    if (cachedMetadata.has_value()) {
        info.sourceType = cachedMetadata->sourceType.isEmpty() ? info.sourceType : cachedMetadata->sourceType;
        info.lutId = cachedMetadata->lutId;
        info.displayName = cachedMetadata->displayName;
        info.revision = cachedMetadata->revision;
        info.manifestUrl = cachedMetadata->manifestUrl;
        info.downloadUrl = cachedMetadata->downloadUrl;
        info.sha256 = cachedMetadata->sha256;
        info.checksumStatus = cachedMetadata->checksumStatus.isEmpty() ? info.checksumStatus : cachedMetadata->checksumStatus;
        info.publishedAt = cachedMetadata->publishedAt;
        info.sizeBytes = cachedMetadata->sizeBytes;
    }

    const QUrl manifestUrl = manifestUrlFromEnvOrDefault();
    info.manifestUrl = manifestUrl.toString();

    std::optional<Manifest> manifest;
    const bool allowRemoteFetch = QCoreApplication::instance() != nullptr ||
                                  manifestUrl.scheme() == QStringLiteral("file");
    if (allowRemoteFetch) {
        manifest = fetchManifest(manifestUrl, nullptr);
    } else {
        SPDLOG_INFO("EModulusLutCatalog: skipping remote LUT fetch because no Qt application instance is active");
    }
    if (manifest.has_value()) {
        info.lutId = manifest->lutId;
        info.displayName = manifest->displayName;
        info.revision = manifest->revision;
        info.downloadUrl = manifest->downloadUrl.toString();
        info.sha256 = manifest->sha256;
        info.sizeBytes = manifest->sizeBytes;
        info.publishedAt = manifest->publishedAt;

        const bool localMatchesRemote = !localSha.isEmpty() && localSha.compare(manifest->sha256, Qt::CaseInsensitive) == 0;
        const bool needsRefresh = !QFileInfo::exists(managedPath) || !localMatchesRemote;
        if (needsRefresh) {
            QString downloadError;
            const auto remoteBytes = readUrlBytes(manifest->downloadUrl, &downloadError);
            if (!remoteBytes.has_value()) {
                info.note = QStringLiteral("remote download failed: %1").arg(downloadError);
                SPDLOG_WARN("EModulusLutCatalog: {}", info.note.toStdString());
            } else {
                const QByteArray remoteSha = sha256Hex(*remoteBytes);
                const bool shaOk = remoteSha.compare(normalizedHex(manifest->sha256), Qt::CaseInsensitive) == 0;
                const bool sizeOk = manifest->sizeBytes < 0 || manifest->sizeBytes == remoteBytes->size();
                if (shaOk && sizeOk) {
                    if (!writeFileBytesAtomic(managedPath, *remoteBytes, errorOut)) {
                        return false;
                    }
                    const LocalMetadata metadata = metadataFromManifest(*manifest, QStringLiteral("r2-public-catalog"), managedPath, QStringLiteral("verified"));
                    if (!writeMetadataFile(metadataPath, metadata, errorOut)) {
                        return false;
                    }
                    info.sourceType = QStringLiteral("r2-public-catalog");
                    info.remoteUpdated = true;
                    info.checksumStatus = QStringLiteral("verified");
                    info.localPath = managedPath;
                    info.note = QStringLiteral("remote LUT updated");
                    localSha = QString::fromLatin1(remoteSha);
                    SPDLOG_INFO("EModulusLutCatalog: updated LUT '{}' revision '{}' from {}",
                                manifest->lutId.toStdString(),
                                manifest->revision.toStdString(),
                                manifestUrl.toString().toStdString());
                } else {
                    info.note = QStringLiteral("remote checksum/size mismatch");
                    SPDLOG_WARN("EModulusLutCatalog: remote LUT checksum/size mismatch for '{}' (sha_ok={}, size_ok={})",
                                manifest->lutId.toStdString(), shaOk, sizeOk);
                }
            }
        }
    } else if (QFileInfo::exists(managedPath)) {
        info.note = QStringLiteral("manifest fetch failed; using local cache");
        SPDLOG_WARN("EModulusLutCatalog: {}: {}", manifestUrl.toString().toStdString(), info.note.toStdString());
    } else {
        info.note = QStringLiteral("manifest fetch failed and no local cache exists");
        SPDLOG_WARN("EModulusLutCatalog: {}", info.note.toStdString());
    }

    if (QFileInfo::exists(managedPath)) {
        if (info.lutId.isEmpty()) {
            info.lutId = QString::fromLatin1(kLutFileName);
        }
        if (info.displayName.isEmpty()) {
            info.displayName = QStringLiteral("Young's modulus LUT");
        }
        if (info.revision.isEmpty()) {
            info.revision = QStringLiteral("local-cache");
        }
        if (info.checksumStatus.isEmpty()) {
            info.checksumStatus = localSha.isEmpty() ? QStringLiteral("unknown") : QStringLiteral("verified");
        }
        info.sourceType = info.remoteUpdated ? QStringLiteral("r2-public-catalog") : (cachedMetadata.has_value() ? cachedMetadata->sourceType : QStringLiteral("local-cache"));
        info.localPath = managedPath;
        if (resolvedPathOut) {
            *resolvedPathOut = managedPath;
        }
        if (infoOut) {
            *infoOut = info;
        }
        return true;
    }

    if (!QFileInfo::exists(bundledPath)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Bundled LUT not found: %1").arg(bundledPath);
        }
        return false;
    }

    QString copyError;
    const auto bundledBytes = readFileBytes(bundledPath, &copyError);
    if (!bundledBytes.has_value()) {
        if (errorOut) {
            *errorOut = copyError;
        }
        return false;
    }
    if (!writeFileBytesAtomic(managedPath, *bundledBytes, errorOut)) {
        if (resolvedPathOut) {
            *resolvedPathOut = bundledPath;
        }
        info.sourceType = QStringLiteral("bundled-fallback");
        info.usedBundledFallback = true;
        info.note = QStringLiteral("using bundled LUT directly because local cache could not be written");
        if (infoOut) {
            *infoOut = info;
        }
        return true;
    }

    const QByteArray bundledSha = sha256Hex(*bundledBytes);
    const LocalMetadata metadata = metadataFromManifest(
        Manifest{
            1,
            QString::fromLatin1(kLutFileName),
            QStringLiteral("Bundled LUT"),
            QStringLiteral("bundled"),
            QUrl::fromLocalFile(bundledPath),
            QString::fromLatin1(bundledSha),
            bundledBytes->size(),
            QDateTime(),
            QString(),
            QString(),
        },
        QStringLiteral("bundled-fallback"),
        managedPath,
        QStringLiteral("verified"));
    if (!writeMetadataFile(metadataPath, metadata, nullptr)) {
        SPDLOG_WARN("EModulusLutCatalog: failed to write LUT metadata at {}", metadataPath.toStdString());
    }

    info.sourceType = QStringLiteral("bundled-fallback");
    info.usedBundledFallback = true;
    info.lutId = QString::fromLatin1(kLutFileName);
    info.displayName = QStringLiteral("Young's modulus LUT");
    info.revision = QStringLiteral("bundled");
    info.localPath = managedPath;
    info.sha256 = QString::fromLatin1(bundledSha);
    info.checksumStatus = QStringLiteral("verified");
    info.note = QStringLiteral("seeded local cache from bundled LUT");
    if (resolvedPathOut) {
        *resolvedPathOut = managedPath;
    }
    if (infoOut) {
        *infoOut = info;
    }
    SPDLOG_INFO("EModulusLutCatalog: seeded local LUT cache from bundled copy at {}", managedPath.toStdString());
    return true;
}

} // namespace backend
