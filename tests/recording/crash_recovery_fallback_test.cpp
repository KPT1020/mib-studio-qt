// crash_recovery_fallback_test
//
// Guards the crash-resilience fallback: when the primary HDF5 file cannot be
// opened (interrupted write / corruption), loadFile() must transparently fall
// back to the "<file>.recovery.h5" checkpoint and read the data. We build the
// recovery file and a corrupt primary deterministically (independent of the
// auto-checkpoint, which is unreliable on Windows due to file locking during
// copy_file of the open file).

#include "backend/recording/Hdf5Service.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <opencv2/core.hpp>

#include <fstream>
#include <string>
#include <vector>

using backend::services::Hdf5Service;

int main()
{
    mib::test::TempDir td("mib_crash_recovery");
    const std::string primary = (td / "experiment.h5").string();
    const std::string recovery = primary + ".recovery.h5";

    // Write a valid recording file directly at the recovery path.
    {
        Hdf5Service h;
        MIB_REQUIRE(h.openFile(recovery), "open recovery file");
        MIB_REQUIRE(h.initializeRecordingDatasets(), "init recording datasets");
        std::vector<cv::Mat> frames{cv::Mat(8, 10, CV_8UC1, cv::Scalar(50)),
                                    cv::Mat(8, 10, CV_8UC1, cv::Scalar(90))};
        std::vector<Hdf5Service::RecordingFrameMeta> meta{{0, 1000, 10, 8},
                                                          {1, 2000, 10, 8}};
        MIB_REQUIRE(h.appendRecordingFrames(frames, meta), "append recording frames");
        MIB_REQUIRE(h.writeRecordingInfo(1000, 2000, 2, 0), "write recording info");
        h.closeFile();
    }

    // Corrupt the primary so H5Fopen on it fails.
    {
        std::ofstream o(primary, std::ios::binary | std::ios::trunc);
        o << "this is not a valid HDF5 file";
    }

    // loadFile(primary) must fall back to the recovery checkpoint.
    {
        Hdf5Service r;
        MIB_REQUIRE(r.loadFile(primary), "loadFile falls back to recovery checkpoint");
        MIB_EXPECT(r.isRecordingFile(), "recovered file detected as recording mode");

        uint64_t start = 0, end = 0, total = 0, filtered = 0;
        MIB_REQUIRE(r.readRecordingInfo(start, end, total, filtered),
                    "read recording info from recovery");
        MIB_EXPECT(total == 2, "frame count recovered from checkpoint");

        std::vector<backend::services::ProcessedFrame> md;
        MIB_REQUIRE(r.readRecordingMetadata(md), "read recording metadata from recovery");
        MIB_EXPECT(md.size() == 2, "metadata rows recovered");
        r.closeFile();
    }

    if (mib::test::exitCode() == 0) {
        std::printf("loadFile falls back to recovery checkpoint and reads data\n");
    }
    return mib::test::exitCode();
}
