#include "frontend/utils/HdfReviewExportPaths.h"

#include "backend/processing/ProcessingService.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <cmath>

namespace frontend::hdfreviewexport {
namespace {

QString reservationKey(const QString& path)
{
    QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    clean = clean.toCaseFolded();
#endif
    return clean;
}

bool isAvailable(const QString& path, const QSet<QString>& reserved)
{
    return !QFileInfo::exists(path) && !reserved.contains(reservationKey(path));
}

QString uniquePath(const QString& dirPath,
                   const QString& firstName,
                   const QString& numberedPattern,
                   QSet<QString>& reserved)
{
    const QDir dir(dirPath);
    QString candidate = dir.filePath(firstName);
    if (isAvailable(candidate, reserved)) {
        reserved.insert(reservationKey(candidate));
        return candidate;
    }

    for (int suffix = 2; suffix < 1000000; ++suffix) {
        candidate = dir.filePath(numberedPattern.arg(suffix));
        if (isAvailable(candidate, reserved)) {
            reserved.insert(reservationKey(candidate));
            return candidate;
        }
    }

    reserved.insert(reservationKey(candidate));
    return candidate;
}

} // namespace

QString sourceBaseName(const QString& hdfPath)
{
    const QFileInfo info(hdfPath);
    QString base = info.completeBaseName().trimmed();
    if (base.isEmpty()) {
        base = QStringLiteral("hdf_export");
    }
    return base;
}

QString metricsCsvPath(const QString& hdfPath, const QString& exportDir)
{
    QSet<QString> reserved;
    const QString base = sourceBaseName(hdfPath);
    return uniquePath(exportDir,
                      QStringLiteral("%1_metrics.csv").arg(base),
                      QStringLiteral("%1_metrics_%2.csv").arg(base),
                      reserved);
}

QString exportAllDirectoryPath(const QString& hdfPath, const QString& exportRootDir)
{
    QSet<QString> reserved;
    const QString base = sourceBaseName(hdfPath);
    return uniquePath(exportRootDir,
                      base,
                      QStringLiteral("%1_%2").arg(base),
                      reserved);
}

QStringList batchMetricsCsvPaths(const QStringList& hdfPaths, const QString& exportDir)
{
    QSet<QString> reserved;
    QStringList results;
    results.reserve(hdfPaths.size());
    for (const QString& hdfPath : hdfPaths) {
        const QString base = sourceBaseName(hdfPath);
        results.push_back(uniquePath(exportDir,
                                     QStringLiteral("%1_metrics.csv").arg(base),
                                     QStringLiteral("%1_metrics_%2.csv").arg(base),
                                     reserved));
    }
    return results;
}

QStringList batchExportAllDirectoryPaths(const QStringList& hdfPaths, const QString& exportRootDir)
{
    QSet<QString> reserved;
    QStringList results;
    results.reserve(hdfPaths.size());
    for (const QString& hdfPath : hdfPaths) {
        const QString base = sourceBaseName(hdfPath);
        results.push_back(uniquePath(exportRootDir,
                                     base,
                                     QStringLiteral("%1_%2").arg(base),
                                     reserved));
    }
    return results;
}

QString metricsCsvHeader()
{
    return QStringLiteral(
        "Frame Type,Index,Timestamp,Object Id,Object Count,Track Id,Track First,Track Last,Track Observations,"
        "Deformability,Area,Area (um\xc2\xb2),Area Ratio,Ring Ratio,"
        "Valid,Touches Border,Single Inner,In Range,Inner Count,"
        "Bright Q1,Bright Q2,Bright Q3,Bright Q4,Young's Modulus (kPa)");
}

QString metricsCsvRow(const QString& frameType,
                      const backend::services::FilterResult& val,
                      quint64 index,
                      quint64 timestampNs,
                      double conversionFactor)
{
    const double areaConversionFactor = conversionFactor * conversionFactor;
    const double areaMicrons = val.area * areaConversionFactor;
    const QString youngsModulus = std::isnan(val.youngsModulus)
        ? QString()
        : QString::number(val.youngsModulus, 'f', 3);

    QString row;
    QTextStream out(&row);
    out << frameType << ",";
    out << index << ",";
    out << timestampNs << ",";
    out << val.objectId << ",";
    out << val.objectCount << ",";
    out << val.trackId << ",";
    out << val.trackFirstFrame << ",";
    out << val.trackLastFrame << ",";
    out << val.trackObservationCount << ",";
    out << QString::number(val.deformability, 'f', 3) << ",";
    out << QString::number(val.area, 'f', 2) << ",";
    out << QString::number(areaMicrons, 'f', 2) << ",";
    out << QString::number(val.areaRatio, 'f', 3) << ",";
    out << QString::number(val.ringRatio, 'f', 3) << ",";
    out << (val.isValid ? "Yes" : "No") << ",";
    out << (val.touchesBorder ? "Yes" : "No") << ",";
    out << (val.hasSingleInnerContour ? "Yes" : "No") << ",";
    out << (val.inRange ? "Yes" : "No") << ",";
    out << val.innerContourCount << ",";
    out << QString::number(val.brightness.q1, 'f', 2) << ",";
    out << QString::number(val.brightness.q2, 'f', 2) << ",";
    out << QString::number(val.brightness.q3, 'f', 2) << ",";
    out << QString::number(val.brightness.q4, 'f', 2) << ",";
    out << youngsModulus;
    return row;
}

} // namespace frontend::hdfreviewexport
