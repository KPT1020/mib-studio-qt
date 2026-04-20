#pragma once

#include <QObject>
#include <QImage>
#include <cstdint>

namespace backend {
class AppBackend;
}

namespace frontend {

/// Bridges Qt-free AppBackend background-capture callback to Qt signals on the GUI thread.
class BackgroundCaptureAdapter : public QObject {
    Q_OBJECT
public:
    explicit BackgroundCaptureAdapter(backend::AppBackend& backend, QObject* parent = nullptr);

signals:
    void backgroundAutoCaptured(const QImage& background, uint64_t frameIndex);
};

} // namespace frontend
