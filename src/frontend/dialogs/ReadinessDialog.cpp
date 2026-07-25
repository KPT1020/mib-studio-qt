#include "frontend/dialogs/ReadinessDialog.h"

#include "frontend/widgets/ChecklistPanel.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace frontend {

using backend::services::checks::CheckStatus;

ReadinessDialog::ReadinessDialog(
    const backend::services::checks::ReadinessSnapshot& snapshot, QWidget* parent)
    : QDialog(parent), snapshot_(snapshot)
{
    setWindowTitle(tr("Experiment Readiness"));
    setModal(true);
    resize(720, 520);

    auto* layout = new QVBoxLayout(this);

    verdictLabel_ = new QLabel(this);
    verdictLabel_->setWordWrap(true);
    verdictLabel_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    layout->addWidget(verdictLabel_);

    panel_ = new ChecklistPanel(this);
    panel_->setOverrideSelectionEnabled(true);
    panel_->setItems(snapshot_.items);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(panel_);
    layout->addWidget(scroll, 1);

    auto* form = new QFormLayout();
    operatorEdit_ = new QLineEdit(this);
    operatorEdit_->setText(QSettings().value(QStringLiteral("Operator/Name")).toString());
    operatorEdit_->setPlaceholderText(tr("Who is operating this run"));
    form->addRow(tr("Operator:"), operatorEdit_);
    reasonEdit_ = new QLineEdit(this);
    reasonEdit_->setPlaceholderText(
        tr("Required when overriding a failed check - recorded in the run provenance"));
    form->addRow(tr("Override reason:"), reasonEdit_);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(this);
    startBtn_ = buttons->addButton(tr("Start Experiment"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(panel_, &ChecklistPanel::overrideSelectionChanged, this,
            &ReadinessDialog::updateStartEnabled);
    connect(reasonEdit_, &QLineEdit::textChanged, this,
            &ReadinessDialog::updateStartEnabled);
    connect(operatorEdit_, &QLineEdit::textChanged, this,
            &ReadinessDialog::updateStartEnabled);
    connect(this, &QDialog::accepted, this, [this]() {
        QSettings().setValue(QStringLiteral("Operator/Name"), operatorName());
    });

    updateStartEnabled();
}

QString ReadinessDialog::operatorName() const
{
    return operatorEdit_->text().trimmed();
}

QString ReadinessDialog::overrideReason() const
{
    return reasonEdit_->text().trimmed();
}

std::vector<std::string> ReadinessDialog::overriddenIds() const
{
    return panel_->checkedOverrideIds();
}

void ReadinessDialog::updateStartEnabled()
{
    const auto checked = panel_->checkedOverrideIds();
    bool blocked = false;
    QString why;
    for (const auto& item : snapshot_.items) {
        if (item.status != CheckStatus::Failed) {
            continue;
        }
        if (!item.overridable) {
            blocked = true;
            why = tr("Blocked: %1 - %2").arg(QString::fromStdString(item.label),
                                             QString::fromStdString(item.detail));
            break;
        }
        if (std::find(checked.begin(), checked.end(), item.id) == checked.end()) {
            blocked = true;
            why = tr("Blocked: %1 requires an explicit override.")
                      .arg(QString::fromStdString(item.label));
            break;
        }
    }
    if (!blocked && !checked.empty()) {
        if (overrideReason().isEmpty()) {
            blocked = true;
            why = tr("An override reason is required.");
        } else if (operatorName().isEmpty()) {
            blocked = true;
            why = tr("An operator name is required when overriding.");
        }
    }
    if (!blocked) {
        why = snapshot_.hasWarnings
                  ? tr("Ready with warnings - review the checklist before starting.")
                  : tr("All readiness checks passed.");
    }
    verdictLabel_->setText(why);
    verdictLabel_->setStyleSheet(blocked
                                     ? QStringLiteral("font-weight: bold; color: #c62828;")
                                     : (snapshot_.hasWarnings
                                            ? QStringLiteral("font-weight: bold; color: #b58900;")
                                            : QStringLiteral("font-weight: bold; color: #2e7d32;")));
    startBtn_->setEnabled(!blocked);
}

} // namespace frontend
