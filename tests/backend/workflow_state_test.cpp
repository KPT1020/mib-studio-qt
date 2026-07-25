// UX-1 (#305): the guided four-stage operator workflow derives stage state
// from backend facts plus explicit operator confirmation — never from
// navigation. This test drives evaluateWorkflow()/WorkflowStateService
// through the canonical paths: success, blocked/warning, failure, device
// loss, and recovery.
#include "backend/services/WorkflowStateService.h"

#include "support/assert.h"

using backend::services::WorkflowFacts;
using backend::services::WorkflowSnapshot;
using backend::services::WorkflowStage;
using backend::services::WorkflowStageStatus;
using backend::services::WorkflowStateService;
using backend::services::evaluateWorkflow;
using backend::services::stageState;

namespace
{

WorkflowFacts healthyIdleFacts()
{
    WorkflowFacts facts;
    facts.cameraConfigured = true;
    facts.processingCoreReady = true;
    facts.profileSelected = true;
    facts.captureRunning = true;
    facts.roiValid = true;
    return facts;
}

} // namespace

int main()
{
    // --- Startup: nothing configured -------------------------------------
    {
        WorkflowFacts facts;
        facts.processingCoreReady = true;
        const auto snap = evaluateWorkflow(facts, false, false);
        MIB_EXPECT(stageState(snap, WorkflowStage::HardwarePreflight).status ==
                       WorkflowStageStatus::NotStarted,
                   "fresh startup: preflight is Not started");
        MIB_EXPECT(snap.recommendedStage == WorkflowStage::HardwarePreflight,
                   "startup recommends the earliest incomplete stage");
        MIB_EXPECT(!stageState(snap, WorkflowStage::HardwarePreflight)
                        .blockingReasons.empty(),
                   "blocked stage exposes its blocking checks");
        MIB_EXPECT(!snap.recommendedAction.empty(),
                   "a recommended next action is always present");
    }

    // --- Camera detection alone must not complete preflight ---------------
    {
        WorkflowFacts facts;
        facts.cameraConfigured = true;
        facts.processingCoreReady = true;
        const auto snap = evaluateWorkflow(facts, false, false);
        const auto &preflight = stageState(snap, WorkflowStage::HardwarePreflight);
        MIB_EXPECT(preflight.status == WorkflowStageStatus::Ready,
                   "camera connection alone leaves preflight Ready, not Complete");
        MIB_EXPECT(preflight.blockingReasons.empty(),
                   "no blocking reasons when checks pass");
    }

    // --- Explicit confirmation completes preflight ------------------------
    {
        WorkflowFacts facts;
        facts.cameraConfigured = true;
        facts.processingCoreReady = true;
        const auto snap = evaluateWorkflow(facts, true, false);
        MIB_EXPECT(stageState(snap, WorkflowStage::HardwarePreflight).status ==
                       WorkflowStageStatus::Complete,
                   "confirmed preflight with healthy facts is Complete");
        MIB_EXPECT(snap.recommendedStage == WorkflowStage::CameraAlignment,
                   "next incomplete stage becomes the recommendation");
    }

    // --- Failure: discovery found no cameras ------------------------------
    {
        WorkflowFacts facts;
        facts.cameraDiscoveryFailed = true;
        facts.processingCoreReady = true;
        const auto snap = evaluateWorkflow(facts, false, false);
        const auto &preflight = stageState(snap, WorkflowStage::HardwarePreflight);
        MIB_EXPECT(preflight.status == WorkflowStageStatus::NeedsAttention,
                   "failed discovery is Needs attention, not Not started");
        MIB_EXPECT(!preflight.recommendedAction.empty(),
                   "failure exposes a recovery action");
    }

    // --- Failure: pinned processing core unavailable ----------------------
    {
        WorkflowFacts facts;
        facts.cameraConfigured = true;
        facts.processingCoreReady = false;
        const auto snap = evaluateWorkflow(facts, false, false);
        MIB_EXPECT(stageState(snap, WorkflowStage::HardwarePreflight).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "missing processing core blocks preflight with attention");
    }

    // --- Alignment gated on preflight completion --------------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        const auto snap = evaluateWorkflow(facts, false, false);
        const auto &alignment = stageState(snap, WorkflowStage::CameraAlignment);
        MIB_EXPECT(alignment.status == WorkflowStageStatus::NotStarted,
                   "alignment stays Not started until preflight is confirmed");
        MIB_EXPECT(!alignment.blockingReasons.empty(),
                   "alignment names preflight as its blocker");
    }

    // --- Success path: confirm both stages, ready to run ------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::CameraAlignment).status ==
                       WorkflowStageStatus::Complete,
                   "confirmed alignment with live camera and ROI is Complete");
        MIB_EXPECT(stageState(snap, WorkflowStage::Experiment).status ==
                       WorkflowStageStatus::Ready,
                   "experiment Ready once prerequisites are met");
        MIB_EXPECT(snap.recommendedStage == WorkflowStage::Experiment,
                   "recommendation advances to the experiment stage");
    }

    // --- Running experiment ------------------------------------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.experimentActive = true;
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::Experiment).status ==
                       WorkflowStageStatus::Running,
                   "active experiment reports Running");
        MIB_EXPECT(stageState(snap, WorkflowStage::Review).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "review is locked while an experiment runs");
        MIB_EXPECT(!stageState(snap, WorkflowStage::Review).blockingReasons.empty(),
                   "review lock explains why");
        MIB_EXPECT(snap.recommendedStage == WorkflowStage::Experiment,
                   "active experiment pins the recommendation");
    }

    // --- Completed experiment feeds review ---------------------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.experimentCompleted = true;
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::Experiment).status ==
                       WorkflowStageStatus::Complete,
                   "successful run marks the experiment stage Complete");
        MIB_EXPECT(stageState(snap, WorkflowStage::Review).status ==
                       WorkflowStageStatus::Ready,
                   "saved recording makes review Ready");

        WorkflowFacts loaded = facts;
        loaded.reviewFileLoaded = true;
        const auto snap2 = evaluateWorkflow(loaded, true, true);
        MIB_EXPECT(stageState(snap2, WorkflowStage::Review).status ==
                       WorkflowStageStatus::Complete,
                   "loading a recording completes review");
    }

    // --- Warning: failed save blocks the next run ---------------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.experimentCompleted = true;
        facts.lastExperimentSaveOk = false;
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::Experiment).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "failed save downgrades the experiment stage");
        MIB_EXPECT(stageState(snap, WorkflowStage::Review).status ==
                       WorkflowStageStatus::NotStarted,
                   "failed save does not advertise a reviewable recording");
    }

    // --- Device loss invalidates confirmed stages (staleness) ---------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.cameraConfigured = false; // camera lost after both confirmations
        facts.captureRunning = false;
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::HardwarePreflight).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "device loss drops confirmed preflight to Needs attention");
        MIB_EXPECT(stageState(snap, WorkflowStage::CameraAlignment).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "device loss drops confirmed alignment to Needs attention");
        MIB_EXPECT(snap.recommendedStage == WorkflowStage::HardwarePreflight,
                   "recommendation returns to the earliest broken stage");
    }

    // --- Recovery: device returns, confirmations still count ----------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::HardwarePreflight).status ==
                       WorkflowStageStatus::Complete,
                   "recovered device restores Complete without re-confirmation");
        MIB_EXPECT(stageState(snap, WorkflowStage::CameraAlignment).status ==
                       WorkflowStageStatus::Complete,
                   "recovered alignment restores Complete");
    }

    // --- Camera stopped mid-alignment (confirmed) ----------------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.captureRunning = false;
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::CameraAlignment).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "stopped camera invalidates a confirmed alignment");
        MIB_EXPECT(stageState(snap, WorkflowStage::Experiment).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "experiment cannot be Ready without a running camera");
    }

    // --- Every stage always carries accessible status text -------------------
    {
        const WorkflowFacts variants[] = {WorkflowFacts{}, healthyIdleFacts()};
        for (const auto &facts : variants)
        {
            for (const bool confirmed : {false, true})
            {
                const auto snap = evaluateWorkflow(facts, confirmed, confirmed);
                for (const auto &st : snap.stages)
                {
                    MIB_EXPECT(!st.statusText.empty(),
                               "status text present, never color-only");
                }
            }
        }
    }

    // --- UX-2: template-only session cannot present a Ready experiment -------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.profileSelected = false;
        const auto snap = evaluateWorkflow(facts, true, true);
        const auto &experiment = stageState(snap, WorkflowStage::Experiment);
        MIB_EXPECT(experiment.status != WorkflowStageStatus::Ready,
                   "no explicit profile keeps the experiment stage gated");
        bool mentionsProfile = false;
        for (const auto &reason : experiment.blockingReasons)
        {
            mentionsProfile = mentionsProfile ||
                              reason.find("profile") != std::string::npos;
        }
        MIB_EXPECT(mentionsProfile, "the gate names the missing profile");
    }

    // --- UX-2: incompatible profile blocks with attention --------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.profileIncompatible = true;
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::Experiment).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "incompatible profile downgrades the experiment stage");
    }

    // --- UX-3: unwritable storage blocks preflight ---------------------------
    {
        WorkflowFacts facts = healthyIdleFacts();
        facts.storageOk = false;
        const auto snap = evaluateWorkflow(facts, true, true);
        MIB_EXPECT(stageState(snap, WorkflowStage::HardwarePreflight).status ==
                       WorkflowStageStatus::NeedsAttention,
                   "unwritable storage invalidates confirmed preflight");
    }

    // --- Service holder: confirmations are authoritative and resettable ------
    {
        WorkflowStateService service;
        MIB_EXPECT(!service.preflightConfirmed(), "confirmations start false");
        service.setPreflightConfirmed(true);
        service.setAlignmentConfirmed(true);
        const auto snap = service.evaluate(healthyIdleFacts());
        MIB_EXPECT(stageState(snap, WorkflowStage::CameraAlignment).status ==
                       WorkflowStageStatus::Complete,
                   "service merges stored confirmations into evaluation");
        service.setAlignmentConfirmed(false);
        const auto snap2 = service.evaluate(healthyIdleFacts());
        MIB_EXPECT(stageState(snap2, WorkflowStage::CameraAlignment).status ==
                       WorkflowStageStatus::Ready,
                   "withdrawn confirmation returns the stage to Ready");
    }

    return mib::test::exitCode();
}
