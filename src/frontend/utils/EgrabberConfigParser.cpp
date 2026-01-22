#include "frontend/utils/EgrabberConfigParser.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>
#include <spdlog/spdlog.h>

namespace frontend
{

    int EgrabberConfigParser::snapToStep(int value, int step, int max)
    {
        int snapped = (value / step) * step;
        if (snapped > max)
            snapped = (max / step) * step; // Clamp to max aligned value
        if (snapped < 0)
            snapped = 0;
        return snapped;
    }

    bool EgrabberConfigParser::readRoiOffsets(const QString &filePath, int &offsetX, int &offsetY)
    {
        QFile file(filePath);

        if (!file.exists())
        {
            QPoint defaultPos = defaultRoiPosition();
            offsetX = defaultPos.x();
            offsetY = defaultPos.y();
            return false;
        }

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            SPDLOG_WARN("Failed to read egrabberConfig.js for ROI initialization: {}", filePath.toStdString());
            QPoint defaultPos = defaultRoiPosition();
            offsetX = defaultPos.x();
            offsetY = defaultPos.y();
            return false;
        }

        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        // Extract OffsetY and OffsetX from content (only non-commented lines)
        QRegularExpression reOffsetY(R"(^\s*g\.RemotePort\.set\("OffsetY",\s*(\d+)\);)", QRegularExpression::MultilineOption);
        QRegularExpression reOffsetX(R"(^\s*g\.RemotePort\.set\("OffsetX",\s*(\d+)\);)", QRegularExpression::MultilineOption);

        // Split content into lines and find first non-commented match
        QStringList contentLines = content.split('\n');
        QRegularExpressionMatch matchY;
        QRegularExpressionMatch matchX;

        for (const QString &line : contentLines)
        {
            // Skip commented lines
            if (line.trimmed().startsWith("//"))
            {
                continue;
            }

            QRegularExpressionMatch mY = reOffsetY.match(line);
            if (mY.hasMatch() && !matchY.hasMatch())
            {
                matchY = mY;
            }

            QRegularExpressionMatch mX = reOffsetX.match(line);
            if (mX.hasMatch() && !matchX.hasMatch())
            {
                matchX = mX;
            }
        }

        QPoint defaultPos = defaultRoiPosition();
        offsetY = defaultPos.y(); // default
        offsetX = defaultPos.x(); // default

        if (matchY.hasMatch())
        {
            offsetY = matchY.captured(1).toInt();
        }

        if (matchX.hasMatch())
        {
            offsetX = matchX.captured(1).toInt();
        }

        // Snap to alignment constraints if needed
        const int maxOffsetX = 10000; // Large enough for typical cameras
        const int maxOffsetY = 10000;
        int snappedX = snapToStep(offsetX, ROI_OFFSET_X_STEP, maxOffsetX);
        int snappedY = snapToStep(offsetY, ROI_OFFSET_Y_STEP, maxOffsetY);

        // If offsets were invalid, update them
        if (snappedX != offsetX || snappedY != offsetY)
        {
            SPDLOG_WARN("egrabberConfig.js contained invalid ROI offsets: requested=({},{}) snapped=({},{})",
                        offsetX, offsetY, snappedX, snappedY);
            offsetX = snappedX;
            offsetY = snappedY;
        }

        return true;
    }

    bool EgrabberConfigParser::updateRoiOffsets(const QString &filePath, int requestedX, int requestedY, QString *errorMsg)
    {
        QFile file(filePath);

        if (!file.exists())
        {
            if (errorMsg)
                *errorMsg = QString("egrabberConfig.js not found at %1").arg(filePath);
            return false;
        }

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMsg)
                *errorMsg = QString("Failed to open egrabberConfig.js for reading: %1").arg(filePath);
            return false;
        }

        QTextStream in(&file);
        QStringList lines = in.readAll().split('\n');
        file.close();

        // Snap to alignment constraints (X step=16, Y step=4)
        const int maxOffsetX = 10000; // Large enough for typical cameras
        const int maxOffsetY = 10000;
        int offsetY = snapToStep(requestedY, ROI_OFFSET_Y_STEP, maxOffsetY);
        int offsetX = snapToStep(requestedX, ROI_OFFSET_X_STEP, maxOffsetX);

        // Match only non-commented lines (lines that don't start with // or have // before the pattern)
        QRegularExpression reOffsetY(R"(^\s*g\.RemotePort\.set\("OffsetY",\s*(\d+)\);)");
        QRegularExpression reOffsetX(R"(^\s*g\.RemotePort\.set\("OffsetX",\s*(\d+)\);)");

        // Search through all lines to find and update OffsetY and OffsetX (only non-commented lines)
        for (int i = 0; i < lines.size(); ++i)
        {
            QString line = lines[i];
            // Skip commented lines
            if (line.trimmed().startsWith("//"))
            {
                continue;
            }

            QRegularExpressionMatch matchY = reOffsetY.match(line);
            if (matchY.hasMatch())
            {
                lines[i] = QString("g.RemotePort.set(\"OffsetY\", %1);").arg(offsetY);
            }

            QRegularExpressionMatch matchX = reOffsetX.match(line);
            if (matchX.hasMatch())
            {
                lines[i] = QString("g.RemotePort.set(\"OffsetX\", %1);").arg(offsetX);
            }
        }

        // Write back to file
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            if (errorMsg)
                *errorMsg = QString("Failed to open egrabberConfig.js for writing: %1").arg(filePath);
            return false;
        }

        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        for (int i = 0; i < lines.size(); ++i)
        {
            out << lines[i];
            if (i < lines.size() - 1)
                out << '\n';
        }
        file.close();

        if (requestedX != offsetX || requestedY != offsetY)
        {
            SPDLOG_INFO("Updated egrabberConfig.js at {}: requested=({},{}) snapped=({},{})",
                        filePath.toStdString(), requestedX, requestedY, offsetX, offsetY);
        }
        else
        {
            SPDLOG_INFO("Updated egrabberConfig.js at {}: OffsetX={}, OffsetY={}", filePath.toStdString(), offsetX, offsetY);
        }

        return true;
    }

} // namespace frontend
