#include "frontend/utils/SidebarWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFrame>
#include <QSettings>
#include <QPropertyAnimation>
#include <QScrollArea>

#include "backend/app/AppBackend.h"
#include "frontend/utils/StatisticsPanel.h"
#include "frontend/utils/BackgroundPreviewWidget.h"
#include "frontend/tabs/NanopositionerTab.h"
#include "frontend/tabs/SyringePumpTab.h"

namespace frontend
{

    SidebarWidget::SidebarWidget(backend::AppBackend& backend, QWidget* parent)
        : QWidget(parent)
        , backend_(backend)
    {
        loadCollapseState();
        setupUI();
        updateCollapseState();
    }

    SidebarWidget::~SidebarWidget() = default;

    void SidebarWidget::setupUI()
    {
        auto* mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // Content widget (contains background preview, statistics and nanopositioner)
        contentWidget_ = new QWidget(this);
        contentLayout_ = new QVBoxLayout(contentWidget_);
        contentLayout_->setContentsMargins(0, 0, 0, 0);
        contentLayout_->setSpacing(0);

        // Background preview widget (at the top)
        backgroundPreview_ = new BackgroundPreviewWidget(contentWidget_);
        contentLayout_->addWidget(backgroundPreview_);

        // Separator
        auto* previewSeparator = new QFrame(contentWidget_);
        previewSeparator->setFrameShape(QFrame::HLine);
        previewSeparator->setFrameShadow(QFrame::Sunken);
        contentLayout_->addWidget(previewSeparator);

        // Statistics panel
        statisticsPanel_ = new StatisticsPanel(contentWidget_);
        contentLayout_->addWidget(statisticsPanel_);

        // Separator
        auto* separator = new QFrame(contentWidget_);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        contentLayout_->addWidget(separator);

        // Nanopositioner tab
        nanopositionerTab_ = new NanopositionerTab(backend_, contentWidget_);
        contentLayout_->addWidget(nanopositionerTab_);

        // Separator
        auto* pumpSeparator = new QFrame(contentWidget_);
        pumpSeparator->setFrameShape(QFrame::HLine);
        pumpSeparator->setFrameShadow(QFrame::Sunken);
        contentLayout_->addWidget(pumpSeparator);

        // Syringe pump tab
        syringePumpTab_ = new SyringePumpTab(backend_, contentWidget_);
        contentLayout_->addWidget(syringePumpTab_);

        contentLayout_->addStretch();

        // Wrap content in a scroll area so the sidebar is scrollable
        scrollArea_ = new QScrollArea(this);
        scrollArea_->setWidget(contentWidget_);
        scrollArea_->setWidgetResizable(true);
        scrollArea_->setFrameShape(QFrame::NoFrame);
        mainLayout->addWidget(scrollArea_);

        // Toggle button (always visible, on the right)
        toggleButton_ = new QToolButton(this);
        toggleButton_->setText(collapsed_ ? "▶" : "◀");
        toggleButton_->setToolTip(collapsed_ ? tr("Expand sidebar") : tr("Collapse sidebar"));
        toggleButton_->setFixedSize(collapsedWidth_, collapsedWidth_);
        toggleButton_->setAutoRaise(true);
        connect(toggleButton_, &QToolButton::clicked, this, &SidebarWidget::toggleCollapse);
        mainLayout->addWidget(toggleButton_);

        // Let the splitter shrink this pane on small displays; use the
        // expanded width as an initial target, not a hard minimum.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        setMinimumWidth(collapsedWidth_);
        setMaximumWidth(collapsed_ ? collapsedWidth_ : 1000);
    }

    void SidebarWidget::toggleCollapse()
    {
        setCollapsed(!collapsed_);
    }

    void SidebarWidget::setCollapsed(bool collapsed)
    {
        if (collapsed_ == collapsed)
            return;

        collapsed_ = collapsed;
        updateCollapseState();
        saveCollapseState();
        emit collapseStateChanged(collapsed_);
    }

    void SidebarWidget::updateCollapseState()
    {
        // Update toggle button
        toggleButton_->setText(collapsed_ ? "▶" : "◀");
        toggleButton_->setToolTip(collapsed_ ? tr("Expand sidebar") : tr("Collapse sidebar"));

        // Show/hide content
        scrollArea_->setVisible(!collapsed_);

        // Update size constraints
        const int targetWidth = collapsed_ ? collapsedWidth_ : expandedWidth_;
        setMinimumWidth(collapsedWidth_);
        setMaximumWidth(collapsed_ ? collapsedWidth_ : 1000);
        
        // Resize to target width (splitter will handle the actual resize)
        resize(targetWidth, height());
    }

    void SidebarWidget::setExpandedWidth(int width)
    {
        if (width < 200)
            width = 300; // Minimum reasonable width
        if (width > 1000)
            width = 1000; // Maximum reasonable width

        expandedWidth_ = width;
        if (!collapsed_)
        {
            setMinimumWidth(collapsedWidth_);
            resize(expandedWidth_, height());
        }
        saveCollapseState();
    }

    void SidebarWidget::loadCollapseState()
    {
        QSettings settings;
        collapsed_ = settings.value("Sidebar/Collapsed", false).toBool();
        expandedWidth_ = settings.value("Sidebar/ExpandedWidth", 300).toInt();
        if (expandedWidth_ < 200)
            expandedWidth_ = 300; // Minimum reasonable width
    }

    void SidebarWidget::saveCollapseState()
    {
        QSettings settings;
        settings.setValue("Sidebar/Collapsed", collapsed_);
        settings.setValue("Sidebar/ExpandedWidth", expandedWidth_);
    }

    void SidebarWidget::updateBackgroundPreview(const QImage& image)
    {
        if (backgroundPreview_)
        {
            backgroundPreview_->setBackgroundImage(image);
        }
    }

} // namespace frontend
