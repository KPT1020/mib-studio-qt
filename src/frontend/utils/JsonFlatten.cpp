#include "frontend/utils/JsonFlatten.h"

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

QMap<QString, FlattenTable> groupJsonBySections(const QJsonDocument& doc) {
	QMap<QString, FlattenTable> result;

	if (doc.isNull()) {
		return result;
	}

	// Only handle top-level objects for grouping
	if (!doc.isObject()) {
		// For arrays or scalars, put everything in "General" section
		FlattenTable general = flattenJsonForTable(doc);
		if (!general.rows.isEmpty()) {
			// Convert to 3-column format if needed
			if (general.columns.size() == 2) {
				general.columns = {"key", "value", "type"};
				for (auto& row : general.rows) {
					if (row.size() == 2) {
						row.append(""); // Add empty type column
					}
				}
			} else if (general.columns.size() == 1) {
				general.columns = {"key", "value", "type"};
				for (auto& row : general.rows) {
					QString val = row.isEmpty() ? QString() : row[0];
					row = QVector<QString>{"(value)", val, ""};
				}
			}
			result.insert("General", general);
		}
		return result;
	}

	const QJsonObject root = doc.object();
	QMap<QString, QString> generalItems;

	// Process each top-level key as a section
	for (auto it = root.begin(); it != root.end(); ++it) {
		const QString sectionName = it.key();
		const QJsonValue sectionValue = it.value();

		FlattenTable sectionTable;
		
		if (sectionValue.isObject()) {
			// Flatten the nested object
			QMap<QString, QString> flat;
			flattenInto(sectionValue, QString(), flat);
			
			// Create 3-column table: key (relative to section), value, type
			sectionTable.columns = {"key", "value", "type"};
			for (auto flatIt = flat.constBegin(); flatIt != flat.constEnd(); ++flatIt) {
				sectionTable.rows.append(QVector<QString>{
					flatIt.key(),  // relative key path within section
					flatIt.value(), // value
					""              // type (empty for now)
				});
			}
		} else if (sectionValue.isArray()) {
			// For arrays, flatten each element if objects, otherwise treat as single value
			const QJsonArray arr = sectionValue.toArray();
			if (arr.isEmpty()) {
				continue;
			}
			
			const bool allObjects = std::all_of(arr.begin(), arr.end(), 
				[](const QJsonValue& v) { return v.isObject(); });
			
			if (allObjects) {
				// Array of objects - create table with union of keys
				QSet<QString> keySet;
				QVector<QMap<QString, QString>> flattened;
				flattened.reserve(arr.size());
				for (const QJsonValue& v : arr) {
					QMap<QString, QString> flat;
					flattenInto(v.toObject(), QString(), flat);
					for (auto flatIt = flat.constBegin(); flatIt != flat.constEnd(); ++flatIt) {
						keySet.insert(flatIt.key());
					}
					flattened.append(std::move(flat));
				}
				sectionTable.columns = QStringList(keySet.cbegin(), keySet.cend());
				sectionTable.columns.sort(Qt::CaseInsensitive);
				sectionTable.columns.prepend("key");
				sectionTable.columns.append("type");
				
				for (const auto& flatMap : flattened) {
					QVector<QString> row;
					row.append(""); // key column (empty for array elements)
					row.reserve(sectionTable.columns.size());
					for (int i = 1; i < sectionTable.columns.size() - 1; ++i) {
						const QString& col = sectionTable.columns[i];
						row.append(flatMap.value(col));
					}
					row.append(""); // type column
					sectionTable.rows.append(std::move(row));
				}
			} else {
				// Array of scalars - single row
				QStringList items;
				items.reserve(arr.size());
				for (const QJsonValue& v : arr) {
					items << toScalarString(v);
				}
				sectionTable.columns = {"key", "value", "type"};
				sectionTable.rows.append(QVector<QString>{
					"(value)",
					items.join(", "),
					""
				});
			}
		} else {
			// Scalar value - add to general section
			generalItems.insert(sectionName, toScalarString(sectionValue));
		}

		if (!sectionTable.rows.isEmpty()) {
			result.insert(sectionName, sectionTable);
		}
	}

	// Add general section for root-level scalars if any
	if (!generalItems.isEmpty()) {
		FlattenTable generalTable;
		generalTable.columns = {"key", "value", "type"};
		for (auto it = generalItems.constBegin(); it != generalItems.constEnd(); ++it) {
			generalTable.rows.append(QVector<QString>{it.key(), it.value(), ""});
		}
		if (!result.contains("General")) {
			result.insert("General", generalTable);
		} else {
			// Merge with existing General section
			FlattenTable& existing = result["General"];
			for (const auto& row : generalTable.rows) {
				existing.rows.append(row);
			}
		}
	}

	return result;
}

} // namespace frontend::jsonutil





