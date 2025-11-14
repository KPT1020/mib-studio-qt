#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/Logger.h"

#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// HDF5 C API (more widely available than C++ API)
#include <hdf5.h>
#include <hdf5_hl.h>

#include <vector>
#include <cstring>
#include <stdexcept>

namespace backend::services
{

    struct Hdf5Service::Impl
    {
        hid_t fileId_{H5I_INVALID_HID};
        std::string filePath_;
        bool isOpen_{false};
        bool datasetsInitialized_{false};
        hsize_t validFramesWritten_{0};
        hsize_t invalidFramesWritten_{0};

        ~Impl()
        {
            if (fileId_ != H5I_INVALID_HID)
            {
                H5Fclose(fileId_);
                fileId_ = H5I_INVALID_HID;
            }
        }
    };

    Hdf5Service::Hdf5Service() : impl_(std::make_unique<Impl>())
    {
    }

    Hdf5Service::~Hdf5Service()
    {
        closeFile();
    }

    bool Hdf5Service::initialize(const std::string &rootDir)
    {
        SPDLOG_INFO("Hdf5Service initialized at {}", rootDir);
        return true;
    }

    bool Hdf5Service::openFile(const std::string &filePath)
    {
        if (impl_->isOpen_)
        {
            SPDLOG_WARN("HDF5 file already open: {}", impl_->filePath_);
            return false;
        }

        // Create file, overwriting if it exists
        impl_->fileId_ = H5Fcreate(filePath.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
        if (impl_->fileId_ < 0)
        {
            SPDLOG_ERROR("Failed to open HDF5 file: {}", filePath);
            return false;
        }

        impl_->filePath_ = filePath;
        impl_->isOpen_ = true;
        impl_->datasetsInitialized_ = false;
        impl_->validFramesWritten_ = 0;
        impl_->invalidFramesWritten_ = 0;
        SPDLOG_INFO("HDF5 file opened: {}", filePath);
        return true;
    }

    void Hdf5Service::closeFile()
    {
        if (impl_->fileId_ != H5I_INVALID_HID && impl_->isOpen_)
        {
            herr_t status = H5Fclose(impl_->fileId_);
            if (status < 0)
            {
                SPDLOG_ERROR("Error closing HDF5 file");
            }
            else
            {
                SPDLOG_INFO("HDF5 file closed: {}", impl_->filePath_);
            }
            impl_->fileId_ = H5I_INVALID_HID;
            impl_->isOpen_ = false;
        }
    }

    bool Hdf5Service::isFileOpen() const
    {
        return impl_->isOpen_ && impl_->fileId_ != H5I_INVALID_HID;
    }

    static bool writeImageDataset(hid_t fileId, const std::string &datasetPath,
                                  const std::vector<cv::Mat> &images)
    {
        if (images.empty())
            return true;

        // Get dimensions from first image
        const cv::Mat &firstImg = images[0];
        int height = firstImg.rows;
        int width = firstImg.cols;
        int channels = firstImg.channels();

        // Create dataspace
        hsize_t dims[4];
        int ndims;
        if (channels == 1)
        {
            ndims = 3;
            dims[0] = images.size();
            dims[1] = static_cast<hsize_t>(height);
            dims[2] = static_cast<hsize_t>(width);
        }
        else
        {
            ndims = 4;
            dims[0] = images.size();
            dims[1] = static_cast<hsize_t>(height);
            dims[2] = static_cast<hsize_t>(width);
            dims[3] = static_cast<hsize_t>(channels);
        }

        // Create dataspace with unlimited first dimension for extensibility
        hsize_t maxDims[4];
        if (channels == 1)
        {
            maxDims[0] = H5S_UNLIMITED;
            maxDims[1] = static_cast<hsize_t>(height);
            maxDims[2] = static_cast<hsize_t>(width);
        }
        else
        {
            maxDims[0] = H5S_UNLIMITED;
            maxDims[1] = static_cast<hsize_t>(height);
            maxDims[2] = static_cast<hsize_t>(width);
            maxDims[3] = static_cast<hsize_t>(channels);
        }

        hid_t dataspaceId = H5Screate_simple(ndims, dims, maxDims);
        if (dataspaceId < 0)
        {
            SPDLOG_ERROR("Failed to create dataspace for {}", datasetPath);
            return false;
        }

        // Create chunked dataset property for extensibility
        hid_t propId = H5Pcreate(H5P_DATASET_CREATE);
        hsize_t chunkDims[4];
        if (channels == 1)
        {
            chunkDims[0] = std::min(static_cast<hsize_t>(100), dims[0]); // Chunk size of 100 frames or less
            chunkDims[1] = dims[1];
            chunkDims[2] = dims[2];
            H5Pset_chunk(propId, 3, chunkDims);
        }
        else
        {
            chunkDims[0] = std::min(static_cast<hsize_t>(100), dims[0]);
            chunkDims[1] = dims[1];
            chunkDims[2] = dims[2];
            chunkDims[3] = dims[3];
            H5Pset_chunk(propId, 4, chunkDims);
        }

        // Create dataset with chunked property
        hid_t datasetId = H5Dcreate2(fileId, datasetPath.c_str(), H5T_NATIVE_UINT8, dataspaceId,
                                     H5P_DEFAULT, propId, H5P_DEFAULT);
        H5Pclose(propId);
        if (datasetId < 0)
        {
            H5Sclose(dataspaceId);
            SPDLOG_ERROR("Failed to create dataset {}", datasetPath);
            return false;
        }

        // Prepare data buffer
        size_t frameSize = height * width * channels;
        std::vector<uint8_t> buffer(images.size() * frameSize);
        size_t offset = 0;
        for (const auto &img : images)
        {
            if (img.rows != height || img.cols != width || img.channels() != channels)
            {
                SPDLOG_WARN("Image size mismatch, skipping");
                continue;
            }
            if (img.isContinuous())
            {
                std::memcpy(buffer.data() + offset, img.data, img.total() * img.elemSize());
                offset += img.total() * img.elemSize();
            }
            else
            {
                for (int r = 0; r < img.rows; r++)
                {
                    std::memcpy(buffer.data() + offset, img.ptr(r), img.cols * img.elemSize());
                    offset += img.cols * img.elemSize();
                }
            }
        }

        // Write data
        herr_t status = H5Dwrite(datasetId, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data());
        if (status < 0)
        {
            SPDLOG_ERROR("Failed to write image dataset {}", datasetPath);
            H5Dclose(datasetId);
            H5Sclose(dataspaceId);
            return false;
        }

        H5Dclose(datasetId);
        H5Sclose(dataspaceId);
        SPDLOG_DEBUG("Wrote {} images to {} ({}x{}x{})", images.size(), datasetPath, height, width, channels);
        return true;
    }

    static bool writeMetadataDataset(hid_t fileId, const std::string &datasetPath,
                                     const std::vector<ProcessedFrame> &frames)
    {
        if (frames.empty())
            return true;

        // Create compound datatype for metadata
        struct FrameMetadata
        {
            uint64_t index;
            uint64_t timestampNs;
            double deformability;
            double area;
            double areaRatio;
            double ringRatio;
            uint8_t isValid;
            uint8_t touchesBorder;
            uint8_t hasSingleInnerContour;
            uint8_t inRange;
            int32_t innerContourCount;
            double brightness_q1;
            double brightness_q2;
            double brightness_q3;
            double brightness_q4;
        };

        // Create compound type
        hid_t compTypeId = H5Tcreate(H5T_COMPOUND, sizeof(FrameMetadata));
        H5Tinsert(compTypeId, "index", HOFFSET(FrameMetadata, index), H5T_NATIVE_UINT64);
        H5Tinsert(compTypeId, "timestampNs", HOFFSET(FrameMetadata, timestampNs), H5T_NATIVE_UINT64);
        H5Tinsert(compTypeId, "deformability", HOFFSET(FrameMetadata, deformability), H5T_NATIVE_DOUBLE);
        H5Tinsert(compTypeId, "area", HOFFSET(FrameMetadata, area), H5T_NATIVE_DOUBLE);
        H5Tinsert(compTypeId, "areaRatio", HOFFSET(FrameMetadata, areaRatio), H5T_NATIVE_DOUBLE);
        H5Tinsert(compTypeId, "ringRatio", HOFFSET(FrameMetadata, ringRatio), H5T_NATIVE_DOUBLE);
        H5Tinsert(compTypeId, "isValid", HOFFSET(FrameMetadata, isValid), H5T_NATIVE_UINT8);
        H5Tinsert(compTypeId, "touchesBorder", HOFFSET(FrameMetadata, touchesBorder), H5T_NATIVE_UINT8);
        H5Tinsert(compTypeId, "hasSingleInnerContour", HOFFSET(FrameMetadata, hasSingleInnerContour), H5T_NATIVE_UINT8);
        H5Tinsert(compTypeId, "inRange", HOFFSET(FrameMetadata, inRange), H5T_NATIVE_UINT8);
        H5Tinsert(compTypeId, "innerContourCount", HOFFSET(FrameMetadata, innerContourCount), H5T_NATIVE_INT32);
        H5Tinsert(compTypeId, "brightness_q1", HOFFSET(FrameMetadata, brightness_q1), H5T_NATIVE_DOUBLE);
        H5Tinsert(compTypeId, "brightness_q2", HOFFSET(FrameMetadata, brightness_q2), H5T_NATIVE_DOUBLE);
        H5Tinsert(compTypeId, "brightness_q3", HOFFSET(FrameMetadata, brightness_q3), H5T_NATIVE_DOUBLE);
        H5Tinsert(compTypeId, "brightness_q4", HOFFSET(FrameMetadata, brightness_q4), H5T_NATIVE_DOUBLE);

        // Create dataspace with unlimited first dimension for extensibility
        hsize_t dims[1] = {frames.size()};
        hsize_t maxDims[1] = {H5S_UNLIMITED};
        hid_t dataspaceId = H5Screate_simple(1, dims, maxDims);
        if (dataspaceId < 0)
        {
            H5Tclose(compTypeId);
            SPDLOG_ERROR("Failed to create dataspace for metadata");
            return false;
        }

        // Create chunked dataset property for extensibility
        hid_t propId = H5Pcreate(H5P_DATASET_CREATE);
        hsize_t chunkDims[1] = {std::min(static_cast<hsize_t>(1000), dims[0])}; // Chunk size of 1000 entries or less
        H5Pset_chunk(propId, 1, chunkDims);

        // Create dataset with chunked property
        hid_t datasetId = H5Dcreate2(fileId, datasetPath.c_str(), compTypeId, dataspaceId,
                                     H5P_DEFAULT, propId, H5P_DEFAULT);
        H5Pclose(propId);
        if (datasetId < 0)
        {
            H5Sclose(dataspaceId);
            H5Tclose(compTypeId);
            SPDLOG_ERROR("Failed to create metadata dataset {}", datasetPath);
            return false;
        }

        // Prepare data
        std::vector<FrameMetadata> metadata;
        metadata.reserve(frames.size());
        for (const auto &frame : frames)
        {
            FrameMetadata md{};
            md.index = frame.index;
            md.timestampNs = frame.timestampNs;
            md.deformability = frame.validation.deformability;
            md.area = frame.validation.area;
            md.areaRatio = frame.validation.areaRatio;
            md.ringRatio = frame.validation.ringRatio;
            md.isValid = frame.validation.isValid ? 1 : 0;
            md.touchesBorder = frame.validation.touchesBorder ? 1 : 0;
            md.hasSingleInnerContour = frame.validation.hasSingleInnerContour ? 1 : 0;
            md.inRange = frame.validation.inRange ? 1 : 0;
            md.innerContourCount = frame.validation.innerContourCount;
            md.brightness_q1 = frame.validation.brightness.q1;
            md.brightness_q2 = frame.validation.brightness.q2;
            md.brightness_q3 = frame.validation.brightness.q3;
            md.brightness_q4 = frame.validation.brightness.q4;
            metadata.push_back(md);
        }

        // Write data
        herr_t status = H5Dwrite(datasetId, compTypeId, H5S_ALL, H5S_ALL, H5P_DEFAULT, metadata.data());
        if (status < 0)
        {
            SPDLOG_ERROR("Failed to write metadata dataset {}", datasetPath);
            H5Dclose(datasetId);
            H5Sclose(dataspaceId);
            H5Tclose(compTypeId);
            return false;
        }

        H5Dclose(datasetId);
        H5Sclose(dataspaceId);
        H5Tclose(compTypeId);
        SPDLOG_DEBUG("Wrote {} metadata entries to {}", frames.size(), datasetPath);
        return true;
    }

    bool Hdf5Service::saveFrames(const std::vector<ProcessedFrame> &validFrames,
                                 const std::vector<ProcessedFrame> &invalidFrames)
    {
        if (!isFileOpen())
        {
            SPDLOG_ERROR("HDF5 file is not open");
            return false;
        }

        // Create groups
        hid_t validGroupId = H5Gcreate2(impl_->fileId_, "/valid_frames", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (validGroupId >= 0)
            H5Gclose(validGroupId);

        hid_t invalidGroupId = H5Gcreate2(impl_->fileId_, "/invalid_frames", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (invalidGroupId >= 0)
            H5Gclose(invalidGroupId);

        // Write valid frames
        if (!validFrames.empty())
        {
            std::vector<cv::Mat> validImages, validMasks;
            for (const auto &frame : validFrames)
            {
                validImages.push_back(frame.originalImage);
                validMasks.push_back(frame.processedImage);
            }
            if (!writeImageDataset(impl_->fileId_, "/valid_frames/images", validImages))
                return false;
            if (!writeImageDataset(impl_->fileId_, "/valid_frames/masks", validMasks))
                return false;
            if (!writeMetadataDataset(impl_->fileId_, "/valid_frames/metadata", validFrames))
                return false;
        }

        // Write invalid frames
        if (!invalidFrames.empty())
        {
            std::vector<cv::Mat> invalidImages, invalidMasks;
            for (const auto &frame : invalidFrames)
            {
                invalidImages.push_back(frame.originalImage);
                invalidMasks.push_back(frame.processedImage);
            }
            if (!writeImageDataset(impl_->fileId_, "/invalid_frames/images", invalidImages))
                return false;
            if (!writeImageDataset(impl_->fileId_, "/invalid_frames/masks", invalidMasks))
                return false;
            if (!writeMetadataDataset(impl_->fileId_, "/invalid_frames/metadata", invalidFrames))
                return false;
        }

        SPDLOG_INFO("Saved {} valid frames and {} invalid frames to HDF5",
                    validFrames.size(), invalidFrames.size());
        return true;
    }

    // Helper function to append images to an existing dataset
    static bool appendImageDataset(hid_t fileId, const std::string &datasetPath,
                                   const std::vector<cv::Mat> &images, hsize_t &currentSize)
    {
        if (images.empty())
            return true;

        // Get dimensions from first image
        const cv::Mat &firstImg = images[0];
        int height = firstImg.rows;
        int width = firstImg.cols;
        int channels = firstImg.channels();

        // Open existing dataset
        hid_t datasetId = H5Dopen2(fileId, datasetPath.c_str(), H5P_DEFAULT);
        if (datasetId < 0)
        {
            SPDLOG_ERROR("Failed to open dataset {} for appending", datasetPath);
            return false;
        }

        // Get current dataspace and dimensions
        hid_t filespaceId = H5Dget_space(datasetId);
        int ndims = H5Sget_simple_extent_ndims(filespaceId);
        hsize_t currentDims[4];
        H5Sget_simple_extent_dims(filespaceId, currentDims, nullptr);
        H5Sclose(filespaceId);

        // Extend dataset once for the whole batch
        hsize_t newDims[4];
        if (channels == 1)
        {
            newDims[0] = currentDims[0] + images.size();
            newDims[1] = currentDims[1];
            newDims[2] = currentDims[2];
        }
        else
        {
            newDims[0] = currentDims[0] + images.size();
            newDims[1] = currentDims[1];
            newDims[2] = currentDims[2];
            newDims[3] = currentDims[3];
        }
        herr_t status = H5Dset_extent(datasetId, newDims);
        if (status < 0)
        {
            H5Dclose(datasetId);
            SPDLOG_ERROR("Failed to extend dataset {}", datasetPath);
            return false;
        }

        // Write each frame as its own hyperslab to avoid assembling a large contiguous buffer
        const size_t imageSizeBytes = static_cast<size_t>(height) * width * channels;
        std::vector<uint8_t> scratch; // allocated only if needed

        for (size_t i = 0; i < images.size(); ++i)
        {
            const cv::Mat &img = images[i];
            if (img.rows != height || img.cols != width || img.channels() != channels)
            {
                SPDLOG_ERROR("Image dimensions mismatch in appendImageDataset");
                H5Dclose(datasetId);
                return false;
            }

            filespaceId = H5Dget_space(datasetId);
            hsize_t start[4] = {currentDims[0] + static_cast<hsize_t>(i), 0, 0, 0};
            hsize_t count[4];
            if (channels == 1)
            {
                count[0] = 1;
                count[1] = static_cast<hsize_t>(height);
                count[2] = static_cast<hsize_t>(width);
            }
            else
            {
                count[0] = 1;
                count[1] = static_cast<hsize_t>(height);
                count[2] = static_cast<hsize_t>(width);
                count[3] = static_cast<hsize_t>(channels);
            }
            H5Sselect_hyperslab(filespaceId, H5S_SELECT_SET, start, nullptr, count, nullptr);

            hid_t memspaceId = H5Screate_simple(channels == 1 ? 3 : 4, count, nullptr);

            const void *srcPtr = nullptr;
            if (img.isContinuous())
            {
                srcPtr = img.data;
            }
            else
            {
                if (scratch.size() < imageSizeBytes) scratch.resize(imageSizeBytes);
                size_t off = 0;
                const size_t rowBytes = static_cast<size_t>(img.cols) * img.elemSize();
                for (int r = 0; r < img.rows; ++r)
                {
                    std::memcpy(scratch.data() + off, img.ptr(r), rowBytes);
                    off += rowBytes;
                }
                srcPtr = scratch.data();
            }

            status = H5Dwrite(datasetId, H5T_NATIVE_UINT8, memspaceId, filespaceId, H5P_DEFAULT, srcPtr);
            H5Sclose(memspaceId);
            H5Sclose(filespaceId);
            if (status < 0)
            {
                SPDLOG_ERROR("Failed to append frame {} to dataset {}", i, datasetPath);
                H5Dclose(datasetId);
                return false;
            }
        }

        H5Dclose(datasetId);

        // Update tracked size to match actual dataset extent after successful write
        currentSize = currentDims[0] + images.size();
        return true;
    }

    // Helper function to append metadata to an existing dataset
    static bool appendMetadataDataset(hid_t fileId, const std::string &datasetPath,
                                      const std::vector<ProcessedFrame> &frames, hsize_t &currentSize)
    {
        if (frames.empty())
            return true;

        struct FrameMetadata
        {
            uint64_t index;
            uint64_t timestampNs;
            double deformability;
            double area;
            double areaRatio;
            double ringRatio;
            uint8_t isValid;
            uint8_t touchesBorder;
            uint8_t hasSingleInnerContour;
            uint8_t inRange;
            int32_t innerContourCount;
            double brightness_q1;
            double brightness_q2;
            double brightness_q3;
            double brightness_q4;
        };

        // Open existing dataset
        hid_t datasetId = H5Dopen2(fileId, datasetPath.c_str(), H5P_DEFAULT);
        if (datasetId < 0)
        {
            SPDLOG_ERROR("Failed to open metadata dataset {} for appending", datasetPath);
            return false;
        }

        // Get compound type
        hid_t compTypeId = H5Dget_type(datasetId);
        if (compTypeId < 0)
        {
            H5Dclose(datasetId);
            SPDLOG_ERROR("Failed to get compound type from dataset {}", datasetPath);
            return false;
        }

        // Extend dataset once for the whole batch
        hid_t filespaceId = H5Dget_space(datasetId);
        hsize_t currentDims[1];
        H5Sget_simple_extent_dims(filespaceId, currentDims, nullptr);
        H5Sclose(filespaceId);

        hsize_t newDims[1] = {currentDims[0] + frames.size()};
        herr_t status = H5Dset_extent(datasetId, newDims);
        if (status < 0)
        {
            H5Tclose(compTypeId);
            H5Dclose(datasetId);
            SPDLOG_ERROR("Failed to extend metadata dataset {}", datasetPath);
            return false;
        }

        // Write each metadata entry individually
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto &frame = frames[i];
            FrameMetadata md{};
            md.index = frame.index;
            md.timestampNs = frame.timestampNs;
            md.deformability = frame.validation.deformability;
            md.area = frame.validation.area;
            md.areaRatio = frame.validation.areaRatio;
            md.ringRatio = frame.validation.ringRatio;
            md.isValid = frame.validation.isValid ? 1 : 0;
            md.touchesBorder = frame.validation.touchesBorder ? 1 : 0;
            md.hasSingleInnerContour = frame.validation.hasSingleInnerContour ? 1 : 0;
            md.inRange = frame.validation.inRange ? 1 : 0;
            md.innerContourCount = frame.validation.innerContourCount;
            md.brightness_q1 = frame.validation.brightness.q1;
            md.brightness_q2 = frame.validation.brightness.q2;
            md.brightness_q3 = frame.validation.brightness.q3;
            md.brightness_q4 = frame.validation.brightness.q4;

            filespaceId = H5Dget_space(datasetId);
            hsize_t start[1] = {currentDims[0] + static_cast<hsize_t>(i)};
            hsize_t count[1] = {1};
            H5Sselect_hyperslab(filespaceId, H5S_SELECT_SET, start, nullptr, count, nullptr);
            hid_t memspaceId = H5Screate_simple(1, count, nullptr);

            status = H5Dwrite(datasetId, compTypeId, memspaceId, filespaceId, H5P_DEFAULT, &md);
            H5Sclose(memspaceId);
            H5Sclose(filespaceId);
            if (status < 0)
            {
                SPDLOG_ERROR("Failed to append metadata entry {} to dataset {}", i, datasetPath);
                H5Tclose(compTypeId);
                H5Dclose(datasetId);
                return false;
            }
        }

        H5Tclose(compTypeId);
        H5Dclose(datasetId);

        // Update tracked size to match actual dataset extent after successful write
        currentSize = currentDims[0] + frames.size();
        return true;
    }

    bool Hdf5Service::initializeDatasets()
    {
        if (!isFileOpen())
        {
            SPDLOG_ERROR("HDF5 file is not open");
            return false;
        }

        if (impl_->datasetsInitialized_)
        {
            return true; // Already initialized
        }

        // Create groups
        hid_t validGroupId = H5Gcreate2(impl_->fileId_, "/valid_frames", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (validGroupId >= 0)
            H5Gclose(validGroupId);

        hid_t invalidGroupId = H5Gcreate2(impl_->fileId_, "/invalid_frames", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (invalidGroupId >= 0)
            H5Gclose(invalidGroupId);

        impl_->datasetsInitialized_ = true;
        SPDLOG_DEBUG("HDF5 datasets initialized (will be created on first append)");
        return true;
    }

    bool Hdf5Service::appendFrames(const std::vector<ProcessedFrame> &validFrames,
                                   const std::vector<ProcessedFrame> &invalidFrames)
    {
        if (!isFileOpen())
        {
            SPDLOG_ERROR("HDF5 file is not open");
            return false;
        }

        // Initialize datasets if needed
        if (!impl_->datasetsInitialized_)
        {
            initializeDatasets();
        }

        // Append valid frames
        if (!validFrames.empty())
        {
            // Create datasets if they don't exist
            if (impl_->validFramesWritten_ == 0)
            {
                std::vector<cv::Mat> validImages, validMasks;
                for (const auto &frame : validFrames)
                {
                    validImages.push_back(frame.originalImage);
                    validMasks.push_back(frame.processedImage);
                }
                if (!writeImageDataset(impl_->fileId_, "/valid_frames/images", validImages))
                    return false;
                if (!writeImageDataset(impl_->fileId_, "/valid_frames/masks", validMasks))
                    return false;
                if (!writeMetadataDataset(impl_->fileId_, "/valid_frames/metadata", validFrames))
                    return false;
                impl_->validFramesWritten_ = validFrames.size();
            }
            else
            {
                // Append to existing datasets
                std::vector<cv::Mat> validImages, validMasks;
                for (const auto &frame : validFrames)
                {
                    validImages.push_back(frame.originalImage);
                    validMasks.push_back(frame.processedImage);
                }
                if (!appendImageDataset(impl_->fileId_, "/valid_frames/images", validImages, impl_->validFramesWritten_))
                    return false;
                if (!appendImageDataset(impl_->fileId_, "/valid_frames/masks", validMasks, impl_->validFramesWritten_))
                    return false;
                if (!appendMetadataDataset(impl_->fileId_, "/valid_frames/metadata", validFrames, impl_->validFramesWritten_))
                    return false;
            }
        }

        // Append invalid frames
        if (!invalidFrames.empty())
        {
            if (impl_->invalidFramesWritten_ == 0)
            {
                std::vector<cv::Mat> invalidImages, invalidMasks;
                for (const auto &frame : invalidFrames)
                {
                    invalidImages.push_back(frame.originalImage);
                    invalidMasks.push_back(frame.processedImage);
                }
                if (!writeImageDataset(impl_->fileId_, "/invalid_frames/images", invalidImages))
                    return false;
                if (!writeImageDataset(impl_->fileId_, "/invalid_frames/masks", invalidMasks))
                    return false;
                if (!writeMetadataDataset(impl_->fileId_, "/invalid_frames/metadata", invalidFrames))
                    return false;
                impl_->invalidFramesWritten_ = invalidFrames.size();
            }
            else
            {
                std::vector<cv::Mat> invalidImages, invalidMasks;
                for (const auto &frame : invalidFrames)
                {
                    invalidImages.push_back(frame.originalImage);
                    invalidMasks.push_back(frame.processedImage);
                }
                if (!appendImageDataset(impl_->fileId_, "/invalid_frames/images", invalidImages, impl_->invalidFramesWritten_))
                    return false;
                if (!appendImageDataset(impl_->fileId_, "/invalid_frames/masks", invalidMasks, impl_->invalidFramesWritten_))
                    return false;
                if (!appendMetadataDataset(impl_->fileId_, "/invalid_frames/metadata", invalidFrames, impl_->invalidFramesWritten_))
                    return false;
            }
        }

        return true;
    }

    bool Hdf5Service::writeExperimentInfo(uint64_t startTimeNs, uint64_t endTimeNs,
                                          size_t totalValidFrames, size_t totalInvalidFrames)
    {
        if (!isFileOpen())
        {
            SPDLOG_ERROR("HDF5 file is not open");
            return false;
        }

        // Create group
        hid_t infoGroupId = H5Gcreate2(impl_->fileId_, "/experiment_info", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (infoGroupId < 0)
        {
            SPDLOG_ERROR("Failed to create experiment_info group");
            return false;
        }

        // Create scalar dataspace for attributes
        hid_t scalarSpaceId = H5Screate(H5S_SCALAR);
        if (scalarSpaceId < 0)
        {
            H5Gclose(infoGroupId);
            SPDLOG_ERROR("Failed to create scalar dataspace");
            return false;
        }

        // Write attributes
        hid_t attr1 = H5Acreate2(infoGroupId, "start_time_ns", H5T_NATIVE_UINT64, scalarSpaceId,
                                 H5P_DEFAULT, H5P_DEFAULT);
        if (attr1 >= 0)
        {
            H5Awrite(attr1, H5T_NATIVE_UINT64, &startTimeNs);
            H5Aclose(attr1);
        }

        hid_t attr2 = H5Acreate2(infoGroupId, "end_time_ns", H5T_NATIVE_UINT64, scalarSpaceId,
                                 H5P_DEFAULT, H5P_DEFAULT);
        if (attr2 >= 0)
        {
            H5Awrite(attr2, H5T_NATIVE_UINT64, &endTimeNs);
            H5Aclose(attr2);
        }

        uint64_t validCount = totalValidFrames;
        hid_t attr3 = H5Acreate2(infoGroupId, "total_valid_frames", H5T_NATIVE_UINT64, scalarSpaceId,
                                 H5P_DEFAULT, H5P_DEFAULT);
        if (attr3 >= 0)
        {
            H5Awrite(attr3, H5T_NATIVE_UINT64, &validCount);
            H5Aclose(attr3);
        }

        uint64_t invalidCount = totalInvalidFrames;
        hid_t attr4 = H5Acreate2(infoGroupId, "total_invalid_frames", H5T_NATIVE_UINT64, scalarSpaceId,
                                 H5P_DEFAULT, H5P_DEFAULT);
        if (attr4 >= 0)
        {
            H5Awrite(attr4, H5T_NATIVE_UINT64, &invalidCount);
            H5Aclose(attr4);
        }

        H5Sclose(scalarSpaceId);
        H5Gclose(infoGroupId);
        SPDLOG_DEBUG("Wrote experiment info to HDF5");
        return true;
    }

    bool Hdf5Service::loadFile(const std::string& filePath)
    {
        if (impl_->isOpen_)
        {
            SPDLOG_WARN("HDF5 file already open: {}", impl_->filePath_);
            return false;
        }

        // Open existing file for reading
        impl_->fileId_ = H5Fopen(filePath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
        if (impl_->fileId_ < 0)
        {
            SPDLOG_ERROR("Failed to open HDF5 file for reading: {}", filePath);
            return false;
        }

        impl_->filePath_ = filePath;
        impl_->isOpen_ = true;
        SPDLOG_INFO("HDF5 file opened for reading: {}", filePath);
        return true;
    }

    static bool readImageDataset(hid_t fileId, const std::string& datasetPath,
                                 std::vector<cv::Mat>& images)
    {
        // Check if dataset exists
        htri_t exists = H5Lexists(fileId, datasetPath.c_str(), H5P_DEFAULT);
        if (exists <= 0)
        {
            SPDLOG_WARN("Dataset {} does not exist", datasetPath);
            return false;
        }

        // Open dataset
        hid_t datasetId = H5Dopen2(fileId, datasetPath.c_str(), H5P_DEFAULT);
        if (datasetId < 0)
        {
            SPDLOG_ERROR("Failed to open dataset {}", datasetPath);
            return false;
        }

        // Get dataspace and dimensions
        hid_t dataspaceId = H5Dget_space(datasetId);
        int ndims = H5Sget_simple_extent_ndims(dataspaceId);
        hsize_t dims[4];
        H5Sget_simple_extent_dims(dataspaceId, dims, nullptr);
        H5Sclose(dataspaceId);

        if (ndims < 3)
        {
            SPDLOG_ERROR("Invalid dataset dimensions: {}", ndims);
            H5Dclose(datasetId);
            return false;
        }

        hsize_t numFrames = dims[0];
        hsize_t height = dims[1];
        hsize_t width = dims[2];
        hsize_t channels = (ndims == 4) ? dims[3] : 1;

        // Read data
        size_t frameSize = static_cast<size_t>(height) * width * channels;
        std::vector<uint8_t> buffer(numFrames * frameSize);
        herr_t status = H5Dread(datasetId, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data());
        H5Dclose(datasetId);

        if (status < 0)
        {
            SPDLOG_ERROR("Failed to read dataset {}", datasetPath);
            return false;
        }

        // Convert to cv::Mat
        images.clear();
        images.reserve(numFrames);
        for (hsize_t i = 0; i < numFrames; ++i)
        {
            size_t offset = i * frameSize;
            cv::Mat img;
            if (channels == 1)
            {
                img = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1,
                             buffer.data() + offset);
            }
            else
            {
                img = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC(channels),
                             buffer.data() + offset);
            }
            images.push_back(img.clone()); // Clone to ensure data ownership
        }

        SPDLOG_DEBUG("Read {} images from {} ({}x{}x{})", numFrames, datasetPath, height, width, channels);
        return true;
    }

    static bool readMetadataDataset(hid_t fileId, const std::string& datasetPath,
                                    std::vector<ProcessedFrame>& frames)
    {
        // Check if dataset exists
        htri_t exists = H5Lexists(fileId, datasetPath.c_str(), H5P_DEFAULT);
        if (exists <= 0)
        {
            SPDLOG_WARN("Dataset {} does not exist", datasetPath);
            return false;
        }

        // Open dataset
        hid_t datasetId = H5Dopen2(fileId, datasetPath.c_str(), H5P_DEFAULT);
        if (datasetId < 0)
        {
            SPDLOG_ERROR("Failed to open metadata dataset {}", datasetPath);
            return false;
        }

        // Get compound type
        hid_t compTypeId = H5Dget_type(datasetId);
        if (compTypeId < 0)
        {
            H5Dclose(datasetId);
            SPDLOG_ERROR("Failed to get compound type from dataset {}", datasetPath);
            return false;
        }

        // Get dataspace and dimensions
        hid_t dataspaceId = H5Dget_space(datasetId);
        hsize_t dims[1];
        H5Sget_simple_extent_dims(dataspaceId, dims, nullptr);
        H5Sclose(dataspaceId);

        hsize_t numFrames = dims[0];
        if (numFrames == 0)
        {
            H5Tclose(compTypeId);
            H5Dclose(datasetId);
            frames.clear();
            return true;
        }

        // Define FrameMetadata structure matching write structure
        struct FrameMetadata
        {
            uint64_t index;
            uint64_t timestampNs;
            double deformability;
            double area;
            double areaRatio;
            double ringRatio;
            uint8_t isValid;
            uint8_t touchesBorder;
            uint8_t hasSingleInnerContour;
            uint8_t inRange;
            int32_t innerContourCount;
            double brightness_q1;
            double brightness_q2;
            double brightness_q3;
            double brightness_q4;
        };

        // Read metadata
        std::vector<FrameMetadata> metadata(numFrames);
        herr_t status = H5Dread(datasetId, compTypeId, H5S_ALL, H5S_ALL, H5P_DEFAULT, metadata.data());
        H5Tclose(compTypeId);
        H5Dclose(datasetId);

        if (status < 0)
        {
            SPDLOG_ERROR("Failed to read metadata dataset {}", datasetPath);
            return false;
        }

        // Convert to ProcessedFrame (images will be filled separately)
        frames.clear();
        frames.reserve(numFrames);
        for (const auto& md : metadata)
        {
            ProcessedFrame frame;
            frame.index = md.index;
            frame.timestampNs = md.timestampNs;
            frame.validation.deformability = md.deformability;
            frame.validation.area = md.area;
            frame.validation.areaRatio = md.areaRatio;
            frame.validation.ringRatio = md.ringRatio;
            frame.validation.isValid = (md.isValid != 0);
            frame.validation.touchesBorder = (md.touchesBorder != 0);
            frame.validation.hasSingleInnerContour = (md.hasSingleInnerContour != 0);
            frame.validation.inRange = (md.inRange != 0);
            frame.validation.innerContourCount = md.innerContourCount;
            frame.validation.brightness.q1 = md.brightness_q1;
            frame.validation.brightness.q2 = md.brightness_q2;
            frame.validation.brightness.q3 = md.brightness_q3;
            frame.validation.brightness.q4 = md.brightness_q4;
            frames.push_back(frame);
        }

        SPDLOG_DEBUG("Read {} metadata entries from {}", numFrames, datasetPath);
        return true;
    }

    bool Hdf5Service::readValidFrames(std::vector<ProcessedFrame>& frames)
    {
        if (!isFileOpen())
        {
            SPDLOG_ERROR("HDF5 file is not open");
            return false;
        }

        // Read metadata first
        if (!readMetadataDataset(impl_->fileId_, "/valid_frames/metadata", frames))
        {
            return false;
        }

        // Read images
        std::vector<cv::Mat> images;
        if (!readImageDataset(impl_->fileId_, "/valid_frames/images", images))
        {
            frames.clear();
            return false;
        }

        // Read masks
        std::vector<cv::Mat> masks;
        if (!readImageDataset(impl_->fileId_, "/valid_frames/masks", masks))
        {
            frames.clear();
            return false;
        }

        // Combine images and masks with metadata
        if (frames.size() != images.size() || frames.size() != masks.size())
        {
            SPDLOG_ERROR("Mismatch in frame counts: metadata={}, images={}, masks={}",
                        frames.size(), images.size(), masks.size());
            frames.clear();
            return false;
        }

        for (size_t i = 0; i < frames.size(); ++i)
        {
            frames[i].originalImage = images[i];
            frames[i].processedImage = masks[i];
        }

        SPDLOG_INFO("Read {} valid frames from HDF5", frames.size());
        return true;
    }

    bool Hdf5Service::readInvalidFrames(std::vector<ProcessedFrame>& frames)
    {
        if (!isFileOpen())
        {
            SPDLOG_ERROR("HDF5 file is not open");
            return false;
        }

        // Read metadata first
        if (!readMetadataDataset(impl_->fileId_, "/invalid_frames/metadata", frames))
        {
            return false;
        }

        // Read images
        std::vector<cv::Mat> images;
        if (!readImageDataset(impl_->fileId_, "/invalid_frames/images", images))
        {
            frames.clear();
            return false;
        }

        // Read masks
        std::vector<cv::Mat> masks;
        if (!readImageDataset(impl_->fileId_, "/invalid_frames/masks", masks))
        {
            frames.clear();
            return false;
        }

        // Combine images and masks with metadata
        if (frames.size() != images.size() || frames.size() != masks.size())
        {
            SPDLOG_ERROR("Mismatch in frame counts: metadata={}, images={}, masks={}",
                        frames.size(), images.size(), masks.size());
            frames.clear();
            return false;
        }

        for (size_t i = 0; i < frames.size(); ++i)
        {
            frames[i].originalImage = images[i];
            frames[i].processedImage = masks[i];
        }

        SPDLOG_INFO("Read {} invalid frames from HDF5", frames.size());
        return true;
    }

    bool Hdf5Service::readExperimentInfo(uint64_t& startTimeNs, uint64_t& endTimeNs,
                                         size_t& totalValidFrames, size_t& totalInvalidFrames)
    {
        if (!isFileOpen())
        {
            SPDLOG_ERROR("HDF5 file is not open");
            return false;
        }

        // Check if group exists
        htri_t exists = H5Lexists(impl_->fileId_, "/experiment_info", H5P_DEFAULT);
        if (exists <= 0)
        {
            SPDLOG_WARN("Experiment info group does not exist");
            return false;
        }

        // Open group
        hid_t infoGroupId = H5Gopen2(impl_->fileId_, "/experiment_info", H5P_DEFAULT);
        if (infoGroupId < 0)
        {
            SPDLOG_ERROR("Failed to open experiment_info group");
            return false;
        }

        // Read attributes
        bool success = true;
        if (H5Aexists(infoGroupId, "start_time_ns") > 0)
        {
            hid_t attr = H5Aopen(infoGroupId, "start_time_ns", H5P_DEFAULT);
            if (attr >= 0)
            {
                H5Aread(attr, H5T_NATIVE_UINT64, &startTimeNs);
                H5Aclose(attr);
            }
            else
            {
                success = false;
            }
        }

        if (H5Aexists(infoGroupId, "end_time_ns") > 0)
        {
            hid_t attr = H5Aopen(infoGroupId, "end_time_ns", H5P_DEFAULT);
            if (attr >= 0)
            {
                H5Aread(attr, H5T_NATIVE_UINT64, &endTimeNs);
                H5Aclose(attr);
            }
            else
            {
                success = false;
            }
        }

        uint64_t validCount = 0;
        if (H5Aexists(infoGroupId, "total_valid_frames") > 0)
        {
            hid_t attr = H5Aopen(infoGroupId, "total_valid_frames", H5P_DEFAULT);
            if (attr >= 0)
            {
                H5Aread(attr, H5T_NATIVE_UINT64, &validCount);
                H5Aclose(attr);
            }
            else
            {
                success = false;
            }
        }
        totalValidFrames = static_cast<size_t>(validCount);

        uint64_t invalidCount = 0;
        if (H5Aexists(infoGroupId, "total_invalid_frames") > 0)
        {
            hid_t attr = H5Aopen(infoGroupId, "total_invalid_frames", H5P_DEFAULT);
            if (attr >= 0)
            {
                H5Aread(attr, H5T_NATIVE_UINT64, &invalidCount);
                H5Aclose(attr);
            }
            else
            {
                success = false;
            }
        }
        totalInvalidFrames = static_cast<size_t>(invalidCount);

        H5Gclose(infoGroupId);

        if (success)
        {
            SPDLOG_DEBUG("Read experiment info: start={}, end={}, valid={}, invalid={}",
                        startTimeNs, endTimeNs, totalValidFrames, totalInvalidFrames);
        }
        return success;
    }

} // namespace backend::services
