#include "backend/services/OperatorChecks.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace backend::services::checks
{

namespace
{

CheckItem makeItem(const char *id, const char *label, CheckStatus status,
                   std::string detail, std::string recovery, bool overridable = false)
{
    CheckItem item;
    item.id = id;
    item.label = label;
    item.status = status;
    item.detail = std::move(detail);
    item.recoveryAction = std::move(recovery);
    item.overridable = overridable;
    return item;
}

std::string formatGb(double gb)
{
    std::ostringstream out;
    out.precision(1);
    out << std::fixed << gb << " GB free";
    return out.str();
}

CheckItem cameraCheck(const PreflightFacts &facts)
{
    if (facts.cameraConfigured)
    {
        const std::string label = facts.cameraLabel.empty() ? "camera" : facts.cameraLabel;
        return makeItem("camera", "Camera", CheckStatus::Passed,
                        facts.mockCamera ? ("Mock camera: " + label +
                                            " (training/simulation, no hardware)")
                                         : ("Connected: " + label),
                        {});
    }
    if (facts.cameraDiscoveryFailed)
    {
        return makeItem("camera", "Camera", CheckStatus::Failed,
                        "No camera found by discovery.",
                        "Check power and cabling, then retry discovery.");
    }
    return makeItem("camera", "Camera", CheckStatus::Failed,
                    "No camera selected.",
                    "Connect a camera (hardware or mock) on the Connect tab.");
}

CheckItem coreCheck(const PreflightFacts &facts)
{
    if (facts.processingCoreReady)
    {
        return makeItem("core", "Processing core", CheckStatus::Passed,
                        facts.processingCoreVersion.empty()
                            ? "Active"
                            : ("Active: " + facts.processingCoreVersion),
                        {});
    }
    return makeItem("core", "Processing core", CheckStatus::Failed,
                    "The required (pinned) processing core is not active.",
                    "Select the core in Settings > Processing Core.");
}

CheckItem storageCheck(const PreflightFacts &facts)
{
    if (!facts.storagePathKnown)
    {
        return makeItem("storage", "Data storage", CheckStatus::Warning,
                        "Storage location could not be inspected.",
                        "Verify the data folder exists and is accessible.");
    }
    if (!facts.storageWritable)
    {
        return makeItem("storage", "Data storage", CheckStatus::Failed,
                        "The data folder is not writable.",
                        "Choose a writable output location or fix permissions.");
    }
    if (facts.storageFreeGb < facts.storageMinFreeGb)
    {
        return makeItem("storage", "Data storage", CheckStatus::Warning,
                        formatGb(facts.storageFreeGb) + " (below the " +
                            formatGb(facts.storageMinFreeGb) + " threshold)",
                        "Free disk space before running long experiments.");
    }
    return makeItem("storage", "Data storage", CheckStatus::Passed,
                    formatGb(facts.storageFreeGb), {});
}

CheckItem profileCheck(const PreflightFacts &facts)
{
    if (!facts.profileSelected)
    {
        return makeItem("profile", "Experiment profile", CheckStatus::Warning,
                        "No profile selected - the built-in template is unvalidated.",
                        "Select or create an experiment profile (Settings > Profiles).");
    }
    if (facts.profileIncompatible)
    {
        return makeItem("profile", "Experiment profile", CheckStatus::Failed,
                        "Profile '" + facts.profileName +
                            "' is incompatible with this app/processing core.",
                        "Select a compatible profile or update the app/profile.");
    }
    return makeItem("profile", "Experiment profile", CheckStatus::Passed,
                    "Selected: " + facts.profileName, {});
}

} // namespace

std::vector<CheckItem> evaluatePreflight(const PreflightFacts &facts)
{
    std::vector<CheckItem> items;
    items.push_back(cameraCheck(facts));
    items.push_back(coreCheck(facts));
    items.push_back(storageCheck(facts));
    items.push_back(profileCheck(facts));
    return items;
}

bool preflightPassed(const std::vector<CheckItem> &items)
{
    for (const auto &item : items)
    {
        if (item.status == CheckStatus::Failed)
        {
            return false;
        }
    }
    return true;
}

std::vector<CheckItem> evaluateAlignmentQuality(const AlignmentFacts &facts)
{
    std::vector<CheckItem> items;

    if (facts.captureRunning && facts.cameraFps > 0.0)
    {
        std::ostringstream detail;
        detail.precision(0);
        detail << std::fixed << "Streaming at " << facts.cameraFps << " fps.";
        items.push_back(makeItem("stream", "Live image", CheckStatus::Passed,
                                 detail.str(), {}));
    }
    else if (facts.captureRunning)
    {
        items.push_back(makeItem("stream", "Live image", CheckStatus::Warning,
                                 "Capture is running but no frames are arriving.",
                                 "Check the camera connection and transport."));
    }
    else
    {
        items.push_back(makeItem("stream", "Live image", CheckStatus::Failed,
                                 "The camera is not running.",
                                 "Start the camera to view the live image."));
    }

    if (facts.roiValid)
    {
        std::ostringstream detail;
        detail << facts.roiW << " x " << facts.roiH << " px.";
        items.push_back(makeItem("roi", "ROI", CheckStatus::Passed, detail.str(), {}));
    }
    else
    {
        items.push_back(makeItem("roi", "ROI", CheckStatus::Failed,
                                 "The ROI is empty or invalid.",
                                 "Drag a valid ROI on the live image."));
    }

    items.push_back(facts.backgroundReady
                        ? makeItem("background", "Background reference",
                                   CheckStatus::Passed, "Background image captured.", {})
                        : makeItem("background", "Background reference",
                                   CheckStatus::Warning,
                                   "No background reference captured yet.",
                                   "Capture or set a background in the Preview page.",
                                   true));

    if (!facts.autofocusAvailable)
    {
        items.push_back(makeItem("focus", "Focus", CheckStatus::NotRequired,
                                 "Autofocus service not available in this session.", {}));
    }
    else if (facts.focusRingRatio <= 0.0)
    {
        items.push_back(makeItem("focus", "Focus", CheckStatus::Warning,
                                 "No focus measurement yet.",
                                 "Start the camera with cells in view, or run Auto Focus."));
    }
    else if (facts.focusAgeMs > 5000.0)
    {
        items.push_back(makeItem("focus", "Focus", CheckStatus::Warning,
                                 "Focus measurement is stale.",
                                 "Verify the live image before trusting the focus score."));
    }
    else
    {
        std::ostringstream detail;
        detail.precision(3);
        detail << std::fixed << "Ring width " << facts.focusRingRatio << ".";
        items.push_back(makeItem("focus", "Focus", CheckStatus::Passed, detail.str(), {}));
    }

    items.push_back(facts.pixelToMicron > 0.0
                        ? makeItem("calibration", "Pixel-to-micron calibration",
                                   CheckStatus::Passed, "Factor set.", {})
                        : makeItem("calibration", "Pixel-to-micron calibration",
                                   CheckStatus::Warning,
                                   "No calibration factor configured.",
                                   "Set it in Settings > Pixel to Micron Conversion.",
                                   true));

    return items;
}

ReadinessSnapshot evaluateReadiness(const ReadinessFacts &facts)
{
    ReadinessSnapshot snapshot;
    auto &items = snapshot.items;

    // System-critical, never overridable ------------------------------
    items.push_back(cameraCheck(facts.preflight));
    items.back().overridable = false;

    if (facts.captureRunning)
    {
        items.push_back(makeItem("capture", "Camera stream", CheckStatus::Passed,
                                 "Live and capturing.", {}));
    }
    else
    {
        items.push_back(makeItem("capture", "Camera stream", CheckStatus::Failed,
                                 "The camera is not running.",
                                 "Start the camera before starting an experiment."));
    }

    items.push_back(coreCheck(facts.preflight));
    items.back().overridable = false;

    items.push_back(storageCheck(facts.preflight));
    // A storage *warning* (low space) may be overridden; a hard failure not.
    items.back().overridable = items.back().status == CheckStatus::Warning;

    if (!facts.lastExperimentSaveOk)
    {
        items.push_back(makeItem("last-save", "Previous save", CheckStatus::Failed,
                                 "The previous experiment save failed.",
                                 "Check disk space and the application log.", true));
    }

    // Method / profile ------------------------------------------------
    if (!facts.preflight.profileSelected)
    {
        items.push_back(makeItem(
            "profile", "Experiment profile", CheckStatus::Failed,
            "No experiment profile selected - built-in defaults are an unvalidated template.",
            "Select or create a profile, or override for an explicitly temporary session.",
            true));
    }
    else if (facts.preflight.profileIncompatible)
    {
        items.push_back(makeItem("profile", "Experiment profile", CheckStatus::Failed,
                                 "Profile '" + facts.preflight.profileName +
                                     "' is incompatible.",
                                 "Select a compatible profile.", false));
    }
    else if (facts.profileDirty)
    {
        items.push_back(makeItem("profile", "Experiment profile", CheckStatus::Warning,
                                 "Profile '" + facts.preflight.profileName +
                                     "' has unsaved/diverged changes.",
                                 "Save the profile or revert the changes.", true));
    }
    else
    {
        items.push_back(makeItem("profile", "Experiment profile", CheckStatus::Passed,
                                 "Selected: " + facts.preflight.profileName, {}));
    }

    if (facts.preflight.profileSelected)
    {
        if (facts.profileVerified)
        {
            items.push_back(makeItem("profile-applied", "Profile applied to hardware",
                                     CheckStatus::Passed,
                                     "Applied and verified on the connected devices.", {}));
        }
        else if (facts.profileApplied)
        {
            items.push_back(makeItem("profile-applied", "Profile applied to hardware",
                                     CheckStatus::Warning,
                                     "Applied, but not fully verified.",
                                     "Re-run Apply & Verify Profile.", true));
        }
        else
        {
            items.push_back(makeItem("profile-applied", "Profile applied to hardware",
                                     CheckStatus::Warning,
                                     "The selected profile has not been applied and "
                                     "verified this session.",
                                     "Run Apply & Verify Profile.", true));
        }
    }

    // Operator confirmations / alignment ------------------------------
    items.push_back(facts.preflightConfirmed
                        ? makeItem("preflight-confirmed", "Preflight confirmed",
                                   CheckStatus::Passed, "Confirmed by the operator.", {})
                        : makeItem("preflight-confirmed", "Preflight confirmed",
                                   CheckStatus::Warning,
                                   "Hardware preflight has not been confirmed.",
                                   "Confirm preflight in the workflow bar.", true));

    items.push_back(facts.alignmentConfirmed
                        ? makeItem("alignment", "Alignment & ROI", CheckStatus::Passed,
                                   "Confirmed by the operator.", {})
                        : makeItem("alignment", "Alignment & ROI", CheckStatus::Warning,
                                   "Camera alignment and ROI are not confirmed.",
                                   "Confirm alignment in the workflow bar.", true));

    items.push_back(facts.roiValid
                        ? makeItem("roi", "ROI", CheckStatus::Passed, "Valid ROI set.", {})
                        : makeItem("roi", "ROI", CheckStatus::Failed,
                                   "The ROI is empty or invalid.",
                                   "Define a valid ROI on the Overview tab."));

    items.push_back(facts.backgroundReady
                        ? makeItem("background", "Background reference",
                                   CheckStatus::Passed, "Background image available.", {})
                        : makeItem("background", "Background reference",
                                   CheckStatus::Warning,
                                   "No background reference captured.",
                                   "Capture/set a background in the Preview page.", true));

    items.push_back(facts.pixelToMicron > 0.0
                        ? makeItem("calibration", "Pixel-to-micron calibration",
                                   CheckStatus::Passed, "Factor set.", {})
                        : makeItem("calibration", "Pixel-to-micron calibration",
                                   CheckStatus::Warning,
                                   "No calibration factor configured.",
                                   "Set it in Settings > Pixel to Micron Conversion.",
                                   true));

    if (facts.experimentActive)
    {
        items.push_back(makeItem("active", "Experiment state", CheckStatus::Failed,
                                 "An experiment is already running.",
                                 "Stop the current experiment first."));
    }

    for (const auto &item : items)
    {
        snapshot.hasBlocks = snapshot.hasBlocks || item.status == CheckStatus::Failed;
        snapshot.hasWarnings = snapshot.hasWarnings || item.status == CheckStatus::Warning;
    }
    return snapshot;
}

const char *checkStatusName(CheckStatus status)
{
    switch (status)
    {
    case CheckStatus::Passed: return "passed";
    case CheckStatus::Warning: return "warning";
    case CheckStatus::Failed: return "failed";
    case CheckStatus::NotRequired: return "not_required";
    }
    return "unknown";
}

std::string readinessToJson(const ReadinessSnapshot &snapshot,
                            const std::string &operatorName,
                            const std::string &profileName,
                            const std::vector<std::string> &overriddenIds,
                            const std::string &overrideReason)
{
    nlohmann::json root;
    root["schema_version"] = 1;
    root["operator"] = operatorName;
    root["profile"] = profileName;
    root["has_blocks"] = snapshot.hasBlocks;
    root["has_warnings"] = snapshot.hasWarnings;

    nlohmann::json items = nlohmann::json::array();
    for (const auto &item : snapshot.items)
    {
        nlohmann::json entry;
        entry["id"] = item.id;
        entry["label"] = item.label;
        entry["status"] = checkStatusName(item.status);
        entry["detail"] = item.detail;
        entry["overridable"] = item.overridable;
        items.push_back(std::move(entry));
    }
    root["checks"] = std::move(items);

    if (!overriddenIds.empty())
    {
        nlohmann::json record;
        record["overridden_checks"] = overriddenIds;
        record["reason"] = overrideReason;
        root["override"] = std::move(record);
    }
    return root.dump();
}

} // namespace backend::services::checks
