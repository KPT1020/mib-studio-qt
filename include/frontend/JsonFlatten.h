#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QVector>

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

} // namespace frontend::jsonutil





