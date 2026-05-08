#pragma once

#include <QString>

class QPlainTextEdit;

namespace frontend
{

    class FileIOUtils
    {
    public:
        // Load text file content into a plain text editor
        static bool loadFileToEditor(const QString &path, QPlainTextEdit *editor, QString *err = nullptr);

        // Save plain text editor content to a file
        static bool saveEditorToFile(QPlainTextEdit *editor, const QString &path, QString *err = nullptr);

        // Load text file content as string
        static bool loadTextFile(const QString &path, QString &content, QString *err = nullptr);

        // Save string content to a text file
        static bool saveTextFile(const QString &path, const QString &content, QString *err = nullptr);

        // Ensure a default file exists by copying from a resource if it doesn't exist
        static bool ensureDefaultsFile(const QString &targetPath, const QString &resourceName, QString *err = nullptr);

        // Ensure a default JSON file exists and add any keys introduced by newer bundled defaults.
        // Existing user values are preserved; only missing object keys are inserted recursively.
        static bool ensureDefaultJsonFile(const QString &targetPath, const QString &resourceName, QString *err = nullptr);

    private:
        FileIOUtils() = default; // Static utility class
    };

} // namespace frontend
