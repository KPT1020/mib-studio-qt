#include "frontend/widgets/RunDashboardStrip.h"

#include <QHBoxLayout>
#include <QLabel>

namespace frontend {

RunDashboardStrip::RunDashboardStrip(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(10);

    stateChip_ = new QLabel(this);
    stateChip_->setStyleSheet(
        QStringLiteral("font-weight: bold; padding: 2px 8px; border-radius: 3px;"));
    layout->addWidget(stateChip_);

    metricsLabel_ = new QLabel(this);
    metricsLabel_->setTextFormat(Qt::PlainText);
    layout->addWidget(metricsLabel_, 1);

    alertLabel_ = new QLabel(this);
    alertLabel_->setTextFormat(Qt::PlainText);
    alertLabel_->setStyleSheet(QStringLiteral("color: #c62828; font-weight: bold;"));
    layout->addWidget(alertLabel_);
}

void RunDashboardStrip::updateData(const Data& data)
{
    // Run state chip: text + color, never color alone.
    QString state;
    QString color;
    if (data.saveFailed) {
        state = tr("SAVE FAILED");
        color = QStringLiteral("#c62828");
    } else if (data.flushInProgress) {
        state = tr("FLUSHING");
        color = QStringLiteral("#b58900");
    } else if (data.experimentActive) {
        state = tr("RUNNING");
        color = QStringLiteral("#2e7d32");
    } else if (data.captureRunning) {
        state = tr("LIVE (not recording)");
        color = QStringLiteral("#268bd2");
    } else {
        state = tr("IDLE");
        color = QStringLiteral("#808080");
    }
    stateChip_->setText(state);
    stateChip_->setStyleSheet(
        QStringLiteral("font-weight: bold; padding: 2px 8px; border-radius: 3px; "
                       "color: white; background-color: %1;")
            .arg(color));
    stateChip_->setAccessibleName(tr("Run state: %1").arg(state));

    // Key metrics with explicit staleness marking.
    const bool stale = data.metricAgeMs > 3000.0;
    QString metrics;
    if (data.experimentActive) {
        const int h = static_cast<int>(data.elapsedSeconds) / 3600;
        const int m = (static_cast<int>(data.elapsedSeconds) % 3600) / 60;
        const int s = static_cast<int>(data.elapsedSeconds) % 60;
        metrics += tr("Elapsed %1:%2:%3 | ")
                       .arg(h, 2, 10, QLatin1Char('0'))
                       .arg(m, 2, 10, QLatin1Char('0'))
                       .arg(s, 2, 10, QLatin1Char('0'));
    }
    metrics += tr("Camera %1 fps | Valid %2/s | Invalid %3/s | Saved %4 | Buffered %5")
                   .arg(QString::number(data.cameraFps, 'f', 0),
                        QString::number(data.validFps, 'f', 1),
                        QString::number(data.invalidFps, 'f', 1),
                        QString::number(static_cast<qulonglong>(data.totalValidFlushed)),
                        QString::number(static_cast<qulonglong>(data.validBuffered +
                                                                data.invalidBuffered)));
    if (stale && data.captureRunning) {
        metrics += tr("  [metrics stale]");
    }
    metricsLabel_->setText(metrics);
    metricsLabel_->setAccessibleName(metrics);

    // Alerts stay visible until the condition clears.
    QStringList alerts;
    if (data.saveFailed) {
        alerts << tr("Save error - data may be incomplete");
    }
    if (data.experimentActive && !data.captureRunning) {
        alerts << tr("Camera stopped during the experiment");
    }
    if (!data.storageWritable) {
        alerts << tr("Data folder not writable");
    } else if (data.storageFreeGb > 0.0 && data.storageFreeGb < 5.0) {
        alerts << tr("Low disk space: %1 GB").arg(QString::number(data.storageFreeGb, 'f', 1));
    }
    alertLabel_->setText(alerts.join(QStringLiteral("  |  ")));
    alertLabel_->setVisible(!alerts.isEmpty());
    if (!alerts.isEmpty()) {
        alertLabel_->setAccessibleName(tr("Alerts: %1").arg(alerts.join(QStringLiteral("; "))));
    }
}

} // namespace frontend
