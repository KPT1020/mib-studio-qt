#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <cstddef>

namespace backend { class AppBackend; }
class QTabWidget;
namespace frontend { class PreviewPage; }

namespace frontend
{

    class StatsDisplayManager : public QObject
    {
        Q_OBJECT
    public:
        explicit StatsDisplayManager(backend::AppBackend &backend, QObject *parent = nullptr);

        // Collect stats and format status string
        QString collectAndFormatStats(bool experimentActive, QTabWidget *experimentTabs, bool flushInProgress);

        // Get fetch time in milliseconds (for diagnostics)
        double lastFetchTimeMs() const { return lastFetchTimeMs_; }

    signals:
        void statsUpdated(const QString &statusText);

    private:
        backend::AppBackend &backend_;
        double lastFetchTimeMs_ = 0.0;
    };

} // namespace frontend
