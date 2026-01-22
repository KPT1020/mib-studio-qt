#pragma once

#include <QWidget>
#include <QImage>

namespace backend
{
    class AppBackend;
}

class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QLabel;
class QTimer;
class QWidget;

namespace Ui { class OverviewTab; }

namespace frontend
{

    class OverviewTab : public QWidget
    {
        Q_OBJECT
    public:
        explicit OverviewTab(backend::AppBackend &backend, QWidget *parent = nullptr);
        ~OverviewTab();

        enum class FitMode
        {
            FitToWindow,
            Zoom100
        };

        QString currentJsPath() const;

    private slots:
        void onTick();
        void onReloadJs();
        void onSaveJs();
        void onApplyJs();
        void onBrowseJs();
        void onClearJs();
        void onToggleFit();
        void onToggleRoiOverlay();
        void onRoiPositionChanged(QPointF imagePos);

    private:
        QString appDirIncludePath(const QString &fileName) const;
        QString defaultJsPath() const { return appDirIncludePath("overviewConfig.js"); }
        bool loadFileToEditor(const QString &path, QPlainTextEdit *editor, QString *err);
        bool saveEditorToFile(QPlainTextEdit *editor, const QString &path, QString *err);

        Ui::OverviewTab* ui;
        backend::AppBackend &backend_;

        // Frame display
        QWidget *canvas_ = nullptr;
        QTimer *timer_ = nullptr;
        QImage frameImage_;
        FitMode fitMode_{FitMode::FitToWindow};

        // ROI overlay state
        bool roiOverlayVisible_ = false;
        QPointF roiPosition_; // Position in image coordinates
        static constexpr int roiWidth_ = 512;
        static constexpr int roiHeight_ = 96;

        // Helper methods
        QString egrabberConfigPath() const;
        void updateEgrabberConfigFromRect(QPointF imagePos);
        void initializeRoiFromConfig();
    };

} // namespace frontend
