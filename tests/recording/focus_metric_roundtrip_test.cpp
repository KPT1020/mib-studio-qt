// focus_metric_roundtrip_test
//
// Round-trip coverage for the focus metrics added to the processed-frame
// metadata schema (focusLaplacianVar / focusTenengrad). Writes frames carrying
// focus values through the append path, reloads, and verifies the values
// survive both readValidMetadata and readValidFrames. Guards against silent
// loss of the new fields and against a schema mismatch breaking the save path.

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

ProcessedFrame makeFrame(uint64_t idx, bool valid, double lapVar, double tenengrad)
{
    ProcessedFrame f;
    f.index = idx;
    f.timestampNs = (idx + 1) * 1000ULL;
    f.originalImage = cv::Mat(8, 10, CV_8UC1, cv::Scalar(50));
    f.processedImage = cv::Mat(8, 10, CV_8UC1, cv::Scalar(valid ? 255 : 0));
    f.validation.isValid = valid;
    f.validation.objectId = static_cast<int>(idx);
    f.validation.objectCount = 1;
    f.validation.focusLaplacianVar = lapVar;
    f.validation.focusTenengrad = tenengrad;
    return f;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

} // namespace

int main()
{
    mib::test::TempDir td("mib_focus_roundtrip");
    const std::string path = (td / "focus.h5").string();

    std::vector<ProcessedFrame> valid{makeFrame(0, true, 4700.5, 11324.9),
                                      makeFrame(1, true, 1112.25, 3050.0)};
    std::vector<ProcessedFrame> invalid{makeFrame(2, false, 0.0, 0.0)};

    {
        Hdf5Service hdf5;
        MIB_REQUIRE(hdf5.openFile(path), "openFile");
        MIB_REQUIRE(hdf5.initializeDatasets(), "initializeDatasets");
        MIB_REQUIRE(hdf5.appendFrames(valid, invalid), "appendFrames");
        ProcessingConfig cfg;
        ProcessingService::Roi roi{0, 0, 10, 8};
        MIB_REQUIRE(hdf5.writeExperimentInfo(1000, 4000, valid.size(), invalid.size(), cfg, roi),
                    "writeExperimentInfo");
        hdf5.closeFile();
    }

    {
        Hdf5Service r;
        MIB_REQUIRE(r.loadFile(path), "reload");

        std::vector<ProcessedFrame> meta;
        MIB_REQUIRE(r.readValidMetadata(meta), "readValidMetadata");
        MIB_EXPECT(meta.size() == 2, "valid metadata count");
        if (meta.size() == 2) {
            MIB_EXPECT(near(meta[0].validation.focusLaplacianVar, 4700.5),
                       "focusLaplacianVar[0] round-trips");
            MIB_EXPECT(near(meta[0].validation.focusTenengrad, 11324.9),
                       "focusTenengrad[0] round-trips");
            MIB_EXPECT(near(meta[1].validation.focusLaplacianVar, 1112.25),
                       "focusLaplacianVar[1] round-trips");
        }

        // Same values must also survive the full-frame read path.
        std::vector<ProcessedFrame> full;
        MIB_REQUIRE(r.readValidFrames(full), "readValidFrames");
        MIB_EXPECT(full.size() == 2, "valid frame count");
        if (full.size() == 2) {
            MIB_EXPECT(near(full[0].validation.focusLaplacianVar, 4700.5),
                       "focusLaplacianVar survives readValidFrames");
        }
        r.closeFile();
    }

    if (mib::test::exitCode() == 0) {
        std::printf("focus metrics round-trip through the HDF5 metadata schema\n");
    }
    return mib::test::exitCode();
}
