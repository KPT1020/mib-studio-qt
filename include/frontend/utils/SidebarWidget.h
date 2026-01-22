#pragma once

#include <QWidget>

namespace backend { class AppBackend; }
namespace frontend { class StatisticsPanel; }
namespace frontend { class NanopositionerTab; }
class QSplitter;
class QToolButton;
class QVBoxLayout;
class QHBoxLayout;

namespace frontend
{

    class SidebarWidget : public QWidget
    {
        Q_OBJECT

    public:
        explicit SidebarWidget(backend::AppBackend& backend, QWidget* parent = nullptr);
        ~SidebarWidget();

        bool isCollapsed() const { return collapsed_; }
        void setCollapsed(bool collapsed);

        StatisticsPanel* statisticsPanel() const { return statisticsPanel_; }
        NanopositionerTab* nanopositionerTab() const { return nanopositionerTab_; }

        int expandedWidth() const { return expandedWidth_; }
        void setExpandedWidth(int width);

    public slots:
        void toggleCollapse();

    signals:
        void collapseStateChanged(bool collapsed);

    private:
        void setupUI();
        void updateCollapseState();
        void loadCollapseState();
        void saveCollapseState();

        backend::AppBackend& backend_;
        StatisticsPanel* statisticsPanel_ = nullptr;
        NanopositionerTab* nanopositionerTab_ = nullptr;
        QToolButton* toggleButton_ = nullptr;
        QWidget* contentWidget_ = nullptr;
        QVBoxLayout* contentLayout_ = nullptr;
        bool collapsed_ = false;
        int expandedWidth_ = 300;
        int collapsedWidth_ = 30;
    };

} // namespace frontend
