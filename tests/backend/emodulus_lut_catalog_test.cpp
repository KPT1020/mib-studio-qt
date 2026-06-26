#include "backend/processing/EModulusLut.h"
#include "backend/processing/EModulusLutCatalog.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

bool writeTextFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Failed to open " << path.toStdString() << " for write: " << file.errorString().toStdString() << '\n';
        return false;
    }
    if (file.write(data) != data.size()) {
        std::cerr << "Failed to write " << path.toStdString() << ": " << file.errorString().toStdString() << '\n';
        return false;
    }
    return true;
}

QByteArray sha256Hex(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QString tempPathSuffix(const QString& root, const QString& relative) {
    return QDir(root).absoluteFilePath(relative);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MIB_Studio_Qt"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    const QString root = QString::fromStdString(std::filesystem::temp_directory_path().string()) +
                         QStringLiteral("/mib_emodulus_lut_test_") +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(root);

    const QString cacheDir = tempPathSuffix(root, QStringLiteral("cache"));
    const QString bundledDir = tempPathSuffix(root, QStringLiteral("bundled/isoelastic_curve"));
    const QString remoteDir = tempPathSuffix(root, QStringLiteral("remote"));
    QDir().mkpath(cacheDir);
    QDir().mkpath(bundledDir);
    QDir().mkpath(remoteDir);

    qputenv("MIB_STUDIO_EMODULUS_LUT_CACHE_DIR", cacheDir.toUtf8());

    const QString bundledPath = tempPathSuffix(bundledDir, QStringLiteral("scaled_isoelastic_data_LUT_6.16-4.24.txt"));
    const QByteArray bundledBytes = QByteArrayLiteral("10.0\t0.2\t12.5\n");
    if (!writeTextFile(bundledPath, bundledBytes)) {
        return 1;
    }

    const QString remotePath = tempPathSuffix(remoteDir, QStringLiteral("scaled_isoelastic_data_LUT_6.16-4.24.txt"));
    const QByteArray remoteBytes = QByteArrayLiteral("10.0\t0.2\t42.0\n");
    if (!writeTextFile(remotePath, remoteBytes)) {
        return 2;
    }

    const QString manifestPath = tempPathSuffix(remoteDir, QStringLiteral("latest.json"));
    QJsonObject manifest;
    manifest.insert(QStringLiteral("manifest_schema_version"), 1);
    manifest.insert(QStringLiteral("lut_id"), QStringLiteral("scaled_isoelastic_data_LUT_6.16-4.24"));
    manifest.insert(QStringLiteral("display_name"), QStringLiteral("Scaled Isoelastic LUT"));
    manifest.insert(QStringLiteral("revision"), QStringLiteral("2026.06.11-1"));
    manifest.insert(QStringLiteral("download_url"), QUrl::fromLocalFile(remotePath).toString());
    manifest.insert(QStringLiteral("sha256"), QString::fromLatin1(sha256Hex(remoteBytes)));
    manifest.insert(QStringLiteral("size_bytes"), remoteBytes.size());
    manifest.insert(QStringLiteral("published_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("app_min_version"), QStringLiteral("0.1.0"));
    if (!writeTextFile(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented))) {
        return 3;
    }

    qputenv("MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL", QUrl::fromLocalFile(manifestPath).toString().toUtf8());

    backend::EModulusLutCatalog catalog;
    QString resolvedPath;
    backend::EModulusLutCatalog::ManagedLutInfo info;
    QString error;
    if (!catalog.ensureManagedLut(bundledPath, &resolvedPath, &info, &error)) {
        std::cerr << "ensureManagedLut failed: " << error.toStdString() << '\n';
        return 4;
    }

    const QString managedPath = backend::EModulusLutCatalog::localLutPath();
    if (resolvedPath != managedPath) {
        std::cerr << "Expected managed path " << managedPath.toStdString() << " but got " << resolvedPath.toStdString() << '\n';
        return 5;
    }
    if (!QFile::exists(managedPath)) {
        std::cerr << "Managed LUT file missing: " << managedPath.toStdString() << '\n';
        return 6;
    }
    if (info.remoteUpdated != true) {
        std::cerr << "Expected remoteUpdated to be true\n";
        return 7;
    }

    backend::EModulusLut lut;
    if (!lut.loadFromFile(managedPath.toStdString())) {
        std::cerr << "Failed to load managed LUT\n";
        return 8;
    }
    const double stiffness = lut.lookup(10.0, 0.2);
    if (std::abs(stiffness - 42.0) > 1e-9) {
        std::cerr << "Unexpected LUT value: " << stiffness << '\n';
        return 9;
    }

    // Break the manifest and ensure the last-known-good local copy is still used.
    const QString badManifestPath = tempPathSuffix(remoteDir, QStringLiteral("broken.json"));
    if (!writeTextFile(badManifestPath, QByteArrayLiteral("{not-json"))) {
        return 10;
    }
    qputenv("MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL", QUrl::fromLocalFile(badManifestPath).toString().toUtf8());

    QString fallbackResolvedPath;
    backend::EModulusLutCatalog::ManagedLutInfo fallbackInfo;
    QString fallbackError;
    if (!catalog.ensureManagedLut(bundledPath, &fallbackResolvedPath, &fallbackInfo, &fallbackError)) {
        std::cerr << "Fallback ensureManagedLut failed: " << fallbackError.toStdString() << '\n';
        return 11;
    }
    if (fallbackResolvedPath != managedPath) {
        std::cerr << "Fallback resolved path changed unexpectedly\n";
        return 12;
    }
    if (!QFile::exists(managedPath)) {
        std::cerr << "Managed LUT disappeared after fallback\n";
        return 13;
    }

    std::error_code ec;
    std::filesystem::remove_all(root.toStdString(), ec);
    return 0;
}
