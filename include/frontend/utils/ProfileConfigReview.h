#pragma once

#include <QJsonDocument>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace frontend::configreview {

struct ReviewRow {
    QString setting;
    QString currentValue;
    QString profileValue;
    QString section;
    bool changed = false;
};

struct ReviewResult {
    QVector<ReviewRow> rows;
    int changedCount = 0;
    int totalCount = 0;
};

ReviewResult buildReviewRows(const QJsonDocument& currentDoc, const QJsonDocument& profileDoc);
QString valueToDisplayString(const QJsonValue& value);

} // namespace frontend::configreview
