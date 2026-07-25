#include "frontend/widgets/WorkflowStageBar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>

namespace frontend {

namespace {

using backend::services::WorkflowStageStatus;

QString statusGlyph(WorkflowStageStatus status)
{
    switch (status) {
    case WorkflowStageStatus::NotStarted: return QString(QChar(0x25CB));     // white circle
    case WorkflowStageStatus::NeedsAttention: return QString(QChar(0x26A0)); // warning sign
    case WorkflowStageStatus::Ready: return QString(QChar(0x25CF));          // black circle
    case WorkflowStageStatus::Running: return QString(QChar(0x25B6));        // play triangle
    case WorkflowStageStatus::Complete: return QString(QChar(0x2714));       // check mark
    }
    return {};
}

const char* statusColor(WorkflowStageStatus status)
{
    switch (status) {
    case WorkflowStageStatus::NotStarted: return "#808080";
    case WorkflowStageStatus::NeedsAttention: return "#b58900";
    case WorkflowStageStatus::Ready: return "#268bd2";
    case WorkflowStageStatus::Running: return "#2aa198";
    case WorkflowStageStatus::Complete: return "#2e7d32";
    }
    return "#808080";
}

} // namespace

WorkflowStageBar::WorkflowStageBar(QWidget* parent)
    : QWidget(parent)
{
    // The bar is a fixed-height strip: without this it shares vertical space
    // equally with the tab area when inserted into the central layout.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);

    for (int i = 0; i < backend::services::kWorkflowStageCount; ++i) {
        auto* button = new QToolButton(this);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setText(stageTitle(i));
        stageButtons_[static_cast<size_t>(i)] = button;
        group_->addButton(button, i);
        layout->addWidget(button);

        if (i + 1 < backend::services::kWorkflowStageCount) {
            auto* arrow = new QLabel(QString(QChar(0x2192)), this); // right arrow
            arrow->setStyleSheet(QStringLiteral("color: #808080;"));
            layout->addWidget(arrow);
        }
    }

    connect(group_, &QButtonGroup::idClicked, this, &WorkflowStageBar::stageSelected);

    layout->addStretch(1);

    nextActionLabel_ = new QLabel(this);
    nextActionLabel_->setTextFormat(Qt::PlainText);
    nextActionLabel_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    layout->addWidget(nextActionLabel_);

    confirmButton_ = new QPushButton(this);
    confirmButton_->setVisible(false);
    connect(confirmButton_, &QPushButton::clicked, this, [this]() {
        if (confirmStage_ >= 0) {
            emit confirmRequested(confirmStage_);
        }
    });
    layout->addWidget(confirmButton_);
}

QString WorkflowStageBar::stageTitle(int stageIndex) const
{
    switch (stageIndex) {
    case 0: return tr("1. Hardware Preflight");
    case 1: return tr("2. Camera && Alignment");
    case 2: return tr("3. Experiment");
    case 3: return tr("4. Review");
    default: return {};
    }
}

void WorkflowStageBar::updateSnapshot(const backend::services::WorkflowSnapshot& snapshot)
{
    using backend::services::WorkflowStage;

    for (int i = 0; i < backend::services::kWorkflowStageCount; ++i) {
        const auto& stage = snapshot.stages[static_cast<size_t>(i)];
        QToolButton* button = stageButtons_[static_cast<size_t>(i)];

        const QString statusText = QString::fromStdString(stage.statusText);
        // QToolButton treats '&' as a mnemonic marker — escape dynamic text.
        QString escapedStatus = statusText;
        escapedStatus.replace(QLatin1Char('&'), QStringLiteral("&&"));
        button->setText(QStringLiteral("%1\n%2 %3")
                            .arg(stageTitle(i), statusGlyph(stage.status), escapedStatus));
        button->setStyleSheet(
            QStringLiteral("QToolButton { color: %1; }").arg(statusColor(stage.status)));

        QStringList tooltip;
        QString plainTitle = stageTitle(i);
        plainTitle.replace(QStringLiteral("&&"), QStringLiteral("&"));
        tooltip << QStringLiteral("%1 - %2").arg(plainTitle, statusText);
        for (const auto& reason : stage.blockingReasons) {
            tooltip << QString(QChar(0x2022)) + QStringLiteral(" ") +
                           QString::fromStdString(reason); // bullet

        }
        if (!stage.recommendedAction.empty()) {
            tooltip << tr("Next: %1").arg(QString::fromStdString(stage.recommendedAction));
        }
        const QString lockReason = lockReasons_[static_cast<size_t>(i)];
        if (!lockReason.isEmpty()) {
            tooltip << lockReason;
        }
        button->setToolTip(tooltip.join(QStringLiteral("\n")));
        button->setAccessibleName(tooltip.first());
        button->setAccessibleDescription(tooltip.join(QStringLiteral(" ")));
    }

    nextActionLabel_->setText(
        tr("Next: %1").arg(QString::fromStdString(snapshot.recommendedAction)));

    // Contextual confirm action for the two stages that need explicit sign-off.
    confirmStage_ = -1;
    const auto recommended = snapshot.recommendedStage;
    const auto& recommendedState = backend::services::stageState(snapshot, recommended);
    if (recommendedState.status == WorkflowStageStatus::Ready) {
        if (recommended == WorkflowStage::HardwarePreflight) {
            confirmStage_ = 0;
            confirmButton_->setText(tr("Confirm Preflight"));
        } else if (recommended == WorkflowStage::CameraAlignment) {
            confirmStage_ = 1;
            confirmButton_->setText(tr("Confirm Alignment && ROI"));
        }
    }
    confirmButton_->setVisible(confirmStage_ >= 0);
}

void WorkflowStageBar::setCurrentStage(int stageIndex)
{
    if (stageIndex < 0 || stageIndex >= backend::services::kWorkflowStageCount) {
        return;
    }
    stageButtons_[static_cast<size_t>(stageIndex)]->setChecked(true);
}

void WorkflowStageBar::setStageNavigationEnabled(int stageIndex, bool enabled,
                                                 const QString& reason)
{
    if (stageIndex < 0 || stageIndex >= backend::services::kWorkflowStageCount) {
        return;
    }
    stageButtons_[static_cast<size_t>(stageIndex)]->setEnabled(enabled);
    lockReasons_[static_cast<size_t>(stageIndex)] = enabled ? QString() : reason;
}

} // namespace frontend
