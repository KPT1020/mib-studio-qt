#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>

namespace backend::services { struct FilterResult; }

namespace frontend::hdfreviewexport {

QString sourceBaseName(const QString& hdfPath);
QString metricsCsvPath(const QString& hdfPath, const QString& exportDir);
QString exportAllDirectoryPath(const QString& hdfPath, const QString& exportRootDir);
QStringList batchMetricsCsvPaths(const QStringList& hdfPaths, const QString& exportDir);
QStringList batchExportAllDirectoryPaths(const QStringList& hdfPaths, const QString& exportRootDir);

// Metrics CSV serialization. Kept here (rather than inline in HdfReviewTab) so
// the exact column set — including Young's modulus — is unit-testable without
// instantiating the Qt widget. The header and rows are always produced by the
// same pair of functions so they cannot drift.
QString metricsCsvHeader();
// conversionFactor is pixels-per-micron; the row emits area in both px and µm².
// A NaN Young's modulus (query outside LUT coverage) is written as an empty
// field rather than the literal "nan".
QString metricsCsvRow(const QString& frameType,
                      const backend::services::FilterResult& validation,
                      quint64 index,
                      quint64 timestampNs,
                      double conversionFactor);

} // namespace frontend::hdfreviewexport
