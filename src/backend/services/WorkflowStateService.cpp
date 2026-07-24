#include "backend/services/WorkflowStateService.h"

namespace backend::services
{

namespace
{

WorkflowStageState evaluatePreflight(const WorkflowFacts &facts, bool confirmed)
{
    WorkflowStageState state;

    if (!facts.cameraConfigured)
    {
        state.blockingReasons.push_back(
            facts.cameraDiscoveryFailed
                ? "No camera found. Check power and cabling, then retry discovery."
                : "Connect a camera (hardware or mock) on the Connect tab.");
    }
    if (!facts.processingCoreReady)
    {
        state.blockingReasons.push_back(
            "Required processing core is not active. Select it in Settings > Processing Core.");
    }

    if (state.blockingReasons.empty())
    {
        if (confirmed)
        {
            state.status = WorkflowStageStatus::Complete;
            state.statusText = "Complete";
            state.recommendedAction = "Proceed to Camera & Alignment.";
        }
        else
        {
            state.status = WorkflowStageStatus::Ready;
            state.statusText = "Checks passed - confirm to complete";
            state.recommendedAction = "Confirm hardware preflight.";
        }
        return state;
    }

    // Untouched startup state reads as Not started; anything that indicates a
    // failed check, a lost prerequisite after confirmation, or a failed
    // discovery is Needs attention.
    const bool attention = confirmed || facts.cameraDiscoveryFailed ||
                           (facts.cameraConfigured || !facts.processingCoreReady);
    state.status = attention ? WorkflowStageStatus::NeedsAttention
                             : WorkflowStageStatus::NotStarted;
    state.statusText = attention ? "Needs attention" : "Not started";
    state.recommendedAction = state.blockingReasons.front();
    return state;
}

WorkflowStageState evaluateAlignment(const WorkflowFacts &facts,
                                     bool preflightComplete,
                                     bool confirmed)
{
    WorkflowStageState state;

    if (!preflightComplete)
    {
        state.blockingReasons.push_back("Complete Hardware Preflight first.");
        state.status = confirmed ? WorkflowStageStatus::NeedsAttention
                                 : WorkflowStageStatus::NotStarted;
        state.statusText = confirmed ? "Needs attention" : "Not started";
        state.recommendedAction = state.blockingReasons.front();
        return state;
    }

    if (!facts.captureRunning)
    {
        state.blockingReasons.push_back("Start the camera to view the live image.");
    }
    if (!facts.roiValid)
    {
        state.blockingReasons.push_back("Define a valid ROI on the Overview tab.");
    }

    if (state.blockingReasons.empty())
    {
        if (confirmed)
        {
            state.status = WorkflowStageStatus::Complete;
            state.statusText = "Complete";
            state.recommendedAction = "Proceed to Experiment.";
        }
        else
        {
            state.status = WorkflowStageStatus::Ready;
            state.statusText = "Live image available - confirm alignment & ROI";
            state.recommendedAction = "Confirm alignment & ROI.";
        }
        return state;
    }

    state.status = confirmed ? WorkflowStageStatus::NeedsAttention
                             : WorkflowStageStatus::NotStarted;
    state.statusText = confirmed ? "Needs attention" : "Not started";
    state.recommendedAction = state.blockingReasons.front();
    return state;
}

WorkflowStageState evaluateExperiment(const WorkflowFacts &facts,
                                      const WorkflowStageState &alignmentState)
{
    const bool alignmentComplete =
        alignmentState.status == WorkflowStageStatus::Complete;
    WorkflowStageState state;

    if (facts.experimentActive)
    {
        state.status = WorkflowStageStatus::Running;
        state.statusText = facts.flushInProgress ? "Running (flushing to disk)"
                                                 : "Running";
        state.recommendedAction = "Monitor the run; stop the experiment when finished.";
        return state;
    }

    if (!alignmentComplete)
    {
        state.blockingReasons.push_back("Complete Camera & Alignment first.");
    }
    if (!facts.captureRunning)
    {
        state.blockingReasons.push_back("Camera must be running to start an experiment.");
    }
    if (!facts.processingCoreReady)
    {
        state.blockingReasons.push_back(
            "Required processing core is not active. Select it in Settings > Processing Core.");
    }
    if (!facts.lastExperimentSaveOk)
    {
        state.blockingReasons.push_back(
            "The last experiment save failed. Check disk space and the application log.");
    }

    if (state.blockingReasons.empty())
    {
        if (facts.experimentCompleted)
        {
            state.status = WorkflowStageStatus::Complete;
            state.statusText = "Complete - data saved";
            state.recommendedAction = "Review the recording, or start another experiment.";
        }
        else
        {
            state.status = WorkflowStageStatus::Ready;
            state.statusText = "Ready to start";
            state.recommendedAction = "Start the experiment.";
        }
        return state;
    }

    // A regressed upstream stage (confirmed, then broken by device loss or a
    // stopped camera) must surface here as attention, not read as untouched.
    const bool attention = facts.experimentCompleted || !facts.lastExperimentSaveOk ||
                           alignmentComplete ||
                           alignmentState.status == WorkflowStageStatus::NeedsAttention;
    state.status = attention ? WorkflowStageStatus::NeedsAttention
                             : WorkflowStageStatus::NotStarted;
    state.statusText = attention ? "Needs attention" : "Not started";
    state.recommendedAction = state.blockingReasons.front();
    return state;
}

WorkflowStageState evaluateReview(const WorkflowFacts &facts)
{
    WorkflowStageState state;

    if (facts.experimentActive)
    {
        state.status = WorkflowStageStatus::NeedsAttention;
        state.statusText = "Locked during experiment";
        state.blockingReasons.push_back(
            "Review is not available while an experiment is running.");
        state.recommendedAction = "Stop the experiment before reviewing data.";
        return state;
    }

    if (facts.reviewFileLoaded)
    {
        state.status = WorkflowStageStatus::Complete;
        state.statusText = "Recording open";
        state.recommendedAction = "Inspect frames, metrics, and exports.";
        return state;
    }

    if (facts.experimentCompleted && facts.lastExperimentSaveOk)
    {
        state.status = WorkflowStageStatus::Ready;
        state.statusText = "Recording available";
        state.recommendedAction = "Open the saved recording in Review.";
        return state;
    }

    state.status = WorkflowStageStatus::NotStarted;
    state.statusText = "Not started";
    state.recommendedAction = "Run an experiment or open an existing recording.";
    return state;
}

} // namespace

WorkflowSnapshot evaluateWorkflow(const WorkflowFacts &facts,
                                  bool preflightConfirmed,
                                  bool alignmentConfirmed)
{
    WorkflowSnapshot snapshot;

    auto &preflight =
        snapshot.stages[static_cast<size_t>(WorkflowStage::HardwarePreflight)];
    preflight = evaluatePreflight(facts, preflightConfirmed);
    const bool preflightComplete =
        preflight.status == WorkflowStageStatus::Complete;

    auto &alignment =
        snapshot.stages[static_cast<size_t>(WorkflowStage::CameraAlignment)];
    alignment = evaluateAlignment(facts, preflightComplete, alignmentConfirmed);

    snapshot.stages[static_cast<size_t>(WorkflowStage::Experiment)] =
        evaluateExperiment(facts, alignment);
    snapshot.stages[static_cast<size_t>(WorkflowStage::Review)] =
        evaluateReview(facts);

    if (facts.experimentActive)
    {
        snapshot.recommendedStage = WorkflowStage::Experiment;
    }
    else
    {
        snapshot.recommendedStage = WorkflowStage::Review; // fallback: all complete
        for (int i = 0; i < kWorkflowStageCount; ++i)
        {
            if (snapshot.stages[static_cast<size_t>(i)].status !=
                WorkflowStageStatus::Complete)
            {
                snapshot.recommendedStage = static_cast<WorkflowStage>(i);
                break;
            }
        }
    }
    snapshot.recommendedAction =
        stageState(snapshot, snapshot.recommendedStage).recommendedAction;
    return snapshot;
}

void WorkflowStateService::setPreflightConfirmed(bool confirmed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    preflightConfirmed_ = confirmed;
}

void WorkflowStateService::setAlignmentConfirmed(bool confirmed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    alignmentConfirmed_ = confirmed;
}

bool WorkflowStateService::preflightConfirmed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return preflightConfirmed_;
}

bool WorkflowStateService::alignmentConfirmed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return alignmentConfirmed_;
}

WorkflowSnapshot WorkflowStateService::evaluate(const WorkflowFacts &facts) const
{
    bool preflight = false;
    bool alignment = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        preflight = preflightConfirmed_;
        alignment = alignmentConfirmed_;
    }
    return evaluateWorkflow(facts, preflight, alignment);
}

} // namespace backend::services
