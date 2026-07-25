#include "frontend/widgets/ChecklistPanel.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

#include <algorithm>

namespace frontend {

namespace {

using backend::services::checks::CheckStatus;

QString statusText(CheckStatus status)
{
    switch (status) {
    case CheckStatus::Passed: return ChecklistPanel::tr("Passed");
    case CheckStatus::Warning: return ChecklistPanel::tr("Warning");
    case CheckStatus::Failed: return ChecklistPanel::tr("Failed");
    case CheckStatus::NotRequired: return ChecklistPanel::tr("Not required");
    }
    return {};
}

QString statusGlyph(CheckStatus status)
{
    switch (status) {
    case CheckStatus::Passed: return QString(QChar(0x2714));      // check mark
    case CheckStatus::Warning: return QString(QChar(0x26A0));     // warning sign
    case CheckStatus::Failed: return QString(QChar(0x2716));      // heavy X
    case CheckStatus::NotRequired: return QString(QChar(0x2013)); // en dash
    }
    return {};
}

const char* statusColor(CheckStatus status)
{
    switch (status) {
    case CheckStatus::Passed: return "#2e7d32";
    case CheckStatus::Warning: return "#b58900";
    case CheckStatus::Failed: return "#c62828";
    case CheckStatus::NotRequired: return "#808080";
    }
    return "#808080";
}

} // namespace

ChecklistPanel::ChecklistPanel(QWidget* parent)
    : QWidget(parent)
{
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(4, 4, 4, 4);
    grid_->setHorizontalSpacing(10);
    grid_->setVerticalSpacing(2);
    grid_->setColumnStretch(2, 1);
}

void ChecklistPanel::setItems(const std::vector<backend::services::checks::CheckItem>& items)
{
    // Called on a periodic refresh — skip the widget rebuild when nothing
    // changed so buttons aren't destroyed mid-interaction.
    const auto sameItem = [](const backend::services::checks::CheckItem& a,
                             const backend::services::checks::CheckItem& b) {
        return a.id == b.id && a.label == b.label && a.status == b.status &&
               a.detail == b.detail && a.recoveryAction == b.recoveryAction &&
               a.overridable == b.overridable;
    };
    if (items.size() == items_.size() &&
        std::equal(items.begin(), items.end(), items_.begin(), sameItem)) {
        return;
    }
    items_ = items;
    rebuild();
}

void ChecklistPanel::setOverrideSelectionEnabled(bool enabled)
{
    if (overrideSelectionEnabled_ == enabled) {
        return;
    }
    overrideSelectionEnabled_ = enabled;
    rebuild();
}

std::vector<std::string> ChecklistPanel::checkedOverrideIds() const
{
    std::vector<std::string> ids;
    for (const auto& [id, box] : overrideBoxes_) {
        if (box && box->isChecked()) {
            ids.push_back(id);
        }
    }
    return ids;
}

void ChecklistPanel::rebuild()
{
    // Preserve which override boxes were ticked across refreshes.
    std::vector<std::string> previouslyChecked = checkedOverrideIds();

    for (QWidget* w : rowWidgets_) {
        w->deleteLater();
    }
    rowWidgets_.clear();
    overrideBoxes_.clear();

    int row = 0;
    for (const auto& item : items_) {
        auto* status = new QLabel(
            QStringLiteral("%1 %2").arg(statusGlyph(item.status), statusText(item.status)),
            this);
        status->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;")
                                  .arg(QLatin1String(statusColor(item.status))));
        status->setAccessibleName(QStringLiteral("%1: %2")
                                      .arg(QString::fromStdString(item.label),
                                           statusText(item.status)));

        auto* label = new QLabel(QString::fromStdString(item.label), this);
        label->setStyleSheet(QStringLiteral("font-weight: bold;"));

        QString detailText = QString::fromStdString(item.detail);
        if (!item.recoveryAction.empty() && !overrideSelectionEnabled_) {
            detailText += QStringLiteral("\n%1 %2")
                              .arg(tr("Next:"), QString::fromStdString(item.recoveryAction));
        }
        auto* detail = new QLabel(detailText, this);
        detail->setWordWrap(true);

        grid_->addWidget(status, row, 0, Qt::AlignTop);
        grid_->addWidget(label, row, 1, Qt::AlignTop);
        grid_->addWidget(detail, row, 2, Qt::AlignTop);
        rowWidgets_.push_back(status);
        rowWidgets_.push_back(label);
        rowWidgets_.push_back(detail);

        const bool needsAction = item.status == CheckStatus::Failed ||
                                 item.status == CheckStatus::Warning;
        if (overrideSelectionEnabled_) {
            if (needsAction && item.overridable) {
                auto* box = new QCheckBox(tr("Override"), this);
                box->setToolTip(QString::fromStdString(item.recoveryAction));
                const bool wasChecked =
                    std::find(previouslyChecked.begin(), previouslyChecked.end(),
                              item.id) != previouslyChecked.end();
                box->setChecked(wasChecked);
                connect(box, &QCheckBox::toggled, this,
                        &ChecklistPanel::overrideSelectionChanged);
                grid_->addWidget(box, row, 3, Qt::AlignTop);
                rowWidgets_.push_back(box);
                overrideBoxes_.emplace_back(item.id, box);
            } else if (item.status == CheckStatus::Failed) {
                auto* fixedLabel = new QLabel(tr("(cannot be overridden)"), this);
                fixedLabel->setStyleSheet(QStringLiteral("color: #c62828;"));
                grid_->addWidget(fixedLabel, row, 3, Qt::AlignTop);
                rowWidgets_.push_back(fixedLabel);
            }
        } else if (needsAction && !item.recoveryAction.empty()) {
            auto* fix = new QPushButton(tr("Fix..."), this);
            fix->setToolTip(QString::fromStdString(item.recoveryAction));
            const QString id = QString::fromStdString(item.id);
            connect(fix, &QPushButton::clicked, this,
                    [this, id]() { emit recoveryRequested(id); });
            grid_->addWidget(fix, row, 3, Qt::AlignTop);
            rowWidgets_.push_back(fix);
        }
        ++row;
    }
}

} // namespace frontend
