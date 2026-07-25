#pragma once

#include <QString>
#include <QWidget>

#include <array>

class QToolButton;

namespace frontend {

// Persistent active-context bar (UX-8, issue #312): profile, camera,
// calibration, operator, storage, and system status are always visible under
// the workflow stage bar. Every segment is text + glyph (never color alone),
// exposes details in its tooltip/accessible name, and activates a navigation
// action in MainWindow via segmentActivated(id).
class ContextBar : public QWidget {
    Q_OBJECT
public:
    struct Data {
        // Experiment Profile
        QString profileName;
        bool profileSelected = false;
        bool profileDirty = false;
        bool profileIncompatible = false;
        bool profileApplied = false;
        bool profileVerified = false;
        // Camera
        QString cameraLabel;
        bool cameraConfigured = false;
        bool mockCamera = false;
        bool captureRunning = false;
        // Calibration
        double pixelToMicron = 0.0;
        // Operator
        QString operatorName;
        // Storage
        bool storageWritable = true;
        double storageFreeGb = 0.0;
        // System status
        QString statusText;    // e.g. recommended action or run state
        int warningCount = 0;  // non-passed checks across surfaces
        bool experimentActive = false;
        bool blocked = false;  // a blocking condition exists
    };

    explicit ContextBar(QWidget* parent = nullptr);

    void updateData(const Data& data);

signals:
    // ids: "profile", "camera", "calibration", "operator", "storage", "status"
    void segmentActivated(const QString& segmentId);

private:
    QToolButton* segment(int index) const;
    std::array<QToolButton*, 6> segments_{};
};

} // namespace frontend
