#include "frontend/utils/FileIOUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>
#include <QPlainTextEdit>

namespace frontend
{

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

} // namespace frontend
