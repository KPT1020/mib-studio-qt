#include "frontend/widgets/ContextBar.h"

#include <QHBoxLayout>
#include <QToolButton>

namespace frontend {

namespace {

constexpr const char* kSegmentIds[6] = {
    "profile", "camera", "calibration", "operator", "storage", "status",
};

} // namespace

ContextBar::ContextBar(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 2);
    layout->setSpacing(12);

    for (size_t i = 0; i < segments_.size(); ++i) {
        auto* button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setCursor(Qt::PointingHandCursor);
        const QString id = QLatin1String(kSegmentIds[i]);
        connect(button, &QToolButton::clicked, this,
                [this, id]() { emit segmentActivated(id); });
        segments_[i] = button;
        layout->addWidget(button);
    }
    layout->addStretch(1);
}

QToolButton* ContextBar::segment(int index) const
{
    return segments_[static_cast<size_t>(index)];
}

void ContextBar::updateData(const Data& data)
{
    // Profile ----------------------------------------------------------
    {
        QString text;
        QString tip;
        if (!data.profileSelected) {
            text = tr("Profile: template (unvalidated)");
            tip = tr("No experiment profile selected. Click to open the profile surface.");
        } else {
            QStringList tags;
            if (data.profileIncompatible) tags << tr("incompatible");
            if (data.profileDirty) tags << tr("dirty");
            if (data.profileVerified) {
                tags << tr("applied+verified");
            } else if (data.profileApplied) {
                tags << tr("applied");
            } else {
                tags << tr("not applied");
            }
            text = tr("Profile: %1 [%2]").arg(data.profileName, tags.join(QStringLiteral(", ")));
            tip = tr("Active Experiment Profile. Click to open the profile surface.");
        }
        segment(0)->setText(text);
        segment(0)->setToolTip(tip);
        segment(0)->setAccessibleName(text);
    }

    // Camera -----------------------------------------------------------
    {
        QString text;
        if (!data.cameraConfigured) {
            text = tr("Camera: none");
        } else if (data.mockCamera) {
            text = tr("Camera: MOCK (%1)")
                       .arg(data.captureRunning ? tr("streaming") : tr("stopped"));
        } else {
            const QString label =
                data.cameraLabel.isEmpty() ? tr("connected") : data.cameraLabel;
            text = tr("Camera: %1 (%2)")
                       .arg(label, data.captureRunning ? tr("streaming") : tr("stopped"));
        }
        segment(1)->setText(text);
        segment(1)->setToolTip(tr("Connected camera identity. Click to open Connect."));
        segment(1)->setAccessibleName(text);
    }

    // Calibration ------------------------------------------------------
    {
        const QString text = data.pixelToMicron > 0.0
            ? tr("Calibration: %1 um/px").arg(QString::number(data.pixelToMicron, 'f', 4))
            : tr("Calibration: not set");
        segment(2)->setText(text);
        segment(2)->setToolTip(
            tr("Pixel-to-micron conversion factor. Click to edit."));
        segment(2)->setAccessibleName(text);
    }

    // Operator ---------------------------------------------------------
    {
        const QString text = data.operatorName.isEmpty()
            ? tr("Operator: not set")
            : tr("Operator: %1").arg(data.operatorName);
        segment(3)->setText(text);
        segment(3)->setToolTip(tr("Operator identity recorded in run provenance. "
                                  "Click to change."));
        segment(3)->setAccessibleName(text);
    }

    // Storage ----------------------------------------------------------
    {
        const QString text = !data.storageWritable
            ? tr("Storage: NOT WRITABLE")
            : tr("Storage: %1 GB free").arg(QString::number(data.storageFreeGb, 'f', 1));
        segment(4)->setText(text);
        segment(4)->setToolTip(tr("Experiment data folder. Click to open."));
        segment(4)->setAccessibleName(text);
    }

    // Status -----------------------------------------------------------
    {
        QString text;
        if (data.experimentActive) {
            text = tr("Status: RUNNING");
        } else if (data.blocked) {
            text = tr("Status: blocked - %1").arg(data.statusText);
        } else if (data.warningCount > 0) {
            text = tr("Status: %1 warning(s) - %2").arg(data.warningCount).arg(data.statusText);
        } else {
            text = tr("Status: %1").arg(data.statusText);
        }
        segment(5)->setText(text);
        segment(5)->setToolTip(tr("System readiness. Click to jump to the recommended stage."));
        segment(5)->setAccessibleName(text);
        segment(5)->setStyleSheet(
            data.blocked ? QStringLiteral("color: #c62828; font-weight: bold;")
                         : (data.warningCount > 0
                                ? QStringLiteral("color: #b58900; font-weight: bold;")
                                : QStringLiteral("font-weight: bold;")));
    }
}

} // namespace frontend
