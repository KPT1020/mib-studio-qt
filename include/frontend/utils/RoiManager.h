#pragma once

#include <QObject>
#include <QPointF>
#include <QString>

namespace frontend
{

    class RoiManager : public QObject
    {
        Q_OBJECT
    public:
        explicit RoiManager(QObject *parent = nullptr);

        // Get current ROI position
        QPointF position() const { return position_; }

        // Set ROI position (will be snapped to alignment constraints)
        void setPosition(const QPointF &position);

        // Initialize ROI position from egrabberConfig.js
        bool initializeFromConfig(const QString &configPath);

        // Update egrabberConfig.js with current ROI position
        bool updateConfig(const QString &configPath, QString *errorMsg = nullptr);

    signals:
        void positionChanged(const QPointF &newPosition);

    private:
        QPointF position_;
    };

} // namespace frontend
