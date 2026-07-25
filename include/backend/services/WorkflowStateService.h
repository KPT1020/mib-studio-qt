#pragma once

#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace backend::services
{

    // Operator workflow stages (UX-1, issue #305). The numeric values match the
    // MainWindow top-level tab order so the frontend can map stage <-> tab
    // without a translation table.
    enum class WorkflowStage : int
    {
        HardwarePreflight = 0,
        CameraAlignment = 1,
        Experiment = 2,
        Review = 3,
    };

    inline constexpr int kWorkflowStageCount = 4;

    enum class WorkflowStageStatus
    {
        NotStarted,
        NeedsAttention,
        Ready,
        Running,
        Complete,
    };

    // Snapshot of the backend facts that drive stage state. The frontend
    // collects these from the authoritative services; visiting a screen must
    // never change them.
    struct WorkflowFacts
    {
        // Hardware Preflight
        bool cameraConfigured = false;      // hardware or mock selection applied
        bool cameraDiscoveryFailed = false; // last discovery reported no cameras
        bool processingCoreReady = false;   // pinned processing core active
        bool storageOk = true;              // data folder writable (UX-3)

        // Experiment Profile (UX-2)
        bool profileSelected = false;     // explicit profile, not the template
        bool profileIncompatible = false; // selected but incompatible

        // Camera & Alignment
        bool captureRunning = false;
        bool roiValid = false;

        // Experiment
        bool experimentActive = false;
        bool flushInProgress = false;      // stop/flush still writing to disk
        bool experimentCompleted = false;  // >=1 experiment finished this session
        bool lastExperimentSaveOk = true;  // metadata/provenance write succeeded

        // Review
        bool reviewFileLoaded = false;
    };

    struct WorkflowStageState
    {
        WorkflowStageStatus status = WorkflowStageStatus::NotStarted;
        std::string statusText;                   // short accessible text, never color-only
        std::vector<std::string> blockingReasons; // specific checks preventing progress
        std::string recommendedAction;            // next safe action for this stage
    };

    struct WorkflowSnapshot
    {
        std::array<WorkflowStageState, kWorkflowStageCount> stages;
        WorkflowStage recommendedStage = WorkflowStage::HardwarePreflight;
        std::string recommendedAction; // global next step, from recommendedStage
    };

    inline const WorkflowStageState &stageState(const WorkflowSnapshot &snapshot,
                                                WorkflowStage stage)
    {
        return snapshot.stages[static_cast<size_t>(stage)];
    }

    // Pure derivation of stage state from backend facts plus explicit operator
    // confirmations. Confirmations gate Complete but can never override a
    // failed fact: a stage that was confirmed and later loses a prerequisite
    // drops to NeedsAttention until the prerequisite recovers.
    WorkflowSnapshot evaluateWorkflow(const WorkflowFacts &facts,
                                      bool preflightConfirmed,
                                      bool alignmentConfirmed);

    // Authoritative holder of the operator's explicit stage confirmations.
    // Owned by AppBackend so completion state survives frontend rebuilds and
    // is never derived from navigation. Thread-safe.
    class WorkflowStateService
    {
    public:
        void setPreflightConfirmed(bool confirmed);
        void setAlignmentConfirmed(bool confirmed);
        bool preflightConfirmed() const;
        bool alignmentConfirmed() const;

        WorkflowSnapshot evaluate(const WorkflowFacts &facts) const;

    private:
        mutable std::mutex mutex_;
        bool preflightConfirmed_ = false;
        bool alignmentConfirmed_ = false;
    };

} // namespace backend::services
