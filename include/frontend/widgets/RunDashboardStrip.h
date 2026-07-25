#pragma once

#include <QWidget>

#include <cstdint>

class QLabel;

namespace frontend {

// Compact run header + key-metrics strip for the integrated Experiment
// dashboard (UX-7, issue #311). Sits above the live image on the Preview
// page so a routine experiment can be operated without switching to the
// Monitoring sub-tab: run state, elapsed time, camera/processing rates,
// totals, buffered backlog, and active alerts are always visible together.
class RunDashboardStrip : public QWidget {
    Q_OBJECT
public:
    struct Data {
        bool experimentActive = false;
        bool flushInProgress = false;
        bool saveFailed = false;
        bool captureRunning = false;
        double elapsedSeconds = 0.0;
        double cameraFps = 0.0;
        double validFps = 0.0;
        double invalidFps = 0.0;
        uint64_t totalValidFlushed = 0;
        uint64_t validBuffered = 0;
        uint64_t invalidBuffered = 0;
        double metricAgeMs = 0.0; // staleness of processing metrics
        double storageFreeGb = 0.0;
        bool storageWritable = true;
    };

    explicit RunDashboardStrip(QWidget* parent = nullptr);

    void updateData(const Data& data);

private:
    QLabel* stateChip_ = nullptr;
    QLabel* metricsLabel_ = nullptr;
    QLabel* alertLabel_ = nullptr;
};

} // namespace frontend
