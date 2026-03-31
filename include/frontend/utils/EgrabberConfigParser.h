#pragma once

#include <QString>
#include <QPoint>
#include <QSize>

namespace frontend
{

    class EgrabberConfigParser
    {
    public:
        // ROI alignment constraints: OffsetX must be multiple of 16, OffsetY must be multiple of 4
        static constexpr int ROI_OFFSET_X_STEP = 16;
        static constexpr int ROI_OFFSET_Y_STEP = 4;

        // ROI size alignment constraints: Width must be multiple of 16, Height must be multiple of 4
        static constexpr int ROI_WIDTH_STEP = 16;
        static constexpr int ROI_HEIGHT_STEP = 4;

        // Read OffsetX and OffsetY from egrabberConfig.js file
        // Returns true if successful, false otherwise
        // If file doesn't exist or parsing fails, offsetX and offsetY are set to default values
        static bool readRoiOffsets(const QString &filePath, int &offsetX, int &offsetY);

        // Update OffsetX and OffsetY in egrabberConfig.js file
        // Values are automatically snapped to alignment constraints
        // Returns true if successful, false otherwise
        static bool updateRoiOffsets(const QString &filePath, int requestedX, int requestedY, QString *errorMsg = nullptr);

        // Get default ROI position
        static QPoint defaultRoiPosition() { return QPoint(704, 500); }

        // Get default ROI size
        static QSize defaultRoiSize() { return QSize(512, 96); }

        // Read Width and Height from egrabberConfig.js file
        static bool readRoiSize(const QString &filePath, int &width, int &height);

        // Update Width and Height in egrabberConfig.js file
        // Values are automatically snapped to alignment constraints
        static bool updateRoiSize(const QString &filePath, int requestedW, int requestedH, QString *errorMsg = nullptr);

    private:
        // Snap a value to the nearest multiple of step, clamping to [0, max]
        static int snapToStep(int value, int step, int max);

        EgrabberConfigParser() = default; // Static utility class
    };

} // namespace frontend
