#pragma once

#include <QObject>
#include <QString>

namespace backend { class AppBackend; }

namespace frontend
{

    class CameraController : public QObject
    {
        Q_OBJECT
    public:
        explicit CameraController(backend::AppBackend &backend, QObject *parent = nullptr);

        bool isRunning() const;
        bool isConfigured() const;

        // Start camera capture
        bool startCapture(QString *errorMsg = nullptr);

        // Stop camera capture
        bool stopCapture(bool force = false, QString *errorMsg = nullptr);

    signals:
        void cameraStarted();
        void cameraStopped();
        void error(const QString &message);

    private:
        backend::AppBackend &backend_;
    };

} // namespace frontend
