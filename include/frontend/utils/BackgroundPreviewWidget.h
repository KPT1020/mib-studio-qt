#pragma once

#include <QWidget>
#include <QImage>

class QLabel;
class QVBoxLayout;
class QPaintEvent;
class QResizeEvent;

namespace frontend
{

    class BackgroundPreviewWidget : public QWidget
    {
        Q_OBJECT

    public:
        explicit BackgroundPreviewWidget(QWidget* parent = nullptr);
        ~BackgroundPreviewWidget() = default;

        void setBackgroundImage(const QImage& image);
        void clearBackground();

    protected:
        void paintEvent(QPaintEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;

    private:
        void updatePreview();

        QImage backgroundImage_;
        QImage scaledPreview_;
        QLabel* imageLabel_ = nullptr;
        QLabel* textLabel_ = nullptr;
        QVBoxLayout* layout_ = nullptr;
        bool hasImage_ = false;
    };

} // namespace frontend
