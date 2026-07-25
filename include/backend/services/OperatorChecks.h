#pragma once

#include <string>
#include <vector>

namespace backend::services::checks
{

    // Shared check-item model for the operator workflow (UX-3 preflight and
    // UX-6 experiment readiness, epic #304). Qt-free and side-effect free so
    // the classification rules are unit-testable; the frontend collects the
    // facts and renders the items.

    enum class CheckStatus
    {
        Passed,
        Warning,
        Failed, // preflight: failed; readiness: blocks start
        NotRequired,
    };

    struct CheckItem
    {
        std::string id;             // stable identifier, e.g. "camera", "storage"
        std::string label;          // short human-readable name
        CheckStatus status = CheckStatus::NotRequired;
        std::string detail;         // current value / cause, human readable
        std::string recoveryAction; // recommended next step when not Passed
        bool overridable = false;   // readiness only: expert override allowed
    };

    // ------------------------------------------------------------------
    // Hardware preflight (UX-1 stage 1 / issue #307)
    // ------------------------------------------------------------------

    struct PreflightFacts
    {
        bool cameraConfigured = false;
        bool cameraDiscoveryFailed = false;
        bool mockCamera = false;
        std::string cameraLabel;

        bool processingCoreReady = false;
        std::string processingCoreVersion;

        bool storagePathKnown = false;
        bool storageWritable = false;
        double storageFreeGb = 0.0;
        double storageMinFreeGb = 5.0; // configurable threshold

        bool profileSelected = false;
        bool profileIncompatible = false;
        std::string profileName;
    };

    std::vector<CheckItem> evaluatePreflight(const PreflightFacts &facts);

    // True when no required check Failed (Warnings allowed).
    bool preflightPassed(const std::vector<CheckItem> &items);

    // ------------------------------------------------------------------
    // Experiment readiness gate (issue #310)
    // ------------------------------------------------------------------

    struct ReadinessFacts
    {
        PreflightFacts preflight;

        bool preflightConfirmed = false;
        bool alignmentConfirmed = false;
        bool captureRunning = false;
        bool roiValid = false;
        bool backgroundReady = false;
        double pixelToMicron = 0.0; // <= 0 means calibration not set

        bool profileDirty = false;
        bool profileApplied = false;  // apply&verify transaction ran (UX-5)
        bool profileVerified = false; // ... and every component verified

        bool lastExperimentSaveOk = true;
        bool experimentActive = false;
    };

    struct ReadinessSnapshot
    {
        std::vector<CheckItem> items;
        bool hasBlocks = false;   // any Failed item
        bool hasWarnings = false; // any Warning item
    };

    ReadinessSnapshot evaluateReadiness(const ReadinessFacts &facts);

    // Provenance record written into the experiment file at start (UX-6).
    // overriddenIds lists the Failed-but-overridable check ids the operator
    // accepted; overrideReason is required when overriddenIds is non-empty.
    std::string readinessToJson(const ReadinessSnapshot &snapshot,
                                const std::string &operatorName,
                                const std::string &profileName,
                                const std::vector<std::string> &overriddenIds,
                                const std::string &overrideReason);

    const char *checkStatusName(CheckStatus status);

} // namespace backend::services::checks
