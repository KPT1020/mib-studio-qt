// Fault-injection regression test: HDF5 readers must reject datasets whose
// rank differs from what the layout promises instead of letting
// H5Sget_simple_extent_dims write `rank` values into a smaller fixed-size
// stack array (stack smash on foreign/corrupt files).
//
// Before the rank guards in Hdf5Service.cpp, a file with a rank-5 image
// dataset or a rank-2 metadata dataset crashed (or silently corrupted the
// stack of) every reader below. Each case must now return false cleanly.

#include "backend/recording/Hdf5Service.h"

#include <hdf5.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("PASS: %s\n", what);
    } else {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Creates a dataset of the given rank at `path`, with tiny extents, filled
// with zeros. Type is uint8 for image-like paths, int for metadata-like ones
// (the rank guard must fire before any type conversion is attempted).
void createDataset(hid_t fileId, const char* path, int rank, hid_t type) {
    std::vector<hsize_t> dims(static_cast<size_t>(rank), 2);
    hid_t space = H5Screate_simple(rank, dims.data(), nullptr);
    hid_t dset = H5Dcreate2(fileId, path, type, space,
                            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dclose(dset);
    H5Sclose(space);
}

} // namespace

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "mib_foreign_rank_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "foreign.h5").string();

    // Build a hostile file: every dataset the readers know, at the wrong rank.
    {
        hid_t fileId = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
        if (fileId < 0) {
            std::printf("FAIL: could not create %s\n", path.c_str());
            return 1;
        }
        hid_t validGroup = H5Gcreate2(fileId, "/valid_frames", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        hid_t recGroup = H5Gcreate2(fileId, "/recorded_frames", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        createDataset(fileId, "/valid_frames/images", 5, H5T_NATIVE_UINT8);        // readers expect 3-4
        createDataset(fileId, "/valid_frames/masks", 5, H5T_NATIVE_UINT8);
        createDataset(fileId, "/valid_frames/metadata", 2, H5T_NATIVE_INT);        // expect 1
        createDataset(fileId, "/valid_frames/series_images", 2, H5T_NATIVE_UINT8); // expect 4
        createDataset(fileId, "/recorded_frames/metadata", 2, H5T_NATIVE_INT);     // expect 1
        createDataset(fileId, "/rank5_probe", 5, H5T_NATIVE_UINT8);
        H5Gclose(validGroup);
        H5Gclose(recGroup);
        H5Fclose(fileId);
    }

    backend::services::Hdf5Service svc;
    check(svc.loadFile(path), "loadFile accepts the file container itself");

    std::vector<backend::services::ProcessedFrame> frames;
    check(!svc.readValidMetadata(frames), "readValidMetadata rejects rank-2 metadata");
    frames.clear();
    check(!svc.readValidFrames(frames), "readValidFrames rejects wrong-rank datasets");
    frames.clear();
    check(!svc.readRecordingMetadata(frames), "readRecordingMetadata rejects rank-2 metadata");

    size_t count = 0;
    int h = 0, w = 0, c = 0;
    check(!svc.getDatasetInfo("/rank5_probe", count, h, w, c),
          "getDatasetInfo rejects rank-5 dataset");

    cv::Mat img;
    check(!svc.readImageByIndex("/rank5_probe", 0, img),
          "readImageByIndex rejects rank-5 dataset");

    std::vector<cv::Mat> series;
    check(!svc.readSeriesImagesByIndex(0, series),
          "readSeriesImagesByIndex rejects rank-2 series dataset");

    svc.closeFile();
    std::filesystem::remove_all(dir);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("hdf5_foreign_rank_test: all cases passed\n");
    return 0;
}
