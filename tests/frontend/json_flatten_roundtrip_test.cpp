#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QVector>

#include <cstdio>

#include "frontend/utils/JsonFlatten.h"

namespace {

QString valueToCompactString(const QJsonValue& value)
{
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return QString::fromUtf8(QJsonDocument::fromVariant(value.toVariant()).toJson(QJsonDocument::Compact));
}

bool valuesEqual(const QJsonValue& lhs, const QJsonValue& rhs)
{
    if (lhs.type() != rhs.type()) {
        if (lhs.isDouble() && rhs.isDouble()) {
            return lhs.toDouble() == rhs.toDouble();
        }
        return false;
    }

    if (lhs.isObject()) {
        const QJsonObject lo = lhs.toObject();
        const QJsonObject ro = rhs.toObject();
        if (lo.size() != ro.size()) {
            return false;
        }
        for (auto it = lo.constBegin(); it != lo.constEnd(); ++it) {
            if (!ro.contains(it.key()) || !valuesEqual(it.value(), ro.value(it.key()))) {
                return false;
            }
        }
        return true;
    }

    if (lhs.isArray()) {
        const QJsonArray la = lhs.toArray();
        const QJsonArray ra = rhs.toArray();
        if (la.size() != ra.size()) {
            return false;
        }
        for (int i = 0; i < la.size(); ++i) {
            if (!valuesEqual(la.at(i), ra.at(i))) {
                return false;
            }
        }
        return true;
    }

    if (lhs.isDouble()) {
        return lhs.toDouble() == rhs.toDouble();
    }

    return lhs == rhs;
}

bool roundTripDocument(const QJsonDocument& doc, const char* label)
{
    const auto sections = frontend::jsonutil::groupJsonBySections(doc);
    const QJsonValue rebuilt = frontend::jsonutil::rebuildJsonValueFromSections(sections);

    const QJsonValue original = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    if (!valuesEqual(original, rebuilt)) {
        std::fprintf(stderr, "round-trip mismatch for %s\n", label);
        std::fprintf(stderr, "original: %s\n", valueToCompactString(original).toUtf8().constData());
        std::fprintf(stderr, "rebuilt : %s\n", valueToCompactString(rebuilt).toUtf8().constData());
        return false;
    }
    return true;
}

bool roundTripScalarSection(const QJsonValue& expected, const frontend::jsonutil::FlattenTable& table, const char* label)
{
    QMap<QString, frontend::jsonutil::FlattenTable> sections;
    sections.insert(QStringLiteral("General"), table);

    const QJsonValue rebuilt = frontend::jsonutil::rebuildJsonValueFromSections(sections);
    if (!valuesEqual(expected, rebuilt)) {
        std::fprintf(stderr, "scalar round-trip mismatch for %s\n", label);
        std::fprintf(stderr, "expected: %s\n", valueToCompactString(expected).toUtf8().constData());
        std::fprintf(stderr, "rebuilt : %s\n", valueToCompactString(rebuilt).toUtf8().constData());
        return false;
    }
    return true;
}

QJsonDocument loadJsonFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "failed to open %s\n", path.toUtf8().constData());
        return {};
    }
    const QByteArray data = file.readAll();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        std::fprintf(stderr, "failed to parse %s: %s\n", path.toUtf8().constData(), error.errorString().toUtf8().constData());
        return {};
    }
    return doc;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QJsonDocument nestedDoc(QJsonObject{
        {QStringLiteral("image_processing"), QJsonObject{
            {QStringLiteral("area_threshold_min"), 60},
            {QStringLiteral("area_threshold_max"), 290},
            {QStringLiteral("filters"), QJsonObject{
                {QStringLiteral("enable_area_range_check"), true},
                {QStringLiteral("enable_ring_ratio_check"), false},
            }},
            {QStringLiteral("multi_image"), QJsonObject{
                {QStringLiteral("enabled"), true},
                {QStringLiteral("count"), 4},
            }},
        }},
        {QStringLiteral("buffer_threshold"), 1000},
    });
    if (!roundTripDocument(nestedDoc, "nested object")) {
        return 1;
    }

    const QJsonDocument arrayOfObjects(QJsonArray{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("a")}, {QStringLiteral("enabled"), true}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("b")}, {QStringLiteral("enabled"), false}},
    });
    if (!roundTripDocument(arrayOfObjects, "array of objects")) {
        return 1;
    }

    const QJsonDocument arrayOfScalars(QJsonArray{
        QStringLiteral("alpha"),
        QStringLiteral("beta"),
        QStringLiteral("gamma"),
    });
    if (!roundTripDocument(arrayOfScalars, "array of scalars")) {
        return 1;
    }

    const frontend::jsonutil::FlattenTable scalarTable{
        QStringList{QStringLiteral("key"), QStringLiteral("value"), QStringLiteral("type")},
        {QVector<QString>{QStringLiteral("(value)"), QStringLiteral("root-scalar"), QString()}}
    };
    if (!roundTripScalarSection(QJsonValue(QStringLiteral("root-scalar")), scalarTable, "root scalar")) {
        return 1;
    }

    const QString defaultsPath = QString::fromUtf8(MIB_PROJECT_SOURCE_DIR "/resources/defaults/config.json");
    const QJsonDocument defaultsDoc = loadJsonFile(defaultsPath);
    if (defaultsDoc.isNull()) {
        return 1;
    }
    if (!roundTripDocument(defaultsDoc, "resources/defaults/config.json")) {
        return 1;
    }

    return 0;
}
