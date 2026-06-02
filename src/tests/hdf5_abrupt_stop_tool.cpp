#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;

constexpr const char *kMockDir = "/workspace/data/mock_frames";

std::string makeTempPath(const std::string &prefix)
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return (std::filesystem::temp_directory_path() /
            (prefix + "_" + std::to_string(dist(gen)) + ".h5"))
        .string();
}

void configureMockEnvironment()
{
    setenv("MIB_CAMERA_MODE", "mock", 1);
    setenv("MIB_MOCK_CAMERA_DIR", kMockDir, 1);
    setenv("MIB_MOCK_CAMERA_INTERVAL_MS", "2", 1);
    setenv("MIB_MOCK_CAMERA_LOOP", "true", 1);
}

void cleanPathArtifacts(const std::string &path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + ".recovery.h5", ec);
    std::filesystem::remove(path + ".recovery.h5.bak", ec);
    std::filesystem::remove(path + ".recovery.h5.tmp", ec);
    std::filesystem::remove(path + ".preview.png", ec);
}

bool isNonEmptyFile(const std::string &path)
{
    std::error_code ec;
    const auto p = std::filesystem::path(path);
    if (!std::filesystem::is_regular_file(p, ec) || ec)
    {
        return false;
    }
    const auto size = std::filesystem::file_size(p, ec);
    return !ec && size > 0;
}

bool startBackend(backend::AppBackend &backend, bool startRealtime)
{
    configureMockEnvironment();
    if (!backend.initialize("/workspace/data"))
    {
        SPDLOG_ERROR("AppBackend initialize failed");
        return false;
    }
    if (!backend.capture().start())
    {
        SPDLOG_ERROR("Capture start failed");
        return false;
    }
    if (startRealtime)
    {
        backend.processing().startRealtime(backend.getFrameStore());
    }
    return true;
}

std::vector<cv::Mat> loadMockReferenceFrames()
{
    std::vector<cv::Mat> refs;
    std::vector<std::filesystem::path> files;
    const std::filesystem::path dir(kMockDir);
    if (!std::filesystem::exists(dir))
    {
        return refs;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto ext = entry.path().extension().string();
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".tif" || lower == ".tiff")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto &f : files)
    {
        cv::Mat img = cv::imread(f.string(), cv::IMREAD_GRAYSCALE);
        if (!img.empty())
        {
            refs.push_back(img);
        }
    }
    return refs;
}

cv::Mat toGray8(const cv::Mat &src)
{
    cv::Mat gray;
    if (src.empty())
    {
        return gray;
    }
    if (src.channels() == 1)
    {
        if (src.type() == CV_8UC1)
        {
            gray = src;
        }
        else
        {
            src.convertTo(gray, CV_8UC1);
        }
    }
    else
    {
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    }
    return gray;
}

double bestMadToReferences(const cv::Mat &frameGray, const std::vector<cv::Mat> &refs)
{
    double bestMad = 1e12;
    for (const auto &refRaw : refs)
    {
        if (refRaw.empty())
        {
            continue;
        }
        cv::Mat ref = toGray8(refRaw);
        if (ref.empty())
        {
            continue;
        }

        cv::Mat refMatched;
        if (ref.size() == frameGray.size())
        {
            refMatched = ref;
        }
        else
        {
            cv::resize(ref, refMatched, frameGray.size(), 0.0, 0.0, cv::INTER_LINEAR);
        }

        cv::Mat diff;
        cv::absdiff(frameGray, refMatched, diff);
        const double mad = cv::mean(diff)[0];
        bestMad = std::min(bestMad, mad);
    }
    return bestMad;
}

bool writeContactSheet(const std::vector<cv::Mat> &frames, const std::string &outPath)
{
    if (frames.empty())
    {
        return false;
    }
    constexpr int cols = 4;
    const int rows = static_cast<int>((frames.size() + cols - 1) / cols);
    constexpr int tileW = 220;
    constexpr int tileH = 220;
    cv::Mat canvas(rows * tileH, cols * tileW, CV_8UC1, cv::Scalar(0));

    for (size_t i = 0; i < frames.size(); ++i)
    {
        cv::Mat gray = toGray8(frames[i]);
        if (gray.empty())
        {
            continue;
        }
        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(tileW, tileH), 0.0, 0.0, cv::INTER_AREA);
        const int r = static_cast<int>(i / cols);
        const int c = static_cast<int>(i % cols);
        resized.copyTo(canvas(cv::Rect(c * tileW, r * tileH, tileW, tileH)));
    }
    return cv::imwrite(outPath, canvas);
}

struct DatasetVisualCheckResult
{
    bool readable{false};
    bool visuallyConsistent{false};
    size_t sampledFrames{0};
    size_t matchedFrames{0};
    double worstMad{0.0};
    std::string previewPath;
};

DatasetVisualCheckResult validateDatasetVisuals(backend::services::Hdf5Service &hdf5,
                                                const std::string &datasetPath,
                                                const std::string &previewPath,
                                                const std::vector<cv::Mat> &refs)
{
    DatasetVisualCheckResult result;
    result.previewPath = previewPath;

    size_t count = 0;
    int h = 0;
    int w = 0;
    int c = 0;
    if (!hdf5.getDatasetInfo(datasetPath, count, h, w, c) || count == 0)
    {
        return result;
    }
    result.readable = true;

    const size_t sampleCount = std::min<size_t>(count, 16);
    std::vector<cv::Mat> sampled;
    sampled.reserve(sampleCount);

    for (size_t i = 0; i < sampleCount; ++i)
    {
        cv::Mat frame;
        if (!hdf5.readImageByIndex(datasetPath, i, frame))
        {
            continue;
        }
        cv::Mat gray = toGray8(frame);
        if (gray.empty())
        {
            continue;
        }
        sampled.push_back(gray);
    }

    result.sampledFrames = sampled.size();
    if (result.sampledFrames == 0)
    {
        return result;
    }

    bool wrotePreview = writeContactSheet(sampled, previewPath);
    if (!wrotePreview)
    {
        SPDLOG_WARN("Failed to write preview contact sheet: {}", previewPath);
    }

    double worstMad = 0.0;
    size_t matched = 0;
    for (const auto &gray : sampled)
    {
        cv::Scalar mean;
        cv::Scalar stddev;
        cv::meanStdDev(gray, mean, stddev);
        const bool nonFlat = stddev[0] > 1.0;
        if (!nonFlat)
        {
            continue;
        }
        const double mad = refs.empty() ? 0.0 : bestMadToReferences(gray, refs);
        worstMad = std::max(worstMad, mad);
        if (refs.empty() || mad < 5.0)
        {
            ++matched;
        }
    }

    result.matchedFrames = matched;
    result.worstMad = worstMad;
    const size_t minimumMatches = std::max<size_t>(1, (result.sampledFrames * 8) / 10);
    result.visuallyConsistent = matched >= minimumMatches;
    return result;
}

int runExperiment(const std::string &path)
{
    cleanPathArtifacts(path);
    backend::AppBackend backend;
    if (!startBackend(backend, true))
    {
        return 2;
    }

    auto &hdf5 = backend.hdf5();
    if (!hdf5.openFile(path))
    {
        SPDLOG_ERROR("openFile failed: {}", path);
        return 3;
    }
    if (!hdf5.initializeDatasets())
    {
        SPDLOG_ERROR("initializeDatasets failed");
        return 4;
    }

    backend.processing().setFlushInterval(5);
    backend.processing().setInvalidFrameSamplingRate(1);
    backend.processing().startExperiment();

    std::atomic<bool> keepFlushing{true};
    std::thread flusher([&]() {
        while (keepFlushing.load(std::memory_order_relaxed))
        {
            if (hdf5.isFileOpen())
            {
                backend.processing().flushBufferedFrames(hdf5);
            }
            std::this_thread::sleep_for(50ms);
        }
    });

    SPDLOG_INFO("Experiment writer running: {}", path);
    while (true)
    {
        std::this_thread::sleep_for(1s);
    }

    keepFlushing.store(false, std::memory_order_relaxed);
    flusher.join();
    return 0;
}

int runRecording(const std::string &path)
{
    cleanPathArtifacts(path);
    backend::AppBackend backend;
    if (!startBackend(backend, false))
    {
        return 2;
    }
    if (!backend.startFrameRecording(path))
    {
        SPDLOG_ERROR("startFrameRecording failed: {}", path);
        return 3;
    }

    SPDLOG_INFO("Recording writer running: {}", path);
    while (true)
    {
        std::this_thread::sleep_for(1s);
    }
    return 0;
}

int checkExperiment(const std::string &path)
{
    backend::services::Hdf5Service hdf5;
    hdf5.initialize("/tmp");
    if (!hdf5.loadFile(path))
    {
        SPDLOG_ERROR("Experiment check: loadFile failed for {}", path);
        return 1;
    }

    size_t count = 0;
    int h = 0;
    int w = 0;
    int c = 0;
    const bool validOk = hdf5.getDatasetInfo("/valid_frames/images", count, h, w, c);
    const size_t validCount = validOk ? count : 0;
    const bool invalidOk = hdf5.getDatasetInfo("/invalid_frames/images", count, h, w, c);
    const size_t invalidCount = invalidOk ? count : 0;

    const std::string datasetPath =
        validCount > 0 ? "/valid_frames/images" :
        invalidCount > 0 ? "/invalid_frames/images" :
        "";
    if (datasetPath.empty())
    {
        SPDLOG_ERROR("Experiment check: no image dataset found");
        hdf5.closeFile();
        return 2;
    }

    const auto refs = loadMockReferenceFrames();
    const auto visual = validateDatasetVisuals(hdf5, datasetPath, path + ".preview.png", refs);
    hdf5.closeFile();

    SPDLOG_INFO("Experiment check: valid={} invalid={} sampled={} matched={} worst_mad={:.3f} preview={}",
                validCount, invalidCount, visual.sampledFrames, visual.matchedFrames,
                visual.worstMad, visual.previewPath);
    if (!visual.readable || !visual.visuallyConsistent)
    {
        SPDLOG_ERROR("Experiment check failed: dataset unreadable or visually inconsistent");
        return 3;
    }
    return 0;
}

int checkRecording(const std::string &path)
{
    backend::services::Hdf5Service hdf5;
    hdf5.initialize("/tmp");
    if (!hdf5.loadFile(path))
    {
        SPDLOG_ERROR("Recording check: loadFile failed for {}", path);
        return 1;
    }

    size_t count = 0;
    int h = 0;
    int w = 0;
    int c = 0;
    const bool imagesOk = hdf5.getDatasetInfo("/recorded_frames/images", count, h, w, c);
    const size_t imageCount = imagesOk ? count : 0;
    std::vector<backend::services::ProcessedFrame> metadata;
    const bool metadataOk = hdf5.readRecordingMetadata(metadata);

    const auto refs = loadMockReferenceFrames();
    const auto visual = validateDatasetVisuals(hdf5, "/recorded_frames/images", path + ".preview.png", refs);
    hdf5.closeFile();

    SPDLOG_INFO("Recording check: images={} metadata={} sampled={} matched={} worst_mad={:.3f} preview={}",
                imageCount, metadata.size(), visual.sampledFrames, visual.matchedFrames,
                visual.worstMad, visual.previewPath);

    if (!imagesOk || imageCount == 0 || !metadataOk || metadata.empty())
    {
        SPDLOG_ERROR("Recording check failed: missing readable image/metadata datasets");
        return 2;
    }
    if (!visual.readable || !visual.visuallyConsistent)
    {
        SPDLOG_ERROR("Recording check failed: dataset unreadable or visually inconsistent");
        return 3;
    }
    return 0;
}

int checkCheckpoint(const std::string &path)
{
    const std::string recoveryPath = path + ".recovery.h5";
    if (!isNonEmptyFile(recoveryPath))
    {
        SPDLOG_ERROR("Recovery checkpoint missing or empty: {}", recoveryPath);
        return 1;
    }
    SPDLOG_INFO("Recovery checkpoint is non-empty: {}", recoveryPath);
    return 0;
}

int runMode(const std::string &mode, const std::string &path)
{
    if (mode == "run-experiment")
        return runExperiment(path);
    if (mode == "run-recording")
        return runRecording(path);
    if (mode == "check-experiment")
        return checkExperiment(path);
    if (mode == "check-recording")
        return checkRecording(path);
    if (mode == "check-checkpoint")
        return checkCheckpoint(path);

    SPDLOG_ERROR("Unknown mode: {}", mode);
    return 64;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        SPDLOG_ERROR("usage: hdf5_abrupt_stop_tool <mode> <h5-path>");
        SPDLOG_ERROR("modes: run-experiment | run-recording | check-experiment | check-recording | check-checkpoint");
        return 64;
    }
    return runMode(argv[1], argv[2]);
}
