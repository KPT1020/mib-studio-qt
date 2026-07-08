#include "frontend/utils/ProfileConfigReview.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QStringList>

namespace frontend::configreview {
namespace {

void flattenValue(const QJsonValue& value, const QString& path, QMap<QString, QJsonValue>& out)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty() && !path.isEmpty()) {
            out.insert(path, value);
            return;
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString childPath = path.isEmpty() ? it.key() : path + QStringLiteral(".") + it.key();
            flattenValue(it.value(), childPath, out);
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty() && !path.isEmpty()) {
            out.insert(path, value);
            return;
        }
        for (int i = 0; i < array.size(); ++i) {
            const QString childPath = path.isEmpty()
                ? QStringLiteral("[%1]").arg(i)
                : QStringLiteral("%1[%2]").arg(path).arg(i);
            flattenValue(array.at(i), childPath, out);
        }
        return;
    }

    out.insert(path.isEmpty() ? QStringLiteral("(value)") : path, value);
}

QString sectionForPath(const QString& path)
{
    if (path.isEmpty() || path == QStringLiteral("(value)")) {
        return QStringLiteral("General");
    }

    const int dot = path.indexOf(QLatin1Char('.'));
    const int bracket = path.indexOf(QLatin1Char('['));
    int end = -1;
    if (dot >= 0 && bracket >= 0) {
        end = std::min(dot, bracket);
    } else {
        end = std::max(dot, bracket);
    }

    const QString section = end > 0 ? path.left(end) : path;
    return section.isEmpty() ? QStringLiteral("General") : section;
}

bool valuesDiffer(const QJsonValue& lhs, const QJsonValue& rhs)
{
    if (lhs.type() != rhs.type()) {
        return true;
    }
    return lhs != rhs;
}

} // namespace

QString valueToDisplayString(const QJsonValue& value)
{
    if (value.isUndefined()) {
        return QStringLiteral("<missing>");
    }
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

ReviewResult buildReviewRows(const QJsonDocument& currentDoc, const QJsonDocument& profileDoc)
{
    QMap<QString, QJsonValue> currentFlat;
    QMap<QString, QJsonValue> profileFlat;

    if (currentDoc.isObject()) {
        flattenValue(QJsonValue(currentDoc.object()), QString(), currentFlat);
    } else if (currentDoc.isArray()) {
        flattenValue(QJsonValue(currentDoc.array()), QString(), currentFlat);
    }

    if (profileDoc.isObject()) {
        flattenValue(QJsonValue(profileDoc.object()), QString(), profileFlat);
    } else if (profileDoc.isArray()) {
        flattenValue(QJsonValue(profileDoc.array()), QString(), profileFlat);
    }

    QSet<QString> allPaths;
    for (auto it = currentFlat.constBegin(); it != currentFlat.constEnd(); ++it) {
        allPaths.insert(it.key());
    }
    for (auto it = profileFlat.constBegin(); it != profileFlat.constEnd(); ++it) {
        allPaths.insert(it.key());
    }

    QStringList sortedPaths = QStringList(allPaths.cbegin(), allPaths.cend());
    std::sort(sortedPaths.begin(), sortedPaths.end(), [](const QString& lhs, const QString& rhs) {
        return lhs.toLower() < rhs.toLower();
    });

    ReviewResult result;
    result.totalCount = sortedPaths.size();
    result.rows.reserve(sortedPaths.size());

    for (const QString& path : sortedPaths) {
        const bool hasCurrent = currentFlat.contains(path);
        const bool hasProfile = profileFlat.contains(path);
        const QJsonValue currentValue = hasCurrent ? currentFlat.value(path) : QJsonValue(QJsonValue::Undefined);
        const QJsonValue profileValue = hasProfile ? profileFlat.value(path) : QJsonValue(QJsonValue::Undefined);
        const bool changed = hasCurrent != hasProfile || valuesDiffer(currentValue, profileValue);
        if (changed) {
            ++result.changedCount;
        }
        result.rows.append(ReviewRow{path,
                                     valueToDisplayString(currentValue),
                                     valueToDisplayString(profileValue),
                                     sectionForPath(path),
                                     changed});
    }

    return result;
}

} // namespace frontend::configreview
