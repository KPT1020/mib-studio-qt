#include "frontend/JsonFlatten.h"

#include <QMap>
#include <QSet>

namespace frontend::jsonutil {

static QString toScalarString(const QJsonValue& v) {
	if (v.isString()) return v.toString();
	if (v.isBool()) return v.toBool() ? "true" : "false";
	if (v.isDouble()) return QString::number(v.toDouble(), 'g', 15);
	if (v.isNull()) return "null";
	if (v.isUndefined()) return "undefined";
	// Fallback for objects/arrays
	return QString::fromUtf8(QJsonDocument::fromVariant(v.toVariant()).toJson(QJsonDocument::Compact));
}

static void flattenInto(const QJsonValue& value, const QString& prefix, QMap<QString, QString>& out) {
	if (value.isObject()) {
		const QJsonObject obj = value.toObject();
		for (auto it = obj.begin(); it != obj.end(); ++it) {
			const QString key = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
			flattenInto(it.value(), key, out);
		}
		return;
	}
	if (value.isArray()) {
		const QJsonArray arr = value.toArray();
		QStringList items;
		items.reserve(arr.size());
		for (const QJsonValue& e : arr) {
			if (e.isObject() || e.isArray()) {
				items << QString::fromUtf8(QJsonDocument::fromVariant(e.toVariant()).toJson(QJsonDocument::Compact));
			} else {
				items << toScalarString(e);
			}
		}
		out.insert(prefix.isEmpty() ? "(value)" : prefix, items.join(", "));
		return;
	}
	out.insert(prefix.isEmpty() ? "(value)" : prefix, toScalarString(value));
}

FlattenTable flattenJsonForTable(const QJsonDocument& doc) {
	FlattenTable table;

	if (doc.isNull()) {
		return table;
	}

	// Top-level object => one row
	if (doc.isObject()) {
		QMap<QString, QString> flat;
		flattenInto(doc.object(), QString(), flat);
		table.columns = {"key", "value"};
		for (auto it = flat.constBegin(); it != flat.constEnd(); ++it) {
			table.rows.append(QVector<QString>{ it.key(), it.value() });
		}
		return table;
	}

	// Top-level array
	if (doc.isArray()) {
		const QJsonArray arr = doc.array();
		if (arr.isEmpty()) {
			// No rows, no columns
			return table;
		}
		const bool allObjects = std::all_of(arr.begin(), arr.end(), [](const QJsonValue& v) { return v.isObject(); });
		if (allObjects) {
			// Build union of keys
			QSet<QString> keySet;
			QVector<QMap<QString, QString>> flattened;
			flattened.reserve(arr.size());
			for (const QJsonValue& v : arr) {
				QMap<QString, QString> flat;
				flattenInto(v.toObject(), QString(), flat);
				for (auto it = flat.constBegin(); it != flat.constEnd(); ++it) {
					keySet.insert(it.key());
				}
				flattened.append(std::move(flat));
			}
			table.columns = QStringList(keySet.cbegin(), keySet.cend());
			table.columns.sort(Qt::CaseInsensitive);
			// Rows
			for (const auto& flatMap : flattened) {
				QVector<QString> row;
				row.reserve(table.columns.size());
				for (const QString& col : table.columns) {
					row.append(flatMap.value(col));
				}
				table.rows.append(std::move(row));
			}
			return table;
		}

		// Array of scalars or mixed => single value cell as joined string if scalars, else compact JSON
		const bool allScalars = std::all_of(arr.begin(), arr.end(), [](const QJsonValue& v) { return !v.isObject() && !v.isArray(); });
		QString valueStr;
		if (allScalars) {
			QStringList items;
			items.reserve(arr.size());
			for (const QJsonValue& v : arr) items << toScalarString(v);
			valueStr = items.join(", ");
		} else {
			valueStr = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
		}
		table.columns = {"(value)"};
		table.rows = { QVector<QString>{ valueStr } };
		return table;
	}

	// Top-level scalar
	table.columns = {"(value)"};
	table.rows = { QVector<QString>{ QString() } };
	return table;
}

} // namespace frontend::jsonutil





