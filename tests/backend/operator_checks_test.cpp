// UX-3 preflight checklist and UX-6 readiness gate classification rules
// (epic #304). Pure evaluator tests: pass, warning, failure, not-required,
// override eligibility, and provenance JSON serialization.
#include "backend/services/OperatorChecks.h"

#include "support/assert.h"

#include <algorithm>
#include <string>

using namespace backend::services::checks;

namespace
{

const CheckItem *find(const std::vector<CheckItem> &items, const std::string &id)
{
    const auto it = std::find_if(items.begin(), items.end(),
                                 [&](const CheckItem &i) { return i.id == id; });
    return it == items.end() ? nullptr : &*it;
}

PreflightFacts healthyPreflight()
{
    PreflightFacts facts;
    facts.cameraConfigured = true;
    facts.cameraLabel = "MockCam-01";
    facts.processingCoreReady = true;
    facts.processingCoreVersion = "0.2.0";
    facts.storagePathKnown = true;
    facts.storageWritable = true;
    facts.storageFreeGb = 120.0;
    facts.profileSelected = true;
    facts.profileName = "hl60-standard";
    return facts;
}

ReadinessFacts healthyReadiness()
{
    ReadinessFacts facts;
    facts.preflight = healthyPreflight();
    facts.preflightConfirmed = true;
    facts.alignmentConfirmed = true;
    facts.captureRunning = true;
    facts.roiValid = true;
    facts.backgroundReady = true;
    facts.pixelToMicron = 0.4886;
    facts.profileApplied = true;
    facts.profileVerified = true;
    return facts;
}

} // namespace

int main()
{
    // --- Preflight: all healthy ------------------------------------------
    {
        const auto items = evaluatePreflight(healthyPreflight());
        MIB_REQUIRE(items.size() == 4, "preflight covers camera/core/storage/profile");
        for (const auto &item : items)
        {
            MIB_EXPECT(item.status == CheckStatus::Passed,
                       "healthy facts pass every check (" + item.id + ")");
        }
        MIB_EXPECT(preflightPassed(items), "healthy preflight passes overall");
    }

    // --- Preflight: failures carry recovery actions -----------------------
    {
        PreflightFacts facts; // nothing configured
        facts.cameraDiscoveryFailed = true;
        const auto items = evaluatePreflight(facts);
        MIB_EXPECT(!preflightPassed(items), "unconfigured preflight fails");
        for (const auto &item : items)
        {
            if (item.status == CheckStatus::Failed || item.status == CheckStatus::Warning)
            {
                MIB_EXPECT(!item.recoveryAction.empty(),
                           "non-passing checks name a recovery action (" + item.id + ")");
            }
        }
        const auto *camera = find(items, "camera");
        MIB_REQUIRE(camera != nullptr, "camera check present");
        MIB_EXPECT(camera->status == CheckStatus::Failed, "no camera fails");
        MIB_EXPECT(camera->detail.find("discovery") != std::string::npos,
                   "failed discovery is distinguished from never-attempted");
    }

    // --- Preflight: low disk is a warning, unwritable is a failure --------
    {
        auto facts = healthyPreflight();
        facts.storageFreeGb = 1.0;
        auto items = evaluatePreflight(facts);
        const auto *storage = find(items, "storage");
        MIB_REQUIRE(storage != nullptr, "storage check present");
        MIB_EXPECT(storage->status == CheckStatus::Warning, "low disk warns");
        MIB_EXPECT(preflightPassed(items), "a warning does not fail preflight");

        facts.storageWritable = false;
        items = evaluatePreflight(facts);
        MIB_EXPECT(find(items, "storage")->status == CheckStatus::Failed,
                   "unwritable storage fails");
        MIB_EXPECT(!preflightPassed(items), "a failure fails preflight");
    }

    // --- Preflight: template session is a warning, not silent -------------
    {
        auto facts = healthyPreflight();
        facts.profileSelected = false;
        const auto *profile = find(evaluatePreflight(facts), "profile");
        MIB_REQUIRE(profile != nullptr, "profile check present");
        MIB_EXPECT(profile->status == CheckStatus::Warning,
                   "template/default session is flagged");
        MIB_EXPECT(profile->detail.find("unvalidated") != std::string::npos,
                   "template is labeled unvalidated");
    }

    // --- Alignment quality (UX-4) ------------------------------------------
    {
        AlignmentFacts facts;
        facts.captureRunning = true;
        facts.cameraFps = 30.0;
        facts.roiValid = true;
        facts.roiW = 512;
        facts.roiH = 96;
        facts.backgroundReady = true;
        facts.autofocusAvailable = true;
        facts.focusRingRatio = 0.42;
        facts.focusAgeMs = 100.0;
        facts.pixelToMicron = 0.4886;
        const auto items = evaluateAlignmentQuality(facts);
        for (const auto &item : items)
        {
            MIB_EXPECT(item.status == CheckStatus::Passed,
                       "healthy alignment passes every signal (" + item.id + ")");
        }

        AlignmentFacts stale = facts;
        stale.focusAgeMs = 60000.0;
        MIB_EXPECT(find(evaluateAlignmentQuality(stale), "focus")->status ==
                       CheckStatus::Warning,
                   "stale focus measurement warns instead of staying green");

        AlignmentFacts noAf = facts;
        noAf.autofocusAvailable = false;
        MIB_EXPECT(find(evaluateAlignmentQuality(noAf), "focus")->status ==
                       CheckStatus::NotRequired,
                   "missing autofocus reports Not required, not a failure");

        AlignmentFacts stopped = facts;
        stopped.captureRunning = false;
        MIB_EXPECT(find(evaluateAlignmentQuality(stopped), "stream")->status ==
                       CheckStatus::Failed,
                   "stopped camera fails the live-image signal");

        AlignmentFacts silent = facts;
        silent.cameraFps = 0.0;
        MIB_EXPECT(find(evaluateAlignmentQuality(silent), "stream")->status ==
                       CheckStatus::Warning,
                   "running capture with no frames warns");
    }

    // --- Readiness: healthy facts produce no blocks -----------------------
    {
        const auto snapshot = evaluateReadiness(healthyReadiness());
        MIB_EXPECT(!snapshot.hasBlocks, "healthy readiness has no blocks");
        MIB_EXPECT(!snapshot.hasWarnings, "healthy readiness has no warnings");
    }

    // --- Readiness: no profile blocks but is overridable (temporary run) --
    {
        auto facts = healthyReadiness();
        facts.preflight.profileSelected = false;
        const auto snapshot = evaluateReadiness(facts);
        MIB_EXPECT(snapshot.hasBlocks, "missing profile blocks a normal start");
        const auto *profile = find(snapshot.items, "profile");
        MIB_REQUIRE(profile != nullptr, "profile item present");
        MIB_EXPECT(profile->status == CheckStatus::Failed, "missing profile is a block");
        MIB_EXPECT(profile->overridable, "temporary/expert session may override");
    }

    // --- Readiness: hard safety conditions are never overridable ----------
    {
        auto facts = healthyReadiness();
        facts.captureRunning = false;
        facts.preflight.processingCoreReady = false;
        facts.roiValid = false;
        const auto snapshot = evaluateReadiness(facts);
        for (const char *id : {"capture", "core", "roi"})
        {
            const auto *item = find(snapshot.items, id);
            MIB_REQUIRE(item != nullptr, std::string("item present: ") + id);
            MIB_EXPECT(item->status == CheckStatus::Failed,
                       std::string("hard condition fails: ") + id);
            MIB_EXPECT(!item->overridable,
                       std::string("hard condition not overridable: ") + id);
        }
    }

    // --- Readiness: unapplied profile and unconfirmed stages warn ---------
    {
        auto facts = healthyReadiness();
        facts.profileApplied = false;
        facts.profileVerified = false;
        facts.preflightConfirmed = false;
        facts.alignmentConfirmed = false;
        facts.backgroundReady = false;
        facts.pixelToMicron = 0.0;
        const auto snapshot = evaluateReadiness(facts);
        MIB_EXPECT(!snapshot.hasBlocks, "warnings alone do not block");
        MIB_EXPECT(snapshot.hasWarnings, "warnings are reported");
        for (const char *id :
             {"profile-applied", "preflight-confirmed", "alignment", "background",
              "calibration"})
        {
            const auto *item = find(snapshot.items, id);
            MIB_REQUIRE(item != nullptr, std::string("item present: ") + id);
            MIB_EXPECT(item->status == CheckStatus::Warning,
                       std::string("soft condition warns: ") + id);
        }
    }

    // --- Readiness: double start is blocked --------------------------------
    {
        auto facts = healthyReadiness();
        facts.experimentActive = true;
        const auto snapshot = evaluateReadiness(facts);
        const auto *active = find(snapshot.items, "active");
        MIB_REQUIRE(active != nullptr, "active-experiment item present");
        MIB_EXPECT(active->status == CheckStatus::Failed && !active->overridable,
                   "double start is a non-overridable block");
    }

    // --- Provenance JSON ----------------------------------------------------
    {
        auto facts = healthyReadiness();
        facts.preflight.profileSelected = false;
        const auto snapshot = evaluateReadiness(facts);
        const std::string json = readinessToJson(snapshot, "jdoe", "",
                                                 {"profile"},
                                                 "validated manually for demo sample");
        MIB_EXPECT(json.find("\"operator\":\"jdoe\"") != std::string::npos,
                   "operator recorded");
        MIB_EXPECT(json.find("\"overridden_checks\":[\"profile\"]") != std::string::npos,
                   "override target recorded");
        MIB_EXPECT(json.find("validated manually") != std::string::npos,
                   "override reason recorded");
        MIB_EXPECT(json.find("\"status\":\"failed\"") != std::string::npos,
                   "check statuses serialized");

        const std::string clean =
            readinessToJson(evaluateReadiness(healthyReadiness()), "jdoe",
                            "hl60-standard", {}, "");
        MIB_EXPECT(clean.find("override") == std::string::npos,
                   "no override record when nothing was overridden");
    }

    return mib::test::exitCode();
}
