#pragma once

#include <QWidget>

#include <array>

#include "backend/services/WorkflowStateService.h"

class QLabel;
class QPushButton;
class QToolButton;
class QButtonGroup;

namespace frontend {

// Guided four-stage operator workflow bar (UX-1, issue #305). Renders one
// button per stage with authoritative status text (never color alone), the
// blocking checks as tooltips, a recommended next action, and a contextual
// confirm action for the stages that require explicit operator sign-off.
// The bar only displays state: completion is decided by
// backend::services::WorkflowStateService, never by navigation.
class WorkflowStageBar : public QWidget {
    Q_OBJECT
public:
    explicit WorkflowStageBar(QWidget* parent = nullptr);

    // Re-render from an authoritative snapshot.
    void updateSnapshot(const backend::services::WorkflowSnapshot& snapshot);

    // Highlight the stage matching the visible tab (does not change state).
    void setCurrentStage(int stageIndex);

    // Enable/disable navigating to a stage (e.g. locked during a run).
    // A non-empty reason is surfaced in the tooltip.
    void setStageNavigationEnabled(int stageIndex, bool enabled, const QString& reason = {});

signals:
    void stageSelected(int stageIndex);
    void confirmRequested(int stageIndex);

private:
    QString stageTitle(int stageIndex) const;

    std::array<QToolButton*, backend::services::kWorkflowStageCount> stageButtons_{};
    std::array<QString, backend::services::kWorkflowStageCount> lockReasons_{};
    QButtonGroup* group_ = nullptr;
    QLabel* nextActionLabel_ = nullptr;
    QPushButton* confirmButton_ = nullptr;
    int confirmStage_ = -1;
};

} // namespace frontend
