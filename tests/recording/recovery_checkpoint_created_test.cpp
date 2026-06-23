// recovery_checkpoint_created_test
//
// Guards the FIX for crash-resilience on Windows: the rolling ".recovery.h5"
// checkpoint is written by copy_file of the still-open HDF5 file. HDF5's
// byte-range file lock previously made that copy fail on Windows every time, so
// the checkpoint was silently never created (the auto-recovery feature was
// non-functional on the production platform). openFile now disables HDF5 file
// locking so the copy succeeds. This test asserts the checkpoint is actually
// created during an append and is itself a valid, loadable HDF5 file.

#include "backend/recording/Hdf5Service.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>
#include <vector>

using backend::services::Hdf5Service;

int main()
{
    mib::test::TempDir td("mib_recovery_created");
    const std::string path = (td / "recording.h5").string();
    const std::string recovery = path + ".recovery.h5";

    {
        Hdf5Service h;
        MIB_REQUIRE(h.openFile(path), "openFile");
        MIB_REQUIRE(h.initializeRecordingDatasets(), "init recording datasets");

        std::vector<cv::Mat> frames{cv::Mat(8, 10, CV_8UC1, cv::Scalar(40)),
                                    cv::Mat(8, 10, CV_8UC1, cv::Scalar(80))};
        std::vector<Hdf5Service::RecordingFrameMeta> meta{{0, 1000, 10, 8},
                                                          {1, 2000, 10, 8}};
        MIB_REQUIRE(h.appendRecordingFrames(frames, meta), "append recording frames");

        // The rolling checkpoint must exist after the append. Before the
        // file-locking fix this failed on Windows (copy_file blocked by the
        // HDF5 lock).
        MIB_EXPECT(std::filesystem::exists(recovery),
                   "recovery checkpoint created during append");

        MIB_REQUIRE(h.writeRecordingInfo(1000, 2000, 2, 0), "write recording info");
        h.closeFile();
    }

    MIB_EXPECT(std::filesystem::exists(recovery),
               "recovery checkpoint persists after close");

    // The checkpoint must itself be a valid, loadable HDF5 file with the data.
    {
        Hdf5Service r;
        MIB_REQUIRE(r.loadFile(recovery), "recovery checkpoint is a loadable HDF5 file");
        uint64_t start = 0, end = 0, total = 0, filtered = 0;
        MIB_REQUIRE(r.readRecordingInfo(start, end, total, filtered),
                    "read recording info from checkpoint");
        MIB_EXPECT(total == 2, "checkpoint holds the appended frames");
        r.closeFile();
    }

    if (mib::test::exitCode() == 0) {
        std::printf("recovery checkpoint is created during append and is loadable\n");
    }
    return mib::test::exitCode();
}
