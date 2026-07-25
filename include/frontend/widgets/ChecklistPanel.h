#pragma once

#include <QWidget>

#include <vector>

#include "backend/services/OperatorChecks.h"

class QGridLayout;
class QCheckBox;

namespace frontend {

// Renders a list of backend::services::checks::CheckItem rows: status glyph +
// status text (never color alone), label, detail, and a recovery button when
// the check names one. Used by the hardware-preflight panel (UX-3) and the
// experiment readiness dialog (UX-6). Display only — classification happens
// in the backend evaluators.
class ChecklistPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChecklistPanel(QWidget* parent = nullptr);

    void setItems(const std::vector<backend::services::checks::CheckItem>& items);

    // When enabled, non-overridable failed rows show "(cannot be overridden)"
    // and overridable ones show an "Override" checkbox instead of a recovery
    // button suffix. Used by the readiness dialog.
    void setOverrideSelectionEnabled(bool enabled);
    std::vector<std::string> checkedOverrideIds() const;

signals:
    void recoveryRequested(const QString& checkId);
    void overrideSelectionChanged();

private:
    void rebuild();

    QGridLayout* grid_ = nullptr;
    std::vector<backend::services::checks::CheckItem> items_;
    bool overrideSelectionEnabled_ = false;
    std::vector<QWidget*> rowWidgets_;
    std::vector<std::pair<std::string, QCheckBox*>> overrideBoxes_;
};

} // namespace frontend
