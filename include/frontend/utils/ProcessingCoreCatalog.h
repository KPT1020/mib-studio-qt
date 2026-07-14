#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace frontend::processingcorecatalog {

struct NativePluginEntry {
    QString filename;
    QString os;
    QString arch;
    QString url;
    QString sha256;
    QString runtimeFingerprint;
    QString entrypoint;
    QString appMinVersion;
    QString appMaxVersion;
    QString signingScheme;
    qint64 sizeBytes{-1};
    int engineAbiVersion{0};
    int contractVersion{0};
    bool signingRequired{false};
};

struct VersionEntry {
    QString channel;
    QString version;
    QString publishedAt;
    QString releaseTag;
    QString releaseUrl;
    QString manifestUrl;
    int contractVersion{0};
    QVector<NativePluginEntry> nativePlugins;
};

struct ParseResult {
    bool ok{false};
    QString error;
    QString channel;
    QString indexActiveVersion;
    QString activeVersion;
    QVector<VersionEntry> versions;
};

struct ManifestResult {
    bool ok{false};
    QString error;
    VersionEntry version;
    QByteArray rawSha256Hex;
};

struct ActivePointerResult {
    bool ok{false};
    QString error;
    QString warning;
    QString version;
};

ParseResult parseIndex(const QByteArray& bytes);
ManifestResult parseVersionManifest(const QByteArray& bytes);
ActivePointerResult validateCanonicalActive(const ParseResult& index,
                                            const ManifestResult& latest);
const NativePluginEntry* findNativePlugin(const VersionEntry& version,
                                          const QString& os,
                                          const QString& arch);
bool isAppCompatible(const NativePluginEntry& plugin, const QString& appVersion);
bool isProcessingContractCompatible(int requiredContractVersion,
                                    int activeContractVersion);
bool isVersionDowngrade(const QString& candidate, const QString& current);

} // namespace frontend::processingcorecatalog
