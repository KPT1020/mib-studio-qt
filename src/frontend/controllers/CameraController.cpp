#include "frontend/controllers/CameraController.h"

#include <QString>
#include <spdlog/spdlog.h>

#include "backend/app/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/processing/ProcessingService.h"

namespace frontend
{

    CameraController::CameraController(backend::AppBackend &backend, QObject *parent)
        : QObject(parent), backend_(backend)
    {
    }

    bool CameraController::isRunning() const
    {
        return backend_.capture().isRunning();
    }

    bool CameraController::isConfigured() const
    {
        return backend_.isCameraConfigured();
    }

    bool CameraController::startCapture(QString *errorMsg)
    {
        auto &cap = backend_.capture();
        if (cap.isRunning())
        {
            if (errorMsg)
                *errorMsg = "Camera is already running";
            return false;
        }

        // Guard: Cannot start camera unless a camera has been connected (hardware or mock)
        if (!backend_.isCameraConfigured())
        {
            if (errorMsg)
                *errorMsg = "No camera is configured. Please connect to a camera or configure a mock camera first.";
            return false;
        }

        // Start capture only (no experiment)
        if (cap.start())
        {
            emit cameraStarted();
            return true;
        }
        else
        {
            if (errorMsg)
                *errorMsg = "Failed to start camera. Please check camera connection and try again.";
            return false;
        }
    }

    bool CameraController::stopCapture(bool force, QString *errorMsg)
    {
        auto &cap = backend_.capture();
        if (!cap.isRunning())
        {
            if (errorMsg)
                *errorMsg = "Camera is not currently running";
            return false;
        }

        // Guard: During experiment cannot stop camera before stopping experiment (unless forced)
        // Note: This check would need access to experiment state - for now, we'll rely on the caller
        // to check experiment state before calling stopCapture

        // Stop capture only (don't end experiment)
        cap.stop();
        backend_.processing().resetRealtimeMetrics();
        emit cameraStopped();
        return true;
    }

} // namespace frontend
