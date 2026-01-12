#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QVector>
#include <QMap>

namespace frontend::jsonutil {

struct FlattenTable {
	QStringList columns;
	QVector<QVector<QString>> rows;
};

// Flattens arbitrary JSON into a tabular representation.
// - Top-level object => one row, dot-path columns.
// - Top-level array of objects => one row per element, union of columns.
// - Other arrays => joined string in a single "(value)" column.
// - Scalars => single "(value)" column.
FlattenTable flattenJsonForTable(const QJsonDocument& doc);

// Groups JSON by top-level object keys, returning a map of section name -> flattened table.
// Root-level scalar values are grouped into a "General" section.
// Each section's table has 3 columns: key (relative to section), value, and type (empty for now).
QMap<QString, FlattenTable> groupJsonBySections(const QJsonDocument& doc);

} // namespace frontend::jsonutil





