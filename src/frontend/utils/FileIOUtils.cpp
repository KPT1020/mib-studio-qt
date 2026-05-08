#include "frontend/utils/FileIOUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>
#include <QPlainTextEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>

namespace frontend
{

    namespace
    {
        bool mergeMissingJsonObjectKeys(QJsonObject &target, const QJsonObject &defaults)
        {
            bool changed = false;
            for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it)
            {
                if (!target.contains(it.key()))
                {
                    target.insert(it.key(), it.value());
                    changed = true;
                    continue;
                }

                if (target.value(it.key()).isObject() && it.value().isObject())
                {
                    QJsonObject targetChild = target.value(it.key()).toObject();
                    const QJsonObject defaultChild = it.value().toObject();
                    if (mergeMissingJsonObjectKeys(targetChild, defaultChild))
                    {
                        target.insert(it.key(), targetChild);
                        changed = true;
                    }
                }
            }
            return changed;
        }
    }

    bool FileIOUtils::loadFileToEditor(const QString &path, QPlainTextEdit *editor, QString *err)
    {
        if (!editor)
        {
            if (err)
                *err = "Editor is null";
            return false;
        }

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (err)
                *err = f.errorString();
            return false;
        }
        QTextStream in(&f);
        const bool blocked = editor->blockSignals(true);
        editor->setPlainText(in.readAll());
        editor->blockSignals(blocked);
        return true;
    }

    bool FileIOUtils::saveEditorToFile(QPlainTextEdit *editor, const QString &path, QString *err)
    {
        if (!editor)
        {
            if (err)
                *err = "Editor is null";
            return false;
        }

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            if (err)
                *err = f.errorString();
            return false;
        }
        QTextStream out(&f);
        out << editor->toPlainText();
        return true;
    }

    bool FileIOUtils::loadTextFile(const QString &path, QString &content, QString *err)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (err)
                *err = f.errorString();
            return false;
        }
        QTextStream in(&f);
        content = in.readAll();
        return true;
    }

    bool FileIOUtils::saveTextFile(const QString &path, const QString &content, QString *err)
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            if (err)
                *err = f.errorString();
            return false;
        }
        QTextStream out(&f);
        out << content;
        return true;
    }

    bool FileIOUtils::ensureDefaultsFile(const QString &targetPath, const QString &resourceName, QString *err)
    {
        QFileInfo fi(targetPath);
        QDir dir(fi.absolutePath());
        if (!dir.exists())
        {
            if (!dir.mkpath("."))
            {
                if (err)
                    *err = QObject::tr("Failed to create directory: %1").arg(dir.absolutePath());
                return false;
            }
        }
        if (QFile::exists(targetPath))
        {
            return true;
        }
        QFile res(resourceName);
        if (!res.open(QIODevice::ReadOnly))
        {
            if (err)
                *err = QObject::tr("Failed to open resource: %1").arg(resourceName);
            return false;
        }
        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            if (err)
                *err = QObject::tr("Failed to create: %1").arg(targetPath);
            return false;
        }
        const QByteArray data = res.readAll();
        if (out.write(data) != data.size())
        {
            if (err)
                *err = QObject::tr("Failed to write: %1").arg(targetPath);
            return false;
        }
        return true;
    }


    bool FileIOUtils::ensureDefaultJsonFile(const QString &targetPath, const QString &resourceName, QString *err)
    {
        if (!ensureDefaultsFile(targetPath, resourceName, err))
        {
            return false;
        }

        QFile res(resourceName);
        if (!res.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (err)
                *err = QObject::tr("Failed to open resource: %1").arg(resourceName);
            return false;
        }

        QJsonParseError defaultParseError;
        const QJsonDocument defaultDoc = QJsonDocument::fromJson(res.readAll(), &defaultParseError);
        if (defaultParseError.error != QJsonParseError::NoError || !defaultDoc.isObject())
        {
            if (err)
                *err = QObject::tr("Default JSON is invalid: %1").arg(defaultParseError.errorString());
            return false;
        }

        QFile target(targetPath);
        if (!target.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (err)
                *err = target.errorString();
            return false;
        }

        QJsonParseError targetParseError;
        const QJsonDocument targetDoc = QJsonDocument::fromJson(target.readAll(), &targetParseError);
        target.close();
        if (targetParseError.error != QJsonParseError::NoError || !targetDoc.isObject())
        {
            if (err)
                *err = QObject::tr("Existing JSON is invalid: %1").arg(targetParseError.errorString());
            return false;
        }

        QJsonObject merged = targetDoc.object();
        if (!mergeMissingJsonObjectKeys(merged, defaultDoc.object()))
        {
            return true;
        }

        if (!target.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            if (err)
                *err = target.errorString();
            return false;
        }

        const QByteArray data = QJsonDocument(merged).toJson(QJsonDocument::Indented);
        if (target.write(data) != data.size())
        {
            if (err)
                *err = QObject::tr("Failed to write: %1").arg(targetPath);
            return false;
        }

        return true;
    }

} // namespace frontend
