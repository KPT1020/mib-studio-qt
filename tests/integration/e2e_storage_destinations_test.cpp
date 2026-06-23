// e2e_storage_destinations_test
//
// Reproduces user complaint: "can save experiment file on one drive but not
// the other." The realistic trigger is choosing a destination folder that does
// not yet exist (e.g. a fresh path on a second/external/network drive). The UI
// hands the chosen path straight to Hdf5Service::openFile(), which calls
// H5Fcreate() without creating the parent directory and reports only a generic
// error. So the same workflow that works in an existing folder fails on a new
// one.
//
// This test drives the real save path (Hdf5Service: openFile ->
// initializeRecordingDatasets -> appendRecordingFrames -> writeRecordingInfo ->
// closeFile -> loadFile -> verify) against several destination shapes that
// mirror picking different drives/folders. A correct application must be able to
// save to a freshly chosen folder; scenarios that "should succeed" gate the
// exit code, so a failure here is a reproduced bug.

#include "backend/recording/Hdf5Service.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using backend::services::Hdf5Service;

namespace {

std::string randomTag()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return std::to_string(dist(gen));
}

cv::Mat makeFrame(unsigned char value)
{
    return cv::Mat(8, 10, CV_8UC1, cv::Scalar(value));
}

// Performs the full experiment-save round trip to `target` and verifies the
// data survived. Returns true only if every step succeeded. Detailed failure
// reason is written to `reason`.
bool saveRoundTrip(const fs::path& target, std::string& reason)
{
    Hdf5Service hdf5;

    if (!hdf5.openFile(target.string())) {
        reason = "openFile() returned false (H5Fcreate failed for this path)";
        return false;
    }
    if (!hdf5.initializeRecordingDatasets()) {
        reason = "initializeRecordingDatasets() failed";
        hdf5.closeFile();
        return false;
    }

    std::vector<cv::Mat> batch{makeFrame(30), makeFrame(60), makeFrame(90)};
    std::vector<Hdf5Service::RecordingFrameMeta> meta{
        {0, 1000, 10, 8}, {1, 2000, 10, 8}, {2, 3000, 10, 8}};
    if (!hdf5.appendRecordingFrames(batch, meta)) {
        reason = "appendRecordingFrames() failed (write to this destination)";
        hdf5.closeFile();
        return false;
    }
    if (!hdf5.writeRecordingInfo(1000, 3000, 3, 0, false, 1)) {
        reason = "writeRecordingInfo() failed";
        hdf5.closeFile();
        return false;
    }
    hdf5.closeFile();

    // Reopen and confirm the experiment is readable from this destination.
    Hdf5Service reader;
    if (!reader.loadFile(target.string())) {
        reason = "loadFile() failed to reopen saved file";
        return false;
    }
    uint64_t start = 0, end = 0, total = 0, filtered = 0;
    if (!reader.readRecordingInfo(start, end, total, filtered) || total != 3) {
        reason = "readRecordingInfo() did not round-trip (total=" +
                 std::to_string(total) + ")";
        reader.closeFile();
        return false;
    }
    reader.closeFile();
    return true;
}

struct Scenario {
    std::string name;
    fs::path target;
    bool mustSucceed;   // gates exit code: correct app must handle this
    std::string note;
};

void cleanup(const fs::path& root)
{
    std::error_code ec;
    fs::remove_all(root, ec);
}

} // namespace

int main()
{
    const fs::path base = fs::temp_directory_path() /
                          ("mib_e2e_storage_" + randomTag());
    std::error_code ec;
    fs::create_directories(base, ec);

    std::vector<Scenario> scenarios;

    // 1. Control: destination folder already exists (the case that "works").
    {
        fs::path dir = base / "existing_folder";
        fs::create_directories(dir, ec);
        scenarios.push_back({"existing_folder", dir / "experiment.h5", true,
                             "control: folder pre-exists"});
    }

    // 2. THE BUG: a freshly chosen folder that does not exist yet. This is what
    //    happens when a user points the save dialog at a new directory on a
    //    different drive. A correct app creates it; current code fails.
    {
        fs::path dir = base / "fresh_drive_folder";  // intentionally not created
        scenarios.push_back({"fresh_nonexistent_folder", dir / "experiment.h5",
                             true, "user picks a new folder on another drive"});
    }

    // 3. Deeply nested fresh path (e.g. D:\Experiments\2026\Subject\Trial\).
    {
        fs::path dir = base / "a" / "b" / "c" / "d";  // not created
        scenarios.push_back({"deep_nonexistent_path", dir / "experiment.h5",
                             true, "multi-level new directory tree"});
    }

    // 4. Long path (> Windows MAX_PATH 260). Genuinely OS/config dependent, so
    //    it is reported but does not gate the exit code.
    {
        std::string longSeg(80, 'x');
        fs::path dir = base / longSeg / longSeg / longSeg;  // ~ 240+ chars
        scenarios.push_back({"long_path_gt_260", dir / "experiment_file.h5",
                             false, "path length exceeds Windows MAX_PATH"});
    }

    std::cout << "=== e2e storage destinations ===\n";
    std::cout << "base: " << base.string() << "\n\n";

    int gatedFailures = 0;
    int reportedFailures = 0;

    for (const auto& s : scenarios) {
        std::string reason;
        const bool ok = saveRoundTrip(s.target, reason);
        const char* verdict = ok ? "PASS" : "FAIL";
        std::cout << "[" << verdict << "] " << s.name
                  << (s.mustSucceed ? " (must-succeed)" : " (informational)")
                  << "\n        " << s.note << "\n"
                  << "        path len=" << s.target.string().size()
                  << " target=" << s.target.string() << "\n";
        if (!ok) {
            std::cout << "        reason: " << reason << "\n";
            if (s.mustSucceed) {
                ++gatedFailures;
            } else {
                ++reportedFailures;
            }
        }
        std::cout << "\n";
    }

    // Graceful-failure: parent path is a regular file. openFile() must fail
    // cleanly (return false) and must not crash. Reaching the line after the
    // call at all proves no crash; an unexpected success would be a bug.
    {
        const fs::path blocker = base / "blocker_file";
        { std::ofstream o(blocker.string(), std::ios::binary); o << "x"; }
        const fs::path target = blocker / "experiment.h5";  // parent is a file
        std::string reason;
        const bool ok = saveRoundTrip(target, reason);
        std::cout << "[" << (ok ? "FAIL" : "PASS")
                  << "] parent_is_a_file (must-fail-gracefully)\n"
                     "        openFile under a file-as-parent must fail cleanly\n";
        if (ok) {
            std::cout << "        BUG: saving under a file-as-parent should fail "
                         "gracefully, not succeed\n";
            ++gatedFailures;
        } else {
            std::cout << "        failed cleanly: " << reason << "\n";
        }
        std::cout << "\n";
    }

    cleanup(base);

    std::cout << "gated (must-succeed) failures: " << gatedFailures << "\n";
    std::cout << "informational failures: " << reportedFailures << "\n";

    if (gatedFailures > 0) {
        std::cout << "\nBUG REPRODUCED: experiment save fails for valid "
                     "destinations that a correct app should handle "
                     "(matches 'can save on one drive but not the other').\n";
        return 1;
    }
    std::cout << "\nAll must-succeed destinations saved correctly.\n";
    return 0;
}
