#pragma once

#include <QWidget>
#include <memory>

namespace backend { class AppBackend; }

class QTimer;

class PlaybackPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackPanel(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~PlaybackPanel() override;

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onTick();

private:
    backend::AppBackend& backend_;
    QTimer* timer_ = nullptr;
    QImage frameImage_;
};


