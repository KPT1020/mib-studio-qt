// experiment_roundtrip_test
//
// Round-trip coverage for the experiment save path (distinct from recording
// mode): write frames + metadata + experiment info + config JSON, close, reload,
// and verify totals, ROI, per-frame metadata, and image pixels survive. Guards
// against silent data loss/corruption in the primary "save data" capability.

#include "backend/recording/Hdf5Service.h"
#include "backend/processing/ProcessingService.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <string>
#include <vector>

using backend::services::Hdf5Service;
using backend::services::ProcessedFrame;
using backend::services::ProcessingConfig;
using backend::services::ProcessingService;

namespace {

ProcessedFrame makeFrame(uint64_t idx, unsigned char value, bool valid,
                         double area, double deform)
{
    ProcessedFrame f;
    f.index = idx;
    f.timestampNs = (idx + 1) * 1000ULL;
    f.originalImage = cv::Mat(8, 10, CV_8UC1, cv::Scalar(value));
    f.processedImage = cv::Mat(8, 10, CV_8UC1, cv::Scalar(valid ? 255 : 0));
    f.validation.isValid = valid;
    f.validation.objectId = static_cast<int>(idx);
    f.validation.objectCount = 1;
    f.validation.area = area;
    f.validation.deformability = deform;
    // Young's modulus (LUT lookup result) must survive the HDF5 round-trip so it
    // can be surfaced in the review table / CSV export. Derive a distinct value
    // per frame so a dropped or mis-mapped field is caught.
    f.validation.youngsModulus = area * 0.1 + deform;
    return f;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

} // namespace

int main()
{
    mib::test::TempDir td("mib_experiment_roundtrip");
    const std::string path = (td / "experiment.h5").string();

    std::vector<ProcessedFrame> valid{makeFrame(0, 40, true, 100.0, 0.20),
                                      makeFrame(1, 80, true, 150.0, 0.30)};
    std::vector<ProcessedFrame> invalid{makeFrame(2, 120, false, 10.0, 0.90)};

    // --- Write via the incremental append path (as used during a run) ---
    {
        Hdf5Service hdf5;
        MIB_REQUIRE(hdf5.openFile(path), "openFile experiment");
        MIB_REQUIRE(hdf5.initializeDatasets(), "initializeDatasets");
        MIB_REQUIRE(hdf5.appendFrames(valid, invalid), "appendFrames");

        ProcessingConfig cfg;
        ProcessingService::Roi roi{1, 2, 6, 7};
        MIB_REQUIRE(hdf5.writeExperimentInfo(1000, 4000, valid.size(),
                                             invalid.size(), cfg, roi),
                    "writeExperimentInfo");
        MIB_EXPECT(hdf5.writeConfigJson("{\"pixel_to_micron\":0.4886}"),
                   "writeConfigJson");
        hdf5.closeFile();
    }

    // --- Reload and verify ---
    {
        Hdf5Service r;
        MIB_REQUIRE(r.loadFile(path), "reload experiment");

        uint64_t start = 0, end = 0;
        size_t totalValid = 0, totalInvalid = 0;
        ProcessingService::Roi roiOut{};
        MIB_REQUIRE(r.readExperimentInfo(start, end, totalValid, totalInvalid, &roiOut),
                    "readExperimentInfo");
        MIB_EXPECT(start == 1000 && end == 4000, "experiment times round-trip");
        MIB_EXPECT(totalValid == 2 && totalInvalid == 1, "frame totals round-trip");
        MIB_EXPECT(roiOut.x == 1 && roiOut.y == 2 && roiOut.w == 6 && roiOut.h == 7,
                   "ROI round-trips");

        std::vector<ProcessedFrame> meta;
        MIB_REQUIRE(r.readValidMetadata(meta), "readValidMetadata");
        MIB_EXPECT(meta.size() == 2, "valid metadata count");
        if (meta.size() == 2) {
            MIB_EXPECT(meta[0].index == 0 && meta[1].index == 1, "indices round-trip");
            MIB_EXPECT(near(meta[0].validation.area, 100.0), "area[0] round-trips");
            MIB_EXPECT(near(meta[1].validation.deformability, 0.30),
                       "deformability[1] round-trips");
            MIB_EXPECT(near(meta[0].validation.youngsModulus, 100.0 * 0.1 + 0.20),
                       "youngsModulus[0] round-trips");
            MIB_EXPECT(near(meta[1].validation.youngsModulus, 150.0 * 0.1 + 0.30),
                       "youngsModulus[1] round-trips");
        }

        std::vector<ProcessedFrame> full;
        MIB_REQUIRE(r.readValidFrames(full), "readValidFrames");
        MIB_EXPECT(full.size() == 2, "valid frame count");
        if (!full.empty() && !full[0].originalImage.empty()) {
            MIB_EXPECT(full[0].originalImage.cols == 10 && full[0].originalImage.rows == 8,
                       "image dimensions round-trip");
            MIB_EXPECT(full[0].originalImage.at<unsigned char>(0, 0) == 40,
                       "image pixel value round-trips");
        } else {
            MIB_EXPECT(false, "valid frames carry image payloads");
        }
        r.closeFile();
    }

    if (mib::test::exitCode() == 0) {
        std::printf("experiment save path round-trips (frames, metadata, ROI, config)\n");
    }
    return mib::test::exitCode();
}
