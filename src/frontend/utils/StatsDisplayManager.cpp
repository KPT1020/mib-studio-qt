#include "frontend/utils/StatsDisplayManager.h"

#include <QString>
#include <QTabWidget>
#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/PlaybackService.h"
#include "backend/services/AutofocusService.h"
#include "backend/Tools.h"
#include "frontend/tabs/PreviewPage.h"
#include "frontend/system/PlaybackPanel.h"

namespace frontend
{

    StatsDisplayManager::StatsDisplayManager(backend::AppBackend &backend, QObject *parent)
        : QObject(parent), backend_(backend)
    {
    }

    QString StatsDisplayManager::collectAndFormatStats(bool experimentActive, QTabWidget *experimentTabs, bool flushInProgress)
    {
        const auto &cap = backend_.capture();
        const auto &s = cap.stats();
        auto &proc = backend_.processing();
        const uint64_t tFetchStartUs = backend::Tools::getTimestamp();
        auto validFrames = proc.getValidFrames();
        auto invalidFrames = proc.getInvalidFrames();
        const uint64_t tFetchEndUs = backend::Tools::getTimestamp();
        lastFetchTimeMs_ = static_cast<double>(tFetchEndUs - tFetchStartUs) / 1000.0;

        QString status;
        // Display / algo / classification rates and totals
        double displayFps = 0.0;
        if (experimentTabs && experimentTabs->count() > 0) {
            auto* previewPage = qobject_cast<PreviewPage*>(experimentTabs->widget(0));
            if (previewPage) {
                auto* playbackPanel = previewPage->getPlaybackPanel();
                if (playbackPanel) {
                    displayFps = playbackPanel->getDisplayFps();
                }
            }
        }
        const double algoAvgUs = proc.getAlgoAvgUs1s();
        const double validFps = proc.getValidFps1s();
        const double invalidFps = proc.getInvalidFps1s();
        const uint64_t totalValidFlushed = proc.getTotalValidFlushed();

        status = QString("Display=%1 fps | Algo=%2 us | Valid=%3/s | Invalid=%4/s | Flushed(valid)=%5")
                     .arg(QString::number(displayFps, 'f', 1))
                     .arg(QString::number(algoAvgUs, 'f', 1))
                     .arg(QString::number(validFps, 'f', 1))
                     .arg(QString::number(invalidFps, 'f', 1))
                     .arg(QString::number(static_cast<qulonglong>(totalValidFlushed)));

        // Camera transport stats
        if (cap.isRunning()) {
            status += QString(" | Camera=%1 fps, %2 MB/s")
                          .arg(QString::number(s.lastFrameRate.load()))
                          .arg(QString::number(s.lastDataRateMBps.load()));
        } else {
            status += " | Camera: stopped";
        }

        // Append live ring width (median from AutofocusService, same value used by autofocus)
        {
            const double ringWidth = backend_.autofocus().getMedianRingRatio();
            status += QString(" | Ring width=%1").arg(QString::number(ringWidth, 'f', 3));
        }

        if (experimentActive)
        {
            size_t totalBuffered = validFrames.size() + invalidFrames.size();

            status += QString(" | Experiment: active | buffered: valid=%1, invalid=%2")
                          .arg(validFrames.size())
                          .arg(invalidFrames.size());
            if (flushInProgress)
            {
                status += " (flushing...)";
            }

            // Throttled diagnostic log (~1 Hz)
            static uint64_t lastDiagLogUs = 0;
            const uint64_t nowUs = backend::Tools::getTimestamp();
            if (nowUs - lastDiagLogUs >= 1'000'000ULL) {
                uint64_t earliest = 0, latest = 0;
                size_t count = 0;
                backend_.playback().queryRange(earliest, latest, count);
                const double memMB = backend::Tools::getProcessMemoryMB();
                size_t flushNeeded = proc.getFlushInterval();
                SPDLOG_INFO("MainWindow stats: buffer_fetch_ms={:.3f}, valid={}, invalid={}, total={}, playback_range=[{},{}] count={}, flush_interval={}, flushing={}, mem_mb={:.1f}",
                            lastFetchTimeMs_, validFrames.size(), invalidFrames.size(), totalBuffered, earliest, latest, count, flushNeeded, flushInProgress ? 1 : 0, memMB);
                lastDiagLogUs = nowUs;
            }
        }
        else
        {
            status += " | Experiment: inactive";
        }

        return status;
    }

} // namespace frontend
