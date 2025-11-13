#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace cv {
    class Mat;
}

namespace backend::services {
    struct ProcessedFrame;
}

namespace backend::services {

class Hdf5Service {
public:
    Hdf5Service();
    ~Hdf5Service();

    bool initialize(const std::string& rootDir);
    
    // File operations
    bool openFile(const std::string& filePath);
    void closeFile();
    bool isFileOpen() const;
    
    // Frame saving (batch write - for final save or periodic flush)
    bool saveFrames(const std::vector<ProcessedFrame>& validFrames,
                    const std::vector<ProcessedFrame>& invalidFrames);
    
    // Incremental frame appending (for round-robin buffer resilience)
    bool initializeDatasets(); // Create datasets with unlimited dimensions
    bool appendFrames(const std::vector<ProcessedFrame>& validFrames,
                      const std::vector<ProcessedFrame>& invalidFrames);
    
    // Experiment metadata
    bool writeExperimentInfo(uint64_t startTimeNs, uint64_t endTimeNs,
                             size_t totalValidFrames, size_t totalInvalidFrames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace backend::services
