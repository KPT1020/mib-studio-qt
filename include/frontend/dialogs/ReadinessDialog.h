#pragma once

#include <QDialog>

#include <vector>

#include "backend/services/OperatorChecks.h"

class QLineEdit;
class QPushButton;
class QLabel;

namespace frontend {

class ChecklistPanel;

// Pre-start experiment readiness gate (UX-6, issue #310). Shows the
// authoritative readiness checklist at the point of Start: blocking failures
// disable Start unless every one of them is overridable and explicitly
// checked, and any override requires an operator name and a non-empty reason.
// The dialog only renders and collects — classification comes from
// backend::services::checks::evaluateReadiness.
class ReadinessDialog : public QDialog {
    Q_OBJECT
public:
    ReadinessDialog(const backend::services::checks::ReadinessSnapshot& snapshot,
                    QWidget* parent = nullptr);

    QString operatorName() const;
    QString overrideReason() const;
    std::vector<std::string> overriddenIds() const;

private:
    void updateStartEnabled();

    backend::services::checks::ReadinessSnapshot snapshot_;
    ChecklistPanel* panel_ = nullptr;
    QLineEdit* operatorEdit_ = nullptr;
    QLineEdit* reasonEdit_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QLabel* verdictLabel_ = nullptr;
};

} // namespace frontend
