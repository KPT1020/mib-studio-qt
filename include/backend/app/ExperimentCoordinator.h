#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace backend
{
    class AppBackend;

    // Backend-owned experiment lifecycle state machine (BE-4, issue #274,
    // epic #246). Owns everything the Qt frontend previously coordinated in
    // MainWindow/ExperimentController: atomic precondition validation, HDF5
    // open/dataset init, the temporary inline-mode override for multi-image
    // experiments, periodic + final flush, write-queue drain ordering,
    // metadata/provenance/config-JSON writes (only after frame data is safely
    // flushed), fatal save-error recovery, and idempotent shutdown.
    //
    // Threading: start()/requestStop()/shutdown() may be called from any one
    // command thread (the bridge dispatch). A coordinator-owned worker thread
    // runs the periodic flush loop and the asynchronous stop finalization so
    // a stop never blocks the caller. Status transitions are emitted through
    // the status callback (fired on the caller or worker thread — consumers
    // must be non-blocking, same rule as the facade event sink).
    class ExperimentCoordinator
    {
    public:
        // Contract-pinned, append-only (bridge-contract.json experiment_states).
        enum class State
        {
            Idle,
            Starting,
            Active,
            Stopping,
            Failed,
        };

        struct Status
        {
            State state{State::Idle};
            std::uint64_t startTimeNs{0};
            std::uint64_t endTimeNs{0};
            std::uint64_t validBuffered{0};
            std::uint64_t invalidBuffered{0};
            std::uint64_t validSaved{0};
            std::uint64_t invalidSaved{0};
            std::uint64_t droppedValid{0};
            std::uint64_t droppedInvalid{0};
            bool flushing{false};
            bool cancelled{false};
            std::string outputPath;
            std::string message;
        };

        using StatusCallback = std::function<void(const Status &)>;

        explicit ExperimentCoordinator(AppBackend &backend);
        ~ExperimentCoordinator();

        ExperimentCoordinator(const ExperimentCoordinator &) = delete;
        ExperimentCoordinator &operator=(const ExperimentCoordinator &) = delete;

        void setStatusCallback(StatusCallback cb);

        // Validate preconditions atomically (processing-core pin satisfied,
        // camera running, usable output path, HDF5 openable) and start. On
        // success the worker thread begins the periodic flush loop. Failure
        // messages match the Qt frontend's wording.
        bool start(const std::string &outputPath, std::string *errorOut = nullptr);

        // Finalize asynchronously on the worker: final flush → drain the write
        // queue → append the remainder → H5Fflush → metadata/provenance →
        // config JSON → close. `cancelled` only marks the terminal status —
        // the file is always finalized so it stays readable. Returns false if
        // no experiment is active (double stop is a safe no-op error).
        bool requestStop(bool cancelled, std::string *errorOut = nullptr);

        // Idempotent synchronous teardown for backend shutdown/close: finishes
        // an active experiment (bounded by the final flush) without corrupting
        // the HDF5 file, then joins the worker.
        void shutdown();

        // Fatal save-error funnel (flush writer / recording threads land here
        // via AppBackend's callback): marks Failed and finalizes safely.
        void onFatalSaveError(const std::string &message);

        Status status() const;
        bool isActive() const;

    private:
        void worker();
        void finalize(bool cancelled, bool failed, const std::string &failMessage);
        Status snapshotLocked() const; // requires stateMutex_
        void emitStatus(const Status &snapshot);
        void refreshCountersLocked();  // requires stateMutex_

        AppBackend &backend_;

        mutable std::mutex stateMutex_;
        Status status_;
        bool restoreRealtimeModeAfterExperiment_{false};
        int realtimeModeBeforeExperiment_{0};

        std::mutex callbackMutex_;
        StatusCallback statusCallback_;

        std::thread workerThread_;
        std::condition_variable workerCv_;
        std::atomic<bool> stopRequested_{false};
        std::atomic<bool> cancelRequested_{false};
        std::atomic<bool> fatalRequested_{false};
        std::string fatalMessage_; // guarded by stateMutex_
    };

} // namespace backend
