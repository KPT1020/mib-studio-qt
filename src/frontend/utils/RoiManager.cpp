#include "frontend/utils/RoiManager.h"
#include "frontend/utils/EgrabberConfigParser.h"
#include "frontend/utils/ConfigPathManager.h"
#include "frontend/utils/FileIOUtils.h"

#include <QPoint>
#include <QPointF>
#include <QString>
#include <spdlog/spdlog.h>

namespace frontend
{

    RoiManager::RoiManager(QObject *parent)
        : QObject(parent)
    {
        // Initialize with default position
        QPoint defaultPos = EgrabberConfigParser::defaultRoiPosition();
        position_ = QPointF(defaultPos);
    }

    void RoiManager::setPosition(const QPointF &position)
    {
        position_ = position;
        emit positionChanged(position_);
    }

    bool RoiManager::initializeFromConfig(const QString &configPath)
    {
        int offsetX, offsetY;
        if (EgrabberConfigParser::readRoiOffsets(configPath, offsetX, offsetY))
        {
            position_ = QPointF(offsetX, offsetY);
            return true;
        }
        else
        {
            // Use default position
            QPoint defaultPos = EgrabberConfigParser::defaultRoiPosition();
            position_ = QPointF(defaultPos);
            return false;
        }
    }

    bool RoiManager::updateConfig(const QString &configPath, QString *errorMsg)
    {
        // Ensure default file exists if using default path
        QString defaultPath = ConfigPathManager::getIncludePath("egrabberConfig.js");
        if (configPath == defaultPath)
        {
            QString err;
            if (!FileIOUtils::ensureDefaultsFile(configPath, ":/defaults/egrabberConfig.js", &err))
            {
                SPDLOG_WARN("ensureDefaultsFile(egrabberConfig.js) failed: {}", err.toStdString());
            }
        }

        int requestedX = static_cast<int>(std::round(position_.x()));
        int requestedY = static_cast<int>(std::round(position_.y()));

        return EgrabberConfigParser::updateRoiOffsets(configPath, requestedX, requestedY, errorMsg);
    }

} // namespace frontend
