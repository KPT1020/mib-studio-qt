#pragma once

#include <filesystem>
#include <string>

namespace backend::diagnostics {

// Startup lifeline for diagnosing silent launch failures.
//
// The application writes a small marker file stage-by-stage while it boots
// and removes it once the main window is visible. When a launch dies without
// ever showing UI — a crash swallowed by the crash handler (which suppresses
// the Windows error dialog by design), a process killed by antivirus, a
// loader failure after main() started — the marker survives, and the NEXT
// launch finds it and can tell the user exactly how far the previous attempt
// got instead of failing silently again.
//
// Qt-free on purpose: lives in the backend so it is unit-testable in the
// linux-backend-only lane and usable before QApplication exists.
class StartupProbe {
public:
    struct PreviousAttempt {
        bool found = false;   // stale marker from an earlier, unfinished launch
        std::string stage;    // last stage that launch reached
        std::string detail;   // full marker content (stage/version/pid/utc)
    };

    // pidForTesting <= 0 means "use the real current process id". Tests pass
    // a fake pid so a marker they write reads as a dead previous instance.
    explicit StartupProbe(long pidForTesting = -1);

    // Reads and consumes any stale marker in markerDir, then arms a fresh
    // marker for this run (initial stage "begin"). A marker whose recorded
    // pid is still alive belongs to a concurrently starting instance, not a
    // failed launch, and is not reported. Never throws; on filesystem
    // failure the probe degrades to inactive and stage()/complete() no-op.
    PreviousAttempt begin(const std::filesystem::path& markerDir,
                          const std::string& appVersion);

    // Records that startup progressed to `name` (overwrites the marker).
    void stage(const std::string& name);

    // Startup finished (UI visible): removes the marker.
    void complete();

    const std::string& currentStage() const { return currentStage_; }
    static const char* markerFileName();

private:
    void write();

    std::filesystem::path markerPath_;
    std::string appVersion_;
    std::string currentStage_;
    long pid_ = -1;
    bool active_ = false;
};

} // namespace backend::diagnostics
