#pragma once

#include <QObject>
#include <QImage>
#include <cstdint>

namespace backend {

class BackgroundCaptureNotifier : public QObject {
    Q_OBJECT
public:
    explicit BackgroundCaptureNotifier(QObject* parent = nullptr) : QObject(parent) {}
    
signals:
    void backgroundAutoCaptured(const QImage& background, uint64_t frameIndex);
};

} // namespace backend
