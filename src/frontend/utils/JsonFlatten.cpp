#include "frontend/utils/JsonFlatten.h"

#include <algorithm>

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

static QJsonValue parseValueFromString(const QString& text) {
	const QString t = text.trimmed();
	if (t == "true") return QJsonValue(true);
	if (t == "false") return QJsonValue(false);
	if (t == "null" || t == "undefined") return QJsonValue();

	bool ok = false;
	const double d = t.toDouble(&ok);
	if (ok && !t.isEmpty()) {
		return QJsonValue(d);
	}

	if (!t.isEmpty() && (t.front() == '{' || t.front() == '[')) {
		const QJsonParseError err = [&]() {
			QJsonParseError parseError;
			if (t.front() == '[') {
				const QByteArray wrapped = QByteArrayLiteral("{\"__value__\":") + t.toUtf8() + QByteArrayLiteral("}");
				(void)QJsonDocument::fromJson(wrapped, &parseError);
			} else {
				(void)QJsonDocument::fromJson(t.toUtf8(), &parseError);
			}
			return parseError;
		}();
		if (err.error == QJsonParseError::NoError) {
			const QJsonDocument doc = (t.front() == '[')
				? QJsonDocument::fromJson(QByteArrayLiteral("{\"__value__\":") + t.toUtf8() + QByteArrayLiteral("}"))
				: QJsonDocument::fromJson(t.toUtf8());
			if (t.front() == '[') {
				return doc.object().value("__value__");
			}
			if (doc.isObject()) return QJsonValue(doc.object());
			if (doc.isArray()) return QJsonValue(doc.array());
		}
	}

	return QJsonValue(t);
}

static void setObjectValueByPath(QJsonObject& obj, const QString& path, const QJsonValue& value) {
	const QStringList parts = path.split('.', Qt::SkipEmptyParts);
	std::function<void(QJsonObject&, int)> setByIndex = [&](QJsonObject& node, int idx) {
		if (idx >= parts.size()) return;
		const QString& key = parts.at(idx);
		if (idx == parts.size() - 1) {
			node.insert(key, value);
			return;
		}
		QJsonObject child = node.value(key).toObject();
		setByIndex(child, idx + 1);
		node.insert(key, child);
	};
	setByIndex(obj, 0);
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
		if (arr.size() > 0) {
			out.insert(prefix.isEmpty() ? "(value)" : prefix,
			           QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
		} else {
			out.insert(prefix.isEmpty() ? "(value)" : prefix, "[]");
		}
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
			valueStr = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
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
			bool handled = false;
			if (doc.isArray()) {
				const QJsonArray arr = doc.array();
				if (std::all_of(arr.begin(), arr.end(), [](const QJsonValue& v) { return v.isObject(); })) {
					// Preserve arrays of objects as array rows, not key/value rows.
					general.columns.prepend("key");
					general.columns.append("type");
					for (auto& row : general.rows) {
						row.prepend("");
						row.append("");
					}
					handled = true;
				}
			}
			if (!handled && general.columns.size() == 2) {
				general.columns = {"key", "value", "type"};
				for (auto& row : general.rows) {
					if (row.size() == 2) {
						row.append(""); // Add empty type column
					}
				}
				handled = true;
			}
			if (!handled && general.columns.size() == 1) {
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
				const QString valueStr = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
				sectionTable.columns = {"key", "value", "type"};
				sectionTable.rows.append(QVector<QString>{
					"(value)",
					valueStr,
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

static QJsonValue rebuildSectionValue(const QString& sectionName, const FlattenTable& table, bool* consumedWholeSection)
{
	if (consumedWholeSection) {
		*consumedWholeSection = false;
	}

	if (table.columns.size() >= 2 && table.columns.at(0) == "key" && table.columns.at(1) == "value") {
		if (sectionName == "General" && table.rows.size() == 1 && table.rows.first().size() >= 2 && table.rows.first().at(0) == "(value)") {
			if (consumedWholeSection) {
				*consumedWholeSection = true;
			}
			return parseValueFromString(table.rows.first().at(1));
		}

		QJsonObject sectionObj;
		for (const auto& r : table.rows) {
			if (r.size() < 2) continue;
			const QString keyPath = r.at(0);
			const QString valStr = r.at(1);
			if (!keyPath.isEmpty() && keyPath != "(value)") {
				setObjectValueByPath(sectionObj, keyPath, parseValueFromString(valStr));
			} else if (keyPath == "(value)") {
				if (consumedWholeSection) {
					*consumedWholeSection = true;
				}
				return parseValueFromString(valStr);
			}
		}
		return sectionObj;
	}

	if (table.columns.size() > 2) {
		QJsonArray arr;
		for (const auto& r : table.rows) {
			QJsonObject obj;
			for (int c = 0; c < table.columns.size() && c < r.size(); ++c) {
				const QString keyPath = table.columns.at(c);
				const QString valStr = r.at(c);
				if (!keyPath.isEmpty() && keyPath != "key" && keyPath != "type") {
					setObjectValueByPath(obj, keyPath, parseValueFromString(valStr));
				}
			}
			if (!obj.isEmpty()) {
				arr.append(obj);
			}
		}
		if (sectionName == "General" && consumedWholeSection) {
			*consumedWholeSection = true;
		}
		return arr;
	}

	return QJsonValue();
}

QJsonValue rebuildJsonValueFromSections(const QMap<QString, FlattenTable>& sections)
{
	if (sections.isEmpty()) {
		return QJsonValue();
	}

	QJsonObject rootObject;
	bool rootValueSet = false;
	QJsonValue rootValue;

	for (auto it = sections.constBegin(); it != sections.constEnd(); ++it) {
		const QString& sectionName = it.key();
		const FlattenTable& table = it.value();
		bool consumedWholeSection = false;
		const QJsonValue sectionValue = rebuildSectionValue(sectionName, table, &consumedWholeSection);

		if (sectionName == "General" && consumedWholeSection) {
			if (rootObject.isEmpty() && !rootValueSet) {
				rootValue = sectionValue;
				rootValueSet = true;
			} else {
				rootObject.insert(sectionName, sectionValue);
			}
			continue;
		}

		if (sectionName == "General" && sectionValue.isObject()) {
			const QJsonObject sectionObj = sectionValue.toObject();
			for (auto it = sectionObj.constBegin(); it != sectionObj.constEnd(); ++it) {
				rootObject.insert(it.key(), it.value());
			}
			continue;
		}

		if (!sectionValue.isUndefined()) {
			rootObject.insert(sectionName, sectionValue);
		}
	}

	if (rootObject.isEmpty() && rootValueSet) {
		return rootValue;
	}

	return rootObject;
}

QJsonDocument rebuildJsonDocumentFromSections(const QMap<QString, FlattenTable>& sections)
{
	const QJsonValue value = rebuildJsonValueFromSections(sections);
	if (value.isObject()) {
		return QJsonDocument(value.toObject());
	}
	if (value.isArray()) {
		return QJsonDocument(value.toArray());
	}
	return QJsonDocument();
}

} // namespace frontend::jsonutil
