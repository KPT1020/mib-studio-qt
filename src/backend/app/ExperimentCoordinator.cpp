#include "backend/app/ExperimentCoordinator.h"

#include "backend/app/AppBackend.h"
#include "backend/processing/ProcessingService.h"
#include "backend/recording/Hdf5Service.h"
#include "backend/services/CaptureService.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <utility>

namespace backend
{
    namespace
    {
        std::uint64_t nowNs()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        std::string normalizeHdf5Path(std::string path)
        {
            if (path.size() < 3 ||
                (path.substr(path.size() - 3) != ".h5" &&
                 (path.size() < 5 || path.substr(path.size() - 5) != ".hdf5")))
            {
                path += ".h5";
            }
            return path;
        }
    } // namespace

    ExperimentCoordinator::ExperimentCoordinator(AppBackend &backend)
        : backend_(backend)
    {
    }

    ExperimentCoordinator::~ExperimentCoordinator()
    {
        shutdown();
    }

    void ExperimentCoordinator::setStatusCallback(StatusCallback cb)
    {
        std::scoped_lock lock(callbackMutex_);
        statusCallback_ = std::move(cb);
    }

    ExperimentCoordinator::Status ExperimentCoordinator::snapshotLocked() const
    {
        return status_;
    }

    void ExperimentCoordinator::emitStatus(const Status &snapshot)
    {
        StatusCallback cb;
        {
            std::scoped_lock lock(callbackMutex_);
            cb = statusCallback_;
        }
        if (cb)
        {
            cb(snapshot);
        }
    }

    void ExperimentCoordinator::refreshCountersLocked()
    {
        auto &processing = backend_.processing();
        const auto buffered = processing.getBufferedFrameCounts();
        status_.validBuffered = buffered.valid;
        status_.invalidBuffered = buffered.invalid;
        status_.validSaved = processing.getTotalValidFlushed();
        status_.invalidSaved = processing.getTotalInvalidFlushed();
        status_.droppedValid = processing.getDroppedValidFrames();
        status_.droppedInvalid = processing.getDroppedInvalidFrames();
    }

    bool ExperimentCoordinator::start(const std::string &outputPath, std::string *errorOut)
    {
        auto fail = [&](const std::string &message) {
            if (errorOut)
            {
                *errorOut = message;
            }
            return false;
        };

        Status started;
        {
            std::scoped_lock lock(stateMutex_);
            if (status_.state != State::Idle && status_.state != State::Failed)
            {
                return fail("Experiment is already running or starting");
            }

            auto &processing = backend_.processing();
            if (!processing.isProcessingCorePinSatisfied())
            {
                return fail("Administrator-pinned processing core " +
                            processing.requiredProcessingCoreVersion() + " is not active");
            }
            if (!backend_.capture().isRunning())
            {
                return fail("Camera must be running before starting an experiment");
            }
            if (outputPath.empty())
            {
                return fail("Experiment output path is empty");
            }

            const std::string hdf5Path = normalizeHdf5Path(outputPath);
            auto &hdf5 = backend_.hdf5();
            if (!hdf5.openFile(hdf5Path))
            {
                return fail("Failed to open HDF5 file: " + hdf5Path);
            }
            if (!hdf5.initializeDatasets())
            {
                SPDLOG_WARN("ExperimentCoordinator: failed to initialize HDF5 datasets");
            }

            // Multi-image series requires inline realtime processing so series
            // images stay reviewable; restore the previous mode at stop.
            restoreRealtimeModeAfterExperiment_ = false;
            realtimeModeBeforeExperiment_ =
                static_cast<int>(processing.getRealtimeProcessingMode());
            const auto config = processing.getProcessingConfig();
            if (config.multi_image_enabled && config.multi_image_count > 1 &&
                processing.getRealtimeProcessingMode() ==
                    services::ProcessingService::RealtimeProcessingMode::AsyncBatch)
            {
                processing.setRealtimeProcessingMode(
                    services::ProcessingService::RealtimeProcessingMode::Inline);
                restoreRealtimeModeAfterExperiment_ = true;
                SPDLOG_INFO("ExperimentCoordinator: realtime mode async_batch -> inline "
                            "for multi-image experiment");
            }

            processing.startExperiment();

            status_ = Status{};
            status_.state = State::Active;
            status_.startTimeNs = nowNs();
            status_.outputPath = hdf5Path;
            status_.message = "Experiment started";

            stopRequested_.store(false);
            cancelRequested_.store(false);
            fatalRequested_.store(false);
            fatalMessage_.clear();

            if (workerThread_.joinable())
            {
                workerThread_.join(); // previous run has already exited its loop
            }
            workerThread_ = std::thread(&ExperimentCoordinator::worker, this);

            started = snapshotLocked();
        }

        emitStatus(started);
        return true;
    }

    bool ExperimentCoordinator::requestStop(bool cancelled, std::string *errorOut)
    {
        {
            std::scoped_lock lock(stateMutex_);
            if (status_.state != State::Active)
            {
                if (errorOut)
                {
                    *errorOut = "No experiment is currently running";
                }
                return false;
            }
            status_.state = State::Stopping;
            status_.message = cancelled ? "Experiment cancelling" : "Experiment stopping";
        }
        cancelRequested_.store(cancelled);
        stopRequested_.store(true);
        workerCv_.notify_all();

        Status snapshot;
        {
            std::scoped_lock lock(stateMutex_);
            snapshot = snapshotLocked();
        }
        emitStatus(snapshot);
        return true;
    }

    void ExperimentCoordinator::shutdown()
    {
        std::string error;
        requestStop(true, &error); // no-op when idle
        if (workerThread_.joinable())
        {
            workerThread_.join();
        }
    }

    void ExperimentCoordinator::onFatalSaveError(const std::string &message)
    {
        {
            std::scoped_lock lock(stateMutex_);
            if (status_.state != State::Active && status_.state != State::Stopping)
            {
                return;
            }
            fatalMessage_ = message;
        }
        fatalRequested_.store(true);
        stopRequested_.store(true);
        workerCv_.notify_all();
    }

    ExperimentCoordinator::Status ExperimentCoordinator::status() const
    {
        std::scoped_lock lock(stateMutex_);
        Status out = status_;
        return out;
    }

    bool ExperimentCoordinator::isActive() const
    {
        std::scoped_lock lock(stateMutex_);
        return status_.state == State::Active || status_.state == State::Stopping;
    }

    void ExperimentCoordinator::worker()
    {
        auto &processing = backend_.processing();
        auto &hdf5 = backend_.hdf5();

        // Periodic flush loop: mirrors the Qt stats-timer flush (flush when the
        // buffered count crosses the configured interval) without any UI timer.
        std::mutex waitMutex;
        while (!stopRequested_.load())
        {
            {
                std::unique_lock<std::mutex> lk(waitMutex);
                workerCv_.wait_for(lk, std::chrono::milliseconds(100),
                                   [this] { return stopRequested_.load(); });
            }
            if (stopRequested_.load())
            {
                break;
            }

            const std::size_t flushInterval = processing.getFlushInterval();
            const auto buffered = processing.getBufferedFrameCounts();
            bool flushed = false;
            if (flushInterval > 0 && buffered.total() >= flushInterval && hdf5.isFileOpen())
            {
                {
                    std::scoped_lock lock(stateMutex_);
                    status_.flushing = true;
                }
                processing.flushBufferedFrames(hdf5);
                flushed = true;
            }

            Status snapshot;
            {
                std::scoped_lock lock(stateMutex_);
                status_.flushing = false;
                refreshCountersLocked();
                snapshot = snapshotLocked();
            }
            if (flushed)
            {
                emitStatus(snapshot);
            }
        }

        std::string fatalMessage;
        {
            std::scoped_lock lock(stateMutex_);
            fatalMessage = fatalMessage_;
        }
        finalize(cancelRequested_.load(), fatalRequested_.load(), fatalMessage);
    }

    void ExperimentCoordinator::finalize(bool cancelled, bool failed,
                                         const std::string &failMessage)
    {
        auto &processing = backend_.processing();
        auto &hdf5 = backend_.hdf5();

        {
            std::scoped_lock lock(stateMutex_);
            status_.state = State::Stopping;
            status_.flushing = true;
        }

        bool metadataOk = true;
        std::string message;
        std::size_t remainderValid = 0;
        std::size_t remainderInvalid = 0;

        if (hdf5.isFileOpen())
        {
            // Final flush, then drain the async write queue so the writer
            // thread has stopped before any direct writes below (no two
            // threads writing the shared file).
            processing.flushBufferedFrames(hdf5);
            if (!processing.finishFlush())
            {
                SPDLOG_WARN("ExperimentCoordinator: flush queue reported a save error "
                            "during final drain");
                failed = true;
                if (message.empty())
                {
                    message = "A save error occurred while flushing experiment data";
                }
            }

            auto validFrames = processing.getValidFrames();
            auto invalidFrames = processing.getInvalidFrames();
            remainderValid = validFrames.size();
            remainderInvalid = invalidFrames.size();

            const std::uint64_t endTimeNs = nowNs();
            {
                std::scoped_lock lock(stateMutex_);
                status_.endTimeNs = endTimeNs;
            }

            if (!validFrames.empty() || !invalidFrames.empty())
            {
                if (!hdf5.appendFrames(validFrames, invalidFrames))
                {
                    SPDLOG_WARN("ExperimentCoordinator: failed to save remaining frames");
                }
            }

            // Frame data must be safely on disk before the metadata write —
            // mandatory metadata/provenance only lands after the data flush.
            if (!hdf5.flush())
            {
                SPDLOG_WARN("ExperimentCoordinator: H5Fflush before writeExperimentInfo failed");
            }

            const std::uint64_t totalValid =
                processing.getTotalValidFlushed() + remainderValid;
            const std::uint64_t totalInvalid =
                processing.getTotalInvalidFlushed() + remainderInvalid;

            const auto processingConfig = processing.getProcessingConfig();
            const auto roi = processing.getRealtimeRoi();
            cv::Mat bg = processing.getRealtimeBackgroundGray();
            const auto processingCore = processing.activeProcessingCoreIdentity();
            std::uint64_t startTimeNs = 0;
            {
                std::scoped_lock lock(stateMutex_);
                startTimeNs = status_.startTimeNs;
            }
            metadataOk = hdf5.writeExperimentInfo(startTimeNs, endTimeNs,
                                                  static_cast<size_t>(totalValid),
                                                  static_cast<size_t>(totalInvalid),
                                                  processingConfig, roi,
                                                  bg.empty() ? nullptr : &bg,
                                                  &processingCore);
            if (!metadataOk)
            {
                SPDLOG_ERROR("ExperimentCoordinator: metadata/provenance write failed");
                message = "Mandatory experiment metadata/provenance write failed";
            }

            const std::string configJson = backend_.getLastConfigJson();
            if (metadataOk && !configJson.empty())
            {
                hdf5.writeConfigJson(configJson);
            }

            hdf5.closeFile();
        }
        else
        {
            std::scoped_lock lock(stateMutex_);
            status_.endTimeNs = nowNs();
        }

        processing.endExperiment();
        processing.resetRealtimeMetrics();

        Status snapshot;
        {
            std::scoped_lock lock(stateMutex_);
            if (restoreRealtimeModeAfterExperiment_)
            {
                const auto restoreMode =
                    realtimeModeBeforeExperiment_ ==
                            static_cast<int>(
                                services::ProcessingService::RealtimeProcessingMode::AsyncBatch)
                        ? services::ProcessingService::RealtimeProcessingMode::AsyncBatch
                        : services::ProcessingService::RealtimeProcessingMode::Inline;
                processing.setRealtimeProcessingMode(restoreMode);
                restoreRealtimeModeAfterExperiment_ = false;
            }

            status_.flushing = false;
            status_.validSaved = processing.getTotalValidFlushed() + remainderValid;
            status_.invalidSaved = processing.getTotalInvalidFlushed() + remainderInvalid;
            status_.cancelled = cancelled;
            if (failed || !metadataOk)
            {
                status_.state = State::Failed;
                status_.message = !failMessage.empty() ? failMessage
                                  : !message.empty()   ? message
                                                       : "Experiment save failed";
            }
            else
            {
                status_.state = State::Idle;
                status_.message = cancelled ? "Experiment cancelled" : "Experiment saved";
            }
            snapshot = snapshotLocked();
        }
        emitStatus(snapshot);
    }

} // namespace backend
